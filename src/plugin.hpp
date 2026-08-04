// plugin.hpp —— 外部脚本插件系统：Hook 调度器 + subprocess 调用
//
// 设计（方案 C：外部进程 + JSON 文件交换，语言无关、零 dll、不破坏单 exe 卖点）：
//   - 插件 = .Cdocs/plugins/<name>/ 目录，含 plugin.json（声明钩子与命令）+ scripts/（任意语言脚本）
//   - 主程序在构建管线关键节点广播钩子：on_config / on_page_collected / on_page_rendered / on_done
//   - 调用方式：subprocess 启动脚本，传入两个路径参数：
//       <cmd> <ctx.json 路径> <out.json 路径>
//     上下文 JSON 由主程序写入，脚本处理后把结果 JSON 写到 out.json
//   - 失败隔离：脚本崩溃 / 超时 / 返回非 0 仅打印警告，不阻断构建

#ifndef CDOCS_PLUGIN_HPP
#define CDOCS_PLUGIN_HPP

#include "core.hpp"

// 扫描 g_engine/plugins/*/plugin.json，构建插件注册表（build 前调用一次）
void plugins_scan_all();

// 执行所有注册在指定钩子上的插件。ctx 为上下文 JSON（发给脚本 stdin 对应物——文件）。
// 任何插件失败都不抛出、不终止构建。
// outs 非空时：收集每个成功插件写的 out.json（JSON 对象），供调用方消费（如 on_config 的注入片段）。
void run_plugin_hooks(const std::string& hook, const json& ctx,
                      std::vector<json>* outs = nullptr);

// 是否有已注册插件（有插件时页面渲染退化单线程，保证 on_page_rendered 时序与安全）
bool plugins_any();

// 是否有插件注册了指定钩子（比 plugins_any 精确：无插件订阅该钩子时，
// 渲染流程可保持并行，不必因"存在插件"整体退化为单线程）
bool plugins_hook_registered(const std::string& hook);

// 终止所有持久插件进程（构建结束时调用，关闭 stdin → 进程优雅退出）
void plugins_terminate_all();

#endif  // CDOCS_PLUGIN_HPP
