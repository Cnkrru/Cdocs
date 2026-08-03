// shortcode.hpp —— 正文 shortcode 引擎（<组件/> 标签语法）
#ifndef CDOCS_SHORTCODE_HPP
#define CDOCS_SHORTCODE_HPP

#include "core.hpp"

// 正文完整管线：shortcode 预扫描 → md4c → admonitions → shortcode 展开（文档级 style 去重）
std::string render_doc_body(const std::string& md, bool en);

#endif  // CDOCS_SHORTCODE_HPP
