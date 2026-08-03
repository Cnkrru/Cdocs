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

// routes：列出站点页面路由清单（sidebar 登记 → 输出 URL）
int cmd_routes();

#endif  // CDOCS_DIAG_HPP
