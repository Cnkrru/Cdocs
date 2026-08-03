// feeds.cpp —— RSS / JSON Feed 生成实现（自 main.cpp 原样搬迁）

#include "feeds.hpp"
#include "i18n.hpp"
#include <functional>   // std::hash（feed 签名）

// feed 摘要：取正文首段纯文本（去标签/去 markdown 语法），截断到 ~280 字
static std::string feed_excerpt(const std::string& html) {
    std::string txt = collapse_ws(strip_tags(html));
    if (txt.size() > 280) txt = truncate_utf8(txt, 277) + "…";
    return txt;
}

// RSS 2.0 + JSON Feed 生成（每语言一份，i18n 站点另在站点根生成默认语言版）
// 只应收订阅流（博客文章）：文档页不进 feed（RSS 语义 + 性能）；无订阅流时跳过。
void gen_feeds(const fs::path& out, const std::string& loc,
                      const std::vector<Page>& pages, const SiteConfig& cfg,
                      const I18nDict& dict, bool multi, bool silent) {
    if (pages.empty()) return;   // 无订阅流（纯文档站 / 无博客版本）→ 不生成 feed
    std::string siteUrl = cfg.url;
    if (!siteUrl.empty() && siteUrl.back() == '/') siteUrl.pop_back();
    std::string siteTitle = i18n_replace(cfg.title, dict);
    if (siteTitle.empty()) siteTitle = "Docs";
    std::string siteDesc  = i18n_replace(cfg.description, dict);
    auto linkFor = [&](const std::string& file) {
        std::string u = siteUrl;
        if (!u.empty()) u += "/";
        if (multi) u += loc + "/";
        u += file + ".html";
        return u;
    };
    // 预计算摘要（每页一次，RSS description 与 JSON summary 共用——避免重复 strip_tags）
    std::vector<std::string> excerpts;
    excerpts.reserve(pages.size());
    for (const auto& p : pages)
        excerpts.push_back(p.draft ? std::string() : feed_excerpt(p.html));
    // ---- RSS 2.0 ----
    std::ostringstream rss;
    rss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<rss version=\"2.0\" xmlns:atom=\"http://www.w3.org/2005/Atom\">\n"
        << "  <channel>\n"
        << "    <title>" << esc(siteTitle) << "</title>\n";
    if (!siteUrl.empty())
        rss << "    <link>" << esc(siteUrl) << "</link>\n";
    rss << "    <description>" << esc(siteDesc) << "</description>\n"
        << "    <language>" << esc(loc.empty() ? std::string("zh-CN") : loc) << "</language>\n"
        << "    <lastBuildDate>" << fmt822(std::time(nullptr)) << "</lastBuildDate>\n";
    if (!siteUrl.empty())
        rss << "    <atom:link rel=\"self\" href=\"" << esc(siteUrl + "/rss.xml")
            << "\" type=\"application/rss+xml\" />\n";
    for (size_t i = 0; i < pages.size(); ++i) {
        const auto& p = pages[i];
        if (p.draft) continue;
        std::time_t pub = p.dateT ? p.dateT : std::time(nullptr);
        rss << "    <item>\n"
            << "      <title>" << esc(i18n_replace(p.title, dict)) << "</title>\n"
            << "      <link>" << esc(linkFor(p.file)) << "</link>\n"
            << "      <guid>" << esc(linkFor(p.file)) << "</guid>\n"
            << "      <description>" << esc(excerpts[i]) << "</description>\n"
            << "      <pubDate>" << fmt822(pub) << "</pubDate>\n"
            << "    </item>\n";
    }
    rss << "  </channel>\n</rss>\n";
    std::ofstream(out / "rss.xml") << rss.str();
    // ---- JSON Feed 1.1 ----
    json jf = json::object();
    jf["version"] = "https://jsonfeed.org/version/1.1";
    jf["title"] = siteTitle;
    jf["description"] = siteDesc;
    if (!siteUrl.empty()) { jf["home_page_url"] = siteUrl; jf["feed_url"] = siteUrl + "/feed.json"; }
    json arr = json::array();
    for (size_t i = 0; i < pages.size(); ++i) {
        const auto& p = pages[i];
        if (p.draft) continue;
        json it = json::object();
        std::string u = linkFor(p.file);
        it["id"] = u; it["url"] = u;
        it["title"] = i18n_replace(p.title, dict);
        it["summary"] = excerpts[i];
        std::time_t pub = p.dateT ? p.dateT : std::time(nullptr);
        it["date_published"] = iso8601(pub);
        arr.push_back(it);
    }
    jf["items"] = arr;
    std::ofstream(out / "feed.json") << jf.dump(2);
    if (!silent)
        std::cout << color::green("已生成 ") << (loc.empty() ? std::string("") : loc + "/")
                  << color::green("rss.xml / feed.json") << "（" << arr.size() << " 条）\n";
}

// feed 内容签名：博客集（file + date + 渲染 HTML 指纹）+ 相关配置。
// 增量构建时与 .build/.feeds.sig 比对，未变则跳过 gen_feeds（复用旧产物）。
std::string feed_sig(const std::vector<Page>& posts, const SiteConfig& cfg,
                     const std::string& loc, bool multi) {
    std::string s = loc + "|" + (multi ? "1" : "0") + "|" + cfg.url + "|"
                  + cfg.title + "|" + cfg.description + "|";
    for (const auto& p : posts) {
        if (p.draft) continue;
        s += p.file + "|" + std::to_string(p.dateT) + "|"
           + std::to_string(p.html.size()) + "|"
           + std::to_string(std::hash<std::string>{}(p.html)) + ";";
    }
    return s;
}
