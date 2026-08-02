// search.hpp —— 搜索索引（search.json）生成
//
// 说明：search.json 的生成原本内联在 builder.cpp 的 run_build() 多语言构建循环内
// （随每个语言目录 locOut 就地写出 assets/search.json，与 pages/tags 等阶段共享同一批
// 局部变量）。现已按「MOVE，不改写」原则抽取为独立函数 gen_search_index()，行为完全一致。

#ifndef CDOCS_SEARCH_HPP
#define CDOCS_SEARCH_HPP

#include "core.hpp"

// 生成每语言独立的搜索索引：内容取自该语言正文（strip + collapse + truncate），
// 标题走 i18n 字典解析，草稿按 includeDrafts 过滤。写出到 locOut/assets/search.json。
void gen_search_index(const std::vector<Page>& pages, bool includeDrafts,
                      const I18nDict& dict, const fs::path& locOut);

#endif  // CDOCS_SEARCH_HPP
