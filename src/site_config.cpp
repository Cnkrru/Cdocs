// site_config.cpp —— 站点配置加载
// （自 builder.cpp 拆分：config.json 三区块解析 + site.route 路由映射 + 侧边栏导航）
// 分三步：① parse_site_block（config.json 字段/版本化/schema 校验/路由映射）
//         ② load_nav_sidebar（侧边栏导航）③ apply_plugin_switches（插件派生渲染开关）

#include "site_config.hpp"
#include "i18n.hpp"
#include "config.hpp"    // parse_link
#include "pages.hpp"     // parse_nav
#include <fstream>
#include <sstream>
#include <set>

// ①-1 site 基础字段（title/description/theme/themeName/url/ogImage/compress/jpegQuality）
static void parse_site_basics(const json& site, SiteConfig& cfg) {
    if (site.contains("title"))       cfg.title = site["title"].get<std::string>();
    if (site.contains("description")) cfg.description = site["description"].get<std::string>();
    if (site.contains("theme"))       cfg.theme = site["theme"].get<std::string>();
    if (site.contains("themeName") && site["themeName"].is_string())
        g_theme_name = site["themeName"].get<std::string>();   // 多主题：themes/<name>
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
}

// ①-2 head 区块（logo/showSearch/showThemeToggle/github/links/nav）
static void parse_head_block(const json& hdr, SiteConfig& cfg) {
    if (hdr.contains("logo"))            cfg.header.logo = hdr["logo"].get<std::string>();
    if (hdr.contains("showSearch"))      cfg.header.showSearch = hdr["showSearch"].get<bool>();
    if (hdr.contains("showThemeToggle")) cfg.header.showThemeToggle = hdr["showThemeToggle"].get<bool>();
    if (hdr.contains("github"))          cfg.header.github = hdr["github"].get<std::string>();
    if (hdr.contains("links")) for (auto& l : hdr["links"]) cfg.header.links.push_back(parse_link(l));
    if (hdr.contains("nav"))   for (auto& l : hdr["nav"])   cfg.header.nav.push_back(parse_link(l));
}

// ①-3 center 区块（plugins / backToTop）；site 用于兼容旧结构 plugins 在顶层
static void parse_center_block(const json& ctr, const json& site, SiteConfig& cfg) {
    if (ctr.contains("plugins")) for (auto& p : ctr["plugins"]) cfg.plugins.push_back(p.get<std::string>());
    // 兼容旧结构：plugins 在顶层
    if (cfg.plugins.empty() && site.contains("plugins"))
        for (auto& p : site["plugins"]) cfg.plugins.push_back(p.get<std::string>());
    if (ctr.contains("backToTop")) {
        auto& b = ctr["backToTop"];
        if (b.contains("threshold")) cfg.backToTopThreshold = b["threshold"].get<int>();
        if (b.contains("label"))     cfg.backToTopLabel = b["label"].get<std::string>();
    }
}

// ①-4 footer 区块（text/links）
static void parse_footer_block(const json& ftr, SiteConfig& cfg) {
    if (ftr.contains("text"))  cfg.footer.text = ftr["text"].get<std::string>();
    if (ftr.contains("links")) for (auto& l : ftr["links"]) cfg.footer.links.push_back(parse_link(l));
}

// ①-5 编辑此页链接（行业标准交互，指向仓库源文件编辑地址）
static void parse_edit_link(const json& site, SiteConfig& cfg) {
    if (!site.contains("editLink")) return;
    auto& e = site["editLink"];
    if (e.contains("base"))    cfg.editBase = e["base"].get<std::string>();
    if (e.contains("docsDir")) cfg.editDocsDir = e["docsDir"].get<std::string>();
}

// ①-6 i18n（多语言）：行业标准 {{key}}+json 字典 + 单语言模式兜底 UI 字典
//   i18n.dir 下放置每份语言的扁平 JSON 字典（.Cdocs/i18n/zh-CN.json / en.json …）
//   注意：fallbackUI 是外层别名（b.fallbackUI），直接赋值，不能重新声明——否则遮蔽别名导致单语言 UI 文案失效。
static void parse_i18n_block(const json& site, I18nCfg& i18n, I18nDict& fallbackUI) {
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
    if (!site.contains("i18n")) return;
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

// ①-7 公开 CSS 属性：用户可在 config.json 的 themeVars 覆盖任意变量
static void parse_theme_vars(const json& site, SiteConfig& cfg) {
    if (!site.contains("themeVars") || !site["themeVars"].is_object()) return;
    std::string body;
    for (auto it = site["themeVars"].begin(); it != site["themeVars"].end(); ++it)
        body += "  " + it.key() + ": " + it.value().get<std::string>() + ";\n";
    cfg.themeVarsBody = ":root, [data-theme=\"light\"], [data-theme=\"dark\"] {\n" + body + "}";
    cfg.themeVars = "<style id=\"user-theme-vars\">\n" + cfg.themeVarsBody + "\n</style>\n";
}

// ①-8 用户自定义 CSS 文件：存在则复制并链接（href 数据供 MetaHead 组件；customCssLink 供 fallback）
static void parse_custom_css(const json& site, SiteConfig& cfg, const fs::path& out_dir) {
    if (!site.contains("customCss")) return;
    fs::path p = site["customCss"].get<std::string>();
    if (!fs::exists(p)) return;
    fs::create_directories(out_dir / "assets");
    fs::copy(p, out_dir / "assets" / p.filename(), fs::copy_options::overwrite_existing);
    cfg.customCssHref = "assets/" + p.filename().string();
    cfg.customCssLink = "<link rel=\"stylesheet\" href=\"" + cfg.customCssHref + "\">\n";
}

// ①-9 首页（home 段）：hero 可选覆盖 + cards 白名单（不配则保持自动全列）
static void parse_home_block(const json& site, SiteConfig& cfg) {
    if (!site.contains("home")) return;
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

// ①-10 反馈上报端点（config.feedback.endpoint）：配了才启用真实统计，空 = 仅本地记忆
static void parse_feedback(const json& site, SiteConfig& cfg) {
    if (!site.contains("feedback") || !site["feedback"].is_object()) return;
    auto& fb = site["feedback"];
    if (fb.contains("endpoint") && fb["endpoint"].is_string())
        cfg.feedbackEndpoint = fb["endpoint"].get<std::string>();
}

// ①-11 版本化文档（config.versions，Docusaurus 风格）+ 自动版本化兜底：
//   { "versions": [ {"name":"v2","label":"2.0","source":"md-v2","default":true}, ... ] }
//   source 为空 = 用主 md/ 目录；default 标记默认版本（根 index 重定向目标）
//   自动版本化：config 未声明 versions，但 run_build 分派时通过 g_versions_json
//   把完整版本列表传给了子构建 → 填进 cfg.versions，供 render_header 渲染版本下拉。
static void parse_versions_block(const json& site, SiteConfig& cfg) {
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
}

// ①-12 config schema 校验（对标成熟工具：未知字段/类型错误构建期提示，防拼写错误静默忽略）
static void validate_schema(const json& j, const json& site, const json& hdr,
                            const json& ctr, const json& ftr) {
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
                        "jpegQuality", "plugins", "sidebar", "route"}, "site");
    warn_unknown(hdr, {"logo", "showSearch", "showThemeToggle", "github", "links", "nav"}, "head");
    warn_unknown(ctr, {"plugins", "backToTop", "comments"}, "center");
    warn_unknown(ftr, {"text", "links"}, "footer");
    if (site.contains("compress") && !site["compress"].is_boolean())
        std::cerr << color::yellow("  [config] site.compress 应为布尔值（true/false）\n");
    if (site.contains("jpegQuality") && !site["jpegQuality"].is_number())
        std::cerr << color::yellow("  [config] site.jpegQuality 应为数字（1-100）\n");
}

// ①-13 路由映射（site.route，兼容旧 site.sidebar）：key=版本源目录名或 "blog"，
//     value=相对 .Cdocs/config/ 的 JSON 路径（route/<name>.json）。每个版本 / 博客区各一份。
static void parse_route_map(const json& j, const json& site, SiteConfig& cfg) {
    auto read_sidebar_map = [&cfg](const json& obj) {
        if (!obj.is_object()) return;
        for (auto& [k, v] : obj.items())
            if (v.is_string()) cfg.sidebarMap[k] = v.get<std::string>();
    };
    if (site.contains("route") && site["route"].is_object())
        read_sidebar_map(site["route"]);
    else if (site.contains("sidebar"))
        read_sidebar_map(site["sidebar"]);
    if (j.contains("route") && j["route"].is_object())
        read_sidebar_map(j["route"]);
    else if (j.contains("sidebar"))
        read_sidebar_map(j["sidebar"]);
}

// ① config.json 三区块解析（site/head/center/footer）+ 版本化 + schema 校验 + 路由映射
//    兼容旧式顶层结构：无 "site" 键时整份当作旧结构解析（header/footer 顶层字段照旧可用）。
static void parse_site_block(BuildContext& b) {
    SiteConfig& cfg = b.cfg;
    I18nCfg& i18n = b.i18n;
    I18nDict& fallbackUI = b.fallbackUI;
    const fs::path& out_dir = b.out_dir;

    fs::path cfg_path = g_engine / "config/config.json";
    if (!fs::exists(cfg_path)) return;   // 无 config.json → 全部保持默认
    try {
        json j = json::parse(read_file(cfg_path));
        json site = j.contains("site") && j["site"].is_object() ? j["site"] : j;   // 全局段（兼容旧顶层）
        json hdr  = j.contains("head")   && j["head"].is_object()   ? j["head"]
                  : j.contains("header") && j["header"].is_object() ? j["header"] : json::object();
        json ctr  = j.contains("center") && j["center"].is_object() ? j["center"] : json::object();
        json ftr  = j.contains("footer") && j["footer"].is_object() ? j["footer"] : json::object();

        // 按字段分区逐块解析（各子函数见上方定义）
        parse_site_basics(site, cfg);
        parse_head_block(hdr, cfg);
        parse_center_block(ctr, site, cfg);
        parse_footer_block(ftr, cfg);
        parse_edit_link(site, cfg);
        parse_i18n_block(site, i18n, fallbackUI);
        parse_theme_vars(site, cfg);
        parse_custom_css(site, cfg, out_dir);
        parse_home_block(site, cfg);
        parse_feedback(site, cfg);
        parse_versions_block(site, cfg);
        // 多版本构建时注入当前版本信息（run_build 分派循环设置全局）
        cfg.curVersion = g_cur_version;
        cfg.curVersionLabel = g_cur_version_label;
        validate_schema(j, site, hdr, ctr, ftr);
        parse_route_map(j, site, cfg);
    } catch (const std::exception& e) {
        // config.json 损坏 / 字段类型错误：不崩溃，用默认配置继续并给出明确提示
        std::cerr << color::error("config.json 解析失败（已用默认配置继续）：") << e.what() << "\n";
    }
}

// ② 侧边栏导航：优先 site.route 映射（key = 版本源目录名），未配置或文件缺失时
//    回退全局 route.json（旧结构零回归）。博客区独立侧边栏（可选，缺省时沿用文档导航）。
static void load_nav_sidebar(BuildContext& b) {
    SiteConfig& cfg = b.cfg;
    fs::path route_path = g_engine / "config/route.json";
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
    std::string navKey = b.in_dir.filename().string();   // "md" / "md-v1" ...
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

// ③ 插件派生渲染开关（config 布尔 × 插件注册）
static void apply_plugin_switches(BuildContext& b) {
    SiteConfig& cfg = b.cfg;
    RenderOpts& opt = b.opt;
    opt.showSearch      = cfg.header.showSearch && has_plugin(cfg.plugins, "search");
    opt.showThemeToggle = cfg.header.showThemeToggle && has_plugin(cfg.plugins, "dark-mode");
    opt.showPager       = has_plugin(cfg.plugins, "pager");
    opt.showToc         = has_plugin(cfg.plugins, "toc");
    opt.showBackToTop   = has_plugin(cfg.plugins, "back-to-top");
    opt.showCodeHighlight = has_plugin(cfg.plugins, "code-highlight");
}

void load_site_config(BuildContext& b) {
    parse_site_block(b);        // ① config.json 字段 + 版本化 + schema 校验 + 路由映射
    load_nav_sidebar(b);        // ② 侧边栏导航
    apply_plugin_switches(b);   // ③ 插件派生渲染开关
}
