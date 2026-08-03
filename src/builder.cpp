// builder.cpp —— 构建编排、渲染部件与站点生命周期命令（自 main.cpp 原样搬迁）

#include "builder.hpp"
#include "component.hpp"
#include "shortcode.hpp"
#include "versions.hpp"
#include "output.hpp"
#include "ctxdata.hpp"
#include "site_config.hpp"
#include "render_pages.hpp"
#include "frontmatter.hpp"
#include "i18n.hpp"
#include "pages.hpp"
#include "feeds.hpp"
#include "pwa.hpp"
#include "config.hpp"
#include "search.hpp"
#include "plugin.hpp"
#include "compress.hpp"
#include "linkcheck.hpp"
#include <algorithm>   // std::find（i18n 缺失键去重）
#include <set>         // 版本化导航过滤（当前版本已生成页面集合）
#include <thread>      // Hugo 式并发渲染：worker pool
#include <mutex>       // pageSig 并发写保护
#include <atomic>      // 任务计数器
#include <cstdio>
#include <regex>       // L2 残留检测：{{}} 模板块 / 数据键 / 组件标签正则扫描

// ---------------- 并发渲染 worker pool（Hugo 式：多核并行渲染页面） ----------------
// tasks 数量 < 2 或单核时退化为顺序执行；每个工作线程从原子计数器取任务。
void run_parallel(size_t n_tasks, const std::function<void(size_t)>& fn) {
    if (n_tasks < 2) { for (size_t i = 0; i < n_tasks; ++i) fn(i); return; }
    unsigned hw = std::thread::hardware_concurrency();
    if (hw <= 1)   { for (size_t i = 0; i < n_tasks; ++i) fn(i); return; }
    unsigned n = std::min<unsigned>(hw, (unsigned)n_tasks);
    std::atomic<size_t> next{0};
    std::vector<std::thread> pool;
    pool.reserve(n);
    for (unsigned t = 0; t < n; ++t)
        pool.emplace_back([&] {
            for (;;) {
                size_t i = next.fetch_add(1, std::memory_order_relaxed);
                if (i >= n_tasks) break;
                fn(i);
            }
        });
    for (auto& th : pool) th.join();
}

static std::string replace_all(std::string s, const std::string& from, const std::string& to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

// ---------------- 构建编排 ----------------

// ============ 构建上下文（run_build 各阶段共享的状态） ============
// 让 run_build 退化为纯编排：每个阶段函数只需一份上下文，不再传递超长参数列表。
// 阶段函数体用局部引用别名（如 SiteConfig& cfg = b.cfg;）绑定上下文成员，
// 因此函数体与重构前 run_build 内联时逐字一致，行为零变化。


// ---------------- 构建阶段（run_build 的拆分，file-local） ----------------

// 1) 载入站点配置：config.json + 侧边栏（sidebar/ 分文件 + site.sidebar 映射，未配置回退 route.json）
static int prepare_pages(BuildContext& b) {
    SiteConfig& cfg = b.cfg;
    std::vector<Page>& pages = b.pages;
    const bool& includeDrafts = b.includeDrafts;
    const fs::path& in_dir = b.in_dir;

    if (!fs::exists(in_dir) || !fs::is_directory(in_dir)) {
        std::cerr << color::error("输入目录不存在: ") << in_dir << "\n";
        return 1;
    }

    // 4) 收集页面：遍历 route.json 叶子节点（前序），即搜索数据来源
    collect_pages(cfg.nav, pages);
    // 兜底：route.json 为空时按 md/ 目录结构自动发现并生成导航
    // （子目录 → 分组，对标 Hugo/VitePress 的"目录即导航"；.md 自动路由到对应子路径）
    if (pages.empty()) {
        std::function<void(const fs::path&, NavNode&)> scan =
            [&](const fs::path& dir, NavNode& node) {
                std::vector<NavNode> files, subdirs;
                std::error_code sec;
                std::vector<fs::path> entries;
                for (auto it = fs::directory_iterator(dir, sec);
                     !sec && it != fs::directory_iterator(); it.increment(sec))
                    entries.push_back(it->path());
                for (const auto& ep : entries) {
                    std::error_code e2;
                    std::string fn = ep.filename().string();
                    if (!fn.empty() && fn[0] == '.') continue;
                    if (fs::is_directory(ep, e2)) {
                        if (fn == "blog") continue;   // 博客目录交给专门的博客流收集，不并入文档导航
                        NavNode grp; grp.title = fn;
                        scan(ep, grp);
                        if (!grp.children.empty()) subdirs.push_back(std::move(grp));
                    } else if (ep.extension() == ".md") {
                        std::string stem = ep.stem().string();
                        if (stem.find('.') != std::string::npos) continue;   // 跳过 x.en.md 变体
                        std::error_code r2;
                        fs::path rel = fs::relative(ep, in_dir, r2);
                        std::string f = rel.generic_string();
                        if (f.size() > 3) f = f.substr(0, f.size() - 3);     // 去 .md
                        NavNode leaf;
                        leaf.file  = f;      // 子目录页自动路由：guide/install → guide/install.html
                        leaf.title = stem;
                        files.push_back(std::move(leaf));
                    }
                }
                std::sort(subdirs.begin(), subdirs.end(),
                          [](const NavNode& a, const NavNode& b) { return a.title < b.title; });
                std::sort(files.begin(), files.end(),
                          [](const NavNode& a, const NavNode& b) { return a.title < b.title; });
                for (auto& g : subdirs) node.children.push_back(std::move(g));
                for (auto& f : files)   node.children.push_back(std::move(f));
            };
        NavNode root;
        scan(in_dir, root);
        cfg.nav = std::move(root.children);
        collect_pages(cfg.nav, pages);
        std::sort(pages.begin(), pages.end(),
                  [](const Page& a, const Page& b) {
                      if (a.weight != b.weight) return a.weight < b.weight;
                      return a.file < b.file;
                  });
    }

    if (pages.empty()) {
        std::cerr << color::error("没有可生成的文档（" + in_dir.string() + " 下无 .md，且 route.json 无叶子节点）\n");
        return 1;
    }

    // 4.5) 预扫描 front matter：读取每篇文档的 draft/weight/title/tags/date，
    //      用于草稿过滤、排序与元数据（自动发现模式已在上面一并处理，这里覆盖 route 模式）。
    {
        std::set<std::string> draftFiles;
        for (auto& pg : pages) {
            fs::path fp = in_dir / (pg.file + ".md");
            if (fs::exists(fp)) {
                std::string body;
                FrontMatter fm = parse_front_matter(read_file(fp), body);
                pg.draft  = fm.draft;
                pg.weight = fm.weight;
                pg.date   = fm.date;
                pg.tags   = fm.tags;
                if (fm.hasTitle) pg.title = fm.title;
                if (pg.draft) draftFiles.insert(pg.file);
            }
        }
        if (!includeDrafts) filter_draft_nav(cfg.nav, draftFiles);
        // 若有页面带 tags，在导航末尾追加“标签”入口，指向标签总览页
        // 标签/博客导航入口由 on_data_query 插件查询结果驱动（prepare_pages 末尾补加）
        // 博客流（约定优于配置）：md/blog/ 目录存在 → 收集为博客文章。
        // 与文档双区：blog/x.md（默认语言）+ blog/x.<loc>.md（其他语言），按 date 倒序；
        // 无 blog/ 目录 → 单文档站点，行为与旧版完全一致（零回归）。
        std::error_code sec2;
        // 博客全局共享：优先 in_dir/blog（单版本）；版本化时 in_dir 是 docs 子目录
        // （md/docs），博客区在上级根 md/blog → 一级回退，各版本共用同一博客
        fs::path blogDir = in_dir / "blog";
        if (!(fs::exists(blogDir, sec2) && fs::is_directory(blogDir, sec2))) {
            fs::path rootBlog = in_dir.parent_path() / "blog";
            if (fs::exists(rootBlog, sec2) && fs::is_directory(rootBlog, sec2))
                blogDir = rootBlog;
        }
        if (fs::exists(blogDir, sec2) && fs::is_directory(blogDir, sec2)) {
            for (const auto& e : fs::directory_iterator(blogDir, sec2)) {
                if (!e.is_regular_file(sec2) || e.path().extension() != ".md") continue;
                std::string stem = e.path().stem().string();
                if (stem.find('.') != std::string::npos) continue;   // 跳过 blog/x.en.md 等变体
                Page pg;
                pg.file = "blog/" + stem;   // 带前缀：RSS/link/搜索输出相对路径天然正确
                std::string body;
                FrontMatter fm = parse_front_matter(read_file(e.path()), body);
                if (fm.draft && !includeDrafts) continue;
                pg.draft  = fm.draft;
                pg.weight = fm.weight;
                pg.title  = fm.hasTitle ? fm.title : stem;
                pg.tags   = fm.tags;
                pg.date   = fm.date;
                pg.dateT  = parse_date_str(fm.date);
                if (!pg.dateT) pg.dateT = file_mtime_t(e.path());
                b.blog_posts.push_back(std::move(pg));
            }
            // 博客排序/筛选/导航由 on_data_query 插件查询结果驱动（prepare_pages 末尾补加）；
            // 此处只做原始收集（目录扫描 + frontmatter 解析），不排序。
        }
    }
    // 数据查询钩子：构建全部页面数据快照 → on_data_query 插件（Python 脚本聚合/排序/分页）。
    // 引擎只产原始数据（收集/解析/渲染），查询逻辑 100% 由插件实现；
    // 插件无输出或输出为空 = 纯文档站（无博客流/无标签页/首页无文章列表）。
    {
        json snap = json::array();
        auto push_snap = [&](const std::vector<Page>& v) {
            for (const auto& p : v) {
                snap.push_back(json{{"file", p.file}, {"title", p.title},
                                    {"date", p.date}, {"dateT_iso", iso8601(p.dateT)},
                                    {"tags", p.tags}, {"weight", p.weight},
                                    {"draft", p.draft}});
            }
        };
        push_snap(b.pages);
        push_snap(b.blog_posts);
        std::vector<json> outs;
        run_plugin_hooks("on_data_query", json{
            {"source", fs::absolute(b.in_dir).string()},
            {"dest",   fs::absolute(b.out_dir).string()},
            {"engine", fs::absolute(g_engine).string()},
            {"count",  b.pages.size() + b.blog_posts.size()},
            {"pages",  snap}
        }, &outs);
        for (const auto& o : outs) {
            if (!o.is_object()) continue;
            for (auto it = o.begin(); it != o.end(); ++it) {
                if (it.key() == "ok" || it.key() == "message") continue;
                b.query_out[it.key()] = it.value();
            }
            b.query_ready = true;
        }
        // 导航入口由查询结果驱动（有博客流 → 博客链接；有标签 → 标签链接）
        auto has_nav = [&](const std::string& url) {
            for (const auto& n : b.cfg.nav)
                if (!n.url.empty() && n.url.find(url) != std::string::npos) return true;
            return false;
        };
        if (b.query_out.contains("blog_order") && b.query_out["blog_order"].is_array()
            && !b.query_out["blog_order"].empty() && !has_nav("blog/index.html")) {
            NavNode blogLink;
            blogLink.title = "{{navBlog}}";
            blogLink.url = "blog/index.html";
            b.cfg.nav.push_back(blogLink);
        }
        if (b.query_out.contains("tags") && b.query_out["tags"].is_array()
            && !b.query_out["tags"].empty() && !has_nav("tags/index.html")) {
            NavNode tagLink;
            tagLink.title = "标签";
            tagLink.url = "tags/index.html";
            b.cfg.nav.push_back(tagLink);
        }
    }
    return 0;   // 到达此处即收集成功
}

// ============ 地图模式整页渲染（v2：读 theme/map/<type>.html 按图拼接，无自定义控制流语法） ============
// 数据：C++ 只产 json（PageCtx + 环境数据），HTML 全部在 theme/map/*.html + theme/components/**。

std::string map_render_page(const SiteConfig& cfg, const RenderOpts& opt,
                                   const PageCtx& pcx, const std::string& mapType,
                                   bool isHome) {
    std::string title = pcx.title.empty() ? cfg.title : (pcx.title + " · " + cfg.title);
    std::string lang = pcx.curLocale.empty() ? "zh-CN" : pcx.curLocale;
    // 语言切换数据（LangSwitch/LangItem 组件渲染）：items href 加 relBase 前缀（子目录页回退一级）
    json langSwitch = pcx.lang_data;
    if (langSwitch.contains("items") && langSwitch["items"].is_array() && !pcx.relBase.empty()) {
        for (auto& it : langSwitch["items"])
            if (it.contains("href") && it["href"].is_string())
                it["href"] = pcx.relBase + it["href"].get<std::string>();
    }
    json data = {
        {"lang", esc_attr(lang)}, {"theme", esc(cfg.theme)}, {"title", esc(title)},
        {"base", pcx.relBase}, {"body_class", isHome ? " class=\"page-home\"" : ""},
        {"is_home", isHome},
        {"site_title", esc(cfg.title)}, {"site_desc", esc(cfg.description)},
        {"meta_desc", esc(pcx.desc)},
        {"head_meta", pcx.head_meta},
        {"head_links", pcx.head_links},
        {"jsonld", pcx.jsonld},
        {"show_highlight", opt.showCodeHighlight},
        {"theme_vars", cfg.themeVarsBody}, {"custom_css_href", cfg.customCssHref},
        {"header", header_json(cfg, opt, pcx.curLocale, langSwitch, pcx.relBase, isHome)},
        {"nav_groups", pcx.nav_groups},
        {"breadcrumb", pcx.breadcrumb_map},
        {"hero", pcx.hero},
        {"cards", pcx.cards},
        {"pager", pcx.pager},
        {"edit", pcx.edit},
        {"toc_items", pcx.toc_items},
        {"show_toc", opt.showToc && !pcx.toc_items.empty()},
        {"blog_posts", pcx.blog_posts},
        {"blog_pager", pcx.blog_pager},
        {"tags", pcx.tags},
        {"tag_name", pcx.tag_name},
        {"tag_docs", pcx.tag_docs},
        {"body", pcx.body},
        {"last_updated", esc(pcx.last_updated)},
        {"body_end", pcx.body_end},
        {"skip_label", (pcx.curLocale == "en") ? "Skip to main content" : "跳到主要内容"},
        {"footer", footer_json(cfg)},
        {"backtop", json{{"show", opt.showBackToTop}, {"threshold", cfg.backToTopThreshold},
                         {"label", (cfg.backToTopLabel == "↑ 顶部") ? "{{backToTop}}" : esc_attr(cfg.backToTopLabel)}}},
        {"scripts", json{{"highlight", opt.showCodeHighlight}, {"search", opt.showSearch},
                         {"i18n_json", pcx.i18nJson}, {"feedback", cfg.feedbackEndpoint}}}
    };
    // 站点自定义数据（v6）：.Cdocs/data/*.json 合并进页面数据作用域（优先级：props > 地图 data > 站点 data > 内置）
    {
        const json& sd = site_data();
        for (auto it = sd.cbegin(); it != sd.cend(); ++it) {
            data[it.key()] = it.value();
            g_tpl_keys.insert(it.key());   // 站点 data 键加入 L2 白名单（"有 kv 就拿"；没 kv 不误报）
        }
    }
    // 收集合法模板键 → g_tpl_keys（L2 残留检测白名单）
    for (auto it = data.cbegin(); it != data.cend(); ++it) g_tpl_keys.insert(it.key());
    std::string out = compose_page(mapType, data);
    // 首页 layout no-sidebar（地图已写则 find 不命中，无害兜底）
    if (isHome) {
        size_t pl = out.find("<div class=\"layout\">");
        if (pl != std::string::npos)
            out.replace(pl, std::strlen("<div class=\"layout\">"), "<div class=\"layout no-sidebar\">");
    }
    return out;
}

// 3) 多语言构建循环：每个语言输出到独立子目录（未开启 i18n 时单语言输出到根）
// 按 mode 查找第一个匹配的页面类型名（找不到返回默认名，保持旧行为）
std::string type_for_mode(const json& maps, const std::string& mode, const std::string& def) {
    if (maps.is_array())
        for (const auto& e : maps)
            if (e.is_object() && e.value("mode", "") == mode) return e.value("type", def);
    return def;
}

static void render_one_locale(BuildContext& b, const json& maps, const std::string& loc,
                         bool multi, const I18nDict& dict, std::time_t asig, bool assetsChanged) {
    const SiteConfig& cfg = b.cfg;
    const I18nCfg& i18n = b.i18n;
    const I18nDict& fallbackUI = b.fallbackUI;
    std::vector<Page>& pages = b.pages;   // 博客流可能回退 dateT（mtime）
    const fs::path& out_dir = b.out_dir;
    fs::path in_dir = b.in_dir;                 // 静态资源发布源（md/ 下非 Markdown 文件）
    const RenderOpts& opt = b.opt;              // 渲染选项（压缩/指纹/地图渲染）
    const bool includeDrafts = b.includeDrafts;
    std::error_code ec;
    std::string i18nJson = dict_to_json(dict);   // 注入页面，供 app.js 客户端文案本地化
    std::string curLocale = loc;                 // 空 = 单语言（render_page 退化为 zh-CN）
    fs::path locOut = out_dir;
    if (multi) locOut = out_dir / loc;
    fs::create_directories(locOut, ec);
    // 复制前端资源到本语言目录（相对链接 assets/... 在子目录内同样成立）
    // 源签名变化或目标缺失 → 复制；40 个字体 ×2 语言是构建最大 I/O 大头，多数构建命中跳过。
    bool needAssets = assetsChanged || !fs::exists(locOut / "assets");
    if (g_verbose && !needAssets)
        std::cout << color::muted("  [incr] 跳过 assets 复制（源未变）\n");
    if (needAssets) {
        if (fs::exists(theme_root() / "assets"))
            fs::copy(theme_root() / "assets", locOut / "assets", fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        else
            fs::create_directories(locOut / "assets", ec);
        // 复制前端运行时依赖（deps/ 中的 JS·CSS 库）到 assets/deps
        // 注意：deps/vendor 是 C++ 编译期头文件（md4c / nlohmann），不属于站点运行资源，不随站点发布
        if (fs::exists(g_engine / "deps")) {
            fs::create_directories(locOut / "assets" / "deps", ec);
            for (const auto& e : fs::directory_iterator(g_engine / "deps")) {
                if (e.path().filename() == "vendor") continue;   // 编译期依赖，跳过发布
                fs::copy(e.path(), locOut / "assets" / "deps" / e.path().filename(),
                         fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
            }
        }
    }

    // 静态资源发布：md/ 下图片/附件等非 Markdown 文件，按相对路径拷到本语言目录
    copy_doc_assets(in_dir, locOut, ec);

    // 构建期压缩（默认开启，config site.compress=false 可关）：
    // 1) 图片：stb_image + libwebp 生成 WebP 副本（原图保留，页面 <picture> 优先 WebP）；
    // 2) CSS：主题 css/ 保守紧凑化（去注释 + 折叠空白，无收益则不动）。
    if (cfg.compress) {
        int nWebp = webpize_dir(locOut, cfg.jpegQuality);
        if (g_verbose && nWebp > 0)
            std::cout << color::muted("  [compress] 生成 " + std::to_string(nWebp) + " 个 WebP 副本\n");
        compress_dir_css(locOut / "assets" / "css");
    }
    // 资源指纹（cache busting）：对主题 css/js 计算内容哈希，页面引用与 sw 缓存同步加 ?v=
    if (assetsChanged || g_fp.empty() || !fs::exists(locOut / "assets" / "css"))
        fingerprint_assets(locOut / "assets");

    // 语言切换器（仅多语言模式）：链接到兄弟语言的 index.html
    json langData = json{{"show", false}, {"current", ""}, {"items", json::array()}};
    if (multi) {
        // 语言切换数据（地图模式 LangSwitch/LangItem 组件渲染；href 的 relBase 前缀由 map_render_page 修正）
        langData["show"] = true;
        langData["current"] = i18n.labels.count(loc) ? i18n.labels.at(loc) : loc;
        langData["items"] = json::array();
        for (auto& kv : i18n.labels)
            if (kv.first != loc)
                langData["items"].push_back(json{{"label", kv.second},
                                                 {"href", "../" + kv.first + "/index.html"}});
    }

    // 本语言在站点基址下的前缀（用于 canonical / 交替链接 / 结构化数据）
    std::string homeBase;
    if (!cfg.url.empty()) { homeBase = cfg.url; if (!homeBase.empty() && homeBase.back() != '/') homeBase += '/';
                            if (multi) homeBase += loc + "/"; }

    // 行业标准增强：本语言 RSS / PWA / 社交分享所需的 head 片段与封面图 URL
    std::string feedTitle = i18n_replace(cfg.title, dict);
    std::string feedLinkTag = "  <link rel=\"alternate\" type=\"application/rss+xml\" title=\""
                              + esc_attr(feedTitle) + "\" href=\"./rss.xml\">\n";
    std::string manifestTag = "  <link rel=\"manifest\" href=\"./manifest.webmanifest\">\n"
                              "  <meta name=\"theme-color\" content=\"#a8332a\">\n";
    std::string ogImageUrl;
    if (!cfg.ogImage.empty() && !cfg.url.empty()) {
        ogImageUrl = cfg.url;
        if (ogImageUrl.back() != '/') ogImageUrl += '/';
        std::string rel = cfg.ogImage;
        if (!rel.empty() && rel.front() == '/') rel = rel.substr(1);   // 去掉开头 /，避免双斜杠
        if (rel.compare(0, 4, "http") == 0) ogImageUrl = rel;
        else ogImageUrl += rel;
    }

    // hreflang 交替链接（i18n 标准 SEO）：指向其他语言同一页面 + x-default。
    // url 配置时用绝对地址；url 为空时用相对路径，需按页面深度加 ../ 前缀
    // （子目录页如 guide/install 在 zh-CN/guide/，兄弟语言在 ../../en/...）。
    // head 数据化（地图模式 MetaLink/MetaOgItem/MetaNameItem/JsonLd 组件渲染）。
    // canonical/prev/next/hreflang/RSS/manifest → links；OG/Twitter/theme-color → meta；
    // JSON-LD → jsonld 字符串。fallback 仍用 headExtra 字符串（双轨并行）。
    // 每语言渲染上下文（收敛跨函数参数）
    LocaleRenderCtx rc{maps, dict, i18nJson, curLocale, homeBase, feedTitle, ogImageUrl, langData, locOut};

    // 页面类型渲染（render_pages.cpp）：首页/文档页/博客流/搜索索引/标签聚合/single
    render_home(b, rc);
    render_doc_pages(b, rc);
    render_blog(b, rc);
    render_search_index(b, rc);
    render_tags(b, rc);
    render_single(b, rc);

    // 11) RSS / JSON Feed（行业标准，内建，无需 Node；博客文章并入订阅流）
    {
        std::vector<Page> allPages = pages;
        allPages.insert(allPages.end(), b.blog_posts.begin(), b.blog_posts.end());
        gen_feeds(locOut, loc, allPages, cfg, dict, multi);
    }
    // 12) PWA（manifest + service worker + theme-color），内建替代 gen-pwa.js
    gen_pwa(locOut, cfg, feedTitle, theme_root() / "assets");}

static void render_locales(BuildContext& b) {
    // 版本化适配：head.nav 指向当前版本不存在的页面时剔除（历史版快照内容不完整——
    // 多版本共用一个 config，旧版本没有新页面属正常；避免死链导航）。单版本零影响。
    {
        std::set<std::string> files;
        for (const auto& p : b.pages) files.insert(p.file);
        bool hasBlog = b.query_ready && b.query_out.contains("blog_order")
                       && b.query_out["blog_order"].is_array()
                       && !b.query_out["blog_order"].empty();
        std::vector<Link> keep;
        for (const auto& l : b.cfg.header.nav) {
            if (l.file.empty()) { keep.push_back(l); continue; }
            if (l.file == "blog/index") { if (hasBlog) keep.push_back(l); continue; }
            if (files.count(l.file)) keep.push_back(l);
        }
        b.cfg.header.nav = std::move(keep);
    }
    // v4 地图驱动必需：theme/map/ 目录（否则无法拼接页面）
    std::error_code mec;
    if (!fs::is_directory(theme_root() / "map", mec)) {
        std::cerr << color::error("错误: 主题缺少 theme/map/ 目录（v4 地图驱动必需）\n");
        return;
    }
    // v5 动态页面类型：config/map.json 的 maps 数组注册所有页面类型（用户可自由增删，
    // 每个页面类型由 {type, map, mode, output?} 声明；mode 决定数据来源与输出方式）。
    // 数量上限 kMaxMapTypes 防意外膨胀/资源耗尽（正常主题远达不到）。
    const int kMaxMapTypes = 64;
    std::error_code rec2;
    json mapRegistry;
    {
        fs::path rp = g_engine / "config" / "map.json";
        if (fs::is_regular_file(rp, rec2)) {
            try { mapRegistry = json::parse(read_file(rp)); } catch (...) {}
        }
    }
    json maps = mapRegistry.value("maps", json());
    // 旧格式兼容：maps 为对象（类型名 → 地图路径）时按内置类型名推导 mode
    if (maps.is_object()) {
        json arr = json::array();
        std::map<std::string, std::string> legacyMode = {
            {"home", "home"}, {"doc", "pages"}, {"blog", "blog-list"},
            {"blog-post", "blog-post"}, {"tags", "tags"}, {"tag-page", "tag-page"}, {"404", "single"}};
        for (auto it = maps.begin(); it != maps.end(); ++it) {
            json e;
            e["type"] = it.key();
            if (it.value().is_string()) e["map"] = it.value().get<std::string>();
            auto lm = legacyMode.find(it.key());
            e["mode"] = (lm != legacyMode.end()) ? lm->second : "single";
            if (it.key() == "404") e["output"] = "404.html";
            arr.push_back(e);
        }
        maps = arr;
    }
    if (!maps.is_array() || maps.empty()) {
        std::cerr << color::error("错误: config/map.json 缺少 maps 数组（v5 页面类型注册表）\n");
        return;
    }
    if (maps.size() > kMaxMapTypes) {
        std::cerr << color::error("错误: 页面类型数量 ") << maps.size() << " 超过上限 "
                  << kMaxMapTypes << "（config/map.json 的 maps 数组）\n";
        return;
    }
    // 按 mode 查找第一个匹配的页面类型名（找不到返回默认名，保持旧行为）
    SiteConfig& cfg = b.cfg;
    RenderOpts& opt = b.opt;
    std::vector<Page>& pages = b.pages;
    I18nCfg& i18n = b.i18n;
    I18nDict& fallbackUI = b.fallbackUI;
    const bool& includeDrafts = b.includeDrafts;
    const fs::path& in_dir = b.in_dir;
    const fs::path& out_dir = b.out_dir;

    // 5) 多语言（i18n）构建：每个语言输出到独立子目录（out_dir/<loc>/），
    //    未开启 i18n 时退化为单语言（输出到 out_dir 根，使用兜底中文 UI 字典）。
    std::error_code ec;
    std::vector<std::string> locs;
    if (i18n.enabled) for (auto& kv : i18n.labels) locs.push_back(kv.first);
    else              locs.push_back("");   // 单语言哨兵（空 loc）

    // 前端资源变化检测（语言循环外统一计算，避免多语言下首个语言复制/写 sig 后
    // 后续语言全部跳过——旧实现导致只有首个语言目录刷新 assets）。
    // 签名 = 主题 assets / 运行时 deps 递归所有文件 mtime 最大值；只 stat 目录本身会在"改文件内容"时漏检。
    std::time_t asig = 0;
    std::error_code rec;
    for (const auto& ap : {theme_root() / "assets", g_engine / "deps"}) {
        if (!fs::exists(ap, rec)) continue;
        rec.clear();
        for (auto it = fs::recursive_directory_iterator(ap, rec), end = fs::recursive_directory_iterator();
             it != end; it.increment(rec)) {
            if (rec) { rec.clear(); continue; }
            if (!it->is_regular_file(rec)) continue;
            struct stat ast;
            if (stat(it->path().string().c_str(), &ast) == 0 && ast.st_mtime > asig) asig = ast.st_mtime;
        }
    }
    std::error_code aec;
    fs::path aSigFile = g_engine / ".build" / ".assets.sig";
    fs::create_directories(g_engine / ".build", aec);
    std::string aPrev = fs::exists(aSigFile, aec) ? trim(read_file(aSigFile)) : std::string();
    std::string aCur = std::to_string(asig);
    bool assetsChanged = (aPrev != aCur);   // 源有变化 → 本次全语言刷新

    // 语言渲染循环：每个语言调用 render_one_locale（assets/压缩/指纹/页面渲染/feeds/PWA）
    for (const auto& loc : locs) {
        bool multi = i18n.enabled;
        const I18nDict& dict = multi ? i18n.dicts[loc] : fallbackUI;
        render_one_locale(b, maps, loc, multi, dict, asig, assetsChanged);
    }

    // 死链检查放在全部语言构建完成后统一执行（避免语言切换链接在兄弟语言目录
    // 尚未生成时被误报）；对标 VitePress/MkDocs 的链接校验，结果末尾告警不阻断。
    for (const auto& loc : locs) {
        fs::path lo = out_dir;
        if (!loc.empty()) lo = out_dir / loc;
        check_links(lo, loc);
    }
    // 全部语言复制完成后统一写资产签名（源变化时刷新了所有语言目录）
    if (assetsChanged) { std::ofstream f(aSigFile); f << aCur; }
}

// --clean 原子替换：把旧输出目录 rename 为 <out>.old（瞬时 O(1)），
// 避免 Windows Defender 对"删除后同一路径立即重建"的逐文件全量扫描
// --clean 清理：直接删除输出目录。
// 实测 rename 原子替换（dist → dist.old → 重建 → 删 .old）在 Windows 上无效：
// rename 后 .old 成为"新路径"，Defender 实时防护会全量扫描它并拖慢同目录写入，
// 反而比直接 remove_all 更不稳。故恢复简单可靠的 remove_all。
int run_build(fs::path in_dir, fs::path out_dir, bool includeDrafts, bool cleanBefore) {
    // 每次构建重置 i18n 未命中键收集（serve --watch 会反复重建，不能跨次累积）
    g_i18n_missing.clear();
    g_link_broken.clear();
    g_tpl_keys.clear();

    // ---- 多版本分派（versions.cpp）----
    // 命中多版本（config.versions 或 md-* 快照约定）则独立构建各版本并返回；
    // 单版本 / 子构建重入时不处理，走下方单版本主流程。
    if (dispatch_versions(in_dir, out_dir, includeDrafts, cleanBefore))
        return 0;

    // 输出横幅 + --clean 清理（阶段 1 起的状态集中到 BuildContext）


    if (g_verbose)
        std::cout << color::muted("source=") << in_dir << color::muted(" dest=") << out_dir
                  << color::muted(" engine=") << g_engine << "\n";
    if (!g_quiet)
        std::cout << color::bold(color::cyan("Cdocs")) << " " << CDOCS_VERSION
                  << color::muted(" — 生成 ") << in_dir << color::muted(" → ") << out_dir << "\n";

    if (cleanBefore) {
        std::error_code ec2;
        fs::remove_all(out_dir, ec2);
        if (g_verbose && !ec2) std::cout << color::muted("已清空输出目录: ") << out_dir << "\n";
    }

    BuildContext b;
    b.in_dir        = in_dir;
    b.out_dir       = out_dir;
    b.includeDrafts = includeDrafts;

    // 外部脚本插件：扫描 .Cdocs/plugins/*/plugin.json（无插件目录时零开销）
    plugins_scan_all();

    load_site_config(b);                      // 1) 配置 + 导航 + 渲染开关
    // 插件钩子：配置加载完成后（可让插件增强/修改站点配置相关上下文）。
    // 收集各插件 out.json 的 inject 对象（key=语言 → 正文末尾注入 HTML，如评论插件），
    // 渲染时按当前语言插入正文末尾（引擎只提供通用注入能力，不感知具体插件）。
    g_body_ends.clear();
    {
        std::vector<json> outs;
        run_plugin_hooks("on_config", json{
            {"source", fs::absolute(in_dir).string()},
            {"dest",   fs::absolute(out_dir).string()},
            {"engine", fs::absolute(g_engine).string()},
            {"title",  b.cfg.title},
            {"plugins", b.cfg.plugins}
        }, &outs);
        for (const auto& o : outs) {
            if (!o.is_object() || !o.contains("inject") || !o["inject"].is_object()) continue;
            for (auto& [loc, html] : o["inject"].items())
                if (html.is_string())
                    g_body_ends[loc] += html.get<std::string>() + "\n";
        }
    }
    if (int rc = prepare_pages(b)) return rc; // 2) 输入检查 + 收集页面 + 预扫描

    // 增量构建判定：仅 serve -w 且未 --clean 时启用。
    // 全局签名（config/route/i18n/templates/assets 的 mtime 最大值）变化 → 全量；
    // 否则源 .md 未变的页面跳过渲染（复用已生成产物），加速 watch 循环。
    if (g_incremental && !cleanBefore) {
        std::error_code ec;
        std::time_t sig = 0;
        for (const auto& p : {g_engine / "config" / "config.json",
                              g_engine / "config" / "route.json",
                              g_engine / "i18n",
                              theme_root() / "theme.json",
                              theme_root() / "templates",
                              theme_root() / "assets"}) {
            struct stat st;
            if (stat(p.string().c_str(), &st) == 0 && st.st_mtime > sig) sig = st.st_mtime;
        }
        // 与上次构建签名比较（存 .build/.sig 文件）
        fs::path buildDir = g_engine / ".build";
        fs::create_directories(buildDir, ec);
        fs::path sigFile = buildDir / ".sig";
        std::string prev = fs::exists(sigFile, ec) ? trim(read_file(sigFile)) : std::string();
        b.globalDirty = (prev != std::to_string(sig));
        if (b.globalDirty && !g_quiet)
            std::cout << color::muted("  [incr] 全局配置变化，全量重建\n");
        else if (!b.globalDirty)
            std::cout << color::muted("  [incr] 全局未变，增量重建\n");
        // 写入本次签名
        { std::ofstream f(sigFile); f << sig; }
        // 载入上次页面指纹（file|loc -> mtime:size）
        fs::path sigMapFile = buildDir / ".pages.sig";
        if (fs::exists(sigMapFile, ec)) {
            try {
                json j = json::parse(read_file(sigMapFile));
                for (auto it = j.begin(); it != j.end(); ++it)
                    b.pageSig[it.key()] = it.value().get<std::string>();
            } catch (...) { b.pageSig.clear(); }
        }
        b.incremental = !b.globalDirty;
    }

    // 插件钩子：页面收集完成后（插件可拿到页面清单做预处理/增删页）
    {
        json pagesArr = json::array();
        for (const auto& p : b.pages) {
            pagesArr.push_back({
                {"file",  p.file},
                {"title", p.title},
                {"draft", p.draft},
                {"tags",  p.tags}
            });
        }
        run_plugin_hooks("on_page_collected", json{
            {"source", fs::absolute(in_dir).string()},
            {"dest",   fs::absolute(out_dir).string()},
            {"count",  b.pages.size()},
            {"pages",  pagesArr}
        });
    }
    render_locales(b);                        // 3) 多语言构建循环
    write_root_redirect(b);                   // 4) 多语言根 index.html 重定向
    write_root_feeds_pwa(b);                  // 5) 根目录 feed / PWA
    write_sitemap(b);                         // 6) sitemap.xml
    write_robots(b);                          // 7) robots.txt
    // 插件钩子：全部产物生成完成后（部署 / 压缩 / 通知等收尾）
    run_plugin_hooks("on_done", json{
        {"source", fs::absolute(in_dir).string()},
        {"dest",   fs::absolute(out_dir).string()},
        {"engine", fs::absolute(g_engine).string()}
    });
    // 持久化页面指纹，供下次 watch 增量判定（全量/增量都写，首次全量后即可增量）
    if (!b.pageSig.empty()) {
        json j = json::object();
        for (const auto& kv : b.pageSig) j[kv.first] = kv.second;
        std::error_code ec;
        fs::create_directories(g_engine / ".build", ec);
        std::ofstream f(g_engine / ".build" / ".pages.sig");
        f << j.dump();
    }
    print_summary(b);                         // 8) 汇总输出
    scan_output_leftovers(out_dir);           // 9) 残留检测：{{}}/组件标签残留 → 显式警告
    return 0;
}
