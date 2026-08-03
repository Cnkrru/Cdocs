// builder.hpp —— 构建编排（run_build）

#ifndef CDOCS_BUILDER_HPP
#define CDOCS_BUILDER_HPP

#include "core.hpp"

// 构建静态站点（默认 docs → dist）
int run_build(fs::path in_dir, fs::path out_dir, bool includeDrafts, bool cleanBefore);

#endif  // CDOCS_BUILDER_HPP
