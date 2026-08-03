// scaffold.hpp —— 站点脚手架命令（init / section / new / clean）
#ifndef CDOCS_SCAFFOLD_HPP
#define CDOCS_SCAFFOLD_HPP

#include "core.hpp"

// init <目录> [--no-engine] [--defaults]：新建完整站点骨架（含引擎资源与 Cdocs.exe）
int cmd_init(fs::path dir, bool copyExe, bool useDefaults = false);

// section <blog|docs|md-v<n>>：添加内容区（分类）
int cmd_section(const std::string& name);

// new/add/page <页面名>：新建内容页（从 archetype），并登记导航
int cmd_add(const std::string& name);

// clean：清空输出目录（对标 jekyll clean / docusaurus clear）
int cmd_clean();

#endif  // CDOCS_SCAFFOLD_HPP
