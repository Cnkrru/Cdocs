// versions.hpp —— 多版本构建分派（Docusaurus 风格）
#ifndef CDOCS_VERSIONS_HPP
#define CDOCS_VERSIONS_HPP

#include "core.hpp"

// 版本探测 + 分派：config.site.versions 优先，未配置时自动扫描 in_dir 同级
// "<源目录名>-*" 快照目录（如 md/ 旁的 md-v1/）。命中多版本则对每个版本独立
// 构建到 out_dir/<name>/ 并生成根 index.html 重定向默认版本，返回 true；
// 单版本 / 子构建重入时不处理，返回 false（调用方走单版本主流程）。
bool dispatch_versions(fs::path in_dir, fs::path out_dir, bool includeDrafts, bool cleanBefore);

#endif  // CDOCS_VERSIONS_HPP
