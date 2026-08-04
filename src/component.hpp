// component.hpp —— 组件系统与站点数据（地图驱动引擎核心）
#ifndef CDOCS_COMPONENT_HPP
#define CDOCS_COMPONENT_HPP

#include "core.hpp"

// 主题根目录（g_engine/theme，缺失则 g_engine）
fs::path theme_root();

// 主题色（theme.json theme_color 字段，默认 #a8332a）
const std::string& theme_color();

// 警告去重（缺失/循环/深度，每键一次；正文渲染多线程安全）
bool comp_warned_once(const std::string& k);

// 加载组件：components/<Name>.html（根级优先，递归查找子目录）
std::string load_component(const std::string& name);

// 数据孔替换（纯文本）：{{a.b.c}} → data 路径取值；缺失原样保留
std::string fill_data_holes(const std::string& html, const json& data);

// 渲染单个组件实例（深度/循环检测 + 子 sections → {{slot}}）
std::string render_map_component(const std::string& name, const json& data,
                                 int depth, std::vector<std::string>& stack,
                                 const json* childSections);

// 遍历 JSON sections 数组（html / component / if / each / sections 五种约定）
std::string compose_sections(const json& sections, const json& data,
                             int depth, std::vector<std::string>& stack);

// 地图主入口：读 config/map.json 注册表 → theme/map/<name>.json（extends 继承）→ sections → 数据孔
std::string compose_page(const std::string& mapName, const json& data);

// 站点数据（.Cdocs/data/*.json 合并；双检锁，多线程安全）
const json& site_data();

// 预加载所有组件 HTML（构建启动时调用一次，消除每页递归文件搜索开销）
void preload_components();

#endif  // CDOCS_COMPONENT_HPP
