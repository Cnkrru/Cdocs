// markdown.cpp —— Markdown 渲染（复用成熟组件 md4c）
//
// 这里不再手写词法器，而是直接调用被 Qt 等众多项目使用的成熟 Markdown
// 引擎 md4c（.Cdocs/deps/vendor/md4c）。md_html() 只生成 <body> 内部片段，
// 通过 process_output 回调把 HTML 拼进 std::string。
//
// 对外接口保持 std::string markdown_to_html(const std::string&) 不变，
// 所以 main.cpp / 页面模板一行都不用改，只是把"解析内核"换成了成熟库。

#include "markdown.hpp"

#include "core.hpp"     // strip_tags / collapse_ws / trim / esc

#include "md4c.h"
#include "md4c-html.h"

#include <string>

namespace {

void append_cb(const MD_CHAR* text, MD_SIZE size, void* data) {
    static_cast<std::string*>(data)->append(text, size);
}

// 图片懒加载：给 <img> 追加 loading="lazy"，减少首屏请求、防止布局抖动（CLS）。
// 跳过已在 <pre>/<code> 中的（已转义为 &lt;img&gt;，不会匹配），并避免重复添加。
std::string add_img_lazy(std::string s) {
    std::string out; out.reserve(s.size());
    size_t i = 0, n = s.size();
    while (i < n) {
        if (s.compare(i, 4, "<img") == 0) {
            size_t e = s.find('>', i);
            if (e == std::string::npos) { out += s.substr(i); break; }
            std::string tag = s.substr(i, e - i);
            if (tag.find("loading=") == std::string::npos)
                out += tag + " loading=\"lazy\"";
            else
                out += tag;
            out += '>';
            i = e + 1;
        } else {
            out += s[i]; ++i;
        }
    }
    return out;
}

// 上下标后处理：^x^ -> <sup>x</sup>，~x~ -> <sub>x</sub>。
// 跳过 <pre>/<code> 块（代码内容不应被转换）；~~x~~ 删除线已由 md4c 转成 <del>，不受影响。
std::string apply_subsup(std::string s) {
    std::string out; out.reserve(s.size());
    size_t i = 0, n = s.size();
    while (i < n) {
        if (s.compare(i, 4, "<pre") == 0) {
            size_t e = s.find("</pre>", i);
            size_t end = (e == std::string::npos) ? n : e + 6;
            out += s.substr(i, end - i); i = end; continue;
        }
        if (s.compare(i, 5, "<code") == 0) {
            size_t e = s.find("</code>", i);
            size_t end = (e == std::string::npos) ? n : e + 7;
            out += s.substr(i, end - i); i = end; continue;
        }
        char c = s[i];
        if (c == '^' || c == '~') {
            char closer = c;
            size_t j = i + 1;
            if (j < n && s[j] != closer && s[j] != ' ' && s[j] != '\t' && s[j] != '\n') {
                size_t k = j;
                while (k < n && s[k] != closer) {
                    if (s[k] == ' ' || s[k] == '\t' || s[k] == '\n') break;
                    ++k;
                }
                if (k < n && s[k] == closer && k > j) {
                    out += (closer == '^') ? "<sup>" : "<sub>";
                    out += s.substr(j, k - j);
                    out += (closer == '^') ? "</sup>" : "</sub>";
                    i = k + 1;
                    continue;
                }
            }
        }
        out += c;
        ++i;
    }
    return out;
}

}  // namespace

std::string markdown_to_html(const std::string& md) {
    std::string out;
    // 开启 GitHub 风格扩展：表格 / 删除线 / 任务列表 / 脚注，更接近常见文档站。
    unsigned parser_flags = MD_FLAG_TABLES | MD_FLAG_STRIKETHROUGH | MD_FLAG_TASKLISTS | MD_FLAG_FOOTNOTES;
    md_html(md.data(), static_cast<MD_SIZE>(md.size()),
            append_cb, &out, parser_flags, 0);
    return add_img_lazy(apply_subsup(out));
}

// ---------------- 构建期 Admonitions（VitePress 风格，对标 MkDocs Material） ----------------

// 在正文 HTML 上把 `> [!type] 标题` 块引用转成 div.admonition（客户端 admonitions.js 兜底，
// 转换后不再存在匹配的 <blockquote>，不会重复处理）。类型表与前端 ADM 保持一致。
std::string render_admonitions(const std::string& s, bool en) {
    struct Ad { const char* key; const char* cls; const char* icon; const char* zh; const char* en; };
    static const Ad AD[] = {
        {"note",      "note",    "sticky-note",    "提示", "Note"},
        {"info",      "info",    "info",           "信息", "Info"},
        {"tip",       "tip",     "lightbulb",      "技巧", "Tip"},
        {"success",   "success", "circle-check",   "成功", "Success"},
        {"example",   "success", "code",           "示例", "Example"},
        {"warning",   "warning", "triangle-alert", "警告", "Warning"},
        {"caution",   "warning", "octagon-alert",  "注意", "Caution"},
        {"danger",    "danger",  "flame",          "危险", "Danger"},
        {"bug",       "danger",  "bug",            "缺陷", "Bug"},
        {"important", "danger",  "circle-alert",   "重要", "Important"},
        {"question",  "info",    "circle-help",    "疑问", "Question"}
    };
    auto find_ad = [&](const std::string& t) -> const Ad* {
        for (const auto& a : AD) if (t == a.key) return &a;
        return nullptr;
    };

    std::string out;
    out.reserve(s.size() + 128);
    const size_t n = s.size();
    size_t i = 0;
    while (i < n) {
        if (s.compare(i, 11, "<blockquote") == 0) {
            size_t close = s.find("</blockquote>", i);
            if (close == std::string::npos) { out += s.substr(i); break; }
            size_t openEnd = s.find('>', i);
            std::string inner = s.substr(openEnd + 1, close - (openEnd + 1));
            size_t p0 = inner.find("<p>");
            size_t p1 = (p0 == std::string::npos) ? std::string::npos : inner.find("</p>", p0);
            if (p0 == std::string::npos || p1 == std::string::npos) {
                out += s.substr(i, close - i + 13);
                i = close + 13;
                continue;
            }
            std::string firstP = inner.substr(p0, p1 - p0);
            std::string text = collapse_ws(strip_tags(firstP));
            // 匹配 [!type]
            if (text.size() >= 4 && text[0] == '[' && text[1] == '!') {
                size_t rb = text.find(']');
                if (rb != std::string::npos) {
                    std::string type = text.substr(2, rb - 2);
                    const Ad* ad = find_ad(type);
                    if (ad) {
                        std::string inlineTitle = trim(text.substr(rb + 1));
                        std::string title = inlineTitle.empty() ? (en ? ad->en : ad->zh) : inlineTitle;
                        std::string body = inner.substr(p1 + 4);   // </p> 之后的剩余块引用内容
                        out += "<div class=\"admonition " + std::string(ad->cls) + "\">"
                               "<div class=\"adm-head\"><span class=\"icon icon-" + ad->icon
                               + " adm-icon\" aria-hidden=\"true\"></span><span class=\"adm-title\">"
                               + esc(title) + "</span></div><div class=\"adm-body\">" + body + "</div></div>";
                        i = close + 13;
                        continue;
                    }
                }
            }
            out += s.substr(i, close - i + 13);
            i = close + 13;
            continue;
        }
        out += s[i];
        ++i;
    }
    return out;
}
