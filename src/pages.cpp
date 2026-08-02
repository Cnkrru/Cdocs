// pages.cpp —— 导航树/草稿/面包屑/TOC 实现（自 main.cpp 原样搬迁）

#include "pages.hpp"

// 从导航树移除 draft 叶子（file 落在 draft 集合内），并剔除因此变空的整组
void filter_draft_nav(std::vector<NavNode>& nodes, const std::set<std::string>& draft) {
    std::vector<NavNode> out;
    for (auto& n : nodes) {
        if (n.is_group()) {
            filter_draft_nav(n.children, draft);
            if (!n.children.empty()) out.push_back(std::move(n));
        } else if (!n.file.empty()) {
            if (draft.count(n.file)) continue;
            out.push_back(std::move(n));
        } else if (!n.url.empty()) {
            out.push_back(std::move(n));
        }
    }
    nodes = std::move(out);
}

// 遍历导航树，收集叶子页面（file 节点），保持前序顺序
void collect_pages(const std::vector<NavNode>& nodes, std::vector<Page>& out) {
    for (const auto& n : nodes) {
        if (n.is_group())
            collect_pages(n.children, out);
        else if (!n.file.empty()) {
            Page p;
            p.file = n.file;
            p.title = n.title;
            out.push_back(std::move(p));
        }
    }
}

// 子树是否包含某个页面（用于侧边栏分组默认展开判定）
bool subtree_contains(const std::vector<NavNode>& nodes, const std::string& target) {
    for (const auto& n : nodes) {
        if (!n.file.empty() && n.file == target) return true;
        if (n.is_group() && subtree_contains(n.children, target)) return true;
    }
    return false;
}

// 求从根到 target 的分组标题链（面包屑用），path 不含当前页自身
bool find_path(const std::vector<NavNode>& nodes, const std::string& target,
                     std::vector<std::string>& path) {
    for (const auto& n : nodes) {
        if (n.is_group()) {
            path.push_back(n.title);
            if (find_path(n.children, target, path)) return true;
            path.pop_back();
        } else if (n.file == target) {
            return true;
        }
    }
    return false;
}

// 右侧边栏 TOC：扫描 h2~h4，注入稳定 slug id + 锚点链接，并生成目录
TocResult build_toc(const std::string& html) {
    TocResult res;
    res.html = html;
    std::string items;
    std::map<std::string, int> used;     // 处理同标题重名（追加 -2 / -3 …）
    size_t pos = 0;
    while (pos < res.html.size()) {
        size_t lt = res.html.find("<h", pos);
        if (lt == std::string::npos) break;
        if (lt + 3 >= res.html.size()) break;
        char c = res.html[lt + 2];
        int level = (c >= '1' && c <= '4') ? (c - '0') : 0;
        if (level < 2) { pos = lt + 1; continue; }   // 跳过 h1（页面标题），只收 h2~h4
        size_t gt = res.html.find('>', lt + 3);
        if (gt == std::string::npos) break;
        size_t close = res.html.find("</h", gt);
        if (close == std::string::npos) break;
        size_t closeEnd = res.html.find('>', close);
        if (closeEnd == std::string::npos) break;
        std::string inner = res.html.substr(gt + 1, close - (gt + 1));
        std::string text = strip_tags(inner);
        // 稳定 slug id（与 TOC 共用，刷新/分享锚点不会变）
        std::string base = slugify(text);
        if (base.empty()) base = "section";
        std::string id = base;
        if (used.count(id)) { used[id]++; id = base + "-" + std::to_string(used[id]); }
        else used[id] = 1;
        std::string newOpen = "<h" + std::to_string(level) + " id=\"" + id + "\">"
            + "<a class=\"anchor\" href=\"#" + id + "\" aria-label=\"锚点链接\">#</a>";
        res.html.replace(lt, gt - lt + 1, newOpen);   // 仅替换开标签，保留 inner + 闭合标签
        items += "        <li class=\"toc-h" + std::to_string(level) + "\"><a href=\"#" + id
                 + "\">" + esc(text) + "</a></li>\n";
        res.items.push_back(json{{"level", level}, {"text", text}, {"id", id}});   // TOC 数据（组件模式）
        pos = lt + newOpen.size();
    }
    if (!items.empty()) {
        res.toc = "<nav class=\"toc-nav\">\n      <div class=\"toc-title\">{{tocTitle}}</div>\n"
                  "      <ul class=\"toc-list\">\n" + items + "      </ul>\n    </nav>\n";
    }
    return res;
}
