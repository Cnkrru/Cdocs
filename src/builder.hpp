// builder.hpp —— 构建编排（run_build）

#ifndef CDOCS_BUILDER_HPP
#define CDOCS_BUILDER_HPP

#include "core.hpp"

// 构建上下文（run_build 内部状态，跨构建阶段共享）
struct BuildContext {
    fs::path in_dir;
    fs::path out_dir;
    bool includeDrafts = false;
    SiteConfig cfg;
    I18nCfg i18n;
    I18nDict fallbackUI;
    RenderOpts opt;
    std::vector<Page> pages;
    std::vector<Page> blog_posts;          // 博客流：md/blog/ 收集的文章（按 date 倒序）
    std::vector<NavNode> blogNav;          // 博客区独立侧边栏（config.sidebar["blog"]；空 = 沿用文档导航）
    json query_out;                        // on_data_query 插件输出：blog_order / blog_pages / home_posts / tags / tag_pages
    bool query_ready = false;              // 插件已产出查询结果（false = 无查询 → 纯文档站，无博客/标签功能）
    // ---- 增量构建状态（--watch 热重载加速；普通 build 全量） ----
    bool incremental = false;              // 本次构建是否启用增量（serve -w 置位）
    std::map<std::string, std::string> pageSig;  // file+loc -> "mtime:size"（源 .md 指纹）
    bool globalDirty = true;               // 配置/导航/i18n/模板任一变化 → 全量重建
};

// 构建静态站点（默认 docs → dist）
int run_build(fs::path in_dir, fs::path out_dir, bool includeDrafts, bool cleanBefore);

#endif  // CDOCS_BUILDER_HPP
