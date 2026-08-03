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
// 并发渲染 worker pool（Hugo 式：多核并行渲染页面；render_pages.cpp 跨文件调用）
void run_parallel(size_t n_tasks, const std::function<void(size_t)>& fn);

struct PageCtx;   // ctxdata.hpp 定义（前向声明：map_render_page 用引用，避免与 ctxdata.hpp 循环 include）

// 地图模式整页渲染（读 theme/map/<type>.html 按图拼接，render_pages.cpp 跨文件调用）
std::string map_render_page(const SiteConfig& cfg, const RenderOpts& opt,
                            const PageCtx& pcx, const std::string& mapType, bool isHome = false);
// 按 mode 查找页面类型名（maps 注册表）
std::string type_for_mode(const json& maps, const std::string& mode, const std::string& def);

int run_build(fs::path in_dir, fs::path out_dir, bool includeDrafts, bool cleanBefore);

#endif  // CDOCS_BUILDER_HPP
