// i18n.cpp —— 多语言字典替换实现（自 main.cpp 原样搬迁）

#include "i18n.hpp"
#include <mutex>

// g_i18n_missing 会被并发渲染的多个工作线程同时 push（Hugo 式并行构建），用互斥保护
static std::mutex g_missing_mutex;

// i18n：把 {{key}} 占位符替换为字典中的值（行业标准的 {{}}+json 写法）。
// 会跳过 <pre>/<code>/<script>/<style> 区块：
//   - <pre>/<code>：避免把示例代码里的 {{}} 也替换掉（展示语法时很关键）
//   - <script>/<style>：避免误改注入的 i18n 字典（window.__I18N__）与样式
std::string i18n_replace(const std::string& s, const I18nDict& dict) {
    static const char* skip_tags[] = { "<pre", "<code", "<script", "<style" };
    std::string out; out.reserve(s.size());
    size_t i = 0, n = s.size();
    while (i < n) {
        bool skipped = false;
        for (const char* tag : skip_tags) {            // 跳过三类区块
            size_t tl = std::strlen(tag);
            if (i + tl <= n && s.compare(i, tl, tag) == 0) {
                size_t close = s.find("</" + std::string(tag + 1) + ">", i);
                if (close == std::string::npos) { out += s.substr(i); i = n; }
                else { size_t end = close + std::strlen(tag + 1) + 3; out += s.substr(i, end - i); i = end; }
                skipped = true; break;
            }
        }
        if (skipped) continue;
        if (s.compare(i, 2, "{{") == 0) {
            size_t close = s.find("}}", i);
            if (close == std::string::npos) { out += s.substr(i); break; }
            std::string key = s.substr(i + 2, close - (i + 2));
            size_t a = 0, b = key.size();
            while (a < b && std::isspace((unsigned char)key[a])) a++;
            while (b > a && std::isspace((unsigned char)key[b - 1])) b--;
            key = key.substr(a, b - a);
            auto it = dict.find(key);
            if (it != dict.end()) out += it->second;     // 命中则替换
            else {
                // 未命中：保留原样，并收集键供构建末尾告警（动态令牌由 subst_tokens 事后替换，白名单排除）
                static const char* dyn_tokens[] = { "minutes", "words" };
                bool dynamic = false;
                for (const char* t : dyn_tokens) if (key == t) { dynamic = true; break; }
                if (!dynamic) { std::lock_guard<std::mutex> lk(g_missing_mutex); g_i18n_missing.push_back(key); }
                out += s.substr(i, close + 2 - i);
            }
            i = close + 2;
            continue;
        }
        out += s[i++];
    }
    return out;
}

// 把字典序列化为 JSON 字符串，注入页面供 app.js 读取（客户端 UI 文案随之本地化）
std::string dict_to_json(const I18nDict& dict) {
    json j = json::object();
    for (const auto& kv : dict) j[kv.first] = kv.second;
    return j.dump(2, ' ', false, json::error_handler_t::replace);
}

// 把值里的动态令牌（如 {{minutes}}）替换为实际数字/文本
std::string subst_tokens(std::string s, const std::vector<std::pair<std::string,std::string>>& kv) {    for (auto& pr : kv) {
        std::string tok = "{{" + pr.first + "}}";
        size_t pos = 0;
        while ((pos = s.find(tok, pos)) != std::string::npos) {
            s.replace(pos, tok.size(), pr.second);
            pos += pr.second.size();
        }
    }
    return s;
}
