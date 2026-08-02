// search.cpp —— 搜索索引（search.json）生成
//
// 索引增强（行业最佳实践）：
//   - 字段化：title（权重最高）+ content（正文）+ tags（标签可命中）+ file
//   - content 保留更长（800 字符），前端可据此定位命中词生成"命中上下文"摘要
//   - 草稿按 includeDrafts 过滤；标题走 i18n 字典解析

#include "search.hpp"
#include "i18n.hpp"   // i18n_replace

void gen_search_index(const std::vector<Page>& pages, bool includeDrafts,
                      const I18nDict& dict, const fs::path& locOut) {
    // 每语言独立：内容取自该语言正文；标题走 i18n 字典解析
    json idx = json::array();
    for (const auto& p : pages) {
        if (p.draft && !includeDrafts) continue;
        json item;
        item["title"]   = i18n_replace(p.title, dict);
        item["file"]    = p.file + ".html";
        std::string content = collapse_ws(strip_tags(p.html));
        if (content.size() > 800) content = truncate_utf8(content, 800) + "…";
        item["content"] = content;
        item["excerpt"] = p.desc;
        item["tags"]    = p.tags;   // 标签字段：可被搜索命中，前端展示徽标
        idx.push_back(item);
    }
    std::ofstream(locOut / "assets/search.json") << idx.dump(2, ' ', false, json::error_handler_t::replace);
}
