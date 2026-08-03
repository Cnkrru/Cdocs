// ============================================================================
// legacy-fallback.cpp —— 老主题 fallback 兼容代码（2026-08-03 从 src/builder.cpp 移出）
//
// 移出原因：v4 地图驱动架构已完全接管渲染（theme/map/*.json + components/*.html），
// 本文件是 v1/v2 时代"C++ 硬编码 HTML + 占位符模板"的旧渲染路径，仅供老主题
// （无 theme/map/ 目录）使用。为让新架构纯粹、不被旧路径拖累，整体移出存档。
//
// 注意：本文件不参与编译。若确需恢复老主题兼容，可将其内容合并回 builder.cpp
// 并在 render_locales 恢复 mapMode 分支。否则可随时删除。
// ============================================================================
#ifndef LEGACY_FALLBACK_ONLY
#error "legacy-fallback.cpp 是存档文件，不参与编译。需要时请合并回 src/builder.cpp 并恢复 mapMode 分支。"
#endif
// ============================================================================

// ---------------- 移出代码（原样存档） ----------------

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
// 页眉数据子块（组件化：Header.html 用这些键组装外观；render_header 组装它们作 fallback）
// 每个子块输出带缩进的完整行；条件不满足返回空串。
static std::string header_menu_toggle(bool isHome) {
    // 首页无左侧边栏（hero 布局）：移动端也不渲染汉堡按钮，行为与桌面一致
    return isHome ? std::string()
        : "      <button id=\"menu-toggle\" class=\"menu-toggle\" type=\"button\" aria-label=\"{{menuToggleLabel}}\"></button>\n";
}
static std::string header_logo(const SiteConfig& cfg, const std::string& relBase) {
    std::string logo = cfg.header.logo.empty() ? cfg.title : cfg.header.logo;
    return "      <a class=\"logo\" href=\"" + esc_attr(relBase) + "index.html\">" + esc(logo) + "</a>\n";
}
static std::string header_topnav(const SiteConfig& cfg, const std::string& relBase) {
    if (cfg.header.links.empty()) return "";
    std::ostringstream o;
    o << "      <nav class=\"topnav\">\n";
    for (const auto& l : cfg.header.links)
        o << "        " << link_html(l, "", relBase) << "\n";
    o << "      </nav>\n";
    return o.str();
}
static std::string header_search(const RenderOpts& opt) {
    return opt.showSearch
        ? "      <input id=\"search\" class=\"search\" type=\"search\" placeholder=\"{{searchPlaceholder}}\" autocomplete=\"off\">\n"
        : std::string();
}
static std::string header_nav_links(const SiteConfig& cfg, const std::string& relBase) {
    // 页眉右侧导航（i18n 按钮左侧）：config header.nav 配置，最多渲染 6 个，超出不渲染
    if (cfg.header.nav.empty()) return "";
    std::ostringstream o;
    o << "      <nav class=\"topbar-nav\">\n";
    size_t n = std::min<size_t>(cfg.header.nav.size(), 6);
    for (size_t i = 0; i < n; ++i)
        o << "        " << link_html(cfg.header.nav[i], "", relBase) << "\n";
    o << "      </nav>\n";
    return o.str();
}
static std::string header_locale_switch(const std::string& localeSwitch) {
    return localeSwitch.empty() ? std::string() : "      " + localeSwitch + "\n";
}
static std::string header_version_select(const SiteConfig& cfg, const std::vector<VersionCfg>& versions,
                                         const std::string& curLocale) {
    if (versions.size() <= 1) return "";
    std::ostringstream o;
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
    return o.str();
}
static std::string header_theme_toggle(const RenderOpts& opt) {
    return opt.showThemeToggle
        ? "      <button id=\"theme-toggle\" class=\"theme-toggle\" aria-label=\"{{themeToggleLabel}}\"></button>\n"
        : std::string();
}
static std::string header_github(const SiteConfig& cfg) {
    // GitHub 图标在亮暗切换按钮右侧（用户布局约定：右区顺序 = 右侧nav · 语言 · 主题 · GitHub）
    if (cfg.header.github.empty()) return "";
    return "      <a class=\"gh-link\" href=\"" + esc_attr(cfg.header.github)
        + "\" target=\"_blank\" rel=\"noopener\" aria-label=\"GitHub\">"
        "<svg viewBox=\"0 0 16 16\" aria-hidden=\"true\"><path fill=\"currentColor\" d=\"M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27.68 0 1.36.09 2 .27 1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.013 8.013 0 0016 8c0-4.42-3.58-8-8-8z\"/></svg></a>\n";
}

static std::string render_header(const SiteConfig& cfg, const RenderOpts& opt,
                                 const std::string& curLocale, const std::string& localeSwitch,
                                 const std::vector<VersionCfg>& versions = {},
                                 const std::string& relBase = "",
                                 bool isHome = false) {
    std::ostringstream o;
    o << "  <header class=\"topbar\">\n"
      << "    <div class=\"topbar-left\">\n"
      << header_menu_toggle(isHome) << header_logo(cfg, relBase) << header_topnav(cfg, relBase)
      << "    </div>\n"
      << "    <div class=\"topbar-center\">\n"
      << header_search(opt)
      << "    </div>\n"
      << "    <div class=\"topbar-right\">\n"
      << header_nav_links(cfg, relBase) << header_locale_switch(localeSwitch)
      << header_version_select(cfg, versions, curLocale) << header_theme_toggle(opt) << header_github(cfg)
      << "    </div>\n  </header>\n";
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

// 页脚数据子块（组件化：Footer.html 用 footer_show/footer_text/footer_links 组装）
static std::string footer_text_block(const SiteConfig& cfg) {
    return cfg.footer.text.empty() ? std::string()
        : "      <span class=\"footer-text\">" + esc(cfg.footer.text) + "</span>\n";
}
static std::string footer_links_block(const SiteConfig& cfg) {
    if (cfg.footer.links.empty()) return "";
    std::ostringstream o;
    o << "      <nav class=\"footer-links\">\n";
    for (const auto& l : cfg.footer.links)
        o << "        " << link_html(l) << "\n";
    o << "      </nav>\n";
    return o.str();
}
static std::string render_footer(const SiteConfig& cfg) {
    if (cfg.footer.text.empty() && cfg.footer.links.empty()) return "";
    std::ostringstream o;
    o << "  <footer class=\"site-footer\">\n    <div class=\"footer-inner\">\n"
      << footer_text_block(cfg) << footer_links_block(cfg)
      << "    </div>\n  </footer>\n";
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
//   条件块（Hugo 式，2026-08-03 新增）：{{ if KEY }} ... {{ end }}
//         KEY 值非空 → 渲染块内容（内部占位符/嵌套条件继续处理）；空/缺失 → 整块删除。
//         用途：{{ if left_nav }}左栏{{ end }}、{{ if toc_sidebar }}右栏{{ end }}——
//         首页（landing，left_nav/toc 为空）自动不渲染左右侧边栏组件。

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
        "{{ if left_nav }}\n"
        "    <aside class=\"sidebar left\">\n      <nav>\n"
        "{{left_nav}}      </nav>\n    </aside>\n"
        "{{ end }}\n"
        "    <main class=\"content\" id=\"main\">\n"
        "{{breadcrumb}}{{body}}{{last_updated}}{{edit_link}}{{pager}}    </main>\n"
        "{{ if toc_sidebar }}{{toc_sidebar}}{{ end }}  </div>\n"
        "{{footer}}{{back_to_top}}{{highlight_js}}{{search_js}}{{i18n_json}}{{feedback_js}}  <script defer src=\"{{base}}assets/js/app.js\"></script>\n"
        "</body>\n</html>\n";
}

// 主题根目录：优先 <engine>/theme（新规范，一个主题=一个文件夹）；
// 不存在时回退引擎根（旧结构：assets/、templates/ 直接在引擎根下）以兼容已有站点。

// ------------ 下一段 ------------


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

// ---------------- 组件系统（Vue 式：一个 .html 文件 = 一个组件，<PascalCase/> 挂载） ----------------
// 组件目录：<theme>/components/（组件文件里可用 {{key}} 占位符与 {{ if }} 条件块，数据由构建器喂）
// 挂载语法：<SiteHeader/>（自闭合）或 <SiteHeader></SiteHeader>（双标签，中间内容第一版忽略）
// 解析规则：标签首字母大写即组件（HTML 原生标签全小写，无歧义）；递归展开子组件；
//           循环引用（A 挂 B、B 挂 A）检测报错；嵌套深度上限 32。
// 兼容：components/ 目录不存在 → 组件标签原样保留（老主题零改动，走 C++ 硬编码子块）。


// ------------ 下一段 ------------

static std::string expand_components(const std::string& html, int depth,
                                     std::vector<std::string>& stack);

// 展开单个挂载点（自闭合 / 双标签）；name 已校验为合法组件名
static std::string expand_component_use(const std::string& name, int depth,
                                        std::vector<std::string>& stack,
                                        const std::string& rawUse) {
    if (depth > 32) {
        if (g_comp_warned.insert("depth:" + name).second) {
            std::cerr << color::warn("警告: ") << "组件嵌套过深（>32 层），疑似循环引用: ";
            for (const auto& s : stack) std::cerr << s << " → ";
            std::cerr << name << "\n";
        }
        return rawUse;   // 原样保留，避免死循环
    }
    // 循环引用检测：同名组件在展开栈中——若为「栈顶自递归」（递归组件，如导航树 NavItem）则合法，
    // 依赖深度上限兜底；异名循环（A→B→A）才报错。
    bool inStack = std::find(stack.begin(), stack.end(), name) != stack.end();
    bool selfRecurse = inStack && !stack.empty() && stack.back() == name;
    if (inStack && !selfRecurse) {
        if (g_comp_warned.insert("cycle:" + name).second) {
            std::cerr << color::warn("警告: ") << "组件循环引用: ";
            for (const auto& s : stack) std::cerr << s << " → ";
            std::cerr << name << "（该挂载点已移除）\n";
        }
        return {};   // 循环：移除该挂载点
    }
    std::string body = load_component(name);
    if (body.empty()) {
        if (g_comp_warned.insert("missing:" + name).second)
            std::cerr << color::warn("警告: ") << "组件文件不存在: components/" << name << ".html（标签原样保留）\n";
        return rawUse;   // 缺失：原样保留标签，便于排查
    }
    stack.push_back(name);
    std::string out = expand_components(body, depth + 1, stack);
    stack.pop_back();
    return out;
}

static std::string expand_components(const std::string& html, int depth,
                                     std::vector<std::string>& stack) {
    std::string out;
    out.reserve(html.size() + 512);
    size_t i = 0;
    while (i < html.size()) {
        // 模板块 {{ ... }}：整体跳过——块内组件标签由数据渲染驱动
        // （{{ if }} / {{ each }} 块在 tpl_render 时按数据展开；expand 只处理"非模板区域"的挂载点，
        //   否则 {{ each children }}<NavItem/>{{ end }} 里的 NavItem 会被无脑静态展开成无限递归）
        if (html[i] == '{' && i + 1 < html.size() && html[i + 1] == '{') {
            size_t end = html.find("}}", i + 2);
            if (end == std::string::npos) { out += html[i]; ++i; continue; }
            std::string tok = trim(html.substr(i + 2, end - i - 2));
            if (tok.rfind("if ", 0) == 0 || tok.rfind("each ", 0) == 0) {
                // 条件/循环块：跳到匹配的 {{ end }}（嵌套计数；{{ else }} 是块内分界不计数）
                size_t j = end + 2;
                int d = 1;
                while (j < html.size() && d > 0) {
                    size_t nxt = html.find("{{", j);
                    if (nxt == std::string::npos) break;
                    size_t nEnd = html.find("}}", nxt + 2);
                    if (nEnd == std::string::npos) break;
                    std::string tk2 = trim(html.substr(nxt + 2, nEnd - nxt - 2));
                    if (tk2.rfind("if ", 0) == 0 || tk2.rfind("each ", 0) == 0) ++d;
                    else if (tk2 == "end") { --d; if (d == 0) { j = nEnd + 2; break; } }
                    j = nEnd + 2;
                }
                out.append(html, i, j - i);   // 整块原样保留
                i = j;
                continue;
            }
            out.append(html, i, end - i + 2);   // 普通占位符原样跳过
            i = end + 2;
            continue;
        }
        if (html[i] == '<' && i + 1 < html.size() && isupper((unsigned char)html[i + 1])) {
            size_t gt = html.find('>', i + 1);
            if (gt != std::string::npos) {
                std::string tag = html.substr(i + 1, gt - i - 1);
                bool selfClose = !tag.empty() && tag.back() == '/';
                std::string name = selfClose ? tag.substr(0, tag.size() - 1) : tag;
                bool valid = !name.empty() && isupper((unsigned char)name[0]);
                for (char c : name)
                    if (!isalnum((unsigned char)c)) { valid = false; break; }
                if (valid) {
                    if (selfClose) {
                        out += expand_component_use(name, depth, stack, html.substr(i, gt - i + 1));
                        i = gt + 1;
                        continue;
                    }
                    // 双标签：找 </Name>
                    std::string closeTag = "</" + name + ">";
                    size_t close = html.find(closeTag, gt + 1);
                    if (close != std::string::npos) {
                        out += expand_component_use(name, depth, stack,
                                                    html.substr(i, close + closeTag.size() - i));
                        i = close + closeTag.size();
                        continue;
                    }
                    // 未闭合：fall through 原样
                }
            }
        }
        out += html[i];
        ++i;
    }
    return out;
}

// ---------------- 模板渲染（json 数据驱动：占位符 + 条件块 + 循环 + 块内组件） ----------------
// 语法（对齐 Hugo / Vue 心智）：
//   {{key}}          → 标量替换（data[key] 字符串/数字/布尔；缺失原样保留，i18n 键留给构建后替换）
//   {{ if KEY }}...{{ end }}    → 条件块：KEY 真值（非空串/非空数组/true）渲染块内，否则删除
//   {{ each KEY }}...{{ end }}  → 循环块：KEY 为数组 → 每项渲染块内；块内上下文 = 当前项合并全局
//                                 （块内 {{field}} 取当前项字段，{{ if }} / {{ each }} 可嵌套；
//                                  块内的 <组件/> 标签在「当前项上下文」中展开——支持递归树组件）
// 未闭合块原样保留（不破坏模板）。


// ------------ 下一段 ------------

static std::string tpl_render(const std::string& tpl, const json& data,

// ------------ 下一段 ------------

static std::string tpl_render(const std::string& tpl, const json& data,
                              std::vector<std::string>& compStack, int compDepth) {
    std::string out;
    out.reserve(tpl.size() + 256);
    size_t i = 0;
    while (i < tpl.size()) {
        if (tpl[i] == '{' && i + 1 < tpl.size() && tpl[i + 1] == '{') {
            size_t end = tpl.find("}}", i + 2);
            if (end != std::string::npos) {
                std::string tok = trim(tpl.substr(i + 2, end - i - 2));
                bool isIf   = tok.rfind("if ", 0) == 0 && tok.size() > 3;
                bool isEach = tok.rfind("each ", 0) == 0 && tok.size() > 5;
                if (isIf || isEach) {
                    std::string key = trim(tok.substr(isEach ? 5 : 3));
                    // 定位匹配的 {{ end }}（支持嵌套：{{ if }}/{{ each }} 计数 +1；块内 {{ else }} 分界）
                    size_t j = end + 2;
                    int depth = 1;
                    size_t innerStart = end + 2;
                    size_t blockEnd = std::string::npos;
                    size_t elsePos = std::string::npos;   // 顶层 {{ else }} 位置
                    while (j < tpl.size() && depth > 0) {
                        size_t nxt = tpl.find("{{", j);
                        if (nxt == std::string::npos) break;
                        size_t nEnd = tpl.find("}}", nxt + 2);
                        if (nEnd == std::string::npos) break;
                        std::string tk2 = trim(tpl.substr(nxt + 2, nEnd - nxt - 2));
                        if (tk2.rfind("if ", 0) == 0 || tk2.rfind("each ", 0) == 0) { ++depth; }
                        else if (tk2 == "end") {
                            --depth;
                            if (depth == 0) {
                                std::string inner = tpl.substr(innerStart, nxt - innerStart);
                                std::string partA = inner, partB;
                                if (elsePos != std::string::npos) {
                                    partA = inner.substr(0, elsePos - innerStart);
                                    partB = inner.substr(elsePos - innerStart + 10);   // 跳过 "{{ else }}"
                                }
                                if (isEach) {
                                    const json* av = json_get_path(data, key);
                                    if (av && av->is_array() && !av->empty()) {
                                        for (const auto& item : *av) {
                                            json ctx = data;                    // 全局数据副本
                                            if (item.is_object())
                                                for (auto it = item.begin(); it != item.end(); ++it)
                                                    ctx[it.key()] = it.value();  // 当前项字段覆盖
                                            // 块内管线：渲染 → 展开组件（当前项上下文）→ 再渲染
                                            std::string part = tpl_render(partA, ctx, compStack, compDepth);
                                            part = expand_components(part, compDepth, compStack);
                                            out += tpl_render(part, ctx, compStack, compDepth);
                                        }
                                    } else if (!partB.empty()) {
                                        out += tpl_render(partB, data, compStack, compDepth);
                                    }
                                } else {
                                    const json* cv = json_get_path(data, key);
                                    std::string part = (cv && tpl_truthy(*cv)) ? partA : partB;
                                    out += tpl_render(part, data, compStack, compDepth);
                                }
                                blockEnd = nEnd + 2;
                                break;
                            }
                        }
                        else if (depth == 1 && tk2 == "else" && elsePos == std::string::npos)
                            elsePos = nxt;
                        j = nEnd + 2;
                    }
                    if (blockEnd != std::string::npos) { i = blockEnd; continue; }
                    // 未闭合块：fall through 原样保留
                }
                // 标量占位符 {{key}}（支持嵌套路径 {{a.b.c}}）
                const json* pv = (!tok.empty()) ? json_get_path(data, tok) : nullptr;
                if (pv) {
                    out += json_scalar(*pv);
                    i = end + 2;
                    continue;
                }
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

// ------------ 下一段 ------------

static std::string build_breadcrumb_html(const json& crumbs) {
    std::ostringstream o;
    o << "  <nav class=\"breadcrumb\" aria-label=\"面包屑\">\n"
      << "    <a href=\"{{base}}index.html\">{{home}}</a>\n";
    for (const auto& c : crumbs) {
        o << "    <span class=\"sep\">/</span>\n";
        std::string href = c.value("href", "");
        if (href.empty())
            o << "    <span class=\"crumb\">" << esc(c.value("title", "")) << "</span>\n";
        else
            o << "    <a class=\"crumb\" href=\"" << esc_attr(href) << "\">" << esc(c.value("title", "")) << "</a>\n";
    }
    o << "  </nav>\n";
    return o.str();
}
static std::string build_toc_html(const json& items) {
    std::ostringstream o;
    o << "<nav class=\"toc-nav\">\n      <div class=\"toc-title\">{{tocTitle}}</div>\n"
      << "      <ul class=\"toc-list\">\n";
    for (const auto& it : items)
        o << "        <li class=\"toc-h" << it.value("level", 2) << "\"><a href=\"#"
          << esc_attr(it.value("id", "")) << "\">" << esc(it.value("text", "")) << "</a></li>\n";
    o << "      </ul>\n    </nav>\n";
    return o.str();
}
static std::string build_pager_html(const json& pg) {
    std::ostringstream o;
    o << "<nav class=\"pager\">\n";
    const json& prev = pg.value("prev", json());
    if (prev.value("show", false))
        o << "  <a class=\"pager-item prev\" href=\"" << esc_attr(prev.value("href", "")) << "\">\n"
          << "    <span class=\"pager-label\">{{prevLabel}}</span>\n"
          << "    <span class=\"pager-title\">" << esc(prev.value("title", "")) << "</span>\n  </a>\n";
    else
        o << "  <span class=\"pager-item prev disabled\" aria-disabled=\"true\">\n"
          << "    <span class=\"pager-label\">{{prevLabel}}</span>\n"
          << "    <span class=\"pager-title\">{{pagerNone}}</span>\n  </span>\n";
    const json& next = pg.value("next", json());
    if (next.value("show", false))
        o << "  <a class=\"pager-item next\" href=\"" << esc_attr(next.value("href", "")) << "\">\n"
          << "    <span class=\"pager-label\">{{nextLabel}}</span>\n"
          << "    <span class=\"pager-title\">" << esc(next.value("title", "")) << "</span>\n  </a>\n";
    else
        o << "  <span class=\"pager-item next disabled\" aria-disabled=\"true\">\n"
          << "    <span class=\"pager-label\">{{nextLabel}}</span>\n"
          << "    <span class=\"pager-title\">{{pagerNone}}</span>\n  </span>\n";
    o << "</nav>\n";
    return o.str();
}

// ---- 组件数据辅助（json 纯数据） ----

// ------------ 下一段 ------------

static std::string build_hero_html(const json& hero) {
    if (hero.is_null() || !hero.contains("title")) return "";
    std::ostringstream o;
    o << "<section class=\"hero\">\n  <h1>" << esc(hero.value("title", "")) << "</h1>\n";
    if (hero.contains("subtitle") && !hero["subtitle"].is_null())
        o << "  <p class=\"subtitle\">" << esc(hero.value("subtitle", "")) << "</p>\n";
    if (hero.contains("cta_href") && !hero["cta_href"].is_null())
        o << "  <a class=\"cta\" href=\"" << esc_attr(hero.value("cta_href", "")) << "\">"
          << esc(hero.value("cta_text", "{{getStarted}}")) << "</a>\n";
    o << "</section>\n";
    return o.str();
}
static std::string build_cards_html(const json& cards) {
    if (!cards.is_array() || cards.empty()) return "";
    std::ostringstream o;
    o << "<section class=\"cards\">\n";
    for (const auto& c : cards) {
        o << "  <a class=\"card\" href=\"" << esc_attr(c.value("href", "")) << "\"><h3>"
          << esc(c.value("title", "")) << "</h3>";
        if (c.contains("desc") && !c["desc"].is_null())
            o << "<p class=\"card-desc\">" << esc(c.value("desc", "")) << "</p>";
        o << "</a>\n";
    }
    o << "</section>\n";
    return o.str();
}

// 整页渲染（组件模式）：读模板 → 展开组件树 → 数据注入（多遍交替直到稳定）
// 组件模式（components/ 存在）：C++ 只产 json 数据，HTML 全部在组件文件；
// fallback（无组件目录，老主题）：旧 HTML 函数 + 占位符模板，零改动兼容。

// ------------ 下一段 ------------

static std::string render_page(const SiteConfig& cfg, const RenderOpts& opt, const PageCtx& pcx) {
    const std::string& relBase = pcx.relBase;
    const bool isHome = pcx.is_home;
    const std::string& curLocale = pcx.curLocale;
    std::string title = pcx.title.empty() ? cfg.title : (pcx.title + " · " + cfg.title);
    std::string lang = curLocale.empty() ? "zh-CN" : curLocale;
    bool compMode = fs::is_directory(components_dir());

    // 语言切换链接在子目录页需加深一级：href="../<loc>/" → relBase+"../<loc>/"
    std::string lsFinal = pcx.localeSwitch;
    if (!lsFinal.empty() && !relBase.empty())
        lsFinal = replace_all(lsFinal, "href=\"../", "href=\"" + relBase + "../");

    if (!compMode) {
        // ---- fallback：老主题（无 components/），旧 HTML 函数 + 占位符模板，行为与旧版一致 ----
        std::vector<std::string> stk;
        std::string leftNav = pcx.fallbackLeftNav;
        std::string tocSide = (opt.showToc && !pcx.toc_items.empty())
            ? "    <aside class=\"sidebar right\">\n" + build_toc_html(pcx.toc_items) + "    </aside>\n" : std::string();
        std::string header = rtrim_nl(render_header(cfg, opt, curLocale, lsFinal, cfg.versions, relBase, isHome));
        std::string foot = render_footer(cfg);
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
        std::string hlJs = opt.showCodeHighlight
            ? "  <script defer src=\"" + relBase + "assets/deps/highlight.min.js\"></script>\n" : std::string();
        std::string searchJs = opt.showSearch
            ? "  <script defer src=\"" + relBase + "assets/deps/flexsearch.bundle.min.js\"></script>\n" : std::string();
        std::string i18nScript = pcx.i18nJson.empty() ? std::string()
            : "  <script>window.__I18N__ = " + pcx.i18nJson + ";</script>\n";
        std::string fbScript = cfg.feedbackEndpoint.empty() ? std::string()
            : "  <script>window.__CDOCS_FEEDBACK__ = \"" + esc_attr(cfg.feedbackEndpoint) + "\";</script>\n";
        std::string bc = pcx.breadcrumb.empty() ? std::string()
            : build_breadcrumb_html(pcx.breadcrumb) + "\n";
        std::string lu = pcx.last_updated.empty() ? std::string()
            : "  <div class=\"page-meta\">" + esc(pcx.last_updated) + "</div>\n";
        std::string pg = (opt.showPager && pcx.pager.value("show", false))
            ? build_pager_html(pcx.pager) : std::string();
        // 首页（fallback）：body = hero + cards（从数据重建，行为与旧 landing_body 一致）
        std::string bodyHtml = isHome
            ? build_hero_html(pcx.hero) + build_cards_html(pcx.cards)
            : pcx.body;
        std::string metaDesc = pcx.desc.empty() ? std::string()
            : "  <meta name=\"description\" content=\"" + esc_attr(pcx.desc) + "\">\n";
        std::string hlCss = opt.showCodeHighlight
            ? "  <link rel=\"stylesheet\" href=\"" + relBase + "assets/deps/highlight-theme.css\">\n" : std::string();
        std::string skipLabel = (curLocale == "en") ? "Skip to main content" : "跳到主要内容";
        std::string skipLink = "<a class=\"skip-link\" href=\"#main\" aria-label=\""
            + esc_attr(skipLabel) + "\">" + esc(skipLabel) + "</a>";
        std::string out = tpl_render(load_layout_template(), {
            {"lang", esc_attr(lang)}, {"theme", esc(cfg.theme)}, {"title", esc(title)},
            {"base", relBase}, {"body_class", isHome ? " class=\"page-home\"" : ""},
            {"meta_desc", metaDesc}, {"highlight_css", hlCss},
            {"custom_head", cfg.customCssLink + cfg.themeVars + pcx.extra_head},
            {"skip_link", skipLink}, {"header", header}, {"left_nav", pcx.fallbackLeftNav},
            {"breadcrumb", bc}, {"body", bodyHtml + "\n"},
            {"last_updated", lu}, {"edit_link", pcx.edit.value("show", false)
                ? "  <div class=\"edit-link\"><a href=\"" + esc_attr(pcx.edit.value("href", "")) + "\" target=\"_blank\" rel=\"noopener\">{{editThisPage}}</a></div>\n"
                : std::string()},
            {"pager", pg + pcx.body_end},
            {"toc_sidebar", tocSide}, {"footer", foot}, {"back_to_top", backTop},
            {"highlight_js", hlJs}, {"search_js", searchJs},
            {"i18n_json", i18nScript}, {"feedback_js", fbScript}
        }, stk, 0);
        out = expand_components(out, 0, stk);
        out = tpl_render(out, {
            {"lang", esc_attr(lang)}, {"theme", esc(cfg.theme)}, {"title", esc(title)},
            {"base", relBase}, {"body_class", isHome ? " class=\"page-home\"" : ""},
            {"meta_desc", metaDesc}, {"highlight_css", hlCss},
            {"custom_head", cfg.customCssLink + cfg.themeVars + pcx.extra_head},
            {"skip_link", skipLink}, {"header", header}, {"left_nav", pcx.fallbackLeftNav},
            {"breadcrumb", bc}, {"body", bodyHtml + "\n"},
            {"last_updated", lu}, {"edit_link", pcx.edit.value("show", false)
                ? "  <div class=\"edit-link\"><a href=\"" + esc_attr(pcx.edit.value("href", "")) + "\" target=\"_blank\" rel=\"noopener\">{{editThisPage}}</a></div>\n"
                : std::string()},
            {"pager", pg + pcx.body_end},
            {"toc_sidebar", tocSide}, {"footer", foot}, {"back_to_top", backTop},
            {"highlight_js", hlJs}, {"search_js", searchJs},
            {"i18n_json", i18nScript}, {"feedback_js", fbScript}
        }, stk, 0);
        if (isHome) {
            size_t pl = out.find("<div class=\"layout\">");
            if (pl != std::string::npos)
                out.replace(pl, std::strlen("<div class=\"layout\">"), "<div class=\"layout no-sidebar\">");
        }
        return out;
    }

    // ---- 组件模式：构造 json 数据，多遍交替渲染（数据 → 展开组件 → 数据…直到稳定） ----
    json langSwitch = json::object();
    if (!lsFinal.empty()) langSwitch["html"] = lsFinal;   // 语言切换仍是短片段（构建期 i18n 链接）
    json data = {
        {"lang", esc_attr(lang)}, {"theme", esc(cfg.theme)}, {"title", esc(title)},
        {"base", relBase}, {"body_class", isHome ? " class=\"page-home\"" : ""},
        {"is_home", isHome},
        {"site_title", esc(cfg.title)}, {"site_desc", esc(cfg.description)},
        // head 区（MetaHead 组件）
        {"meta_desc", esc(pcx.desc)},
        {"extra_head", pcx.extra_head},
        {"show_highlight", opt.showCodeHighlight},
        {"theme_vars", cfg.themeVars}, {"custom_css_href", cfg.customCssHref},
        // header（Header 组件）
        {"header", header_json(cfg, opt, curLocale, langSwitch, relBase, isHome)},
        // center（LeftNav / Breadcrumb / Hero / CardGrid / Pager / EditLink / TOC）
        {"nav_tree", pcx.nav_tree},
        {"breadcrumb", pcx.breadcrumb},
        {"hero", pcx.hero},
        {"cards", pcx.cards},
        {"pager", pcx.pager},
        {"edit", pcx.edit},
        {"toc_items", pcx.toc_items},
        {"show_toc", opt.showToc && !pcx.toc_items.empty()},
        {"body", pcx.body},
        {"last_updated", esc(pcx.last_updated)},
        {"body_end", pcx.body_end},
        {"skip_label", (curLocale == "en") ? "Skip to main content" : "跳到主要内容"},
        // footer（Footer 组件）
        {"footer", footer_json(cfg)},
        // misc（BackToTop / Scripts）
        {"backtop", json{{"show", opt.showBackToTop}, {"threshold", cfg.backToTopThreshold},
                        {"label", (cfg.backToTopLabel == "↑ 顶部") ? "{{backToTop}}" : esc_attr(cfg.backToTopLabel)}}},
        {"scripts", json{{"highlight", opt.showCodeHighlight}, {"search", opt.showSearch},
                         {"i18n_json", pcx.i18nJson}, {"feedback", cfg.feedbackEndpoint}}}
    };
    // 收集合法模板键 → g_tpl_keys（L2 残留检测白名单：教学文档展示的占位符示例不算残留）
    for (auto it = data.cbegin(); it != data.cend(); ++it) g_tpl_keys.insert(it.key());
    std::string out = load_layout_template();
    std::vector<std::string> compStack;
    for (int pass = 0; pass < 64; ++pass) {
        out = tpl_render(out, data, compStack, 0);
        std::string ex = expand_components(out, 0, compStack);
        if (ex == out) break;
        out = ex;
    }
    if (isHome) {
        size_t pl = out.find("<div class=\"layout\">");
        if (pl != std::string::npos)
            out.replace(pl, std::strlen("<div class=\"layout\">"), "<div class=\"layout no-sidebar\">");
    }
    return out;
}

// 首页 Hero + 文档卡片
// config.home 可配：hero（title/subtitle/cta 覆盖）+ cards 白名单（顺序即展示顺序）；
// 未配置 home 时保持自动：Hero 取 config.title/description，卡片列出全部非草稿页。
// 首页 Hero（组件模式下与卡片分离：landing_hero 直接进 body，卡片由 <CardGrid/> 组件挂载）
static std::string landing_hero(const SiteConfig& cfg, const std::vector<Page>& pages) {
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
    return o.str();
}

// 首页文档卡片列表（CardGrid 组件的数据：{{cards_html}}；空 = 无卡片）
static std::string landing_cards(const SiteConfig& cfg, const std::vector<Page>& pages) {
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
    if (shown.empty()) return "";
    std::ostringstream o;
    for (const auto* p : shown) {
        std::string t = p->title;
        std::string d;
        for (const auto& hc : cfg.homeCards)
            if (hc.file == p->file) { if (!hc.title.empty()) t = hc.title; d = hc.desc; break; }
        o << "  <a class=\"card\" href=\"" << esc_attr(p->file) << ".html\"><h3>" << esc(t) << "</h3>";
        if (!d.empty()) o << "<p class=\"card-desc\">" << esc(d) << "</p>";
        o << "</a>\n";
    }
    return o.str();
}

// 首页完整内容（fallback：无 components/ 时 landing 的 body 用整块，行为与旧版一致）

// ------------ 下一段 ------------

static std::string landing_body(const SiteConfig& cfg, const std::vector<Page>& pages) {
    std::string cards = landing_cards(cfg, pages);
    return landing_hero(cfg, pages) + (cards.empty() ? std::string() : "<section class=\"cards\">\n" + cards + "</section>\n");
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
