// color.hpp — Cdocs 内置的最小跨平台控制台着色库（vendored，无外部依赖）
//
// 目标：让命令行输出带 ANSI 颜色，且在「非终端（被管道/重定向）」或
//       「不支持 ANSI 的旧版 Windows 控制台」时自动关闭，保证日志干净。
//
// 用法（字符串包裹，自动复位，最省心）：
//   #include "color.hpp"
//   std::cout << color::cyan("Cdocs") << " " << color::green("v0.1.0") << "\n";
//   std::cout << color::error("出错了") << "\n";
//   std::cout << color::bold(color::yellow("警告")) << "\n";
//
// 进阶（流式组合，需手动复位）：
//   std::cout << color::BOLD << color::GREEN << "ok" << color::RESET << "\n";
//
// 环境变量 CDOCS_FORCE_COLOR=1 可强制开启（用于 CI / 测试 / 管道也要上色）。

#pragma once

#include <iostream>
#include <string>
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
  #include <io.h>
#else
  #include <unistd.h>
#endif

namespace color {

enum Code {
    RESET = 0,
    BOLD = 1,
    DIM = 2,
    ITALIC = 3,
    UNDERLINE = 4,
    BLACK = 30, RED = 31, GREEN = 32, YELLOW = 33,
    BLUE = 34, MAGENTA = 35, CYAN = 36, WHITE = 37,
    GRAY = 90, BRIGHT_RED = 91, BRIGHT_GREEN = 92,
    BRIGHT_YELLOW = 93, BRIGHT_BLUE = 94, BRIGHT_MAGENTA = 95, BRIGHT_CYAN = 96, BRIGHT_WHITE = 97,
    BG_BLACK = 40, BG_RED = 41, BG_GREEN = 42, BG_YELLOW = 43,
    BG_BLUE = 44, BG_MAGENTA = 45, BG_CYAN = 46, BG_WHITE = 47
};

inline bool& enabled_ref() {
    static bool e = false;
    return e;
}

inline bool enabled() { return enabled_ref(); }

// 初始化：检测 TTY 与平台能力，决定是否需要输出转义序列（只需调用一次）
inline void init() {
    static bool done = false;
    if (done) return;
    done = true;

    bool is_tty = false;
#ifdef _WIN32
    is_tty = (_isatty(_fileno(stdout)) != 0);
#else
    is_tty = (::isatty(fileno(stdout)) != 0);
#endif

    bool force = false;
    const char* fc = std::getenv("CDOCS_FORCE_COLOR");
    if (fc) {
        std::string v(fc);
        force = (v == "1" || v == "true" || v == "2");
    }

    if (!is_tty && !force) { enabled_ref() = false; return; }

#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode)) {
        // 开启虚拟终端处理，让 Windows 10+ 控制台识别 ANSI 转义
        SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
    // 非强制模式下，若 stdout 不是真实控制台（被重定向）则关闭颜色，
    // 避免出现裸转义码。
    if (!force && !is_tty) { enabled_ref() = false; return; }
#else
    (void)force;
#endif
    enabled_ref() = true;
}

// 流式组合用：color::BOLD << color::GREEN << "x" << color::RESET
inline std::ostream& operator<<(std::ostream& os, Code c) {
    if (enabled_ref()) os << "\x1b[" << static_cast<int>(c) << "m";
    return os;
}

inline std::string wrap(Code c, const std::string& s) {
    if (!enabled_ref()) return s;
    return "\x1b[" + std::to_string(static_cast<int>(c)) + "m" + s + "\x1b[0m";
}

// ---- 前景色 ----
inline std::string black(const std::string& s)   { return wrap(BLACK, s); }
inline std::string red(const std::string& s)     { return wrap(RED, s); }
inline std::string green(const std::string& s)    { return wrap(GREEN, s); }
inline std::string yellow(const std::string& s)   { return wrap(YELLOW, s); }
inline std::string blue(const std::string& s)     { return wrap(BLUE, s); }
inline std::string magenta(const std::string& s)  { return wrap(MAGENTA, s); }
inline std::string cyan(const std::string& s)     { return wrap(CYAN, s); }
inline std::string white(const std::string& s)    { return wrap(WHITE, s); }
inline std::string gray(const std::string& s)     { return wrap(GRAY, s); }

// ---- 样式 ----
inline std::string bold(const std::string& s)      { return wrap(BOLD, s); }
inline std::string dim(const std::string& s)       { return wrap(DIM, s); }
inline std::string underline(const std::string& s) { return wrap(UNDERLINE, s); }

// ---- 语义别名（让调用处意图更清晰）----
inline std::string success(const std::string& s) { return green(s); }
inline std::string error(const std::string& s)   { return red(s); }
inline std::string warn(const std::string& s)    { return yellow(s); }
inline std::string info(const std::string& s)     { return cyan(s); }
inline std::string muted(const std::string& s)    { return gray(s); }
inline std::string head(const std::string& s)     { return bold(s); }

} // namespace color
