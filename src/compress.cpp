// compress.cpp —— 构建期压缩实现
//
// 图片：stb_image（成熟单头解码器）读图 → libwebp（Google 编码器）编 WebP 副本。
//   - 原图保留（老浏览器回退），页面 <img> 升级 <picture> 优先 WebP；
//   - 仅当 WebP 严格更小时保留副本（幂等：已有副本且未过期则跳过）。
// 代码：HTML/CSS 保守紧凑化（去注释 + 折叠空白，保护有语义区域）。
//
// 依赖（编译期，vendor 目录）：
//   .Cdocs/deps/vendor/stb_image.h
//   .Cdocs/deps/vendor/libwebp/   （libwebp v1.4.0 源码，build.cmd 预编译为 libwebp.a）

#include "compress.hpp"
#include "core.hpp"     // read_file

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_RESIZE2_IMPLEMENTATION
#define STBIR_MAX_CHANNELS 4
#include "stb_image_resize2.h"

#include "webp/encode.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace {

inline bool is_ws_char(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

std::string lower_ext(const fs::path& p) {
    std::string e = p.extension().string();
    std::transform(e.begin(), e.end(), e.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return e;
}

bool starts_with(const std::string& s, const char* pre) {
    return s.compare(0, std::strlen(pre), pre) == 0;
}

// 取小写标签名（从 '<' 后一位开始）
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

}  // namespace

// ---------------- 图片压缩（stb_image + libwebp → WebP 副本 + 多尺寸 srcset） ----------------

namespace {
// 把 RGBA 像素缩放到目标宽，并用 libwebp 编码成 <outBase>-<tw>w.webp
void encode_resized_webp(const unsigned char* px, int w, int h, int quality,
                         const fs::path& outBase, int tw) {
    if (tw >= w) return;
    int th = (int)(((long long)h * tw) / w);
    if (th < 1) return;
    std::vector<unsigned char> rp((size_t)tw * (size_t)th * 4);
    if (!stbir_resize_uint8_srgb(px, w, h, w * 4, rp.data(), tw, th, tw * 4, STBIR_RGBA)) return;
    uint8_t* o = nullptr;
    size_t sz = WebPEncodeRGBA(rp.data(), tw, th, tw * 4, (float)quality, &o);
    if (sz && o) {
        fs::path wp = outBase; wp += "-" + std::to_string(tw) + "w.webp";
        std::error_code wec;
        { std::ofstream f(wp, std::ios::binary); f.write(reinterpret_cast<const char*>(o), sz); }
        WebPFree(o);
        (void)wec;
    }
}
}  // namespace

fs::path webpize_file(const fs::path& p, int quality) {
    std::string ext = lower_ext(p);
    bool isImg = (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".gif" ||
                  ext == ".bmp" || ext == ".tga");
    if (!isImg) return {};
    std::error_code ec;
    if (!fs::is_regular_file(p, ec) || fs::file_size(p, ec) < 256) return {};

    fs::path wp = p; wp.replace_extension("webp");
    // 幂等跳过：WebP 副本已存在且不早于原图 → 无需重编
    std::error_code wec;
    if (fs::exists(wp, wec)) {
        struct stat a, b;
        if (stat(p.string().c_str(), &a) == 0 && stat(wp.string().c_str(), &b) == 0 &&
            b.st_mtime >= a.st_mtime)
            return wp;
    }

    // stb_image 解码（强制 RGBA，保留透明通道）
    int w = 0, h = 0, ch = 0;
    unsigned char* px = stbi_load(p.string().c_str(), &w, &h, &ch, 4);
    if (!px || w <= 0 || h <= 0) return {};
    if (w > 16382 || h > 16382) { stbi_image_free(px); return {}; }   // WebP 尺寸上限

    // libwebp 编码原尺寸
    uint8_t* out = nullptr;
    size_t sz = WebPEncodeRGBA(px, w, h, w * 4, (float)quality, &out);
    if (!sz || !out) { stbi_image_free(px); return {}; }

    { std::ofstream f(wp, std::ios::binary); f.write(reinterpret_cast<const char*>(out), sz); }
    WebPFree(out);

    // 收益判断：WebP 必须比原图小才保留，否则删除（无收益图）
    uintmax_t origSz = fs::file_size(p, ec);
    uintmax_t newSz  = fs::file_size(wp, wec);
    bool keep = (newSz > 0 && newSz < origSz);
    if (!keep) {
        fs::remove(wp, wec);
        stbi_image_free(px);
        return {};
    }

    // 响应式多尺寸：480 / 800 / 1200 宽（只对宽超过该档的图生成；小图跳过）
    fs::path outBase = p; outBase.replace_extension("");
    encode_resized_webp(px, w, h, quality, outBase, 480);
    encode_resized_webp(px, w, h, quality, outBase, 800);
    encode_resized_webp(px, w, h, quality, outBase, 1200);
    stbi_image_free(px);
    return wp;
}

int webpize_dir(const fs::path& dir, int quality) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return 0;
    int n = 0;
    for (auto it = fs::recursive_directory_iterator(dir, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        std::error_code e2;
        if (!fs::is_regular_file(*it, e2)) continue;
        std::string ext = lower_ext(*it);
        if (ext == ".webp") continue;
        if (!webpize_file(*it, quality).empty()) ++n;
    }
    return n;
}

// HTML <picture> 升级：src 指向的图片存在同名 .webp 时包装（保留原图作回退），
// 并生成响应式 srcset（480/800/1200 多尺寸）+ <img width/height>（stbi 读头部拿尺寸，防 CLS）
std::string wrap_webp(const std::string& html, const fs::path& locOut) {
    std::string out;
    out.reserve(html.size() + 256);
    const size_t n = html.size();
    size_t i = 0;
    while (i < n) {
        if (html.compare(i, 4, "<img") == 0) {
            size_t e = html.find('>', i);
            if (e == std::string::npos) { out += html.substr(i); break; }
            std::string tag = html.substr(i, e - i);
            // 解析 src 属性（带引号或无引号）
            std::string src;
            size_t sp = tag.find("src=");
            if (sp != std::string::npos) {
                sp += 4;
                if (sp < tag.size() && (tag[sp] == '"' || tag[sp] == '\'')) {
                    char q = tag[sp];
                    size_t s2 = tag.find(q, sp + 1);
                    if (s2 != std::string::npos) src = tag.substr(sp + 1, s2 - sp - 1);
                } else {
                    size_t s2 = tag.find_first_of(" \t\n", sp);
                    src = tag.substr(sp, s2 == std::string::npos ? tag.size() - sp : s2 - sp);
                }
            }
            bool ok = !src.empty() && !starts_with(src, "http") && src.compare(0, 5, "data:") != 0 &&
                      src.find('#') == std::string::npos;
            if (ok) {
                // 归一化 src（去掉 ../ 前缀）后检查 WebP 副本是否存在
                std::string rel = src;
                while (rel.compare(0, 3, "../") == 0) rel = rel.substr(3);
                size_t dot = rel.find_last_of('.');
                std::string relW = (dot == std::string::npos) ? rel + ".webp"
                                                              : rel.substr(0, dot) + ".webp";
                std::error_code wec;
                fs::path wpath = locOut / relW;
                if (fs::exists(wpath, wec)) {
                    // srcset 用页面相对路径（原 src 去扩展名 + 各尺寸后缀）
                    std::string srcBase = src;
                    size_t d2 = srcBase.find_last_of('.');
                    if (d2 != std::string::npos) srcBase = srcBase.substr(0, d2);
                    std::string srcset;
                    const int widths[] = {480, 800, 1200};
                    for (int tw : widths) {
                        std::string wfile = srcBase + "-" + std::to_string(tw) + "w.webp";
                        if (fs::exists(locOut / (rel.substr(0, rel.find_last_of('.')) + "-" +
                                                 std::to_string(tw) + "w.webp"), wec))
                            srcset += ", " + wfile + " " + std::to_string(tw) + "w";
                    }
                    if (!srcset.empty()) srcset = srcset.substr(2);   // 去掉首个 ", "
                    // 原始尺寸（stbi 读头部，不整图解码）
                    int iw = 0, ih = 0, ic = 0;
                    bool gotSize = stbi_info((locOut / rel).string().c_str(), &iw, &ih, &ic) != 0;
                    std::string wh;
                    if (gotSize && iw > 0 && ih > 0)
                        wh = " width=\"" + std::to_string(iw) + "\" height=\"" + std::to_string(ih) + "\"";
                    std::string srcW = srcBase + ".webp";
                    out += "<picture><source type=\"image/webp\" srcset=\"";
                    if (!srcset.empty()) { out += srcset + ", " + srcW + " " + std::to_string(iw) + "w"; }
                    else { out += srcW; }
                    out += "\">";
                    out += "<img" + wh;
                    out += tag.substr(4);   // 原 img 剩余属性（src/loading/alt 等）
                    out += "</picture>";
                    i = e + 1;
                    continue;
                }
            }
            out += tag;
            out += '>';
            i = e + 1;
            continue;
        }
        out += html[i];
        ++i;
    }
    return out;
}

// ---------------- 代码压缩（构建期保守紧凑化） ----------------

std::string minify_html(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    const size_t n = s.size();
    size_t i = 0;
    int protect = 0;
    std::string protectTag;
    bool pendingSpace = false;

    while (i < n) {
        if (protect > 0) {
            // 保护块内：原样拷贝，检测闭合标签 </tag>
            if (s[i] == '<' && i + 1 < n && s[i + 1] == '/') {
                std::string t = tag_name_at(s, i + 1);
                if (t == protectTag) {
                    size_t gt = s.find('>', i);
                    if (gt == std::string::npos) { out += s.substr(i); break; }
                    out += s.substr(i, gt - i + 1);
                    i = gt + 1;
                    --protect;
                    if (protect == 0) protectTag.clear();
                    continue;
                }
            }
            out += s[i]; ++i;
            continue;
        }
        if (s.compare(i, 4, "<!--") == 0) {
            size_t e = s.find("-->", i + 4);
            if (e == std::string::npos) break;   // 未闭合注释：忽略剩余
            i = e + 3;
            continue;
        }
        if (s[i] == '<') {
            if (i + 1 < n && s[i + 1] != '/' && s[i + 1] != '!') {
                std::string t = tag_name_at(s, i);
                if (is_protected_tag(t)) {
                    size_t gt = s.find('>', i);
                    if (gt == std::string::npos) { out += s.substr(i); break; }
                    if (pendingSpace) { out += ' '; pendingSpace = false; }
                    out += s.substr(i, gt - i + 1);
                    ++protect; protectTag = t;
                    i = gt + 1;
                    continue;
                }
            }
            size_t gt = s.find('>', i);
            if (gt == std::string::npos) { out += s.substr(i); break; }
            if (pendingSpace) { out += ' '; pendingSpace = false; }
            out += s.substr(i, gt - i + 1);
            i = gt + 1;
            continue;
        }
        if (is_ws_char(s[i])) {
            pendingSpace = true;
            while (i < n && is_ws_char(s[i])) ++i;
            continue;
        }
        if (pendingSpace) { out += ' '; pendingSpace = false; }
        out += s[i]; ++i;
    }
    return out;
}

std::string minify_css(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    const size_t n = s.size();
    size_t i = 0;
    char quote = 0;
    bool wsPending = false;

    while (i < n) {
        char c = s[i];
        if (quote) {
            out += c;
            if (c == '\\' && i + 1 < n) { out += s[i + 1]; i += 2; continue; }
            if (c == quote) quote = 0;
            ++i;
            continue;
        }
        if (c == '/' && i + 1 < n && s[i + 1] == '*') {
            bool keep = (i + 2 < n && s[i + 2] == '!');   // /*! 版权注释保留
            size_t e = s.find("*/", i + 2);
            if (e == std::string::npos) break;
            if (keep) { out += s.substr(i, e - i + 2); }
            i = e + 2;
            continue;
        }
        if (c == '"' || c == '\'') { quote = c; out += c; ++i; continue; }
        if (is_ws_char(c)) { wsPending = true; while (i < n && is_ws_char(s[i])) ++i; continue; }
        if (wsPending) { out += ' '; wsPending = false; }
        out += c; ++i;
    }
    return out;
}

// 递归压缩目录下所有 .css（比原文件更小才替换）
void compress_dir_css(const fs::path& dir) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return;
    for (auto it = fs::recursive_directory_iterator(dir, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        std::error_code e2;
        if (!fs::is_regular_file(*it, e2)) continue;
        std::string ext = lower_ext(*it);
        if (ext != ".css") continue;
        std::string css = read_file(*it);
        std::string min = minify_css(css);
        if (min.size() >= css.size()) continue;   // 无收益（已是压缩态）
        fs::path tmp = *it; tmp += ".cdocs_tmp";
        std::error_code wec;
        { std::ofstream o(tmp, std::ios::binary); o << min; }
        if (!fs::exists(tmp, wec)) continue;
        if (fs::file_size(tmp, wec) < css.size()) {
            fs::remove(*it, wec);
            fs::rename(tmp, *it, wec);
        }
        fs::remove(tmp, wec);
    }
}

// ---------------- 资源指纹（cache busting） ----------------

namespace {

// FNV-1a 64 位哈希 → 8 位 hex（内容哈希，用于资源版本）
std::string fnv1a_hex(const std::string& s) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
    return std::string(buf).substr(0, 8);
}

}  // namespace

void fingerprint_assets(const fs::path& assetsDir) {
    g_fp.clear();
    std::error_code ec;
    if (!fs::is_directory(assetsDir, ec)) return;
    for (auto it = fs::recursive_directory_iterator(assetsDir, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        std::error_code e2;
        if (!fs::is_regular_file(*it, e2)) continue;
        fs::path rel = fs::relative(*it, assetsDir, e2);
        if (e2) continue;
        std::string rels = rel.generic_string();
        // 只指纹主题自己的 css/js（deps/ 第三方库保持 URL 稳定）
        if (rels.compare(0, 4, "css/") != 0 && rels.compare(0, 3, "js/") != 0) continue;
        std::string ext = lower_ext(*it);
        if (ext != ".css" && ext != ".js") continue;
        std::string content = read_file(*it);
        g_fp["assets/" + rels] = fnv1a_hex(content);
    }
}

std::string apply_fingerprints(const std::string& html) {
    if (g_fp.empty()) return html;
    std::string out = html;
    for (const auto& kv : g_fp) {
        // 匹配 "assets/css/style.css" 后跟闭合引号（href="/src=" 属性值）
        std::string needle = kv.first + "\"";
        std::string repl   = kv.first + "?v=" + kv.second + "\"";
        size_t pos = 0;
        while ((pos = out.find(needle, pos)) != std::string::npos) {
            out.replace(pos, needle.size(), repl);
            pos += repl.size();
        }
    }
    return out;
}
