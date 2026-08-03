// ctxdata.hpp —— 页面数据装配：C++ 数据 → 组件可消费的 json
// （自 builder.cpp 拆分：PageCtx 上下文 + link/nav/cards/pager/header/footer 等 json 装配函数）
// 原则：C++ 只产数据（PageCtx + 环境数据），HTML 一律在 theme/map/*.json + components/*.html。

#ifndef CDOCS_CTXDATA_HPP
#define CDOCS_CTXDATA_HPP

#include "core.hpp"
#include "builder.hpp"   // BuildContext（header_json 用到 opt）

// ============ 页面渲染上下文（组件数据层：C++ 只产数据，HTML 一律在 components/*.html） ============
struct PageCtx {
    json nav_groups = json::array();  // 左导航 2 层展平（地图模式 NavGroup/NavItem：[{title, items:[{title,url,active_class}]}]）
    json toc_items = json::array();   // 目录项（h2-h4 平铺 [{level,text,id}]）
    json pager = json::object();      // {show, prev:{show,hidden,title,href}, next:{...}}
    json breadcrumb_map = json::object(); // 地图模式面包屑 {links:[{title,href}], texts:[{title}], current}
    json edit = json::object();       // {show, href, label}
    json hero = json::object();       // 首页 {title, subtitle, cta_text, cta_href}
    json cards = json::array();       // 首页卡片 [{title, desc, href}]
    json blog_posts = json::array();  // 博客列表项（BlogCard each：[{date, href, title, desc}]）
    json blog_pager = json::object(); // 博客分页 {show, prev_href, next_href, cur:{num}, pages:[{num,href}]}
    json tags = json::array();        // 标签聚合（TagItem each：[{name, href}]）
    std::string tag_name;             // 标签单页名（TagPage）
    json tag_docs = json::array();    // 标签单页文档（TagDocItem each：[{href, title}]）
    std::string body;          // 正文（markdown 内容 HTML——内容层，非组件）
    std::string title, desc;   // 页面标题 / meta 描述
    std::string last_updated;  // 「最后更新于 x · 约 n 分钟阅读」纯文本（LastUpdated 组件渲染）
    std::string body_end;      // 正文末尾注入（插件 HTML，如评论——外部插件产出，非 C++ 硬编码）
    json head_meta = json::object();   // head meta 数据（地图模式：{desc, og:[{property,content}], twitter:[{name,content}]}）
    json head_links = json::array();   // head link 数据（地图模式 MetaLink each：[{rel,href,attrs}]）
    std::string jsonld;                // JSON-LD 数据（地图模式 JsonLd 组件 <script> 内嵌）
    std::string curLocale, i18nJson, relBase;
    json lang_data = json::object();   // 语言切换数据（地图模式）：{show, current, items:[{label,href}]}
    bool is_home = false;
};

// 链接 → json（relBase 前缀补偿子目录深度）
json link_json(const Link& l, const std::string& relBase = "");
// 导航树 → 2 层展平（递归收集，对标 Hugo 侧边栏嵌套）
json nav_tree_json(const std::vector<NavNode>& nodes, const std::string& curFile,
                   const std::string& relBase = "");
// 导航组 → 分组 json（地图模式 NavGroup/NavItem）
json nav_groups_json(const std::vector<NavNode>& nodes, const std::string& curFile,
                     const std::string& relBase = "");
// 首页卡片白名单（cfg.homeCards 不配则自动全列）
json cards_json(const SiteConfig& cfg, const std::vector<Page>& pages);
// 上下篇分页数据（{show, prev:{...}, next:{...}}）
json pager_json(const std::vector<Page>& pages, size_t i, const std::string& relBase);
// 编辑此页链接（指向仓库源文件编辑地址）
json edit_json(const SiteConfig& cfg, const std::string& file);
// 顶栏数据（logo/导航/语言/版本/主题/GitHub——全部由组件渲染）
json header_json(const SiteConfig& cfg, const RenderOpts& opt, const std::string& curLocale,
                 const json& langSwitch, const std::string& relBase, bool isHome);
// 页脚数据（show/text/links）
json footer_json(const SiteConfig& cfg);

#endif  // CDOCS_CTXDATA_HPP
