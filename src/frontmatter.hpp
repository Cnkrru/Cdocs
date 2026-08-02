// frontmatter.hpp —— 文档头元数据（YAML front matter）解析

#ifndef CDOCS_FRONTMATTER_HPP
#define CDOCS_FRONTMATTER_HPP

#include "core.hpp"

// 文档头元数据（YAML front matter，最小实现）：title / date / draft / weight / tags /
// description（每页 SEO 描述）/ lastmod（修改时间）/ aliases（旧路径重定向）
struct FrontMatter {
    std::string title;
    std::string date;
    std::string description;   // 页面描述（优先于正文自动摘要，用于 meta description / 搜索 / 社交）
    std::string lastmod;       // 修改时间（YYYY-MM-DD，优先于文件 mtime）
    std::vector<std::string> aliases;  // 旧路径（生成重定向页，对标 Hugo aliases）
    bool draft = false;
    int weight = 0;
    std::vector<std::string> tags;
    bool hasTitle = false;
};

// 解析文档开头的 `--- ... ---` 块，返回元数据并把剥离后的正文写入 body_out。
// 无 front matter 时 body_out 等于原文、FrontMatter 全默认（向后兼容旧文档）。
FrontMatter parse_front_matter(const std::string& md, std::string& body_out);

#endif  // CDOCS_FRONTMATTER_HPP
