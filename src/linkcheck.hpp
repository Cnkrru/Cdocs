// linkcheck.hpp —— 死链检查（构建期）
//
// 对标 VitePress / MkDocs 的链接完整性校验：扫描每个已生成 HTML 页面的站内
// 相对链接（<a href> / <img src> / <link href> / <source srcset>），解析到输出
// 目录下的目标文件，不存在的记入 g_link_broken（末尾统一告警，不阻断构建）。
// 保护块（<pre>/<code>/<script>/<style>）内的链接跳过——那是文档展示的示例。
#pragma once

#include <filesystem>
namespace fs = std::filesystem;

// 检查一个语言输出目录下的全部 .html 页面；broken 目标收集进 g_link_broken（去重）。
void check_links(const fs::path& locOut, const std::string& loc);
