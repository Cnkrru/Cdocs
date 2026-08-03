// output.cpp —— 构建产物收尾
// （自 builder.cpp 拆分：根重定向 / 根 feed+PWA / sitemap / robots / 构建汇总 / 残留检测）
// 这些函数在 run_build 主流程末尾依次调用，全部只读 BuildContext + 全局状态，
// 不参与页面渲染——独立成模块使 builder.cpp 只剩"配置→收集→渲染"核心管线。

#include "output.hpp"
#include "builder.hpp"      // BuildContext
#include "component.hpp"    // theme_root（PWA 图标源目录）
#include "feeds.hpp"        // gen_feeds
#include "pwa.hpp"          // gen_pwa
#include "i18n.hpp"         // i18n_replace
#include <fstream>
#include <sstream>
#include <regex>
#include <set>
#include <algorithm>

// 4) 多语言根 index.html 重定向到默认语言（单语言模式已在循环中生成，无需重定向）
void write_root_redirect(BuildContext& b) {
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
void write_root_feeds_pwa(BuildContext& b) {
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
void write_sitemap(BuildContext& b) {
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
void write_robots(BuildContext& b) {
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
void print_summary(BuildContext& b) {
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
        std::cout << color::muted("配置: config.json + route/ | 插件: ");
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

// ============ L2: 构建期残留检测（把模板语法的"静默失败"变成显式警告） ============
// 扫描输出目录所有 .html，三类残留（对标 Hugo/Vue/Astro 的 fail-fast 哲学，不阻塞构建）：
//   1) 模板块残留 {{ if/each/else/end ... }} → 语法错误级警告（页面将显示语法原文，
//      通常是对应块未闭合，tpl_render 无法定位匹配的 {{ end }}）
//   2) 未解析数据键 {{含下划线的 key}} → 警告（模板数据键拼错；
//      纯单词/驼峰键是客户端 i18n（{{navHome}}/{{minutes}}），保留给前端 JS 替换，不报）
//   3) 大写组件标签残留 <PascalCase> → 警告（组件未展开，通常因组件文件缺失/循环引用，
//      或 expand 未覆盖到该处；跳过 <pre> 代码块内的示例）
void scan_output_leftovers(const fs::path& outDir) {
    std::error_code ec;
    if (!fs::is_directory(outDir, ec)) return;
    int total = 0;
    for (auto it = fs::recursive_directory_iterator(outDir, ec), end = fs::recursive_directory_iterator();
         it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (!it->is_regular_file(ec)) continue;
        if (it->path().extension().string() != ".html") continue;
        std::string html = read_file(it->path());
        // 剔除 <pre>...</pre> 代码块（文档示例会故意包含 {{}} / 大写标签）
        std::string s;
        {
            size_t i = 0;
            while (i < html.size()) {
                size_t pre = html.find("<pre", i);
                if (pre == std::string::npos) { s += html.substr(i); break; }
                s += html.substr(i, pre - i);
                size_t pe = html.find("</pre>", pre + 4);
                if (pe == std::string::npos) { s += html.substr(pre); break; }
                i = pe + 6;
            }
        }
        if (s.empty()) continue;
        std::vector<std::string> found;
        static const std::regex reBlock(R"(\{\{\s*(if|each|else|end)\b[^}]*\}\})");
        static const std::regex reKey(R"(\{\{[a-z][a-z0-9_]*_[a-z0-9_.]*\}\})");
        static const std::regex reComp(R"(<([A-Z][A-Za-z0-9]*)(\s[^>]*)?\s*\/?>)");
        for (auto m = std::sregex_iterator(s.begin(), s.end(), reBlock); m != std::sregex_iterator(); ++m)
            found.push_back("模板块残留 " + m->str());
        for (auto m = std::sregex_iterator(s.begin(), s.end(), reKey); m != std::sregex_iterator(); ++m) {
            // 教学文档会故意展示 {{left_nav}} 这类占位符示例（行内 <code>）——
            // 键在合法集合（当前 data 键 + fallback 历史键）中则跳过，只有真拼错的键才报
            static const std::set<std::string> kLegacyKeys = {
                // fallback 时代的模板占位符键（themes.md 等教学文档仍在展示）
                "skip_link","header","left_nav","breadcrumb","edit_link","pager","toc_sidebar",
                "footer","back_to_top","highlight_js","search_js","i18n_json","feedback_js",
                "highlight_css","meta_desc","custom_head","last_updated","body","body_class",
                // shortcode 组件数据孔（shortcodes.md 等教学文档展示 {{slot}}/{{slot_raw}}）
                "slot","slot_raw",
                // shortcode 组件数据孔（shortcodes.md 等教学文档展示 {{slot}}/{{slot_raw}}）
                "slot","slot_raw",
                // 组件子块键（Header/Footer/CardGrid 拆分时的数据键，文档有展示）
                "left_nav_tree","cards_html","menu_toggle","logo","topnav","search","header_nav",
                "locale_switch","version_select","theme_toggle","github_link",
                "footer_show","footer_text","footer_links","extra_head"
            };
            std::string key = m->str();
            key = key.substr(2, key.size() - 4);          // 剥掉 {{ }}
            if (g_tpl_keys.count(key) || kLegacyKeys.count(key)) continue;
            found.push_back("未解析数据键 " + m->str());
        }
        for (auto m = std::sregex_iterator(s.begin(), s.end(), reComp); m != std::sregex_iterator(); ++m)
            found.push_back("未展开组件 <" + m->str(1) + ">");
        if (found.empty()) continue;
        std::sort(found.begin(), found.end());
        found.erase(std::unique(found.begin(), found.end()), found.end());
        total += (int)found.size();
        if (!g_quiet) {
            std::cerr << color::warn("警告: ") << "模板残留 " << found.size() << " 处 → "
                      << fs::relative(it->path(), outDir).string() << "\n";
            for (const auto& f : found) std::cerr << "      · " << f << "\n";
        }
    }
    if (total && !g_quiet)
        std::cout << color::muted("  残留检查: ") << color::red(std::to_string(total))
                  << color::muted(" 处模板残留（{{}} 模板块/数据键/组件标签），请检查上方警告\n");
}
