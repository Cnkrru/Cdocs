// feeds.cpp —— RSS / JSON Feed 生成实现（自 main.cpp 原样搬迁）

#include "feeds.hpp"
#include "i18n.hpp"

// feed 摘要：取正文首段纯文本（去标签/去 markdown 语法），截断到 ~280 字
static std::string feed_excerpt(const std::string& html) {
    std::string txt = collapse_ws(strip_tags(html));
    if (txt.size() > 280) txt = truncate_utf8(txt, 277) + "…";
    return txt;
}

// RSS 2.0 + JSON Feed 生成（每语言一份，i18n 站点另在站点根生成默认语言版）
void gen_feeds(const fs::path& out, const std::string& loc,
                      const std::vector<Page>& pages, const SiteConfig& cfg,
                      const I18nDict& dict, bool multi, bool silent) {
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
    for (const auto& p : pages) {
        if (p.draft) continue;
        std::time_t pub = p.dateT ? p.dateT : std::time(nullptr);
        rss << "    <item>\n"
            << "      <title>" << esc(i18n_replace(p.title, dict)) << "</title>\n"
            << "      <link>" << esc(linkFor(p.file)) << "</link>\n"
            << "      <guid>" << esc(linkFor(p.file)) << "</guid>\n"
            << "      <description>" << esc(feed_excerpt(p.html)) << "</description>\n"
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
    for (const auto& p : pages) {
        if (p.draft) continue;
        json it = json::object();
        std::string u = linkFor(p.file);
        it["id"] = u; it["url"] = u;
        it["title"] = i18n_replace(p.title, dict);
        it["summary"] = feed_excerpt(p.html);
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
