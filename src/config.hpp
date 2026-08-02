// config.hpp —— config.json / route.json 中链接与导航节点的解析

#ifndef CDOCS_CONFIG_HPP
#define CDOCS_CONFIG_HPP

#include "core.hpp"

Link parse_link(const json& j);
NavNode parse_nav(const json& j);

#endif  // CDOCS_CONFIG_HPP
