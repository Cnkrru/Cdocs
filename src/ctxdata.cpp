// ctxdata.cpp —— 页面数据装配：C++ 数据 → 组件可消费的 json
// （自 builder.cpp 拆分：PageCtx + link/nav/cards/pager/header/footer 装配）

#include "ctxdata.hpp"
#include "pages.hpp"   // subtree_contains（导航展开判定）

namespace {

// fallback 辅助（老主题无 components/ 时，把 PageCtx 数据还原为 HTML；组件模式不使用）
// —— 原 builder.cpp 中此段之后的辅助函数，当前组件模式已不依赖，保留注释说明。

}  // namespace
json link_json(const Link& l, const std::string& relBase) {
    json d;
    d["title"] = l.title;
    d["href"] = l.external() ? l.url : relBase + l.file + ".html";
    return d;
}

// 导航树 → json（递归；depth 供 --depth CSS 变量；open/collapsed 供折叠组）
json nav_tree_json(const std::vector<NavNode>& nodes, const std::string& curFile,
                          const std::string& relBase, int depth) {
    json arr = json::array();
    for (const auto& n : nodes) {
        json d;
        d["title"] = n.title;
        d["depth"] = depth;
        d["is_group"] = n.is_group();
        if (n.is_group()) {
            bool open = curFile.empty() || subtree_contains(n.children, curFile);
            d["open"] = open;
            d["collapsed"] = !open;
            d["children"] = nav_tree_json(n.children, curFile, relBase, depth + 1);
        } else if (!n.file.empty()) {
            d["file"] = n.file;
            d["active"] = (n.file == curFile);
        } else {
            bool external = n.url.rfind("http://", 0) == 0 || n.url.rfind("https://", 0) == 0
                         || n.url[0] == '#' || n.url.rfind("mailto:", 0) == 0;
            d["url"] = external ? n.url : relBase + n.url;
            d["external"] = external;
        }
        arr.push_back(d);
    }
    return arr;
}

// 左导航 → json（地图模式：人为约束 2 层「分组 → 条目」，替代 NavItem 无限递归）。
// active_class 是属性级数据孔（" class=\"active\" aria-current=\"page\"" 或空），结构由组件定、状态由数据定。
json nav_groups_json(const std::vector<NavNode>& nodes, const std::string& curFile,
                            const std::string& relBase) {
    auto item_json = [&](const NavNode& n) {
        json it;
        it["title"] = n.title;
        if (!n.file.empty()) {
            it["url"] = relBase + n.file + ".html";
            it["active_class"] = (n.file == curFile) ? " class=\"active\" aria-current=\"page\"" : "";
        } else {
            bool external = n.url.rfind("http://", 0) == 0 || n.url.rfind("https://", 0) == 0
                         || (!n.url.empty() && (n.url[0] == '#' || n.url.rfind("mailto:", 0) == 0));
            it["url"] = external ? n.url : relBase + n.url;
            it["active_class"] = "";
        }
        return it;
    };
    json arr = json::array();
    for (const auto& n : nodes) {
        json g;
        g["title"] = n.title;
        g["items"] = json::array();
        if (n.is_group()) {
            for (const auto& c : n.children) g["items"].push_back(item_json(c));
        } else {
            g["items"].push_back(item_json(n));   // 无分组的顶层条目 → 单条目组
        }
        arr.push_back(g);
    }
    return arr;
}

// 首页卡片 → json（landing 用）
json cards_json(const SiteConfig& cfg, const std::vector<Page>& pages) {    std::vector<const Page*> shown;
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
    json arr = json::array();
    for (const auto* p : shown) {
        json d;
        d["title"] = p->title;
        d["href"] = p->file + ".html";
        d["desc"] = "";
        for (const auto& hc : cfg.homeCards)
            if (hc.file == p->file) { if (!hc.title.empty()) d["title"] = hc.title; d["desc"] = hc.desc; break; }
        arr.push_back(d);
    }
    return arr;
}

// 上下篇翻页 → json（Docusaurus 同款：无上下篇保留禁用占位）
json pager_json(const std::vector<Page>& pages, size_t i, const std::string& relBase) {
    json d;
    d["show"] = (i > 0) || (i + 1 < pages.size());
    if (i > 0) {
        d["prev"] = json{{"show", true}, {"hidden", false}, {"title", pages[i - 1].title},
                         {"href", relBase + pages[i - 1].file + ".html"}};
    } else d["prev"] = json{{"show", false}, {"hidden", true}};
    if (i + 1 < pages.size()) {
        d["next"] = json{{"show", true}, {"hidden", false}, {"title", pages[i + 1].title},
                         {"href", relBase + pages[i + 1].file + ".html"}};
    } else d["next"] = json{{"show", false}, {"hidden", true}};
    return d;
}

// 编辑此页 → json（{show, href, label}；空 = 不渲染）
json edit_json(const SiteConfig& cfg, const std::string& file) {
    if (cfg.editBase.empty()) return json{{"show", false}};
    std::string url = cfg.editBase;
    if (!url.empty() && url.back() != '/') url += '/';
    std::string dir = cfg.editDocsDir;
    if (!dir.empty() && dir.back() != '/') dir += '/';
    return json{{"show", true}, {"href", url + dir + file + ".md"}, {"label", "{{editThisPage}}"}};
}

// 页眉数据 → json（Header 组件）
json header_json(const SiteConfig& cfg, const RenderOpts& opt, const std::string& curLocale,
                        const json& langSwitch, const std::string& relBase, bool isHome) {
    json d;
    d["show_menu_toggle"] = !isHome;
    d["menu_label"] = "{{menuToggleLabel}}";
    d["logo_text"] = cfg.header.logo.empty() ? cfg.title : cfg.header.logo;
    d["logo_href"] = relBase + "index.html";
    d["topnav"] = json::array();
    for (const auto& l : cfg.header.links) d["topnav"].push_back(link_json(l, relBase));
    d["show_search"] = opt.showSearch;
    d["search_placeholder"] = "{{searchPlaceholder}}";
    d["header_nav"] = json::array();
    size_t n = std::min<size_t>(cfg.header.nav.size(), 6);
    for (size_t i = 0; i < n; ++i) d["header_nav"].push_back(link_json(cfg.header.nav[i], relBase));
    d["lang_switch"] = langSwitch;
    d["versions"] = json::object();
    if (cfg.versions.size() > 1) {
        d["versions"]["current"] = json{{"label", cfg.curVersionLabel.empty() ? cfg.curVersion : cfg.curVersionLabel}};
        d["versions"]["links"] = json::array();
        for (const auto& v : cfg.versions) {
            if (v.name == cfg.curVersion) continue;   // current 已放入 current
            json vd;
            vd["label"] = v.label;
            // 版本链接相对当前页：relBase 补偿子目录深度（首页 "" → ../../；
            // docs/ 下 "../" → ../../../），从 <版本>/<语言>/ 回退两级到 dist/ 再进目标版本
            std::string verBase = relBase + (curLocale.empty() ? "../" : "../../");
            vd["href"] = verBase + v.name
                         + (curLocale.empty() ? "/index.html" : "/" + curLocale + "/index.html");
            d["versions"]["links"].push_back(vd);
        }
    }
    d["cur_version"] = cfg.curVersionLabel.empty() ? cfg.curVersion : cfg.curVersionLabel;
    d["show_theme_toggle"] = opt.showThemeToggle;
    d["theme_label"] = "{{themeToggleLabel}}";
    d["github"] = cfg.header.github;
    return d;
}

// 页脚数据 → json（Footer 组件）
json footer_json(const SiteConfig& cfg) {
    json d;
    d["show"] = !(cfg.footer.text.empty() && cfg.footer.links.empty());
    d["text"] = cfg.footer.text;
    d["links"] = json::array();
    for (const auto& l : cfg.footer.links) d["links"].push_back(link_json(l));
    return d;
}
