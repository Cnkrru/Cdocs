// output.hpp —— 构建产物收尾（构建期输出文件 + 汇总 + 残留检测）
#ifndef CDOCS_OUTPUT_HPP
#define CDOCS_OUTPUT_HPP

#include "core.hpp"

struct BuildContext;   // builder.hpp 定义

// 4) 多语言根 index.html 重定向到默认语言（单语言模式已在循环中生成）
void write_root_redirect(BuildContext& b);
// 5) 根目录额外生成默认语言 feed 与 PWA（供根 index.html 重定向页使用）
void write_root_feeds_pwa(BuildContext& b);
// 6) sitemap.xml（SEO 标配）：多语言列出全部语言 URL + hreflang 交替
void write_sitemap(BuildContext& b);
// 7) robots.txt（标准：允许抓取，附 sitemap 地址）
void write_robots(BuildContext& b);
// 8) 构建汇总输出（发布的文档数 + 文件清单 + 配置/插件摘要 + i18n/死链告警）
void print_summary(BuildContext& b);
// 9) L2 残留检测：扫描输出目录，三类模板残留（模板块/未解析数据键/未展开组件）显式警告
void scan_output_leftovers(const fs::path& outDir);

#endif  // CDOCS_OUTPUT_HPP
