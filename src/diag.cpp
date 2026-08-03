// diag.cpp —— 诊断命令：doctor / check / config / routes
// 目标：把构建期已有的检查能力（死链/配置/路由）暴露成独立 CLI 命令，
// 对标 Hugo doctor、Docusaurus build --debug 的诊断输出。

#include "diag.hpp"
#include "linkcheck.hpp"
#include "component.hpp"   // theme_root
#include <filesystem>
#include <fstream>
#include <set>

namespace fs = std::filesystem;

// ---------- 工具探测（PATH 扫描，跨平台） ----------
static bool tool_exists(const std::string& name) {
    const char* env = std::getenv("PATH");
    if (!env) return false;
    std::string path = env;
    std::string sep = path.find(';') != std::string::npos ? ";" : ":";
    size_t start = 0;
    std::vector<std::string> exts = { "", ".exe", ".cmd" };
    while (start <= path.size()) {
        size_t end = path.find(sep, start);
        std::string dir = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        try {   // PATH 段可能含非 UTF-8（中文系统 GBK），filesystem 转换会抛异常
            for (const auto& ext : exts) {
                fs::path p = fs::path(dir) / (name + ext);
                std::error_code ec;
                if (fs::is_regular_file(p, ec)) return true;
            }
        } catch (...) {}
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return false;
}

// ---------- 小工具 ----------
static json load_json(const fs::path& p) {
    std::error_code ec;
    if (!fs::is_regular_file(p, ec)) return json();
    try { return json::parse(read_file(p)); } catch (...) { return json(); }
}

static std::string join(const std::vector<std::string>& v, const std::string& sep) {
    std::string out;
    for (const auto& s : v) { if (!out.empty()) out += sep; out += s; }
    return out;
}

static void print_row(const std::string& label, const std::string& status, bool ok) {
    std::cout << "  " << (ok ? color::green("[OK] ") : color::warn("[警告] "))
              << color::muted(label + ": ") << status << "\n";
}

// ============================================================
// doctor：环境与配置自检
// ============================================================
int cmd_doctor() {
    using namespace color;
    int errors = 0;
    std::cout << cyan("Cdocs ") << green(CDOCS_VERSION) << muted(" 环境自检\n\n");

    // 1) 引擎/配置根目录
    std::error_code ec;
    bool engineOk = fs::is_directory(g_engine, ec);
    print_row("引擎目录", engineOk ? g_engine.string() : (g_engine.string() + "（不存在）"), engineOk);
    if (!engineOk) return 1;

    // 2) 核心配置文件
    auto check_cfg = [&](const char* name, const char* rel) {
        fs::path p = g_engine / rel;
        bool ok = fs::is_regular_file(p, ec);
        if (!ok) ++errors;
        print_row(name, ok ? p.string() : (rel + std::string("（缺失）")), ok);
    };
    check_cfg("config.json", "config/config.json");
    check_cfg("map.json", "config/map.json");
    {
        bool themeOk = fs::is_directory(g_engine / "theme", ec);
        if (!themeOk) ++errors;
        print_row("主题目录", themeOk ? "theme/" : "缺失 theme/", themeOk);
    }
    fs::path themeDir = g_engine / "theme";
    if (fs::is_directory(themeDir, ec)) {
        bool hasComponents = fs::is_directory(themeDir / "components", ec);
        if (!hasComponents) ++errors;
        print_row("组件目录", hasComponents ? "theme/components" : "缺失 theme/components", hasComponents);
        fs::path themeJson = themeDir / "theme.json";
        bool hasTheme = fs::is_regular_file(themeJson, ec);
        print_row("主题配置", hasTheme ? "theme.json" : "无 theme.json（使用默认）", true);
    }

    // 3) 内容区
    fs::path mdDir = g_source;
    bool mdOk = fs::is_directory(mdDir, ec);
    print_row("内容源", mdOk ? g_source.string() : (g_source.string() + "（缺失）"), mdOk);
    if (mdOk) {
        std::vector<std::string> areas;
        for (const auto& it : fs::directory_iterator(mdDir, ec)) {
            if (it.is_directory(ec) && it.path().filename() != ".Cdocs")
                areas.push_back(it.path().filename().string());
        }
        if (areas.empty()) {
            ++errors;
            print_row("内容区", "无内容目录（md/docs、md/blog…）", false);
        } else {
            std::string list;
            for (const auto& a : areas) { if (!list.empty()) list += " / "; list += "md/" + a; }
            print_row("内容区", list, true);
        }
    }

    // 4) sidebar 登记
    fs::path sbDir = g_engine / "config" / "sidebar";
    int sbCount = 0;
    if (fs::is_directory(sbDir, ec)) {
        for (const auto& it : fs::directory_iterator(sbDir, ec))
            if (it.is_regular_file(ec) && it.path().extension() == ".json") ++sbCount;
    }
    print_row("侧边栏登记", sbCount > 0 ? (std::to_string(sbCount) + " 个 sidebar 文件") : "无 sidebar 文件", sbCount > 0);

    // 5) 输出目录
    bool distOk = fs::is_directory(g_dest, ec);
    print_row("输出目录", distOk ? g_dest.string() : (g_dest.string() + "（未构建，先运行 build）"), true);

    // 6) 外部工具
    struct { const char* n; bool need; } tools[] = {
        { "git", false }, { "python", false }, { "node", false }, { "curl", false },
    };
    bool anyToolMissing = false;
    for (const auto& t : tools) {
        bool ok = tool_exists(t.n);
        if (!ok) anyToolMissing = true;
        print_row(std::string("工具 ") + t.n, ok ? "可用" : "未找到（可选，视场景需要）", true);
    }

    std::cout << "\n";
    if (errors > 0) {
        std::cerr << error("发现 ") << errors << error(" 个问题\n");
        return 1;
    }
    std::cout << green("自检通过（工具缺失仅为提示，不影响构建）\n");
    return 0;
}

// ============================================================
// check：站点质量检查（死链 + token 残留 + 未渲染数据孔）
// ============================================================
int cmd_check() {
    using namespace color;
    std::error_code ec;
    if (!fs::is_directory(g_dest, ec)) {
        std::cerr << error("输出目录不存在: ") << g_dest
                  << error("（先运行 ") << cyan("Cdocs build") << error("）\n");
        return 1;
    }
    std::cout << cyan("检查输出目录 ") << g_dest << "\n\n";

    // 剥离保护块（script/style/pre/code）：i18n 字典的 {{minutes}} 是 JS 运行时占位符、
    // 代码示例里的 {{}} 与占位 token 字样是文档内容，均非引擎未渲染标记。
    const char* protTags[] = { "script", "style", "pre", "code" };
    auto strip_blocks = [&](std::string html) {
        for (const char* tg : protTags) {
            std::string openTag = std::string("<") + tg;
            std::string closeTag = std::string("</") + tg + ">";
            for (;;) {
                size_t o = html.find(openTag);
                if (o == std::string::npos) break;
                size_t ge = html.find('>', o);
                if (ge == std::string::npos) break;
                size_t c = html.find(closeTag, ge);
                if (c == std::string::npos) break;
                for (size_t k = o; k <= c + closeTag.size(); ++k)
                    if (k < html.size()) html[k] = ' ';
            }
        }
        return html;
    };

    // 1) 死链检查（每语言目录）
    int locales = 0, broken = 0;
    for (const auto& it : fs::directory_iterator(g_dest, ec)) {
        if (!it.is_directory(ec)) continue;
        std::string loc = it.path().filename().string();
        if (loc == "assets" || loc == "deps" || loc == "icons") continue;
        check_links(it.path(), loc);
        ++locales;
    }
    broken = (int)g_link_broken.size();
    if (broken > 0) {
        for (const auto& b : g_link_broken) std::cerr << warn("  死链: ") << b << "\n";
    }
    print_row("死链检查", std::to_string(locales) + " 个语言目录，" + std::to_string(broken) + " 条死链", broken == 0);

    // 2) token / 数据孔残留扫描（跳过 script/style/pre/code 保护块：教学文档会展示占位 token 字样）
    int leftovers = 0;
    std::vector<std::string> samples;
    for (const auto& it : fs::recursive_directory_iterator(g_dest, ec)) {
        if (!it.is_regular_file(ec) || it.path().extension() != ".html") continue;
        std::string html = strip_blocks(read_file(it.path()));
        if (html.find("@@CDOCS_SC_") != std::string::npos) {
            ++leftovers; if (samples.size() < 5) samples.push_back(it.path().string());
        }
    }
    if (leftovers > 0)
        for (const auto& s : samples) std::cerr << warn("  token 残留: ") << s << "\n";
    print_row("组件 token 残留", leftovers == 0 ? "无" : (std::to_string(leftovers) + " 个文件"), leftovers == 0);

    // 3) 未渲染数据孔（{{xxx}} 仍以 {{ 形式出现在页面正文）
    int holes = 0;
    std::vector<std::string> holeSamples;
    for (const auto& it : fs::recursive_directory_iterator(g_dest, ec)) {
        if (!it.is_regular_file(ec) || it.path().extension() != ".html") continue;
        std::string html = strip_blocks(read_file(it.path()));
        size_t h = 0;
        while ((h = html.find("{{", h)) != std::string::npos) {
            ++holes; h += 2;
            if (holeSamples.size() < 5) holeSamples.push_back(it.path().string() + " @" + std::to_string(h));
            if (holes > 50) break;
        }
        if (holes > 50) break;
    }
    bool ok = (broken == 0 && leftovers == 0 && holes == 0);
    std::cout << "\n" << (ok ? green("检查通过\n") : warn("存在上述问题（建议排查）\n"));
    return ok ? 0 : 1;
}

// ============================================================
// config：打印解析后的配置摘要
// ============================================================
int cmd_config() {
    using namespace color;
    json cfg = load_json(g_engine / "config" / "config.json");
    json map = load_json(g_engine / "config" / "map.json");
    json route = load_json(g_engine / "config" / "route.json");
    if (cfg.empty() && route.empty()) {
        std::cerr << error("未找到 config.json / route.json\n");
        return 1;
    }
    std::cout << cyan("站点配置摘要") << "\n";
    if (cfg.contains("site") && cfg["site"].is_object()) {
        const json& s = cfg["site"];
        auto show = [&](const char* k) {
            if (s.contains(k)) std::cout << "  " << muted(k) << " = " << s[k].dump() << "\n";
        };
        show("title"); show("langs"); show("url"); show("version");
        show("logo_text"); show("copyright");
        if (s.contains("sidebar") && s["sidebar"].is_object())
            std::cout << "  " << muted("sidebar") << " = " << s["sidebar"].dump() << "\n";
    }
    std::cout << "  " << muted("theme") << " = " << (theme_root() / "theme.json").string() << "\n";
    if (map.contains("maps")) {
        std::cout << "  " << muted("maps") << " = ";
        if (map["maps"].is_array()) {
            std::vector<std::string> t;
            for (const auto& m : map["maps"])
                if (m.is_object()) t.push_back(m.value("type", "?"));
            std::cout << "[ " << (t.empty() ? std::string("无") : join(t, " / ")) << " ]\n";
        } else std::cout << map["maps"].dump() << "\n";
    }
    fs::path sb = g_engine / "config" / "sidebar";
    int n = 0;
    if (fs::is_directory(sb)) {
        std::error_code ec;
        for (const auto& it : fs::directory_iterator(sb, ec))
            if (it.is_regular_file(ec) && it.path().extension() == ".json") ++n;
    }
    std::cout << "  " << muted("sidebar 文件") << " = " << n << " 个\n";
    return 0;
}

// ============================================================
// routes：列出站点页面路由
// ============================================================
int cmd_routes() {
    using namespace color;
    json cfg = load_json(g_engine / "config" / "config.json");
    std::string lang = "zh-CN";
    if (cfg.contains("site") && cfg["site"].is_object() && cfg["site"].contains("langs")
        && cfg["site"]["langs"].is_array() && !cfg["site"]["langs"].empty())
        lang = cfg["site"]["langs"][0].get<std::string>();

    std::vector<std::string> files;
    std::set<std::string> seen;
    fs::path sb = g_engine / "config" / "sidebar";
    std::error_code ec;
    if (fs::is_directory(sb, ec)) {
        for (const auto& it : fs::directory_iterator(sb, ec)) {
            if (!it.is_regular_file(ec) || it.path().extension() != ".json") continue;
            json j = load_json(it.path());
            std::function<void(const json&)> walk = [&](const json& node) {
                if (!node.is_object()) return;
                if (node.contains("file") && node["file"].is_string()) {
                    std::string f = node["file"].get<std::string>();
                    if (seen.insert(f).second) files.push_back(f);
                }
                if (node.contains("items") && node["items"].is_array())
                    for (const auto& itm : node["items"]) walk(itm);
            };
            for (const auto& grp : j.value("sidebar", json::array())) walk(grp);
        }
    }
    if (files.empty()) {
        std::cerr << error("sidebar 中未登记任何页面\n");
        return 1;
    }
    std::cout << cyan("页面路由清单") << muted("（") << lang << muted("，")
              << files.size() << muted(" 篇）\n");
    for (const auto& f : files) {
        // file "docs/x" → zh-CN/docs/x.html
        std::string url = "/" + lang + "/" + f + ".html";
        std::string src = (g_source / f).string() + ".md";
        std::cout << "  " << green(url) << "  " << muted("← ") << src << "\n";
    }
    return 0;
}
