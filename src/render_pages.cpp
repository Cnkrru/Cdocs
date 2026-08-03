// render_pages.cpp —— 页面类型渲染
// （自 builder.cpp render_one_locale 拆分：6 种页面类型 + head 数据）
// 渲染管线：render_one_locale 准备资产后调本模块渲染各页面，feeds/PWA 仍在调用方。

#include "render_pages.hpp"
#include "ctxdata.hpp"       // nav_groups_json / cards_json / pager_json
#include "compress.hpp"      // apply_fingerprints / minify_html / wrap_webp
#include "i18n.hpp"          // i18n_replace / dict_to_json
#include "feeds.hpp"         // gen_feeds
#include "pwa.hpp"           // gen_pwa
#include "search.hpp"
#include "plugin.hpp"      // plugins_any（插件注册探测）
#include "frontmatter.hpp"  // FrontMatter（front matter 解析）
#include "pages.hpp"        // TocResult / toc 提取 / find_path
#include "shortcode.hpp"    // render_doc_body（正文完整管线）
#include <fstream>
#include <sstream>
#include <mutex>

json build_head_data(BuildContext& b, const LocaleRenderCtx& rc,
                     const std::string& file, int depth,
                     const std::string& title, const std::string& desc,
                     std::time_t published, std::time_t modified, bool article,
                     const std::vector<std::string>& crumbs,
                     const std::string& prevFile, const std::string& nextFile) {
    const SiteConfig& cfg = b.cfg;
    const I18nCfg& i18n = b.i18n;
        json hd = json::object();
        hd["meta"] = json{{"desc", desc}};
        hd["meta"]["og"] = json::array();
        hd["meta"]["names"] = json::array();
        hd["links"] = json::array();
        std::string up;
        for (int k = 0; k < depth + 1; ++k) up += "../";
        if (!cfg.url.empty()) {
            std::string u = rc.homeBase;
            hd["links"].push_back(json{{"rel", "canonical"}, {"href", u + file + ".html"}, {"attrs", ""}});
            if (!prevFile.empty()) hd["links"].push_back(json{{"rel", "prev"}, {"href", u + prevFile + ".html"}, {"attrs", ""}});
            if (!nextFile.empty()) hd["links"].push_back(json{{"rel", "next"}, {"href", u + nextFile + ".html"}, {"attrs", ""}});
            if (i18n.enabled) {
                for (auto& kv : i18n.labels) {
                    if (kv.first == rc.curLocale) continue;
                    std::string ou;
                    if (cfg.url.empty()) ou = up + kv.first + "/" + file + ".html";
                    else { ou = cfg.url; if (!ou.empty() && ou.back() != '/') ou += '/'; ou += kv.first + "/" + file + ".html"; }
                    hd["links"].push_back(json{{"rel", "alternate"}, {"href", ou}, {"attrs", " hreflang=\"" + kv.first + "\""}});
                }
                std::string du;
                if (cfg.url.empty()) du = up + i18n.defaultLocale + "/" + file + ".html";
                else { du = cfg.url; if (!du.empty() && du.back() != '/') du += '/'; du += i18n.defaultLocale + "/" + file + ".html"; }
                hd["links"].push_back(json{{"rel", "alternate"}, {"href", du}, {"attrs", " hreflang=\"x-default\""}});
            }
            // JSON-LD：文章页 → BreadcrumbList；首页 → WebSite
            if (article) {
                std::ostringstream items;
                int pos = 1;
                items << "{\"@type\":\"ListItem\",\"position\":" << pos++ << ",\"name\":\""
                      << esc_attr(i18n_replace("{{home}}", rc.dict)) << "\",\"item\":\""
                      << rc.homeBase << "index.html\"}";
                for (const auto& c : crumbs)
                    items << ",{\"@type\":\"ListItem\",\"position\":" << pos++ << ",\"name\":\""
                          << esc_attr(i18n_replace(c, rc.dict)) << "\"}";
                items << ",{\"@type\":\"ListItem\",\"position\":" << pos++ << ",\"name\":\""
                      << esc_attr(i18n_replace(title, rc.dict)) << "\",\"item\":\"" << rc.homeBase << file << ".html\"}";
                hd["jsonld"] = "{\"@context\":\"https://schema.org\",\"@type\":\"BreadcrumbList\",\"itemListElement\":["
                               + items.str() + "]}";
            } else {
                std::string jn = i18n_replace(cfg.title, rc.dict);
                std::string je = esc_attr(jn);
                hd["jsonld"] = "{\"@context\":\"https://schema.org\",\"@type\":\"WebSite\",\"name\":\""
                               + je + "\",\"url\":\"" + rc.homeBase + "index.html\"}";
            }
        } else hd["jsonld"] = "";
        // RSS + PWA manifest + theme-color（RSS/manifest 用 depth 层 ../；hreflang 用 depth+1 层上级语言目录）
        std::string upRss;
        for (int k = 0; k < depth; ++k) upRss += "../";
        hd["links"].push_back(json{{"rel", "alternate"}, {"href", upRss + "rss.xml"},
                                   {"attrs", " type=\"application/rss+xml\" title=\"" + esc_attr(rc.feedTitle) + "\""}});
        hd["links"].push_back(json{{"rel", "manifest"}, {"href", upRss + "manifest.webmanifest"}, {"attrs", ""}});
        hd["meta"]["names"].push_back(json{{"name", "theme-color"}, {"content", "#a8332a"}});
        // OG / Twitter（social_head 数据化；摘要去标题前缀）
        std::string d = desc;
        size_t pp = 0;
        while (pp < d.size() && (d[pp] == ' ' || d[pp] == '\t' || d[pp] == '\n' || d[pp] == '\r')) ++pp;
        if (!title.empty() && d.compare(pp, title.size(), title) == 0) {
            pp += title.size();
            while (pp < d.size() && (d[pp] == ' ' || d[pp] == '\t' || d[pp] == '\n' || d[pp] == '\r')) ++pp;
            d = d.substr(pp);
        }
        std::string ogUrl = cfg.url.empty() ? std::string() : rc.homeBase + file + ".html";

        if (!ogUrl.empty()) hd["meta"]["og"].push_back(json{{"property", "og:url"}, {"content", ogUrl}});
        hd["meta"]["og"].push_back(json{{"property", "og:type"}, {"content", article ? "article" : "website"}});
        hd["meta"]["og"].push_back(json{{"property", "og:title"}, {"content", title}});
        if (!d.empty()) hd["meta"]["og"].push_back(json{{"property", "og:description"}, {"content", d}});
        if (!cfg.url.empty() && !cfg.title.empty())
            hd["meta"]["og"].push_back(json{{"property", "og:site_name"}, {"content", cfg.title}});
        if (!rc.curLocale.empty()) hd["meta"]["og"].push_back(json{{"property", "og:locale"}, {"content", rc.curLocale}});
        if (!rc.ogImageUrl.empty()) hd["meta"]["og"].push_back(json{{"property", "og:image"}, {"content", rc.ogImageUrl}});
        if (article) {
            if (published) hd["meta"]["og"].push_back(json{{"property", "article:published_time"}, {"content", iso8601(published)}});
            if (modified)  hd["meta"]["og"].push_back(json{{"property", "article:modified_time"}, {"content", iso8601(modified)}});
        }
        hd["meta"]["names"].push_back(json{{"name", "twitter:card"}, {"content", rc.ogImageUrl.empty() ? "summary" : "summary_large_image"}});
        hd["meta"]["names"].push_back(json{{"name", "twitter:title"}, {"content", title}});
        if (!d.empty()) hd["meta"]["names"].push_back(json{{"name", "twitter:description"}, {"content", d}});
        if (!rc.ogImageUrl.empty()) hd["meta"]["names"].push_back(json{{"name", "twitter:image"}, {"content", rc.ogImageUrl}});
        return hd;
}

void render_home(BuildContext& b, const LocaleRenderCtx& rc) {

    const SiteConfig& cfg = b.cfg;
    const I18nCfg& i18n = b.i18n;
    std::vector<Page>& pages = b.pages;
    const RenderOpts& opt = b.opt;
    const fs::path& out_dir = b.out_dir;
    fs::path in_dir = b.in_dir;
    const bool includeDrafts = b.includeDrafts;
    const json& maps = rc.maps;
    const I18nDict& dict = rc.dict;
    const std::string& i18nJson = rc.i18nJson;
    const std::string& curLocale = rc.curLocale;
    const std::string& homeBase = rc.homeBase;
    const std::string& feedTitle = rc.feedTitle;
    const std::string& ogImageUrl = rc.ogImageUrl;
    const json& langData = rc.langData;
    const bool multi = i18n.enabled;
    std::error_code ec;
    const std::string& loc = rc.curLocale;
    const fs::path& locOut = rc.locOut;
    // 6) 首页（rc.maps 注册表 mode=home；isHome 标记页眉/移动端一致；无左侧边栏）
    // 首页 → PageCtx（hero/cards 数据由 Hero/Cards 组件渲染）
    {
        PageCtx ctx;
        ctx.is_home = true;
        ctx.hero = json{{"title", cfg.homeTitle.empty() ? cfg.title : cfg.homeTitle},
                        {"subtitle", cfg.homeSubtitle.empty() ? cfg.description : cfg.homeSubtitle}};
        std::string ctaFile = cfg.homeCtaFile;
        std::string ctaText = cfg.homeCtaText.empty() ? std::string("{{getStarted}}") : cfg.homeCtaText;
        if (ctaFile.empty() && !pages.empty()) ctaFile = pages[0].file;
        if (!ctaFile.empty()) {
            ctx.hero["cta_href"] = ctaFile + ".html";
            ctx.hero["cta_text"] = ctaText;
        }
        ctx.cards = cards_json(cfg, pages);
        // 博客流注入首页（插件 home_posts 决定取哪些；引擎只做渲染数据映射）
        if (b.query_ready && b.query_out.contains("home_posts") && b.query_out["home_posts"].is_array()
            && !b.query_out["home_posts"].empty()) {
            json posts = json::array();
            std::map<std::string, const Page*> pgMap;
            for (const auto& bp : b.blog_posts) pgMap[bp.file] = &bp;
            for (const auto& fl : b.query_out["home_posts"]) {
                auto it = pgMap.find(fl.get<std::string>());
                if (it == pgMap.end()) continue;
                const Page& bp = *it->second;
                posts.push_back(json{{"date", format_date_local(bp.dateT)},
                                     {"href", "blog/" + bp.file.substr(5) + ".html"},
                                     {"title", bp.title},
                                     {"desc", std::string()}});
            }
            if (!posts.empty()) ctx.blog_posts = posts;
        }
        ctx.title = cfg.title;
        ctx.desc = cfg.description;
        ctx.curLocale = rc.curLocale; ctx.lang_data = rc.langData; ctx.i18nJson = rc.i18nJson;
        // head 数据化（MetaLink/MetaOgItem/MetaNameItem/JsonLd 组件渲染）
        {
            json hd = build_head_data(b, rc, "index", 0, cfg.title, cfg.description, 0, 0, false, {}, "", "");
            ctx.head_meta = hd.value("meta", json::object());
            ctx.head_links = hd.value("links", json::array());
            ctx.jsonld = hd.value("jsonld", "");
        }
        std::string landing = map_render_page(cfg, opt, ctx, type_for_mode(rc.maps, "home", "home"), true);
        std::ofstream(rc.locOut / "index.html")
            << apply_fingerprints(cfg.compress ? wrap_webp(minify_html(i18n_replace(landing, rc.dict)), rc.locOut) : i18n_replace(landing, rc.dict));
    }
}

void render_doc_pages(BuildContext& b, const LocaleRenderCtx& rc) {

    const SiteConfig& cfg = b.cfg;
    const I18nCfg& i18n = b.i18n;
    std::vector<Page>& pages = b.pages;
    const RenderOpts& opt = b.opt;
    const fs::path& out_dir = b.out_dir;
    fs::path in_dir = b.in_dir;
    const bool includeDrafts = b.includeDrafts;
    const json& maps = rc.maps;
    const I18nDict& dict = rc.dict;
    const std::string& i18nJson = rc.i18nJson;
    const std::string& curLocale = rc.curLocale;
    const std::string& homeBase = rc.homeBase;
    const std::string& feedTitle = rc.feedTitle;
    const std::string& ogImageUrl = rc.ogImageUrl;
    const json& langData = rc.langData;
    const bool multi = i18n.enabled;
    std::error_code ec;
    const std::string& loc = rc.curLocale;
    const fs::path& locOut = rc.locOut;
    // 7) 各文档页（含 TOC 注入 + 上下篇 + i18n）
    // ---- Hugo 式并发渲染（多核并行）----
    // 两阶段：①并行渲染正文到内存（写各自 pages[i]，无跨元素竞争）；
    //         ②并行拼装页面并写盘（pager 需全部页面 title 就绪）。
    // 有插件注册时退化串行（on_page_rendered 依赖顺序与安全）。
    bool hasPlugs = plugins_any();
    std::mutex sigMutex;                       // pageSig 并发读写保护
    std::vector<char> skip(pages.size(), 0);   // 增量跳过标记（阶段 2 复用）
    // 阶段 1 产物暂存（并发下各线程只写自己索引，安全；阶段 2 只读自身索引）
    std::vector<std::string> tocHtml(pages.size()), tocNav(pages.size());
    std::vector<std::string> metaStore(pages.size());
    std::vector<json> tocItemsStore(pages.size()),
                     crumbsMapStore(pages.size()), headDataStore(pages.size());
    auto render_content = [&](size_t i) {
        if (pages[i].draft && !b.includeDrafts) return;   // 草稿默认不发布；-D/--drafts 时包含
        // 内容翻译：优先 md/<file>.<loc>.md（多语言），否则退回默认 .md（部分翻译）
        fs::path f = in_dir / (pages[i].file + ".md");
        if (multi) {
            fs::path fl = in_dir / (pages[i].file + "." + loc + ".md");
            if (fs::exists(fl)) f = fl;
        }
        if (!fs::exists(f)) { std::cerr << color::error("缺少文件: ") << f << "\n"; return; }
        // 增量构建：源 .md 指纹（mtime:size）未变且输出已存在 → 跳过渲染复用产物。
        // 指纹无论全量/增量都记录（全量也更新 .pages.sig，否则下次增量永不命中）。
        struct stat fst;
        std::string sig;
        if (stat(f.string().c_str(), &fst) == 0) {
            sig = std::to_string(fst.st_mtime) + ":" + std::to_string((long long)fst.st_size);
            std::string key = pages[i].file + "|" + (multi ? loc : "");
            if (b.incremental && !b.globalDirty) {
                fs::path existingOut = rc.locOut / (pages[i].file + ".html");
                std::lock_guard<std::mutex> lk(sigMutex);
                auto it = b.pageSig.find(key);
                if (it != b.pageSig.end() && it->second == sig && fs::exists(existingOut)) {
                    skip[i] = 1;
                    if (g_verbose)
                        std::cout << color::muted("  [incr] 跳过未变化: ") << pages[i].file << "\n";
                    return;   // 复用已有 HTML，跳过本次渲染
                }
                b.pageSig[key] = sig;   // 记录本次指纹（全量/增量一致）
            } else {
                std::lock_guard<std::mutex> lk(sigMutex);
                b.pageSig[key] = sig;
            }
        }
        std::string mdRaw = read_file(f);
        pages[i].dateT = parse_date_str(pages[i].date);
        if (!pages[i].dateT) pages[i].dateT = file_mtime_t(f);
        std::string md;
        FrontMatter fm = parse_front_matter(mdRaw, md);   // 剥离 front matter，取到正文与元数据
        pages[i].html  = render_doc_body(md, rc.curLocale == "en");
        if (pages[i].title.empty()) pages[i].title = extract_title(md, pages[i].file);
        pages[i].lastmod  = fm.lastmod;
        pages[i].aliases  = fm.aliases;
        TocResult toc = build_toc(pages[i].html);
        pages[i].html = toc.html;   // 修复：正文必须用注入 slug id 的版本（TOC 锚点/滚动高亮依赖）
        tocHtml[i] = toc.html; tocNav[i] = toc.toc;
        tocItemsStore[i] = toc.items;   // TOC 数据（TocSidebar 组件）
        std::string excerpt = collapse_ws(strip_tags(pages[i].html));
        if (excerpt.size() > 160) excerpt = truncate_utf8(excerpt, 160) + "…";
        pages[i].desc = fm.description.empty() ? excerpt : fm.description;   // front matter 优先
        // 面包屑：首页 / 祖先分组 / 当前页（当前页不链接）；子目录页首页链接需 ../ 回退
        std::string up;
        { size_t pos = 0; int d = 0;
          while ((pos = pages[i].file.find('/', pos)) != std::string::npos) { ++d; ++pos; }
          for (int k = 0; k < d; ++k) up += "../"; }
        std::vector<std::string> crumbs;
        find_path(cfg.nav, pages[i].file, crumbs);
        // 面包屑数据（Breadcrumb 组件）：首页链接 / 祖先分组（无链接）/ 当前页（current）
        json crumbsJson = json::array();
        crumbsJson.push_back(json{{"title", "{{home}}"}, {"href", up + "index.html"}, {"current", false}});
        for (const auto& c : crumbs)
            crumbsJson.push_back(json{{"title", c}, {"href", ""}, {"current", false}});
        crumbsJson.push_back(json{{"title", pages[i].title}, {"href", ""}, {"current", true}});
        // 最后更新时间 + 阅读时长（front matter lastmod 优先，否则源 .md 修改时间；数字走令牌插值）
        auto [cjk, words] = count_words(strip_tags(pages[i].html));
        int mins = (int)std::ceil(cjk / 300.0 + words / 200.0);
        if (mins < 1) mins = 1;
        std::string updated = pages[i].lastmod.empty() ? format_mtime(f) : pages[i].lastmod;
        std::string luPrefix = rc.dict.count("lastUpdated") ? rc.dict.at("lastUpdated") : "最后更新于";
        std::string rt = rc.dict.count("readingTime") ? rc.dict.at("readingTime")
                                                    : "约 {{minutes}} 分钟阅读（{{words}} 字）";
        rt = subst_tokens(rt, {{"minutes", std::to_string(mins)}, {"words", std::to_string(cjk + words)}});
        std::ostringstream meta;
        if (!updated.empty()) meta << luPrefix << " " << esc(updated) << " · ";
        meta << rt;
        std::string updatedText = meta.str();   // 纯文本（LastUpdated 组件包 <div class="page-meta">）
        // head 数据化（MetaLink/MetaOgItem/MetaNameItem/JsonLd 组件渲染）
        int depth = 0;   // 页面相对语言根的目录深度（guide/install → 1），修正 hreflang 相对路径
        { size_t pos = 0; while ((pos = pages[i].file.find('/', pos)) != std::string::npos) { ++depth; ++pos; } }
        headDataStore[i] = build_head_data(b, rc, 
            pages[i].file, depth, pages[i].title, pages[i].desc,
            pages[i].dateT, pages[i].dateT, true, crumbs,
            (i > 0) ? pages[i - 1].file : std::string(),
            (i + 1 < pages.size()) ? pages[i + 1].file : std::string());
        // 阶段 1 产物暂存到 pages[i] 之外（避免跨线程重读 pages 元素）：面包屑/元信息/head 数据
        {
            // 地图模式面包屑：{links:[有 href], texts:[纯文本], current}（CrumbLink/CrumbText/CrumbCurrent）
            json cm = json::object();
            cm["links"] = json::array();
            cm["texts"] = json::array();
            for (const auto& item : crumbsJson) {
                if (item.value("current", false)) cm["current"] = item["title"];
                else if (!item.value("href", "").empty()) cm["links"].push_back(item);
                else cm["texts"].push_back(item);
            }
            if (!cm.contains("current")) cm["current"] = "";
            crumbsMapStore[i] = cm;
        }
        metaStore[i] = updatedText;          // 纯文本（LastUpdated 组件渲染 <div class="page-meta">）
    };
    auto emit_page = [&](size_t i) {
        if (skip[i]) return;   // 增量跳过（阶段 1 已复用旧产物）
        // 子目录页面（如 guide/install.html）需要 ../ 前缀修正导航/资源相对路径
        std::string relBase;
        {
            size_t pos = 0;
            while ((pos = pages[i].file.find('/', pos)) != std::string::npos) { relBase += "../"; ++pos; }
        }
        // 文档页 → PageCtx（地图模式数据）
        PageCtx ctx;
        ctx.nav_groups = nav_groups_json(cfg.nav, pages[i].file, relBase);
        ctx.toc_items = tocItemsStore[i];
        ctx.pager = pager_json(pages, i, relBase);
        ctx.breadcrumb_map = crumbsMapStore[i];
        ctx.edit = edit_json(cfg, pages[i].file);
        ctx.body = pages[i].html;
        ctx.title = pages[i].title;
        ctx.desc = pages[i].desc;
        ctx.last_updated = metaStore[i];
        auto beIt = g_body_ends.find(rc.curLocale);
        ctx.body_end = (beIt != g_body_ends.end()) ? beIt->second : "";
        ctx.head_meta = headDataStore[i].value("meta", json::object());
        ctx.head_links = headDataStore[i].value("links", json::array());
        ctx.jsonld = headDataStore[i].value("jsonld", "");
        ctx.curLocale = rc.curLocale; ctx.lang_data = rc.langData;
        ctx.i18nJson = rc.i18nJson; ctx.relBase = relBase;
        std::string page = map_render_page(cfg, opt, ctx, type_for_mode(rc.maps, "pages", "doc"));
        fs::path pageOut = rc.locOut / (pages[i].file + ".html");
        std::error_code pe2;
        fs::create_directories(pageOut.parent_path(), pe2);   // 子目录路由需建父目录
        std::ofstream(pageOut)
            << apply_fingerprints(cfg.compress ? wrap_webp(minify_html(i18n_replace(page, rc.dict)), rc.locOut) : i18n_replace(page, rc.dict));
        // front matter aliases：为每个旧路径生成重定向页（canonical + meta refresh + JS 兜底，
        // 对标 Hugo aliases —— 文档改名后旧链接自动指向新页）
        for (const auto& a : pages[i].aliases) {
            std::string alias = a;
            while (!alias.empty() && (alias.front() == '/' || alias.front() == '\\')) alias = alias.substr(1);
            if (alias.empty()) continue;
            fs::path af = rc.locOut / (alias + ".html");
            std::error_code aec;
            fs::create_directories(af.parent_path(), aec);
            std::error_code a2;
            fs::path tgt = fs::relative(pageOut, af.parent_path(), a2);
            std::string rel = a2 ? std::string("index.html") : tgt.generic_string();
            for (auto& c : rel) if (c == '\\') c = '/';
            std::string rd = "<!DOCTYPE html>\n<html lang=\"" + esc_attr(rc.curLocale) + "\">\n<head>\n"
                "  <meta charset=\"utf-8\">\n"
                "  <title>" + esc(pages[i].title) + "</title>\n"
                "  <link rel=\"canonical\" href=\"" + esc_attr(rel) + "\">\n"
                "  <meta http-equiv=\"refresh\" content=\"0; url=" + esc_attr(rel) + "\">\n"
                "  <script>location.replace(\"" + esc_attr(rel) + "\");</script>\n"
                "</head>\n<body><a href=\"" + esc_attr(rel) + "\">" + esc(pages[i].title) + "</a></body>\n</html>\n";
            std::ofstream(af) << rd;
        }
        // 插件钩子：每页渲染写盘后（有插件时整体串行，保证时序与安全；
        // 路径用绝对路径，插件进程 cwd 是插件目录，相对路径会解析失败）
        if (hasPlugs)
            run_plugin_hooks("on_page_rendered", json{
                {"file",   pages[i].file},
                {"locale", multi ? loc : ""},
                {"path",   fs::absolute(pageOut).string()}
            });
    };
    // 阶段 1 暂存数组已在上面声明（crumbsStore 等）
    if (hasPlugs) { for (size_t i = 0; i < pages.size(); ++i) render_content(i); }
    else run_parallel(pages.size(), render_content);
    if (hasPlugs) { for (size_t i = 0; i < pages.size(); ++i) emit_page(i); }
    else run_parallel(pages.size(), emit_page);
}

void render_blog(BuildContext& b, const LocaleRenderCtx& rc) {

    const SiteConfig& cfg = b.cfg;
    const I18nCfg& i18n = b.i18n;
    std::vector<Page>& pages = b.pages;
    const RenderOpts& opt = b.opt;
    const fs::path& out_dir = b.out_dir;
    fs::path in_dir = b.in_dir;
    const bool includeDrafts = b.includeDrafts;
    const json& maps = rc.maps;
    const I18nDict& dict = rc.dict;
    const std::string& i18nJson = rc.i18nJson;
    const std::string& curLocale = rc.curLocale;
    const std::string& homeBase = rc.homeBase;
    const std::string& feedTitle = rc.feedTitle;
    const std::string& ogImageUrl = rc.ogImageUrl;
    const json& langData = rc.langData;
    const bool multi = i18n.enabled;
    std::error_code ec;
    const std::string& loc = rc.curLocale;
    const fs::path& locOut = rc.locOut;
    // 8.5) 博客流（约定优于配置：md/blog/ 存在时启用）
    //      详情页 blog/<name>.html（面包屑=首页/博客/标题，上下篇=博客邻篇）
    //      + 列表页 blog/index.html + 分页 blog/page/N.html（每页 10 篇）
    if (b.query_ready && b.query_out.contains("blog_order") && b.query_out["blog_order"].is_array()
        && !b.query_out["blog_order"].empty()) {
        fs::path blogDir = in_dir / "blog";
        {
            std::error_code bec2;
            if (!(fs::exists(blogDir, bec2) && fs::is_directory(blogDir, bec2))) {
                fs::path rootBlog = in_dir.parent_path() / "blog";
                if (fs::exists(rootBlog, bec2) && fs::is_directory(rootBlog, bec2))
                    blogDir = rootBlog;
            }
        }
        fs::create_directories(rc.locOut / "blog", ec);
        // 文章渲染顺序由插件 blog_order（有序 file 列表）决定；引擎只做渲染
        std::map<std::string, Page> pgMap;
        for (const auto& bp : b.blog_posts) pgMap[bp.file] = bp;
        const auto& qOrder = b.query_out["blog_order"];
        for (size_t bi = 0; bi < qOrder.size(); ++bi) {
            auto pit = pgMap.find(qOrder[bi].get<std::string>());
            if (pit == pgMap.end()) continue;
            Page p = pit->second;
            if (p.draft && !b.includeDrafts) continue;
            std::string rel = p.file.substr(5);          // "blog/xxx" → "xxx"
            fs::path f = blogDir / (rel + ".md");
            if (multi) {
                fs::path fl = blogDir / (rel + "." + loc + ".md");
                if (fs::exists(fl)) f = fl;
            }
            if (!fs::exists(f)) continue;
            std::string mdRaw = read_file(f);
            std::string md;
            parse_front_matter(mdRaw, md);
            p.html = render_doc_body(md, false);   // blog 正文跨语言共享，用默认中文标题
            if (p.title.empty()) p.title = extract_title(md, rel);
            std::string excerpt = collapse_ws(strip_tags(p.html));
            if (excerpt.size() > 160) excerpt = truncate_utf8(excerpt, 160) + "…";
            p.desc = excerpt;
            // 面包屑数据（Breadcrumb 组件；博客详情页在 blog/ 下，链接相对本目录）
            json bcJson = json::array();
            bcJson.push_back(json{{"title", "{{home}}"}, {"href", "../index.html"}, {"current", false}});
            bcJson.push_back(json{{"title", "{{navBlog}}"}, {"href", "index.html"}, {"current", false}});
            bcJson.push_back(json{{"title", p.title}, {"href", ""}, {"current", true}});
            json bcMap = json::object();
            bcMap["links"] = json::array();
            bcMap["texts"] = json::array();
            for (const auto& item : bcJson) {
                if (item.value("current", false)) bcMap["current"] = item["title"];
                else if (!item.value("href", "").empty()) bcMap["links"].push_back(item);
                else bcMap["texts"].push_back(item);
            }
            if (!bcMap.contains("current")) bcMap["current"] = "";
            // 元信息：发布日期 + 阅读时长（纯文本，LastUpdated 组件包 <div class="page-meta">）
            auto [cjk, words] = count_words(strip_tags(p.html));
            int mins = (int)std::ceil(cjk / 300.0 + words / 200.0);
            if (mins < 1) mins = 1;
            std::string pub = format_date_local(p.dateT);   // 本地时区，避免 UTC 倒退一天
            std::string rt = rc.dict.count("readingTime") ? rc.dict.at("readingTime")
                                                        : "约 {{minutes}} 分钟阅读（{{words}} 字）";
            rt = subst_tokens(rt, {{"minutes", std::to_string(mins)}, {"words", std::to_string(cjk + words)}});
            std::string meta = esc(pub) + " · " + rt;
            // 上下篇数据（博客邻篇，缺位置灰——行业标准；链接相对 blog/ 目录）
            // 邻篇 = 插件排序结果的前后项（机械索引，非查询）
            json pagerBj;
            pagerBj["show"] = (bi > 0) || (bi + 1 < qOrder.size());
            if (bi > 0) {
                auto pit2 = pgMap.find(qOrder[bi - 1].get<std::string>());
                if (pit2 != pgMap.end())
                    pagerBj["prev"] = json{{"show", true}, {"title", pit2->second.title},
                                           {"href", pit2->second.file.substr(5) + ".html"}};
                else pagerBj["prev"] = json{{"show", false}};
            } else pagerBj["prev"] = json{{"show", false}};
            if (bi + 1 < qOrder.size()) {
                auto nit = pgMap.find(qOrder[bi + 1].get<std::string>());
                if (nit != pgMap.end())
                    pagerBj["next"] = json{{"show", true}, {"title", nit->second.title},
                                           {"href", nit->second.file.substr(5) + ".html"}};
                else pagerBj["next"] = json{{"show", false}};
            } else pagerBj["next"] = json{{"show", false}};
            // head 数据化（MetaLink/MetaOgItem/MetaNameItem/JsonLd 组件渲染）
            json hd = build_head_data(b, rc, p.file, 1, p.title, p.desc, p.dateT, p.dateT, true, {}, "", "");
            TocResult t = build_toc(p.html);
            // 博客详情页 → PageCtx
            PageCtx ctx;
            ctx.nav_groups = nav_groups_json(b.blogNav.empty() ? cfg.nav : b.blogNav, "", "../");
            ctx.head_meta = hd.value("meta", json::object());
            ctx.head_links = hd.value("links", json::array());
            ctx.jsonld = hd.value("jsonld", "");
            ctx.toc_items = t.items;
            ctx.pager = pagerBj;
            ctx.breadcrumb_map = bcMap;
            ctx.edit = json{{"show", false}};
            ctx.body = t.html;
            ctx.title = p.title;
            ctx.desc = p.desc;
            ctx.last_updated = meta;
            auto beIt2 = g_body_ends.find(rc.curLocale);
            ctx.body_end = (beIt2 != g_body_ends.end()) ? beIt2->second : "";
            ctx.curLocale = rc.curLocale; ctx.lang_data = rc.langData;
            ctx.i18nJson = rc.i18nJson; ctx.relBase = "../";
            std::string page = map_render_page(cfg, opt, ctx, type_for_mode(rc.maps, "blog-post", "blog-post"));
            std::ofstream(rc.locOut / "blog" / (rel + ".html"))
                << apply_fingerprints(cfg.compress ? wrap_webp(minify_html(i18n_replace(page, rc.dict)), rc.locOut) : i18n_replace(page, rc.dict));
        }
        // 列表页 + 分页：第一页 blog/index.html，后续 blog/page/N.html
        // （列表页在 blog/ 下 relBase="../"；分页页在 blog/page/ 下 relBase="../../"）
        // 分页分组由插件 blog_pages 决定（每页 file 数组）；引擎只渲染 + 生成分页导航
        {
            json qPages = b.query_out.value("blog_pages", json::array());
            if (!qPages.is_array()) qPages = json::array();
            size_t pagesN = qPages.size();
            for (size_t pi = 0; pi < pagesN; ++pi) {
                bool isPageSub = (pi > 0);                  // 分页页位于 blog/page/
                std::string relBase = isPageSub ? "../../" : "../";
                std::string cardBase = isPageSub ? "../" : "";   // 卡片链接前缀
                std::string navBase  = isPageSub ? "../" : "";   // 分页导航链接前缀
                // 博客列表数据（BlogList/BlogCard 组件渲染；分页 BlogPager 数据化）
                json posts = json::array();
                for (const auto& fl : qPages[pi]) {
                    auto pit = pgMap.find(fl.get<std::string>());
                    if (pit == pgMap.end()) continue;
                    const Page& p = pit->second;
                    posts.push_back(json{{"date", format_date_local(p.dateT)},
                                         {"href", cardBase + p.file.substr(5) + ".html"},
                                         {"title", p.title}, {"desc", p.desc}});
                }
                auto pageHref = [&](size_t pp) {
                    if (pp == 0) return navBase + "index.html";
                    return navBase + "page/" + std::to_string(pp + 1) + ".html";
                };
                json bp = json::object();
                bp["show"] = (pagesN > 1);
                if (pagesN > 1) {
                    if (pi > 0) bp["prev_href"] = pageHref(pi - 1);
                    if (pi + 1 < pagesN) bp["next_href"] = pageHref(pi + 1);
                    bp["cur"] = json{{"num", pi + 1}};
                    bp["pages"] = json::array();
                    for (size_t pp = 0; pp < pagesN; ++pp)
                        bp["pages"].push_back(json{{"num", pp + 1}, {"href", pageHref(pp)}});
                }
                // 博客列表页 → PageCtx（BlogList/BlogCard/BlogPager 组件渲染）
                PageCtx ctx;
                ctx.blog_posts = posts;
                ctx.blog_pager = bp;
                ctx.title = "{{blogTitle}}";
                ctx.desc = cfg.description;
            ctx.curLocale = rc.curLocale; ctx.lang_data = rc.langData;
            ctx.i18nJson = rc.i18nJson; ctx.relBase = relBase;
            ctx.nav_groups = nav_groups_json(b.blogNav.empty() ? cfg.nav : b.blogNav, "", relBase);
            std::string page = map_render_page(cfg, opt, ctx, type_for_mode(rc.maps, "blog-list", "blog"));
                if (pi == 0)
                    std::ofstream(rc.locOut / "blog" / "index.html")
                        << apply_fingerprints(cfg.compress ? wrap_webp(minify_html(i18n_replace(page, rc.dict)), rc.locOut) : i18n_replace(page, rc.dict));
                else {
                    fs::create_directories(rc.locOut / "blog" / "page", ec);
                    std::ofstream(rc.locOut / "blog" / "page" / (std::to_string(pi + 1) + ".html"))
                        << apply_fingerprints(cfg.compress ? wrap_webp(minify_html(i18n_replace(page, rc.dict)), rc.locOut) : i18n_replace(page, rc.dict));
                }
            }
        }
    }
}

void render_search_index(BuildContext& b, const LocaleRenderCtx& rc) {

    const SiteConfig& cfg = b.cfg;
    const I18nCfg& i18n = b.i18n;
    std::vector<Page>& pages = b.pages;
    const RenderOpts& opt = b.opt;
    const fs::path& out_dir = b.out_dir;
    fs::path in_dir = b.in_dir;
    const bool includeDrafts = b.includeDrafts;
    const json& maps = rc.maps;
    const I18nDict& dict = rc.dict;
    const std::string& i18nJson = rc.i18nJson;
    const std::string& curLocale = rc.curLocale;
    const std::string& homeBase = rc.homeBase;
    const std::string& feedTitle = rc.feedTitle;
    const std::string& ogImageUrl = rc.ogImageUrl;
    const json& langData = rc.langData;
    const bool multi = i18n.enabled;
    std::error_code ec;
    const std::string& loc = rc.curLocale;
    const fs::path& locOut = rc.locOut;
    // 8) 搜索索引（每语言独立，内容取自该语言正文；标题走 i18n 字典解析）
    {   // 合并博客文章进索引（file 带 blog/ 前缀，链接相对当前语言目录正确）
        std::vector<Page> allPages = pages;
        allPages.insert(allPages.end(), b.blog_posts.begin(), b.blog_posts.end());
        gen_search_index(allPages, b.includeDrafts, rc.dict, rc.locOut);
    }
}

void render_tags(BuildContext& b, const LocaleRenderCtx& rc) {

    const SiteConfig& cfg = b.cfg;
    const I18nCfg& i18n = b.i18n;
    std::vector<Page>& pages = b.pages;
    const RenderOpts& opt = b.opt;
    const fs::path& out_dir = b.out_dir;
    fs::path in_dir = b.in_dir;
    const bool includeDrafts = b.includeDrafts;
    const json& maps = rc.maps;
    const I18nDict& dict = rc.dict;
    const std::string& i18nJson = rc.i18nJson;
    const std::string& curLocale = rc.curLocale;
    const std::string& homeBase = rc.homeBase;
    const std::string& feedTitle = rc.feedTitle;
    const std::string& ogImageUrl = rc.ogImageUrl;
    const json& langData = rc.langData;
    const bool multi = i18n.enabled;
    std::error_code ec;
    const std::string& loc = rc.curLocale;
    const fs::path& locOut = rc.locOut;
    // 9) 标签聚合页：基于 front matter 的 tags，自动生成每个标签一个列表页 + 总览页
    //    （tags 页位于 tags/ 子目录：relBase="../"；博客文章也参与聚合，链接 ../blog/xxx.html）
    {
        if (b.query_ready && b.query_out.contains("tags") && b.query_out["tags"].is_array()
            && !b.query_out["tags"].empty()) {
            fs::create_directories(rc.locOut / "tags", ec);
            // 标签聚合由插件完成（tags 数组 + tag_pages 每标签 file 列表）；引擎只渲染
            std::map<std::string, const Page*> pgMap;
            for (const auto& p : pages) pgMap[p.file] = &p;
            for (const auto& p : b.blog_posts) pgMap[p.file] = &p;
            json tags = b.query_out["tags"];
            // tags 聚合页 → PageCtx（TagOverview/TagItem 组件渲染）
            PageCtx ctx;
            ctx.nav_groups = nav_groups_json(cfg.nav, "", "../");
            ctx.tags = tags;
            ctx.title = "{{allTags}}";
            ctx.desc = cfg.description;
            ctx.curLocale = rc.curLocale; ctx.lang_data = rc.langData;
            ctx.i18nJson = rc.i18nJson; ctx.relBase = "../";
            std::string ov = map_render_page(cfg, opt, ctx, type_for_mode(rc.maps, "tags", "tags"));
            std::ofstream(rc.locOut / "tags" / "index.html")
                << apply_fingerprints(cfg.compress ? wrap_webp(minify_html(i18n_replace(ov, rc.dict)), rc.locOut) : i18n_replace(ov, rc.dict));
            const json& tagPages = b.query_out.contains("tag_pages")
                                    ? b.query_out["tag_pages"] : json::object();
            for (auto it = tagPages.begin(); it != tagPages.end(); ++it) {
                const std::string& tname = it.key();
                // 标签单页数据（TagPage/TagDocItem 组件渲染；docs 顺序由插件给出）
                json docs = json::array();
                for (const auto& fl : it.value()) {
                    auto pit = pgMap.find(fl.get<std::string>());
                    if (pit == pgMap.end()) continue;
                    const Page& p = *pit->second;
                    std::string d = (p.file.size() > 5 && p.file.compare(0, 5, "blog/") == 0)
                                    ? format_date_local(p.dateT) : "";
                    docs.push_back(json{{"href", "../" + p.file + ".html"}, {"title", p.title},
                                        {"date", d}, {"desc", std::string()}});
                }
                PageCtx tctx;
                tctx.nav_groups = nav_groups_json(cfg.nav, "", "../");
                tctx.tag_name = tname;
                tctx.tag_docs = docs;
                tctx.title = "#" + tname;
                tctx.desc = cfg.description;
                tctx.curLocale = rc.curLocale; tctx.lang_data = rc.langData;
                tctx.i18nJson = rc.i18nJson; tctx.relBase = "../";
                std::string tp = map_render_page(cfg, opt, tctx, type_for_mode(rc.maps, "tag-page", "tag-page"));
                std::ofstream(rc.locOut / "tags" / (slugify(tname) + ".html"))
                    << apply_fingerprints(cfg.compress ? wrap_webp(minify_html(i18n_replace(tp, rc.dict)), rc.locOut) : i18n_replace(tp, rc.dict));
            }
        }
    }
}

// 插件市场 / 主题市场页（数据来自 .Cdocs/data/*-market.json 站点数据，
// 由短代码组件 PluginMarket/ThemeMarket 渲染列表 + 下载链接）
void render_markets(BuildContext& b, const LocaleRenderCtx& rc) {
    const SiteConfig& cfg = b.cfg;
    const RenderOpts& opt = b.opt;
    const json& maps = rc.maps;
    const I18nDict& dict = rc.dict;
    std::error_code ec;
    struct Mkt { const char* type; const char* mode; const char* title; };
    for (const auto& m : { Mkt{"plugin-market", "plugin-market", "插件市场"},
                           Mkt{"theme-market", "theme-market", "主题市场"} }) {
        PageCtx ctx;
        ctx.nav_groups = nav_groups_json(cfg.nav, "", "../");
        ctx.title = m.title;
        ctx.desc = cfg.description;
        ctx.curLocale = rc.curLocale; ctx.lang_data = rc.langData;
        ctx.i18nJson = rc.i18nJson; ctx.relBase = "../";
        std::string ov = map_render_page(cfg, opt, ctx, type_for_mode(maps, m.mode, m.type));
        fs::create_directories(rc.locOut / m.type, ec);
        std::ofstream(rc.locOut / m.type / "index.html")
            << apply_fingerprints(cfg.compress ? wrap_webp(minify_html(i18n_replace(ov, rc.dict)), rc.locOut)
                                               : i18n_replace(ov, rc.dict));
    }
}

void render_single(BuildContext& b, const LocaleRenderCtx& rc) {

    const SiteConfig& cfg = b.cfg;
    const I18nCfg& i18n = b.i18n;
    std::vector<Page>& pages = b.pages;
    const RenderOpts& opt = b.opt;
    const fs::path& out_dir = b.out_dir;
    fs::path in_dir = b.in_dir;
    const bool includeDrafts = b.includeDrafts;
    const json& maps = rc.maps;
    const I18nDict& dict = rc.dict;
    const std::string& i18nJson = rc.i18nJson;
    const std::string& curLocale = rc.curLocale;
    const std::string& homeBase = rc.homeBase;
    const std::string& feedTitle = rc.feedTitle;
    const std::string& ogImageUrl = rc.ogImageUrl;
    const json& langData = rc.langData;
    const bool multi = i18n.enabled;
    std::error_code ec;
    const std::string& loc = rc.curLocale;
    const fs::path& locOut = rc.locOut;
    // 10) single 通用单页（rc.maps 注册表 mode=single：404 + 第三方自定义单页统一入口。
    //     output 自定输出文件名（默认 <type>.html）；页面结构/内容由地图 + 地图 data + props 完全自定义）
    for (const auto& e : rc.maps) {
        if (!e.is_object() || e.value("mode", "") != "single") continue;
        std::string stype = e.value("type", "");
        if (stype.empty()) continue;
        std::string output = e.value("output", "");
        if (output.empty()) output = stype + ".html";
        PageCtx ctx;
        ctx.nav_groups = nav_groups_json(cfg.nav, "", "");
        ctx.title = (stype == "404") ? "404" : cfg.title;
        ctx.desc = cfg.description;
        ctx.curLocale = rc.curLocale; ctx.lang_data = rc.langData; ctx.i18nJson = rc.i18nJson;
        std::string sp = map_render_page(cfg, opt, ctx, stype);
        std::ofstream(rc.locOut / output)
            << apply_fingerprints(cfg.compress ? wrap_webp(minify_html(i18n_replace(sp, rc.dict)), rc.locOut) : i18n_replace(sp, rc.dict));
    }
}

