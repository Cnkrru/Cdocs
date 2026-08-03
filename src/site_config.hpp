// site_config.hpp —— 站点配置加载（config.json + 路由映射 + 侧边栏导航）
#ifndef CDOCS_SITE_CONFIG_HPP
#define CDOCS_SITE_CONFIG_HPP

#include "core.hpp"
#include "builder.hpp"   // BuildContext

// 载入站点配置：config.json（site/head/center/footer）+ site.route 路由映射 + 侧边栏导航
void load_site_config(BuildContext& b);

#endif  // CDOCS_SITE_CONFIG_HPP
