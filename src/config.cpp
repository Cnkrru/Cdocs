// config.cpp —— config/route JSON 解析实现（自 main.cpp 原样搬迁）

#include "config.hpp"

Link parse_link(const json& j) {
    Link l;
    l.title = j.value("title", std::string());
    l.file  = j.value("file", std::string());
    l.url   = j.value("url", std::string());
    return l;
}

NavNode parse_nav(const json& j) {
    NavNode n;
    n.title = j.value("title", std::string());
    n.file  = j.value("file", std::string());
    n.url   = j.value("url", std::string());
    if (j.contains("weight")) n.weight = j["weight"].get<int>();
    if (j.contains("items"))
        for (auto& c : j["items"]) n.children.push_back(parse_nav(c));
    return n;
}
