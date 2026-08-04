// component.cpp —— 组件系统与站点数据（地图驱动引擎核心）
// （自 builder.cpp 拆分：组件加载/数据孔填充/地图 sections 组合/站点 data 目录加载）
// 地图 = theme/map/<type>.json（JSON sections 数组，五种约定：html/component/if/each/sections）
// 组件 = 纯 HTML 片段 + 数据孔 {{field}} / {{slot}}——无控制流，条件/循环/嵌套由 JSON 地图表达。
// 数据作用域优先级：全局页面数据 < each 当前项 < props（最局部优先）。

#include "component.hpp"
#include <map>
#include <mutex>
#include <algorithm>

// 地图 JSON 缓存（前置声明；定义见 compose_page 前）
static json load_map_json(const fs::path& path);

// ---------------- 渲染部件（file-local） ----------------

// 社交分享 + 文章结构化元信息（Open Graph / Twitter Card / article:*)
// ogUrl 为空时跳过需绝对地址的 og:url / og:image（通常由 config.url 驱动）
fs::path theme_root() {
    std::error_code ec;
    // 多主题：config site.themeName 指定 → themes/<name>（引擎目录优先，其次项目根 themes/）
    if (!g_theme_name.empty()) {
        fs::path t1 = g_engine / "themes" / g_theme_name;
        if (fs::is_directory(t1, ec)) return t1;
        fs::path t2 = fs::current_path() / "themes" / g_theme_name;
        if (fs::is_directory(t2, ec)) return t2;
        if (comp_warned_once("theme:" + g_theme_name))
            std::cerr << color::warn("警告: ") << "主题 themes/" << g_theme_name
                      << " 不存在（回退 .Cdocs/theme）\n";
    }
    // 单主题兼容：.Cdocs/theme（默认，未配置 themeName 时）
    fs::path t = g_engine / "theme";
    if (fs::is_directory(t, ec)) return t;
    return g_engine;
}

const std::string& theme_color() {
    static std::string cached = "#a8332a";
    static bool loaded = false;
    if (!loaded) {
        fs::path tf = theme_root() / "theme.json";
        std::error_code ec;
        if (fs::is_regular_file(tf, ec)) {
            try {
                json j = json::parse(read_file(tf));
                if (j.contains("theme_color") && j["theme_color"].is_string())
                    cached = j["theme_color"].get<std::string>();
            } catch (...) {}
        }
        loaded = true;
    }
    return cached;
}

static std::set<std::string> g_comp_warned;   // 缺失/循环警告去重（每组件名一次）
static std::mutex g_comp_mtx;                  // g_comp_warned 并发保护（正文渲染多线程）
bool comp_warned_once(const std::string& k) {
    std::lock_guard<std::mutex> lk(g_comp_mtx);
    return g_comp_warned.insert(k).second;
}

static fs::path components_dir() { return theme_root() / "components"; }

// 在当前主题目录内查找并读取组件
static std::string find_component_in(const fs::path& dir, const std::string& name) {
    std::error_code ec;
    fs::path p = dir / (name + ".html");
    if (fs::is_regular_file(p, ec)) return read_file(p);
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

// ---- 组件预加载：构建启动时递归扫描 components/ 目录，预解析所有 HTML 为 segments ----
//   后续 fill_data_holes_segs 直接用 segments 填数据孔，省去每页文件搜索 + 字符串扫描。
struct Seg { bool isHole; std::string val; };  // false=literal HTML, true={{key}}
static std::map<std::string, std::vector<Seg>> g_comp_cache;
static std::mutex g_comp_cache_mtx;
static bool g_comp_preloaded = false;

static std::vector<Seg> parse_segments(const std::string& html) {
    std::vector<Seg> out;
    size_t i = 0, n = html.size();
    while (i < n) {
        if (html[i] == '{' && i + 1 < n && html[i + 1] == '{') {
            size_t e = html.find("}}", i + 2);
            if (e != std::string::npos) {
                std::string key = trim(html.substr(i + 2, e - i - 2));
                if (!key.empty()) { out.push_back({true, key}); i = e + 2; continue; }
            }
        }
        size_t next = html.find("{{", i);
        out.push_back({false, html.substr(i, next == std::string::npos ? std::string::npos : next - i)});
        i = (next == std::string::npos) ? n : next;
    }
    return out;
}

void preload_components() {
    if (g_comp_preloaded) return;
    std::lock_guard<std::mutex> lk(g_comp_cache_mtx);
    if (g_comp_preloaded) return;
    auto load_dir = [](const fs::path& dir) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) return;
        for (auto it = fs::recursive_directory_iterator(dir, ec), end = fs::recursive_directory_iterator();
             !ec && it != end; it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            if (!it->is_regular_file(ec) || it->path().extension() != ".html") continue;
            std::string name = it->path().stem().string();
            if (g_comp_cache.count(name)) continue;
            g_comp_cache[name] = parse_segments(read_file(it->path()));
        }
    };
    load_dir(components_dir());
    fs::path fallback = g_engine / "theme" / "components";
    if (theme_root() != fallback) load_dir(fallback);
    g_comp_preloaded = true;
}

std::string load_component(const std::string& name) {
    preload_components();
    {
        std::lock_guard<std::mutex> lk(g_comp_cache_mtx);
        auto it = g_comp_cache.find(name);
        if (it != g_comp_cache.end()) {
            // 现有调用方（shortcode.cpp）需要完整 HTML 字符串；从 segments 重构
            std::string out;
            for (const auto& s : it->second)
                out += s.isHole ? ("{{" + s.val + "}}") : s.val;
            return out;
        }
    }
    // 缓存未命中回退文件搜索（新组件/自定义主题）
    std::string body = find_component_in(components_dir(), name);
    if (!body.empty()) return body;
    fs::path fallback = g_engine / "theme" / "components";
    if (theme_root() != g_engine / "theme") {
        std::error_code fec;
        if (fs::is_directory(fallback, fec)) return find_component_in(fallback, name);
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
    if (v.is_array() || v.is_object()) return v.dump();   // 数组/对象 → JSON 文本（组件 script 内嵌数据用，如市场列表）
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

// 基于预解析 segments 的数据孔替换 — 追加到 out（避免中间字符串分配）
static void fill_data_holes_segs_to(const std::vector<Seg>& segs, const json& data, std::string& out) {
    for (const auto& s : segs) {
        if (!s.isHole) { out += s.val; continue; }
        const json* pv = json_get_path(data, s.val);
        if (pv) out += json_scalar(*pv); else { out += "{{"; out += s.val; out += "}}"; }
    }
}

static void fill_data_holes_to(const std::string& html, const json& data, std::string& out) {
    size_t i = 0, n = html.size();
    while (i < n) {
        if (html[i] == '{' && i + 1 < n && html[i + 1] == '{') {
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
}

// 前向声明
static void compose_sections_to(const json& sections, const json& data,
                                int depth, std::vector<std::string>& stack, std::string& out);

// 渲染单个组件实例 → 追加到 out
static void render_map_component_to(const std::string& name, const json& data,
                                     int depth, std::vector<std::string>& stack,
                                     const json* childSections, std::string& out) {
    if (depth > 32) {
        if (comp_warned_once("depth:" + name))
            std::cerr << color::warn("警告: ") << "组件嵌套过深（>32 层）: " << name << "\n";
        return;
    }
    if (std::find(stack.begin(), stack.end(), name) != stack.end()) {
        if (comp_warned_once("cycle:" + name))
            std::cerr << color::warn("警告: ") << "组件循环引用: " << name << "（该挂载点已移除）\n";
        return;
    }
    preload_components();
    std::vector<Seg> segs;
    {
        std::lock_guard<std::mutex> lk(g_comp_cache_mtx);
        auto it = g_comp_cache.find(name);
        if (it != g_comp_cache.end()) segs = it->second;
    }
    if (segs.empty()) {
        std::string body = load_component(name);
        if (body.empty()) {
            if (comp_warned_once("missing:" + name))
                std::cerr << color::warn("警告: ") << "组件文件不存在: components/" << name << ".html\n";
            return;
        }
        segs = parse_segments(body);
        { std::lock_guard<std::mutex> lk(g_comp_cache_mtx); g_comp_cache[name] = segs; }
    }
    stack.push_back(name);
    json ctx = data;
    if (childSections) {
        std::string slotBuf;
        compose_sections_to(*childSections, data, depth + 1, stack, slotBuf);
        ctx["slot"] = std::move(slotBuf);
    } else {
        ctx["slot"] = std::string();
    }
    stack.pop_back();
    fill_data_holes_segs_to(segs, ctx, out);
}

// 遍历 JSON sections 数组 → 追加到 out
static void compose_sections_to(const json& sections, const json& data,
                                int depth, std::vector<std::string>& stack, std::string& out) {
    if (!sections.is_array()) return;
    for (const auto& sec : sections) {
        if (!sec.is_object()) continue;
        if (sec.contains("html") && sec["html"].is_string()) {
            out += sec["html"].get<std::string>();
            continue;
        }
        if (!sec.contains("component") || !sec["component"].is_string()) continue;
        std::string name = sec["component"].get<std::string>();
        if (sec.contains("if") && sec["if"].is_string()) {
            const json* cv = json_get_path(data, sec["if"].get<std::string>());
            if (!cv || !tpl_truthy(*cv)) continue;
        }
        const json* child = (sec.contains("sections") && sec["sections"].is_array()) ? &sec["sections"] : nullptr;
        json propsObj;
        if (sec.contains("props") && sec["props"].is_object()) {
            for (auto it = sec["props"].begin(); it != sec["props"].end(); ++it) {
                if (it.value().is_string()) {
                    std::string resolved;
                    fill_data_holes_to(it.value().get<std::string>(), data, resolved);
                    propsObj[it.key()] = resolved;
                } else propsObj[it.key()] = it.value();
            }
        }
        auto applyProps = [&](json& ctx) {
            for (auto it = propsObj.begin(); it != propsObj.end(); ++it)
                ctx[it.key()] = it.value();
        };
        if (sec.contains("each") && sec["each"].is_string()) {
            const json* av = json_get_path(data, sec["each"].get<std::string>());
            if (av && av->is_array()) {
                for (const auto& item : *av) {
                    json ctx = data;
                    if (item.is_object())
                        for (auto it = item.begin(); it != item.end(); ++it)
                            ctx[it.key()] = it.value();
                    applyProps(ctx);
                    render_map_component_to(name, ctx, depth, stack, child, out);
                }
            }
            continue;
        }
        json ctx = data;
        applyProps(ctx);
        render_map_component_to(name, ctx, depth, stack, child, out);
    }
}

// ---- 兼容旧接口（fill_data_holes / render_map_component / compose_sections 仍然可用）----
static std::string fill_data_holes_segs(const std::vector<Seg>& segs, const json& data) {
    std::string out; out.reserve(256); fill_data_holes_segs_to(segs, data, out); return out;
}
std::string fill_data_holes(const std::string& html, const json& data) {
    std::string out; out.reserve(html.size() + 128); fill_data_holes_to(html, data, out); return out;
}
std::string render_map_component(const std::string& name, const json& data,
                                  int depth, std::vector<std::string>& stack,
                                  const json* childSections) {
    std::string out; render_map_component_to(name, data, depth, stack, childSections, out); return out;
}
std::string compose_sections(const json& sections, const json& data,
                             int depth, std::vector<std::string>& stack) {
    std::string out; compose_sections_to(sections, data, depth, stack, out); return out;
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
    json parent = load_map_json(pp);
    if (parent.is_null()) {
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

// 地图 JSON 读盘缓存（构建间不刷新，每页重复读取相同文件，并发安全）
static std::mutex g_map_cache_mtx;
static std::map<std::string, json> g_map_cache;   // 绝对路径 → 已解析 JSON
static json load_map_json(const fs::path& path) {
    std::string key = fs::absolute(path).string();
    {
        std::lock_guard<std::mutex> lk(g_map_cache_mtx);
        auto it = g_map_cache.find(key);
        if (it != g_map_cache.end()) return it->second;
    }
    json j;
    try { j = json::parse(read_file(path)); } catch (...) { return json(); }
    {
        std::lock_guard<std::mutex> lk(g_map_cache_mtx);
        g_map_cache[key] = j;
    }
    return j;
}

// ============ 地图预编译：JSON sections → FlatCmd 指令列表（消除每页 JSON 遍历 + 字符串扫描） ============
enum class FCmd { Raw, Hole, Comp };  // Raw=字面量, Hole={{key}}, Comp=渲染组件

struct FlatCmd {
    FCmd kind;
    std::string val;           // Raw: 字面文本; Hole: 键路径; Comp: 组件名
    std::string eachPath;      // Comp: each 数据路径（非空=循环渲染）
    std::string ifPath;        // Comp: if 数据路径（非空=条件渲染）
    json props;                // Comp: 静态 props
    std::vector<FlatCmd> children; // Comp: 子 sections（编译后的 slot 模板）
};

// 缓存：地图类型名 → 编译后指令列表
static std::map<std::string, std::vector<FlatCmd>> g_compiled_maps;
static std::mutex g_compiled_mtx;

// 递归编译 JSON sections → FlatCmd 列表
static std::vector<FlatCmd> compile_sections(const json& sections) {
    std::vector<FlatCmd> cmds;
    if (!sections.is_array()) return cmds;
    for (const auto& sec : sections) {
        if (!sec.is_object()) continue;
        // html: "..." → 预解析为 Raw + Hole 序列
        if (sec.contains("html") && sec["html"].is_string()) {
            for (const auto& s : parse_segments(sec["html"].get<std::string>())) {
                cmds.push_back({s.isHole ? FCmd::Hole : FCmd::Raw, s.val});
            }
            continue;
        }
        if (!sec.contains("component") || !sec["component"].is_string()) continue;
        FlatCmd cmd{FCmd::Comp, sec["component"].get<std::string>()};
        if (sec.contains("each") && sec["each"].is_string())
            cmd.eachPath = sec["each"].get<std::string>();
        if (sec.contains("if") && sec["if"].is_string())
            cmd.ifPath = sec["if"].get<std::string>();
        if (sec.contains("props") && sec["props"].is_object())
            cmd.props = sec["props"];
        if (sec.contains("sections") && sec["sections"].is_array())
            cmd.children = compile_sections(sec["sections"]);
        cmds.push_back(std::move(cmd));
    }
    return cmds;
}

// 获取或编译地图的指令列表
static const std::vector<FlatCmd>& get_compiled(const std::string& mapName, const json& sections) {
    {
        std::lock_guard<std::mutex> lk(g_compiled_mtx);
        auto it = g_compiled_maps.find(mapName);
        if (it != g_compiled_maps.end()) return it->second;
    }
    auto cmds = compile_sections(sections);
    std::lock_guard<std::mutex> lk(g_compiled_mtx);
    return g_compiled_maps.emplace(mapName, std::move(cmds)).first->second;
}

// 渲染编译后的指令列表 → out（替代 compose_sections_to + top fill_data_holes）
static void render_compiled(const std::vector<FlatCmd>& cmds, const json& data, std::string& out) {
    for (const auto& c : cmds) {
        switch (c.kind) {
        case FCmd::Raw: out += c.val; break;
        case FCmd::Hole: {
            const json* pv = json_get_path(data, c.val);
            if (pv) out += json_scalar(*pv); else { out += "{{"; out += c.val; out += "}}"; }
            break;
        }
        case FCmd::Comp: {
            if (!c.ifPath.empty()) {
                const json* cv = json_get_path(data, c.ifPath);
                if (!cv || !tpl_truthy(*cv)) break;
            }
            preload_components();
            const std::vector<Seg>* segs = nullptr;
            {
                std::lock_guard<std::mutex> lk(g_comp_cache_mtx);
                auto it = g_comp_cache.find(c.val);
                if (it != g_comp_cache.end()) segs = &it->second;
            }
            if (!segs) break;  // 组件未找到
            auto render_one = [&](const json& ctx) {
                json rctx = ctx;
                // props
                if (!c.props.is_null()) {
                    for (auto it = c.props.begin(); it != c.props.end(); ++it) {
                        if (it.value().is_string()) {
                            std::string rs; fill_data_holes_to(it.value().get<std::string>(), data, rs);
                            rctx[it.key()] = rs;
                        } else rctx[it.key()] = it.value();
                    }
                }
                // slot
                if (!c.children.empty()) {
                    std::string slot; render_compiled(c.children, rctx, slot);
                    rctx["slot"] = std::move(slot);
                } else rctx["slot"] = std::string();
                fill_data_holes_segs_to(*segs, rctx, out);
            };
            if (!c.eachPath.empty()) {
                const json* av = json_get_path(data, c.eachPath);
                if (av && av->is_array()) {
                    for (const auto& item : *av) {
                        json ictx = data;
                        if (item.is_object())
                            for (auto it = item.begin(); it != item.end(); ++it) ictx[it.key()] = it.value();
                        render_one(ictx);
                    }
                }
            } else {
                render_one(data);
            }
            break;
        }
        }
    }
}

// 地图主入口：读 config/map.json 注册表 → theme/map/<name>.json（递归解析 extends 继承）→ 预编译 → 渲染
std::string compose_page(const std::string& mapName, const json& data) {
    // config/map.json：maps（页面类型注册数组）+ templates（父级地图注册）。地图不硬编码进 C++。
    // maps 数组项：{type, map, mode, output?, home?}；兼容旧格式（maps 为对象：类型名 → 地图路径）。
    std::string mapPath;
    json templates = json::object();
    fs::path cfgPath = g_engine / "config" / "map.json";
    std::error_code cec;
    if (fs::is_regular_file(cfgPath, cec)) {
        json j = load_map_json(cfgPath);
        if (!j.is_null()) {
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
        }
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
    json map = load_map_json(mp);
    if (map.is_null()) {
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
    // 预编译路径：FlatCmd 指令列表缓存 → 渲染时直写 buffer，无 JSON 遍历、无顶层字符串扫描
    const auto& cmds = get_compiled(mapName, sections);
    std::string out;
    out.reserve(32768);
    render_compiled(cmds, mapData, out);
    return out;
}

static json g_site_data;
static bool g_site_data_loaded = false;
static std::mutex g_site_data_mtx;   // site_data 并发保护（正文渲染多线程首次加载竞争）
const json& site_data() {
    if (g_site_data_loaded) return g_site_data;
    std::lock_guard<std::mutex> lk(g_site_data_mtx);
    if (g_site_data_loaded) return g_site_data;   // 双检锁
    std::error_code ec;
    fs::path d = g_engine / "data";
    if (fs::is_directory(d, ec)) {
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
    }
    g_site_data_loaded = true;   // 注意：必须等加载完成后才置位，否则并发首访的线程会拿到半初始化的 json
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
