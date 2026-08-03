// shortcode.cpp —— 正文 shortcode 引擎（<组件/> 标签语法）
// （自 builder.cpp 拆分：预扫描 → 占位 token → md4c 渲染后展开 → 文档级 style 去重）
// 语法：首字母大写 = shortcode（<Tabs>…</Tabs> / <Badge type="new"/>），参数 → props，子内容 → slot；
// \<Name> 转义为字面量；代码围栏/行内代码内不解析；未闭合双标签警告；深度上限 16。

#include "shortcode.hpp"
#include "component.hpp"   // load_component / fill_data_holes / site_data
#include "markdown.hpp"    // markdown_to_html / render_admonitions
#include <cctype>
#include <set>

// shortcode 实例（预扫描产出，展开时按索引取用）
struct ScInst {
    std::string name;
    json props;
    std::string inner;
};
static thread_local std::vector<ScInst> g_sc_insts;
static thread_local std::set<std::string> g_emitted_styles;   // 同文档内组件 <style> 去重（只输出第一份）
static const char* kScTok = "@@CDOCS_SC_";
static std::string sc_token(int i) { return std::string(kScTok) + std::to_string(i) + "@@"; }

// 组件 HTML 里可内嵌 <style> 块（样式组件化）。同文档多次使用同一组件时
// style 只输出一次（CSS 全局生效，位置无关）；结构/script 每处保留。
static std::string dedup_style_blocks(const std::string& html) {
    std::string out;
    out.reserve(html.size());
    size_t pos = 0;
    for (;;) {
        size_t s = html.find("<style", pos);
        if (s == std::string::npos) { out += html.substr(pos); break; }
        size_t e = html.find("</style>", s);
        if (e == std::string::npos) { out += html.substr(pos); break; }  // 未闭合：原样保留
        out += html.substr(pos, s - pos);
        std::string block = html.substr(s, e + 8 - s);
        if (g_emitted_styles.insert(block).second) out += block;
        pos = e + 8;
    }
    return out;
}

static std::string expand_shortcodes(const std::string& html, bool en);

// 预扫描：md 原文 → shortcode 替换为占位 token；跳过 fenced code / inline code；\<Name> → &lt;Name&gt;
static std::string prescan_shortcodes(const std::string& md) {
    std::string out;
    out.reserve(md.size() + 64);
    const size_t n = md.size();
    size_t i = 0;
    bool inFence = false;
    char fenceCh = 0;
    size_t fenceLen = 0;
    while (i < n) {
        // fenced code 检测（行首 ``` 或 ~~~）
        if (!inFence && (i == 0 || md[i - 1] == '\n') && (md[i] == '`' || md[i] == '~')) {
            size_t j = i, cnt = 0;
            while (j < n && md[j] == md[i]) { ++cnt; ++j; }
            if (cnt >= 3) {
                inFence = true; fenceCh = md[i]; fenceLen = cnt;
                out += md.substr(i, j - i);
                i = j;
                continue;
            }
        }
        if (inFence) {
            size_t nl = md.find('\n', i);
            size_t segEnd = (nl == std::string::npos) ? n : nl + 1;
            std::string line = md.substr(i, segEnd - i);
            size_t ls = 0;
            while (ls < line.size() && (line[ls] == ' ' || line[ls] == '\t')) ++ls;
            size_t cnt = 0;
            while (ls + cnt < line.size() && line[ls + cnt] == fenceCh) ++cnt;
            if (cnt >= fenceLen) inFence = false;
            out += line;
            i = segEnd;
            continue;
        }
        // L3 转义：\<Tabs> → &amp;lt;Tabs&amp;gt;（字面量，不再解析）
        // 注意：反斜杠后是 '<' + 大写字母（标签形态）；\<Tabs> 的 \ 后是 < 不是大写，旧判断永不触发
        if (md[i] == '\\' && i + 1 < n && md[i + 1] == '<'
            && i + 2 < n && isupper((unsigned char)md[i + 2])) {
            size_t gt = md.find('>', i + 2);
            if (gt != std::string::npos) {
                // 整个标签转义：&amp;lt;Tabs&amp;gt; → md4c 解码 &amp; → & → 浏览器显示 <Tabs> 字面量
                out += "&amp;lt;" + md.substr(i + 2, gt - i - 2) + "&amp;gt;";
                i = gt + 1;
                continue;
            }
            out += "&amp;lt;";
            i += 2;
            continue;
        }
        // inline code：`...` 跨度跳过（内容里的标签不解析）
        if (md[i] == '`') {
            size_t j = i, cnt = 0;
            while (j < n && md[j] == '`') { ++cnt; ++j; }
            size_t close = md.find(std::string(cnt, '`'), j);
            if (close != std::string::npos) {
                out += md.substr(i, close + cnt - i);
                i = close + cnt;
                continue;
            }
            out += md[i]; ++i;
            continue;
        }
        // shortcode 开标签：<大写字母
        if (md[i] == '<' && i + 1 < n && isupper((unsigned char)md[i + 1])) {
            size_t j = i + 1;
            while (j < n && isalnum((unsigned char)md[j])) ++j;
            std::string name = md.substr(i + 1, j - i - 1);
            if (!name.empty()) {
                size_t k = j;
                json props;
                bool selfClose = false, ok = true;
                while (k < n) {
                    while (k < n && (md[k] == ' ' || md[k] == '\t' || md[k] == '\n' || md[k] == '\r')) ++k;
                    if (k >= n) { ok = false; break; }
                    if (md[k] == '/' && k + 1 < n && md[k + 1] == '>') { selfClose = true; k += 2; break; }
                    if (md[k] == '>') { ++k; break; }
                    size_t a0 = k;
                    while (k < n && (isalnum((unsigned char)md[k]) || md[k] == '-' || md[k] == '_')) ++k;
                    std::string aname = md.substr(a0, k - a0);
                    while (k < n && (md[k] == ' ' || md[k] == '\t')) ++k;
                    if (k < n && md[k] == '=') {
                        ++k;
                        while (k < n && (md[k] == ' ' || md[k] == '\t')) ++k;
                        std::string aval;
                        if (k < n && (md[k] == '"' || md[k] == '\'')) {
                            char q = md[k]; ++k;
                            size_t v0 = k;
                            while (k < n && md[k] != q) ++k;
                            aval = md.substr(v0, k - v0);
                            if (k < n) ++k;
                        } else {
                            size_t v0 = k;
                            while (k < n && md[k] != ' ' && md[k] != '\t' && md[k] != '\n' && md[k] != '\r' && md[k] != '>' && md[k] != '/') ++k;
                            aval = md.substr(v0, k - v0);
                        }
                        if (!aname.empty()) props[aname] = aval;
                    } else if (!aname.empty()) {
                        props[aname] = true;
                    }
                    if (aname.empty()) { ok = false; break; }
                }
                if (ok) {
                    if (selfClose) {
                        int idx = (int)g_sc_insts.size();
                        g_sc_insts.push_back({name, props, ""});
                        out += sc_token(idx);
                        i = k;
                        continue;
                    }
                    std::string closeTag = "</" + name + ">";
                    size_t close = md.find(closeTag, k);
                    if (close != std::string::npos) {
                        std::string inner = md.substr(k, close - k);
                        int idx = (int)g_sc_insts.size();
                        g_sc_insts.push_back({name, props, inner});
                        out += sc_token(idx);
                        i = close + closeTag.size();
                        continue;
                    }
                    if (comp_warned_once("scopen:" + name))
                        std::cerr << color::warn("警告: ") << "shortcode 未闭合: <" << name << ">（原样保留）\n";
                    out += md.substr(i, k - i);
                    i = k;
                    continue;
                }
            }
        }
        out += md[i];
        ++i;
    }
    return out;
}

// 渲染单个 shortcode 实例：innerMd 递归完整管线 → {{slot}}；参数 → props；{{slot_raw}} = 转义原文
static std::string render_shortcode(int idx, bool en) {
    if (idx < 0 || idx >= (int)g_sc_insts.size()) return {};
    ScInst inst = g_sc_insts[idx];   // 值拷贝：prescan 内部 push_back 扩容时本地副本不受影响（防悬垂引用）
    std::string body = load_component(inst.name);
    if (body.empty()) {
        if (comp_warned_once("sc:" + inst.name))
            std::cerr << color::warn("警告: ") << "shortcode 组件不存在: components/" << inst.name
                      << ".html（正文原样保留）\n";
        return "<" + inst.name + ">";
    }
    std::string innerHtml;
    if (!inst.inner.empty()) {
        std::string md2 = prescan_shortcodes(inst.inner);
        std::string h1 = markdown_to_html(md2);
        std::string h2 = render_admonitions(h1, en);
        innerHtml = expand_shortcodes(h2, en);
    }
    json ctx = inst.props;
    ctx["slot"] = innerHtml;
    ctx["slot_raw"] = esc(inst.inner);
    const json& sd = site_data();
    for (auto it = sd.cbegin(); it != sd.cend(); ++it)
        if (!ctx.contains(it.key())) ctx[it.key()] = it.value();
    std::string res = fill_data_holes(body, ctx);
    return res;
}

// 渲染后：占位 token → 组件渲染结果（嵌套深度上限 16）
static std::string expand_shortcodes(const std::string& html, bool en) {
    std::string out;
    out.reserve(html.size() + 256);
    const size_t n = html.size();
    size_t i = 0;
    int depth = 0;
    const size_t tokLen = std::strlen(kScTok);
    while (i < n) {
        if (html.compare(i, tokLen, kScTok) == 0) {
            size_t e0 = i + tokLen;
            size_t e1 = html.find("@@", e0);
            if (e1 != std::string::npos) {
                int idx = -1;
                try { idx = std::stoi(html.substr(e0, e1 - e0)); } catch (...) {}
                if (idx >= 0 && depth < 16) {
                    ++depth;
                    out += render_shortcode(idx, en);
                    --depth;
                    i = e1 + 2;
                    continue;
                }
            }
        }
        out += html[i];
        ++i;
    }
    return out;
}

// 正文完整管线（shortcode 预扫描 → md4c → admonitions → shortcode 展开）
std::string render_doc_body(const std::string& md, bool en) {
    g_sc_insts.clear();
    g_emitted_styles.clear();
    std::string md2 = prescan_shortcodes(md);
    // style 去重必须在最终输出做一次（而非组件级）：嵌套组件的 style 嵌在父组件内，
    // 组件级去重会把父组件内部的子组件 style 误删
    return dedup_style_blocks(expand_shortcodes(render_admonitions(markdown_to_html(md2), en), en));
}
