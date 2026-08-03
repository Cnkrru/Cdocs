// component.cpp —— 组件系统与站点数据（地图驱动引擎核心）
// （自 builder.cpp 拆分：组件加载/数据孔填充/地图 sections 组合/站点 data 目录加载）
// 地图 = theme/map/<type>.json（JSON sections 数组，五种约定：html/component/if/each/sections）
// 组件 = 纯 HTML 片段 + 数据孔 {{field}} / {{slot}}——无控制流，条件/循环/嵌套由 JSON 地图表达。
// 数据作用域优先级：全局页面数据 < each 当前项 < props（最局部优先）。

#include "component.hpp"
#include <mutex>
#include <algorithm>

// ---------------- 渲染部件（file-local） ----------------

// 社交分享 + 文章结构化元信息（Open Graph / Twitter Card / article:*)
// ogUrl 为空时跳过需绝对地址的 og:url / og:image（通常由 config.url 驱动）
fs::path theme_root() {
    std::error_code ec;
    fs::path t = g_engine / "theme";
    if (fs::is_directory(t, ec)) return t;
    return g_engine;
}
static std::set<std::string> g_comp_warned;   // 缺失/循环警告去重（每组件名一次）
static std::mutex g_comp_mtx;                  // g_comp_warned 并发保护（正文渲染多线程）
bool comp_warned_once(const std::string& k) {
    std::lock_guard<std::mutex> lk(g_comp_mtx);
    return g_comp_warned.insert(k).second;
}

static fs::path components_dir() { return theme_root() / "components"; }

std::string load_component(const std::string& name) {
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

std::string compose_sections(const json& sections, const json& data,
                                    int depth, std::vector<std::string>& stack);

// 数据孔替换（纯文本）：{{a.b.c}} → data 路径取值；缺失原样保留（L2 兜底）
std::string fill_data_holes(const std::string& html, const json& data) {
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
std::string render_map_component(const std::string& name, const json& data,
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
std::string compose_sections(const json& sections, const json& data,
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
std::string compose_page(const std::string& mapName, const json& data) {
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

static json g_site_data;
static bool g_site_data_loaded = false;
static std::mutex g_site_data_mtx;   // site_data 并发保护（正文渲染多线程首次加载竞争）
const json& site_data() {
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
