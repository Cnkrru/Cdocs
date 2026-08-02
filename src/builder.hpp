// builder.hpp —— 构建编排（run_build）与站点生命周期命令（init / add / clean）

#ifndef CDOCS_BUILDER_HPP
#define CDOCS_BUILDER_HPP

#include "core.hpp"

// 构建静态站点（默认 docs → dist）
int run_build(fs::path in_dir, fs::path out_dir, bool includeDrafts, bool cleanBefore);

// init：在指定目录创建一个完整的新站点（对标 hugo new site）
//   useDefaults=true 时跳过交互询问（默认：文档 + 博客，不带历史版本）
int cmd_init(fs::path dir, bool copyExe, bool useDefaults = false);

// section：添加一个内容区文件夹（分类），名字限制 blog/docs/docs-v<数字>；
//          blog 只能存在一份（已存在则拒绝）。
int cmd_section(const std::string& name);

// add：新建一篇文档（对标 hugo new content/...），并自动登记到 route.json 导航
int cmd_add(const std::string& name);

// clean：清空输出目录（对标 jekyll clean / docusaurus clear）
int cmd_clean();

#endif  // CDOCS_BUILDER_HPP
