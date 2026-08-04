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
#include <windows.h>   // CreateProcessA / WaitForSingleObject（core.hpp 已含，显式再引）
#else
#include <sys/wait.h>
#endif
#include <thread>      // 并行执行互不依赖的插件（run_plugin_hooks worker pool）
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
        std::error_code lec;   // 并发 worker 各自持有，避免共享 ec 数据竞争

        // 1) 写上下文 JSON
        fs::path ctxPath = swapDir / (p.name + "." + hook + ".ctx.json");
        fs::path outPath = swapDir / (p.name + "." + hook + ".out.json");
        fs::remove(outPath, lec);                      // 清掉上次结果
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

        // 2) 执行脚本：<cmd> <ctx> <out>（工作目录 = 插件目录，便于相对路径引用脚本；
        //    Windows 走 lpCurrentDirectory，不修改全局 cwd → 多插件并行安全）
        if (!g_quiet) {
            std::lock_guard<std::mutex> lk(ioMutex);
            std::cout << color::muted("  · 插件 ") << color::cyan(p.name)
                      << color::muted(" → ") << hook << "\n";
        }
        std::string cmdLine = h.cmd + " \"" + ctxPath.string() + "\" \"" + outPath.string() + "\"";
        int rc = run_subprocess(cmdLine, h.timeout, &p.dir);

        if (rc != 0) {
            std::lock_guard<std::mutex> lk(ioMutex);
            std::cerr << color::yellow("  ⚠ 插件 ") << p.name << " (" << hook
                      << ") 执行失败 exit=" << rc << "（已忽略，构建继续）\n";
            return;
        }

        // 3) 读结果 JSON 摘要（可选：脚本可写 {ok, message} 到 out.json）
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
                    outs->push_back(std::move(out));   // 供调用方消费（注入片段等）
                }
            } catch (...) { /* out.json 非 JSON 时忽略 */ }
        }
    };

    // worker pool：互不依赖的插件并行执行（≤硬件并发；单插件时自然串行）
    size_t n = matches.size();
    unsigned hw = std::thread::hardware_concurrency();
    unsigned nThreads = (hw <= 1) ? 1u : std::min<unsigned>(hw, (unsigned)n);
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
}
