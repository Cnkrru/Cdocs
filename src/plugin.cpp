// plugin.cpp —— 外部脚本插件系统实现
//
// 核心机制：
//   1. plugins_scan_all()：扫描 .Cdocs/plugins/*/plugin.json，注册每个插件的钩子→命令映射
//   2. run_plugin_hooks()：对每个匹配钩子的插件：
//        a. 把上下文 JSON 写入 <engine>/.build/plugins/<name>.<hook>.ctx.json
//        b. subprocess 执行 <cmd> <ctx.json> <out.json>（命令行参数传路径）
//        c. 等待退出（可设超时，超时杀进程）；读 out.json 展示插件输出摘要
//   3. 失败隔离：任何一步失败只打警告，不影响构建

#include "plugin.hpp"
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#endif
#include <memory>
#include <sstream>
#include <thread>
#include <atomic>
#include <mutex>

namespace {

// ---------------- 插件注册表 ----------------
struct PluginHook {
    std::string cmd;       // 要执行的命令（不含 ctx/out 参数，运行时追加）
    int timeout = 30;      // 超时秒数（Windows 生效；POSIX 简化等待）
};
struct Plugin {
    std::string name;
    fs::path dir;                               // 插件目录（脚本相对路径的基准）
    std::map<std::string, PluginHook> hooks;    // hook 名 -> 命令
};
std::vector<Plugin> g_plugins;                  // 已扫描到的插件
bool g_scanned = false;

// ============ 持久子进程（构建开始 spawn 一次，后续调用走 stdin/stdout 行协议） ============

// Python 持久包装器：常驻进程，stdin 行 JSON → subprocess 调用插件脚本 → stdout 响应。
// subprocess 冷启动比 OS 级 spawn 快 20-30 倍（解释器+DLL 已驻留）。
static const char* kPersistRunnerCode = R"(import sys,json,tempfile,os,importlib.util
sp=sys.argv[1]
d=os.path.dirname(sp)
spec=importlib.util.spec_from_file_location("plugin_module",sp)
plugin=importlib.util.module_from_spec(spec)
sys.path.insert(0,d)
spec.loader.exec_module(plugin)
while True:
 l=sys.stdin.readline()
 if not l: break
 r=json.loads(l)
 c=r.get('ctx',{})
 fd,cp=tempfile.mkstemp(suffix='.json',text=True,dir=d)
 with os.fdopen(fd,'w') as f: json.dump(c,f)
 op=cp.replace('.json','.out.json')
 sys.argv=[sp,cp,op]
 try:
  plugin.main()
  with open(op) as f: out=json.load(f)
 except Exception as e: out={'ok':False,'message':str(e)}
 try: os.unlink(cp)
 except: pass
 try: os.unlink(op)
 except: pass
 sys.stdout.write(json.dumps(out,ensure_ascii=False)+'\n')
 sys.stdout.flush()
)";

struct PersistentProc {
    std::string name;
#ifdef _WIN32
    HANDLE hProcess;
    HANDLE hStdinWrite;
    HANDLE hStdoutRead;
#else
    pid_t pid;
    int stdin_fd;
    int stdout_fd;
#endif
    std::mutex mtx;   // 每进程串行化请求（同一 stdin/stdout 不能并发写读）
};

static std::map<std::string, std::unique_ptr<PersistentProc>> g_persistent;
static std::mutex g_persistent_mtx;

// 解析 "python scripts/foo.py" → "scripts/foo.py"（取最后一个空白分隔 token）
static std::string extract_script_arg(const std::string& cmd) {
    std::istringstream iss(cmd);
    std::string tok, last;
    while (iss >> tok) last = tok;
    return last;
}

// 启动持久插件进程（首次调用时触发；失败时返回 false，调用方回退一次性子进程）
static bool spawn_persistent(const Plugin& p, const PluginHook& h) {
    // 写持久运行器脚本到 .build/（仅一次），文件级启动避免 CreateProcessW 内联引号问题
    static bool runnerWritten = false;
    if (!runnerWritten) {
        std::error_code ec;
        fs::path rp = g_engine / ".build" / "persist_runner.py";
        fs::create_directories(g_engine / ".build", ec);
        {
            std::ofstream f(rp);
            if (!f) { std::cerr << "[persist] 运行器写入失败: " << rp << "\n"; return false; }
            f << kPersistRunnerCode;
        }
        runnerWritten = fs::exists(rp);
        if (!runnerWritten) { std::cerr << "[persist] 运行器验证失败: " << rp << "\n"; return false; }
    }
    std::string script = extract_script_arg(h.cmd);
    fs::path scriptPath = fs::absolute(p.dir / script);
    fs::path runnerPath = fs::absolute(g_engine / ".build" / "persist_runner.py");
#ifdef _WIN32
    HANDLE hR1, hW1, hR2, hW2;
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE; sa.lpSecurityDescriptor = nullptr;
    if (!CreatePipe(&hR1, &hW1, &sa, 0)) return false;
    if (!CreatePipe(&hR2, &hW2, &sa, 0)) { CloseHandle(hR1); CloseHandle(hW1); return false; }
    SetHandleInformation(hW1, HANDLE_FLAG_INHERIT, 0);  // 子进程不继承写端
    SetHandleInformation(hR2, HANDLE_FLAG_INHERIT, 0);  // 子进程不继承读端

    std::string cmdLine = "python -u \"" + runnerPath.string() + "\" \"" + scriptPath.string() + "\"";
    int wlen = MultiByteToWideChar(CP_UTF8, 0, cmdLine.c_str(), (int)cmdLine.size(), nullptr, 0);
    std::vector<wchar_t> wcmd(wlen + 1);
    MultiByteToWideChar(CP_UTF8, 0, cmdLine.c_str(), (int)cmdLine.size(), wcmd.data(), wlen);
    wcmd[wlen] = L'\0';

    std::vector<wchar_t> wcwd;
    if (!p.dir.empty()) {
        std::string cs = p.dir.string();
        int clen = MultiByteToWideChar(CP_UTF8, 0, cs.c_str(), (int)cs.size(), nullptr, 0);
        wcwd.resize(clen + 1);
        MultiByteToWideChar(CP_UTF8, 0, cs.c_str(), (int)cs.size(), wcwd.data(), clen);
        wcwd[clen] = L'\0';
    }

    STARTUPINFOW si; ZeroMemory(&si, sizeof(si)); si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdInput  = hR1; si.hStdOutput = hW2;
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof(pi));
    if (!CreateProcessW(nullptr, wcmd.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr,
                        wcwd.empty() ? nullptr : wcwd.data(), &si, &pi)) {
        std::cerr << "[persist] CreateProcessW 失败 err=" << GetLastError() << " cmd=" << cmdLine << "\n";
        CloseHandle(hR1); CloseHandle(hW1); CloseHandle(hR2); CloseHandle(hW2);
        return false;
    }
    CloseHandle(hR1); CloseHandle(hW2); CloseHandle(pi.hThread);

    auto pp = std::make_unique<PersistentProc>();
    pp->name = p.name; pp->hProcess = pi.hProcess;
    pp->hStdinWrite = hW1; pp->hStdoutRead = hR2;
    { std::lock_guard<std::mutex> lk(g_persistent_mtx); g_persistent[p.name] = std::move(pp); }
    return true;
#else
    int sin[2], sout[2];
    if (pipe(sin) < 0 || pipe(sout) < 0) return false;
    pid_t pid = fork();
    if (pid < 0) { close(sin[0]); close(sin[1]); close(sout[0]); close(sout[1]); return false; }
    if (pid == 0) {
        close(sin[1]); close(sout[0]);
        dup2(sin[0], STDIN_FILENO); dup2(sout[1], STDOUT_FILENO);
        close(sin[0]); close(sout[1]);
        if (!p.dir.empty()) fs::current_path(p.dir);
        std::string pyCmd = "python -u '" + runnerPath.string() + "' '" + scriptPath.string() + "'";
        execl("/bin/sh", "sh", "-c", pyCmd.c_str(), nullptr);
        _exit(1);
    }
    close(sin[0]); close(sout[1]);
    auto pp = std::make_unique<PersistentProc>();
    pp->name = p.name; pp->pid = pid; pp->stdin_fd = sin[1]; pp->stdout_fd = sout[0];
    { std::lock_guard<std::mutex> lk(g_persistent_mtx); g_persistent[p.name] = std::move(pp); }
    return true;
#endif
}

// 读管道直到换行符（简单行协议）
static std::string read_pipe_line(PersistentProc& pp) {
    std::string buf;
#ifdef _WIN32
    char ch; DWORD rd;
    while (ReadFile(pp.hStdoutRead, &ch, 1, &rd, nullptr) && rd == 1) {
        if (ch == '\n') break;
        buf += ch;
    }
#else
    char ch;
    while (read(pp.stdout_fd, &ch, 1) > 0) {
        if (ch == '\n') break;
        buf += ch;
    }
#endif
    return buf;
}

// 通过持久进程执行一次钩子调用（失败返回 false，调用方回退一次性子进程）
static bool run_hook_persistent(const Plugin& p, const PluginHook& h, const std::string& hook,
                                const json& ctx, json* outJson) {
    std::string key = p.name;
    PersistentProc* pp = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_persistent_mtx);
        auto it = g_persistent.find(key);
        if (it != g_persistent.end()) pp = it->second.get();
    }
    if (!pp) {
        // 首次调用：spawn 持久进程
        if (!spawn_persistent(p, h)) return false;
        std::lock_guard<std::mutex> lk(g_persistent_mtx);
        auto it = g_persistent.find(key);
        if (it == g_persistent.end()) return false;
        pp = it->second.get();
    }
    std::lock_guard<std::mutex> lk(pp->mtx);

    json req;
    req["hook"] = hook; req["ctx"] = ctx;
    std::string line = req.dump() + "\n";

#ifdef _WIN32
    DWORD wr;
    if (!WriteFile(pp->hStdinWrite, line.c_str(), (DWORD)line.size(), &wr, nullptr))
        return false;
#else
    if (write(pp->stdin_fd, line.c_str(), line.size()) < 0) return false;
#endif

    std::string resp = read_pipe_line(*pp);
    if (resp.empty()) return false;

    try {
        json out = json::parse(resp);
        if (outJson) *outJson = std::move(out);
        return true;
    } catch (...) { return false; }
}

// 终止所有持久进程：关闭 stdin → 等 3s 优雅退出 → 强杀
static void terminate_all_persistent() {
    std::lock_guard<std::mutex> lk(g_persistent_mtx);
#ifdef _WIN32
    for (auto& kv : g_persistent) {
        auto& pp = *kv.second;
        CloseHandle(pp.hStdinWrite);
        WaitForSingleObject(pp.hProcess, 3000);
        TerminateProcess(pp.hProcess, 0);
        CloseHandle(pp.hProcess);
        CloseHandle(pp.hStdoutRead);
    }
#else
    for (auto& kv : g_persistent) {
        auto& pp = *kv.second;
        close(pp.stdin_fd);
        usleep(3000000);
        int st;
        if (waitpid(pp.pid, &st, WNOHANG) == 0)
            kill(pp.pid, SIGTERM);
        close(pp.stdout_fd);
    }
#endif
    g_persistent.clear();
}

// 跨平台 subprocess：执行 cmdLine，可选超时。返回进程退出码（0=成功，-1=启动失败，124=超时）
// cwd 非空时以该目录为子进程工作目录：Windows 走 lpCurrentDirectory（不碰全局 cwd，线程安全，
// 支持插件并行执行）；POSIX 无等效参数，用锁串行化 chdir（构建场景可接受）。
int run_subprocess(const std::string& cmdLine, int timeoutSec, const fs::path* cwd = nullptr) {
#ifdef _WIN32
    // Windows：CreateProcessW（宽字符，兼容中文路径）+ 可控超时 + 可杀进程
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    // 转 UTF-16：lpCommandLine 要求可写缓冲区
    int wlen = MultiByteToWideChar(CP_UTF8, 0, cmdLine.c_str(), (int)cmdLine.size(), nullptr, 0);
    std::vector<wchar_t> wcmd(wlen + 1);
    MultiByteToWideChar(CP_UTF8, 0, cmdLine.c_str(), (int)cmdLine.size(), wcmd.data(), wlen);
    wcmd[wlen] = L'\0';
    // 可选工作目录：转 UTF-16 后作为 lpCurrentDirectory 传入（线程安全，不修改全局 cwd）
    std::vector<wchar_t> wcwd;
    if (cwd && !cwd->empty()) {
        std::string cs = cwd->string();
        int clen = MultiByteToWideChar(CP_UTF8, 0, cs.c_str(), (int)cs.size(), nullptr, 0);
        wcwd.resize(clen + 1);
        MultiByteToWideChar(CP_UTF8, 0, cs.c_str(), (int)cs.size(), wcwd.data(), clen);
        wcwd[clen] = L'\0';
    }
    LPCWSTR lpCwd = wcwd.empty() ? nullptr : wcwd.data();
    if (!CreateProcessW(nullptr, wcmd.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, lpCwd, &si, &pi)) {
        return -1;
    }
    DWORD waitMs = (timeoutSec > 0) ? (DWORD)timeoutSec * 1000 : INFINITE;
    DWORD wr = WaitForSingleObject(pi.hProcess, waitMs);
    DWORD code = 124;
    if (wr == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 124);          // 超时杀进程
        WaitForSingleObject(pi.hProcess, 5000);
    } else {
        GetExitCodeProcess(pi.hProcess, &code);
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)code;
#else
    // POSIX：system 简化（构建场景可接受）；信号等待退出码
    // cwd 切换是进程级全局操作，并行调用时用锁串行化（构建脚本量级，可接受）
    static std::mutex posixCwdMutex;
    std::lock_guard<std::mutex> cwdLk(posixCwdMutex);
    fs::path saved;
    if (cwd && !cwd->empty()) { saved = fs::current_path(); fs::current_path(*cwd); }
    int rc = std::system(cmdLine.c_str());
    // Linux 常见「只有 python3 没有 python」：cmd 以 "python " 开头且 command not found(127) 时，
    // 自动回退 python3 重试（对 Windows 分支无影响，其他插件不受影响）。
    if (rc == 127 && cmdLine.rfind("python ", 0) == 0) {
        std::string alt = "python3" + cmdLine.substr(6);
        rc = std::system(alt.c_str());
    }
    if (!saved.empty()) fs::current_path(saved);
    if (rc < 0) return -1;
    if (WIFEXITED(rc)) return WEXITSTATUS(rc);
    return -1;
#endif
}

// 从 plugin.json 解析一个插件；解析失败返回 false
bool load_plugin(const fs::path& mf) {
    try {
        json j = json::parse(read_file(mf));
        Plugin p;
        p.name = j.value("name", mf.parent_path().filename().string());
        p.dir = mf.parent_path();
        if (j.contains("hooks") && j["hooks"].is_object()) {
            for (auto it = j["hooks"].begin(); it != j["hooks"].end(); ++it) {
                PluginHook h;
                if (it.value().is_string()) {
                    h.cmd = it.value().get<std::string>();
                } else if (it.value().is_object()) {
                    h.cmd   = it.value().value("cmd", "");
                    h.timeout = it.value().value("timeout", 30);
                }
                if (!h.cmd.empty()) p.hooks[it.key()] = h;
            }
        }
        if (!p.hooks.empty()) {
            g_plugins.push_back(std::move(p));
            if (g_verbose)
                std::cout << color::muted("  · 插件注册: ") << p.name << "\n";
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << color::yellow("  ⚠ 插件清单解析失败: ") << mf
                  << " (" << e.what() << ")\n";
        return false;
    }
}

} // namespace

void plugins_scan_all() {
    g_plugins.clear();
    g_scanned = true;
    fs::path pdir = g_engine / "plugins";
    std::error_code ec;
    if (!fs::exists(pdir, ec)) return;
    for (const auto& e : fs::directory_iterator(pdir, ec)) {
        if (!e.is_directory(ec)) continue;
        fs::path mf = e.path() / "plugin.json";
        if (fs::exists(mf, ec)) load_plugin(mf);
    }
}

bool plugins_any() {
    return g_scanned && !g_plugins.empty();
}

bool plugins_hook_registered(const std::string& hook) {
    if (!g_scanned) return false;
    for (const auto& p : g_plugins)
        if (p.hooks.count(hook)) return true;
    return false;
}

void run_plugin_hooks(const std::string& hook, const json& ctx,
                      std::vector<json>* outs) {
    if (!g_scanned) return;                 // 未初始化（非 build 路径）直接跳过
    if (g_plugins.empty()) return;          // 无插件，零开销

    // 临时交换目录：<engine>/.build/plugins/（绝对路径——子进程 cwd 是插件目录，
    // 相对路径会被解析到插件目录下导致读写失败）
    std::error_code ec;
    fs::path swapDir = fs::absolute(g_engine / ".build" / "plugins");
    fs::create_directories(swapDir, ec);

    // 收集匹配该钩子的插件（各插件 ctx/out 文件相互独立，可并行执行）
    std::vector<const Plugin*> matches;
    for (const auto& p : g_plugins)
        if (p.hooks.count(hook)) matches.push_back(&p);
    if (matches.empty()) return;

    // 插件输出摘要与注入结果收集：并发下用锁保护（outs 顺序无保证，调用方按 key 消费）
    std::mutex ioMutex;
    auto run_one = [&](const Plugin& p) {
        const PluginHook& h = p.hooks.at(hook);

        // 持久进程优先（仅 Windows）：Python 插件 spawn 后 stdin/stdout 复用
#ifdef _WIN32
        bool isPython = (h.cmd.rfind("python ", 0) == 0 || h.cmd.rfind("python3 ", 0) == 0);
        if (isPython) {
            if (!g_quiet) {
                std::lock_guard<std::mutex> lk(ioMutex);
                std::cout << color::muted("  · 插件 ") << color::cyan(p.name)
                          << color::muted(" → ") << hook << "\n";
            }
            auto t0 = std::chrono::steady_clock::now();
            json out;
            if (run_hook_persistent(p, h, hook, ctx, &out)) {
                std::cerr << "[perf] 插件 " << p.name << " " << hook << " persist "
                          << std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() << "s\n";
                if (outs) { std::lock_guard<std::mutex> lk(ioMutex); outs->push_back(std::move(out)); }
                return;
            }
            // 失败回退：持久进程可能崩溃/超时，走常规子进程路径
        }
#endif

        // 一次性子进程（持久未启用或失败回退）
        std::error_code lec;
        fs::path ctxPath = swapDir / (p.name + "." + hook + ".ctx.json");
        fs::path outPath = swapDir / (p.name + "." + hook + ".out.json");
        fs::remove(outPath, lec);
        {
            std::ofstream f(ctxPath);
            if (!f) {
                std::lock_guard<std::mutex> lk(ioMutex);
                std::cerr << color::yellow("  ⚠ 插件 ") << p.name
                          << ": 无法写入上下文 " << ctxPath << "\n";
                return;
            }
            f << ctx.dump(2);
        }

        if (!g_quiet) {
            std::lock_guard<std::mutex> lk(ioMutex);
            std::cout << color::muted("  · 插件 ") << color::cyan(p.name)
                      << color::muted(" → ") << hook << "\n";
        }
        std::string cmdLine = h.cmd + " \"" + ctxPath.string() + "\" \"" + outPath.string() + "\"";
        auto tSpawn = std::chrono::steady_clock::now();
        int rc = run_subprocess(cmdLine, h.timeout, &p.dir);
        auto tSpawnEnd = std::chrono::steady_clock::now();
        std::cerr << "[perf] 插件 " << p.name << " " << hook << " 子进程 "
                  << std::chrono::duration<double>(tSpawnEnd - tSpawn).count() << "s rc=" << rc << "\n";

        if (rc != 0) {
            std::lock_guard<std::mutex> lk(ioMutex);
            std::cerr << color::yellow("  ⚠ 插件 ") << p.name << " (" << hook
                      << ") 执行失败 exit=" << rc << "（已忽略，构建继续）\n";
            return;
        }

        // 读结果 JSON
        if (fs::exists(outPath, lec)) {
            try {
                json out = json::parse(read_file(outPath));
                bool ok = out.value("ok", true);
                std::string msg = out.value("message", "");
                if (!ok) {
                    std::lock_guard<std::mutex> lk(ioMutex);
                    std::cerr << color::yellow("  ⚠ 插件 ") << p.name << ": " << msg << "\n";
                } else if (!msg.empty() && !g_quiet) {
                    std::lock_guard<std::mutex> lk(ioMutex);
                    std::cout << color::green("  ✓ ") << p.name << color::muted(": ") << msg << "\n";
                }
                if (outs) {
                    std::lock_guard<std::mutex> lk(ioMutex);
                    outs->push_back(std::move(out));
                }
            } catch (...) { /* out.json 非 JSON 时忽略 */ }
        }
    };

    // worker pool：互不依赖的插件并行执行（≤硬件并发；单插件时自然串行）
    size_t n = matches.size();
    unsigned hw = std::thread::hardware_concurrency();
    unsigned nThreads = (hw <= 1) ? 1u : std::min<unsigned>(hw, (unsigned)n);
    std::cerr << "[perf] hook=" << hook << " matches=" << n << " hw=" << hw << " threads=" << nThreads << "\n";
    auto tPool0 = std::chrono::steady_clock::now();
    std::atomic<size_t> next{0};
    std::vector<std::thread> pool;
    pool.reserve(nThreads);
    for (unsigned t = 0; t < nThreads; ++t)
        pool.emplace_back([&] {
            for (;;) {
                size_t i = next.fetch_add(1, std::memory_order_relaxed);
                if (i >= n) break;
                run_one(*matches[i]);
            }
        });
    for (auto& th : pool) th.join();
    std::cerr << "[perf] hook=" << hook << " 批次总耗时 "
              << std::chrono::duration<double>(std::chrono::steady_clock::now() - tPool0).count() << "s\n";
}

void plugins_terminate_all() {
    terminate_all_persistent();
}
