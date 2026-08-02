// frontmatter.cpp —— front matter 解析实现（自 main.cpp 原样搬迁）

#include "frontmatter.hpp"

// 解析文档开头的 `--- ... ---` 块，返回元数据并把剥离后的正文写入 body_out。
// 无 front matter 时 body_out 等于原文、FrontMatter 全默认（向后兼容旧文档）。
FrontMatter parse_front_matter(const std::string& md, std::string& body_out) {
    FrontMatter fm;
    body_out = md;
    if (md.rfind("---", 0) != 0) return fm;            // 必须以 --- 开头
    size_t nl = md.find('\n');
    if (nl == std::string::npos) return fm;
    size_t searchFrom = nl + 1, close = std::string::npos, end = std::string::npos;
    while (true) {
        size_t n2 = md.find('\n', searchFrom);
        std::string line = md.substr(searchFrom, (n2 == std::string::npos) ? std::string::npos : n2 - searchFrom);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line == "---" || line == "...") {
            close = searchFrom; end = (n2 == std::string::npos) ? md.size() : n2;
            break;
        }
        if (n2 == std::string::npos) break;
        searchFrom = n2 + 1;
    }
    if (close == std::string::npos) return fm;          // 无闭合 ---，当作无 front matter
    std::string fmtext = md.substr(nl + 1, close - (nl + 1));
    body_out = md.substr(end + 1);
    std::string curKey;
    std::istringstream iss(fmtext);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string tl = trim(line);   // YAML 列表项常有缩进（如 "  - item"），先去掉行首空白
        if (!tl.empty() && tl[0] == '-' && (tl.size() == 1 || tl[1] == ' ')) {
            if (curKey == "tags")    fm.tags.push_back(trim(tl.substr(1)));
            else if (curKey == "aliases") fm.aliases.push_back(trim(tl.substr(1)));
            continue;
        }
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = trim(line.substr(0, colon));
        std::string val = trim(line.substr(colon + 1));
        if (val.size() >= 2 && ((val.front() == '"' && val.back() == '"') ||
                                 (val.front() == '\'' && val.back() == '\'')))
            val = val.substr(1, val.size() - 2);
        curKey = key;
        if (key == "title")       { fm.title = val; fm.hasTitle = true; }
        else if (key == "date")   fm.date = val;
        else if (key == "description") fm.description = val;
        else if (key == "lastmod")    fm.lastmod = val;
        else if (key == "draft")  fm.draft = (val == "true" || val == "yes" || val == "1");
        else if (key == "weight") fm.weight = std::atoi(val.c_str());
        else if (key == "aliases") {
            if (!val.empty() && val.front() == '[') {
                std::string inner = val.substr(1);
                if (!inner.empty() && inner.back() == ']') inner.pop_back();
                std::istringstream ts(inner);
                std::string t;
                while (std::getline(ts, t, ',')) { t = trim(t); if (!t.empty()) fm.aliases.push_back(t); }
            } else if (!val.empty()) {
                fm.aliases.push_back(val);
            }
        }
        else if (key == "tags") {
            if (!val.empty() && val.front() == '[') {
                std::string inner = val.substr(1);
                if (!inner.empty() && inner.back() == ']') inner.pop_back();
                std::istringstream ts(inner);
                std::string t;
                while (std::getline(ts, t, ',')) { t = trim(t); if (!t.empty()) fm.tags.push_back(t); }
            }
        }
    }
    return fm;
}
