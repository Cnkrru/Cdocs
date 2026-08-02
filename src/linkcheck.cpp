// linkcheck.cpp —— 死链检查（构建期）
//
// 逐页扫描 <a href> / <img src> / <link href> / <source srcset> 等站内相对引用，
// 解析到输出目录 locOut 下的目标文件并核对存在性：
//   - 外链（http/https///）、mailto:/tel:/data:/javascript:、纯锚点（#xxx）跳过；
//   - 无扩展名链接按 <目标>.html、<目标>/index.html 补全尝试；
//   - 目标带锚点（x.html#sec）只核对页面存在；
//   - 页面所在子目录（blog/、tags/）用 ../ 回退解析；
//   - <pre>/<code>/<script>/<style> 内的引用跳过（示例代码，非真实链接）。
// 结果收集进 g_link_broken（去重），print_summary 末尾黄色告警，不阻断构建。

#include "linkcheck.hpp"
#include "core.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

namespace {

inline bool is_ws_char(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

std::string lower_str(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string tag_name_at(const std::string& s, size_t i) {
    size_t j = i + 1;
    std::string t;
    while (j < s.size() && (isalnum(static_cast<unsigned char>(s[j])) || s[j] == '-')) {
        t += static_cast<char>(std::tolower(static_cast<unsigned char>(s[j])));
        ++j;
    }
    return t;
}

bool is_protected_tag(const std::string& t) {
    return t == "pre" || t == "textarea" || t == "script" || t == "style";
}

// 提取标签内属性值：attr 属性名（小写，如 href/src/srcset），返回值或空
std::string attr_value(const std::string& tag, const char* attr) {
    std::string needle = std::string(attr) + "=";
    size_t p = 0;
    while (true) {
        size_t pos = tag.find(needle, p);
        if (pos == std::string::npos) return {};
        // 确认是完整属性名（前面是空白或标签边界）
        if (pos > 0 && !is_ws_char(tag[pos - 1]) && tag[pos - 1] != '<' && tag[pos - 1] != '"' &&
            tag[pos - 1] != '\'') { p = pos + 1; continue; }
        size_t v = pos + needle.size();
        if (v >= tag.size()) return {};
        if (tag[v] == '"' || tag[v] == '\'') {
            char q = tag[v];
            size_t e = tag.find(q, v + 1);
            return (e == std::string::npos) ? std::string() : tag.substr(v + 1, e - v - 1);
        }
        size_t e = tag.find_first_of(" \t\n", v);
        return tag.substr(v, (e == std::string::npos) ? tag.size() - v : e - v);
    }
}

// 规范化目标：去查询串与锚点，返回 (路径部分, 是否有锚点)
std::string strip_query_frag(const std::string& s) {
    size_t q = s.find_first_of("?#");
    return (q == std::string::npos) ? s : s.substr(0, q);
}

// 判断是否外链 / 特殊协议 / 纯锚点 / 空
bool skip_target(const std::string& t) {
    if (t.empty()) return true;
    if (t[0] == '#') return true;                       // 纯锚点（同页内部，暂不校验 id）
    if (t[0] == '/') return false;                      // 根相对：也校验（locOut 下）
    std::string lo = lower_str(t);
    if (lo.compare(0, 7, "http://") == 0 || lo.compare(0, 8, "https://") == 0 ||
        lo.compare(0, 2, "//") == 0) return true;       // 外链
    for (const char* p : {"mailto:", "tel:", "data:", "javascript:", "ftp:"})
        if (lo.compare(0, std::strlen(p), p) == 0) return true;
    return false;
}

// URL 百分号解码（%XX → 字节）。自动导航对中文文件名生成 %E5%9F%BA... 编码链接，
// 核对前须还原为中文路径，否则 fs::exists 找不到而误报死链。
static std::string url_decode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    auto hexv = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int h = hexv(s[i + 1]), l = hexv(s[i + 2]);
            if (h >= 0 && l >= 0) {
                out.push_back((char)((h << 4) | l));
                i += 2;
                continue;
            }
        }
        out.push_back(s[i]);
    }
    return out;
}

// 相对 locOut 解析：baseDir 是页面所在子目录（相对 locOut，如 "" 或 "blog"），
// target 是页面内链接路径。返回相对 locOut 的规范化路径（存在性由调用方核对）。
fs::path resolve_target(const fs::path& baseDir, const std::string& raw) {
    std::string t = strip_query_frag(raw);
    if (t.empty()) return {};
    t = url_decode(t);                              // 中文文件名链接还原（%E5%9F%BA... → 中文）
    fs::path base = baseDir.empty() ? fs::path(".") : baseDir;
    fs::path p = t;
    if (p.is_absolute()) {                              // 根相对：从 locOut 起
        fs::path rel = p.relative_path();
        return rel;
    }
    return (base / p).lexically_normal();
}

// 检查单个链接目标是否存在（尝试补 .html / index.html）
bool target_exists(const fs::path& locOut, const fs::path& rel) {
    if (rel.empty()) return false;
    std::error_code ec;
    if (fs::exists(locOut / rel, ec)) return true;
    // 无扩展名：试补 .html；目录：试 /index.html
    if (rel.extension().empty()) {
        if (fs::exists(locOut / (rel.string() + ".html"), ec)) return true;
        if (fs::exists(locOut / rel / "index.html", ec)) return true;
    }
    return false;
}

}  // namespace

void check_links(const fs::path& locOut, const std::string& loc) {
    std::error_code ec;
    if (!fs::is_directory(locOut, ec)) return;

    // 收集待检查的文件列表（先列再查，避免遍历中递归）
    std::vector<fs::path> pages;
    for (auto it = fs::recursive_directory_iterator(locOut, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        std::error_code e2;
        if (!fs::is_regular_file(*it, e2)) continue;
        if (lower_str((*it).path().extension().string()) != ".html") continue;
        pages.push_back(*it);
    }

    std::vector<std::string> local;
    for (const auto& page : pages) {
        std::string html = read_file(page);
        // 页面相对 locOut 的目录（""=根，或 "blog"、"tags"）
        fs::path relPage = fs::relative(page.parent_path(), locOut, ec);
        fs::path baseDir = (relPage == ".") ? fs::path() : relPage;

        // 逐字符扫描，跳过保护块；在标签内提取 href/src/srcset
        size_t i = 0, n = html.size();
        int protect = 0;
        std::string protectTag;
        while (i < n) {
            if (protect > 0) {
                if (html[i] == '<' && i + 1 < n && html[i + 1] == '/') {
                    std::string t = tag_name_at(html, i + 1);
                    if (t == protectTag) {
                        size_t gt = html.find('>', i);
                        if (gt == std::string::npos) break;
                        i = gt + 1; --protect;
                        if (protect == 0) protectTag.clear();
                        continue;
                    }
                }
                ++i; continue;
            }
            if (html[i] != '<') { ++i; continue; }
            size_t gt = html.find('>', i);
            if (gt == std::string::npos) break;
            std::string tag = html.substr(i, gt - i + 1);
            std::string tname = tag_name_at(html, i);
            if (!tname.empty() && html[i + 1] != '/' && html[i + 1] != '!' &&
                is_protected_tag(tname)) {
                ++protect; protectTag = tname;
                i = gt + 1; continue;
            }
            // 提取引用属性
            std::vector<std::string> attrs;
            std::string v;
            if (!(v = attr_value(tag, "href")).empty()) attrs.push_back(v);
            if (!(v = attr_value(tag, "src")).empty())  attrs.push_back(v);
            if (!(v = attr_value(tag, "srcset")).empty()) {
                // srcset 可能是 "a.webp 1x, b.png 2x"，取第一个 URL
                size_t sp = v.find_first_of(" \t");
                attrs.push_back((sp == std::string::npos) ? v : v.substr(0, sp));
            }
            for (const auto& raw : attrs) {
                if (skip_target(raw)) continue;
                fs::path rel = resolve_target(baseDir, raw);
                if (rel.empty()) continue;
                if (!target_exists(locOut, rel)) {
                    local.push_back(relPage.string() + " → " + raw);
                }
            }
            i = gt + 1;
        }
    }

    // 去重并入全局（线程外调用，无需锁；serve -w 反复构建由 run_build 开头清空）
    std::vector<std::string> uniq;
    for (auto& s : local)
        if (std::find(uniq.begin(), uniq.end(), s) == uniq.end()) uniq.push_back(s);
    for (auto& s : uniq)
        if (std::find(g_link_broken.begin(), g_link_broken.end(), s) == g_link_broken.end())
            g_link_broken.push_back(s);
}
