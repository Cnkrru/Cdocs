// render_pages.hpp —— 页面类型渲染（每语言构建的页面渲染子函数）
// （自 builder.cpp render_one_locale 拆分：首页/文档页/博客流/搜索索引/标签聚合/single + head 数据）
// 渲染管线：render_one_locale 准备资产 → 调本模块函数渲染各页面类型 → feeds/PWA 收尾。

#ifndef CDOCS_RENDER_PAGES_HPP
#define CDOCS_RENDER_PAGES_HPP

#include "core.hpp"
#include "builder.hpp"   // BuildContext

// 每语言渲染上下文（render_one_locale 准备的共享状态，收敛跨函数参数）
struct LocaleRenderCtx {
    const json& maps;          // 页面类型注册表（config/map.json）
    const I18nDict& dict;      // 当前语言 UI 字典
    std::string i18nJson;      // 字典 JSON（注入页面供 app.js）
    std::string curLocale;     // 当前语言（空 = 单语言）
    std::string homeBase;      // 站点基址前缀（url + 语言）
    std::string feedTitle;     // RSS 标题（i18n 解析后）
    bool hasFeed = false;      // 站点有订阅流（博客非空）→ head 输出 RSS alternate link
    std::string ogImageUrl;    // 社交分享封面 URL
    json langData;             // 语言切换数据
    fs::path locOut;           // 本语言输出目录
};

// head 数据（canonical/prev/next/hreflang/JSON-LD/OG/Twitter——MetaLink/MetaOg 组件渲染）
json build_head_data(BuildContext& b, const LocaleRenderCtx& rc,
                     const std::string& file, int depth,
                     const std::string& title, const std::string& desc,
                     std::time_t published, std::time_t modified, bool article,
                     const std::vector<std::string>& crumbs,
                     const std::string& prevFile, const std::string& nextFile);

// 6 种页面类型渲染
void render_home(BuildContext& b, const LocaleRenderCtx& rc);
void render_doc_pages(BuildContext& b, const LocaleRenderCtx& rc);
void render_blog(BuildContext& b, const LocaleRenderCtx& rc);
void render_search_index(BuildContext& b, const LocaleRenderCtx& rc);
void render_tags(BuildContext& b, const LocaleRenderCtx& rc);
void render_markets(BuildContext& b, const LocaleRenderCtx& rc);   // 插件/主题市场页
void render_single(BuildContext& b, const LocaleRenderCtx& rc);

#endif  // CDOCS_RENDER_PAGES_HPP
