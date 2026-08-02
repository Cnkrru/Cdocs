// cli.cpp —— 全局旗标解析与子命令分发（纯代码搬迁自原 main.cpp）

#include "cli.hpp"
#include "builder.hpp"
#include "server.hpp"
#include "deploy.hpp"

void print_version() {
    std::cout << color::cyan("Cdocs") << " " << color::green(CDOCS_VERSION) << "\n";
}

void print_help() {
    using namespace color;
    std::cout
      << bold(cyan("Cdocs ")) << bold(CDOCS_VERSION)
      << muted(" — 静态文档站生成器 (C++)\n\n")
      << bold("用法:") << "\n"
      << "  " << cyan("Cdocs") << " [全局旗标] <命令> [参数]\n\n"
      << bold("命令:") << "\n"
      << "  " << green("init")  << muted(" <目录> [--no-engine]     ") << "新建完整站点骨架\n"
      << "  " << green("new")   << muted(" <页面名> [别名 add/page] ") << "新建一篇文档并登记导航\n"
      << "  " << green("build")  << muted(" [-D] [--clean] [源] [目标]") << " 构建站点（默认 docs → dist）\n"
      << "  " << green("serve")  << muted(" [-p 端口] [-w] [-o]      ") << "构建并启动本地预览服务器\n"
      << "  " << green("deploy") << muted(" [--remote <url>] [--branch <b>] [-m <msg>] [--vercel]") << " 构建并发布\n"
      << "  " << green("clean")  << muted("                       ") << "清空输出目录（dist）\n"
      << "  " << green("version")<< muted("                       ") << "显示版本号\n"
      << "  " << green("help")   << muted("                       ") << "显示本帮助\n\n"
      << bold("全局旗标（放在子命令前）:") << "\n"
      << "  " << yellow("-c, --config <目录>") << "  引擎/配置根目录（默认 .Cdocs）\n"
      << "  " << yellow("-s, --source <目录>") << "  Markdown 源目录（默认 docs）\n"
      << "  " << yellow("-d, --dest <目录>  ") << "  输出目录（默认 dist，serve 也用它作预览根）\n"
      << "  " << yellow("-q, --quiet")        << "        静默输出\n"
      << "  " << yellow("-V, --verbose")      << "        详细输出\n"
      << "  " << yellow("-h, --help")         << "        显示本帮助\n"
      << "  " << yellow("-v, --version")      << "      显示版本号\n\n"
      << bold("子命令旗标:") << "\n"
      << "  " << yellow("build -D, --drafts") << "    包含草稿页（默认排除）\n"
      << "  " << yellow("build --clean")      << "        构建前清空输出目录\n"
      << "  " << yellow("serve -p, --port <n>") << "  指定端口（默认 8088）\n"
      << "  " << yellow("serve -w, --watch")  << "      文件改动时自动重建\n"
      << "  " << yellow("serve -o, --open")   << "      启动后自动打开浏览器\n"
      << "  " << yellow("serve --no-build")   << "      跳过构建，直接预览现有 dist\n"
      << "  " << yellow("init --no-engine")   << "      仅生成内容骨架，不复制 Cdocs.exe\n"
      << "  " << yellow("deploy --remote <url>") << "  指定远端仓库（否则读 config site.deploy.remote）\n"
      << "  " << yellow("deploy --branch <b>") << "   目标分支（默认 gh-pages）\n"
      << "  " << yellow("deploy -m <msg>")    << "      提交信息（默认 \"Deploy Cdocs site\"）\n"
      << "  " << yellow("deploy --force")     << "      构建前清空输出目录\n"
      << "  " << yellow("deploy --vercel")    << "      发布到 Vercel（构建后调 vercel CLI，需先 vercel login）\n\n"
      << bold("示例:") << "\n"
      << "  " << cyan("Cdocs init mysite") << muted("            # 新建站点（自动构建）") << "\n"
      << "  " << cyan("Cdocs new my-page") << muted("           # 新建文档并登记导航") << "\n"
      << "  " << cyan("Cdocs build") << muted("                  # docs → dist") << "\n"
      << "  " << cyan("Cdocs build -D --clean") << muted("      # 含草稿并先清空") << "\n"
      << "  " << cyan("Cdocs serve -o -w -p 3000") << muted(" # 开浏览器+热重载+指定端口") << "\n"
      << "  " << cyan("Cdocs deploy") << muted("               # 构建并推送 gh-pages") << "\n"
      << "  " << cyan("Cdocs deploy --vercel") << muted("      # 构建并发布到 Vercel 生产环境") << "\n"
      << "  " << cyan("Cdocs deploy --remote https://github.com/u/r.git") << muted("  # 指定远端") << "\n"
      << "  " << cyan("Cdocs clean") << muted("                  # 清空 dist\n\n")
      << muted("无参数运行打印本帮助并退出；双击 exe 无参数会等待 Ctrl+C 退出（防窗口一闪而过）。\n")
      << muted("配置: .Cdocs/config/config.json + route.json\n");
}

void print_subcommand_help(const std::string& cmd) {
    using namespace color;
    if (cmd == "build") {
        std::cout << bold(green("Cdocs build")) << " [" << yellow("-D/--drafts") << "] ["
                  << yellow("--clean") << "] [" << yellow("-q/-V") << "] [源] [目标]\n"
                  << muted("  构建静态站点。默认源=docs，目标=dist；可用全局 -s/-d 覆盖。\n")
                  << muted("  -D/--drafts  包含草稿页（默认排除）\n")
                  << muted("  --clean      构建前清空目标目录\n");
    } else if (cmd == "serve") {
        std::cout << bold(green("Cdocs serve")) << " [" << yellow("-p/--port <n>") << "] ["
                  << yellow("-o/--open") << "] [" << yellow("-w/--watch") << "] ["
                  << yellow("--no-build") << "]\n"
                  << muted("  构建并启动本地预览服务器（常驻，Ctrl+C 退出）。默认端口 8088。\n")
                  << muted("  -w/--watch   监听 docs/ 与配置，改动自动重建\n");
    } else if (cmd == "init") {
        std::cout << bold(green("Cdocs init")) << " <目录> [" << yellow("--no-engine") << "]\n"
                  << muted("  新建完整站点骨架（config/route/i18n/示例文档/前端资源），并自动构建。\n");
    } else if (cmd == "clean") {
        std::cout << bold(green("Cdocs clean")) << "\n"
                  << muted("  清空输出目录（默认 dist）。对标 jekyll clean / docusaurus clear。\n");
    } else if (cmd == "deploy") {
        std::cout << bold(green("Cdocs deploy")) << " [" << yellow("--remote <url>") << "] ["
                  << yellow("--branch <b>") << "] [" << yellow("-m <msg>") << "] ["
                  << yellow("--force") << "] [" << yellow("--vercel") << "]\n"
                  << muted("  构建站点并发布。两种目标：\n")
                  << muted("    （默认）推送到远端分支（gh-pages），对标 mkdocs gh-deploy；\n")
                  << muted("      远端解析：--remote > config site.deploy.remote > 已有 origin > site.url 推断。\n")
                  << muted("    --vercel  发布到 Vercel 生产环境（构建后调 vercel CLI：vercel --prod --yes dist）。\n")
                  << muted("      依赖 Node + vercel CLI：npm i -g vercel && vercel login（首次）。\n")
                  << muted("  --force  构建前清空输出目录（默认仅首次部署清空，保留部署历史）。\n");
    } else if (cmd == "new" || cmd == "add" || cmd == "page") {
        std::cout << bold(green("Cdocs new")) << " <页面名>\n"
                  << muted("  新建内容页（从 archetypes/default.md），并登记到 route.json 导航。\n");
    } else {
        print_help();
    }
}

// 解析子命令之前的全局旗标（对标 Hugo/MkDocs：全局旗标放在子命令前）。
// earlyExit >= 0 表示已处理并应直接退出（help/version/用法错误）；否则返回剩余参数。
std::vector<std::string> parse_global_flags(std::vector<std::string>& args, int& earlyExit) {
    earlyExit = -1;
    auto need_val = [&](size_t& i, const std::string& flag) -> std::string {
        if (i + 1 >= args.size()) {
            std::cerr << color::error("错误: ") << flag << color::error(" 需要一个参数\n");
            earlyExit = 2; return "";
        }
        return args[++i];
    };
    size_t i = 0;
    while (i < args.size()) {
        const std::string& a = args[i];
        if (a.empty() || a[0] != '-') break;            // 遇到子命令，停止
        if (a == "--") { ++i; break; }                  // 显式结束全局旗标
        if (a == "-c" || a == "--config")        g_engine = need_val(i, a);
        else if (a == "-s" || a == "--source")   g_source = need_val(i, a);
        else if (a == "-d" || a == "--dest")     g_dest   = need_val(i, a);
        else if (a == "-q" || a == "--quiet")    g_quiet  = true;
        else if (a == "-V" || a == "--verbose")  g_verbose = true;
        else if (a == "-h" || a == "--help")     { print_help();    earlyExit = 0; }
        else if (a == "-v" || a == "--version")  { print_version(); earlyExit = 0; }
        else {
            std::cerr << color::error("错误: 未知全局旗标 '") << a
                      << color::error("'（见 ") << color::cyan("Cdocs -h") << color::error("）\n");
            earlyExit = 2;
        }
        ++i;
        if (earlyExit >= 0) break;
    }
    return std::vector<std::string>(args.begin() + i, args.end());
}

// 执行单条命令。args[0] 为命令名。
// 退出码：0=成功，1=运行错误，2=用法错误（未知命令 / 缺参数 / 未知旗标）。
int run_command(std::vector<std::string> args) {
    std::string cmd = args[0];

    if (cmd == "-h" || cmd == "--help" || cmd == "help")       { print_help();    return 0; }
    if (cmd == "-v" || cmd == "--version" || cmd == "version") { print_version(); return 0; }

    // init <目录> [--no-engine]：新建完整站点骨架（含引擎资源与 Cdocs.exe）
    if (cmd == "init") {
        if (args.size() < 2) {
            std::cerr << color::error("用法: Cdocs [-c <引擎>] init <目录> [--no-engine]\n");
            return 2;
        }
        bool noEngine = false;
        for (size_t i = 2; i < args.size(); ++i)
            if (args[i] == "--no-engine") noEngine = true;
        return cmd_init(args[1], !noEngine);
    }

    // new/add/page <页面名>：新建内容页（从 archetype），并登记导航
    if (cmd == "new" || cmd == "add" || cmd == "page") {
        if (args.size() < 2) {
            std::cerr << color::error("用法: Cdocs new <页面名>\n");
            return 2;
        }
        return cmd_add(args[1]);
    }

    // clean：清空输出目录（对标 jekyll clean / docusaurus clear）
    if (cmd == "clean") {
        if (args.size() >= 2 && (args[1] == "-h" || args[1] == "--help")) { print_subcommand_help("clean"); return 0; }
        return cmd_clean();
    }

    // build [-D/--drafts] [--clean] [-q/-V] [源] [目标]：构建站点
    if (cmd == "build") {
        bool drafts = false, clean = false;
        std::vector<std::string> pos;
        for (size_t i = 1; i < args.size(); ++i) {
            const std::string& a = args[i];
            if (a == "-D" || a == "--drafts")          drafts = true;
            else if (a == "--clean")                   clean = true;
            else if (a == "-q" || a == "--quiet")      g_quiet = true;
            else if (a == "-V" || a == "--verbose")    g_verbose = true;
            else if (a == "-h" || a == "--help")       { print_subcommand_help("build"); return 0; }
            else if (a.rfind("-", 0) == 0) {
                std::cerr << color::error("错误: 未知 build 旗标 '") << a << color::error("'\n");
                return 2;
            } else pos.push_back(a);
        }
        if (!pos.empty()) g_source = pos[0];
        if (pos.size() > 1) g_dest = pos[1];
        return run_build(g_source, g_dest, drafts, clean);
    }

    // serve [-p/--port] [-o/--open] [-w/--watch] [--no-build]：构建并预览（常驻）
    if (cmd == "serve") {
        int port = 8088;
        bool build = true, watch = false, open = false;
        for (size_t i = 1; i < args.size(); ++i) {
            const std::string& a = args[i];
            if ((a == "-p" || a == "--port") && i + 1 < args.size())
                port = std::atoi(args[++i].c_str());
            else if (a == "-o" || a == "--open") open = true;
            else if (a == "-w" || a == "--watch") watch = true;
            else if (a == "--no-build")           build = false;
            else if (a == "-h" || a == "--help")  { print_subcommand_help("serve"); return 0; }
            else if (a.rfind("-", 0) == 0) {
                std::cerr << color::error("错误: 未知 serve 旗标 '") << a << color::error("'\n");
                return 2;
            }
        }
        return cmd_serve(g_dest, g_source, port, build, watch, open);
    }

    // deploy [--remote <url>] [--branch <b>] [-m <msg>] [--force] [--vercel]：构建并发布
    if (cmd == "deploy") {
        std::string remote, branch, message;
        bool force = false, toVercel = false;
        for (size_t i = 1; i < args.size(); ++i) {
            const std::string& a = args[i];
            if ((a == "--remote") && i + 1 < args.size())         remote  = args[++i];
            else if ((a == "--branch") && i + 1 < args.size())    branch  = args[++i];
            else if ((a == "-m" || a == "--message") && i + 1 < args.size()) message = args[++i];
            else if (a == "--force" || a == "-f")                 force   = true;
            else if (a == "--vercel")                             toVercel = true;
            else if (a == "-h" || a == "--help")                  { print_subcommand_help("deploy"); return 0; }
            else if (a.rfind("-", 0) == 0) {
                std::cerr << color::error("错误: 未知 deploy 旗标 '") << a << color::error("'\n");
                return 2;
            }
        }
        return cmd_deploy(g_source, g_dest, remote, branch, message, force, toVercel);
    }

    // 严格 CLI：未知命令明确报错并退出（退出码 2 = 用法错误）
    std::cerr << color::error("错误: 未知命令 '") << cmd << color::error("'。\n")
              << color::muted("可用命令：") << color::cyan("init / new / add / build / serve / deploy / clean / -h / -v")
              << color::muted("（详见 ") << color::cyan("Cdocs -h") << color::muted("）。\n");
    return 2;
}
