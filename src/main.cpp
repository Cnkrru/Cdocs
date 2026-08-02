// main.cpp —— Cdocs 薄转发层（纯代码搬迁重构后）
//
// 本 TU 仅：
//   1) 定义并拥有全部全局状态（其余模块用 core.hpp 的 extern 引用）
//   2) 定义 main()：初始化 → 解析全局旗标 → 分发子命令
// 所有函数/类型定义均已迁往各模块（core / frontmatter / i18n / config /
// pages / feeds / pwa / search / server / builder / cli）。
// 纯代码搬迁：逻辑、字符串、控制流、输出均与原单体 main.cpp 保持一致。

#include "core.hpp"
#include "frontmatter.hpp"
#include "i18n.hpp"
#include "config.hpp"
#include "pages.hpp"
#include "feeds.hpp"
#include "pwa.hpp"
#include "search.hpp"
#include "server.hpp"
#include "builder.hpp"
#include "cli.hpp"

// ---------------- 全局状态（本 TU 拥有其定义） ----------------

volatile std::sig_atomic_t g_serve_running = 1;
volatile std::sig_atomic_t g_idle_running  = 1;

fs::path g_source   = "md";      // -s/--source：Markdown 源目录
fs::path g_dest     = "dist";    // -d/--dest：输出目录（serve 也用作预览根）
fs::path g_engine   = ".Cdocs";  // -c/--config：引擎/配置根目录
bool     g_quiet    = false;     // -q/--quiet：静默输出
bool     g_verbose  = false;     // -V/--verbose：详细输出
bool     g_include_drafts = false;// build -D/--drafts：包含草稿
bool     g_clean_before   = false;// build --clean：构建前清空输出
bool     g_incremental    = false;// serve -w：增量构建（仅 watch 循环置位）
std::string g_cur_version;         // 多版本构建：当前版本名（空 = 单版本）
std::string g_cur_version_label;   // 多版本构建：当前版本显示名
std::string g_versions_json;       // 多版本构建：完整版本列表（JSON 数组）
std::map<std::string, std::string> g_body_ends;  // 正文末尾注入（插件 on_config 提供，key=语言 → HTML）
std::vector<std::string> g_i18n_missing;   // i18n 未命中键收集（run_build 开头清空、末尾告警）
std::vector<std::string> g_link_broken;    // 死链收集（页面 → 不存在的站内目标，末尾告警）
std::map<std::string, std::string> g_fp;   // 资源指纹 map（assets/css/style.css → 8hex 哈希）

int main(int argc, char** argv) {
#ifdef _WIN32
    // Windows 控制台默认 GBK(936)，源码为 UTF-8，切到 UTF-8 输出避免中文乱码
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    color::init();   // 终端颜色：检测 TTY / 平台能力，决定输出转义序列

    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) args.push_back(argv[i]);

    // 先解析子命令前的全局旗标（-c/-s/-d/-q/-V 等）
    int early = -1;
    std::vector<std::string> rest = parse_global_flags(args, early);
    if (early >= 0) return early;

    // 纯子命令 CLI（对标 Hugo / MkDocs）：无参数时打印帮助并退出。
    // 双击 exe（父进程是 explorer）为避免窗口一闪而过，等待用户按 Ctrl+C 关闭
    // （行业惯例：Ctrl+C 退出，不响应回车键）。
    if (rest.empty()) {
        print_help();
        if (launched_by_doubleclick()) {
            std::signal(SIGINT,  idle_signal_handler);
            std::signal(SIGTERM, idle_signal_handler);
            std::cout << color::muted("\n按 Ctrl+C 退出…");
            std::cout.flush();
            while (g_idle_running)
                std::this_thread::sleep_for(std::chrono::milliseconds(120));
            std::cout << color::muted(" 已退出。\n");
        }
        return 0;
    }
    return run_command(rest);
}
