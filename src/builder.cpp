// builder.cpp —— 构建编排、渲染部件与站点生命周期命令（自 main.cpp 原样搬迁）

#include "builder.hpp"
#include "frontmatter.hpp"
#include "i18n.hpp"
#include "pages.hpp"
#include "feeds.hpp"
#include "pwa.hpp"
#include "config.hpp"
#include "search.hpp"
#include "plugin.hpp"
#include "compress.hpp"
#include "linkcheck.hpp"
#include <algorithm>   // std::find（i18n 缺失键去重）
#include <thread>      // Hugo 式并发渲染：worker pool
#include <mutex>       // pageSig 并发写保护
#include <atomic>      // 任务计数器
#include <cstdio>
#include <regex>       // L2 残留检测：{{}} 模板块 / 数据键 / 组件标签正则扫描

// ---------------- 并发渲染 worker pool（Hugo 式：多核并行渲染页面） ----------------
// tasks 数量 < 2 或单核时退化为顺序执行；每个工作线程从原子计数器取任务。
static void run_parallel(size_t n_tasks, const std::function<void(size_t)>& fn) {
    if (n_tasks < 2) { for (size_t i = 0; i < n_tasks; ++i) fn(i); return; }
    unsigned hw = std::thread::hardware_concurrency();
    if (hw <= 1)   { for (size_t i = 0; i < n_tasks; ++i) fn(i); return; }
    unsigned n = std::min<unsigned>(hw, (unsigned)n_tasks);
    std::atomic<size_t> next{0};
    std::vector<std::thread> pool;
    pool.reserve(n);
    for (unsigned t = 0; t < n; ++t)
        pool.emplace_back([&] {
            for (;;) {
                size_t i = next.fetch_add(1, std::memory_order_relaxed);
                if (i >= n_tasks) break;
                fn(i);
            }
        });
    for (auto& th : pool) th.join();
}

// ---------------- 渲染部件（file-local） ----------------

// 社交分享 + 文章结构化元信息（Open Graph / Twitter Card / article:*)
// ogUrl 为空时跳过需绝对地址的 og:url / og:image（通常由 config.url 驱动）
static fs::path theme_root() {
    std::error_code ec;
    fs::path t = g_engine / "theme";
    if (fs::is_directory(t, ec)) return t;
    return g_engine;
}
static std::set<std::string> g_comp_warned;   // 缺失/循环警告去重（每组件名一次）
static std::mutex g_comp_mtx;                  // g_comp_warned 并发保护（正文渲染多线程）
static bool comp_warned_once(const std::string& k) {
    std::lock_guard<std::mutex> lk(g_comp_mtx);
    return g_comp_warned.insert(k).second;
}

static fs::path components_dir() { return theme_root() / "components"; }

static std::string load_component(const std::string& name) {
    fs::path dir = components_dir();
    std::error_code ec;
    // 根级优先：components/<Name>.html
    fs::path p = dir / (name + ".html");
    if (fs::is_regular_file(p, ec)) return read_file(p);
    // 区域子目录（header/ footer/ center/ …）：递归查找 <Name>.html
    if (fs::is_directory(dir, ec)) {
        for (auto it = fs::recursive_directory_iterator(dir, ec), end = fs::recursive_directory_iterator();
             it != end; it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            if (it->is_regular_file(ec) && it->path().filename().string() == name + ".html")
                return read_file(it->path());
        }
    }
    return {};
}

static bool tpl_truthy(const json& v) {
    if (v.is_null()) return false;
    if (v.is_boolean()) return v.get<bool>();
    if (v.is_string()) return !v.get<std::string>().empty();
    if (v.is_array() || v.is_object()) return !v.empty();
    if (v.is_number()) return v.get<double>() != 0.0;
    return false;
}

static std::string json_scalar(const json& v) {
    if (v.is_string()) return v.get<std::string>();
    if (v.is_boolean()) return v.get<bool>() ? "1" : "";
    if (v.is_number()) {
        if (v.is_number_integer()) return std::to_string(v.get<long long>());
        return v.dump();
    }
    return "";
}

// 嵌套路径取值：{{a.b.c}} → data["a"]["b"]["c"]；缺失返回 nullptr
static const json* json_get_path(const json& data, const std::string& path) {
    if (path.empty() || !data.is_object()) return nullptr;
    const json* cur = &data;
    size_t start = 0;
    while (cur) {
        size_t dot = path.find('.', start);
        std::string seg = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
        if (seg.empty() || !cur->is_object() || !cur->contains(seg)) return nullptr;
        cur = &(*cur)[seg];
        if (dot == std::string::npos) return cur;
        start = dot + 1;
    }
    return nullptr;
}

// ============ 地图驱动引擎（Map-Driven，v3：纯 JSON 约定，无任何自定义语法） ============
// 地图 = theme/map/<type>.json（JSON sections 数组，每项一种约定）：
//   { "html": "静态 HTML 片段" }                → 原样输出（可含数据孔 {{lang}} 等）
//   { "component": "Name" }                    → 渲染组件 components/**/<Name>.html
//   { "component": "Name", "if": "path" }      → 数据路径真值才渲染
//   { "component": "Name", "each": "path" }    → 数组循环渲染（每项合并进数据作用域）
//   { "component": "Name", "sections": [...] } → 子序列渲染结果填进组件 {{slot}}
//   { "component": "Name", "props": {...} }    → props 传参：合并进该组件数据作用域（组件内 {{k}} 可取，
//                                                 优先级最高）；字符串值中的 {{path}} 引用全局数据
// 组件 = 纯 HTML 片段 + 数据孔 {{field}} / {{slot}}——没有 {{ if }}/{{ each }} 控制流，
// 没有 <组件/> 标签，没有属性语法。条件/循环/嵌套全部由 JSON 地图字段表达。
// 数据作用域优先级：全局页面数据 < each 当前项 < props（最局部优先）。
// 地图 JSON 顶层可带 "data": {...} 对象 → 合并进全局页面数据（第三方自定义数据源入口）。

static std::string compose_sections(const json& sections, const json& data,
                                    int depth, std::vector<std::string>& stack);

// 数据孔替换（纯文本）：{{a.b.c}} → data 路径取值；缺失原样保留（L2 兜底）
static std::string fill_data_holes(const std::string& html, const json& data) {
    std::string out;
    out.reserve(html.size() + 128);
    size_t i = 0;
    while (i < html.size()) {
        if (html[i] == '{' && i + 1 < html.size() && html[i + 1] == '{') {
            size_t end = html.find("}}", i + 2);
            if (end != std::string::npos) {
                std::string tok = trim(html.substr(i + 2, end - i - 2));
                if (!tok.empty()) {
                    const json* pv = json_get_path(data, tok);
                    if (pv) { out += json_scalar(*pv); i = end + 2; continue; }
                }
            }
        }
        out += html[i];
        ++i;
    }
    return out;
}

// 渲染单个组件实例：读 <Name>.html（纯 HTML 片段）→ 子 sections → {{slot}} → 数据孔替换
static std::string render_map_component(const std::string& name, const json& data,
                                        int depth, std::vector<std::string>& stack,
                                        const json* childSections) {
    if (depth > 32) {
        if (comp_warned_once("depth:" + name))
            std::cerr << color::warn("警告: ") << "组件嵌套过深（>32 层）: " << name << "\n";
        return {};
    }
    if (std::find(stack.begin(), stack.end(), name) != stack.end()) {
        if (comp_warned_once("cycle:" + name))
            std::cerr << color::warn("警告: ") << "组件循环引用: " << name << "（该挂载点已移除）\n";
        return {};
    }
    std::string body = load_component(name);
    if (body.empty()) {
        if (comp_warned_once("missing:" + name))
            std::cerr << color::warn("警告: ") << "组件文件不存在: components/" << name
                      << ".html（该组件已跳过）\n";
        return {};
    }
    stack.push_back(name);
    // 子 sections → slot 内容（each 当前项上下文已由调用方合并进 data）
    json ctx = data;
    if (childSections) ctx["slot"] = compose_sections(*childSections, data, depth + 1, stack);
    else               ctx["slot"] = std::string();
    stack.pop_back();
    // 组件内是纯 HTML + 数据孔（{{slot}} / {{field}}），只做数据孔替换
    return fill_data_holes(body, ctx);
}

// 遍历 JSON sections 数组（地图核心：component/if/each/sections/html 五种约定）
static std::string compose_sections(const json& sections, const json& data,
                                    int depth, std::vector<std::string>& stack) {
    std::string out;
    if (!sections.is_array()) return out;
    for (const auto& sec : sections) {
        if (!sec.is_object()) continue;
        // 静态 HTML 片段（骨架：<!DOCTYPE>、<div class="layout"> 等；可含数据孔）
        if (sec.contains("html") && sec["html"].is_string()) {
            out += sec["html"].get<std::string>();
            continue;
        }
        if (!sec.contains("component") || !sec["component"].is_string()) continue;
        std::string name = sec["component"].get<std::string>();
        // if 判定：数据路径真值才渲染
        if (sec.contains("if") && sec["if"].is_string()) {
            const json* cv = json_get_path(data, sec["if"].get<std::string>());
            if (!cv || !tpl_truthy(*cv)) continue;
        }
        const json* child = (sec.contains("sections") && sec["sections"].is_array()) ? &sec["sections"] : nullptr;
        // props 传参：合并进该组件数据作用域（优先级最高）；字符串值中的 {{path}} 引用全局数据
        json propsObj;
        if (sec.contains("props") && sec["props"].is_object()) {
            for (auto it = sec["props"].begin(); it != sec["props"].end(); ++it) {
                if (it.value().is_string())
                    propsObj[it.key()] = fill_data_holes(it.value().get<std::string>(), data);
                else propsObj[it.key()] = it.value();
            }
        }
        auto applyProps = [&](json& ctx) {
            for (auto it = propsObj.begin(); it != propsObj.end(); ++it)
                ctx[it.key()] = it.value();
        };
        // each 循环：数组每项渲染一次（当前项字段合并进数据作用域）
        if (sec.contains("each") && sec["each"].is_string()) {
            const json* av = json_get_path(data, sec["each"].get<std::string>());
            if (av && av->is_array()) {
                for (const auto& item : *av) {
                    json ctx = data;
                    if (item.is_object())
                        for (auto it = item.begin(); it != item.end(); ++it)
                            ctx[it.key()] = it.value();
                    applyProps(ctx);
                    out += render_map_component(name, ctx, depth, stack, child);
                }
            }
            continue;
        }
        json ctx = data;
        applyProps(ctx);
        out += render_map_component(name, ctx, depth, stack, child);
    }
    return out;
}

// 递归解析地图继承（v4）：子地图 "extends" 声明父级（kv 约定），父级 {"slot": X} 槽位被子 sections 展开替换。
// 父级路径：config/map.json 的 templates 注册表优先，否则 theme/map/<name>.json。支持多级继承；循环检测报错。
static json resolve_map_sections(const std::string& mapName, const json& mapRoot,
                                 const json& templates, std::vector<std::string>& chain) {
    json sections = mapRoot.value("sections", mapRoot);
    if (!mapRoot.is_object() || !mapRoot.contains("extends") || !mapRoot["extends"].is_string())
        return sections;   // 无父级：自身 sections 即最终
    std::string parentName = mapRoot["extends"].get<std::string>();
    if (std::find(chain.begin(), chain.end(), parentName) != chain.end()) {
        if (comp_warned_once("extends:" + parentName)) {
            std::cerr << color::warn("警告: ") << "地图继承循环: ";
            for (const auto& c : chain) std::cerr << c << " → ";
            std::cerr << parentName << "（子地图 sections 独立使用）\n";
        }
        return sections;
    }
    fs::path pp = theme_root() / "map" / (parentName + ".json");
    if (templates.is_object() && templates.contains(parentName) && templates[parentName].is_string())
        pp = theme_root() / templates[parentName].get<std::string>();
    std::error_code ec;
    if (!fs::is_regular_file(pp, ec)) {
        if (comp_warned_once("extends:" + parentName))
            std::cerr << color::warn("警告: ") << "父级地图不存在: " << pp
                      << "（子地图 sections 独立使用）\n";
        return sections;
    }
    json parent;
    try { parent = json::parse(read_file(pp)); } catch (...) {
        if (comp_warned_once("extends:" + parentName))
            std::cerr << color::warn("警告: ") << "父级地图 JSON 解析失败: " << pp << "\n";
        return sections;
    }
    chain.push_back(parentName);
    json parentSections = resolve_map_sections(parentName, parent, templates, chain);
    chain.pop_back();
    if (!parentSections.is_array()) return sections;
    // 父级 sections 中纯 {"slot": X} 占位（无 component/html）→ 用子 sections 展开替换
    json merged = json::array();
    bool filled = false;
    for (const auto& sec : parentSections) {
        if (sec.is_object() && sec.contains("slot") && !sec.contains("component") && !sec.contains("html")) {
            for (const auto& sub : sections) merged.push_back(sub);
            filled = true;
        } else merged.push_back(sec);
    }
    if (!filled && comp_warned_once("slot:" + mapName))
        std::cerr << color::warn("警告: ") << "父级地图 " << parentName
                  << " 没有 {\"slot\": ...} 槽位，子地图内容未注入\n";
    return merged;
}

// 地图主入口：读 config/map.json 注册表 → theme/map/<name>.json（递归解析 extends 继承）→ 遍历 sections → 数据孔替换
static std::string compose_page(const std::string& mapName, const json& data) {
    // config/map.json：maps（页面类型注册数组）+ templates（父级地图注册）。地图不硬编码进 C++。
    // maps 数组项：{type, map, mode, output?, home?}；兼容旧格式（maps 为对象：类型名 → 地图路径）。
    std::string mapPath;
    json templates = json::object();
    fs::path cfgPath = g_engine / "config" / "map.json";
    std::error_code cec;
    if (fs::is_regular_file(cfgPath, cec)) {
        try {
            json j = json::parse(read_file(cfgPath));
            if (j.contains("maps")) {
                if (j["maps"].is_object() && j["maps"].contains(mapName))
                    mapPath = j["maps"][mapName].get<std::string>();
                else if (j["maps"].is_array())
                    for (const auto& e : j["maps"])
                        if (e.is_object() && e.value("type", "") == mapName
                            && e.contains("map") && e["map"].is_string()) {
                            mapPath = e["map"].get<std::string>();
                            break;
                        }
            }
            if (j.contains("templates") && j["templates"].is_object())
                templates = j["templates"];
        } catch (...) {}
    }
    fs::path mp = mapPath.empty() ? (theme_root() / "map" / (mapName + ".json"))
                                  : (theme_root() / mapPath);
    std::error_code ec;
    if (!fs::is_regular_file(mp, ec)) {
        if (comp_warned_once("map:" + mapName))
            std::cerr << color::warn("警告: ") << "页面地图不存在: " << mp
                      << "（使用内置最小骨架兜底）\n";
        std::string out = "<!DOCTYPE html>\n<html lang=\"" + esc_attr(data.value("lang", "zh-CN"))
            + "\">\n<head>\n<meta charset=\"utf-8\">\n<title>{{title}}</title>\n</head>\n<body>\n{{body}}\n</body>\n</html>\n";
        return fill_data_holes(out, data);
    }
    json map;
    try { map = json::parse(read_file(mp)); } catch (...) {
        if (comp_warned_once("map:" + mapName))
            std::cerr << color::warn("警告: ") << "页面地图 JSON 解析失败: " << mp << "\n";
        return {};
    }
    // 地图级数据（v5）：地图 JSON 顶层 "data": {...} 合并进页面数据作用域（第三方自定义数据源入口）
    json mapData = data;
    if (map.is_object() && map.contains("data") && map["data"].is_object()) {
        for (auto it = map["data"].begin(); it != map["data"].end(); ++it)
            mapData[it.key()] = it.value();
    }
    std::vector<std::string> chain;
    json sections = resolve_map_sections(mapName, map, templates, chain);
    std::vector<std::string> stack;
    std::string out = compose_sections(sections, mapData, 0, stack);
    // 地图 html 片段里的顶层数据孔（{{lang}}/{{title}}/{{body}}/{{extra_head}} 等）
    out = fill_data_holes(out, mapData);
    return out;
}

static std::string replace_all(std::string s, const std::string& from, const std::string& to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

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

// ---- fallback 辅助（老主题无 components/ 时，把 PageCtx 数据还原为 HTML；组件模式不使用） ----
static json link_json(const Link& l, const std::string& relBase = "") {
    json d;
    d["title"] = l.title;
    d["href"] = l.external() ? l.url : relBase + l.file + ".html";
    return d;
}

// 导航树 → json（递归；depth 供 --depth CSS 变量；open/collapsed 供折叠组）
static json nav_tree_json(const std::vector<NavNode>& nodes, const std::string& curFile,
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
static json nav_groups_json(const std::vector<NavNode>& nodes, const std::string& curFile,
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
static json cards_json(const SiteConfig& cfg, const std::vector<Page>& pages) {    std::vector<const Page*> shown;
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
static json pager_json(const std::vector<Page>& pages, size_t i, const std::string& relBase) {
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
static json edit_json(const SiteConfig& cfg, const std::string& file) {
    if (cfg.editBase.empty()) return json{{"show", false}};
    std::string url = cfg.editBase;
    if (!url.empty() && url.back() != '/') url += '/';
    std::string dir = cfg.editDocsDir;
    if (!dir.empty() && dir.back() != '/') dir += '/';
    return json{{"show", true}, {"href", url + dir + file + ".md"}, {"label", "{{editThisPage}}"}};
}

// 页眉数据 → json（Header 组件）
static json header_json(const SiteConfig& cfg, const RenderOpts& opt, const std::string& curLocale,
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
            vd["href"] = curLocale.empty()
                         ? "../" + v.name + "/index.html"
                         : "../../" + v.name + "/" + curLocale + "/index.html";
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
static json footer_json(const SiteConfig& cfg) {
    json d;
    d["show"] = !(cfg.footer.text.empty() && cfg.footer.links.empty());
    d["text"] = cfg.footer.text;
    d["links"] = json::array();
    for (const auto& l : cfg.footer.links) d["links"].push_back(link_json(l));
    return d;
}

// ---------------- 构建编排 ----------------

// ============ 构建上下文（run_build 各阶段共享的状态） ============
// 让 run_build 退化为纯编排：每个阶段函数只需一份上下文，不再传递超长参数列表。
// 阶段函数体用局部引用别名（如 SiteConfig& cfg = b.cfg;）绑定上下文成员，
// 因此函数体与重构前 run_build 内联时逐字一致，行为零变化。
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
    // ---- 增量构建状态（--watch 热重载加速；普通 build 全量） ----
    bool incremental = false;              // 本次构建是否启用增量（serve -w 置位）
    std::map<std::string, std::string> pageSig;  // file+loc -> "mtime:size"（源 .md 指纹）
    bool globalDirty = true;               // 配置/导航/i18n/模板任一变化 → 全量重建
};

// ---------------- 构建阶段（run_build 的拆分，file-local） ----------------

// 1) 载入站点配置：config.json + 侧边栏（sidebar/ 分文件 + site.sidebar 映射，未配置回退 route.json）
static void load_site_config(BuildContext& b) {
    SiteConfig& cfg = b.cfg;
    I18nCfg& i18n = b.i18n;
    I18nDict& fallbackUI = b.fallbackUI;
    RenderOpts& opt = b.opt;
    const fs::path& out_dir = b.out_dir;

    fs::path cfg_path = g_engine / "config/config.json";
    fs::path route_path = g_engine / "config/route.json";

    // 1) 站点配置 config.json
    //    三区块结构（head / center / footer）+ 全局 site 段：
    //      { "site": {全局}, "head": {页眉}, "center": {内容/功能}, "footer": {页脚} }
    //    兼容旧式顶层结构：无 "site" 键时整份当作旧结构解析（header/footer 顶层字段照旧可用）。
    if (fs::exists(cfg_path)) {
        try {
        json j = json::parse(read_file(cfg_path));
        json site = j.contains("site") && j["site"].is_object() ? j["site"] : j;   // 全局段（兼容旧顶层）
        json hdr  = j.contains("head")   && j["head"].is_object()   ? j["head"]
                  : j.contains("header") && j["header"].is_object() ? j["header"] : json::object();
        json ctr  = j.contains("center") && j["center"].is_object() ? j["center"] : json::object();
        json ftr  = j.contains("footer") && j["footer"].is_object() ? j["footer"] : json::object();

        if (site.contains("title"))       cfg.title = site["title"].get<std::string>();
        if (site.contains("description")) cfg.description = site["description"].get<std::string>();
        if (site.contains("theme"))       cfg.theme = site["theme"].get<std::string>();
        if (site.contains("url"))         cfg.url = site["url"].get<std::string>();
        if (site.contains("ogImage"))     cfg.ogImage = site["ogImage"].get<std::string>();
        if (site.contains("compress") && site["compress"].is_boolean())
            cfg.compress = site["compress"].get<bool>();          // 默认开启
        else if (site.contains("compress"))
            std::cerr << color::yellow("  [config] site.compress 应为布尔值（true/false）\n");
        if (site.contains("jpegQuality") && site["jpegQuality"].is_number()) {
            cfg.jpegQuality = site["jpegQuality"].get<int>();     // 1-100
            if (cfg.jpegQuality < 1)  cfg.jpegQuality = 1;
            if (cfg.jpegQuality > 100) cfg.jpegQuality = 100;
        } else if (site.contains("jpegQuality"))
            std::cerr << color::yellow("  [config] site.jpegQuality 应为数字（1-100）\n");
        if (hdr.contains("logo"))            cfg.header.logo = hdr["logo"].get<std::string>();
        if (hdr.contains("showSearch"))      cfg.header.showSearch = hdr["showSearch"].get<bool>();
        if (hdr.contains("showThemeToggle")) cfg.header.showThemeToggle = hdr["showThemeToggle"].get<bool>();
        if (hdr.contains("github"))          cfg.header.github = hdr["github"].get<std::string>();
        if (hdr.contains("links")) for (auto& l : hdr["links"]) cfg.header.links.push_back(parse_link(l));
        if (hdr.contains("nav"))   for (auto& l : hdr["nav"])   cfg.header.nav.push_back(parse_link(l));
        if (ftr.contains("text"))  cfg.footer.text = ftr["text"].get<std::string>();
        if (ftr.contains("links")) for (auto& l : ftr["links"]) cfg.footer.links.push_back(parse_link(l));
        if (ctr.contains("plugins")) for (auto& p : ctr["plugins"]) cfg.plugins.push_back(p.get<std::string>());
        // 兼容旧结构：plugins 在顶层
        if (cfg.plugins.empty() && site.contains("plugins"))
            for (auto& p : site["plugins"]) cfg.plugins.push_back(p.get<std::string>());
        if (ctr.contains("backToTop")) {
            auto& b = ctr["backToTop"];
            if (b.contains("threshold")) cfg.backToTopThreshold = b["threshold"].get<int>();
            if (b.contains("label"))     cfg.backToTopLabel = b["label"].get<std::string>();
        }
        // 编辑此页链接（行业标准交互，指向仓库源文件编辑地址）
        if (site.contains("editLink")) {
            auto& e = site["editLink"];
            if (e.contains("base"))    cfg.editBase = e["base"].get<std::string>();
            if (e.contains("docsDir")) cfg.editDocsDir = e["docsDir"].get<std::string>();
        }
        // i18n（多语言）：行业标准 {{key}}+json 实现
        //   i18n.dir 下放置每份语言的扁平 JSON 字典（.Cdocs/i18n/zh-CN.json / en.json …）
        //   每个字典含全部 UI 文案键；正文里也可用 {{key}} 引用共享短语
        if (site.contains("i18n")) {
            i18n.enabled = true;
            auto& ic = site["i18n"];
            i18n.defaultLocale = ic.value("defaultLocale", std::string("zh-CN"));
            std::string ldir = ic.value("dir", std::string(".Cdocs/i18n"));
            if (ic.contains("locales"))
                for (auto it = ic["locales"].begin(); it != ic["locales"].end(); ++it) {
                    std::string loc = it.key();
                    std::string label = it.value().value("label", loc);
                    i18n.labels[loc] = label;
                    fs::path p = ldir + "/" + loc + ".json";
                    json d = json::object();
                    if (fs::exists(p)) {
                        try { d = json::parse(read_file(p)); } catch (...) { d = json::object(); }
                    }
                    I18nDict m;
                    for (auto kit = d.begin(); kit != d.end(); ++kit)
                        if (kit.value().is_string()) m[kit.key()] = kit.value().get<std::string>();
                    i18n.dicts[loc] = std::move(m);
                }
        }
        // 单语言模式兜底 UI 字典（与未开启 i18n 时原有中文文案一致）
        // 注意：这里是赋值外层别名（360 行 I18nDict& fallbackUI = b.fallbackUI;），
        // 不能重新声明——否则会遮蔽别名、b.fallbackUI 保持为空，单语言模式 UI 文案失效。
        fallbackUI = {
            {"siteTitle", "Cdocs 文档"},
            {"siteDesc", "一个用 C++ 编写、复用成熟组件（md4c + nlohmann/json + FlexSearch）的极简静态文档站点生成器。"},
            {"menuToggleLabel", "打开导航"}, {"searchPlaceholder", "搜索文档…"},
            {"themeToggleLabel", "切换主题"}, {"tocTitle", "本页目录"}, {"home", "首页"},
            {"getStarted", "开始阅读"}, {"prevLabel", "上一篇"}, {"nextLabel", "下一篇"},
            {"pagerNone", "暂无"},
            {"editThisPage", "编辑此页"}, {"lastUpdated", "最后更新于"},
            {"readingTime", "约 {{minutes}} 分钟阅读（{{words}} 字）"},
            {"backToTop", "顶部"}, {"notFoundTitle", "页面不见了"},
            {"notFoundDesc", "你访问的页面不存在或已被移动。"}, {"backHome", "返回首页"},
            {"brand", "Cdocs"}, {"navGitHub", "GitHub"}, {"navProject", "项目主页"},
            {"footerText", "© 2026 Cdocs · 基于 C++、md4c、nlohmann/json、FlexSearch 构建"},
            {"localeLabel", "语言"},
            {"copyCode", "复制代码"}, {"copy", "复制"}, {"copied", "已复制"},
            {"searchLoadFailed", "搜索数据加载失败"},
            {"useHttpServer", "：请用本地服务器(http)访问，不要直接以 file:// 打开本页"},
            {"noResults", "没有匹配「"}, {"noResultsSuffix", "」的结果"},
            {"navGettingStarted", "入门"}, {"navIntro", "介绍"}, {"navGuide", "使用指南"},
            {"navReference", "参考"}, {"navApi", "接口说明"}, {"navAdvanced", "进阶"},
            {"navArchitecture", "架构"}, {"navRendering", "渲染"}, {"navInternals", "底层实现"},
            {"navPipeline", "渲染管线"}, {"navRender", "渲染循环"}, {"allTags", "全部标签"},
            {"navBlog", "博客"}, {"blogTitle", "博客"}
        };
        // 公开 CSS 属性：用户可在 config.json 的 themeVars 覆盖任意变量
        if (site.contains("themeVars") && site["themeVars"].is_object()) {
            std::string body;
            for (auto it = site["themeVars"].begin(); it != site["themeVars"].end(); ++it)
                body += "  " + it.key() + ": " + it.value().get<std::string>() + ";\n";
            cfg.themeVarsBody = ":root, [data-theme=\"light\"], [data-theme=\"dark\"] {\n" + body + "}";
            cfg.themeVars = "<style id=\"user-theme-vars\">\n" + cfg.themeVarsBody + "\n</style>\n";
        }
        // 用户自定义 CSS 文件：存在则复制并链接（href 数据供 MetaHead 组件；customCssLink 供 fallback）
        if (site.contains("customCss")) {
            fs::path p = site["customCss"].get<std::string>();
            if (fs::exists(p)) {
                fs::create_directories(out_dir / "assets");
                fs::copy(p, out_dir / "assets" / p.filename(), fs::copy_options::overwrite_existing);
                cfg.customCssHref = "assets/" + p.filename().string();
                cfg.customCssLink = "<link rel=\"stylesheet\" href=\"" + cfg.customCssHref + "\">\n";
            }
        }
        // 首页（home 段）：hero 可选覆盖 + cards 白名单（不配则保持自动全列）
        if (site.contains("home")) {
            auto& hm = site["home"];
            if (hm.contains("hero")) {
                auto& h = hm["hero"];
                if (h.contains("title"))    cfg.homeTitle    = h["title"].get<std::string>();
                if (h.contains("subtitle")) cfg.homeSubtitle = h["subtitle"].get<std::string>();
                if (h.contains("cta") && h["cta"].is_object()) {
                    auto& c = h["cta"];
                    if (c.contains("text")) cfg.homeCtaText = c["text"].get<std::string>();
                    if (c.contains("file")) cfg.homeCtaFile = c["file"].get<std::string>();
                }
            }
            if (hm.contains("cards") && hm["cards"].is_array()) {
                for (auto& c : hm["cards"]) {
                    if (!c.is_object() || !c.contains("file")) continue;
                    HomeCardCfg card;
                    card.file = c["file"].get<std::string>();
                    if (c.contains("title")) card.title = c["title"].get<std::string>();
                    if (c.contains("desc"))  card.desc  = c["desc"].get<std::string>();
                    cfg.homeCards.push_back(std::move(card));
                }
            }
        }
        // 反馈上报端点（config.feedback.endpoint）：配了才启用真实统计，空 = 仅本地记忆
        if (site.contains("feedback") && site["feedback"].is_object()) {
            auto& fb = site["feedback"];
            if (fb.contains("endpoint") && fb["endpoint"].is_string())
                cfg.feedbackEndpoint = fb["endpoint"].get<std::string>();
        }
        // 版本化文档（config.versions，Docusaurus 风格）：
        //   { "versions": [ {"name":"v2","label":"2.0","source":"md-v2","default":true}, ... ] }
        //   source 为空 = 用主 md/ 目录；default 标记默认版本（根 index 重定向目标）
        if (site.contains("versions") && site["versions"].is_array()) {
            for (auto& v : site["versions"]) {
                if (!v.is_object() || !v.contains("name")) continue;
                VersionCfg vc;
                vc.name   = v["name"].get<std::string>();
                vc.label  = v.value("label", vc.name);
                vc.source = v.value("source", std::string());
                vc.default_v = v.value("default", false);
                cfg.versions.push_back(std::move(vc));
            }
            // 未标记 default 时取第一个版本
            if (!cfg.versions.empty() && !cfg.versions[0].default_v) {
                bool any = false;
                for (auto& v : cfg.versions) if (v.default_v) { any = true; break; }
                if (!any) cfg.versions[0].default_v = true;
            }
        }
        // 自动版本化（约定驱动）：config 未声明 versions，但 run_build 分派时
        // 通过 g_versions_json 把完整版本列表传给了子构建 → 填进 cfg.versions，
        // 供 render_header 渲染版本下拉。
        if (cfg.versions.empty() && !g_versions_json.empty()) {
            try {
                json va = json::parse(g_versions_json);
                if (va.is_array()) {
                    for (auto& v : va) {
                        VersionCfg vc;
                        vc.name = v.value("name", std::string());
                        vc.label = v.value("label", vc.name);
                        vc.source = v.value("source", std::string());
                        vc.default_v = v.value("default", false);
                        if (!vc.name.empty()) cfg.versions.push_back(std::move(vc));
                    }
                }
            } catch (...) { /* 忽略解析失败 */ }
        }
        // 多版本构建时注入当前版本信息（run_build 分派循环设置全局）
        cfg.curVersion = g_cur_version;
        cfg.curVersionLabel = g_cur_version_label;

        // ---- config schema 校验（对标成熟工具：未知字段/类型错误构建期提示，防拼写错误静默忽略）----
        auto warn_unknown = [&](const json& o, std::initializer_list<const char*> known, const char* sec) {
            if (!o.is_object()) return;
            for (auto it = o.begin(); it != o.end(); ++it) {
                bool okk = false;
                for (auto k : known) if (it.key() == k) { okk = true; break; }
                if (!okk && !g_quiet)
                    std::cerr << color::yellow("  [config] 未知字段 ") << sec << "." << it.key()
                              << color::muted("（拼写检查？）\n");
            }
        };
        warn_unknown(j,   {"site", "head", "header", "center", "footer"}, "$");
        warn_unknown(site, {"title", "description", "theme", "url", "ogImage", "editLink", "i18n",
                            "themeVars", "customCss", "home", "feedback", "versions", "compress",
                            "jpegQuality", "plugins", "sidebar"}, "site");
        warn_unknown(hdr, {"logo", "showSearch", "showThemeToggle", "github", "links", "nav"}, "head");
        warn_unknown(ctr, {"plugins", "backToTop", "comments"}, "center");
        warn_unknown(ftr, {"text", "links"}, "footer");
        if (site.contains("compress") && !site["compress"].is_boolean())
            std::cerr << color::yellow("  [config] site.compress 应为布尔值（true/false）\n");
        if (site.contains("jpegQuality") && !site["jpegQuality"].is_number())
            std::cerr << color::yellow("  [config] site.jpegQuality 应为数字（1-100）\n");
        // 侧边栏映射（site.sidebar；兼容顶层 sidebar）：key=版本源目录名或 "blog"，
        // value=相对 .Cdocs/config/ 的 JSON 路径。每个版本 / 博客区各一份独立侧边栏。
        auto read_sidebar_map = [&cfg](const json& obj) {
            if (!obj.is_object()) return;
            for (auto& [k, v] : obj.items())
                if (v.is_string()) cfg.sidebarMap[k] = v.get<std::string>();
        };
        read_sidebar_map(site.contains("sidebar") ? site["sidebar"] : json());
        read_sidebar_map(j.contains("sidebar") ? j["sidebar"] : json());
        } catch (const std::exception& e) {
            // config.json 损坏 / 字段类型错误：不崩溃，用默认配置继续并给出明确提示
            std::cerr << color::error("config.json 解析失败（已用默认配置继续）：") << e.what() << "\n";
        }
    }

    // 2) 侧边栏导航：优先 config.sidebar 映射（key = 版本源目录名，如 md / md-v1；
    //    每个版本一份独立 JSON，文件放 .Cdocs/config/sidebar/），未配置或文件缺失时
    //    回退全局 route.json（旧结构零回归）。
    // 博客区独立侧边栏：config.sidebar["blog"]（可选，缺省时博客页沿用文档导航）。
    {
        auto load_nav_file = [](const fs::path& p, std::vector<NavNode>& nav) {
            std::error_code ec;
            if (!fs::exists(p, ec)) return false;
            try {
                json r = json::parse(read_file(p));
                if (r.contains("sidebar") && r["sidebar"].is_array())
                    for (auto& item : r["sidebar"]) nav.push_back(parse_nav(item));
                return true;
            } catch (...) { return false; }
        };
        std::string navKey = b.in_dir.filename().string();   // "md" / "md-v1" ...
        fs::path navPath = route_path;
        auto it = cfg.sidebarMap.find(navKey);
        if (it != cfg.sidebarMap.end()) {
            fs::path p = g_engine / "config" / it->second;
            std::error_code e2;
            if (fs::exists(p, e2)) navPath = p;
        }
        if (fs::exists(navPath))
            load_nav_file(navPath, cfg.nav);
        auto bit = cfg.sidebarMap.find("blog");
        if (bit != cfg.sidebarMap.end()) {
            fs::path p = g_engine / "config" / bit->second;
            std::error_code e2;
            if (fs::exists(p, e2))
                load_nav_file(p, b.blogNav);
        }
    }

    // 3) 插件派生渲染开关
    opt.showSearch      = cfg.header.showSearch && has_plugin(cfg.plugins, "search");
    opt.showThemeToggle = cfg.header.showThemeToggle && has_plugin(cfg.plugins, "dark-mode");
    opt.showPager       = has_plugin(cfg.plugins, "pager");
    opt.showToc         = has_plugin(cfg.plugins, "toc");
    opt.showBackToTop   = has_plugin(cfg.plugins, "back-to-top");
    opt.showCodeHighlight = has_plugin(cfg.plugins, "code-highlight");
}

// 2) 输入检查 + 收集页面（route 导航，空则按文件名自动发现）+ 预扫描 front matter
static int prepare_pages(BuildContext& b) {
    SiteConfig& cfg = b.cfg;
    std::vector<Page>& pages = b.pages;
    const bool& includeDrafts = b.includeDrafts;
    const fs::path& in_dir = b.in_dir;

    if (!fs::exists(in_dir) || !fs::is_directory(in_dir)) {
        std::cerr << color::error("输入目录不存在: ") << in_dir << "\n";
        return 1;
    }

    // 4) 收集页面：遍历 route.json 叶子节点（前序），即搜索数据来源
    collect_pages(cfg.nav, pages);
    // 兜底：route.json 为空时按 md/ 目录结构自动发现并生成导航
    // （子目录 → 分组，对标 Hugo/VitePress 的"目录即导航"；.md 自动路由到对应子路径）
    if (pages.empty()) {
        std::function<void(const fs::path&, NavNode&)> scan =
            [&](const fs::path& dir, NavNode& node) {
                std::vector<NavNode> files, subdirs;
                std::error_code sec;
                std::vector<fs::path> entries;
                for (auto it = fs::directory_iterator(dir, sec);
                     !sec && it != fs::directory_iterator(); it.increment(sec))
                    entries.push_back(it->path());
                for (const auto& ep : entries) {
                    std::error_code e2;
                    std::string fn = ep.filename().string();
                    if (!fn.empty() && fn[0] == '.') continue;
                    if (fs::is_directory(ep, e2)) {
                        if (fn == "blog") continue;   // 博客目录交给专门的博客流收集，不并入文档导航
                        NavNode grp; grp.title = fn;
                        scan(ep, grp);
                        if (!grp.children.empty()) subdirs.push_back(std::move(grp));
                    } else if (ep.extension() == ".md") {
                        std::string stem = ep.stem().string();
                        if (stem.find('.') != std::string::npos) continue;   // 跳过 x.en.md 变体
                        std::error_code r2;
                        fs::path rel = fs::relative(ep, in_dir, r2);
                        std::string f = rel.generic_string();
                        if (f.size() > 3) f = f.substr(0, f.size() - 3);     // 去 .md
                        NavNode leaf;
                        leaf.file  = f;      // 子目录页自动路由：guide/install → guide/install.html
                        leaf.title = stem;
                        files.push_back(std::move(leaf));
                    }
                }
                std::sort(subdirs.begin(), subdirs.end(),
                          [](const NavNode& a, const NavNode& b) { return a.title < b.title; });
                std::sort(files.begin(), files.end(),
                          [](const NavNode& a, const NavNode& b) { return a.title < b.title; });
                for (auto& g : subdirs) node.children.push_back(std::move(g));
                for (auto& f : files)   node.children.push_back(std::move(f));
            };
        NavNode root;
        scan(in_dir, root);
        cfg.nav = std::move(root.children);
        collect_pages(cfg.nav, pages);
        std::sort(pages.begin(), pages.end(),
                  [](const Page& a, const Page& b) {
                      if (a.weight != b.weight) return a.weight < b.weight;
                      return a.file < b.file;
                  });
    }

    if (pages.empty()) {
        std::cerr << color::error("没有可生成的文档（" + in_dir.string() + " 下无 .md，且 route.json 无叶子节点）\n");
        return 1;
    }

    // 4.5) 预扫描 front matter：读取每篇文档的 draft/weight/title/tags/date，
    //      用于草稿过滤、排序与元数据（自动发现模式已在上面一并处理，这里覆盖 route 模式）。
    {
        std::set<std::string> draftFiles;
        for (auto& pg : pages) {
            fs::path fp = in_dir / (pg.file + ".md");
            if (fs::exists(fp)) {
                std::string body;
                FrontMatter fm = parse_front_matter(read_file(fp), body);
                pg.draft  = fm.draft;
                pg.weight = fm.weight;
                pg.date   = fm.date;
                pg.tags   = fm.tags;
                if (fm.hasTitle) pg.title = fm.title;
                if (pg.draft) draftFiles.insert(pg.file);
            }
        }
        if (!includeDrafts) filter_draft_nav(cfg.nav, draftFiles);
        // 若有页面带 tags，在导航末尾追加“标签”入口，指向标签总览页
        bool hasTags = false;
        for (auto& pg : pages) if (!pg.draft && !pg.tags.empty()) { hasTags = true; break; }
        if (hasTags) {
            NavNode tagLink;
            tagLink.title = "标签";
            tagLink.url = "tags/index.html";
            cfg.nav.push_back(tagLink);
        }
        // 博客流（约定优于配置）：md/blog/ 目录存在 → 收集为博客文章。
        // 与文档双区：blog/x.md（默认语言）+ blog/x.<loc>.md（其他语言），按 date 倒序；
        // 无 blog/ 目录 → 单文档站点，行为与旧版完全一致（零回归）。
        std::error_code sec2;
        fs::path blogDir = in_dir / "blog";
        if (fs::exists(blogDir, sec2) && fs::is_directory(blogDir, sec2)) {
            for (const auto& e : fs::directory_iterator(blogDir, sec2)) {
                if (!e.is_regular_file(sec2) || e.path().extension() != ".md") continue;
                std::string stem = e.path().stem().string();
                if (stem.find('.') != std::string::npos) continue;   // 跳过 blog/x.en.md 等变体
                Page pg;
                pg.file = "blog/" + stem;   // 带前缀：RSS/link/搜索输出相对路径天然正确
                std::string body;
                FrontMatter fm = parse_front_matter(read_file(e.path()), body);
                if (fm.draft && !includeDrafts) continue;
                pg.draft  = fm.draft;
                pg.weight = fm.weight;
                pg.title  = fm.hasTitle ? fm.title : stem;
                pg.tags   = fm.tags;
                pg.date   = fm.date;
                pg.dateT  = parse_date_str(fm.date);
                if (!pg.dateT) pg.dateT = file_mtime_t(e.path());
                b.blog_posts.push_back(std::move(pg));
            }
            std::sort(b.blog_posts.begin(), b.blog_posts.end(),
                      [](const Page& a, const Page& b) { return a.dateT > b.dateT; });
            if (!b.blog_posts.empty()) {
                NavNode blogLink;
                blogLink.title = "{{navBlog}}";
                blogLink.url = "blog/index.html";
                cfg.nav.push_back(blogLink);
            }
        }
    }
    return 0;   // 到达此处即收集成功
}

// ============ 地图模式整页渲染（v2：读 theme/map/<type>.html 按图拼接，无自定义控制流语法） ============
// 数据：C++ 只产 json（PageCtx + 环境数据），HTML 全部在 theme/map/*.html + theme/components/**。

// ---- 站点自定义数据（v6）：.Cdocs/data/*.json ----
// 用户放任意 KV 数据文件（多文件按文件名序加载，键平铺合并）。"有 KV 就拿，没 KV 就算了"——
// 合并进页面数据作用域后，组件数据孔命中即取；未命中的键走内置数据层 / 原样保留（L2 兜底）。
// 优先级：props > 地图 data > 站点 data > 内置引擎键。
static json g_site_data;
static bool g_site_data_loaded = false;
static std::mutex g_site_data_mtx;   // site_data 并发保护（正文渲染多线程首次加载竞争）
static const json& site_data() {
    if (g_site_data_loaded) return g_site_data;
    std::lock_guard<std::mutex> lk(g_site_data_mtx);
    if (g_site_data_loaded) return g_site_data;   // 双检锁
    g_site_data_loaded = true;
    std::error_code ec;
    fs::path d = g_engine / "data";
    if (!fs::is_directory(d, ec)) return g_site_data;
    std::vector<fs::path> files;
    for (auto it = fs::directory_iterator(d, ec), end = fs::directory_iterator();
         it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (it->is_regular_file(ec) && it->path().extension() == ".json") files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
        try {
            json j = json::parse(read_file(f));
            if (j.is_object())
                for (auto it = j.begin(); it != j.end(); ++it) g_site_data[it.key()] = it.value();
        } catch (...) {
            if (comp_warned_once("data:" + f.string()))
                std::cerr << color::warn("警告: ") << "站点数据文件解析失败: " << f << "\n";
        }
    }
    return g_site_data;
}

// ============ 正文 Shortcodes（v7）：<PascalCase/> 标签内容组件 ============
// 语法：<Name/> 自闭合；<Name k="v">…</Name> 带参+包裹（子内容为 Markdown，可嵌套）；
//       首字母大写 = shortcode（与原生 HTML 小写标签天然隔离）；\<Name> 转义为字面量（L3）。
// 管线：prescan_shortcodes(md) 在 md4c 之前把 shortcode 替换为占位 token（跳过代码围栏/行内代码/转义），
//       markdown_to_html + render_admonitions 渲染正文后，expand_shortcodes(html) 把 token 替换为
//       组件渲染结果（innerMd 递归走同一管线 → {{slot}}；参数 → props；{{slot_raw}} = 转义原文）。
// 实例表 thread_local：正文渲染多线程并发（render_content 阶段 1），每文档独立生命周期。
struct ScInst {
    std::string name;
    json props;
    std::string inner;
};
static thread_local std::vector<ScInst> g_sc_insts;
static const char* kScTok = "@@CDOCS_SC_";
static std::string sc_token(int i) { return std::string(kScTok) + std::to_string(i) + "@@"; }

static std::string expand_shortcodes(const std::string& html, bool en);

// 预扫描：md 原文 → shortcode 替换为占位 token；跳过 fenced code / inline code；\<Name> → &lt;Name&gt;
static std::string prescan_shortcodes(const std::string& md) {
    std::string out;
    out.reserve(md.size() + 64);
    const size_t n = md.size();
    size_t i = 0;
    bool inFence = false;
    char fenceCh = 0;
    size_t fenceLen = 0;
    while (i < n) {
        // fenced code 检测（行首 ``` 或 ~~~）
        if (!inFence && (i == 0 || md[i - 1] == '\n') && (md[i] == '`' || md[i] == '~')) {
            size_t j = i, cnt = 0;
            while (j < n && md[j] == md[i]) { ++cnt; ++j; }
            if (cnt >= 3) {
                inFence = true; fenceCh = md[i]; fenceLen = cnt;
                out += md.substr(i, j - i);
                i = j;
                continue;
            }
        }
        if (inFence) {
            size_t nl = md.find('\n', i);
            size_t segEnd = (nl == std::string::npos) ? n : nl + 1;
            std::string line = md.substr(i, segEnd - i);
            size_t ls = 0;
            while (ls < line.size() && (line[ls] == ' ' || line[ls] == '\t')) ++ls;
            size_t cnt = 0;
            while (ls + cnt < line.size() && line[ls + cnt] == fenceCh) ++cnt;
            if (cnt >= fenceLen) inFence = false;
            out += line;
            i = segEnd;
            continue;
        }
        // L3 转义：\<Tabs> → &amp;lt;Tabs&amp;gt;（字面量，不再解析）
        // 注意：反斜杠后是 '<' + 大写字母（标签形态）；\<Tabs> 的 \ 后是 < 不是大写，旧判断永不触发
        if (md[i] == '\\' && i + 1 < n && md[i + 1] == '<'
            && i + 2 < n && isupper((unsigned char)md[i + 2])) {
            size_t gt = md.find('>', i + 2);
            if (gt != std::string::npos) {
                // 整个标签转义：&amp;lt;Tabs&amp;gt; → md4c 解码 &amp; → & → 浏览器显示 <Tabs> 字面量
                out += "&amp;lt;" + md.substr(i + 2, gt - i - 2) + "&amp;gt;";
                i = gt + 1;
                continue;
            }
            out += "&amp;lt;";
            i += 2;
            continue;
        }
        // inline code：`...` 跨度跳过（内容里的标签不解析）
        if (md[i] == '`') {
            size_t j = i, cnt = 0;
            while (j < n && md[j] == '`') { ++cnt; ++j; }
            size_t close = md.find(std::string(cnt, '`'), j);
            if (close != std::string::npos) {
                out += md.substr(i, close + cnt - i);
                i = close + cnt;
                continue;
            }
            out += md[i]; ++i;
            continue;
        }
        // shortcode 开标签：<大写字母
        if (md[i] == '<' && i + 1 < n && isupper((unsigned char)md[i + 1])) {
            size_t j = i + 1;
            while (j < n && isalnum((unsigned char)md[j])) ++j;
            std::string name = md.substr(i + 1, j - i - 1);
            if (!name.empty()) {
                size_t k = j;
                json props;
                bool selfClose = false, ok = true;
                while (k < n) {
                    while (k < n && (md[k] == ' ' || md[k] == '\t' || md[k] == '\n' || md[k] == '\r')) ++k;
                    if (k >= n) { ok = false; break; }
                    if (md[k] == '/' && k + 1 < n && md[k + 1] == '>') { selfClose = true; k += 2; break; }
                    if (md[k] == '>') { ++k; break; }
                    size_t a0 = k;
                    while (k < n && (isalnum((unsigned char)md[k]) || md[k] == '-' || md[k] == '_')) ++k;
                    std::string aname = md.substr(a0, k - a0);
                    while (k < n && (md[k] == ' ' || md[k] == '\t')) ++k;
                    if (k < n && md[k] == '=') {
                        ++k;
                        while (k < n && (md[k] == ' ' || md[k] == '\t')) ++k;
                        std::string aval;
                        if (k < n && (md[k] == '"' || md[k] == '\'')) {
                            char q = md[k]; ++k;
                            size_t v0 = k;
                            while (k < n && md[k] != q) ++k;
                            aval = md.substr(v0, k - v0);
                            if (k < n) ++k;
                        } else {
                            size_t v0 = k;
                            while (k < n && md[k] != ' ' && md[k] != '\t' && md[k] != '\n' && md[k] != '\r' && md[k] != '>' && md[k] != '/') ++k;
                            aval = md.substr(v0, k - v0);
                        }
                        if (!aname.empty()) props[aname] = aval;
                    } else if (!aname.empty()) {
                        props[aname] = true;
                    }
                    if (aname.empty()) { ok = false; break; }
                }
                if (ok) {
                    if (selfClose) {
                        int idx = (int)g_sc_insts.size();
                        g_sc_insts.push_back({name, props, ""});
                        out += sc_token(idx);
                        i = k;
                        continue;
                    }
                    std::string closeTag = "</" + name + ">";
                    size_t close = md.find(closeTag, k);
                    if (close != std::string::npos) {
                        std::string inner = md.substr(k, close - k);
                        int idx = (int)g_sc_insts.size();
                        g_sc_insts.push_back({name, props, inner});
                        out += sc_token(idx);
                        i = close + closeTag.size();
                        continue;
                    }
                    if (comp_warned_once("scopen:" + name))
                        std::cerr << color::warn("警告: ") << "shortcode 未闭合: <" << name << ">（原样保留）\n";
                    out += md.substr(i, k - i);
                    i = k;
                    continue;
                }
            }
        }
        out += md[i];
        ++i;
    }
    return out;
}

// 渲染单个 shortcode 实例：innerMd 递归完整管线 → {{slot}}；参数 → props；{{slot_raw}} = 转义原文
static std::string render_shortcode(int idx, bool en) {
    if (idx < 0 || idx >= (int)g_sc_insts.size()) return {};
    ScInst inst = g_sc_insts[idx];   // 值拷贝：prescan 内部 push_back 扩容时本地副本不受影响（防悬垂引用）
    std::string body = load_component(inst.name);
    if (body.empty()) {
        if (comp_warned_once("sc:" + inst.name))
            std::cerr << color::warn("警告: ") << "shortcode 组件不存在: components/" << inst.name
                      << ".html（正文原样保留）\n";
        return "<" + inst.name + ">";
    }
    std::string innerHtml;
    if (!inst.inner.empty()) {
        std::string md2 = prescan_shortcodes(inst.inner);
        std::string h1 = markdown_to_html(md2);
        std::string h2 = render_admonitions(h1, en);
        innerHtml = expand_shortcodes(h2, en);
    }
    json ctx = inst.props;
    ctx["slot"] = innerHtml;
    ctx["slot_raw"] = esc(inst.inner);
    const json& sd = site_data();
    for (auto it = sd.cbegin(); it != sd.cend(); ++it)
        if (!ctx.contains(it.key())) ctx[it.key()] = it.value();
    std::string res = fill_data_holes(body, ctx);
    return res;
}

// 渲染后：占位 token → 组件渲染结果（嵌套深度上限 16）
static std::string expand_shortcodes(const std::string& html, bool en) {
    std::string out;
    out.reserve(html.size() + 256);
    const size_t n = html.size();
    size_t i = 0;
    int depth = 0;
    const size_t tokLen = std::strlen(kScTok);
    while (i < n) {
        if (html.compare(i, tokLen, kScTok) == 0) {
            size_t e0 = i + tokLen;
            size_t e1 = html.find("@@", e0);
            if (e1 != std::string::npos) {
                int idx = -1;
                try { idx = std::stoi(html.substr(e0, e1 - e0)); } catch (...) {}
                if (idx >= 0 && depth < 16) {
                    ++depth;
                    out += render_shortcode(idx, en);
                    --depth;
                    i = e1 + 2;
                    continue;
                }
            }
        }
        out += html[i];
        ++i;
    }
    return out;
}

// 正文完整管线（shortcode 预扫描 → md4c → admonitions → shortcode 展开）
static std::string render_doc_body(const std::string& md, bool en) {
    g_sc_insts.clear();
    std::string md2 = prescan_shortcodes(md);
    return expand_shortcodes(render_admonitions(markdown_to_html(md2), en), en);
}

static std::string map_render_page(const SiteConfig& cfg, const RenderOpts& opt,
                                   const PageCtx& pcx, const std::string& mapType,
                                   bool isHome = false) {
    std::string title = pcx.title.empty() ? cfg.title : (pcx.title + " · " + cfg.title);
    std::string lang = pcx.curLocale.empty() ? "zh-CN" : pcx.curLocale;
    // 语言切换数据（LangSwitch/LangItem 组件渲染）：items href 加 relBase 前缀（子目录页回退一级）
    json langSwitch = pcx.lang_data;
    if (langSwitch.contains("items") && langSwitch["items"].is_array() && !pcx.relBase.empty()) {
        for (auto& it : langSwitch["items"])
            if (it.contains("href") && it["href"].is_string())
                it["href"] = pcx.relBase + it["href"].get<std::string>();
    }
    json data = {
        {"lang", esc_attr(lang)}, {"theme", esc(cfg.theme)}, {"title", esc(title)},
        {"base", pcx.relBase}, {"body_class", isHome ? " class=\"page-home\"" : ""},
        {"is_home", isHome},
        {"site_title", esc(cfg.title)}, {"site_desc", esc(cfg.description)},
        {"meta_desc", esc(pcx.desc)},
        {"head_meta", pcx.head_meta},
        {"head_links", pcx.head_links},
        {"jsonld", pcx.jsonld},
        {"show_highlight", opt.showCodeHighlight},
        {"theme_vars", cfg.themeVarsBody}, {"custom_css_href", cfg.customCssHref},
        {"header", header_json(cfg, opt, pcx.curLocale, langSwitch, pcx.relBase, isHome)},
        {"nav_groups", pcx.nav_groups},
        {"breadcrumb", pcx.breadcrumb_map},
        {"hero", pcx.hero},
        {"cards", pcx.cards},
        {"pager", pcx.pager},
        {"edit", pcx.edit},
        {"toc_items", pcx.toc_items},
        {"show_toc", opt.showToc && !pcx.toc_items.empty()},
        {"blog_posts", pcx.blog_posts},
        {"blog_pager", pcx.blog_pager},
        {"tags", pcx.tags},
        {"tag_name", pcx.tag_name},
        {"tag_docs", pcx.tag_docs},
        {"body", pcx.body},
        {"last_updated", esc(pcx.last_updated)},
        {"body_end", pcx.body_end},
        {"skip_label", (pcx.curLocale == "en") ? "Skip to main content" : "跳到主要内容"},
        {"footer", footer_json(cfg)},
        {"backtop", json{{"show", opt.showBackToTop}, {"threshold", cfg.backToTopThreshold},
                         {"label", (cfg.backToTopLabel == "↑ 顶部") ? "{{backToTop}}" : esc_attr(cfg.backToTopLabel)}}},
        {"scripts", json{{"highlight", opt.showCodeHighlight}, {"search", opt.showSearch},
                         {"i18n_json", pcx.i18nJson}, {"feedback", cfg.feedbackEndpoint}}}
    };
    // 站点自定义数据（v6）：.Cdocs/data/*.json 合并进页面数据作用域（优先级：props > 地图 data > 站点 data > 内置）
    {
        const json& sd = site_data();
        for (auto it = sd.cbegin(); it != sd.cend(); ++it) {
            data[it.key()] = it.value();
            g_tpl_keys.insert(it.key());   // 站点 data 键加入 L2 白名单（"有 kv 就拿"；没 kv 不误报）
        }
    }
    // 收集合法模板键 → g_tpl_keys（L2 残留检测白名单）
    for (auto it = data.cbegin(); it != data.cend(); ++it) g_tpl_keys.insert(it.key());
    std::string out = compose_page(mapType, data);
    // 首页 layout no-sidebar（地图已写则 find 不命中，无害兜底）
    if (isHome) {
        size_t pl = out.find("<div class=\"layout\">");
        if (pl != std::string::npos)
            out.replace(pl, std::strlen("<div class=\"layout\">"), "<div class=\"layout no-sidebar\">");
    }
    return out;
}

// 3) 多语言构建循环：每个语言输出到独立子目录（未开启 i18n 时单语言输出到根）
static void render_locales(BuildContext& b) {
    // v4 地图驱动必需：theme/map/ 目录（否则无法拼接页面）
    std::error_code mec;
    if (!fs::is_directory(theme_root() / "map", mec)) {
        std::cerr << color::error("错误: 主题缺少 theme/map/ 目录（v4 地图驱动必需）\n");
        return;
    }
    // v5 动态页面类型：config/map.json 的 maps 数组注册所有页面类型（用户可自由增删，
    // 每个页面类型由 {type, map, mode, output?} 声明；mode 决定数据来源与输出方式）。
    // 数量上限 kMaxMapTypes 防意外膨胀/资源耗尽（正常主题远达不到）。
    const int kMaxMapTypes = 64;
    std::error_code rec2;
    json mapRegistry;
    {
        fs::path rp = g_engine / "config" / "map.json";
        if (fs::is_regular_file(rp, rec2)) {
            try { mapRegistry = json::parse(read_file(rp)); } catch (...) {}
        }
    }
    json maps = mapRegistry.value("maps", json());
    // 旧格式兼容：maps 为对象（类型名 → 地图路径）时按内置类型名推导 mode
    if (maps.is_object()) {
        json arr = json::array();
        std::map<std::string, std::string> legacyMode = {
            {"home", "home"}, {"doc", "pages"}, {"blog", "blog-list"},
            {"blog-post", "blog-post"}, {"tags", "tags"}, {"tag-page", "tag-page"}, {"404", "single"}};
        for (auto it = maps.begin(); it != maps.end(); ++it) {
            json e;
            e["type"] = it.key();
            if (it.value().is_string()) e["map"] = it.value().get<std::string>();
            auto lm = legacyMode.find(it.key());
            e["mode"] = (lm != legacyMode.end()) ? lm->second : "single";
            if (it.key() == "404") e["output"] = "404.html";
            arr.push_back(e);
        }
        maps = arr;
    }
    if (!maps.is_array() || maps.empty()) {
        std::cerr << color::error("错误: config/map.json 缺少 maps 数组（v5 页面类型注册表）\n");
        return;
    }
    if (maps.size() > kMaxMapTypes) {
        std::cerr << color::error("错误: 页面类型数量 ") << maps.size() << " 超过上限 "
                  << kMaxMapTypes << "（config/map.json 的 maps 数组）\n";
        return;
    }
    // 按 mode 查找第一个匹配的页面类型名（找不到返回默认名，保持旧行为）
    auto typeForMode = [&](const std::string& mode, const std::string& def) {
        for (const auto& e : maps)
            if (e.is_object() && e.value("mode", "") == mode) return e.value("type", def);
        return def;
    };
    SiteConfig& cfg = b.cfg;
    RenderOpts& opt = b.opt;
    std::vector<Page>& pages = b.pages;
    I18nCfg& i18n = b.i18n;
    I18nDict& fallbackUI = b.fallbackUI;
    const bool& includeDrafts = b.includeDrafts;
    const fs::path& in_dir = b.in_dir;
    const fs::path& out_dir = b.out_dir;

    // 5) 多语言（i18n）构建：每个语言输出到独立子目录（out_dir/<loc>/），
    //    未开启 i18n 时退化为单语言（输出到 out_dir 根，使用兜底中文 UI 字典）。
    std::error_code ec;
    std::vector<std::string> locs;
    if (i18n.enabled) for (auto& kv : i18n.labels) locs.push_back(kv.first);
    else              locs.push_back("");   // 单语言哨兵（空 loc）

    // 前端资源变化检测（语言循环外统一计算，避免多语言下首个语言复制/写 sig 后
    // 后续语言全部跳过——旧实现导致只有首个语言目录刷新 assets）。
    // 签名 = 主题 assets / 运行时 deps 递归所有文件 mtime 最大值；只 stat 目录本身会在"改文件内容"时漏检。
    std::time_t asig = 0;
    std::error_code rec;
    for (const auto& ap : {theme_root() / "assets", g_engine / "deps"}) {
        if (!fs::exists(ap, rec)) continue;
        rec.clear();
        for (auto it = fs::recursive_directory_iterator(ap, rec), end = fs::recursive_directory_iterator();
             it != end; it.increment(rec)) {
            if (rec) { rec.clear(); continue; }
            if (!it->is_regular_file(rec)) continue;
            struct stat ast;
            if (stat(it->path().string().c_str(), &ast) == 0 && ast.st_mtime > asig) asig = ast.st_mtime;
        }
    }
    std::error_code aec;
    fs::path aSigFile = g_engine / ".build" / ".assets.sig";
    fs::create_directories(g_engine / ".build", aec);
    std::string aPrev = fs::exists(aSigFile, aec) ? trim(read_file(aSigFile)) : std::string();
    std::string aCur = std::to_string(asig);
    bool assetsChanged = (aPrev != aCur);   // 源有变化 → 本次全语言刷新

    for (const auto& loc : locs) {
        bool multi = i18n.enabled;
        const I18nDict& dict = multi ? i18n.dicts[loc] : fallbackUI;
        std::string i18nJson = dict_to_json(dict);   // 注入页面，供 app.js 客户端文案本地化
        std::string curLocale = loc;                 // 空 = 单语言（render_page 退化为 zh-CN）
        fs::path locOut = out_dir;
        if (multi) locOut = out_dir / loc;
        fs::create_directories(locOut, ec);
        // 复制前端资源到本语言目录（相对链接 assets/... 在子目录内同样成立）
        // 源签名变化或目标缺失 → 复制；40 个字体 ×2 语言是构建最大 I/O 大头，多数构建命中跳过。
        bool needAssets = assetsChanged || !fs::exists(locOut / "assets");
        if (g_verbose && !needAssets)
            std::cout << color::muted("  [incr] 跳过 assets 复制（源未变）\n");
        if (needAssets) {
            if (fs::exists(theme_root() / "assets"))
                fs::copy(theme_root() / "assets", locOut / "assets", fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
            else
                fs::create_directories(locOut / "assets", ec);
            // 复制前端运行时依赖（deps/ 中的 JS·CSS 库）到 assets/deps
            // 注意：deps/vendor 是 C++ 编译期头文件（md4c / nlohmann），不属于站点运行资源，不随站点发布
            if (fs::exists(g_engine / "deps")) {
                fs::create_directories(locOut / "assets" / "deps", ec);
                for (const auto& e : fs::directory_iterator(g_engine / "deps")) {
                    if (e.path().filename() == "vendor") continue;   // 编译期依赖，跳过发布
                    fs::copy(e.path(), locOut / "assets" / "deps" / e.path().filename(),
                             fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
                }
            }
        }

        // 静态资源发布：md/ 下图片/附件等非 Markdown 文件，按相对路径拷到本语言目录
        copy_doc_assets(in_dir, locOut, ec);

        // 构建期压缩（默认开启，config site.compress=false 可关）：
        // 1) 图片：stb_image + libwebp 生成 WebP 副本（原图保留，页面 <picture> 优先 WebP）；
        // 2) CSS：主题 css/ 保守紧凑化（去注释 + 折叠空白，无收益则不动）。
        if (cfg.compress) {
            int nWebp = webpize_dir(locOut, cfg.jpegQuality);
            if (g_verbose && nWebp > 0)
                std::cout << color::muted("  [compress] 生成 " + std::to_string(nWebp) + " 个 WebP 副本\n");
            compress_dir_css(locOut / "assets" / "css");
        }
        // 资源指纹（cache busting）：对主题 css/js 计算内容哈希，页面引用与 sw 缓存同步加 ?v=
        if (assetsChanged || g_fp.empty() || !fs::exists(locOut / "assets" / "css"))
            fingerprint_assets(locOut / "assets");

        // 语言切换器（仅多语言模式）：链接到兄弟语言的 index.html
        json langData = json{{"show", false}, {"current", ""}, {"items", json::array()}};
        if (multi) {
            // 语言切换数据（地图模式 LangSwitch/LangItem 组件渲染；href 的 relBase 前缀由 map_render_page 修正）
            langData["show"] = true;
            langData["current"] = i18n.labels.count(loc) ? i18n.labels.at(loc) : loc;
            langData["items"] = json::array();
            for (auto& kv : i18n.labels)
                if (kv.first != loc)
                    langData["items"].push_back(json{{"label", kv.second},
                                                     {"href", "../" + kv.first + "/index.html"}});
        }

        // 本语言在站点基址下的前缀（用于 canonical / 交替链接 / 结构化数据）
        std::string homeBase;
        if (!cfg.url.empty()) { homeBase = cfg.url; if (!homeBase.empty() && homeBase.back() != '/') homeBase += '/';
                                if (multi) homeBase += loc + "/"; }

        // 行业标准增强：本语言 RSS / PWA / 社交分享所需的 head 片段与封面图 URL
        std::string feedTitle = i18n_replace(cfg.title, dict);
        std::string feedLinkTag = "  <link rel=\"alternate\" type=\"application/rss+xml\" title=\""
                                  + esc_attr(feedTitle) + "\" href=\"./rss.xml\">\n";
        std::string manifestTag = "  <link rel=\"manifest\" href=\"./manifest.webmanifest\">\n"
                                  "  <meta name=\"theme-color\" content=\"#a8332a\">\n";
        std::string ogImageUrl;
        if (!cfg.ogImage.empty() && !cfg.url.empty()) {
            ogImageUrl = cfg.url;
            if (ogImageUrl.back() != '/') ogImageUrl += '/';
            std::string rel = cfg.ogImage;
            if (!rel.empty() && rel.front() == '/') rel = rel.substr(1);   // 去掉开头 /，避免双斜杠
            if (rel.compare(0, 4, "http") == 0) ogImageUrl = rel;
            else ogImageUrl += rel;
        }

        // hreflang 交替链接（i18n 标准 SEO）：指向其他语言同一页面 + x-default。
        // url 配置时用绝对地址；url 为空时用相对路径，需按页面深度加 ../ 前缀
        // （子目录页如 guide/install 在 zh-CN/guide/，兄弟语言在 ../../en/...）。
        // head 数据化（地图模式 MetaLink/MetaOgItem/MetaNameItem/JsonLd 组件渲染）。
        // canonical/prev/next/hreflang/RSS/manifest → links；OG/Twitter/theme-color → meta；
        // JSON-LD → jsonld 字符串。fallback 仍用 headExtra 字符串（双轨并行）。
        auto build_head_data = [&](const std::string& file, int depth,
                                   const std::string& title, const std::string& desc,
                                   std::time_t published, std::time_t modified,
                                   bool article, const std::vector<std::string>& crumbs,
                                   const std::string& prevFile, const std::string& nextFile) {
            json hd = json::object();
            hd["meta"] = json{{"desc", desc}};
            hd["meta"]["og"] = json::array();
            hd["meta"]["names"] = json::array();
            hd["links"] = json::array();
            std::string up;
            for (int k = 0; k < depth + 1; ++k) up += "../";
            if (!cfg.url.empty()) {
                std::string u = homeBase;
                hd["links"].push_back(json{{"rel", "canonical"}, {"href", u + file + ".html"}, {"attrs", ""}});
                if (!prevFile.empty()) hd["links"].push_back(json{{"rel", "prev"}, {"href", u + prevFile + ".html"}, {"attrs", ""}});
                if (!nextFile.empty()) hd["links"].push_back(json{{"rel", "next"}, {"href", u + nextFile + ".html"}, {"attrs", ""}});
                if (i18n.enabled) {
                    for (auto& kv : i18n.labels) {
                        if (kv.first == loc) continue;
                        std::string ou;
                        if (cfg.url.empty()) ou = up + kv.first + "/" + file + ".html";
                        else { ou = cfg.url; if (!ou.empty() && ou.back() != '/') ou += '/'; ou += kv.first + "/" + file + ".html"; }
                        hd["links"].push_back(json{{"rel", "alternate"}, {"href", ou}, {"attrs", " hreflang=\"" + kv.first + "\""}});
                    }
                    std::string du;
                    if (cfg.url.empty()) du = up + i18n.defaultLocale + "/" + file + ".html";
                    else { du = cfg.url; if (!du.empty() && du.back() != '/') du += '/'; du += i18n.defaultLocale + "/" + file + ".html"; }
                    hd["links"].push_back(json{{"rel", "alternate"}, {"href", du}, {"attrs", " hreflang=\"x-default\""}});
                }
                // JSON-LD：文章页 → BreadcrumbList；首页 → WebSite
                if (article) {
                    std::ostringstream items;
                    int pos = 1;
                    items << "{\"@type\":\"ListItem\",\"position\":" << pos++ << ",\"name\":\""
                          << esc_attr(i18n_replace("{{home}}", dict)) << "\",\"item\":\""
                          << homeBase << "index.html\"}";
                    for (const auto& c : crumbs)
                        items << ",{\"@type\":\"ListItem\",\"position\":" << pos++ << ",\"name\":\""
                              << esc_attr(i18n_replace(c, dict)) << "\"}";
                    items << ",{\"@type\":\"ListItem\",\"position\":" << pos++ << ",\"name\":\""
                          << esc_attr(i18n_replace(title, dict)) << "\",\"item\":\"" << homeBase << file << ".html\"}";
                    hd["jsonld"] = "{\"@context\":\"https://schema.org\",\"@type\":\"BreadcrumbList\",\"itemListElement\":["
                                   + items.str() + "]}";
                } else {
                    hd["jsonld"] = "{\"@context\":\"https://schema.org\",\"@type\":\"WebSite\",\"name\":\""
                                   + esc_attr(i18n_replace(cfg.title, dict)) + "\",\"url\":\"" + homeBase + "index.html\"}";
                }
            } else hd["jsonld"] = "";
            // RSS + PWA manifest + theme-color（RSS/manifest 用 depth 层 ../；hreflang 用 depth+1 层上级语言目录）
            std::string upRss;
            for (int k = 0; k < depth; ++k) upRss += "../";
            hd["links"].push_back(json{{"rel", "alternate"}, {"href", upRss + "rss.xml"},
                                       {"attrs", " type=\"application/rss+xml\" title=\"" + esc_attr(feedTitle) + "\""}});
            hd["links"].push_back(json{{"rel", "manifest"}, {"href", upRss + "manifest.webmanifest"}, {"attrs", ""}});
            hd["meta"]["names"].push_back(json{{"name", "theme-color"}, {"content", "#a8332a"}});
            // OG / Twitter（social_head 数据化；摘要去标题前缀）
            std::string d = desc;
            size_t pp = 0;
            while (pp < d.size() && (d[pp] == ' ' || d[pp] == '\t' || d[pp] == '\n' || d[pp] == '\r')) ++pp;
            if (!title.empty() && d.compare(pp, title.size(), title) == 0) {
                pp += title.size();
                while (pp < d.size() && (d[pp] == ' ' || d[pp] == '\t' || d[pp] == '\n' || d[pp] == '\r')) ++pp;
                d = d.substr(pp);
            }
            std::string ogUrl = cfg.url.empty() ? std::string() : homeBase + file + ".html";
            if (!ogUrl.empty()) hd["meta"]["og"].push_back(json{{"property", "og:url"}, {"content", ogUrl}});
            hd["meta"]["og"].push_back(json{{"property", "og:type"}, {"content", article ? "article" : "website"}});
            hd["meta"]["og"].push_back(json{{"property", "og:title"}, {"content", title}});
            if (!d.empty()) hd["meta"]["og"].push_back(json{{"property", "og:description"}, {"content", d}});
            if (!cfg.url.empty() && !cfg.title.empty())
                hd["meta"]["og"].push_back(json{{"property", "og:site_name"}, {"content", cfg.title}});
            if (!loc.empty()) hd["meta"]["og"].push_back(json{{"property", "og:locale"}, {"content", loc}});
            if (!ogImageUrl.empty()) hd["meta"]["og"].push_back(json{{"property", "og:image"}, {"content", ogImageUrl}});
            if (article) {
                if (published) hd["meta"]["og"].push_back(json{{"property", "article:published_time"}, {"content", iso8601(published)}});
                if (modified)  hd["meta"]["og"].push_back(json{{"property", "article:modified_time"}, {"content", iso8601(modified)}});
            }
            hd["meta"]["names"].push_back(json{{"name", "twitter:card"}, {"content", ogImageUrl.empty() ? "summary" : "summary_large_image"}});
            hd["meta"]["names"].push_back(json{{"name", "twitter:title"}, {"content", title}});
            if (!d.empty()) hd["meta"]["names"].push_back(json{{"name", "twitter:description"}, {"content", d}});
            if (!ogImageUrl.empty()) hd["meta"]["names"].push_back(json{{"name", "twitter:image"}, {"content", ogImageUrl}});
            return hd;
        };
        // 6) 首页（maps 注册表 mode=home；isHome 标记页眉/移动端一致；无左侧边栏）
        // 首页 → PageCtx（hero/cards 数据由 Hero/Cards 组件渲染）
        {
            PageCtx ctx;
            ctx.is_home = true;
            ctx.hero = json{{"title", cfg.homeTitle.empty() ? cfg.title : cfg.homeTitle},
                            {"subtitle", cfg.homeSubtitle.empty() ? cfg.description : cfg.homeSubtitle}};
            std::string ctaFile = cfg.homeCtaFile;
            std::string ctaText = cfg.homeCtaText.empty() ? std::string("{{getStarted}}") : cfg.homeCtaText;
            if (ctaFile.empty() && !pages.empty()) ctaFile = pages[0].file;
            if (!ctaFile.empty()) {
                ctx.hero["cta_href"] = ctaFile + ".html";
                ctx.hero["cta_text"] = ctaText;
            }
            ctx.cards = cards_json(cfg, pages);
            ctx.title = cfg.title;
            ctx.desc = cfg.description;
            ctx.curLocale = curLocale; ctx.lang_data = langData; ctx.i18nJson = i18nJson;
            // head 数据化（MetaLink/MetaOgItem/MetaNameItem/JsonLd 组件渲染）
            {
                json hd = build_head_data("index", 0, cfg.title, cfg.description, 0, 0, false, {}, "", "");
                ctx.head_meta = hd.value("meta", json::object());
                ctx.head_links = hd.value("links", json::array());
                ctx.jsonld = hd.value("jsonld", "");
            }
            std::string landing = map_render_page(cfg, opt, ctx, typeForMode("home", "home"), true);
            std::ofstream(locOut / "index.html")
                << apply_fingerprints(cfg.compress ? wrap_webp(minify_html(i18n_replace(landing, dict)), locOut) : i18n_replace(landing, dict));
        }

        // 7) 各文档页（含 TOC 注入 + 上下篇 + i18n）
        // ---- Hugo 式并发渲染（多核并行）----
        // 两阶段：①并行渲染正文到内存（写各自 pages[i]，无跨元素竞争）；
        //         ②并行拼装页面并写盘（pager 需全部页面 title 就绪）。
        // 有插件注册时退化串行（on_page_rendered 依赖顺序与安全）。
        bool hasPlugs = plugins_any();
        std::mutex sigMutex;                       // pageSig 并发读写保护
        std::vector<char> skip(pages.size(), 0);   // 增量跳过标记（阶段 2 复用）
        // 阶段 1 产物暂存（并发下各线程只写自己索引，安全；阶段 2 只读自身索引）
        std::vector<std::string> tocHtml(pages.size()), tocNav(pages.size());
        std::vector<std::string> metaStore(pages.size());
        std::vector<json> tocItemsStore(pages.size()),
                         crumbsMapStore(pages.size()), headDataStore(pages.size());
        auto render_content = [&](size_t i) {
            if (pages[i].draft && !includeDrafts) return;   // 草稿默认不发布；-D/--drafts 时包含
            // 内容翻译：优先 md/<file>.<loc>.md（多语言），否则退回默认 .md（部分翻译）
            fs::path f = in_dir / (pages[i].file + ".md");
            if (multi) {
                fs::path fl = in_dir / (pages[i].file + "." + loc + ".md");
                if (fs::exists(fl)) f = fl;
            }
            if (!fs::exists(f)) { std::cerr << color::error("缺少文件: ") << f << "\n"; return; }
            // 增量构建：源 .md 指纹（mtime:size）未变且输出已存在 → 跳过渲染复用产物。
            // 指纹无论全量/增量都记录（全量也更新 .pages.sig，否则下次增量永不命中）。
            struct stat fst;
            std::string sig;
            if (stat(f.string().c_str(), &fst) == 0) {
                sig = std::to_string(fst.st_mtime) + ":" + std::to_string((long long)fst.st_size);
                std::string key = pages[i].file + "|" + (multi ? loc : "");
                if (b.incremental && !b.globalDirty) {
                    fs::path existingOut = locOut / (pages[i].file + ".html");
                    std::lock_guard<std::mutex> lk(sigMutex);
                    auto it = b.pageSig.find(key);
                    if (it != b.pageSig.end() && it->second == sig && fs::exists(existingOut)) {
                        skip[i] = 1;
                        if (g_verbose)
                            std::cout << color::muted("  [incr] 跳过未变化: ") << pages[i].file << "\n";
                        return;   // 复用已有 HTML，跳过本次渲染
                    }
                    b.pageSig[key] = sig;   // 记录本次指纹（全量/增量一致）
                } else {
                    std::lock_guard<std::mutex> lk(sigMutex);
                    b.pageSig[key] = sig;
                }
            }
            std::string mdRaw = read_file(f);
            pages[i].dateT = parse_date_str(pages[i].date);
            if (!pages[i].dateT) pages[i].dateT = file_mtime_t(f);
            std::string md;
            FrontMatter fm = parse_front_matter(mdRaw, md);   // 剥离 front matter，取到正文与元数据
            pages[i].html  = render_doc_body(md, curLocale == "en");
            if (pages[i].title.empty()) pages[i].title = extract_title(md, pages[i].file);
            pages[i].lastmod  = fm.lastmod;
            pages[i].aliases  = fm.aliases;
            TocResult toc = build_toc(pages[i].html);
            tocHtml[i] = toc.html; tocNav[i] = toc.toc;
            tocItemsStore[i] = toc.items;   // TOC 数据（TocSidebar 组件）
            std::string excerpt = collapse_ws(strip_tags(pages[i].html));
            if (excerpt.size() > 160) excerpt = truncate_utf8(excerpt, 160) + "…";
            pages[i].desc = fm.description.empty() ? excerpt : fm.description;   // front matter 优先
            // 面包屑：首页 / 祖先分组 / 当前页（当前页不链接）；子目录页首页链接需 ../ 回退
            std::string up;
            { size_t pos = 0; int d = 0;
              while ((pos = pages[i].file.find('/', pos)) != std::string::npos) { ++d; ++pos; }
              for (int k = 0; k < d; ++k) up += "../"; }
            std::vector<std::string> crumbs;
            find_path(cfg.nav, pages[i].file, crumbs);
            // 面包屑数据（Breadcrumb 组件）：首页链接 / 祖先分组（无链接）/ 当前页（current）
            json crumbsJson = json::array();
            crumbsJson.push_back(json{{"title", "{{home}}"}, {"href", up + "index.html"}, {"current", false}});
            for (const auto& c : crumbs)
                crumbsJson.push_back(json{{"title", c}, {"href", ""}, {"current", false}});
            crumbsJson.push_back(json{{"title", pages[i].title}, {"href", ""}, {"current", true}});
            // 最后更新时间 + 阅读时长（front matter lastmod 优先，否则源 .md 修改时间；数字走令牌插值）
            auto [cjk, words] = count_words(strip_tags(pages[i].html));
            int mins = (int)std::ceil(cjk / 300.0 + words / 200.0);
            if (mins < 1) mins = 1;
            std::string updated = pages[i].lastmod.empty() ? format_mtime(f) : pages[i].lastmod;
            std::string luPrefix = dict.count("lastUpdated") ? dict.at("lastUpdated") : "最后更新于";
            std::string rt = dict.count("readingTime") ? dict.at("readingTime")
                                                        : "约 {{minutes}} 分钟阅读（{{words}} 字）";
            rt = subst_tokens(rt, {{"minutes", std::to_string(mins)}, {"words", std::to_string(cjk + words)}});
            std::ostringstream meta;
            if (!updated.empty()) meta << luPrefix << " " << esc(updated) << " · ";
            meta << rt;
            std::string updatedText = meta.str();   // 纯文本（LastUpdated 组件包 <div class="page-meta">）
            // head 数据化（MetaLink/MetaOgItem/MetaNameItem/JsonLd 组件渲染）
            int depth = 0;   // 页面相对语言根的目录深度（guide/install → 1），修正 hreflang 相对路径
            { size_t pos = 0; while ((pos = pages[i].file.find('/', pos)) != std::string::npos) { ++depth; ++pos; } }
            headDataStore[i] = build_head_data(
                pages[i].file, depth, pages[i].title, pages[i].desc,
                pages[i].dateT, pages[i].dateT, true, crumbs,
                (i > 0) ? pages[i - 1].file : std::string(),
                (i + 1 < pages.size()) ? pages[i + 1].file : std::string());
            // 阶段 1 产物暂存到 pages[i] 之外（避免跨线程重读 pages 元素）：面包屑/元信息/head 数据
            {
                // 地图模式面包屑：{links:[有 href], texts:[纯文本], current}（CrumbLink/CrumbText/CrumbCurrent）
                json cm = json::object();
                cm["links"] = json::array();
                cm["texts"] = json::array();
                for (const auto& item : crumbsJson) {
                    if (item.value("current", false)) cm["current"] = item["title"];
                    else if (!item.value("href", "").empty()) cm["links"].push_back(item);
                    else cm["texts"].push_back(item);
                }
                if (!cm.contains("current")) cm["current"] = "";
                crumbsMapStore[i] = cm;
            }
            metaStore[i] = updatedText;          // 纯文本（LastUpdated 组件渲染 <div class="page-meta">）
        };
        auto emit_page = [&](size_t i) {
            if (skip[i]) return;   // 增量跳过（阶段 1 已复用旧产物）
            // 子目录页面（如 guide/install.html）需要 ../ 前缀修正导航/资源相对路径
            std::string relBase;
            {
                size_t pos = 0;
                while ((pos = pages[i].file.find('/', pos)) != std::string::npos) { relBase += "../"; ++pos; }
            }
            // 文档页 → PageCtx（地图模式数据）
            PageCtx ctx;
            ctx.nav_groups = nav_groups_json(cfg.nav, pages[i].file, relBase);
            ctx.toc_items = tocItemsStore[i];
            ctx.pager = pager_json(pages, i, relBase);
            ctx.breadcrumb_map = crumbsMapStore[i];
            ctx.edit = edit_json(cfg, pages[i].file);
            ctx.body = pages[i].html;
            ctx.title = pages[i].title;
            ctx.desc = pages[i].desc;
            ctx.last_updated = metaStore[i];
            auto beIt = g_body_ends.find(curLocale);
            ctx.body_end = (beIt != g_body_ends.end()) ? beIt->second : "";
            ctx.head_meta = headDataStore[i].value("meta", json::object());
            ctx.head_links = headDataStore[i].value("links", json::array());
            ctx.jsonld = headDataStore[i].value("jsonld", "");
            ctx.curLocale = curLocale; ctx.lang_data = langData;
            ctx.i18nJson = i18nJson; ctx.relBase = relBase;
            std::string page = map_render_page(cfg, opt, ctx, typeForMode("pages", "doc"));
            fs::path pageOut = locOut / (pages[i].file + ".html");
            std::error_code pe2;
            fs::create_directories(pageOut.parent_path(), pe2);   // 子目录路由需建父目录
            std::ofstream(pageOut)
                << apply_fingerprints(cfg.compress ? wrap_webp(minify_html(i18n_replace(page, dict)), locOut) : i18n_replace(page, dict));
            // front matter aliases：为每个旧路径生成重定向页（canonical + meta refresh + JS 兜底，
            // 对标 Hugo aliases —— 文档改名后旧链接自动指向新页）
            for (const auto& a : pages[i].aliases) {
                std::string alias = a;
                while (!alias.empty() && (alias.front() == '/' || alias.front() == '\\')) alias = alias.substr(1);
                if (alias.empty()) continue;
                fs::path af = locOut / (alias + ".html");
                std::error_code aec;
                fs::create_directories(af.parent_path(), aec);
                std::error_code a2;
                fs::path tgt = fs::relative(pageOut, af.parent_path(), a2);
                std::string rel = a2 ? std::string("index.html") : tgt.generic_string();
                for (auto& c : rel) if (c == '\\') c = '/';
                std::string rd = "<!DOCTYPE html>\n<html lang=\"" + esc_attr(curLocale) + "\">\n<head>\n"
                    "  <meta charset=\"utf-8\">\n"
                    "  <title>" + esc(pages[i].title) + "</title>\n"
                    "  <link rel=\"canonical\" href=\"" + esc_attr(rel) + "\">\n"
                    "  <meta http-equiv=\"refresh\" content=\"0; url=" + esc_attr(rel) + "\">\n"
                    "  <script>location.replace(\"" + esc_attr(rel) + "\");</script>\n"
                    "</head>\n<body><a href=\"" + esc_attr(rel) + "\">" + esc(pages[i].title) + "</a></body>\n</html>\n";
                std::ofstream(af) << rd;
            }
            // 插件钩子：每页渲染写盘后（有插件时整体串行，保证时序与安全；
            // 路径用绝对路径，插件进程 cwd 是插件目录，相对路径会解析失败）
            if (hasPlugs)
                run_plugin_hooks("on_page_rendered", json{
                    {"file",   pages[i].file},
                    {"locale", multi ? loc : ""},
                    {"path",   fs::absolute(pageOut).string()}
                });
        };
        // 阶段 1 暂存数组已在上面声明（crumbsStore 等）
        if (hasPlugs) { for (size_t i = 0; i < pages.size(); ++i) render_content(i); }
        else run_parallel(pages.size(), render_content);
        if (hasPlugs) { for (size_t i = 0; i < pages.size(); ++i) emit_page(i); }
        else run_parallel(pages.size(), emit_page);

        // 8.5) 博客流（约定优于配置：md/blog/ 存在时启用）
        //      详情页 blog/<name>.html（面包屑=首页/博客/标题，上下篇=博客邻篇）
        //      + 列表页 blog/index.html + 分页 blog/page/N.html（每页 10 篇）
        if (!b.blog_posts.empty()) {
            fs::path blogDir = in_dir / "blog";
            fs::create_directories(locOut / "blog", ec);
            for (size_t bi = 0; bi < b.blog_posts.size(); ++bi) {
                Page& p = b.blog_posts[bi];
                if (p.draft && !includeDrafts) continue;
                std::string rel = p.file.substr(5);          // "blog/xxx" → "xxx"
                fs::path f = blogDir / (rel + ".md");
                if (multi) {
                    fs::path fl = blogDir / (rel + "." + loc + ".md");
                    if (fs::exists(fl)) f = fl;
                }
                if (!fs::exists(f)) continue;
                std::string mdRaw = read_file(f);
                std::string md;
                parse_front_matter(mdRaw, md);
                p.html = render_doc_body(md, false);   // blog 正文跨语言共享，用默认中文标题
                if (p.title.empty()) p.title = extract_title(md, rel);
                std::string excerpt = collapse_ws(strip_tags(p.html));
                if (excerpt.size() > 160) excerpt = truncate_utf8(excerpt, 160) + "…";
                p.desc = excerpt;
                // 面包屑数据（Breadcrumb 组件；博客详情页在 blog/ 下，链接相对本目录）
                json bcJson = json::array();
                bcJson.push_back(json{{"title", "{{home}}"}, {"href", "../index.html"}, {"current", false}});
                bcJson.push_back(json{{"title", "{{navBlog}}"}, {"href", "index.html"}, {"current", false}});
                bcJson.push_back(json{{"title", p.title}, {"href", ""}, {"current", true}});
                json bcMap = json::object();
                bcMap["links"] = json::array();
                bcMap["texts"] = json::array();
                for (const auto& item : bcJson) {
                    if (item.value("current", false)) bcMap["current"] = item["title"];
                    else if (!item.value("href", "").empty()) bcMap["links"].push_back(item);
                    else bcMap["texts"].push_back(item);
                }
                if (!bcMap.contains("current")) bcMap["current"] = "";
                // 元信息：发布日期 + 阅读时长（纯文本，LastUpdated 组件包 <div class="page-meta">）
                auto [cjk, words] = count_words(strip_tags(p.html));
                int mins = (int)std::ceil(cjk / 300.0 + words / 200.0);
                if (mins < 1) mins = 1;
                std::string pub = format_date_local(p.dateT);   // 本地时区，避免 UTC 倒退一天
                std::string rt = dict.count("readingTime") ? dict.at("readingTime")
                                                            : "约 {{minutes}} 分钟阅读（{{words}} 字）";
                rt = subst_tokens(rt, {{"minutes", std::to_string(mins)}, {"words", std::to_string(cjk + words)}});
                std::string meta = esc(pub) + " · " + rt;
                // 上下篇数据（博客邻篇，缺位置灰——行业标准；链接相对 blog/ 目录）
                json pagerBj;
                pagerBj["show"] = (bi > 0) || (bi + 1 < b.blog_posts.size());
                if (bi > 0)
                    pagerBj["prev"] = json{{"show", true}, {"title", b.blog_posts[bi - 1].title},
                                           {"href", b.blog_posts[bi - 1].file.substr(5) + ".html"}};
                else pagerBj["prev"] = json{{"show", false}};
                if (bi + 1 < b.blog_posts.size())
                    pagerBj["next"] = json{{"show", true}, {"title", b.blog_posts[bi + 1].title},
                                           {"href", b.blog_posts[bi + 1].file.substr(5) + ".html"}};
                else pagerBj["next"] = json{{"show", false}};
                // head 数据化（MetaLink/MetaOgItem/MetaNameItem/JsonLd 组件渲染）
                json hd = build_head_data(p.file, 1, p.title, p.desc, p.dateT, p.dateT, true, {}, "", "");
                TocResult t = build_toc(p.html);
                // 博客详情页 → PageCtx
                PageCtx ctx;
                ctx.nav_groups = nav_groups_json(b.blogNav.empty() ? cfg.nav : b.blogNav, "", "../");
                ctx.head_meta = hd.value("meta", json::object());
                ctx.head_links = hd.value("links", json::array());
                ctx.jsonld = hd.value("jsonld", "");
                ctx.toc_items = t.items;
                ctx.pager = pagerBj;
                ctx.breadcrumb_map = bcMap;
                ctx.edit = json{{"show", false}};
                ctx.body = t.html;
                ctx.title = p.title;
                ctx.desc = p.desc;
                ctx.last_updated = meta;
                auto beIt2 = g_body_ends.find(curLocale);
                ctx.body_end = (beIt2 != g_body_ends.end()) ? beIt2->second : "";
                ctx.curLocale = curLocale; ctx.lang_data = langData;
                ctx.i18nJson = i18nJson; ctx.relBase = "../";
                std::string page = map_render_page(cfg, opt, ctx, typeForMode("blog-post", "blog-post"));
                std::ofstream(locOut / "blog" / (rel + ".html"))
                    << apply_fingerprints(cfg.compress ? wrap_webp(minify_html(i18n_replace(page, dict)), locOut) : i18n_replace(page, dict));
            }
            // 列表页 + 分页：第一页 blog/index.html，后续 blog/page/N.html
            // （列表页在 blog/ 下 relBase="../"；分页页在 blog/page/ 下 relBase="../../"）
            {
                const int kPerPage = 10;
                std::vector<const Page*> vis;
                for (const auto& p : b.blog_posts)
                    if (!(p.draft && !includeDrafts)) vis.push_back(&p);
                size_t pagesN = (vis.size() + kPerPage - 1) / kPerPage;
                for (size_t pi = 0; pi < pagesN; ++pi) {
                    bool isPageSub = (pi > 0);                  // 分页页位于 blog/page/
                    std::string relBase = isPageSub ? "../../" : "../";
                    std::string cardBase = isPageSub ? "../" : "";   // 卡片链接前缀
                    std::string navBase  = isPageSub ? "../" : "";   // 分页导航链接前缀
                    // 博客列表数据（BlogList/BlogCard 组件渲染；分页 BlogPager 数据化）
                    json posts = json::array();
                    for (size_t k = pi * kPerPage; k < vis.size() && k < (pi + 1) * kPerPage; ++k) {
                        const Page& p = *vis[k];
                        posts.push_back(json{{"date", format_date_local(p.dateT)},
                                             {"href", cardBase + p.file.substr(5) + ".html"},
                                             {"title", p.title}, {"desc", p.desc}});
                    }
                    auto pageHref = [&](size_t pp) {
                        if (pp == 0) return navBase + "index.html";
                        return navBase + "page/" + std::to_string(pp + 1) + ".html";
                    };
                    json bp = json::object();
                    bp["show"] = (pagesN > 1);
                    if (pagesN > 1) {
                        if (pi > 0) bp["prev_href"] = pageHref(pi - 1);
                        if (pi + 1 < pagesN) bp["next_href"] = pageHref(pi + 1);
                        bp["cur"] = json{{"num", pi + 1}};
                        bp["pages"] = json::array();
                        for (size_t pp = 0; pp < pagesN; ++pp)
                            bp["pages"].push_back(json{{"num", pp + 1}, {"href", pageHref(pp)}});
                    }
                    // 博客列表页 → PageCtx（BlogList/BlogCard/BlogPager 组件渲染）
                    PageCtx ctx;
                    ctx.blog_posts = posts;
                    ctx.blog_pager = bp;
                    ctx.title = "{{blogTitle}}";
                    ctx.desc = cfg.description;
                ctx.curLocale = curLocale; ctx.lang_data = langData;
                ctx.i18nJson = i18nJson; ctx.relBase = relBase;
                ctx.nav_groups = nav_groups_json(b.blogNav.empty() ? cfg.nav : b.blogNav, "", relBase);
                std::string page = map_render_page(cfg, opt, ctx, typeForMode("blog-list", "blog"));
                    if (pi == 0)
                        std::ofstream(locOut / "blog" / "index.html")
                            << apply_fingerprints(cfg.compress ? wrap_webp(minify_html(i18n_replace(page, dict)), locOut) : i18n_replace(page, dict));
                    else {
                        fs::create_directories(locOut / "blog" / "page", ec);
                        std::ofstream(locOut / "blog" / "page" / (std::to_string(pi + 1) + ".html"))
                            << apply_fingerprints(cfg.compress ? wrap_webp(minify_html(i18n_replace(page, dict)), locOut) : i18n_replace(page, dict));
                    }
                }
            }
        }

        // 8) 搜索索引（每语言独立，内容取自该语言正文；标题走 i18n 字典解析）
        {   // 合并博客文章进索引（file 带 blog/ 前缀，链接相对当前语言目录正确）
            std::vector<Page> allPages = pages;
            allPages.insert(allPages.end(), b.blog_posts.begin(), b.blog_posts.end());
            gen_search_index(allPages, includeDrafts, dict, locOut);
        }

        // 9) 标签聚合页：基于 front matter 的 tags，自动生成每个标签一个列表页 + 总览页
        //    （tags 页位于 tags/ 子目录：relBase="../"；博客文章也参与聚合，链接 ../blog/xxx.html）
        {
            std::map<std::string, std::vector<std::string>> tagMap;
            for (const auto& p : pages) {
                if (p.draft && !includeDrafts) continue;
                for (const auto& t : p.tags) tagMap[t].push_back(p.file);
            }
            for (const auto& p : b.blog_posts) {
                if (p.draft && !includeDrafts) continue;
                for (const auto& t : p.tags) tagMap[t].push_back(p.file);
            }
            if (!tagMap.empty()) {
                fs::create_directories(locOut / "tags", ec);
                // 标签聚合数据（TagOverview/TagItem 组件渲染）
                json tags = json::array();
                for (auto& kv : tagMap)
                    // 聚合页位于 tags/ 目录内，标签链接用相对自身的 X.html（不能带 tags/ 前缀）
                    tags.push_back(json{{"name", kv.first}, {"href", slugify(kv.first) + ".html"}});
                // tags 聚合页 → PageCtx（TagOverview/TagItem 组件渲染）
                PageCtx ctx;
                ctx.nav_groups = nav_groups_json(cfg.nav, "", "../");
                ctx.tags = tags;
                ctx.title = "{{allTags}}";
                ctx.desc = cfg.description;
                ctx.curLocale = curLocale; ctx.lang_data = langData;
                ctx.i18nJson = i18nJson; ctx.relBase = "../";
                std::string ov = map_render_page(cfg, opt, ctx, typeForMode("tags", "tags"));
                std::ofstream(locOut / "tags" / "index.html")
                    << apply_fingerprints(cfg.compress ? wrap_webp(minify_html(i18n_replace(ov, dict)), locOut) : i18n_replace(ov, dict));
                for (auto& kv : tagMap) {
                    // 标签单页数据（TagPage/TagDocItem 组件渲染）
                    json docs = json::array();
                    for (auto& fl : kv.second) {
                        std::string t;
                        for (const auto& p : pages) if (p.file == fl) { t = p.title; break; }
                        if (t.empty() && fl.size() > 5 && fl.compare(0, 5, "blog/") == 0)
                            for (const auto& p : b.blog_posts) if (p.file == fl) { t = p.title; break; }
                        if (t.empty()) t = fl;
                        docs.push_back(json{{"href", "../" + fl + ".html"}, {"title", t}});
                    }
                    PageCtx tctx;
                    tctx.nav_groups = nav_groups_json(cfg.nav, "", "../");
                    tctx.tag_name = kv.first;
                    tctx.tag_docs = docs;
                    tctx.title = "#" + kv.first;
                    tctx.desc = cfg.description;
                    tctx.curLocale = curLocale; tctx.lang_data = langData;
                    tctx.i18nJson = i18nJson; tctx.relBase = "../";
                    std::string tp = map_render_page(cfg, opt, tctx, typeForMode("tag-page", "tag-page"));
                    std::ofstream(locOut / "tags" / (slugify(kv.first) + ".html"))
                        << apply_fingerprints(cfg.compress ? wrap_webp(minify_html(i18n_replace(tp, dict)), locOut) : i18n_replace(tp, dict));
                }
            }
        }

        // 10) single 通用单页（maps 注册表 mode=single：404 + 第三方自定义单页统一入口。
        //     output 自定输出文件名（默认 <type>.html）；页面结构/内容由地图 + 地图 data + props 完全自定义）
        for (const auto& e : maps) {
            if (!e.is_object() || e.value("mode", "") != "single") continue;
            std::string stype = e.value("type", "");
            if (stype.empty()) continue;
            std::string output = e.value("output", "");
            if (output.empty()) output = stype + ".html";
            PageCtx ctx;
            ctx.nav_groups = nav_groups_json(cfg.nav, "", "");
            ctx.title = (stype == "404") ? "404" : cfg.title;
            ctx.desc = cfg.description;
            ctx.curLocale = curLocale; ctx.lang_data = langData; ctx.i18nJson = i18nJson;
            std::string sp = map_render_page(cfg, opt, ctx, stype);
            std::ofstream(locOut / output)
                << apply_fingerprints(cfg.compress ? wrap_webp(minify_html(i18n_replace(sp, dict)), locOut) : i18n_replace(sp, dict));
        }

        // 11) RSS / JSON Feed（行业标准，内建，无需 Node；博客文章并入订阅流）
        {
            std::vector<Page> allPages = pages;
            allPages.insert(allPages.end(), b.blog_posts.begin(), b.blog_posts.end());
            gen_feeds(locOut, loc, allPages, cfg, dict, multi);
        }
        // 12) PWA（manifest + service worker + theme-color），内建替代 gen-pwa.js
        gen_pwa(locOut, cfg, feedTitle, theme_root() / "assets");
    }
    // 死链检查放在全部语言构建完成后统一执行（避免语言切换链接在兄弟语言目录
    // 尚未生成时被误报）；对标 VitePress/MkDocs 的链接校验，结果末尾告警不阻断。
    for (const auto& loc : locs) {
        fs::path lo = out_dir;
        if (!loc.empty()) lo = out_dir / loc;
        check_links(lo, loc);
    }
    // 全部语言复制完成后统一写资产签名（源变化时刷新了所有语言目录）
    if (assetsChanged) { std::ofstream f(aSigFile); f << aCur; }
}

// 4) 多语言根 index.html 重定向到默认语言（单语言模式已在循环中生成，无需重定向）
static void write_root_redirect(BuildContext& b) {
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
static void write_root_feeds_pwa(BuildContext& b) {
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
static void write_sitemap(BuildContext& b) {
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
static void write_robots(BuildContext& b) {
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
static void print_summary(BuildContext& b) {
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
        std::cout << color::muted("配置: config.json + sidebar/ | 插件: ");
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
static void scan_output_leftovers(const fs::path& outDir) {
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

// --clean 原子替换：把旧输出目录 rename 为 <out>.old（瞬时 O(1)），
// 避免 Windows Defender 对"删除后同一路径立即重建"的逐文件全量扫描
// --clean 清理：直接删除输出目录。
// 实测 rename 原子替换（dist → dist.old → 重建 → 删 .old）在 Windows 上无效：
// rename 后 .old 成为"新路径"，Defender 实时防护会全量扫描它并拖慢同目录写入，
// 反而比直接 remove_all 更不稳。故恢复简单可靠的 remove_all。
int run_build(fs::path in_dir, fs::path out_dir, bool includeDrafts, bool cleanBefore) {
    // 每次构建重置 i18n 未命中键收集（serve --watch 会反复重建，不能跨次累积）
    g_i18n_missing.clear();
    g_link_broken.clear();
    g_tpl_keys.clear();

    // ---- 版本化文档分派（Docusaurus 风格）----
    // 仅最外层调用执行版本循环（static 重入锁：子版本构建不再分派，避免无限递归）。
    // 先探测 config.json 的 versions 列表；非空则对每个版本独立构建到 dist/<name>/，
    // 并在全部完成后生成根 index.html 重定向到默认版本。单版本站点完全不走此分支。
    static bool s_version_dispatching = false;
    if (!s_version_dispatching) {
        s_version_dispatching = true;
        fs::path cfgp = g_engine / "config/config.json";
        std::vector<VersionCfg> vers;
        if (fs::exists(cfgp)) {
            try {
                json j = json::parse(read_file(cfgp));
                json site = j.contains("site") && j["site"].is_object() ? j["site"] : j;
                if (site.contains("versions") && site["versions"].is_array()) {
                    for (auto& v : site["versions"]) {
                        if (!v.is_object() || !v.contains("name")) continue;
                        VersionCfg vc;
                        vc.name = v["name"].get<std::string>();
                        vc.label = v.value("label", vc.name);
                        vc.source = v.value("source", std::string());
                        vc.default_v = v.value("default", false);
                        vers.push_back(std::move(vc));
                    }
                    if (!vers.empty() && !vers[0].default_v) {
                        bool any = false;
                        for (auto& v : vers) if (v.default_v) { any = true; break; }
                        if (!any) vers[0].default_v = true;
                    }
                }
            } catch (...) { vers.clear(); }
        }

        // 约定优于配置：config 未声明 versions 时，自动扫描 in_dir 同级下的
        // "<源目录名>-*" 快照目录（如 md/ 旁的 md-v1/、md-v2/）识别为历史版本。
        // md/ 恒为 current（默认版）；无任何快照目录 → 单版本，行为与旧版完全一致。
        if (vers.empty()) {
            std::string base = in_dir.filename().string();   // 如 "md"
            // in_dir 可能是相对路径（如 "md"），parent_path() 为空 → 用 "." 表示项目根
            fs::path parent = in_dir.parent_path();
            if (parent.empty()) parent = fs::path(".");
            std::error_code sec;
            if (fs::exists(parent, sec) && fs::is_directory(parent, sec)) {
                std::vector<std::string> snapshots;
                for (auto& e : fs::directory_iterator(parent, sec)) {
                    if (!e.is_directory(sec)) continue;
                    std::string name = e.path().filename().string();
                    // 匹配 "<base>-<suffix>"，且排除 .Cdocs/.build/dist 等隐藏/产物目录
                    if (name.size() > base.size() + 1 && name.compare(0, base.size(), base) == 0
                        && name[base.size()] == '-' && name[0] != '.') {
                        snapshots.push_back(name);
                    }
                }
                if (!snapshots.empty()) {
                    // current 恒为首位 + 默认；历史版本按名排序（md-v1 < md-v2 < ...）
                    std::sort(snapshots.begin(), snapshots.end());
                    VersionCfg cur;
                    cur.name = "current";
                    cur.label = "最新";
                    cur.default_v = true;
                    vers.push_back(std::move(cur));
                    for (auto& s : snapshots) {
                        VersionCfg vc;
                        vc.name = s.substr(base.size() + 1);   // md-v1 → v1
                        vc.label = vc.name;                    // label 默认即版本名
                        vc.source = s;
                        vers.push_back(std::move(vc));
                    }
                    if (!g_quiet)
                        std::cout << color::muted("  [versions] 自动识别 ") << snapshots.size()
                                  << " 个历史版本: ";
                    for (auto& v : vers)
                        if (!g_quiet) std::cout << color::cyan(v.name) << " ";
                    if (!g_quiet) std::cout << "\n";
                }
            }
        }

        if (!vers.empty()) {
            // 多版本模式：dist 整体重建（每个版本独立子目录）
            if (cleanBefore) {
                std::error_code ec2;
                fs::remove_all(out_dir, ec2);
            }
            std::string defName;
            for (const auto& v : vers) if (v.default_v) defName = v.name;

            // 把完整版本列表序列化传给子构建（供 header 版本下拉）
            {
                json va = json::array();
                for (const auto& v : vers) {
                    va.push_back({{"name", v.name}, {"label", v.label},
                                  {"source", v.source}, {"default", v.default_v}});
                }
                g_versions_json = va.dump();
            }

            for (const auto& v : vers) {
                // 版本源目录：source 为空用主 in_dir（md）；否则取 in_dir 同级下的 <source>
                // （in_dir 可能为相对路径，parent 为空时用 "." 表示项目根）
                fs::path parent = in_dir.parent_path();
                if (parent.empty()) parent = fs::path(".");
                fs::path vIn  = in_dir;
                if (!v.source.empty() && v.source != in_dir.filename().string())
                    vIn = parent / v.source;
                fs::path vOut = out_dir / v.name;
                if (!g_quiet)
                    std::cout << color::bold(color::cyan("\n=== 构建版本 "))
                              << color::cyan(v.label) << color::bold(color::cyan(" → ")) << vOut << "\n";
                // 版本信息通过全局传给子构建：内部 load_site_config 读取
                g_cur_version = v.name;
                g_cur_version_label = v.label;
                int rc = run_build(vIn, vOut, includeDrafts, false);
                g_cur_version.clear();
                g_cur_version_label.clear();
                if (rc) { g_versions_json.clear(); s_version_dispatching = false; return rc; }
            }
            g_versions_json.clear();
            // 根 index.html：重定向到默认版本（Docusaurus 同款行为）
            if (!defName.empty()) {
                std::ofstream(out_dir / "index.html")
                    << "<!DOCTYPE html>\n<html lang=\"zh-CN\">\n<head>\n"
                    << "  <meta charset=\"utf-8\">\n  <title>Redirecting…</title>\n"
                    << "  <meta http-equiv=\"refresh\" content=\"0; url=./" << esc_attr(defName) << "/index.html\">\n"
                    << "  <link rel=\"canonical\" href=\"./" << esc_attr(defName) << "/index.html\">\n"
                    << "</head>\n<body>\n  <p>正在跳转到默认版本 <a href=\"./"
                    << esc_attr(defName) << "/index.html\">" << esc(defName) << "</a> …</p>\n</body>\n</html>\n";
            }
            s_version_dispatching = false;
            return 0;
        }
        s_version_dispatching = false;
    }

    // 输出横幅 + --clean 清理（阶段 1 起的状态集中到 BuildContext）


    if (g_verbose)
        std::cout << color::muted("source=") << in_dir << color::muted(" dest=") << out_dir
                  << color::muted(" engine=") << g_engine << "\n";
    if (!g_quiet)
        std::cout << color::bold(color::cyan("Cdocs")) << " " << CDOCS_VERSION
                  << color::muted(" — 生成 ") << in_dir << color::muted(" → ") << out_dir << "\n";

    if (cleanBefore) {
        std::error_code ec2;
        fs::remove_all(out_dir, ec2);
        if (g_verbose && !ec2) std::cout << color::muted("已清空输出目录: ") << out_dir << "\n";
    }

    BuildContext b;
    b.in_dir        = in_dir;
    b.out_dir       = out_dir;
    b.includeDrafts = includeDrafts;

    // 外部脚本插件：扫描 .Cdocs/plugins/*/plugin.json（无插件目录时零开销）
    plugins_scan_all();

    load_site_config(b);                      // 1) 配置 + 导航 + 渲染开关
    // 插件钩子：配置加载完成后（可让插件增强/修改站点配置相关上下文）。
    // 收集各插件 out.json 的 inject 对象（key=语言 → 正文末尾注入 HTML，如评论插件），
    // 渲染时按当前语言插入正文末尾（引擎只提供通用注入能力，不感知具体插件）。
    g_body_ends.clear();
    {
        std::vector<json> outs;
        run_plugin_hooks("on_config", json{
            {"source", fs::absolute(in_dir).string()},
            {"dest",   fs::absolute(out_dir).string()},
            {"engine", fs::absolute(g_engine).string()},
            {"title",  b.cfg.title},
            {"plugins", b.cfg.plugins}
        }, &outs);
        for (const auto& o : outs) {
            if (!o.is_object() || !o.contains("inject") || !o["inject"].is_object()) continue;
            for (auto& [loc, html] : o["inject"].items())
                if (html.is_string())
                    g_body_ends[loc] += html.get<std::string>() + "\n";
        }
    }
    if (int rc = prepare_pages(b)) return rc; // 2) 输入检查 + 收集页面 + 预扫描

    // 增量构建判定：仅 serve -w 且未 --clean 时启用。
    // 全局签名（config/route/i18n/templates/assets 的 mtime 最大值）变化 → 全量；
    // 否则源 .md 未变的页面跳过渲染（复用已生成产物），加速 watch 循环。
    if (g_incremental && !cleanBefore) {
        std::error_code ec;
        std::time_t sig = 0;
        for (const auto& p : {g_engine / "config" / "config.json",
                              g_engine / "config" / "route.json",
                              g_engine / "i18n",
                              theme_root() / "theme.json",
                              theme_root() / "templates",
                              theme_root() / "assets"}) {
            struct stat st;
            if (stat(p.string().c_str(), &st) == 0 && st.st_mtime > sig) sig = st.st_mtime;
        }
        // 与上次构建签名比较（存 .build/.sig 文件）
        fs::path buildDir = g_engine / ".build";
        fs::create_directories(buildDir, ec);
        fs::path sigFile = buildDir / ".sig";
        std::string prev = fs::exists(sigFile, ec) ? trim(read_file(sigFile)) : std::string();
        b.globalDirty = (prev != std::to_string(sig));
        if (b.globalDirty && !g_quiet)
            std::cout << color::muted("  [incr] 全局配置变化，全量重建\n");
        else if (!b.globalDirty)
            std::cout << color::muted("  [incr] 全局未变，增量重建\n");
        // 写入本次签名
        { std::ofstream f(sigFile); f << sig; }
        // 载入上次页面指纹（file|loc -> mtime:size）
        fs::path sigMapFile = buildDir / ".pages.sig";
        if (fs::exists(sigMapFile, ec)) {
            try {
                json j = json::parse(read_file(sigMapFile));
                for (auto it = j.begin(); it != j.end(); ++it)
                    b.pageSig[it.key()] = it.value().get<std::string>();
            } catch (...) { b.pageSig.clear(); }
        }
        b.incremental = !b.globalDirty;
    }

    // 插件钩子：页面收集完成后（插件可拿到页面清单做预处理/增删页）
    {
        json pagesArr = json::array();
        for (const auto& p : b.pages) {
            pagesArr.push_back({
                {"file",  p.file},
                {"title", p.title},
                {"draft", p.draft},
                {"tags",  p.tags}
            });
        }
        run_plugin_hooks("on_page_collected", json{
            {"source", fs::absolute(in_dir).string()},
            {"dest",   fs::absolute(out_dir).string()},
            {"count",  b.pages.size()},
            {"pages",  pagesArr}
        });
    }
    render_locales(b);                        // 3) 多语言构建循环
    write_root_redirect(b);                   // 4) 多语言根 index.html 重定向
    write_root_feeds_pwa(b);                  // 5) 根目录 feed / PWA
    write_sitemap(b);                         // 6) sitemap.xml
    write_robots(b);                          // 7) robots.txt
    // 插件钩子：全部产物生成完成后（部署 / 压缩 / 通知等收尾）
    run_plugin_hooks("on_done", json{
        {"source", fs::absolute(in_dir).string()},
        {"dest",   fs::absolute(out_dir).string()},
        {"engine", fs::absolute(g_engine).string()}
    });
    // 持久化页面指纹，供下次 watch 增量判定（全量/增量都写，首次全量后即可增量）
    if (!b.pageSig.empty()) {
        json j = json::object();
        for (const auto& kv : b.pageSig) j[kv.first] = kv.second;
        std::error_code ec;
        fs::create_directories(g_engine / ".build", ec);
        std::ofstream f(g_engine / ".build" / ".pages.sig");
        f << j.dump();
    }
    print_summary(b);                         // 8) 汇总输出
    scan_output_leftovers(out_dir);           // 9) 残留检测：{{}}/组件标签残留 → 显式警告
    return 0;
}


// ============ 内置 i18n 字典（新站点初始化用，确保完整可构建） ============
static const char* kZhCN = R"CDOCS({
  "siteTitle": "Cdocs 文档",
  "siteDesc": "一个用 C++ 编写、复用成熟组件（md4c + nlohmann/json + FlexSearch）的极简静态文档站点生成器。",
  "menuToggleLabel": "打开导航",
  "searchPlaceholder": "搜索文档…",
  "themeToggleLabel": "切换主题",
  "tocTitle": "本页目录",
  "home": "首页",
  "getStarted": "开始阅读",
  "prevLabel": "上一篇",
  "nextLabel": "下一篇",
  "pagerNone": "暂无",
  "editThisPage": "编辑此页",
  "lastUpdated": "最后更新于",
  "readingTime": "约 {{minutes}} 分钟阅读（{{words}} 字）",
  "backToTop": "顶部",
  "notFoundTitle": "页面不见了",
  "notFoundDesc": "你访问的页面不存在或已被移动。",
  "backHome": "返回首页",
  "localeLabel": "语言",
  "brand": "Cdocs",
  "navGitHub": "GitHub",
  "navProject": "项目主页",
  "footerText": "© 2026 Cdocs · 基于 C++、md4c、nlohmann/json、FlexSearch 构建",
  "navHome": "首页",
  "navDocs": "文档",
  "navGettingStarted": "入门",
  "navIntro": "介绍",
  "navGuide": "使用指南",
  "navReference": "参考",
  "navApi": "接口说明",
  "navAdvanced": "进阶",
  "navArchitecture": "架构",
  "navRendering": "渲染",
  "navInternals": "底层实现",
  "navPipeline": "渲染管线",
  "navRender": "渲染循环",
  "allTags": "全部标签",
  "navBlog": "博客",
  "blogTitle": "博客",
  "copyCode": "复制代码",
  "copy": "复制",
  "copied": "已复制",
  "searchLoadFailed": "搜索数据加载失败",
  "useHttpServer": "：请用本地服务器(http)访问，不要直接以 file:// 打开本页",
  "noResults": "没有匹配「",
  "noResultsSuffix": "」的结果",
  "cmdTitle": "搜索文档",
  "cmdPlaceholder": "搜索文档…",
  "cmdType": "输入关键词开始搜索",
  "mermaidError": "图表渲染失败",
  "rssLabel": "RSS",
  "printPage": "打印 / 导出 PDF",
  "feedbackTitle": "本页有帮助吗？",
  "feedbackYes": "有帮助",
  "feedbackNo": "需改进",
  "feedbackThanks": "感谢你的反馈！",
  "lightboxLabel": "图片预览",
  "close": "关闭"
})CDOCS";

static const char* kEn = R"CDOCS({
  "siteTitle": "Cdocs Docs",
  "siteDesc": "A minimal static documentation site generator written in C++, reusing mature components (md4c + nlohmann/json + FlexSearch).",
  "menuToggleLabel": "Open navigation",
  "searchPlaceholder": "Search md…",
  "themeToggleLabel": "Toggle theme",
  "tocTitle": "On this page",
  "home": "Home",
  "getStarted": "Get started",
  "prevLabel": "Previous",
  "nextLabel": "Next",
  "pagerNone": "None",
  "editThisPage": "Edit this page",
  "lastUpdated": "Last updated:",
  "readingTime": "About {{minutes}} min read ({{words}} words)",
  "backToTop": "Top",
  "notFoundTitle": "Page not found",
  "notFoundDesc": "The page you are looking for does not exist or has moved.",
  "backHome": "Back to home",
  "localeLabel": "Language",
  "brand": "Cdocs",
  "navGitHub": "GitHub",
  "navProject": "Project Home",
  "footerText": "© 2026 Cdocs · Built with C++, md4c, nlohmann/json, FlexSearch",
  "navHome": "Home",
  "navDocs": "Docs",
  "navGettingStarted": "Getting Started",
  "navIntro": "Introduction",
  "navGuide": "Guide",
  "navReference": "Reference",
  "navApi": "API Reference",
  "navAdvanced": "Advanced",
  "navArchitecture": "Architecture",
  "navRendering": "Rendering",
  "navInternals": "Internals",
  "navPipeline": "Render Pipeline",
  "navRender": "Render Loop",
  "allTags": "All Tags",
  "navBlog": "Blog",
  "blogTitle": "Blog",
  "copyCode": "Copy code",
  "copy": "Copy",
  "copied": "Copied",
  "searchLoadFailed": "Failed to load search data",
  "useHttpServer": ": please access via a local HTTP server, not file://",
  "noResults": "No results for \"",
  "noResultsSuffix": "\"",
  "cmdTitle": "Search md",
  "cmdPlaceholder": "Search md…",
  "cmdType": "Type to search",
  "mermaidError": "Failed to render diagram",
  "rssLabel": "RSS",
  "printPage": "Print / Export PDF",
  "feedbackTitle": "Was this page helpful?",
  "feedbackYes": "Yes",
  "feedbackNo": "No",
  "feedbackThanks": "Thanks for your feedback!",
  "lightboxLabel": "Image preview",
  "close": "Close"
})CDOCS";

// 把文件名/段名美化为页面标题（my-page → My Page）
static std::string pretty_title(const std::string& stem) {
    std::string t = stem;
    for (size_t i = 0; i < t.size(); i++)
        if (t[i] == '-' || t[i] == '_') t[i] = ' ';
    if (!t.empty() && t[0] >= 'a' && t[0] <= 'z') t[0] = (char)(t[0] - 'a' + 'A');
    return t;
}

// init：在指定目录创建一个完整的新站点（对标 hugo new site）
// 交互式询问内容区（文档/博客/两者）与是否带历史版本（md-v1/）；useDefaults 跳过（默认 文档+博客，不带版本）
int cmd_init(fs::path dir, bool copyExe, bool useDefaults) {
    std::error_code ec;
    if (fs::exists(dir) && fs::is_directory(dir) && !fs::is_empty(dir, ec)) {
        std::cerr << color::error("目标目录已存在且非空: ") << dir << "\n";
        return 1;
    }

    // ---- 交互选择内容区与版本（--defaults / 非终端时用默认） ----
    int contentMode = 3;          // 1=仅文档  2=仅博客  3=文档+博客
    bool withVersion = false;     // 文档带历史版本（md-v1/）
    bool interactive = !useDefaults;
    if (interactive) {
#ifdef _WIN32
        // Windows 控制台：getline 前需保证 stdin 未损坏
#endif
        std::string line;
        std::cout << "\n" << color::cyan("初始化内容区：") << "\n"
                  << "  [1] 仅文档（md/docs/）\n"
                  << "  [2] 仅博客（md/blog/）\n"
                  << "  [3] 文档 + 博客\n"
                  << color::muted("请选择 (1-3，默认 3): ") << std::flush;
        if (std::getline(std::cin, line) && !line.empty()) {
            int v = std::atoi(line.c_str());
            if (v >= 1 && v <= 3) contentMode = v;
        }
        if (contentMode != 2) {   // 含文档 → 询问是否带版本
            std::cout << color::muted("文档区要带历史版本快照（md-v1/）吗？[y/N] ") << std::flush;
            line.clear();
            std::getline(std::cin, line);
            withVersion = (line == "y" || line == "Y" || line == "yes");
        }
        std::cout << "\n";
    }
    bool needDocs = (contentMode != 2);   // 内容区含文档
    bool needBlog = (contentMode != 1);   // 内容区含博客
    fs::create_directories(dir, ec);
    fs::create_directories(dir / "md", ec);
    fs::create_directories(dir / "md" / "docs", ec);
    fs::create_directories(dir / "archetypes", ec);
    fs::create_directories(dir / ".Cdocs" / "config", ec);
    fs::create_directories(dir / ".Cdocs" / "i18n", ec);
    fs::create_directories(dir / ".Cdocs" / "data", ec);   // 站点自定义数据（v6）：任意 KV 文件

    // 复制运行所需资源（theme 主题 + deps 运行时依赖；config/i18n 会被下面写入覆盖）
    fs::path eng = exe_dir() / ".Cdocs";
    bool haveEngine = fs::exists(eng);
    if (haveEngine) {
        for (const char* sub : { "theme", "deps" }) {
            fs::path src = eng / sub;
            if (fs::exists(src))
                fs::copy(src, dir / ".Cdocs" / sub,
                         fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        }
        if (copyExe) {
            fs::path exe = exe_dir() / "Cdocs.exe";
            if (fs::exists(exe))
                fs::copy(exe, dir / "Cdocs.exe", fs::copy_options::overwrite_existing, ec);
        }
    }

    std::string stem = dir.stem().empty() ? "md" : dir.stem().string();

    // 1) config.json（三区块：site 全局 / head 页眉 / center 内容 / footer 页脚）
    //    开箱即双语 + 全功能；所有 true/false 开关集中在对应区块顶部便于配置
    {
        std::ofstream o(dir / ".Cdocs/config/config.json");
        o << "{\n"
          << "  \"site\": {\n"
          << "    \"title\": \"" << esc_attr(stem) << "\",\n"
          << "    \"description\": \"由 Cdocs 生成的静态文档站\",\n"
          << "    \"theme\": \"dark\",\n"
          << "    \"url\": \"\",\n"
          << "    \"i18n\": {\n"
          << "      \"defaultLocale\": \"zh-CN\",\n"
          << "      \"dir\": \".Cdocs/i18n\",\n"
          << "      \"locales\": { \"zh-CN\": { \"label\": \"简体中文\" }, \"en\": { \"label\": \"English\" } }\n"
          << "    },\n"
          << "    \"editLink\": { \"base\": \"\", \"docsDir\": \"md\" },\n"
          << "    \"sidebar\": {\n";
        if (needDocs)
            o << "      \"md\": \"sidebar/md.json\",\n";
        if (withVersion)
            o << "      \"md-v1\": \"sidebar/v1.json\",\n";
        if (needBlog)
            o << "      \"blog\": \"sidebar/blog.json\"\n";
        o << "    }\n"
          << "  },\n"
          << "  \"head\": {\n"
          << "    \"logo\": \"\",\n"
          << "    \"showSearch\": true,\n"
          << "    \"showThemeToggle\": true,\n"
          << "    \"nav\": [\n"
          << "      { \"title\": \"{{navHome}}\", \"file\": \"index\" }";
        if (needDocs) o << ",\n      { \"title\": \"{{navDocs}}\", \"file\": \"docs/intro\" }";
        if (needBlog) o << ",\n      { \"title\": \"{{navBlog}}\", \"file\": \"blog/index\" }";
        o << "\n    ]\n"
          << "  },\n"
          << "  \"center\": {\n"
          << "    \"plugins\": [\"search\", \"dark-mode\", \"pager\", \"back-to-top\", \"toc\", \"code-highlight\"],\n"
          << "    \"backToTop\": { \"threshold\": 300, \"label\": \"顶部\" }\n"
          << "  },\n"
          << "  \"footer\": {\n"
          << "    \"text\": \"© 2026 " << esc_attr(stem) << " · 由 Cdocs 生成\"\n"
          << "  }\n"
          << "}\n";
    }

    // 2) sidebar/ 分文件侧边栏（新结构：每个版本 / 博客区一份独立 JSON，名字可自定义）
    //    （不再生成全局 route.json——侧边栏已按版本/区域拆分，config.json 的 site.sidebar 映射接管）
    {
        std::error_code sec;
        fs::create_directories(dir / ".Cdocs/config/sidebar", sec);
        if (needDocs) {
            std::ofstream(dir / ".Cdocs/config/sidebar/md.json")
                << "{\n  \"sidebar\": [\n"
                << "    {\n      \"title\": \"{{navGettingStarted}}\",\n      \"items\": [\n"
                << "        { \"title\": \"{{navIntro}}\", \"file\": \"docs/intro\" },\n"
                << "        { \"title\": \"{{navGuide}}\", \"file\": \"docs/guide\" }\n"
                << "      ]\n    }\n  ]\n}\n";
            if (withVersion) {
                std::ofstream(dir / ".Cdocs/config/sidebar/v1.json")
                    << "{\n  \"sidebar\": [\n"
                    << "    {\n      \"title\": \"v1 快照\",\n      \"items\": [\n"
                    << "        { \"title\": \"v1 示例\", \"file\": \"docs/old-intro\" }\n"
                    << "      ]\n    }\n  ]\n}\n";
            }
        }
        if (needBlog) {
            std::ofstream(dir / ".Cdocs/config/sidebar/blog.json")
                << "{\n  \"sidebar\": [\n"
                << "    {\n      \"title\": \"博客\",\n      \"items\": [\n"
                << "        { \"title\": \"示例博文\", \"file\": \"blog/hello-cdocs\" }\n"
                << "      ]\n    }\n  ]\n}\n";
        }
    }

    // 3) i18n 字典（双语完整，确保可构建）
    std::ofstream(dir / ".Cdocs/i18n/zh-CN.json") << kZhCN;
    std::ofstream(dir / ".Cdocs/i18n/en.json")   << kEn;

    // 3.5) 站点自定义数据示例（v6：.Cdocs/data/*.json 任意 KV，合并进页面数据作用域——
    //     组件数据孔 {{products.0.name}} 等命中即取；没 kv 就算了，走内置数据层）
    std::ofstream(dir / ".Cdocs/data/site.json")
        << "{\n"
        << "  \"site_author\": \"Cdocs 团队\",\n"
        << "  \"products\": [\n"
        << "    { \"name\": \"Cdocs 生成器\", \"price\": \"免费\" },\n"
        << "    { \"name\": \"Cdocs 主题包\", \"price\": \"开源\" }\n"
        << "  ]\n"
        << "}\n";

    // 4) 页面地图注册表（v2：C++ 构建时读此配置了解有哪些站点地图；地图本体在 theme/map/，JSON 约定）
    std::ofstream(dir / ".Cdocs/config/map.json")
        << "{\n"
        << "  \"maps\": [\n"
        << "    { \"type\": \"home\", \"map\": \"map/home.json\", \"mode\": \"home\" },\n"
        << "    { \"type\": \"doc\", \"map\": \"map/doc.json\", \"mode\": \"pages\" },\n"
        << "    { \"type\": \"blog\", \"map\": \"map/blog.json\", \"mode\": \"blog-list\" },\n"
        << "    { \"type\": \"blog-post\", \"map\": \"map/blog-post.json\", \"mode\": \"blog-post\" },\n"
        << "    { \"type\": \"tags\", \"map\": \"map/tags.json\", \"mode\": \"tags\" },\n"
        << "    { \"type\": \"tag-page\", \"map\": \"map/tag-page.json\", \"mode\": \"tag-page\" },\n"
        << "    { \"type\": \"404\", \"map\": \"map/404.json\", \"mode\": \"single\", \"output\": \"404.html\" }\n"
        << "  ],\n"
        << "  \"templates\": {\n"
        << "    \"base\": \"map/base.json\"\n"
        << "  }\n"
        << "}\n";

    // 4) 示例内容（按交互选择：文档 / 博客 / 版本）
    if (needDocs) {
    std::ofstream(dir / "md/docs/intro.md") <<
        "# 欢迎使用 Cdocs\n\n"
        "Cdocs 是一个用 **C++** 编写的静态文档站生成器。你在 `md/docs/` 下写 Markdown，"
        "它生成零依赖、可离线、中英双语的静态站点。\n\n"
        "## 快速开始\n\n"
        "1. 编写文档：`md/docs/` 下新建 `xxx.md`（英文版 `xxx.en.md`）。\n"
        "2. 构建站点：`Cdocs build` 生成到 `dist/`。\n"
        "3. 本地预览：`Cdocs serve`（默认 http://localhost:8088）。\n\n"
        "> 提示：用 `Cdocs serve -o --watch` 可自动打开浏览器并在文件改动时热重载。\n\n"
        "## 特性\n\n"
        "- 多语言：通过 `{{key}}` + i18n 字典实现\n"
        "- 全文搜索、明暗主题、阅读时长、面包屑、SEO 结构化数据\n"
        "- 单文件二进制，无运行时依赖\n";
    std::ofstream(dir / "md/docs/intro.en.md") <<
        "# Welcome to Cdocs\n\n"
        "Cdocs is a **C++** static documentation site generator. Write Markdown under `md/docs/`, "
        "and it builds a zero-dependency, offline-capable, bilingual static site.\n\n"
        "## Quick Start\n\n"
        "1. Write md: add `xxx.md` under `md/docs/` (English: `xxx.en.md`).\n"
        "2. Build: `Cdocs build` outputs to `dist/`.\n"
        "3. Preview: `Cdocs serve` (default http://localhost:8088).\n\n"
        "> Tip: `Cdocs serve -o --watch` opens your browser and hot-reloads on file changes.\n\n"
        "## Features\n\n"
        "- i18n via `{{key}}` + dictionaries\n"
        "- Full-text search, dark mode, reading time, breadcrumbs, SEO structured data\n"
        "- Single-file binary, no runtime deps\n";
    std::ofstream(dir / "md/docs/guide.md") <<
        "# 使用指南\n\n"
        "本章介绍 Cdocs 的常用工作流。\n\n"
        "## 项目结构\n\n"
        "```\n"
        "my-site/\n"
        "├── .Cdocs/\n"
        "│   ├── config/   config.json + sidebar/（分文件侧边栏）\n"
        "│   ├── i18n/     zh-CN.json / en.json\n"
        "│   └── theme/    主题（assets 前端资源 + templates/layout.html 页面骨架）\n"
        "├── md/\n"
        "│   ├── docs/     *.md 文档\n"
        "│   ├── blog/     博客\n"
        "│   └── static/   静态资源\n"
        "└── dist/         生成的静态站点\n"
        "```\n\n"
        "## 新建一篇文档\n\n"
        "运行 `Cdocs add my-page`，会在 `md/docs/my-page.md` 生成文件，并自动加入当前版本侧边栏。\n\n"
        "## 代码高亮\n\n"
        "```cpp\n"
        "#include <iostream>\n"
        "int main() { std::cout << \"Hello, Cdocs\\n\"; }\n"
        "```\n\n"
        "## 数学公式\n\n"
        "行内 $E=mc^2$，块级：\n\n"
        "$$\n"
        "\\int_0^1 x^2\\,dx = \\frac13\n"
        "$$\n";
    std::ofstream(dir / "md/docs/guide.en.md") <<
        "# Guide\n\n"
        "This chapter covers common Cdocs workflows.\n\n"
        "## Project Structure\n\n"
        "```\n"
        "my-site/\n"
        "├── .Cdocs/\n"
        "│   ├── config/   config.json + sidebar/ (per-area sidebars)\n"
        "│   ├── i18n/     zh-CN.json / en.json\n"
        "│   └── theme/    theme (assets + templates/layout.html)\n"
        "├── md/\n"
        "│   ├── docs/     *.md documents\n"
        "│   ├── blog/     blog posts\n"
        "│   └── static/   static assets\n"
        "└── dist/         generated site\n"
        "```\n\n"
        "## Add a page\n\n"
        "Run `Cdocs add my-page` to create `md/docs/my-page.md` and register it in `route.json`.\n\n"
        "## Code highlighting\n\n"
        "```cpp\n"
        "#include <iostream>\n"
        "int main() { std::cout << \"Hello, Cdocs\\n\"; }\n"
        "```\n\n"
        "## Math\n\n"
        "Inline $E=mc^2$, block:\n\n"
        "$$\n"
        "\\int_0^1 x^2\\,dx = \\frac13\n"
        "$$\n";
    }   // end needDocs

    if (withVersion) {
        // 历史版本快照：md-v1/ 示例（结构同 md/：docs/ 子目录；sidebar v1.json 引用 docs/old-intro）
        fs::create_directories(dir / "md-v1", ec);
        fs::create_directories(dir / "md-v1" / "docs", ec);
        std::ofstream(dir / "md-v1/docs/old-intro.md") <<
            "---\ntitle: v1 快照示例\n---\n\n# v1 版本\n\n这是历史版本（md-v1/）的快照示例。用 `cp -r md md-v1` 可锁定当前版本。\n";
    }

    if (needBlog) {
        // 博客示例（sidebar/blog.json 引用 blog/hello-cdocs）
        fs::create_directories(dir / "md/blog", ec);
        std::ofstream(dir / "md/blog/hello-cdocs.md") <<
            "---\ntitle: 你好，Cdocs\ndate: 2026-01-01\ntags: [cdocs]\n---\n\n# 你好，Cdocs\n\n这是第一篇博客示例。写博客请放在 `md/blog/` 下。\n";
        std::ofstream(dir / "md/blog/hello-cdocs.en.md") <<
            "---\ntitle: Hello, Cdocs\ndate: 2026-01-01\ntags: [cdocs]\n---\n\n# Hello, Cdocs\n\nThis is the first sample post. Write blog posts under `md/blog/`.\n";
    }

    // 5) 页面原型（archetype）：add 命令会读取并把 {{title}}/{{date}}/{{slug}} 替换
    //    原型自带 front matter 示例，使 add 生成的文档天生可用元数据（title/date/tags）
    std::ofstream(dir / "archetypes/default.md") <<
        "---\n"
        "title: \"{{title}}\"\n"
        "date: {{date}}\n"
        "tags: []\n"
        "---\n\n"
        "# {{title}}\n\n"
        "在这里开始写作…\n";

    // 6) 预览启动器
    std::ofstream(dir / "serve.bat") <<
        "@echo off\n"
        "chcp 65001 >nul 2>&1\n"
        "cd /d \"%~dp0\"\n"
        "echo 正在启动 Cdocs 预览服务器... （按 Ctrl+C 停止）\n"
        "Cdocs.exe serve %*\n"
        "if errorlevel 1 pause\n";

    // 7) 自动构建：建完框架立即生成 dist/，让 style.css / app.js 等资源直接就位、开箱即看
    bool built = false;
    if (haveEngine) {
        fs::path saved = fs::current_path(ec);
        fs::current_path(dir, ec);
        if (!ec) {
            int rc = run_build(fs::path("md"), fs::path("dist"), false, false);
            built = (rc == 0);
            fs::current_path(saved, ec);
        }
    }

    std::cout << color::green("已创建新站点: ") << dir << "\n";
    if (copyExe && haveEngine)
        std::cout << color::green("已复制运行资源(.Cdocs)与 Cdocs.exe。\n");
    else if (haveEngine)
        std::cout << color::green("已复制运行资源(.Cdocs)。\n");
    else
        std::cout << color::warn("注意: ") << "未找到引擎资源(.Cdocs)，仅生成内容骨架，未构建。\n";
    if (built)
        std::cout << color::green("✔ 已自动构建 → dist/（含 style.css / app.js 等前端资源，可直接 serve 预览）。\n");
    std::cout << color::muted("下一步:") << "\n  cd " << dir.string()
              << "\n  " << color::cyan("Cdocs serve")     << color::muted("    # 本地预览（可加 -o 自动开浏览器、--watch 热重载）")
              << "\n  " << color::cyan("Cdocs add <名>")   << color::muted("   # 新建一篇文档并加入导航")
              << "\n  " << color::cyan("Cdocs section <名>") << color::muted(" # 添加内容区（blog / docs / md-v<版本>）") << "\n";
    return 0;
}

// section：添加一个内容区文件夹（分类）。合法名字：blog / docs / md-v<数字>
//   - blog  只能存在一份（已存在 → 拒绝）；创建 md/blog/
//   - md  只能存在一份（已存在 → 拒绝）；创建 md/ + sidebar/md.json 示例
//   - md-v<数字>  版本目录（如 md-v1、md-v2），可多个；创建目录 + sidebar/<name>.json + 映射
//   所有情况都同步 config.json 的 site.sidebar 映射与 head.nav。
int cmd_section(const std::string& name) {
    // ---- 1) 名字校验：必须是 blog / docs / md-v<数字> ----
    std::string n = name;
    for (auto& c : n) c = (char)::tolower((unsigned char)c);
    bool isBlog = (n == "blog");
    bool isDocs = (n == "docs");
    bool isVersion = false;
    if (n.rfind("md-v", 0) == 0 && n.size() > 6) {
        std::string v = n.substr(6);
        bool digits = !v.empty() && std::all_of(v.begin(), v.end(), ::isdigit);
        if (digits) isVersion = true;
    }
    if (!isBlog && !isDocs && !isVersion) {
        std::cerr << color::error("无效的内容区名 '") << name
                  << color::error("'：仅支持 blog / docs / md-v<数字>（如 md-v1、md-v2）\n");
        return 2;
    }

    std::error_code ec;
    // ---- 2) 唯一性检查（blog/docs 只能一份，版本可多个但目录不可重复） ----
    // 结构约定：md/ 下三个内容区 = docs/（文档）+ blog/（博客）+ static/（静态资源）
    fs::path docsDir = fs::path("md/docs");
    fs::path blogDir = fs::path("md/blog");
    if (isBlog && fs::exists(blogDir, ec)) {
        std::cerr << color::error("博客区已存在（md/blog/）：blog 只能有一份，不能重复添加。\n");
        return 1;
    }
    if (isDocs && fs::exists(docsDir / "intro.md", ec)) {
        std::cerr << color::error("文档区已存在（md/docs/）：docs 只能有一份。\n");
        return 1;
    }
    if (isVersion && fs::exists(fs::path(n), ec)) {
        std::cerr << color::error("版本目录已存在: ") << n << "\n";
        return 1;
    }

    // ---- 3) 读取现有 config.json（保留其他配置） ----
    json cfg = json::object();
    fs::path cfgPath = ".Cdocs/config/config.json";
    if (fs::exists(cfgPath, ec)) {
        try { cfg = json::parse(read_file(cfgPath)); } catch (...) {}
    }
    if (!cfg.contains("site") || !cfg["site"].is_object()) cfg["site"] = json::object();
    auto& sbMap = cfg["site"]["sidebar"];

    if (isBlog) {
        fs::create_directories(blogDir, ec);
        std::ofstream(blogDir / "hello-cdocs.md") <<
            "---\ntitle: 你好，Cdocs\ndate: 2026-01-01\ntags: [cdocs]\n---\n\n# 你好，Cdocs\n\n博客区（md/blog/）第一篇。\n";
        std::ofstream(blogDir / "hello-cdocs.en.md") <<
            "---\ntitle: Hello, Cdocs\ndate: 2026-01-01\ntags: [cdocs]\n---\n\n# Hello, Cdocs\n\nBlog section (`md/blog/`).\n";
        fs::create_directories(".Cdocs/config/sidebar", ec);
        std::ofstream(".Cdocs/config/sidebar/blog.json")
            << "{\n  \"sidebar\": [\n    {\n      \"title\": \"博客\",\n      \"items\": [\n"
            << "        { \"title\": \"示例博文\", \"file\": \"blog/hello-cdocs\" }\n"
            << "      ]\n    }\n  ]\n}\n";
        sbMap["blog"] = "sidebar/blog.json";
        auto& nav = cfg["head"]["nav"];
        bool has = false;
        for (auto& x : nav) if (x.value("file", "") == "blog/index") has = true;
        if (!has) nav.push_back({{"title", "{{navBlog}}"}, {"file", "blog/index"}});
        std::cout << color::green("已创建博客区: ") << blogDir << "\n";
    } else if (isDocs) {
        fs::create_directories(docsDir, ec);
        std::ofstream(docsDir / "intro.md") << "# 欢迎\n\n在 `md/docs/` 下写 Markdown。\n";
        std::ofstream(docsDir / "guide.md") << "# 使用指南\n\n文档示例。\n";
        fs::create_directories(".Cdocs/config/sidebar", ec);
        std::ofstream(".Cdocs/config/sidebar/md.json")
            << "{\n  \"sidebar\": [\n    {\n      \"title\": \"{{navGettingStarted}}\",\n      \"items\": [\n"
            << "        { \"title\": \"{{navIntro}}\", \"file\": \"docs/intro\" },\n"
            << "        { \"title\": \"{{navGuide}}\", \"file\": \"docs/guide\" }\n"
            << "      ]\n    }\n  ]\n}\n";
        sbMap["md"] = "sidebar/md.json";
        auto& nav = cfg["head"]["nav"];
        bool has = false;
        for (auto& x : nav) if (x.value("file", "") == "docs/intro") has = true;
        if (!has) nav.push_back({{"title", "{{navDocs}}"}, {"file", "docs/intro"}});
        std::cout << color::green("已创建文档区: ") << docsDir << "\n";
    } else {   // isVersion
        fs::create_directories(fs::path(n), ec);
        fs::create_directories(fs::path(n) / "docs", ec);
        std::ofstream(fs::path(n) / "docs/index.md") <<
            "---\ntitle: " << n << " 快照\n---\n\n# " << n << "\n\n历史版本快照目录。\n";
        fs::create_directories(".Cdocs/config/sidebar", ec);
        std::ofstream(".Cdocs/config/sidebar/" + n + ".json")
            << "{\n  \"sidebar\": [\n    {\n      \"title\": \"" << n << "\",\n      \"items\": [\n"
            << "        { \"title\": \"快照首页\", \"file\": \"docs/index\" }\n"
            << "      ]\n    }\n  ]\n}\n";
        sbMap[n] = "sidebar/" + n + ".json";
        std::cout << color::green("已创建版本区: ") << n << "\n";
    }

    // ---- 4) 写回 config.json ----
    if (!cfg.empty()) {
        std::ofstream(cfgPath) << cfg.dump(2, ' ', false, json::error_handler_t::replace) << "\n";
    }
    std::cout << color::muted("已同步 config.json（site.sidebar 映射）。\n")
              << color::muted("构建预览: ") << color::cyan("Cdocs build && Cdocs serve") << "\n";
    return 0;
}

// add：新建一篇文档（对标 hugo new content/...），并自动登记到当前版本的侧边栏（sidebar/md.json）
static std::string current_date() {
    std::time_t now = std::time(nullptr);
    struct tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tmv);
    return std::string(buf);
}

int cmd_add(const std::string& name) {
    fs::path p(name);
    std::string stem = p.stem().string();
    if (stem.empty()) {
        std::cerr << color::error("无效的页面名: ") << name << "\n";
        return 1;
    }
    std::string title = pretty_title(stem);

    // 读取原型模板（若站点提供 archetypes/default.md），替换 {{title}}
    std::string body = "# " + title + "\n\n在这里开始写作…\n";
    fs::path arch = "archetypes/default.md";
    if (fs::exists(arch)) {
        std::string t = read_file(arch);
        auto sub = [&](const std::string& tok, const std::string& val) {
            size_t pos = 0;
            while ((pos = t.find(tok, pos)) != std::string::npos) {
                t.replace(pos, tok.size(), val);
                pos += val.size();
            }
        };
        sub("{{title}}", title);
        sub("{{date}}", current_date());
        sub("{{slug}}", stem);
        body = t;
    }

    fs::path doc = "md/docs/" + stem + ".md";
    fs::path docEn = "md/docs/" + stem + ".en.md";
    if (fs::exists(doc)) {
        std::cerr << color::error("文档已存在: ") << doc << "\n";
        return 1;
    }
    std::ofstream(doc)   << body;
    std::ofstream(docEn) << "# " << title << "\n\nStart writing…\n";

    // 登记到当前版本文档侧边栏 sidebar/md.json（追加到第一个分组，无分组则新建）
    // 旧站点的全局 route.json 兼容：仅当 sidebar/md.json 不存在时回退登记到 route.json
    fs::path sp = ".Cdocs/config/sidebar/md.json";
    fs::path rp = ".Cdocs/config/route.json";
    fs::path target = fs::exists(sp) ? sp : rp;
    json rj = json::object();
    if (fs::exists(target)) {
        try { rj = json::parse(read_file(target)); } catch (...) { rj = json::object(); }
    }
    if (!rj.contains("sidebar") || !rj["sidebar"].is_array()) rj["sidebar"] = json::array();
    auto& sb = rj["sidebar"];
    json item = json::object();
    item["title"] = title;
    item["file"]  = stem;
    if (!sb.empty() && sb[0].is_object() && sb[0].contains("items") && sb[0]["items"].is_array())
        sb[0]["items"].push_back(item);
    else {
        json grp = json::object();
        grp["title"] = "文档";
        grp["items"] = json::array();
        grp["items"].push_back(item);
        sb.push_back(grp);
    }
    std::ofstream(target) << rj.dump(2, ' ', false, json::error_handler_t::replace) << "\n";

    std::cout << color::green("已创建文档: ") << doc << color::muted(" (+ ") << docEn << ")\n"
              << color::green("已加入导航: ") << title
              << color::muted("（" + target.string() + "）\n")
              << color::muted("构建预览: ") << color::cyan("Cdocs serve --watch") << "\n";
    return 0;
}

// clean：清空输出目录（对标 jekyll clean / docusaurus clear）
int cmd_clean() {
    std::error_code ec;
    if (!fs::exists(g_dest, ec)) {
        std::cout << color::muted("无需清理：") << g_dest << color::muted(" 不存在。\n");
        return 0;
    }
    uintmax_t n = fs::remove_all(g_dest, ec);
    if (ec) {
        std::cerr << color::error("清理失败: ") << ec.message() << "\n";
        return 1;
    }
    std::cout << color::green("已清理 ") << g_dest << color::muted("（移除 ") << n
              << color::muted(" 项）\n");
    return 0;
}
