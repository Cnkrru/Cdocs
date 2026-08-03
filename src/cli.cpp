// cli.cpp —— 命令分发（表驱动）+ 统一 flag 解析
// v2：命令注册表 {name, 别名, usage, 摘要, 处理器}，新增 doctor/check/config/routes 诊断命令

#include "cli.hpp"
#include "builder.hpp"
#include "server.hpp"
#include "deploy.hpp"
#include "scaffold.hpp"
#include "diag.hpp"
#include <map>

// ---------- 无值（布尔）flag 白名单 ----------
static bool is_bool_flag(const std::string& f) {
    static const std::set<std::string> bools = {
        "-D", "--drafts", "--clean", "-q", "--quiet", "-V", "--verbose",
        "-o", "--open", "-w", "--watch", "--no-build",
        "-f", "--force", "--vercel", "--setup",
        "-y", "--defaults", "--no-engine",
        "-h", "--help", "-v", "--version",
    };
    return bools.count(f) > 0;
}

// ---------- 统一 flag 解析 ----------
// 规则：--flag value（下一参数非 '-' 开头则视为值）；布尔 flag 白名单内始终无值；
// "--" 之后全部视为位置参数。未知 flag 不在此层报错（由命令决定是否检查）。
struct ParsedArgs {
    std::map<std::string, std::string> flags;  // "--flag" → 值（布尔 flag 为空串）
    std::vector<std::string> pos;              // 裸参数
    size_t stopIndex = 0;                      // stopAtFirstPos 时停止处的索引
};

static ParsedArgs parse_flags(const std::vector<std::string>& args, size_t from,
                              bool stopAtFirstPos = false) {
    ParsedArgs r;
    bool posOnly = false;
    for (size_t i = from; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (posOnly || a.empty() || a[0] != '-') {
            r.pos.push_back(a);
            if (stopAtFirstPos) { r.stopIndex = i; break; }   // 命令名即停（子命令 flag 留给 handler）
            continue;
        }
        if (a == "--") { posOnly = true; continue; }
        if (!is_bool_flag(a) && i + 1 < args.size() && !args[i + 1].empty() && args[i + 1][0] != '-')
            r.flags[a] = args[++i];
        else
            r.flags[a] = "";
    }
    return r;
}

static void apply_global_flags(const ParsedArgs& p) {
    for (const auto& [k, v] : p.flags) {
        if (k == "-c" || k == "--config") g_engine = v;
        else if (k == "-s" || k == "--source") g_source = v;
        else if (k == "-d" || k == "--dest") g_dest = v;
        else if (k == "-q" || k == "--quiet") g_quiet = true;
        else if (k == "-V" || k == "--verbose") g_verbose = true;
    }
}

void print_version() {
    std::cout << color::cyan("Cdocs") << " " << color::green(CDOCS_VERSION) << "\n";
}

// ---------- 命令注册表 ----------
struct Command {
    const char* name;
    const char* alias;       // 空格分隔别名，"" 表示无
    const char* usage;       // 参数行（不含命令名）
    const char* brief;       // 一句话说明
    int (*fn)(const std::vector<std::string>& args);
};

static const Command kCommands[] = {
    { "init",    "",      "<目录> [--no-engine] [--defaults]",   "新建完整站点骨架并自动构建", nullptr },
    { "new",     "add page", "<页面名>",                         "新建一篇文档并登记导航",     nullptr },
    { "section", "",      "<blog|docs|md-v<n>>",                 "添加内容区（分类）",         nullptr },
    { "build",   "",      "[-D] [--clean] [源] [目标]",          "构建站点（md → dist）",      nullptr },
    { "serve",   "",      "[-p 端口] [-o] [-w] [--no-build]",    "构建并启动本地预览服务器",   nullptr },
    { "deploy",  "",      "[--remote <url>] [--branch <b>] [-m <msg>] [--force] [--vercel] [--setup]",
                                                               "构建并发布（gh-pages / Vercel）", nullptr },
    { "clean",   "",      "",                                     "清空输出目录（dist）",      nullptr },
    { "doctor",  "",      "",                                     "环境与配置自检",            nullptr },
    { "check",   "",      "",                                     "站点质量检查（死链/token/数据孔）", nullptr },
    { "config",  "",      "",                                     "打印解析后的配置摘要",      nullptr },
    { "routes",  "",      "",                                     "列出站点页面路由清单",      nullptr },
    { "theme",   "",      "",                                     "列出可用主题 + 当前主题",   nullptr },
    { "plugins", "",      "",                                     "列出已注册插件 + 钩子",     nullptr },
    { "versions","",      "",                                     "列出配置的版本",            nullptr },
    { "version", "",      "",                                     "显示版本号",                nullptr },
    { "help",    "h",     "[命令]",                               "显示帮助（可指定命令）",    nullptr },
};
static const int kCmdCount = (int)(sizeof(kCommands) / sizeof(kCommands[0]));

static const Command* find_command(const std::string& name) {
    for (const auto& c : kCommands) {
        if (name == c.name) return &c;
        if (c.alias[0]) {
            std::string al = c.alias;
            size_t start = 0;
            while (start <= al.size()) {
                size_t sp = al.find(' ', start);
                std::string tok = al.substr(start, sp == std::string::npos ? std::string::npos : sp - start);
                if (tok == name) return &c;
                if (sp == std::string::npos) break;
                start = sp + 1;
            }
        }
    }
    return nullptr;
}

// ---------- 子命令帮助 ----------
static void print_subcommand_help(const Command& c) {
    using namespace color;
    std::cout << bold(green("Cdocs ")) << bold(green(c.name));
    if (c.alias[0]) std::cout << muted("（别名: ") << c.alias << muted("）");
    std::cout << " " << yellow(c.usage) << "\n"
              << "  " << muted(c.brief) << "\n"
              << "  " << muted("详见 ") << cyan("Cdocs help " + std::string(c.name)) << muted("。\n");
}

// ---------- 各命令处理器 ----------
static int h_init(const std::vector<std::string>& args) {
    auto p = parse_flags(args, 1);
    if (p.pos.empty()) {
        std::cerr << color::error("用法: Cdocs init <目录> [--no-engine] [--defaults]\n");
        return 2;
    }
    bool noEngine = p.flags.count("--no-engine") > 0;
    bool defs = p.flags.count("--defaults") > 0 || p.flags.count("-y") > 0;
    return cmd_init(p.pos[0], !noEngine, defs);
}

static int h_new(const std::vector<std::string>& args) {
    auto p = parse_flags(args, 1);
    if (p.pos.empty()) {
        std::cerr << color::error("用法: Cdocs new <页面名>\n");
        return 2;
    }
    return cmd_add(p.pos[0]);
}

static int h_section(const std::vector<std::string>& args) {
    auto p = parse_flags(args, 1);
    if (p.pos.empty()) {
        std::cerr << color::error("用法: Cdocs section <blog|docs|docs-v<数字>>\n");
        return 2;
    }
    return cmd_section(p.pos[0]);
}

static int h_build(const std::vector<std::string>& args) {
    auto p = parse_flags(args, 1);
    apply_global_flags(p);
    bool drafts = p.flags.count("-D") > 0 || p.flags.count("--drafts") > 0;
    bool clean = p.flags.count("--clean") > 0;
    if (!p.pos.empty()) g_source = p.pos[0];
    if (p.pos.size() > 1) g_dest = p.pos[1];
    return run_build(g_source, g_dest, drafts, clean);
}

static int h_serve(const std::vector<std::string>& args) {
    auto p = parse_flags(args, 1);
    apply_global_flags(p);
    int port = 8088;
    auto it = p.flags.find("--port");
    if (it == p.flags.end()) it = p.flags.find("-p");
    if (it != p.flags.end() && !it->second.empty()) port = std::atoi(it->second.c_str());
    bool build = p.flags.count("--no-build") == 0;
    bool watch = p.flags.count("-w") > 0 || p.flags.count("--watch") > 0;
    bool open = p.flags.count("-o") > 0 || p.flags.count("--open") > 0;
    return cmd_serve(g_dest, g_source, port, build, watch, open);
}

static int h_deploy(const std::vector<std::string>& args) {
    auto p = parse_flags(args, 1);
    apply_global_flags(p);
    if (p.flags.count("--setup") > 0) return cmd_deploy_setup(g_source);
    std::string remote = p.flags.count("--remote") ? p.flags["--remote"] : "";
    std::string branch = p.flags.count("--branch") ? p.flags["--branch"] : "";
    std::string msg = p.flags.count("-m") ? p.flags["-m"]
                     : (p.flags.count("--message") ? p.flags["--message"] : "");
    bool force = p.flags.count("-f") > 0 || p.flags.count("--force") > 0;
    bool vercel = p.flags.count("--vercel") > 0;
    return cmd_deploy(g_source, g_dest, remote, branch, msg, force, vercel);
}

static int h_clean(const std::vector<std::string>& args) {
    (void)args;
    return cmd_clean();
}

static int h_theme(const std::vector<std::string>& args) {
    (void)args;
    return cmd_theme();
}

static int h_plugins(const std::vector<std::string>& args) {
    (void)args;
    return cmd_plugins();
}

static int h_versions(const std::vector<std::string>& args) {
    (void)args;
    return cmd_versions();
}

static int h_version(const std::vector<std::string>& args) {
    (void)args;
    print_version();
    return 0;
}

static int h_help(const std::vector<std::string>& args);

// ---------- 总帮助（表驱动） ----------
void print_help() {
    using namespace color;
    std::cout
      << bold(cyan("Cdocs ")) << bold(CDOCS_VERSION)
      << muted(" — 静态文档站生成器 (C++)\n\n")
      << bold("用法: ") << "\n"
      << "  " << cyan("Cdocs") << " [全局旗标] <命令> [参数]\n\n"
      << bold("命令:") << "\n";
    for (const auto& c : kCommands) {
        std::string left = std::string("  ") + green(c.name);
        if (c.alias[0]) left += muted(" (") + std::string(c.alias) + muted(")");
        if (left.size() < 24) left.append(24 - left.size(), ' ');
        std::cout << left << muted(c.brief) << "\n";
    }
    std::cout << "\n"
      << bold("全局旗标（放在命令前）:") << "\n"
      << "  " << yellow("-c, --config <目录>") << "  引擎/配置根目录（默认 .Cdocs）\n"
      << "  " << yellow("-s, --source <目录>") << "  Markdown 源目录（默认 md）\n"
      << "  " << yellow("-d, --dest <目录>  ") << "  输出目录（默认 dist）\n"
      << "  " << yellow("-q, --quiet")        << "        静默输出\n"
      << "  " << yellow("-V, --verbose")      << "        详细输出\n"
      << "  " << yellow("-h, --help")         << "        显示本帮助\n"
      << "  " << yellow("-v, --version")      << "      显示版本号\n\n"
      << bold("示例:") << "\n"
      << "  " << cyan("Cdocs init mysite")       << muted("   # 新建站点") << "\n"
      << "  " << cyan("Cdocs new my-page")       << muted("   # 新建文档") << "\n"
      << "  " << cyan("Cdocs build --clean")     << muted(" # 构建") << "\n"
      << "  " << cyan("Cdocs serve -o -w")       << muted("  # 预览+热重载") << "\n"
      << "  " << cyan("Cdocs doctor")            << muted("    # 环境自检") << "\n"
      << "  " << cyan("Cdocs check")             << muted("     # 质量检查") << "\n"
      << "  " << cyan("Cdocs help build")        << muted("  # 子命令帮助") << "\n\n"
      << muted("无参数运行打印本帮助并退出。配置: .Cdocs/config/config.json + map.json + sidebar/\n");
}

static int h_help(const std::vector<std::string>& args) {
    auto p = parse_flags(args, 1);
    if (p.pos.empty()) { print_help(); return 0; }
    const Command* c = find_command(p.pos[0]);
    if (!c) {
        std::cerr << color::error("未知命令 '") << p.pos[0] << color::error("'。\n");
        return 2;
    }
    print_subcommand_help(*c);
    return 0;
}

// ---------- 命令分发 ----------
// 退出码：0=成功，1=运行错误，2=用法错误（未知命令/缺参数/未知旗标）
int run_command(std::vector<std::string> args) {
    // 全局旗标解析（到第一个非 '-' 参数 = 命令名）
    // 全局旗标解析：遇第一个位置参数（命令名）即停，子命令 flag 原样留给 handler
    ParsedArgs g = parse_flags(args, 0, /*stopAtFirstPos=*/true);
    apply_global_flags(g);
    if (g.flags.count("-h") > 0 || g.flags.count("--help") > 0) { print_help(); return 0; }
    if (g.flags.count("-v") > 0 || g.flags.count("--version") > 0) { print_version(); return 0; }
    if (g.pos.empty()) { print_help(); return 0; }

    std::string cmd = g.pos[0];
    std::vector<std::string> cmdArgs(args.begin() + g.stopIndex, args.end());
    const Command* c = find_command(cmd);
    if (!c) {
        std::cerr << color::error("错误: 未知命令 '") << cmd << color::error("'。\n")
                  << color::muted("可用命令：") << color::cyan("help")
                  << color::muted("（列出全部，或 ") << color::cyan("Cdocs help <命令>")
                  << color::muted(" 查看单个命令）。\n");
        return 2;
    }

    // 命令内 -h/--help
    ParsedArgs inner = parse_flags(cmdArgs, 1);
    if (inner.flags.count("-h") > 0 || inner.flags.count("--help") > 0) {
        print_subcommand_help(*c);
        return 0;
    }

    // 分发
    if (cmd == "init")    return h_init(cmdArgs);
    if (cmd == "new" || cmd == "add" || cmd == "page") return h_new(cmdArgs);
    if (cmd == "section") return h_section(cmdArgs);
    if (cmd == "build")   return h_build(cmdArgs);
    if (cmd == "serve")   return h_serve(cmdArgs);
    if (cmd == "deploy")  return h_deploy(cmdArgs);
    if (cmd == "clean")   return h_clean(cmdArgs);
    if (cmd == "doctor")  return cmd_doctor();
    if (cmd == "check")   return cmd_check();
    if (cmd == "config")  return cmd_config();
    if (cmd == "routes")  return cmd_routes();
    if (cmd == "theme")   return h_theme(cmdArgs);
    if (cmd == "plugins") return h_plugins(cmdArgs);
    if (cmd == "versions")return h_versions(cmdArgs);
    if (cmd == "version") return h_version(cmdArgs);
    if (cmd == "help")    return h_help(cmdArgs);
    return 2;  // unreachable
}
