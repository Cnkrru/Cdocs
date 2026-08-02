// cli.hpp —— 全局旗标解析与子命令分发（纯代码搬迁自原 main.cpp）

#ifndef CDOCS_CLI_HPP
#define CDOCS_CLI_HPP

#include <string>
#include <vector>
#include "core.hpp"

// 显示版本号
void print_version();

// 显示总帮助
void print_help();

// 显示子命令帮助
void print_subcommand_help(const std::string& cmd);

// 解析子命令之前的全局旗标（对标 Hugo/MkDocs：全局旗标放在子命令前）。
// earlyExit >= 0 表示已处理并应直接退出（help/version/用法错误）；否则返回剩余参数。
std::vector<std::string> parse_global_flags(std::vector<std::string>& args, int& earlyExit);

// 执行单条命令。args[0] 为命令名。
// 退出码：0=成功，1=运行错误，2=用法错误（未知命令 / 缺参数 / 未知旗标）。
int run_command(std::vector<std::string> args);

#endif  // CDOCS_CLI_HPP
