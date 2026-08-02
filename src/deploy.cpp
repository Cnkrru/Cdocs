// deploy.cpp —— 部署子命令实现（内置 git 调用，零外部脚本依赖）
//
// 行业对标：mkdocs gh-deploy（构建 → git add/commit/push 到 gh-pages 分支）。
// 流程：
//   1. 检查 git 可用（git --version）
//   2. 构建站点（默认全量；dist 已有 .git 时不 clean，保留部署历史）
//   3. 写入 dist/.nojekyll（GitHub Pages 不经过 Jekyll）
//   4. 在 dist 内执行 git：init（必要时）→ 切分支 → add → commit（有变更时）
//   5. 解析远端（--remote > config > 已有 origin > 从 site.url 推断 GitHub 仓库）
//   6. git push -u origin <branch>
//
// 失败安全：任一步失败即中止并返回非 0，不破坏 dist。

#include "deploy.hpp"
#include "builder.hpp"
#include "plugin.hpp"     // cmd_deploy_setup：扫描并执行部署插件（setup 钩子）

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#endif

namespace {

// ---------------- 子进程执行 + 输出捕获 ----------------
// 执行 cmdLine（Windows 走 CreateProcessW，兼容中文路径），stdout/stderr 合并到 out。
// 返回退出码：0=成功，-1=启动失败，124=超时（timeoutSec<=0 表示不设超时）。
int run_capture(const std::string& cmdLine, std::string& out, int timeoutSec = 300) {
    out.clear();
#ifdef _WIN32
    // 管道：子进程 stdout/stderr 都接到写端
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;
    HANDLE hOutR = nullptr, hOutW = nullptr;
    if (!CreatePipe(&hOutR, &hOutW, &sa, 0)) return -1;
    SetHandleInformation(hOutR, HANDLE_FLAG_INHERIT, 0);   // 读端不继承

    // 命令转 UTF-16（lpCommandLine 需可写缓冲区）
    int wlen = MultiByteToWideChar(CP_UTF8, 0, cmdLine.c_str(), (int)cmdLine.size(), nullptr, 0);
    std::vector<wchar_t> wcmd(wlen + 1);
    MultiByteToWideChar(CP_UTF8, 0, cmdLine.c_str(), (int)cmdLine.size(), wcmd.data(), wlen);
    wcmd[wlen] = L'\0';

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hOutW;
    si.hStdError  = hOutW;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessW(nullptr, wcmd.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(hOutR);
        CloseHandle(hOutW);
        return -1;
    }
    CloseHandle(hOutW);   // 父端关闭写，读端可 EOF

    DWORD startTick = GetTickCount();
    char buf[4096];
    bool timedOut = false;
    for (;;) {
        // 先读净当前管道内数据（避免输出大时管道写满导致子进程阻塞）
        for (;;) {
            DWORD avail = 0;
            if (!PeekNamedPipe(hOutR, nullptr, 0, nullptr, &avail, nullptr) || avail == 0) break;
            DWORD rd = 0;
            if (!ReadFile(hOutR, buf, sizeof(buf), &rd, nullptr) || rd == 0) break;
            out.append(buf, rd);
        }
        DWORD w = WaitForSingleObject(pi.hProcess, 150);
        if (w == WAIT_OBJECT_0) break;
        if (w == WAIT_TIMEOUT && timeoutSec > 0 &&
            (GetTickCount() - startTick) > (DWORD)timeoutSec * 1000) {
            timedOut = true;
            TerminateProcess(pi.hProcess, 124);
            WaitForSingleObject(pi.hProcess, 5000);
            break;
        }
    }
    // 读剩余输出
    for (;;) {
        DWORD rd = 0;
        if (!ReadFile(hOutR, buf, sizeof(buf), &rd, nullptr) || rd == 0) break;
        out.append(buf, rd);
    }
    DWORD code = timedOut ? 124 : 1;
    if (!timedOut) GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(hOutR);
    return (int)code;
#else
    // POSIX：popen 捕获合并输出
    FILE* fp = popen((cmdLine + " 2>&1").c_str(), "r");
    if (!fp) return -1;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0) out.append(buf, n);
    int rc = pclose(fp);
    if (rc < 0) return -1;
    if (WIFEXITED(rc)) return WEXITSTATUS(rc);
    return -1;
#endif
}

// 打印 git 命令输出（去空行；保留 stderr 语义用 color）
void show_git_out(const std::string& tag, const std::string& out, bool failed) {
    if (out.empty()) return;
    std::istringstream ss(out);
    std::string line;
    while (std::getline(ss, line)) {
        if (trim(line).empty()) continue;
        if (failed)
            std::cerr << color::yellow("  ") << tag << " " << line << "\n";
        else if (!g_quiet)
            std::cout << color::muted("  ") << tag << " " << line << "\n";
    }
}

// 从 GitHub Pages 域名推断仓库：https://<owner>.github.io/<repo>/ → https://github.com/<owner>/<repo>.git
std::string infer_github_remote(const std::string& url) {
    // 规范化：去协议前缀与结尾斜杠
    std::string u = url;
    std::string proto = "https://";
    if (u.rfind(proto, 0) == 0) u = u.substr(proto.size());
    else if (u.rfind("http://", 0) == 0) u = u.substr(7);
    if (!u.empty() && u.back() == '/') u.pop_back();

    // 支持 user.github.io 与 github.io 组织站
    auto ends_with = [](const std::string& s, const std::string& suf) {
        return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
    };
    if (!ends_with(u, ".github.io")) return "";     // 自定义域名无法推断
    std::string host = u.substr(0, u.find('/'));
    if (!ends_with(host, ".github.io")) return "";
    std::string owner = host.substr(0, host.size() - 10);      // 去掉 ".github.io"
    std::string repo = u.substr(host.size() + 1);
    if (repo.empty() || repo.find('/') != std::string::npos) return "";
    return "https://github.com/" + owner + "/" + repo + ".git";
}

} // namespace

// ---------------- Vercel 部署（构建 → 调 vercel CLI 发布 dist） ----------------
// 依赖 vercel CLI（Node 生态）：优先 PATH 里的 vercel，其次 npx vercel@latest。
// 首次使用需先 `vercel login`（或设置 VERCEL_TOKEN），此后一条命令即发布生产环境。
static int deploy_vercel(const fs::path& source, const fs::path& dest) {
    if (!g_quiet)
        std::cout << color::cyan("构建站点 ") << source << color::muted(" → ") << dest << "\n";
    int rc = run_build(source, dest, false, true);
    if (rc != 0) {
        std::cerr << color::error("错误: 构建失败，中止部署。\n");
        return rc;
    }

    // 检测 Vercel CLI：vercel > npx vercel
    std::string out, vercCmd;
    if (run_capture("vercel --version", out, 30) == 0) {
        vercCmd = "vercel";
        if (!g_quiet) std::cout << color::muted("  · 使用 ") << trim(out) << "\n";
    } else if (run_capture("npx --version", out, 30) == 0) {
        vercCmd = "npx --yes vercel@latest";
        if (!g_quiet) std::cout << color::muted("  · vercel 未安装，用 npx 临时拉取\n");
    } else {
        std::cerr << color::error("错误: 未找到 Vercel CLI。") << "\n"
                  << color::muted("  请先安装 Node.js（https://nodejs.org），然后：\n")
                  << color::muted("    npm i -g vercel && vercel login\n");
        return 1;
    }

    if (!g_quiet)
        std::cout << color::cyan("发布到 Vercel 生产环境 ") << dest << "\n";
    std::string cmd = vercCmd + " --prod --yes \"" + dest.string() + "\"";
    rc = run_capture(cmd, out, 900);
    std::cout << out << "\n";
    if (rc != 0) {
        std::cerr << color::error("错误: Vercel 部署失败。")
                  << color::muted("（若提示未登录，先执行 vercel login；若提示未关联项目，先 vercel link）\n");
        return 1;
    }
    return 0;
}

int cmd_deploy(fs::path source, fs::path dest,
               std::string remote, std::string branch,
               std::string message, bool force, bool toVercel) {
    std::error_code ec;

    // ---- 0) Vercel 平台：构建 → vercel CLI 发布（不依赖 git） ----
    if (toVercel)
        return deploy_vercel(source, dest);

    // ---- 0) git 可用性 ----
    std::string ver;
    if (run_capture("git --version", ver, 20) != 0) {
        std::cerr << color::error("错误: 未找到 git。") << "\n"
                  << color::muted("  deploy 依赖 git（构建产物推送到远端分支）。请先安装 Git："
                                  "https://git-scm.com/download/win\n");
        return 1;
    }
    if (!g_quiet)
        std::cout << color::muted("  · ") << trim(ver) << "\n";

    // ---- 1) 读取 config site.deploy 默认值（CLI 参数优先） ----
    fs::path cfgPath = g_engine / "config" / "config.json";
    if (fs::exists(cfgPath, ec)) {
        try {
            json j = json::parse(read_file(cfgPath));
            json site = j.contains("site") && j["site"].is_object() ? j["site"] : j;
            if (site.contains("deploy") && site["deploy"].is_object()) {
                auto& dp = site["deploy"];
                if (remote.empty())  remote  = dp.value("remote", std::string());
                if (branch.empty())  branch  = dp.value("branch", std::string());
                if (message.empty()) message = dp.value("message", std::string());
            }
            if (remote.empty() && site.contains("url"))
                remote = infer_github_remote(site["url"].get<std::string>());
        } catch (...) { /* config 解析失败不影响 deploy 主流程 */ }
    }
    if (branch.empty())  branch  = "gh-pages";
    if (message.empty()) message = "Deploy Cdocs site";

    // ---- 2) 构建站点 ----
    // dist 已有 .git（重复部署）→ 保留历史不清空；否则 --force 或首次部署时清空保证产物干净
    bool hasGit = fs::exists(dest / ".git", ec);
    bool clean = force || !hasGit;
    if (!g_quiet)
        std::cout << color::cyan("构建站点 ") << source << color::muted(" → ") << dest
                  << (clean ? color::muted("（全量，已清空）") : color::muted("（覆盖式）")) << "\n";
    int rc = run_build(source, dest, false, clean);
    if (rc != 0) {
        std::cerr << color::error("错误: 构建失败，中止部署。\n");
        return rc;
    }

    // ---- 3) .nojekyll（GitHub Pages 不使用 Jekyll 处理） ----
    std::ofstream(dest / ".nojekyll") << "";
    if (!g_quiet)
        std::cout << color::muted("  · 已写入 .nojekyll\n");

    // ---- 4) git 仓库准备（cwd = dest） ----
    fs::path saved = fs::current_path(ec);
    fs::current_path(dest, ec);
    if (ec) {
        std::cerr << color::error("错误: 无法进入输出目录 ") << dest << "\n";
        return 1;
    }
    struct CwdGuard {
        fs::path saved; std::error_code ec;
        ~CwdGuard() { fs::current_path(saved, ec); }
    } guard{saved, ec};

    std::string out;
    bool inited = (run_capture("git rev-parse --is-inside-work-tree", out, 20) == 0
                   && trim(out) == "true");
    if (!inited) {
        std::cout << color::cyan("初始化部署仓库") << color::muted("（orphan 分支 ") << branch << color::muted("）\n");
        if (run_capture("git init -q", out, 30) != 0) {
            std::cerr << color::error("错误: git init 失败。\n"); return 1;
        }
        // 孤儿分支：HEAD 指向不存在的 gh-pages，首次 commit 直接落在该分支
        std::string sym = "git symbolic-ref HEAD refs/heads/" + branch;
        if (run_capture(sym, out, 20) != 0) {
            std::cerr << color::error("错误: 无法创建分支 ") << branch << "。\n"; return 1;
        }
    } else {
        // 已初始化：确保当前在目标分支
        std::string cur;
        run_capture("git branch --show-current", cur, 20);
        if (trim(cur) != branch) {
            std::string chk = "git checkout -q " + branch;
            if (run_capture(chk, out, 30) != 0) {
                chk = "git checkout -q -b " + branch;
                if (run_capture(chk, out, 30) != 0) {
                    std::cerr << color::error("错误: 无法切换到分支 ") << branch << "。\n";
                    return 1;
                }
            }
        }
    }

    // ---- 5) add + commit（无变更则跳过，避免 nothing to commit） ----
    std::string st;
    run_capture("git status --porcelain", st, 30);
    if (!trim(st).empty()) {
        if (run_capture("git add -A", out, 60) != 0) {
            std::cerr << color::error("错误: git add 失败。\n"); return 1;
        }
        std::string cmt = "git commit -q -m \"" + message + "\"";
        if (run_capture(cmt, out, 60) != 0) {
            std::cerr << color::error("错误: git commit 失败。\n")
                      << color::muted("  请先配置提交身份：\n")
                      << color::muted("    git config --global user.name  \"Your Name\"\n")
                      << color::muted("    git config --global user.email \"you@example.com\"\n");
            return 1;
        }
        if (!g_quiet)
            std::cout << color::green("  ✓ 已提交: ") << message << "\n";
    } else if (!g_quiet) {
        std::cout << color::muted("  · 无内容变更，跳过 commit\n");
    }

    // ---- 6) 远端解析 + push ----
    if (remote.empty()) {
        std::string ro;
        if (run_capture("git remote get-url origin", ro, 20) == 0 && !trim(ro).empty())
            remote = trim(ro);
    }
    if (remote.empty()) {
        std::cerr << color::error("错误: 无法确定远端仓库。\n")
                  << color::muted("  请用 --remote <url> 指定，或在 config.json 的 ")
                  << color::cyan("site.deploy.remote") << color::muted(" 配置，\n")
                  << color::muted("  或将 site.url 设为 GitHub Pages 地址（如 https://user.github.io/repo/）。\n");
        return 1;
    }
    std::string ro;
    bool hasOrigin = (run_capture("git remote get-url origin", ro, 20) == 0 && !trim(ro).empty());
    if (!hasOrigin) {
        std::string add = "git remote add origin \"" + remote + "\"";
        if (run_capture(add, out, 30) != 0) {
            std::cerr << color::error("错误: git remote add 失败。\n"); return 1;
        }
        if (!g_quiet)
            std::cout << color::muted("  · 已添加远端 origin → ") << remote << "\n";
    } else if (!g_quiet) {
        std::cout << color::muted("  · 远端 origin = ") << trim(ro) << "\n";
    }

    std::cout << color::cyan("推送 ") << color::green(branch) << color::muted(" → ") << remote << " …\n";
    std::string push = "git push -u origin " + branch;
    int prc = run_capture(push, out, 600);
    show_git_out("[git]", out, prc != 0);
    if (prc != 0) {
        std::cerr << color::error("错误: push 失败（exit=") << prc << color::error("）。\n")
                  << color::muted("  检查网络与远端权限；无远端分支时可用 --force 强推首次提交。\n");
        return prc;
    }
    std::cout << color::green("部署完成 ✓ ") << color::muted("站点已发布到 ")
              << color::cyan(branch) << color::muted(" 分支\n");
    return 0;
}

// ---------------- 部署配置生成（插件驱动） ----------------
// 把「自动化部署」做成插件：每个平台一个插件（.Cdocs/plugins/<name>/），声明 `setup` 钩子；
// 本命令扫描并执行所有 setup 钩子，插件脚本以 source（项目根）为上下文生成
// .github/workflows/*.yml / vercel.json 等平台配置文件。引擎不感知任何平台细节。
int cmd_deploy_setup(fs::path source) {
    // 部署配置文件生成到项目根（source 的父目录：默认 source=docs → 项目根），
    // 而非源文档目录本身（.github/、vercel.json 应位于仓库根）。
    std::error_code ec;
    fs::path root = fs::absolute(source);
    if (!fs::exists(root / ".Cdocs", ec)) root = root.parent_path();   // docs → 项目根
    plugins_scan_all();                       // 扫描 .Cdocs/plugins/*/plugin.json
    if (!plugins_any()) {
        std::cout << color::yellow("  无插件注册（.Cdocs/plugins/ 为空）。\n")
                  << color::muted("  部署插件参考：\n")
                  << color::muted("    .Cdocs/plugins/github-pages/    GitHub Pages 自动部署\n")
                  << color::muted("    .Cdocs/plugins/vercel/          Vercel 自动部署\n");
        return 0;
    }
    json ctx = {
        {"source", root.string()},             // 项目根（插件写文件的基准）
        {"engine", fs::absolute(g_engine).string()}
    };
    std::cout << color::cyan("\n[deploy setup] 生成自动化部署配置（插件驱动）\n");
    run_plugin_hooks("setup", ctx);
    std::cout << color::green("\n完成。生成的部署文件请提交进 git（push 后自动部署生效）。\n");
    return 0;
}
