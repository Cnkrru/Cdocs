// markdown.hpp —— Markdown 渲染（复用成熟组件 md4c）

#ifndef MARKDOWN_HPP
#define MARKDOWN_HPP

#include <string>

// 把一段 Markdown 文本转换成 HTML 字符串。
std::string markdown_to_html(const std::string& md);

// 构建期 Admonitions：把 `> [!type] 标题` 块引用转成 VitePress 风格 div.admonition
// （不依赖客户端 JS；未给内联标题时按语言用内置默认标题）。
std::string render_admonitions(const std::string& html, bool en);

#endif  // MARKDOWN_HPP
