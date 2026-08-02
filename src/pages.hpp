// pages.hpp —— 导航树遍历/收集、草稿过滤、面包屑路径、右侧 TOC 生成

#ifndef CDOCS_PAGES_HPP
#define CDOCS_PAGES_HPP

#include "core.hpp"

// 从导航树移除 draft 叶子（file 落在 draft 集合内），并剔除因此变空的整组
void filter_draft_nav(std::vector<NavNode>& nodes, const std::set<std::string>& draft);

// 遍历导航树，收集叶子页面（file 节点），保持前序顺序
void collect_pages(const std::vector<NavNode>& nodes, std::vector<Page>& out);

// 子树是否包含某个页面（用于侧边栏分组默认展开判定）
bool subtree_contains(const std::vector<NavNode>& nodes, const std::string& target);

// 求从根到 target 的分组标题链（面包屑用），path 不含当前页自身
bool find_path(const std::vector<NavNode>& nodes, const std::string& target,
               std::vector<std::string>& path);

// 右侧边栏 TOC：扫描 h2~h4，注入稳定 slug id + 锚点链接，并生成目录
struct TocResult {
    std::string toc;                 // 目录 HTML（fallback 用）
    std::string html;                // 注入锚点后的正文
    json items = json::array();      // 目录数据 [{level, text, id}]（TocSidebar 组件用）
};
TocResult build_toc(const std::string& html);

#endif  // CDOCS_PAGES_HPP
