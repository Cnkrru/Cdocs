// builder.hpp —— 构建编排（run_build）与站点生命周期命令（init / add / clean）

#ifndef CDOCS_BUILDER_HPP
#define CDOCS_BUILDER_HPP

#include "core.hpp"

// 构建静态站点（默认 docs → dist）
int run_build(fs::path in_dir, fs::path out_dir, bool includeDrafts, bool cleanBefore);

// init：在指定目录创建一个完整的新站点（对标 hugo new site）
int cmd_init(fs::path dir, bool copyExe);

// add：新建一篇文档（对标 hugo new content/...），并自动登记到 route.json 导航
int cmd_add(const std::string& name);

// clean：清空输出目录（对标 jekyll clean / docusaurus clear）
int cmd_clean();

#endif  // CDOCS_BUILDER_HPP
