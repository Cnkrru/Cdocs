// diag.hpp —— 诊断命令（doctor / check / config / routes）
#ifndef CDOCS_DIAG_HPP
#define CDOCS_DIAG_HPP

#include "core.hpp"

// doctor：环境与配置自检（版本/config/内容区/主题/工具探测）
int cmd_doctor();

// check：站点质量检查（死链 + 组件 token 残留 + 未渲染数据孔）
int cmd_check();

// config：打印解析后的配置摘要（诊断用）
int cmd_config();

// routes：列出站点页面路由清单（route 登记 → 输出 URL）
int cmd_routes();

// theme：列出可用主题（themes/ 目录）+ 当前生效主题
int cmd_theme();

// plugins：列出已注册插件（.Cdocs/plugins/）+ 各自钩子
int cmd_plugins();

// versions：列出配置的版本（site.versions；未配置时扫描 md-* 快照约定）
int cmd_versions();

#endif  // CDOCS_DIAG_HPP
