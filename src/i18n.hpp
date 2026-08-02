// i18n.hpp —— 多语言（i18n）字典替换与序列化

#ifndef CDOCS_I18N_HPP
#define CDOCS_I18N_HPP

#include "core.hpp"

// i18n：把 {{key}} 占位符替换为字典中的值（行业标准的 {{}}+json 写法）。
std::string i18n_replace(const std::string& s, const I18nDict& dict);

// 把字典序列化为 JSON 字符串，注入页面供 app.js 读取（客户端 UI 文案随之本地化）
std::string dict_to_json(const I18nDict& dict);

// 把值里的动态令牌（如 {{minutes}}）替换为实际数字/文本
std::string subst_tokens(std::string s, const std::vector<std::pair<std::string,std::string>>& kv);

#endif  // CDOCS_I18N_HPP
