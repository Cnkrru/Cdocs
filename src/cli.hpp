// cli.hpp —— 命令分发入口

#ifndef CDOCS_CLI_HPP
#define CDOCS_CLI_HPP

#include <string>
#include <vector>
#include "core.hpp"

// 显示版本号
void print_version();

// 显示总帮助（表驱动）
void print_help();

// 执行单条命令。args[0] 为命令名。
// 退出码：0=成功，1=运行错误，2=用法错误（未知命令 / 缺参数 / 未知旗标）。
int run_command(std::vector<std::string> args);

#endif  // CDOCS_CLI_HPP
