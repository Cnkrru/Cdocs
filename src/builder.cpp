// builder.cpp —— 构建编排、渲染部件与站点生命周期命令（自 main.cpp 原样搬迁）

#include "builder.hpp"
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
#include <thread>      // Hugo 式并发渲染：worker pool
#include <mutex>       // pageSig 并发写保护
#include <atomic>      // 任务计数器

// ---------------- 并发渲染 worker pool（Hugo 式：多核并行渲染页面） ----------------
// tasks 数量 < 2 或单核时退化为顺序执行；每个工作线程从原子计数器取任务。
static void run_parallel(size_t n_tasks, const std::function<void(size_t)>& fn) {
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

// ---------------- 渲染部件（file-local） ----------------

// 社交分享 + 文章结构化元信息（Open Graph / Twitter Card / article:*)
// ogUrl 为空时跳过需绝对地址的 og:url / og:image（通常由 config.url 驱动）
static std::string social_head(const SiteConfig& cfg, const std::string& title,
                                const std::string& desc, const std::string& ogUrl,
                                const std::string& ogImageUrl, std::time_t published,
                                std::time_t modified,                                 const std::string& loc, bool article) {
    // 摘要常以标题开头（如“介绍 欢迎…”），去掉标题前缀，避免 meta description 与标题重复
    std::string d = desc;
    auto ltrim = [](const std::string& s, size_t& i) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
    };
    size_t p = 0;
    ltrim(d, p);
    if (!title.empty() && d.compare(p, title.size(), title) == 0) {
        p += title.size();
        ltrim(d, p);   // 跳过标题后可能紧跟的空格/换行
        d = d.substr(p);
    }
    std::string s;
    if (!ogUrl.empty())
        s += "  <meta property=\"og:url\" content=\"" + esc_attr(ogUrl) + "\">\n";
    s += "  <meta property=\"og:type\" content=\"" + std::string(article ? "article" : "website") + "\">\n";
    s += "  <meta property=\"og:title\" content=\"" + esc_attr(title) + "\">\n";
    if (!d.empty())
        s += "  <meta property=\"og:description\" content=\"" + esc_attr(d) + "\">\n";
    if (!cfg.url.empty() && !cfg.title.empty())
        s += "  <meta property=\"og:site_name\" content=\"" + esc_attr(cfg.title) + "\">\n";
    if (!loc.empty())
        s += "  <meta property=\"og:locale\" content=\"" + esc_attr(loc) + "\">\n";
    if (!ogImageUrl.empty())
        s += "  <meta property=\"og:image\" content=\"" + esc_attr(ogImageUrl) + "\">\n";
    if (article) {
        if (published) s += "  <meta property=\"article:published_time\" content=\"" + iso8601(published) + "\">\n";
        if (modified)  s += "  <meta property=\"article:modified_time\" content=\"" + iso8601(modified) + "\">\n";
    }
    s += "  <meta name=\"twitter:card\" content=\"" + std::string(ogImageUrl.empty() ? "summary" : "summary_large_image") + "\">\n";
    s += "  <meta name=\"twitter:title\" content=\"" + esc_attr(title) + "\">\n";
    if (!d.empty())
        s += "  <meta name=\"twitter:description\" content=\"" + esc_attr(d) + "\">\n";
    if (!ogImageUrl.empty())
        s += "  <meta name=\"twitter:image\" content=\"" + esc_attr(ogImageUrl) + "\">\n";
    return s;
}

static std::string link_html(const Link& l, const std::string& extra = "",
                             const std::string& relBase = "") {
    if (l.external())
        return "<a href=\"" + esc_attr(l.url) + "\" target=\"_blank\" rel=\"noopener\""
               + (extra.empty() ? "" : " " + extra) + ">" + esc(l.title) + "</a>";
    return "<a href=\"" + esc_attr(relBase + l.file) + ".html\"" + (extra.empty() ? "" : " " + extra)
           + ">" + esc(l.title) + "</a>";
}

// 页眉：三栏布局（左 logo / 中搜索框居中 / 右按钮组）
static std::string render_header(const SiteConfig& cfg, const RenderOpts& opt,
                                 const std::string& curLocale, const std::string& localeSwitch,
                                 const std::vector<VersionCfg>& versions = {},
                                 const std::string& relBase = "",
                                 bool isHome = false) {
    std::ostringstream o;
    std::string logo = cfg.header.logo.empty() ? cfg.title : cfg.header.logo;
    o << "  <header class=\"topbar\">\n"
      << "    <div class=\"topbar-left\">\n";
    // 首页无左侧边栏（hero 布局）：移动端也不渲染汉堡按钮，行为与桌面一致
    if (!isHome)
        o << "      <button id=\"menu-toggle\" class=\"menu-toggle\" type=\"button\" aria-label=\"{{menuToggleLabel}}\"></button>\n";
    o << "      <a class=\"logo\" href=\"" << esc_attr(relBase) << "index.html\">" << esc(logo) << "</a>\n";
    if (!cfg.header.links.empty()) {
        o << "      <nav class=\"topnav\">\n";
        for (const auto& l : cfg.header.links)
            o << "        " << link_html(l, "", relBase) << "\n";
        o << "      </nav>\n";
    }
    o << "    </div>\n"
      << "    <div class=\"topbar-center\">\n";
    if (opt.showSearch)
        o << "      <input id=\"search\" class=\"search\" type=\"search\" placeholder=\"{{searchPlaceholder}}\" autocomplete=\"off\">\n";
    o << "    </div>\n"
      << "    <div class=\"topbar-right\">\n";
    // 页眉右侧导航（i18n 按钮左侧）：config header.nav 配置，最多渲染 6 个，超出不渲染
    if (!cfg.header.nav.empty()) {
        o << "      <nav class=\"topbar-nav\">\n";
        size_t n = std::min<size_t>(cfg.header.nav.size(), 6);
        for (size_t i = 0; i < n; ++i)
            o << "        " << link_html(cfg.header.nav[i], "", relBase) << "\n";
        o << "      </nav>\n";
    }
    if (!localeSwitch.empty())
        o << "      " << localeSwitch << "\n";
    // 版本化文档下拉（Docusaurus 风格）：current 页 → ../<name>/ 同级切换
    if (versions.size() > 1) {
        o << "      <div class=\"version-switch\" aria-label=\"版本\">\n"
          << "        <span class=\"vs-current\">" << esc(cfg.curVersionLabel.empty() ? cfg.curVersion : cfg.curVersionLabel)
          << "<svg viewBox=\"0 0 16 16\" aria-hidden=\"true\" class=\"vs-caret\"><path fill=\"currentColor\" d=\"M4 6l4 4 4-4z\"/></svg></span>\n"
          << "        <div class=\"vs-menu\">\n";
        for (const auto& v : versions) {
            if (v.name == cfg.curVersion) {
                o << "          <span class=\"vs-item current\">" << esc(v.label) << "</span>\n";
            } else {
                // 非当前版本统一相对上级目录，并保持当前语言：
                //   多语言：页面在 <version>/<locale>/ 下 → ../../<name>/<locale>/index.html
                //   单语言：页面在 <version>/ 下       → ../<name>/index.html
                std::string href = curLocale.empty()
                                   ? "../" + esc_attr(v.name) + "/index.html"
                                   : "../../" + esc_attr(v.name) + "/" + esc_attr(curLocale) + "/index.html";
                o << "          <a class=\"vs-item\" href=\"" << href << "\">" << esc(v.label) << "</a>\n";
            }
        }
        o << "        </div>\n      </div>\n";
    }
    if (opt.showThemeToggle)
        o << "      <button id=\"theme-toggle\" class=\"theme-toggle\" aria-label=\"{{themeToggleLabel}}\"></button>\n";
    // GitHub 图标在亮暗切换按钮右侧（用户布局约定：右区顺序 = 右侧nav · 语言 · 主题 · GitHub）
    if (!cfg.header.github.empty())
        o << "      <a class=\"gh-link\" href=\"" << esc_attr(cfg.header.github)
          << "\" target=\"_blank\" rel=\"noopener\" aria-label=\"GitHub\">"
          << "<svg viewBox=\"0 0 16 16\" aria-hidden=\"true\"><path fill=\"currentColor\" d=\"M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27.68 0 1.36.09 2 .27 1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.013 8.013 0 0016 8c0-4.42-3.58-8-8-8z\"/></svg></a>\n";
    o << "    </div>\n  </header>\n";
    return o.str();
}

// JSON-LD 面包屑（SEO 结构化数据，Google 富媒体结果支持）
// 名称需走 i18n 字典解析（crumbs / 当前页标题都是 {{key}}），因为页面级 i18n_replace 会跳过 <script> 块
static std::string jsonld_breadcrumb(const std::vector<std::string>& crumbs,
                                     const std::string& curTitle, const std::string& curFile,
                                     const std::string& base, const I18nDict& dict) {
    std::ostringstream items;
    int pos = 1;
    items << "      {\"@type\":\"ListItem\",\"position\":" << pos++ << ",\"name\":\""
          << esc_attr(i18n_replace("{{home}}", dict)) << "\",\"item\":\""
          << base << "index.html\"},\n";
    for (const auto& c : crumbs)
        items << "      {\"@type\":\"ListItem\",\"position\":" << pos++ << ",\"name\":\""
              << esc_attr(i18n_replace(c, dict)) << "\"},\n";
    items << "      {\"@type\":\"ListItem\",\"position\":" << pos++ << ",\"name\":\""
          << esc_attr(i18n_replace(curTitle, dict)) << "\",\"item\":\"" << base << esc_attr(curFile) << ".html\"}";
    return "  <script type=\"application/ld+json\">\n  {\"@context\":\"https://schema.org\","
           "\"@type\":\"BreadcrumbList\",\"itemListElement\":[\n" + items.str() + "\n  ]}\n  </script>\n";
}

// JSON-LD 站点信息（首页用，WebSite 类型，含站内搜索动作声明）
static std::string jsonld_website(const SiteConfig& cfg, const std::string& base, const I18nDict& dict) {
    return "  <script type=\"application/ld+json\">\n  {\"@context\":\"https://schema.org\","
           "\"@type\":\"WebSite\",\"name\":\"" + esc_attr(i18n_replace(cfg.title, dict)) + "\",\"url\":\"" + base
           + "index.html\"}\n  </script>\n";
}

// 编辑此页链接（指向仓库源 .md 的编辑地址，VitePress/Docusaurus 标配）
static std::string render_edit_link(const SiteConfig& cfg, const std::string& file) {
    if (cfg.editBase.empty()) return "";
    std::string url = cfg.editBase;
    if (!url.empty() && url.back() != '/') url += '/';
    std::string dir = cfg.editDocsDir;
    if (!dir.empty() && dir.back() != '/') dir += '/';
    url += dir + file + ".md";
    return "  <div class=\"edit-link\"><a href=\"" + esc_attr(url)
        + "\" target=\"_blank\" rel=\"noopener\">{{editThisPage}}</a></div>\n";
}

// 页脚
static std::string render_footer(const SiteConfig& cfg) {
    if (cfg.footer.text.empty() && cfg.footer.links.empty()) return "";
    std::ostringstream o;
    o << "  <footer class=\"site-footer\">\n    <div class=\"footer-inner\">\n";
    if (!cfg.footer.text.empty())
        o << "      <span class=\"footer-text\">" << esc(cfg.footer.text) << "</span>\n";
    if (!cfg.footer.links.empty()) {
        o << "      <nav class=\"footer-links\">\n";
        for (const auto& l : cfg.footer.links)
            o << "        " << link_html(l) << "\n";
        o << "      </nav>\n";
    }
    o << "    </div>\n  </footer>\n";
    return o.str();
}

// 左侧边栏（递归，支持多层嵌套，depth 通过 CSS 变量 --depth 控制缩进）
// 分组可折叠：含当前页的分组（及其祖先）默认展开，其余默认折叠（行业标准做法）
// relBase：子目录页（tags/、blog/）的链接前缀，如 "../"——页面在子目录时导航链接须相对上级
static std::string render_left_nav(const std::vector<NavNode>& nodes,
                                   const std::string& cur, int depth,
                                   const std::string& relBase = "") {
    std::ostringstream o;
    for (const auto& n : nodes) {
        std::string style = " style=\"--depth:" + std::to_string(depth) + "\"";
        if (n.is_group()) {
            bool open = cur.empty() || subtree_contains(n.children, cur);
            std::string depthAttr = " style=\"--depth:" + std::to_string(depth) + "\"";
            std::string itemStyle = " style=\"--depth:" + std::to_string(depth)
                                  + (open ? "\"" : "; display:none\"");
            // 折叠组按钮初始带 collapsed class：与内联 display:none、aria-expanded 保持一致，
            // 避免 JS classList.toggle 第一次点击只补 class、第二次才展开的问题。
            o << "      <button class=\"nav-group" << (open ? "" : " collapsed") << "\""
              << depthAttr << " type=\"button\""
              << " aria-expanded=\"" << (open ? "true" : "false") << "\">"
              << esc(n.title) << "<span class=\"caret\">▾</span></button>\n";
            o << "      <div class=\"nav-group-items\"" << itemStyle << ">\n";
            o << render_left_nav(n.children, cur, depth + 1, relBase);
            o << "      </div>\n";
        } else if (!n.file.empty()) {
            std::string cls = (n.file == cur) ? " class=\"active\" aria-current=\"page\"" : "";
            o << "      <a href=\"" << esc_attr(relBase + n.file) << ".html\"" << cls << style
              << " data-title=\"" << esc_attr(n.title) << "\">" << esc(n.title) << "</a>\n";
        } else if (!n.url.empty()) {
            // 外链（http/https）或锚点（#）原样输出；站内相对 URL（如 tags/、blog/ 入口）
            // 在子目录页需加 relBase，否则解析到子目录下 404
            bool external = n.url.rfind("http://", 0) == 0 || n.url.rfind("https://", 0) == 0
                         || n.url[0] == '#' || n.url.rfind("mailto:", 0) == 0;
            std::string href = external ? n.url : relBase + n.url;
            o << "      <a href=\"" << esc_attr(href) << "\" target=\"_blank\" rel=\"noopener\""
              << style << " data-title=\"" << esc_attr(n.title) << "\">" << esc(n.title) << "</a>\n";
        }
    }
    return o.str();
}

// ---------------- 整页模板（主题解耦） ----------------
// 主题规范：一个主题 = 一个文件夹 <engine>/theme/，含
//   theme.json          主题元数据（name/version/description/author…）
//   templates/layout.html  页面骨架（可被主题整体覆盖）
//   assets/             前端资源（css/js/pwa/icons，整目录拷入 dist/assets/）
// 引擎只生成数据子块（nav 树 / TOC / pager / 条件块），通过 {{key}} 占位符注入模板；
// 模板缺失 / 损坏时回退内置默认骨架（与 templates/layout.html 初始内容一致）。
// 旧结构兼容：引擎根下直接放 assets/ + templates/ 的旧版站点同样可构建。
// 模板占位符约定（不含 i18n 驼峰键，避免与 {{backToTop}} 等运行时键冲突）：
//   单值：{{lang}} {{theme}} {{title}}
//   子块：{{meta_desc}} {{highlight_css}} {{custom_head}} {{skip_link}} {{header}}
//         {{left_nav}} {{breadcrumb}} {{body}} {{last_updated}} {{edit_link}} {{pager}}
//         {{toc_sidebar}} {{footer}} {{back_to_top}} {{highlight_js}} {{search_js}}
//         {{i18n_json}} {{feedback_js}}

// 内置默认骨架（出厂主题；与 templates/layout.html 保持一致，作为兜底）
static const char* default_layout_html() {
    return
        "<!DOCTYPE html>\n"
        "<html lang=\"{{lang}}\" data-theme=\"{{theme}}\">\n<head>\n"
        "  <meta charset=\"utf-8\">\n"
        "  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
        "  <script>try{var t=localStorage.getItem('theme')||document.documentElement.getAttribute('data-theme')||'light';document.documentElement.setAttribute('data-theme',t);}catch(e){}</script>\n"
        "  <title>{{title}}</title>\n"
        "{{meta_desc}}  <link rel=\"stylesheet\" href=\"{{base}}assets/css/style.css\">\n"
        "{{highlight_css}}{{custom_head}}</head>\n<body{{body_class}}>\n"
        "{{skip_link}}\n"
        "{{header}}\n"
        "  <div class=\"layout\">\n"
        "    <aside class=\"sidebar left\">\n      <nav>\n"
        "{{left_nav}}      </nav>\n    </aside>\n"
        "    <main class=\"content\" id=\"main\">\n"
        "{{breadcrumb}}{{body}}{{last_updated}}{{edit_link}}{{pager}}    </main>\n"
        "{{toc_sidebar}}  </div>\n"
        "{{footer}}{{back_to_top}}{{highlight_js}}{{search_js}}{{i18n_json}}{{feedback_js}}  <script defer src=\"{{base}}assets/js/app.js\"></script>\n"
        "</body>\n</html>\n";
}

// 主题根目录：优先 <engine>/theme（新规范，一个主题=一个文件夹）；
// 不存在时回退引擎根（旧结构：assets/、templates/ 直接在引擎根下）以兼容已有站点。
static fs::path theme_root() {
    std::error_code ec;
    fs::path t = g_engine / "theme";
    if (fs::is_directory(t, ec)) return t;
    return g_engine;
}

// 读取主题布局模板（跟随 -c 引擎目录）；缺失 / 为空 / 缺少 {{body}} 时回退内置骨架
static std::string load_layout_template() {
    std::error_code ec;
    fs::path p = theme_root() / "templates" / "layout.html";
    if (!fs::exists(p, ec)) p = theme_root() / "layout.html";   // 兼容 layout.html 放主题根
    if (fs::exists(p, ec)) {
        std::string tpl = read_file(p);
        if (!tpl.empty() && tpl.find("{{body}}") != std::string::npos)
            return tpl;
    }
    return default_layout_html();
}

// 单遍占位符替换：未知 {{key}}（如 i18n 键）原样保留，替换值不递归处理
static std::string tpl_render(const std::string& tpl,
                              const std::vector<std::pair<std::string, std::string>>& kv) {
    std::string out;
    out.reserve(tpl.size() + 256);
    size_t i = 0;
    while (i < tpl.size()) {
        if (tpl[i] == '{' && i + 1 < tpl.size() && tpl[i + 1] == '{') {
            size_t end = tpl.find("}}", i + 2);
            if (end != std::string::npos) {
                std::string key = tpl.substr(i + 2, end - i - 2);
                bool hit = false;
                for (const auto& p : kv)
                    if (p.first == key) { out += p.second; hit = true; break; }
                if (hit) { i = end + 2; continue; }
            }
        }
        out += tpl[i];
        ++i;
    }
    return out;
}

// 去掉末尾一个换行（模板行尾负责行结束的占位符使用）
static std::string rtrim_nl(std::string s) {
    if (!s.empty() && s.back() == '\n') {
        s.pop_back();
        if (!s.empty() && s.back() == '\r') s.pop_back();
    }
    return s;
}

// 全量替换子串（子目录 relBase 修正语言切换链接用）
static std::string replace_all(std::string s, const std::string& from, const std::string& to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

// 整页渲染：读模板 + 注入子块（子块换行规格与原 render_page 输出逐字节一致）
// relBase：页面相对语言根的目录前缀（""=语言根；"../"=tags/或blog/；"../../"=blog/page/），
//          模板资源 / header / 导航链接据此定位到正确的 assets 与页面
static std::string render_page(const SiteConfig& cfg, const RenderOpts& opt,
                               const std::string& leftNav, const std::string& body,
                               const std::string& toc, const std::string& pager,
                               const std::string& extraHead,
                               const std::string& pageTitle,
                               const std::string& pageDesc,
                               const std::string& breadcrumb,
                               const std::string& lastUpdated,
                               const std::string& editLink,
                               const std::string& curLocale = "",
                               const std::string& localeSwitch = "",
                               const std::string& i18nJson = "",
                               const std::string& relBase = "",
                               bool isHome = false) {
    std::string title = pageTitle.empty() ? cfg.title : (pageTitle + " · " + cfg.title);
    std::string lang = curLocale.empty() ? "zh-CN" : curLocale;
    // 首页标记：body class（CSS/JS 据此区分首页与内容页——首页无侧边栏）
    std::string bodyClass = isHome ? " class=\"page-home\"" : "";
    // head 条件块
    std::string metaDesc = pageDesc.empty() ? std::string()
        : "  <meta name=\"description\" content=\"" + esc_attr(pageDesc) + "\">\n";
    std::string hlCss = opt.showCodeHighlight
        ? "  <link rel=\"stylesheet\" href=\"" + relBase + "assets/deps/highlight-theme.css\">\n" : std::string();
    // 跳到主内容：随语言本地化（a11y 文本，对屏幕阅读器可见）；行尾换行由模板提供
    std::string skipLabel = (curLocale == "en") ? "Skip to main content" : "跳到主要内容";
    std::string skipLink = "<a class=\"skip-link\" href=\"#main\" aria-label=\""
        + esc_attr(skipLabel) + "\">" + esc(skipLabel) + "</a>";
    // 语言切换链接在子目录页需加深一级：href="../<loc>/" → relBase+"../<loc>/"
    std::string lsFinal = localeSwitch;
    if (!lsFinal.empty() && !relBase.empty())
        lsFinal = replace_all(lsFinal, "href=\"../", "href=\"" + relBase + "../");
    // 页眉（去尾换行，模板行尾提供；relBase 修正 logo/nav/语言切换链接；首页不渲染汉堡）
    std::string header = rtrim_nl(render_header(cfg, opt, curLocale, lsFinal, cfg.versions, relBase, isHome));
    std::string foot = render_footer(cfg);
    std::string tocSide = (opt.showToc && !toc.empty())
        ? "    <aside class=\"sidebar right\">\n" + toc + "    </aside>\n" : std::string();
    // 返回顶部按钮（纯 SVG 环形进度 + 箭头，无文字）；label 仅作 aria-label（默认走 i18n {{backToTop}}）
    std::string backTop;
    if (opt.showBackToTop) {
        std::string btl = cfg.backToTopLabel;
        std::string btlAttr = (btl == "↑ 顶部") ? std::string("{{backToTop}}") : esc_attr(btl);
        backTop = "  <button id=\"back-to-top\" class=\"back-to-top\" data-threshold=\""
            + std::to_string(cfg.backToTopThreshold) + "\" aria-label=\"" + btlAttr + "\">\n"
            "    <svg viewBox=\"0 0 40 40\" aria-hidden=\"true\">\n"
            "      <circle class=\"btt-track\" cx=\"20\" cy=\"20\" r=\"17\" fill=\"none\"/>\n"
            "      <circle class=\"btt-progress\" cx=\"20\" cy=\"20\" r=\"17\" fill=\"none\"/>\n"
            "      <path class=\"btt-arrow\" d=\"M20 26V14M14 20l6-6 6 6\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"2.4\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>\n"
            "    </svg>\n"
            "  </button>\n";
    }
    // 脚本条件块（relBase 修正资源路径）
    std::string hlJs = opt.showCodeHighlight
        ? "  <script defer src=\"" + relBase + "assets/deps/highlight.min.js\"></script>\n" : std::string();
    std::string searchJs = opt.showSearch
        ? "  <script defer src=\"" + relBase + "assets/deps/flexsearch.bundle.min.js\"></script>\n" : std::string();
    std::string i18nScript = i18nJson.empty() ? std::string()
        : "  <script>window.__I18N__ = " + i18nJson + ";</script>\n";
    std::string fbScript = cfg.feedbackEndpoint.empty() ? std::string()
        : "  <script>window.__CDOCS_FEEDBACK__ = \"" + esc_attr(cfg.feedbackEndpoint) + "\";</script>\n";
    // 条件追加换行的子块（与原输出逐字节一致）
    std::string bc = breadcrumb.empty() ? std::string() : breadcrumb + "\n";
    std::string lu = lastUpdated.empty() ? std::string() : lastUpdated + "\n";
    std::string pg = (opt.showPager && !pager.empty()) ? pager : std::string();
    // 正文末尾注入（插件 on_config 提供，如 giscus 评论；追加在 pager 之后、</main> 之前）
    auto bodyIt = g_body_ends.find(curLocale);
    std::string bodyEnd = (bodyIt != g_body_ends.end()) ? bodyIt->second : std::string();
    // 单遍注入模板
    std::string out = tpl_render(load_layout_template(), {
        {"lang", esc_attr(lang)}, {"theme", esc(cfg.theme)}, {"title", esc(title)},
        {"base", relBase}, {"body_class", bodyClass},
        {"meta_desc", metaDesc}, {"highlight_css", hlCss},
        {"custom_head", cfg.customCssLink + cfg.themeVars + extraHead},
        {"skip_link", skipLink}, {"header", header}, {"left_nav", leftNav},
        {"breadcrumb", bc}, {"body", body + "\n"},
        {"last_updated", lu}, {"edit_link", editLink}, {"pager", pg + bodyEnd},
        {"toc_sidebar", tocSide}, {"footer", foot}, {"back_to_top", backTop},
        {"highlight_js", hlJs}, {"search_js", searchJs},
        {"i18n_json", i18nScript}, {"feedback_js", fbScript}
    });
    // 无左侧边栏（首页默认）：移除空 aside，layout 标记 no-sidebar（content 居中）。
    // 仅匹配内置骨架的空侧栏形态；自定义模板不一致时原样保留（fallback 空侧栏无害）。
    if (leftNav.empty()) {
        const char* emptyAside = "    <aside class=\"sidebar left\">\n      <nav>\n      </nav>\n    </aside>\n";
        size_t pa = out.find(emptyAside);
        if (pa != std::string::npos) out.replace(pa, std::strlen(emptyAside), "");
        size_t pl = out.find("<div class=\"layout\">");
        if (pl != std::string::npos)
            out.replace(pl, std::strlen("<div class=\"layout\">"), "<div class=\"layout no-sidebar\">");
    }
    return out;
}

// 首页 Hero + 文档卡片
// config.home 可配：hero（title/subtitle/cta 覆盖）+ cards 白名单（顺序即展示顺序）；
// 未配置 home 时保持自动：Hero 取 config.title/description，卡片列出全部非草稿页。
static std::string landing_body(const SiteConfig& cfg, const std::vector<Page>& pages) {
    std::ostringstream o;
    std::string hTitle = cfg.homeTitle.empty() ? cfg.title : cfg.homeTitle;
    std::string hSub   = cfg.homeSubtitle.empty() ? cfg.description : cfg.homeSubtitle;
    o << "<section class=\"hero\">\n  <h1>" << esc(hTitle) << "</h1>\n";
    if (!hSub.empty())
        o << "  <p class=\"subtitle\">" << esc(hSub) << "</p>\n";
    // CTA：home.hero.cta 优先；否则指向第一篇文档（自动模式）
    std::string ctaFile = cfg.homeCtaFile;
    std::string ctaText = cfg.homeCtaText.empty() ? std::string("{{getStarted}}") : cfg.homeCtaText;
    if (ctaFile.empty() && !pages.empty()) ctaFile = pages[0].file;
    if (!ctaFile.empty())
        o << "  <a class=\"cta\" href=\"" << esc_attr(ctaFile) << ".html\">" << esc(ctaText) << "</a>\n";
    o << "</section>\n";
    // 卡片：白名单模式（home.cards 非空）优先；否则自动列出全部非草稿页
    std::vector<const Page*> shown;
    if (!cfg.homeCards.empty()) {
        for (const auto& hc : cfg.homeCards) {
            const Page* found = nullptr;
            for (const auto& p : pages)
                if (!p.draft && p.file == hc.file) { found = &p; break; }
            if (!found) {
                std::cerr << color::warn("提示: ") << "home.cards 引用了不存在的页面: "
                          << hc.file << "（已跳过）\n";
                continue;
            }
            shown.push_back(found);
        }
    } else {
        for (const auto& p : pages) if (!p.draft) shown.push_back(&p);
    }
    if (!shown.empty()) {
        o << "<section class=\"cards\">\n";
        for (const auto* p : shown) {
            std::string t = p->title;
            std::string d;
            for (const auto& hc : cfg.homeCards)
                if (hc.file == p->file) { if (!hc.title.empty()) t = hc.title; d = hc.desc; break; }
            o << "  <a class=\"card\" href=\"" << esc_attr(p->file) << ".html\"><h3>" << esc(t) << "</h3>";
            if (!d.empty()) o << "<p class=\"card-desc\">" << esc(d) << "</p>";
            o << "</a>\n";
        }
        o << "</section>\n";
    }
    return o.str();
}

// 上一页 / 下一页（上下篇）
// 行业标准（Docusaurus 同款）：无上篇/下篇时保留按钮占位，置灰（disabled）
// 并显示「暂无」，避免布局跳动、保持导航结构完整。
static std::string pager_html(const std::vector<Page>& pages, size_t i,
                              const std::string& relBase = "") {
    std::ostringstream o;
    o << "<nav class=\"pager\">\n";
    if (i > 0)
        o << "  <a class=\"pager-item prev\" href=\"" << relBase << pages[i - 1].file << ".html\">\n"
          << "    <span class=\"pager-label\">{{prevLabel}}</span>\n"
          << "    <span class=\"pager-title\">" << esc(pages[i - 1].title) << "</span>\n  </a>\n";
    else
        o << "  <span class=\"pager-item prev disabled\" aria-disabled=\"true\">\n"
          << "    <span class=\"pager-label\">{{prevLabel}}</span>\n"
          << "    <span class=\"pager-title\">{{pagerNone}}</span>\n  </span>\n";
    if (i + 1 < pages.size())
        o << "  <a class=\"pager-item next\" href=\"" << relBase << pages[i + 1].file << ".html\">\n"
          << "    <span class=\"pager-label\">{{nextLabel}}</span>\n"
          << "    <span class=\"pager-title\">" << esc(pages[i + 1].title) << "</span>\n  </a>\n";
    else
        o << "  <span class=\"pager-item next disabled\" aria-disabled=\"true\">\n"
          << "    <span class=\"pager-label\">{{nextLabel}}</span>\n"
          << "    <span class=\"pager-title\">{{pagerNone}}</span>\n  </span>\n";
    o << "</nav>\n";
    return o.str();
}

// ---------------- 构建编排 ----------------

// ============ 构建上下文（run_build 各阶段共享的状态） ============
// 让 run_build 退化为纯编排：每个阶段函数只需一份上下文，不再传递超长参数列表。
// 阶段函数体用局部引用别名（如 SiteConfig& cfg = b.cfg;）绑定上下文成员，
// 因此函数体与重构前 run_build 内联时逐字一致，行为零变化。
struct BuildContext {
    fs::path in_dir;
    fs::path out_dir;
    bool includeDrafts = false;
    SiteConfig cfg;
    I18nCfg i18n;
    I18nDict fallbackUI;
    RenderOpts opt;
    std::vector<Page> pages;
    std::vector<Page> blog_posts;          // 博客流：docs/blog/ 收集的文章（按 date 倒序）
    std::vector<NavNode> blogNav;          // 博客区独立侧边栏（config.sidebar["blog"]；空 = 沿用文档导航）
    // ---- 增量构建状态（--watch 热重载加速；普通 build 全量） ----
    bool incremental = false;              // 本次构建是否启用增量（serve -w 置位）
    std::map<std::string, std::string> pageSig;  // file+loc -> "mtime:size"（源 .md 指纹）
    bool globalDirty = true;               // 配置/导航/i18n/模板任一变化 → 全量重建
};

// ---------------- 构建阶段（run_build 的拆分，file-local） ----------------

// 1) 载入站点配置：config.json + 侧边栏（sidebar/ 分文件 + site.sidebar 映射，未配置回退 route.json）
static void load_site_config(BuildContext& b) {
    SiteConfig& cfg = b.cfg;
    I18nCfg& i18n = b.i18n;
    I18nDict& fallbackUI = b.fallbackUI;
    RenderOpts& opt = b.opt;
    const fs::path& out_dir = b.out_dir;

    fs::path cfg_path = g_engine / "config/config.json";
    fs::path route_path = g_engine / "config/route.json";

    // 1) 站点配置 config.json
    //    三区块结构（head / center / footer）+ 全局 site 段：
    //      { "site": {全局}, "head": {页眉}, "center": {内容/功能}, "footer": {页脚} }
    //    兼容旧式顶层结构：无 "site" 键时整份当作旧结构解析（header/footer 顶层字段照旧可用）。
    if (fs::exists(cfg_path)) {
        try {
        json j = json::parse(read_file(cfg_path));
        json site = j.contains("site") && j["site"].is_object() ? j["site"] : j;   // 全局段（兼容旧顶层）
        json hdr  = j.contains("head")   && j["head"].is_object()   ? j["head"]
                  : j.contains("header") && j["header"].is_object() ? j["header"] : json::object();
        json ctr  = j.contains("center") && j["center"].is_object() ? j["center"] : json::object();
        json ftr  = j.contains("footer") && j["footer"].is_object() ? j["footer"] : json::object();

        if (site.contains("title"))       cfg.title = site["title"].get<std::string>();
        if (site.contains("description")) cfg.description = site["description"].get<std::string>();
        if (site.contains("theme"))       cfg.theme = site["theme"].get<std::string>();
        if (site.contains("url"))         cfg.url = site["url"].get<std::string>();
        if (site.contains("ogImage"))     cfg.ogImage = site["ogImage"].get<std::string>();
        if (site.contains("compress") && site["compress"].is_boolean())
            cfg.compress = site["compress"].get<bool>();          // 默认开启
        else if (site.contains("compress"))
            std::cerr << color::yellow("  [config] site.compress 应为布尔值（true/false）\n");
        if (site.contains("jpegQuality") && site["jpegQuality"].is_number()) {
            cfg.jpegQuality = site["jpegQuality"].get<int>();     // 1-100
            if (cfg.jpegQuality < 1)  cfg.jpegQuality = 1;
            if (cfg.jpegQuality > 100) cfg.jpegQuality = 100;
        } else if (site.contains("jpegQuality"))
            std::cerr << color::yellow("  [config] site.jpegQuality 应为数字（1-100）\n");
        if (hdr.contains("logo"))            cfg.header.logo = hdr["logo"].get<std::string>();
        if (hdr.contains("showSearch"))      cfg.header.showSearch = hdr["showSearch"].get<bool>();
        if (hdr.contains("showThemeToggle")) cfg.header.showThemeToggle = hdr["showThemeToggle"].get<bool>();
        if (hdr.contains("github"))          cfg.header.github = hdr["github"].get<std::string>();
        if (hdr.contains("links")) for (auto& l : hdr["links"]) cfg.header.links.push_back(parse_link(l));
        if (hdr.contains("nav"))   for (auto& l : hdr["nav"])   cfg.header.nav.push_back(parse_link(l));
        if (ftr.contains("text"))  cfg.footer.text = ftr["text"].get<std::string>();
        if (ftr.contains("links")) for (auto& l : ftr["links"]) cfg.footer.links.push_back(parse_link(l));
        if (ctr.contains("plugins")) for (auto& p : ctr["plugins"]) cfg.plugins.push_back(p.get<std::string>());
        // 兼容旧结构：plugins 在顶层
        if (cfg.plugins.empty() && site.contains("plugins"))
            for (auto& p : site["plugins"]) cfg.plugins.push_back(p.get<std::string>());
        if (ctr.contains("backToTop")) {
            auto& b = ctr["backToTop"];
            if (b.contains("threshold")) cfg.backToTopThreshold = b["threshold"].get<int>();
            if (b.contains("label"))     cfg.backToTopLabel = b["label"].get<std::string>();
        }
        // 编辑此页链接（行业标准交互，指向仓库源文件编辑地址）
        if (site.contains("editLink")) {
            auto& e = site["editLink"];
            if (e.contains("base"))    cfg.editBase = e["base"].get<std::string>();
            if (e.contains("docsDir")) cfg.editDocsDir = e["docsDir"].get<std::string>();
        }
        // i18n（多语言）：行业标准 {{key}}+json 实现
        //   i18n.dir 下放置每份语言的扁平 JSON 字典（.Cdocs/i18n/zh-CN.json / en.json …）
        //   每个字典含全部 UI 文案键；正文里也可用 {{key}} 引用共享短语
        if (site.contains("i18n")) {
            i18n.enabled = true;
            auto& ic = site["i18n"];
            i18n.defaultLocale = ic.value("defaultLocale", std::string("zh-CN"));
            std::string ldir = ic.value("dir", std::string(".Cdocs/i18n"));
            if (ic.contains("locales"))
                for (auto it = ic["locales"].begin(); it != ic["locales"].end(); ++it) {
                    std::string loc = it.key();
                    std::string label = it.value().value("label", loc);
                    i18n.labels[loc] = label;
                    fs::path p = ldir + "/" + loc + ".json";
                    json d = json::object();
                    if (fs::exists(p)) {
                        try { d = json::parse(read_file(p)); } catch (...) { d = json::object(); }
                    }
                    I18nDict m;
                    for (auto kit = d.begin(); kit != d.end(); ++kit)
                        if (kit.value().is_string()) m[kit.key()] = kit.value().get<std::string>();
                    i18n.dicts[loc] = std::move(m);
                }
        }
        // 单语言模式兜底 UI 字典（与未开启 i18n 时原有中文文案一致）
        // 注意：这里是赋值外层别名（360 行 I18nDict& fallbackUI = b.fallbackUI;），
        // 不能重新声明——否则会遮蔽别名、b.fallbackUI 保持为空，单语言模式 UI 文案失效。
        fallbackUI = {
            {"siteTitle", "Cdocs 文档"},
            {"siteDesc", "一个用 C++ 编写、复用成熟组件（md4c + nlohmann/json + FlexSearch）的极简静态文档站点生成器。"},
            {"menuToggleLabel", "打开导航"}, {"searchPlaceholder", "搜索文档…"},
            {"themeToggleLabel", "切换主题"}, {"tocTitle", "本页目录"}, {"home", "首页"},
            {"getStarted", "开始阅读"}, {"prevLabel", "上一篇"}, {"nextLabel", "下一篇"},
            {"pagerNone", "暂无"},
            {"editThisPage", "编辑此页"}, {"lastUpdated", "最后更新于"},
            {"readingTime", "约 {{minutes}} 分钟阅读（{{words}} 字）"},
            {"backToTop", "顶部"}, {"notFoundTitle", "页面不见了"},
            {"notFoundDesc", "你访问的页面不存在或已被移动。"}, {"backHome", "返回首页"},
            {"brand", "Cdocs"}, {"navGitHub", "GitHub"}, {"navProject", "项目主页"},
            {"footerText", "© 2026 Cdocs · 基于 C++、md4c、nlohmann/json、FlexSearch 构建"},
            {"localeLabel", "语言"},
            {"copyCode", "复制代码"}, {"copy", "复制"}, {"copied", "已复制"},
            {"searchLoadFailed", "搜索数据加载失败"},
            {"useHttpServer", "：请用本地服务器(http)访问，不要直接以 file:// 打开本页"},
            {"noResults", "没有匹配「"}, {"noResultsSuffix", "」的结果"},
            {"navGettingStarted", "入门"}, {"navIntro", "介绍"}, {"navGuide", "使用指南"},
            {"navReference", "参考"}, {"navApi", "接口说明"}, {"navAdvanced", "进阶"},
            {"navArchitecture", "架构"}, {"navRendering", "渲染"}, {"navInternals", "底层实现"},
            {"navPipeline", "渲染管线"}, {"navRender", "渲染循环"}, {"allTags", "全部标签"},
            {"navBlog", "博客"}, {"blogTitle", "博客"}
        };
        // 公开 CSS 属性：用户可在 config.json 的 themeVars 覆盖任意变量
        if (site.contains("themeVars") && site["themeVars"].is_object()) {
            std::string body;
            for (auto it = site["themeVars"].begin(); it != site["themeVars"].end(); ++it)
                body += "  " + it.key() + ": " + it.value().get<std::string>() + ";\n";
            cfg.themeVars = "<style id=\"user-theme-vars\">\n:root, [data-theme=\"light\"], [data-theme=\"dark\"] {\n"
                            + body + "}\n</style>\n";
        }
        // 用户自定义 CSS 文件：存在则复制并链接
        if (site.contains("customCss")) {
            fs::path p = site["customCss"].get<std::string>();
            if (fs::exists(p)) {
                fs::create_directories(out_dir / "assets");
                fs::copy(p, out_dir / "assets" / p.filename(), fs::copy_options::overwrite_existing);
                cfg.customCssLink = "<link rel=\"stylesheet\" href=\"assets/" + p.filename().string() + "\">\n";
            }
        }
        // 首页（home 段）：hero 可选覆盖 + cards 白名单（不配则保持自动全列）
        if (site.contains("home")) {
            auto& hm = site["home"];
            if (hm.contains("hero")) {
                auto& h = hm["hero"];
                if (h.contains("title"))    cfg.homeTitle    = h["title"].get<std::string>();
                if (h.contains("subtitle")) cfg.homeSubtitle = h["subtitle"].get<std::string>();
                if (h.contains("cta") && h["cta"].is_object()) {
                    auto& c = h["cta"];
                    if (c.contains("text")) cfg.homeCtaText = c["text"].get<std::string>();
                    if (c.contains("file")) cfg.homeCtaFile = c["file"].get<std::string>();
                }
            }
            if (hm.contains("cards") && hm["cards"].is_array()) {
                for (auto& c : hm["cards"]) {
                    if (!c.is_object() || !c.contains("file")) continue;
                    HomeCardCfg card;
                    card.file = c["file"].get<std::string>();
                    if (c.contains("title")) card.title = c["title"].get<std::string>();
                    if (c.contains("desc"))  card.desc  = c["desc"].get<std::string>();
                    cfg.homeCards.push_back(std::move(card));
                }
            }
        }
        // 反馈上报端点（config.feedback.endpoint）：配了才启用真实统计，空 = 仅本地记忆
        if (site.contains("feedback") && site["feedback"].is_object()) {
            auto& fb = site["feedback"];
            if (fb.contains("endpoint") && fb["endpoint"].is_string())
                cfg.feedbackEndpoint = fb["endpoint"].get<std::string>();
        }
        // 版本化文档（config.versions，Docusaurus 风格）：
        //   { "versions": [ {"name":"v2","label":"2.0","source":"docs-v2","default":true}, ... ] }
        //   source 为空 = 用主 docs/ 目录；default 标记默认版本（根 index 重定向目标）
        if (site.contains("versions") && site["versions"].is_array()) {
            for (auto& v : site["versions"]) {
                if (!v.is_object() || !v.contains("name")) continue;
                VersionCfg vc;
                vc.name   = v["name"].get<std::string>();
                vc.label  = v.value("label", vc.name);
                vc.source = v.value("source", std::string());
                vc.default_v = v.value("default", false);
                cfg.versions.push_back(std::move(vc));
            }
            // 未标记 default 时取第一个版本
            if (!cfg.versions.empty() && !cfg.versions[0].default_v) {
                bool any = false;
                for (auto& v : cfg.versions) if (v.default_v) { any = true; break; }
                if (!any) cfg.versions[0].default_v = true;
            }
        }
        // 自动版本化（约定驱动）：config 未声明 versions，但 run_build 分派时
        // 通过 g_versions_json 把完整版本列表传给了子构建 → 填进 cfg.versions，
        // 供 render_header 渲染版本下拉。
        if (cfg.versions.empty() && !g_versions_json.empty()) {
            try {
                json va = json::parse(g_versions_json);
                if (va.is_array()) {
                    for (auto& v : va) {
                        VersionCfg vc;
                        vc.name = v.value("name", std::string());
                        vc.label = v.value("label", vc.name);
                        vc.source = v.value("source", std::string());
                        vc.default_v = v.value("default", false);
                        if (!vc.name.empty()) cfg.versions.push_back(std::move(vc));
                    }
                }
            } catch (...) { /* 忽略解析失败 */ }
        }
        // 多版本构建时注入当前版本信息（run_build 分派循环设置全局）
        cfg.curVersion = g_cur_version;
        cfg.curVersionLabel = g_cur_version_label;

        // ---- config schema 校验（对标成熟工具：未知字段/类型错误构建期提示，防拼写错误静默忽略）----
        auto warn_unknown = [&](const json& o, std::initializer_list<const char*> known, const char* sec) {
            if (!o.is_object()) return;
            for (auto it = o.begin(); it != o.end(); ++it) {
                bool okk = false;
                for (auto k : known) if (it.key() == k) { okk = true; break; }
                if (!okk && !g_quiet)
                    std::cerr << color::yellow("  [config] 未知字段 ") << sec << "." << it.key()
                              << color::muted("（拼写检查？）\n");
            }
        };
        warn_unknown(j,   {"site", "head", "header", "center", "footer"}, "$");
        warn_unknown(site, {"title", "description", "theme", "url", "ogImage", "editLink", "i18n",
                            "themeVars", "customCss", "home", "feedback", "versions", "compress",
                            "jpegQuality", "plugins", "sidebar"}, "site");
        warn_unknown(hdr, {"logo", "showSearch", "showThemeToggle", "github", "links", "nav"}, "head");
        warn_unknown(ctr, {"plugins", "backToTop", "comments"}, "center");
        warn_unknown(ftr, {"text", "links"}, "footer");
        if (site.contains("compress") && !site["compress"].is_boolean())
            std::cerr << color::yellow("  [config] site.compress 应为布尔值（true/false）\n");
        if (site.contains("jpegQuality") && !site["jpegQuality"].is_number())
            std::cerr << color::yellow("  [config] site.jpegQuality 应为数字（1-100）\n");
        // 侧边栏映射（site.sidebar；兼容顶层 sidebar）：key=版本源目录名或 "blog"，
        // value=相对 .Cdocs/config/ 的 JSON 路径。每个版本 / 博客区各一份独立侧边栏。
        auto read_sidebar_map = [&cfg](const json& obj) {
            if (!obj.is_object()) return;
            for (auto& [k, v] : obj.items())
                if (v.is_string()) cfg.sidebarMap[k] = v.get<std::string>();
        };
        read_sidebar_map(site.contains("sidebar") ? site["sidebar"] : json());
        read_sidebar_map(j.contains("sidebar") ? j["sidebar"] : json());
        } catch (const std::exception& e) {
            // config.json 损坏 / 字段类型错误：不崩溃，用默认配置继续并给出明确提示
            std::cerr << color::error("config.json 解析失败（已用默认配置继续）：") << e.what() << "\n";
        }
    }

    // 2) 侧边栏导航：优先 config.sidebar 映射（key = 版本源目录名，如 docs / docs-v1；
    //    每个版本一份独立 JSON，文件放 .Cdocs/config/sidebar/），未配置或文件缺失时
    //    回退全局 route.json（旧结构零回归）。
    // 博客区独立侧边栏：config.sidebar["blog"]（可选，缺省时博客页沿用文档导航）。
    {
        auto load_nav_file = [](const fs::path& p, std::vector<NavNode>& nav) {
            std::error_code ec;
            if (!fs::exists(p, ec)) return false;
            try {
                json r = json::parse(read_file(p));
                if (r.contains("sidebar") && r["sidebar"].is_array())
                    for (auto& item : r["sidebar"]) nav.push_back(parse_nav(item));
                return true;
            } catch (...) { return false; }
        };
        std::string navKey = b.in_dir.filename().string();   // "docs" / "docs-v1" ...
        fs::path navPath = route_path;
        auto it = cfg.sidebarMap.find(navKey);
        if (it != cfg.sidebarMap.end()) {
            fs::path p = g_engine / "config" / it->second;
            std::error_code e2;
            if (fs::exists(p, e2)) navPath = p;
        }
        if (fs::exists(navPath))
            load_nav_file(navPath, cfg.nav);
        auto bit = cfg.sidebarMap.find("blog");
        if (bit != cfg.sidebarMap.end()) {
            fs::path p = g_engine / "config" / bit->second;
            std::error_code e2;
            if (fs::exists(p, e2))
                load_nav_file(p, b.blogNav);
        }
    }

    // 3) 插件派生渲染开关
    opt.showSearch      = cfg.header.showSearch && has_plugin(cfg.plugins, "search");
    opt.showThemeToggle = cfg.header.showThemeToggle && has_plugin(cfg.plugins, "dark-mode");
    opt.showPager       = has_plugin(cfg.plugins, "pager");
    opt.showToc         = has_plugin(cfg.plugins, "toc");
    opt.showBackToTop   = has_plugin(cfg.plugins, "back-to-top");
    opt.showCodeHighlight = has_plugin(cfg.plugins, "code-highlight");
}

// 2) 输入检查 + 收集页面（route 导航，空则按文件名自动发现）+ 预扫描 front matter
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
    // 兜底：route.json 为空时按 docs/ 目录结构自动发现并生成导航
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
        bool hasTags = false;
        for (auto& pg : pages) if (!pg.draft && !pg.tags.empty()) { hasTags = true; break; }
        if (hasTags) {
            NavNode tagLink;
            tagLink.title = "标签";
            tagLink.url = "tags/index.html";
            cfg.nav.push_back(tagLink);
        }
        // 博客流（约定优于配置）：docs/blog/ 目录存在 → 收集为博客文章。
        // 与文档双区：blog/x.md（默认语言）+ blog/x.<loc>.md（其他语言），按 date 倒序；
        // 无 blog/ 目录 → 单文档站点，行为与旧版完全一致（零回归）。
        std::error_code sec2;
        fs::path blogDir = in_dir / "blog";
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
            std::sort(b.blog_posts.begin(), b.blog_posts.end(),
                      [](const Page& a, const Page& b) { return a.dateT > b.dateT; });
            if (!b.blog_posts.empty()) {
                NavNode blogLink;
                blogLink.title = "{{navBlog}}";
                blogLink.url = "blog/index.html";
                cfg.nav.push_back(blogLink);
            }
        }
    }
    return 0;   // 到达此处即收集成功
}

// 3) 多语言构建循环：每个语言输出到独立子目录（未开启 i18n 时单语言输出到根）
static void render_locales(BuildContext& b) {
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

    for (const auto& loc : locs) {
        bool multi = i18n.enabled;
        const I18nDict& dict = multi ? i18n.dicts[loc] : fallbackUI;
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

        // 静态资源发布：docs/ 下图片/附件等非 Markdown 文件，按相对路径拷到本语言目录
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
        std::string localeSwitch;
        if (multi) {
            std::ostringstream sw;
            sw << "<div class=\"locale-switch\" aria-label=\"{{localeLabel}}\">";
            for (auto& kv : i18n.labels) {
                if (kv.first == loc)
                    sw << "<span class=\"loc current\">" << esc(kv.second) << "</span>";
                else
                    sw << "<a href=\"../" << esc_attr(kv.first) << "/index.html\">" << esc(kv.second) << "</a>";
            }
            sw << "</div>";
            localeSwitch = sw.str();
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
        auto alt_links = [&](const std::string& file, int depth) {
            if (!i18n.enabled) return std::string();
            std::string up;
            for (int k = 0; k < depth + 1; ++k) up += "../";
            std::string s;
            for (auto& kv : i18n.labels) {
                if (kv.first == loc) continue;
                std::string ou;
                if (cfg.url.empty()) ou = up + kv.first + "/" + file + ".html";
                else { ou = cfg.url; if (!ou.empty() && ou.back() != '/') ou += '/';
                       ou += kv.first + "/" + file + ".html"; }
                s += "  <link rel=\"alternate\" hreflang=\"" + esc_attr(kv.first) + "\" href=\"" + ou + "\">\n";
            }
            std::string du;
            if (cfg.url.empty()) du = up + i18n.defaultLocale + "/" + file + ".html";
            else { du = cfg.url; if (!du.empty() && du.back() != '/') du += '/';
                   du += i18n.defaultLocale + "/" + file + ".html"; }
            s += "  <link rel=\"alternate\" hreflang=\"x-default\" href=\"" + du + "\">\n";
            return s;
        };

        // 6) 首页（默认无左侧边栏：hero + 卡片居中，config.home 可配内容；isHome 标记页眉/移动端一致）
        std::string landing = render_page(cfg, opt, "", landing_body(cfg, pages),
                                          "", "", jsonld_website(cfg, homeBase, dict) + alt_links("index", 0)
                                          + feedLinkTag + manifestTag
                                          + social_head(cfg, feedTitle, cfg.description,
                                                        homeBase + "index.html", ogImageUrl, 0, 0, loc, false),
                                          "", cfg.description, "", "", "", curLocale, localeSwitch, i18nJson,
                                          "" /*relBase*/, true /*isHome*/);
        std::ofstream(locOut / "index.html")
            << apply_fingerprints(cfg.compress ? wrap_webp(minify_html(i18n_replace(landing, dict)), locOut) : i18n_replace(landing, dict));

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
        std::vector<std::string> crumbsStore(pages.size()), metaStore(pages.size()),
                                 editStore(pages.size()), headStore(pages.size());
        auto render_content = [&](size_t i) {
            if (pages[i].draft && !includeDrafts) return;   // 草稿默认不发布；-D/--drafts 时包含
            // 内容翻译：优先 docs/<file>.<loc>.md（多语言），否则退回默认 .md（部分翻译）
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
                    fs::path existingOut = locOut / (pages[i].file + ".html");
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
            pages[i].html  = render_admonitions(markdown_to_html(md), curLocale == "en");
            if (pages[i].title.empty()) pages[i].title = extract_title(md, pages[i].file);
            pages[i].lastmod  = fm.lastmod;
            pages[i].aliases  = fm.aliases;
            TocResult toc = build_toc(pages[i].html);
            tocHtml[i] = toc.html; tocNav[i] = toc.toc;
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
            std::ostringstream bc;
            bc << "  <nav class=\"breadcrumb\" aria-label=\"面包屑\">\n"
               << "    <a href=\"" << up << "index.html\">{{home}}</a>\n";
            for (const auto& c : crumbs)
                bc << "    <span class=\"sep\">/</span>\n    <span class=\"crumb\">" << esc(c) << "</span>\n";
            bc << "    <span class=\"sep\">/</span>\n    <span class=\"crumb current\">" << esc(pages[i].title) << "</span>\n";
            bc << "  </nav>\n";
            // 最后更新时间 + 阅读时长（front matter lastmod 优先，否则源 .md 修改时间；数字走令牌插值）
            auto [cjk, words] = count_words(strip_tags(pages[i].html));
            int mins = (int)std::ceil(cjk / 300.0 + words / 200.0);
            if (mins < 1) mins = 1;
            std::string updated = pages[i].lastmod.empty() ? format_mtime(f) : pages[i].lastmod;
            std::string luPrefix = dict.count("lastUpdated") ? dict.at("lastUpdated") : "最后更新于";
            std::string rt = dict.count("readingTime") ? dict.at("readingTime")
                                                        : "约 {{minutes}} 分钟阅读（{{words}} 字）";
            rt = subst_tokens(rt, {{"minutes", std::to_string(mins)}, {"words", std::to_string(cjk + words)}});
            std::ostringstream meta;
            if (!updated.empty()) meta << luPrefix << " " << esc(updated) << " · ";
            meta << rt;
            std::string updatedHtml = "  <div class=\"page-meta\">" + meta.str() + "</div>\n";
            // 编辑此页链接（指向仓库源 .md）
            std::string editHtml = render_edit_link(cfg, pages[i].file);
            // SEO：canonical / 上下篇 rel / hreflang 交替 / 面包屑 JSON-LD
            // （prev/next 用 pages[].file——收集阶段已填、并发只写 html/desc/title/dateT，安全）
            std::string headExtra;
            int depth = 0;   // 页面相对语言根的目录深度（guide/install → 1），修正 RSS/manifest/hreflang 相对路径
            { size_t pos = 0; while ((pos = pages[i].file.find('/', pos)) != std::string::npos) { ++depth; ++pos; } }
            if (!cfg.url.empty()) {
                std::string u = homeBase;
                headExtra += "  <link rel=\"canonical\" href=\"" + u + esc_attr(pages[i].file) + ".html\">\n";
                if (i > 0)               headExtra += "  <link rel=\"prev\" href=\"" + u + esc_attr(pages[i-1].file) + ".html\">\n";
                if (i + 1 < pages.size()) headExtra += "  <link rel=\"next\" href=\"" + u + esc_attr(pages[i+1].file) + ".html\">\n";
                headExtra += alt_links(pages[i].file, depth);
                headExtra += jsonld_breadcrumb(crumbs, pages[i].title, pages[i].file, u, dict);
            }
            // 行业标准增强：RSS 候选链接 + PWA manifest + 社交分享/文章结构化元信息
            std::string ogUrl = cfg.url.empty() ? std::string()
                                                : homeBase + pages[i].file + ".html";
            // 子目录页面 RSS/manifest 需按深度加 ../（./rss.xml 在子目录下指向不存在）
            std::string fl = feedLinkTag, mt = manifestTag;
            if (depth > 0) {
                std::string up;
                for (int k = 0; k < depth; ++k) up += "../";
                fl = replace_all(fl, "href=\"./", "href=\"" + up);
                mt = replace_all(mt, "href=\"./", "href=\"" + up);
            }
            headExtra += fl + mt
                       + social_head(cfg, i18n_replace(pages[i].title, dict), pages[i].desc, ogUrl, ogImageUrl,
                                     pages[i].dateT, pages[i].dateT, loc, true);
            // 阶段 1 产物暂存到 pages[i] 之外（避免跨线程重读 pages 元素）：面包屑/元信息/headExtra
            // 用紧凑结构暂存，阶段 2 只读自身索引。
            // （为最小化改动，暂存经局部数组而非 Page 结构体扩容）
            crumbsStore[i] = bc.str();
            metaStore[i] = updatedHtml;
            editStore[i] = editHtml;
            headStore[i] = headExtra;
        };
        auto emit_page = [&](size_t i) {
            if (skip[i]) return;   // 增量跳过（阶段 1 已复用旧产物）
            // 子目录页面（如 guide/install.html）需要 ../ 前缀修正导航/资源相对路径
            std::string relBase;
            {
                size_t pos = 0;
                while ((pos = pages[i].file.find('/', pos)) != std::string::npos) { relBase += "../"; ++pos; }
            }
            std::string page = render_page(cfg, opt, render_left_nav(cfg.nav, pages[i].file, 0, relBase),
                                          tocHtml[i], tocNav[i], pager_html(pages, i, relBase), headStore[i],
                                          pages[i].title, pages[i].desc, crumbsStore[i], metaStore[i], editStore[i],
                                          curLocale, localeSwitch, i18nJson, relBase);
            fs::path pageOut = locOut / (pages[i].file + ".html");
            std::error_code pe2;
            fs::create_directories(pageOut.parent_path(), pe2);   // 子目录路由需建父目录
            std::ofstream(pageOut)
                << apply_fingerprints(cfg.compress ? wrap_webp(minify_html(i18n_replace(page, dict)), locOut) : i18n_replace(page, dict));
            // front matter aliases：为每个旧路径生成重定向页（canonical + meta refresh + JS 兜底，
            // 对标 Hugo aliases —— 文档改名后旧链接自动指向新页）
            for (const auto& a : pages[i].aliases) {
                std::string alias = a;
                while (!alias.empty() && (alias.front() == '/' || alias.front() == '\\')) alias = alias.substr(1);
                if (alias.empty()) continue;
                fs::path af = locOut / (alias + ".html");
                std::error_code aec;
                fs::create_directories(af.parent_path(), aec);
                std::error_code a2;
                fs::path tgt = fs::relative(pageOut, af.parent_path(), a2);
                std::string rel = a2 ? std::string("index.html") : tgt.generic_string();
                for (auto& c : rel) if (c == '\\') c = '/';
                std::string rd = "<!DOCTYPE html>\n<html lang=\"" + esc_attr(curLocale) + "\">\n<head>\n"
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

        // 8.5) 博客流（约定优于配置：docs/blog/ 存在时启用）
        //      详情页 blog/<name>.html（面包屑=首页/博客/标题，上下篇=博客邻篇）
        //      + 列表页 blog/index.html + 分页 blog/page/N.html（每页 10 篇）
        if (!b.blog_posts.empty()) {
            fs::path blogDir = in_dir / "blog";
            fs::create_directories(locOut / "blog", ec);
            for (size_t bi = 0; bi < b.blog_posts.size(); ++bi) {
                Page& p = b.blog_posts[bi];
                if (p.draft && !includeDrafts) continue;
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
                p.html = render_admonitions(markdown_to_html(md), false);   // blog 正文跨语言共享，用默认中文标题
                if (p.title.empty()) p.title = extract_title(md, rel);
                std::string excerpt = collapse_ws(strip_tags(p.html));
                if (excerpt.size() > 160) excerpt = truncate_utf8(excerpt, 160) + "…";
                p.desc = excerpt;
                // 面包屑：首页 / 博客 / 标题（博客详情页在 blog/ 下，链接相对本目录）
                std::ostringstream bc;
                bc << "  <nav class=\"breadcrumb\" aria-label=\"面包屑\">\n"
                   << "    <a href=\"../index.html\">{{home}}</a>\n"
                   << "    <span class=\"sep\">/</span>\n"
                   << "    <a href=\"index.html\">{{navBlog}}</a>\n"
                   << "    <span class=\"sep\">/</span>\n"
                   << "    <span class=\"crumb current\">" << esc(p.title) << "</span>\n"
                   << "  </nav>\n";
                // 元信息：发布日期 + 阅读时长
                auto [cjk, words] = count_words(strip_tags(p.html));
                int mins = (int)std::ceil(cjk / 300.0 + words / 200.0);
                if (mins < 1) mins = 1;
                std::string pub = format_date_local(p.dateT);   // 本地时区，避免 UTC 倒退一天
                std::string rt = dict.count("readingTime") ? dict.at("readingTime")
                                                            : "约 {{minutes}} 分钟阅读（{{words}} 字）";
                rt = subst_tokens(rt, {{"minutes", std::to_string(mins)}, {"words", std::to_string(cjk + words)}});
                std::string meta = "  <div class=\"page-meta\">" + esc(pub) + " · " + rt + "</div>\n";
                // 上下篇（博客邻篇，缺位置灰——行业标准；链接相对 blog/ 目录用 rel 无前缀）
                std::ostringstream bp;
                bp << "<nav class=\"pager\">\n";
                if (bi > 0)
                    bp << "  <a class=\"pager-item prev\" href=\""
                       << esc_attr(b.blog_posts[bi-1].file.substr(5)) << ".html\">\n"
                       << "    <span class=\"pager-label\">{{prevLabel}}</span>\n"
                       << "    <span class=\"pager-title\">" << esc(b.blog_posts[bi-1].title) << "</span>\n  </a>\n";
                else
                    bp << "  <span class=\"pager-item prev disabled\" aria-disabled=\"true\">\n"
                       << "    <span class=\"pager-label\">{{prevLabel}}</span>\n"
                       << "    <span class=\"pager-title\">{{pagerNone}}</span>\n  </span>\n";
                if (bi + 1 < b.blog_posts.size())
                    bp << "  <a class=\"pager-item next\" href=\""
                       << esc_attr(b.blog_posts[bi+1].file.substr(5)) << ".html\">\n"
                       << "    <span class=\"pager-label\">{{nextLabel}}</span>\n"
                       << "    <span class=\"pager-title\">" << esc(b.blog_posts[bi+1].title) << "</span>\n  </a>\n";
                else
                    bp << "  <span class=\"pager-item next disabled\" aria-disabled=\"true\">\n"
                       << "    <span class=\"pager-label\">{{nextLabel}}</span>\n"
                       << "    <span class=\"pager-title\">{{pagerNone}}</span>\n  </span>\n";
                bp << "</nav>\n";
                // SEO head：canonical / hreflang 交替 / RSS / manifest / 社交（article 时间戳）
                std::string headExtra;
                if (!cfg.url.empty()) {
                    std::string u = homeBase;
                    headExtra += "  <link rel=\"canonical\" href=\"" + u + esc_attr(p.file) + ".html\">\n";
                    headExtra += alt_links(p.file, 1);   // 博客页在 blog/ 子目录（深度 1）
                }
                // 博客页位于 blog/ 子目录，RSS/manifest 需回退一级（../rss.xml、../manifest.webmanifest）
                {
                    std::string bb = feedLinkTag;
                    bb = replace_all(bb, "href=\"./", "href=\"../");
                    std::string mb = manifestTag;
                    mb = replace_all(mb, "href=\"./", "href=\"../");
                    headExtra += bb + mb;
                }
                std::string ogUrl = cfg.url.empty() ? std::string() : homeBase + p.file + ".html";
                headExtra += social_head(cfg, i18n_replace(p.title, dict), p.desc, ogUrl, ogImageUrl,
                                         p.dateT, p.dateT, loc, true);
                TocResult t = build_toc(p.html);
                std::string page = render_page(cfg, opt, render_left_nav(b.blogNav.empty() ? cfg.nav : b.blogNav, "", 0, "../"),
                                              t.html, t.toc, bp.str(), headExtra,
                                              p.title, p.desc, bc.str(), meta, "",
                                              curLocale, localeSwitch, i18nJson, "../");
                std::ofstream(locOut / "blog" / (rel + ".html"))
                    << apply_fingerprints(cfg.compress ? wrap_webp(minify_html(i18n_replace(page, dict)), locOut) : i18n_replace(page, dict));
            }
            // 列表页 + 分页：第一页 blog/index.html，后续 blog/page/N.html
            // （列表页在 blog/ 下 relBase="../"；分页页在 blog/page/ 下 relBase="../../"）
            {
                const int kPerPage = 10;
                std::vector<const Page*> vis;
                for (const auto& p : b.blog_posts)
                    if (!(p.draft && !includeDrafts)) vis.push_back(&p);
                size_t pagesN = (vis.size() + kPerPage - 1) / kPerPage;
                for (size_t pi = 0; pi < pagesN; ++pi) {
                    bool isPageSub = (pi > 0);                  // 分页页位于 blog/page/
                    std::string relBase = isPageSub ? "../../" : "../";
                    std::string cardBase = isPageSub ? "../" : "";   // 卡片链接前缀
                    std::string navBase  = isPageSub ? "../" : "";   // 分页导航链接前缀
                    std::ostringstream body;
                    body << "<section class=\"blog-list\">\n  <h1>{{blogTitle}}</h1>\n";
                    for (size_t k = pi * kPerPage; k < vis.size() && k < (pi + 1) * kPerPage; ++k) {
                        const Page& p = *vis[k];
                        std::string pub = format_date_local(p.dateT);   // 本地时区
                        body << "  <article class=\"blog-card\">\n"
                             << "    <div class=\"blog-date\">" << esc(pub) << "</div>\n"
                             << "    <h2><a href=\"" << esc_attr(cardBase + p.file.substr(5)) << ".html\">"
                             << esc(p.title) << "</a></h2>\n"
                             << "    <p class=\"blog-desc\">" << esc(p.desc) << "</p>\n"
                             << "  </article>\n";
                    }
                    if (pagesN > 1) {
                        body << "  <nav class=\"blog-pager\">\n";
                        auto pageHref = [&](size_t pp) {
                            if (pp == 0) return navBase + "index.html";
                            return navBase + "page/" + std::to_string(pp + 1) + ".html";
                        };
                        if (pi > 0)
                            body << "    <a class=\"bp-prev\" href=\"" << pageHref(pi - 1) << "\">{{prevLabel}}</a>\n";
                        for (size_t pp = 0; pp < pagesN; ++pp) {
                            if (pp == pi)
                                body << "    <span class=\"bp-cur\">" << (pp + 1) << "</span>\n";
                            else
                                body << "    <a class=\"bp-num\" href=\"" << pageHref(pp) << "\">" << (pp + 1) << "</a>\n";
                        }
                        if (pi + 1 < pagesN)
                            body << "    <a class=\"bp-next\" href=\"" << pageHref(pi + 1) << "\">{{nextLabel}}</a>\n";
                        body << "  </nav>\n";
                    }
                    body << "</section>\n";
                    std::string page = render_page(cfg, opt, render_left_nav(b.blogNav.empty() ? cfg.nav : b.blogNav, "", 0, relBase),
                                                  body.str(), "", "", "", "{{blogTitle}}", cfg.description,
                                                  "", "", "", curLocale, localeSwitch, i18nJson, relBase);
                    if (pi == 0)
                        std::ofstream(locOut / "blog" / "index.html")
                            << apply_fingerprints(cfg.compress ? wrap_webp(minify_html(i18n_replace(page, dict)), locOut) : i18n_replace(page, dict));
                    else {
                        fs::create_directories(locOut / "blog" / "page", ec);
                        std::ofstream(locOut / "blog" / "page" / (std::to_string(pi + 1) + ".html"))
                            << apply_fingerprints(cfg.compress ? wrap_webp(minify_html(i18n_replace(page, dict)), locOut) : i18n_replace(page, dict));
                    }
                }
            }
        }

        // 8) 搜索索引（每语言独立，内容取自该语言正文；标题走 i18n 字典解析）
        {   // 合并博客文章进索引（file 带 blog/ 前缀，链接相对当前语言目录正确）
            std::vector<Page> allPages = pages;
            allPages.insert(allPages.end(), b.blog_posts.begin(), b.blog_posts.end());
            gen_search_index(allPages, includeDrafts, dict, locOut);
        }

        // 9) 标签聚合页：基于 front matter 的 tags，自动生成每个标签一个列表页 + 总览页
        //    （tags 页位于 tags/ 子目录：relBase="../"；博客文章也参与聚合，链接 ../blog/xxx.html）
        {
            std::map<std::string, std::vector<std::string>> tagMap;
            for (const auto& p : pages) {
                if (p.draft && !includeDrafts) continue;
                for (const auto& t : p.tags) tagMap[t].push_back(p.file);
            }
            for (const auto& p : b.blog_posts) {
                if (p.draft && !includeDrafts) continue;
                for (const auto& t : p.tags) tagMap[t].push_back(p.file);
            }
            if (!tagMap.empty()) {
                fs::create_directories(locOut / "tags", ec);
                std::string overview = "<section class=\"tag-cloud\"><h1>{{allTags}}</h1>\n"
                                       "  <div class=\"tags\">\n";
                for (auto& kv : tagMap)
                    // 聚合页位于 tags/ 目录内，标签链接用相对自身的 X.html（不能带 tags/ 前缀，否则解析成 tags/tags/X.html 404）
                    overview += "    <a class=\"tag\" href=\"" + esc_attr(slugify(kv.first))
                                + ".html\">#" + esc(kv.first) + "</a>\n";
                overview += "  </div>\n</section>\n";
                std::string ov = render_page(cfg, opt, render_left_nav(cfg.nav, "", 0, "../"), overview,
                                              "", "", "", "{{allTags}}", cfg.description, "", "", "",
                                              curLocale, localeSwitch, i18nJson, "../");
                std::ofstream(locOut / "tags" / "index.html")
                    << apply_fingerprints(cfg.compress ? wrap_webp(minify_html(i18n_replace(ov, dict)), locOut) : i18n_replace(ov, dict));
                for (auto& kv : tagMap) {
                    std::string body = "<section class=\"tag-page\"><h1>#" + esc(kv.first) + "</h1>\n"
                                       "  <ul class=\"doc-list\">\n";
                    for (auto& fl : kv.second) {
                        std::string t;
                        for (const auto& p : pages) if (p.file == fl) { t = p.title; break; }
                        if (t.empty() && fl.size() > 5 && fl.compare(0, 5, "blog/") == 0)
                            for (const auto& p : b.blog_posts) if (p.file == fl) { t = p.title; break; }
                        if (t.empty()) t = fl;
                        body += "    <li><a href=\"../" + esc_attr(fl) + ".html\">" + esc(t) + "</a></li>\n";
                    }
                    body += "  </ul>\n</section>\n";
                    std::string tp = render_page(cfg, opt, render_left_nav(cfg.nav, "", 0, "../"), body,
                                                 "", "", "", "#" + kv.first, cfg.description, "", "", "",
                                                 curLocale, localeSwitch, i18nJson, "../");
                    std::ofstream(locOut / "tags" / (slugify(kv.first) + ".html"))
                        << apply_fingerprints(cfg.compress ? wrap_webp(minify_html(i18n_replace(tp, dict)), locOut) : i18n_replace(tp, dict));
                }
            }
        }

        // 10) 自定义 404 页（每语言一份，文案走 i18n 字典）
        {
            std::string nf = "<section class=\"notfound\">\n"
                "  <div class=\"nf-code\">404</div>\n"
                "  <h1>{{notFoundTitle}}</h1>\n"
                "  <p class=\"nf-desc\">{{notFoundDesc}}</p>\n"
                "  <a class=\"cta\" href=\"index.html\">{{backHome}}</a>\n"
                "</section>\n";
            std::string nfPage = render_page(cfg, opt, render_left_nav(cfg.nav, "", 0), nf,
                                            "", "", "", "404", cfg.description, "", "", "", curLocale, localeSwitch, i18nJson);
            std::ofstream(locOut / "404.html")
                << apply_fingerprints(cfg.compress ? wrap_webp(minify_html(i18n_replace(nfPage, dict)), locOut) : i18n_replace(nfPage, dict));
        }

        // 11) RSS / JSON Feed（行业标准，内建，无需 Node；博客文章并入订阅流）
        {
            std::vector<Page> allPages = pages;
            allPages.insert(allPages.end(), b.blog_posts.begin(), b.blog_posts.end());
            gen_feeds(locOut, loc, allPages, cfg, dict, multi);
        }
        // 12) PWA（manifest + service worker + theme-color），内建替代 gen-pwa.js
        gen_pwa(locOut, cfg, feedTitle, theme_root() / "assets");
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

// 4) 多语言根 index.html 重定向到默认语言（单语言模式已在循环中生成，无需重定向）
static void write_root_redirect(BuildContext& b) {
    I18nCfg& i18n = b.i18n;
    const fs::path& out_dir = b.out_dir;

    // 根目录 index.html：多语言模式下重定向到默认语言（单语言模式已在循环中生成，无需重定向）
    if (i18n.enabled) {
        std::string target = i18n.defaultLocale + "/index.html";
        std::ofstream(out_dir / "index.html") <<
            "<!DOCTYPE html>\n<html lang=\"" << esc_attr(i18n.defaultLocale) << "\">\n<head>\n"
            "  <meta charset=\"utf-8\">\n  <title>Redirecting…</title>\n"
            "  <meta http-equiv=\"refresh\" content=\"0; url=./" << esc_attr(target) << "\">\n"
            "  <link rel=\"canonical\" href=\"./" << esc_attr(target) << "\">\n"
            "</head>\n<body>\n  <p>正在跳转到 <a href=\"./" << esc_attr(target) << "\">"
            << esc(i18n.defaultLocale) << "</a> …</p>\n</body>\n</html>\n";
    }
}

// 5) 根目录额外生成默认语言 feed 与 PWA（供根 index.html 重定向页使用）
static void write_root_feeds_pwa(BuildContext& b) {
    SiteConfig& cfg = b.cfg;
    I18nCfg& i18n = b.i18n;
    I18nDict& fallbackUI = b.fallbackUI;
    std::vector<Page>& pages = b.pages;
    const fs::path& out_dir = b.out_dir;

    // i18n 站点：在 dist 根额外生成默认语言 feed 与 PWA（供根 index.html 重定向页使用）
    if (i18n.enabled) {
        const I18nDict& dDict = i18n.dicts.count(i18n.defaultLocale) ? i18n.dicts.at(i18n.defaultLocale) : fallbackUI;
        std::vector<Page> allPages = pages;
        allPages.insert(allPages.end(), b.blog_posts.begin(), b.blog_posts.end());
        gen_feeds(out_dir, i18n.defaultLocale, allPages, cfg, dDict, true, /*silent=*/true);
        gen_pwa(out_dir, cfg, i18n_replace(cfg.title, dDict), theme_root() / "assets");
    }
}

// 6) sitemap.xml（SEO 标配）：多语言列出全部语言 URL + hreflang 交替；单语言与旧行为一致
static void write_sitemap(BuildContext& b) {
    const SiteConfig& cfg = b.cfg;
    const I18nCfg& i18n = b.i18n;
    const std::vector<Page>& pages = b.pages;
    const bool& includeDrafts = b.includeDrafts;
    const fs::path& out_dir = b.out_dir;

    // 9) sitemap.xml（SEO 标配）：多语言列出全部语言 URL + hreflang 交替；单语言与旧行为一致
    if (!cfg.url.empty()) {
        std::string base = cfg.url;
        if (!base.empty() && base.back() != '/') base += '/';
        std::ostringstream sm;
        sm << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           << "<urlset xmlns=\"http://www.sitemaps.org/schemas/sitemap/0.9\"\n"
           << "        xmlns:xhtml=\"http://www.w3.org/1999/xhtml\">\n";
        auto emit = [&](const std::string& rel) {
            sm << "  <url><loc>" << base << esc_attr(rel) << "</loc>\n";
            if (i18n.enabled) {
                // rel 形如 "zh-CN/index.html"，去掉语言前缀得到页面路径 "index.html"
                std::string pageRel = rel;
                size_t slash = rel.find('/');
                if (slash != std::string::npos) pageRel = rel.substr(slash + 1);
                for (auto& kv : i18n.labels) {
                    std::string other = base + kv.first + "/" + pageRel;
                    sm << "    <xhtml:link rel=\"alternate\" hreflang=\"" << esc_attr(kv.first)
                       << "\" href=\"" << other << "\"/>\n";
                }
                // x-default 指向默认语言版本（多语言 SEO 标准）
                sm << "    <xhtml:link rel=\"alternate\" hreflang=\"x-default\" href=\""
                    << base << i18n.defaultLocale << "/" << pageRel << "\"/>\n";
            }
            sm << "  </url>\n";
        };
        int pub = 0; for (const auto& p : pages) if (!p.draft) ++pub;
        if (i18n.enabled) {
            for (auto& kv : i18n.labels) emit(kv.first + "/index.html");
            for (const auto& p : pages) { if (p.draft && !includeDrafts) continue; for (auto& kv : i18n.labels) emit(kv.first + "/" + p.file + ".html"); }
        } else {
            emit("index.html");
            for (const auto& p : pages) { if (p.draft && !includeDrafts) continue; emit(p.file + ".html"); }
        }
        sm << "</urlset>\n";
        std::ofstream(out_dir / "sitemap.xml") << sm.str();
        std::cout << color::green("已生成 sitemap.xml") << "（"
                  << (i18n.enabled ? pub * i18n.labels.size() + i18n.labels.size()
                                   : pub + 1) << " 个 URL）\n";
    } else {
        std::cout << color::warn("提示: ") << "在 config.json 设置 url 即可生成 sitemap.xml（SEO）\n";
    }
}

// 7) robots.txt（标准：允许抓取，附 sitemap 地址）
static void write_robots(BuildContext& b) {
    const SiteConfig& cfg = b.cfg;
    const fs::path& out_dir = b.out_dir;

    // 11) robots.txt（标准：允许抓取，附 sitemap 地址）
    {
        std::ostringstream rb;
        rb << "User-agent: *\nAllow: /\n";
        if (!cfg.url.empty()) {
            std::string u = cfg.url; if (!u.empty() && u.back() != '/') u += '/';
            rb << "Sitemap: " << u << "sitemap.xml\n";
        }
        std::ofstream(out_dir / "robots.txt") << rb.str();
    }
}

// 8) 构建汇总输出（发布的文档数 + 文件清单 + 配置/插件摘要）
static void print_summary(BuildContext& b) {
    const SiteConfig& cfg = b.cfg;
    const I18nCfg& i18n = b.i18n;
    const I18nDict& fallbackUI = b.fallbackUI;
    const std::vector<Page>& pages = b.pages;
    const bool& includeDrafts = b.includeDrafts;
    const fs::path& out_dir = b.out_dir;

    int published = 0; for (const auto& p : pages) if (!p.draft || includeDrafts) ++published;
    std::cout << color::green("已生成 ") << published << color::green(" 篇文档到 ") << out_dir << "\n";
    const I18nDict& defDict = (i18n.enabled && i18n.dicts.count(i18n.defaultLocale))
                                  ? i18n.dicts.at(i18n.defaultLocale) : fallbackUI;
    if (!g_quiet) {
        for (const auto& p : pages) {
            if (p.draft && !includeDrafts) continue;
            std::cout << "  - " << color::cyan(p.file + ".html") << color::muted("  (")
                      << i18n_replace(p.title, defDict) << color::muted(")\n");
        }
        std::cout << color::muted("配置: config.json + sidebar/ | 插件: ");
        for (const auto& p : cfg.plugins) std::cout << color::blue(p) << " ";
        if (cfg.plugins.empty()) std::cout << color::muted("(默认全开)");
        std::cout << (cfg.themeVars.empty() ? std::string("") : color::muted(" | 已注入主题变量")) << "\n";
    }
    // i18n 键缺失告警：写错键名 / 字典缺翻译时，页面会显示 {{key}} 字面量，构建期及时指出（不阻塞构建）
    if (!g_i18n_missing.empty() && !g_quiet) {
        std::vector<std::string> uniq;
        for (const auto& k : g_i18n_missing)
            if (std::find(uniq.begin(), uniq.end(), k) == uniq.end()) uniq.push_back(k);
        std::cerr << color::yellow("\n⚠ 警告：") << uniq.size()
                  << " 个 i18n 键在字典中缺失（页面将显示 {{key}} 字面量）：\n";
        for (const auto& k : uniq) std::cerr << "    " << k << "\n";
    }
    // 死链告警：站内相对链接指向不存在的目标（对标 VitePress/MkDocs 的链接校验，不阻塞构建）
    if (!g_link_broken.empty() && !g_quiet) {
        std::cerr << color::yellow("\n⚠ 警告：发现 ") << g_link_broken.size()
                  << " 个站内链接目标不存在（死链）：\n";
        for (const auto& s : g_link_broken) std::cerr << "    " << s << "\n";
    }
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

    // ---- 版本化文档分派（Docusaurus 风格）----
    // 仅最外层调用执行版本循环（static 重入锁：子版本构建不再分派，避免无限递归）。
    // 先探测 config.json 的 versions 列表；非空则对每个版本独立构建到 dist/<name>/，
    // 并在全部完成后生成根 index.html 重定向到默认版本。单版本站点完全不走此分支。
    static bool s_version_dispatching = false;
    if (!s_version_dispatching) {
        s_version_dispatching = true;
        fs::path cfgp = g_engine / "config/config.json";
        std::vector<VersionCfg> vers;
        if (fs::exists(cfgp)) {
            try {
                json j = json::parse(read_file(cfgp));
                json site = j.contains("site") && j["site"].is_object() ? j["site"] : j;
                if (site.contains("versions") && site["versions"].is_array()) {
                    for (auto& v : site["versions"]) {
                        if (!v.is_object() || !v.contains("name")) continue;
                        VersionCfg vc;
                        vc.name = v["name"].get<std::string>();
                        vc.label = v.value("label", vc.name);
                        vc.source = v.value("source", std::string());
                        vc.default_v = v.value("default", false);
                        vers.push_back(std::move(vc));
                    }
                    if (!vers.empty() && !vers[0].default_v) {
                        bool any = false;
                        for (auto& v : vers) if (v.default_v) { any = true; break; }
                        if (!any) vers[0].default_v = true;
                    }
                }
            } catch (...) { vers.clear(); }
        }

        // 约定优于配置：config 未声明 versions 时，自动扫描 in_dir 同级下的
        // "<源目录名>-*" 快照目录（如 docs/ 旁的 docs-v1/、docs-v2/）识别为历史版本。
        // docs/ 恒为 current（默认版）；无任何快照目录 → 单版本，行为与旧版完全一致。
        if (vers.empty()) {
            std::string base = in_dir.filename().string();   // 如 "docs"
            // in_dir 可能是相对路径（如 "docs"），parent_path() 为空 → 用 "." 表示项目根
            fs::path parent = in_dir.parent_path();
            if (parent.empty()) parent = fs::path(".");
            std::error_code sec;
            if (fs::exists(parent, sec) && fs::is_directory(parent, sec)) {
                std::vector<std::string> snapshots;
                for (auto& e : fs::directory_iterator(parent, sec)) {
                    if (!e.is_directory(sec)) continue;
                    std::string name = e.path().filename().string();
                    // 匹配 "<base>-<suffix>"，且排除 .Cdocs/.build/dist 等隐藏/产物目录
                    if (name.size() > base.size() + 1 && name.compare(0, base.size(), base) == 0
                        && name[base.size()] == '-' && name[0] != '.') {
                        snapshots.push_back(name);
                    }
                }
                if (!snapshots.empty()) {
                    // current 恒为首位 + 默认；历史版本按名排序（docs-v1 < docs-v2 < ...）
                    std::sort(snapshots.begin(), snapshots.end());
                    VersionCfg cur;
                    cur.name = "current";
                    cur.label = "最新";
                    cur.default_v = true;
                    vers.push_back(std::move(cur));
                    for (auto& s : snapshots) {
                        VersionCfg vc;
                        vc.name = s.substr(base.size() + 1);   // docs-v1 → v1
                        vc.label = vc.name;                    // label 默认即版本名
                        vc.source = s;
                        vers.push_back(std::move(vc));
                    }
                    if (!g_quiet)
                        std::cout << color::muted("  [versions] 自动识别 ") << snapshots.size()
                                  << " 个历史版本: ";
                    for (auto& v : vers)
                        if (!g_quiet) std::cout << color::cyan(v.name) << " ";
                    if (!g_quiet) std::cout << "\n";
                }
            }
        }

        if (!vers.empty()) {
            // 多版本模式：dist 整体重建（每个版本独立子目录）
            if (cleanBefore) {
                std::error_code ec2;
                fs::remove_all(out_dir, ec2);
            }
            std::string defName;
            for (const auto& v : vers) if (v.default_v) defName = v.name;

            // 把完整版本列表序列化传给子构建（供 header 版本下拉）
            {
                json va = json::array();
                for (const auto& v : vers) {
                    va.push_back({{"name", v.name}, {"label", v.label},
                                  {"source", v.source}, {"default", v.default_v}});
                }
                g_versions_json = va.dump();
            }

            for (const auto& v : vers) {
                // 版本源目录：source 为空用主 in_dir（docs）；否则取 in_dir 同级下的 <source>
                // （in_dir 可能为相对路径，parent 为空时用 "." 表示项目根）
                fs::path parent = in_dir.parent_path();
                if (parent.empty()) parent = fs::path(".");
                fs::path vIn  = in_dir;
                if (!v.source.empty() && v.source != in_dir.filename().string())
                    vIn = parent / v.source;
                fs::path vOut = out_dir / v.name;
                if (!g_quiet)
                    std::cout << color::bold(color::cyan("\n=== 构建版本 "))
                              << color::cyan(v.label) << color::bold(color::cyan(" → ")) << vOut << "\n";
                // 版本信息通过全局传给子构建：内部 load_site_config 读取
                g_cur_version = v.name;
                g_cur_version_label = v.label;
                int rc = run_build(vIn, vOut, includeDrafts, false);
                g_cur_version.clear();
                g_cur_version_label.clear();
                if (rc) { g_versions_json.clear(); s_version_dispatching = false; return rc; }
            }
            g_versions_json.clear();
            // 根 index.html：重定向到默认版本（Docusaurus 同款行为）
            if (!defName.empty()) {
                std::ofstream(out_dir / "index.html")
                    << "<!DOCTYPE html>\n<html lang=\"zh-CN\">\n<head>\n"
                    << "  <meta charset=\"utf-8\">\n  <title>Redirecting…</title>\n"
                    << "  <meta http-equiv=\"refresh\" content=\"0; url=./" << esc_attr(defName) << "/index.html\">\n"
                    << "  <link rel=\"canonical\" href=\"./" << esc_attr(defName) << "/index.html\">\n"
                    << "</head>\n<body>\n  <p>正在跳转到默认版本 <a href=\"./"
                    << esc_attr(defName) << "/index.html\">" << esc(defName) << "</a> …</p>\n</body>\n</html>\n";
            }
            s_version_dispatching = false;
            return 0;
        }
        s_version_dispatching = false;
    }

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
    return 0;
}


// ============ 内置 i18n 字典（新站点初始化用，确保完整可构建） ============
static const char* kZhCN = R"CDOCS({
  "siteTitle": "Cdocs 文档",
  "siteDesc": "一个用 C++ 编写、复用成熟组件（md4c + nlohmann/json + FlexSearch）的极简静态文档站点生成器。",
  "menuToggleLabel": "打开导航",
  "searchPlaceholder": "搜索文档…",
  "themeToggleLabel": "切换主题",
  "tocTitle": "本页目录",
  "home": "首页",
  "getStarted": "开始阅读",
  "prevLabel": "上一篇",
  "nextLabel": "下一篇",
  "pagerNone": "暂无",
  "editThisPage": "编辑此页",
  "lastUpdated": "最后更新于",
  "readingTime": "约 {{minutes}} 分钟阅读（{{words}} 字）",
  "backToTop": "顶部",
  "notFoundTitle": "页面不见了",
  "notFoundDesc": "你访问的页面不存在或已被移动。",
  "backHome": "返回首页",
  "localeLabel": "语言",
  "brand": "Cdocs",
  "navGitHub": "GitHub",
  "navProject": "项目主页",
  "footerText": "© 2026 Cdocs · 基于 C++、md4c、nlohmann/json、FlexSearch 构建",
  "navGettingStarted": "入门",
  "navIntro": "介绍",
  "navGuide": "使用指南",
  "navReference": "参考",
  "navApi": "接口说明",
  "navAdvanced": "进阶",
  "navArchitecture": "架构",
  "navRendering": "渲染",
  "navInternals": "底层实现",
  "navPipeline": "渲染管线",
  "navRender": "渲染循环",
  "allTags": "全部标签",
  "navBlog": "博客",
  "blogTitle": "博客",
  "copyCode": "复制代码",
  "copy": "复制",
  "copied": "已复制",
  "searchLoadFailed": "搜索数据加载失败",
  "useHttpServer": "：请用本地服务器(http)访问，不要直接以 file:// 打开本页",
  "noResults": "没有匹配「",
  "noResultsSuffix": "」的结果",
  "cmdTitle": "搜索文档",
  "cmdPlaceholder": "搜索文档…",
  "cmdType": "输入关键词开始搜索",
  "mermaidError": "图表渲染失败",
  "rssLabel": "RSS",
  "printPage": "打印 / 导出 PDF",
  "feedbackTitle": "本页有帮助吗？",
  "feedbackYes": "有帮助",
  "feedbackNo": "需改进",
  "feedbackThanks": "感谢你的反馈！",
  "lightboxLabel": "图片预览",
  "close": "关闭"
})CDOCS";

static const char* kEn = R"CDOCS({
  "siteTitle": "Cdocs Docs",
  "siteDesc": "A minimal static documentation site generator written in C++, reusing mature components (md4c + nlohmann/json + FlexSearch).",
  "menuToggleLabel": "Open navigation",
  "searchPlaceholder": "Search docs…",
  "themeToggleLabel": "Toggle theme",
  "tocTitle": "On this page",
  "home": "Home",
  "getStarted": "Get started",
  "prevLabel": "Previous",
  "nextLabel": "Next",
  "pagerNone": "None",
  "editThisPage": "Edit this page",
  "lastUpdated": "Last updated:",
  "readingTime": "About {{minutes}} min read ({{words}} words)",
  "backToTop": "Top",
  "notFoundTitle": "Page not found",
  "notFoundDesc": "The page you are looking for does not exist or has moved.",
  "backHome": "Back to home",
  "localeLabel": "Language",
  "brand": "Cdocs",
  "navGitHub": "GitHub",
  "navProject": "Project Home",
  "footerText": "© 2026 Cdocs · Built with C++, md4c, nlohmann/json, FlexSearch",
  "navGettingStarted": "Getting Started",
  "navIntro": "Introduction",
  "navGuide": "Guide",
  "navReference": "Reference",
  "navApi": "API Reference",
  "navAdvanced": "Advanced",
  "navArchitecture": "Architecture",
  "navRendering": "Rendering",
  "navInternals": "Internals",
  "navPipeline": "Render Pipeline",
  "navRender": "Render Loop",
  "allTags": "All Tags",
  "navBlog": "Blog",
  "blogTitle": "Blog",
  "copyCode": "Copy code",
  "copy": "Copy",
  "copied": "Copied",
  "searchLoadFailed": "Failed to load search data",
  "useHttpServer": ": please access via a local HTTP server, not file://",
  "noResults": "No results for \"",
  "noResultsSuffix": "\"",
  "cmdTitle": "Search docs",
  "cmdPlaceholder": "Search docs…",
  "cmdType": "Type to search",
  "mermaidError": "Failed to render diagram",
  "rssLabel": "RSS",
  "printPage": "Print / Export PDF",
  "feedbackTitle": "Was this page helpful?",
  "feedbackYes": "Yes",
  "feedbackNo": "No",
  "feedbackThanks": "Thanks for your feedback!",
  "lightboxLabel": "Image preview",
  "close": "Close"
})CDOCS";

// 把文件名/段名美化为页面标题（my-page → My Page）
static std::string pretty_title(const std::string& stem) {
    std::string t = stem;
    for (size_t i = 0; i < t.size(); i++)
        if (t[i] == '-' || t[i] == '_') t[i] = ' ';
    if (!t.empty() && t[0] >= 'a' && t[0] <= 'z') t[0] = (char)(t[0] - 'a' + 'A');
    return t;
}

// init：在指定目录创建一个完整的新站点（对标 hugo new site）
int cmd_init(fs::path dir, bool copyExe) {
    std::error_code ec;
    if (fs::exists(dir) && fs::is_directory(dir) && !fs::is_empty(dir, ec)) {
        std::cerr << color::error("目标目录已存在且非空: ") << dir << "\n";
        return 1;
    }
    fs::create_directories(dir, ec);
    fs::create_directories(dir / "docs", ec);
    fs::create_directories(dir / "archetypes", ec);
    fs::create_directories(dir / ".Cdocs" / "config", ec);
    fs::create_directories(dir / ".Cdocs" / "i18n", ec);

    // 复制运行所需资源（theme 主题 + deps 运行时依赖；config/i18n 会被下面写入覆盖）
    fs::path eng = exe_dir() / ".Cdocs";
    bool haveEngine = fs::exists(eng);
    if (haveEngine) {
        for (const char* sub : { "theme", "deps" }) {
            fs::path src = eng / sub;
            if (fs::exists(src))
                fs::copy(src, dir / ".Cdocs" / sub,
                         fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        }
        if (copyExe) {
            fs::path exe = exe_dir() / "Cdocs.exe";
            if (fs::exists(exe))
                fs::copy(exe, dir / "Cdocs.exe", fs::copy_options::overwrite_existing, ec);
        }
    }

    std::string stem = dir.stem().empty() ? "docs" : dir.stem().string();

    // 1) config.json（三区块：site 全局 / head 页眉 / center 内容 / footer 页脚）
    //    开箱即双语 + 全功能；所有 true/false 开关集中在对应区块顶部便于配置
    {
        std::ofstream o(dir / ".Cdocs/config/config.json");
        o << "{\n"
          << "  \"site\": {\n"
          << "    \"title\": \"" << esc_attr(stem) << "\",\n"
          << "    \"description\": \"由 Cdocs 生成的静态文档站\",\n"
          << "    \"theme\": \"dark\",\n"
          << "    \"url\": \"\",\n"
          << "    \"i18n\": {\n"
          << "      \"defaultLocale\": \"zh-CN\",\n"
          << "      \"dir\": \".Cdocs/i18n\",\n"
          << "      \"locales\": { \"zh-CN\": { \"label\": \"简体中文\" }, \"en\": { \"label\": \"English\" } }\n"
          << "    },\n"
          << "    \"editLink\": { \"base\": \"\", \"docsDir\": \"docs\" },\n"
          << "    \"sidebar\": {\n"
          << "      \"docs\": \"sidebar/docs.json\",\n"
          << "      \"blog\": \"sidebar/blog.json\"\n"
          << "    }\n"
          << "  },\n"
          << "  \"head\": {\n"
          << "    \"logo\": \"\",\n"
          << "    \"showSearch\": true,\n"
          << "    \"showThemeToggle\": true\n"
          << "  },\n"
          << "  \"center\": {\n"
          << "    \"plugins\": [\"search\", \"dark-mode\", \"pager\", \"back-to-top\", \"toc\", \"code-highlight\"],\n"
          << "    \"backToTop\": { \"threshold\": 300, \"label\": \"顶部\" }\n"
          << "  },\n"
          << "  \"footer\": {\n"
          << "    \"text\": \"© 2026 " << esc_attr(stem) << " · 由 Cdocs 生成\"\n"
          << "  }\n"
          << "}\n";
    }

    // 2) sidebar/ 分文件侧边栏（新结构：每个版本 / 博客区一份独立 JSON，名字可自定义）
    //    （不再生成全局 route.json——侧边栏已按版本/区域拆分，config.json 的 site.sidebar 映射接管）
    {
        std::error_code sec;
        fs::create_directories(dir / ".Cdocs/config/sidebar", sec);
        std::ofstream(dir / ".Cdocs/config/sidebar/docs.json")
            << "{\n  \"sidebar\": [\n"
            << "    {\n      \"title\": \"{{navGettingStarted}}\",\n      \"items\": [\n"
            << "        { \"title\": \"{{navIntro}}\", \"file\": \"intro\" },\n"
            << "        { \"title\": \"{{navGuide}}\", \"file\": \"guide\" }\n"
            << "      ]\n    }\n  ]\n}\n";
        std::ofstream(dir / ".Cdocs/config/sidebar/blog.json")
            << "{\n  \"sidebar\": [\n"
            << "    {\n      \"title\": \"博客\",\n      \"items\": [\n"
            << "        { \"title\": \"示例博文\", \"file\": \"blog/hello-cdocs\" }\n"
            << "      ]\n    }\n  ]\n}\n";
    }

    // 3) i18n 字典（双语完整，确保可构建）
    std::ofstream(dir / ".Cdocs/i18n/zh-CN.json") << kZhCN;
    std::ofstream(dir / ".Cdocs/i18n/en.json")   << kEn;

    // 4) 示例双语文档
    std::ofstream(dir / "docs/intro.md") <<
        "# 欢迎使用 Cdocs\n\n"
        "Cdocs 是一个用 **C++** 编写的静态文档站生成器。你在 `docs/` 下写 Markdown，"
        "它生成零依赖、可离线、中英双语的静态站点。\n\n"
        "## 快速开始\n\n"
        "1. 编写文档：`docs/` 下新建 `xxx.md`（英文版 `xxx.en.md`）。\n"
        "2. 构建站点：`Cdocs build` 生成到 `dist/`。\n"
        "3. 本地预览：`Cdocs serve`（默认 http://localhost:8088）。\n\n"
        "> 提示：用 `Cdocs serve -o --watch` 可自动打开浏览器并在文件改动时热重载。\n\n"
        "## 特性\n\n"
        "- 多语言：通过 `{{key}}` + i18n 字典实现\n"
        "- 全文搜索、明暗主题、阅读时长、面包屑、SEO 结构化数据\n"
        "- 单文件二进制，无运行时依赖\n";
    std::ofstream(dir / "docs/intro.en.md") <<
        "# Welcome to Cdocs\n\n"
        "Cdocs is a **C++** static documentation site generator. Write Markdown under `docs/`, "
        "and it builds a zero-dependency, offline-capable, bilingual static site.\n\n"
        "## Quick Start\n\n"
        "1. Write docs: add `xxx.md` under `docs/` (English: `xxx.en.md`).\n"
        "2. Build: `Cdocs build` outputs to `dist/`.\n"
        "3. Preview: `Cdocs serve` (default http://localhost:8088).\n\n"
        "> Tip: `Cdocs serve -o --watch` opens your browser and hot-reloads on file changes.\n\n"
        "## Features\n\n"
        "- i18n via `{{key}}` + dictionaries\n"
        "- Full-text search, dark mode, reading time, breadcrumbs, SEO structured data\n"
        "- Single-file binary, no runtime deps\n";
    std::ofstream(dir / "docs/guide.md") <<
        "# 使用指南\n\n"
        "本章介绍 Cdocs 的常用工作流。\n\n"
        "## 项目结构\n\n"
        "```\n"
        "my-site/\n"
        "├── .Cdocs/\n"
        "│   ├── config/   config.json + sidebar/（分文件侧边栏）\n"
        "│   ├── i18n/     zh-CN.json / en.json\n"
        "│   └── theme/    主题（assets 前端资源 + templates/layout.html 页面骨架）\n"
        "├── docs/         *.md 源文档\n"
        "└── dist/         生成的静态站点\n"
        "```\n\n"
        "## 新建一篇文档\n\n"
        "运行 `Cdocs add my-page`，会在 `docs/my-page.md` 生成文件，并自动加入当前版本侧边栏。\n\n"
        "## 代码高亮\n\n"
        "```cpp\n"
        "#include <iostream>\n"
        "int main() { std::cout << \"Hello, Cdocs\\n\"; }\n"
        "```\n\n"
        "## 数学公式\n\n"
        "行内 $E=mc^2$，块级：\n\n"
        "$$\n"
        "\\int_0^1 x^2\\,dx = \\frac13\n"
        "$$\n";
    std::ofstream(dir / "docs/guide.en.md") <<
        "# Guide\n\n"
        "This chapter covers common Cdocs workflows.\n\n"
        "## Project Structure\n\n"
        "```\n"
        "my-site/\n"
        "├── .Cdocs/\n"
        "│   ├── config/   config.json + sidebar/ (per-area sidebars)\n"
        "│   ├── i18n/     zh-CN.json / en.json\n"
        "│   └── theme/    theme (assets + templates/layout.html)\n"
        "├── docs/         *.md source\n"
        "└── dist/         generated site\n"
        "```\n\n"
        "## Add a page\n\n"
        "Run `Cdocs add my-page` to create `docs/my-page.md` and register it in `route.json`.\n\n"
        "## Code highlighting\n\n"
        "```cpp\n"
        "#include <iostream>\n"
        "int main() { std::cout << \"Hello, Cdocs\\n\"; }\n"
        "```\n\n"
        "## Math\n\n"
        "Inline $E=mc^2$, block:\n\n"
        "$$\n"
        "\\int_0^1 x^2\\,dx = \\frac13\n"
        "$$\n";

    // 5) 页面原型（archetype）：add 命令会读取并把 {{title}}/{{date}}/{{slug}} 替换
    //    原型自带 front matter 示例，使 add 生成的文档天生可用元数据（title/date/tags）
    std::ofstream(dir / "archetypes/default.md") <<
        "---\n"
        "title: \"{{title}}\"\n"
        "date: {{date}}\n"
        "tags: []\n"
        "---\n\n"
        "# {{title}}\n\n"
        "在这里开始写作…\n";

    // 6) 预览启动器
    std::ofstream(dir / "serve.bat") <<
        "@echo off\n"
        "chcp 65001 >nul 2>&1\n"
        "cd /d \"%~dp0\"\n"
        "echo 正在启动 Cdocs 预览服务器... （按 Ctrl+C 停止）\n"
        "Cdocs.exe serve %*\n"
        "if errorlevel 1 pause\n";

    // 7) 自动构建：建完框架立即生成 dist/，让 style.css / app.js 等资源直接就位、开箱即看
    bool built = false;
    if (haveEngine) {
        fs::path saved = fs::current_path(ec);
        fs::current_path(dir, ec);
        if (!ec) {
            int rc = run_build(fs::path("docs"), fs::path("dist"), false, false);
            built = (rc == 0);
            fs::current_path(saved, ec);
        }
    }

    std::cout << color::green("已创建新站点: ") << dir << "\n";
    if (copyExe && haveEngine)
        std::cout << color::green("已复制运行资源(.Cdocs)与 Cdocs.exe。\n");
    else if (haveEngine)
        std::cout << color::green("已复制运行资源(.Cdocs)。\n");
    else
        std::cout << color::warn("注意: ") << "未找到引擎资源(.Cdocs)，仅生成内容骨架，未构建。\n";
    if (built)
        std::cout << color::green("✔ 已自动构建 → dist/（含 style.css / app.js 等前端资源，可直接 serve 预览）。\n");
    std::cout << color::muted("下一步:") << "\n  cd " << dir.string()
              << "\n  " << color::cyan("Cdocs serve")   << color::muted("    # 本地预览（可加 -o 自动开浏览器、--watch 热重载）")
              << "\n  " << color::cyan("Cdocs add <名>") << color::muted("  # 新建一篇文档并加入导航") << "\n";
    return 0;
}

// add：新建一篇文档（对标 hugo new content/...），并自动登记到当前版本的侧边栏（sidebar/docs.json）
// 当前日期 YYYY-MM-DD（archetype 模板 {{date}} 占位用）
static std::string current_date() {
    std::time_t now = std::time(nullptr);
    struct tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tmv);
    return std::string(buf);
}

int cmd_add(const std::string& name) {
    fs::path p(name);
    std::string stem = p.stem().string();
    if (stem.empty()) {
        std::cerr << color::error("无效的页面名: ") << name << "\n";
        return 1;
    }
    std::string title = pretty_title(stem);

    // 读取原型模板（若站点提供 archetypes/default.md），替换 {{title}}
    std::string body = "# " + title + "\n\n在这里开始写作…\n";
    fs::path arch = "archetypes/default.md";
    if (fs::exists(arch)) {
        std::string t = read_file(arch);
        auto sub = [&](const std::string& tok, const std::string& val) {
            size_t pos = 0;
            while ((pos = t.find(tok, pos)) != std::string::npos) {
                t.replace(pos, tok.size(), val);
                pos += val.size();
            }
        };
        sub("{{title}}", title);
        sub("{{date}}", current_date());
        sub("{{slug}}", stem);
        body = t;
    }

    fs::path doc = "docs/" + stem + ".md";
    fs::path docEn = "docs/" + stem + ".en.md";
    if (fs::exists(doc)) {
        std::cerr << color::error("文档已存在: ") << doc << "\n";
        return 1;
    }
    std::ofstream(doc)   << body;
    std::ofstream(docEn) << "# " << title << "\n\nStart writing…\n";

    // 登记到当前版本文档侧边栏 sidebar/docs.json（追加到第一个分组，无分组则新建）
    // 旧站点的全局 route.json 兼容：仅当 sidebar/docs.json 不存在时回退登记到 route.json
    fs::path sp = ".Cdocs/config/sidebar/docs.json";
    fs::path rp = ".Cdocs/config/route.json";
    fs::path target = fs::exists(sp) ? sp : rp;
    json rj = json::object();
    if (fs::exists(target)) {
        try { rj = json::parse(read_file(target)); } catch (...) { rj = json::object(); }
    }
    if (!rj.contains("sidebar") || !rj["sidebar"].is_array()) rj["sidebar"] = json::array();
    auto& sb = rj["sidebar"];
    json item = json::object();
    item["title"] = title;
    item["file"]  = stem;
    if (!sb.empty() && sb[0].is_object() && sb[0].contains("items") && sb[0]["items"].is_array())
        sb[0]["items"].push_back(item);
    else {
        json grp = json::object();
        grp["title"] = "文档";
        grp["items"] = json::array();
        grp["items"].push_back(item);
        sb.push_back(grp);
    }
    std::ofstream(target) << rj.dump(2, ' ', false, json::error_handler_t::replace) << "\n";

    std::cout << color::green("已创建文档: ") << doc << color::muted(" (+ ") << docEn << ")\n"
              << color::green("已加入导航: ") << title
              << color::muted("（" + target.string() + "）\n")
              << color::muted("构建预览: ") << color::cyan("Cdocs serve --watch") << "\n";
    return 0;
}

// clean：清空输出目录（对标 jekyll clean / docusaurus clear）
int cmd_clean() {
    std::error_code ec;
    if (!fs::exists(g_dest, ec)) {
        std::cout << color::muted("无需清理：") << g_dest << color::muted(" 不存在。\n");
        return 0;
    }
    uintmax_t n = fs::remove_all(g_dest, ec);
    if (ec) {
        std::cerr << color::error("清理失败: ") << ec.message() << "\n";
        return 1;
    }
    std::cout << color::green("已清理 ") << g_dest << color::muted("（移除 ") << n
              << color::muted(" 项）\n");
    return 0;
}
