// scaffold.cpp —— 站点脚手架：init / section / new / clean
// （自 builder.cpp 拆分：站点骨架生成、内容区管理、页面新建、输出清理）

#include "scaffold.hpp"
#include "builder.hpp"   // run_build（init 后自动构建）
#include <fstream>
#include <sstream>
#include <cctype>

// ---- 数据查询插件（Python）：博客流 + 标签聚合 ----
// 数据查询必须走插件（引擎不内置查询逻辑）；init/add 博客区时自动生成到 .Cdocs/plugins/。
static const char* kBlogQueryJson = R"J({
  "name": "blog-query",
  "description": "博客流查询插件：筛选 blog/* 文章，按 date 倒序，输出分页与首页流（on_data_query 钩子）。",
  "version": "1.0.0",
  "hooks": {
    "on_data_query": {
      "cmd": "python scripts/blog_query.py",
      "timeout": 15
    }
  }
}
)J";
static const char* kBlogQueryPy = R"PY(#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""blog-query: 博客流查询（on_data_query 钩子）。引擎只产数据快照，本文档决定取哪些/顺序/分页。"""
import json
import sys

PER_PAGE = 10      # 列表页每篇数
HOME_TOP = 8       # 首页文章流条数


def main():
    if len(sys.argv) < 3:
        print("usage: blog_query.py <ctx.json> <out.json>", file=sys.stderr)
        sys.exit(1)
    with open(sys.argv[1], encoding='utf-8') as f:
        ctx = json.load(f)
    posts = [p for p in ctx.get("pages", [])
             if p.get("file", "").startswith("blog/") and not p.get("draft")]
    posts.sort(key=lambda p: (p.get("dateT_iso") or "", p.get("date") or "", p.get("file") or ""),
               reverse=True)
    order = [p["file"] for p in posts]
    out = {"ok": True}
    out["blog_order"] = order
    out["blog_pages"] = [order[i:i + PER_PAGE] for i in range(0, len(order), PER_PAGE)]
    out["home_posts"] = order[:HOME_TOP]
    with open(sys.argv[2], 'w', encoding='utf-8') as f:
        json.dump(out, f, ensure_ascii=False, indent=2)


if __name__ == "__main__":
    main()
)PY";
static const char* kTagsQueryJson = R"J({
  "name": "tags-query",
  "description": "标签聚合查询插件：聚合文档+博客的 frontmatter tags，输出标签总览与每标签文章列表（on_data_query 钩子）。",
  "version": "1.0.0",
  "hooks": {
    "on_data_query": {
      "cmd": "python scripts/tags_query.py",
      "timeout": 15
    }
  }
}
)J";
static const char* kTagsQueryPy = R"PY(#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""tags-query: 标签聚合查询（on_data_query 钩子）。聚合全部文章 tags，输出总览 + 每标签列表。"""
import json
import sys


def slugify(s):
    out = []
    for ch in s:
        o = ord(ch)
        if o >= 0x80:
            out.append(ch)
        elif 'a' <= ch <= 'z' or '0' <= ch <= '9':
            out.append(ch)
        elif 'A' <= ch <= 'Z':
            out.append(ch.lower())
        elif ch in ' -_/':
            out.append('-')
    res = []
    prev = ''
    for ch in out:
        if ch == '-' and prev == '-':
            continue
        res.append(ch)
        prev = ch
    return ''.join(res).strip('-')


def main():
    if len(sys.argv) < 3:
        print("usage: tags_query.py <ctx.json> <out.json>", file=sys.stderr)
        sys.exit(1)
    with open(sys.argv[1], encoding='utf-8') as f:
        ctx = json.load(f)
    pages = [p for p in ctx.get("pages", []) if not p.get("draft")]
    by_file = {p["file"]: p for p in pages}
    tag_map = {}
    for p in pages:
        for t in p.get("tags", []):
            tag_map.setdefault(t, []).append(p["file"])
    out = {"ok": True}
    out["tags"] = [{"name": k, "href": slugify(k) + ".html"} for k in sorted(tag_map)]
    out["tag_pages"] = {}
    for name, files in tag_map.items():
        blog = sorted((f for f in files if f.startswith("blog/")),
                      key=lambda f: by_file.get(f, {}).get("dateT_iso") or "",
                      reverse=True)
        docs = [f for f in files if not f.startswith("blog/")]
        out["tag_pages"][name] = blog + docs
    with open(sys.argv[2], 'w', encoding='utf-8') as f:
        json.dump(out, f, ensure_ascii=False, indent=2)


if __name__ == "__main__":
    main()
)PY";

// 写入查询插件到站点（init/add 博客区时调用；幂等，可重复执行）
static void write_query_plugins(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir / ".Cdocs/plugins/blog-query/scripts", ec);
    fs::create_directories(dir / ".Cdocs/plugins/tags-query/scripts", ec);
    { std::ofstream f(dir / ".Cdocs/plugins/blog-query/plugin.json"); f << kBlogQueryJson; }
    { std::ofstream f(dir / ".Cdocs/plugins/blog-query/scripts/blog_query.py"); f << kBlogQueryPy; }
    { std::ofstream f(dir / ".Cdocs/plugins/tags-query/plugin.json"); f << kTagsQueryJson; }
    { std::ofstream f(dir / ".Cdocs/plugins/tags-query/scripts/tags_query.py"); f << kTagsQueryPy; }
}

static const char* kZhCN = R"CDOCS({
  "siteTitle": "Cdocs 文档",
  "siteDesc": "一个用 C++ 编写、复用成熟组件（md4c + nlohmann/json + FlexSearch）的极简静态文档站点生成器。",
  "menuToggleLabel": "打开导航",
  "searchPlaceholder": "搜索文档…",
  "themeToggleLabel": "切换主题",
  "tocTitle": "本页目录",
  "home": "首页",
  "getStarted": "开始阅读",
  "prevLabel": "上一篇",
  "nextLabel": "下一篇",
  "pagerNone": "暂无",
  "editThisPage": "编辑此页",
  "lastUpdated": "最后更新于",
  "readingTime": "约 {{minutes}} 分钟阅读（{{words}} 字）",
  "backToTop": "顶部",
  "notFoundTitle": "页面不见了",
  "notFoundDesc": "你访问的页面不存在或已被移动。",
  "backHome": "返回首页",
  "localeLabel": "语言",
  "brand": "Cdocs",
  "navGitHub": "GitHub",
  "navProject": "项目主页",
  "footerText": "© 2026 Cdocs · 基于 C++、md4c、nlohmann/json、FlexSearch 构建",
  "navHome": "首页",
  "navDocs": "文档",
  "navGettingStarted": "入门",
  "navIntro": "介绍",
  "navGuide": "使用指南",
  "navReference": "参考",
  "navApi": "接口说明",
  "navAdvanced": "进阶",
  "navArchitecture": "架构",
  "navRendering": "渲染",
  "navInternals": "底层实现",
  "navPipeline": "渲染管线",
  "navRender": "渲染循环",
  "allTags": "全部标签",
  "navBlog": "博客",
  "blogTitle": "博客",
  "copyCode": "复制代码",
  "copy": "复制",
  "copied": "已复制",
  "searchLoadFailed": "搜索数据加载失败",
  "useHttpServer": "：请用本地服务器(http)访问，不要直接以 file:// 打开本页",
  "noResults": "没有匹配「",
  "noResultsSuffix": "」的结果",
  "cmdTitle": "搜索文档",
  "cmdPlaceholder": "搜索文档…",
  "cmdType": "输入关键词开始搜索",
  "mermaidError": "图表渲染失败",
  "rssLabel": "RSS",
  "printPage": "打印 / 导出 PDF",
  "feedbackTitle": "本页有帮助吗？",
  "feedbackYes": "有帮助",
  "feedbackNo": "需改进",
  "feedbackThanks": "感谢你的反馈！",
  "lightboxLabel": "图片预览",
  "close": "关闭"
})CDOCS";

static const char* kEn = R"CDOCS({
  "siteTitle": "Cdocs Docs",
  "siteDesc": "A minimal static documentation site generator written in C++, reusing mature components (md4c + nlohmann/json + FlexSearch).",
  "menuToggleLabel": "Open navigation",
  "searchPlaceholder": "Search md…",
  "themeToggleLabel": "Toggle theme",
  "tocTitle": "On this page",
  "home": "Home",
  "getStarted": "Get started",
  "prevLabel": "Previous",
  "nextLabel": "Next",
  "pagerNone": "None",
  "editThisPage": "Edit this page",
  "lastUpdated": "Last updated:",
  "readingTime": "About {{minutes}} min read ({{words}} words)",
  "backToTop": "Top",
  "notFoundTitle": "Page not found",
  "notFoundDesc": "The page you are looking for does not exist or has moved.",
  "backHome": "Back to home",
  "localeLabel": "Language",
  "brand": "Cdocs",
  "navGitHub": "GitHub",
  "navProject": "Project Home",
  "footerText": "© 2026 Cdocs · Built with C++, md4c, nlohmann/json, FlexSearch",
  "navHome": "Home",
  "navDocs": "Docs",
  "navGettingStarted": "Getting Started",
  "navIntro": "Introduction",
  "navGuide": "Guide",
  "navReference": "Reference",
  "navApi": "API Reference",
  "navAdvanced": "Advanced",
  "navArchitecture": "Architecture",
  "navRendering": "Rendering",
  "navInternals": "Internals",
  "navPipeline": "Render Pipeline",
  "navRender": "Render Loop",
  "allTags": "All Tags",
  "navBlog": "Blog",
  "blogTitle": "Blog",
  "copyCode": "Copy code",
  "copy": "Copy",
  "copied": "Copied",
  "searchLoadFailed": "Failed to load search data",
  "useHttpServer": ": please access via a local HTTP server, not file://",
  "noResults": "No results for \"",
  "noResultsSuffix": "\"",
  "cmdTitle": "Search md",
  "cmdPlaceholder": "Search md…",
  "cmdType": "Type to search",
  "mermaidError": "Failed to render diagram",
  "rssLabel": "RSS",
  "printPage": "Print / Export PDF",
  "feedbackTitle": "Was this page helpful?",
  "feedbackYes": "Yes",
  "feedbackNo": "No",
  "feedbackThanks": "Thanks for your feedback!",
  "lightboxLabel": "Image preview",
  "close": "Close"
})CDOCS";

// 把文件名/段名美化为页面标题（my-page → My Page）
static std::string pretty_title(const std::string& stem) {
    std::string t = stem;
    for (size_t i = 0; i < t.size(); i++)
        if (t[i] == '-' || t[i] == '_') t[i] = ' ';
    if (!t.empty() && t[0] >= 'a' && t[0] <= 'z') t[0] = (char)(t[0] - 'a' + 'A');
    return t;
}

// init：在指定目录创建一个完整的新站点（对标 hugo new site）
// 交互式询问内容区（文档/博客/两者）与是否带历史版本（md-v1/）；useDefaults 跳过（默认 文档+博客，不带版本）
int cmd_init(fs::path dir, bool copyExe, bool useDefaults) {
    std::error_code ec;
    if (fs::exists(dir) && fs::is_directory(dir) && !fs::is_empty(dir, ec)) {
        std::cerr << color::error("目标目录已存在且非空: ") << dir << "\n";
        return 1;
    }

    // ---- 交互选择内容区与版本（--defaults / 非终端时用默认） ----
    int contentMode = 3;          // 1=仅文档  2=仅博客  3=文档+博客
    bool withVersion = false;     // 文档带历史版本（md-v1/）
    bool interactive = !useDefaults;
    if (interactive) {
#ifdef _WIN32
        // Windows 控制台：getline 前需保证 stdin 未损坏
#endif
        std::string line;
        std::cout << "\n" << color::cyan("初始化内容区：") << "\n"
                  << "  [1] 仅文档（md/docs/）\n"
                  << "  [2] 仅博客（md/blog/）\n"
                  << "  [3] 文档 + 博客\n"
                  << color::muted("请选择 (1-3，默认 3): ") << std::flush;
        if (std::getline(std::cin, line) && !line.empty()) {
            int v = std::atoi(line.c_str());
            if (v >= 1 && v <= 3) contentMode = v;
        }
        if (contentMode != 2) {   // 含文档 → 询问是否带版本
            std::cout << color::muted("文档区要带历史版本快照（md-v1/）吗？[y/N] ") << std::flush;
            line.clear();
            std::getline(std::cin, line);
            withVersion = (line == "y" || line == "Y" || line == "yes");
        }
        std::cout << "\n";
    }
    bool needDocs = (contentMode != 2);   // 内容区含文档
    bool needBlog = (contentMode != 1);   // 内容区含博客
    fs::create_directories(dir, ec);
    fs::create_directories(dir / "md", ec);
    fs::create_directories(dir / "md" / "docs", ec);
    fs::create_directories(dir / "archetypes", ec);
    fs::create_directories(dir / ".Cdocs" / "config", ec);
    fs::create_directories(dir / ".Cdocs" / "i18n", ec);
    fs::create_directories(dir / ".Cdocs" / "data", ec);   // 站点自定义数据（v6）：任意 KV 文件

    // 复制运行所需资源（theme 主题 + deps 运行时依赖；config/i18n 会被下面写入覆盖）
    fs::path eng = exe_dir() / ".Cdocs";
    bool haveEngine = fs::exists(eng);
    if (haveEngine) {
        for (const char* sub : { "theme", "deps" }) {
            fs::path src = eng / sub;
            if (fs::exists(src))
                fs::copy(src, dir / ".Cdocs" / sub,
                         fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        }
        if (copyExe) {
            fs::path exe = exe_dir() / "Cdocs.exe";
            if (fs::exists(exe))
                fs::copy(exe, dir / "Cdocs.exe", fs::copy_options::overwrite_existing, ec);
        }
    }

    std::string stem = dir.stem().empty() ? "md" : dir.stem().string();

    // 1) config.json（三区块：site 全局 / head 页眉 / center 内容 / footer 页脚）
    //    开箱即双语 + 全功能；所有 true/false 开关集中在对应区块顶部便于配置
    {
        std::ofstream o(dir / ".Cdocs/config/config.json");
        o << "{\n"
          << "  \"site\": {\n"
          << "    \"title\": \"" << esc_attr(stem) << "\",\n"
          << "    \"description\": \"由 Cdocs 生成的静态文档站\",\n"
          << "    \"theme\": \"dark\",\n"
          << "    \"url\": \"\",\n"
          << "    \"i18n\": {\n"
          << "      \"defaultLocale\": \"zh-CN\",\n"
          << "      \"dir\": \".Cdocs/i18n\",\n"
          << "      \"locales\": { \"zh-CN\": { \"label\": \"简体中文\" }, \"en\": { \"label\": \"English\" } }\n"
          << "    },\n"
          << "    \"editLink\": { \"base\": \"\", \"docsDir\": \"md\" },\n"
          << "    \"sidebar\": {\n";
        if (needDocs)
            o << "      \"md\": \"sidebar/md.json\",\n";
        if (withVersion)
            o << "      \"md-v1\": \"sidebar/v1.json\",\n";
        if (needBlog)
            o << "      \"blog\": \"sidebar/blog.json\"\n";
        o << "    }\n"
          << "  },\n"
          << "  \"head\": {\n"
          << "    \"logo\": \"\",\n"
          << "    \"showSearch\": true,\n"
          << "    \"showThemeToggle\": true,\n"
          << "    \"nav\": [\n"
          << "      { \"title\": \"{{navHome}}\", \"file\": \"index\" }";
        if (needDocs) o << ",\n      { \"title\": \"{{navDocs}}\", \"file\": \"docs/intro\" }";
        if (needBlog) o << ",\n      { \"title\": \"{{navBlog}}\", \"file\": \"blog/index\" }";
        o << "\n    ]\n"
          << "  },\n"
          << "  \"center\": {\n"
          << "    \"plugins\": [\"search\", \"dark-mode\", \"pager\", \"back-to-top\", \"toc\", \"code-highlight\"],\n"
          << "    \"backToTop\": { \"threshold\": 300, \"label\": \"顶部\" }\n"
          << "  },\n"
          << "  \"footer\": {\n"
          << "    \"text\": \"© 2026 " << esc_attr(stem) << " · 由 Cdocs 生成\"\n"
          << "  }\n"
          << "}\n";
    }

    // 2) sidebar/ 分文件侧边栏（新结构：每个版本 / 博客区一份独立 JSON，名字可自定义）
    //    （不再生成全局 route.json——侧边栏已按版本/区域拆分，config.json 的 site.sidebar 映射接管）
    {
        std::error_code sec;
        fs::create_directories(dir / ".Cdocs/config/sidebar", sec);
        if (needDocs) {
            std::ofstream(dir / ".Cdocs/config/sidebar/md.json")
                << "{\n  \"sidebar\": [\n"
                << "    {\n      \"title\": \"{{navGettingStarted}}\",\n      \"items\": [\n"
                << "        { \"title\": \"{{navIntro}}\", \"file\": \"docs/intro\" },\n"
                << "        { \"title\": \"{{navGuide}}\", \"file\": \"docs/guide\" }\n"
                << "      ]\n    }\n  ]\n}\n";
            if (withVersion) {
                std::ofstream(dir / ".Cdocs/config/sidebar/v1.json")
                    << "{\n  \"sidebar\": [\n"
                    << "    {\n      \"title\": \"v1 快照\",\n      \"items\": [\n"
                    << "        { \"title\": \"v1 示例\", \"file\": \"docs/old-intro\" }\n"
                    << "      ]\n    }\n  ]\n}\n";
            }
        }
        if (needBlog) {
            std::ofstream(dir / ".Cdocs/config/sidebar/blog.json")
                << "{\n  \"sidebar\": [\n"
                << "    {\n      \"title\": \"博客\",\n      \"items\": [\n"
                << "        { \"title\": \"示例博文\", \"file\": \"blog/hello-cdocs\" }\n"
                << "      ]\n    }\n  ]\n}\n";
            // 博客数据查询必须走插件：同步生成内置查询插件（blog-query/tags-query）
            write_query_plugins(dir);
        }
    }

    // 3) i18n 字典（双语完整，确保可构建）
    std::ofstream(dir / ".Cdocs/i18n/zh-CN.json") << kZhCN;
    std::ofstream(dir / ".Cdocs/i18n/en.json")   << kEn;

    // 3.5) 站点自定义数据示例（v6：.Cdocs/data/*.json 任意 KV，合并进页面数据作用域——
    //     组件数据孔 {{products.0.name}} 等命中即取；没 kv 就算了，走内置数据层）
    std::ofstream(dir / ".Cdocs/data/site.json")
        << "{\n"
        << "  \"site_author\": \"Cdocs 团队\",\n"
        << "  \"products\": [\n"
        << "    { \"name\": \"Cdocs 生成器\", \"price\": \"免费\" },\n"
        << "    { \"name\": \"Cdocs 主题包\", \"price\": \"开源\" }\n"
        << "  ]\n"
        << "}\n";

    // 4) 页面地图注册表（v2：C++ 构建时读此配置了解有哪些站点地图；地图本体在 theme/map/，JSON 约定）
    std::ofstream(dir / ".Cdocs/config/map.json")
        << "{\n"
        << "  \"maps\": [\n"
        << "    { \"type\": \"home\", \"map\": \"map/home.json\", \"mode\": \"home\" },\n"
        << "    { \"type\": \"doc\", \"map\": \"map/doc.json\", \"mode\": \"pages\" },\n"
        << "    { \"type\": \"blog\", \"map\": \"map/blog.json\", \"mode\": \"blog-list\" },\n"
        << "    { \"type\": \"blog-post\", \"map\": \"map/blog-post.json\", \"mode\": \"blog-post\" },\n"
        << "    { \"type\": \"tags\", \"map\": \"map/tags.json\", \"mode\": \"tags\" },\n"
        << "    { \"type\": \"tag-page\", \"map\": \"map/tag-page.json\", \"mode\": \"tag-page\" },\n"
        << "    { \"type\": \"404\", \"map\": \"map/404.json\", \"mode\": \"single\", \"output\": \"404.html\" }\n"
        << "  ],\n"
        << "  \"templates\": {\n"
        << "    \"base\": \"map/base.json\"\n"
        << "  }\n"
        << "}\n";

    // 4) 示例内容（按交互选择：文档 / 博客 / 版本）
    if (needDocs) {
    std::ofstream(dir / "md/docs/intro.md") <<
        "# 欢迎使用 Cdocs\n\n"
        "Cdocs 是一个用 **C++** 编写的静态文档站生成器。你在 `md/docs/` 下写 Markdown，"
        "它生成零依赖、可离线、中英双语的静态站点。\n\n"
        "## 快速开始\n\n"
        "1. 编写文档：`md/docs/` 下新建 `xxx.md`（英文版 `xxx.en.md`）。\n"
        "2. 构建站点：`Cdocs build` 生成到 `dist/`。\n"
        "3. 本地预览：`Cdocs serve`（默认 http://localhost:8088）。\n\n"
        "> 提示：用 `Cdocs serve -o --watch` 可自动打开浏览器并在文件改动时热重载。\n\n"
        "## 特性\n\n"
        "- 多语言：通过 `{{key}}` + i18n 字典实现\n"
        "- 全文搜索、明暗主题、阅读时长、面包屑、SEO 结构化数据\n"
        "- 单文件二进制，无运行时依赖\n";
    std::ofstream(dir / "md/docs/intro.en.md") <<
        "# Welcome to Cdocs\n\n"
        "Cdocs is a **C++** static documentation site generator. Write Markdown under `md/docs/`, "
        "and it builds a zero-dependency, offline-capable, bilingual static site.\n\n"
        "## Quick Start\n\n"
        "1. Write md: add `xxx.md` under `md/docs/` (English: `xxx.en.md`).\n"
        "2. Build: `Cdocs build` outputs to `dist/`.\n"
        "3. Preview: `Cdocs serve` (default http://localhost:8088).\n\n"
        "> Tip: `Cdocs serve -o --watch` opens your browser and hot-reloads on file changes.\n\n"
        "## Features\n\n"
        "- i18n via `{{key}}` + dictionaries\n"
        "- Full-text search, dark mode, reading time, breadcrumbs, SEO structured data\n"
        "- Single-file binary, no runtime deps\n";
    std::ofstream(dir / "md/docs/guide.md") <<
        "# 使用指南\n\n"
        "本章介绍 Cdocs 的常用工作流。\n\n"
        "## 项目结构\n\n"
        "```\n"
        "my-site/\n"
        "├── .Cdocs/\n"
        "│   ├── config/   config.json + sidebar/（分文件侧边栏）\n"
        "│   ├── i18n/     zh-CN.json / en.json\n"
        "│   └── theme/    主题（assets 前端资源 + templates/layout.html 页面骨架）\n"
        "├── md/\n"
        "│   ├── docs/     *.md 文档\n"
        "│   ├── blog/     博客\n"
        "│   └── static/   静态资源\n"
        "└── dist/         生成的静态站点\n"
        "```\n\n"
        "## 新建一篇文档\n\n"
        "运行 `Cdocs add my-page`，会在 `md/docs/my-page.md` 生成文件，并自动加入当前版本侧边栏。\n\n"
        "## 代码高亮\n\n"
        "```cpp\n"
        "#include <iostream>\n"
        "int main() { std::cout << \"Hello, Cdocs\\n\"; }\n"
        "```\n\n"
        "## 数学公式\n\n"
        "行内 $E=mc^2$，块级：\n\n"
        "$$\n"
        "\\int_0^1 x^2\\,dx = \\frac13\n"
        "$$\n";
    std::ofstream(dir / "md/docs/guide.en.md") <<
        "# Guide\n\n"
        "This chapter covers common Cdocs workflows.\n\n"
        "## Project Structure\n\n"
        "```\n"
        "my-site/\n"
        "├── .Cdocs/\n"
        "│   ├── config/   config.json + sidebar/ (per-area sidebars)\n"
        "│   ├── i18n/     zh-CN.json / en.json\n"
        "│   └── theme/    theme (assets + templates/layout.html)\n"
        "├── md/\n"
        "│   ├── docs/     *.md documents\n"
        "│   ├── blog/     blog posts\n"
        "│   └── static/   static assets\n"
        "└── dist/         generated site\n"
        "```\n\n"
        "## Add a page\n\n"
        "Run `Cdocs add my-page` to create `md/docs/my-page.md` and register it in `route.json`.\n\n"
        "## Code highlighting\n\n"
        "```cpp\n"
        "#include <iostream>\n"
        "int main() { std::cout << \"Hello, Cdocs\\n\"; }\n"
        "```\n\n"
        "## Math\n\n"
        "Inline $E=mc^2$, block:\n\n"
        "$$\n"
        "\\int_0^1 x^2\\,dx = \\frac13\n"
        "$$\n";
    }   // end needDocs

    if (withVersion) {
        // 历史版本快照：md-v1/ 示例（结构同 md/：docs/ 子目录；sidebar v1.json 引用 docs/old-intro）
        fs::create_directories(dir / "md-v1", ec);
        fs::create_directories(dir / "md-v1" / "docs", ec);
        std::ofstream(dir / "md-v1/docs/old-intro.md") <<
            "---\ntitle: v1 快照示例\n---\n\n# v1 版本\n\n这是历史版本（md-v1/）的快照示例。用 `cp -r md md-v1` 可锁定当前版本。\n";
    }

    if (needBlog) {
        // 博客示例（sidebar/blog.json 引用 blog/hello-cdocs）
        fs::create_directories(dir / "md/blog", ec);
        std::ofstream(dir / "md/blog/hello-cdocs.md") <<
            "---\ntitle: 你好，Cdocs\ndate: 2026-01-01\ntags: [cdocs]\n---\n\n# 你好，Cdocs\n\n这是第一篇博客示例。写博客请放在 `md/blog/` 下。\n";
        std::ofstream(dir / "md/blog/hello-cdocs.en.md") <<
            "---\ntitle: Hello, Cdocs\ndate: 2026-01-01\ntags: [cdocs]\n---\n\n# Hello, Cdocs\n\nThis is the first sample post. Write blog posts under `md/blog/`.\n";
    }

    // 5) 页面原型（archetype）：add 命令会读取并把 {{title}}/{{date}}/{{slug}} 替换
    //    原型自带 front matter 示例，使 add 生成的文档天生可用元数据（title/date/tags）
    std::ofstream(dir / "archetypes/default.md") <<
        "---\n"
        "title: \"{{title}}\"\n"
        "date: {{date}}\n"
        "tags: []\n"
        "---\n\n"
        "# {{title}}\n\n"
        "在这里开始写作…\n";

    // 6) 预览启动器
    std::ofstream(dir / "serve.bat") <<
        "@echo off\n"
        "chcp 65001 >nul 2>&1\n"
        "cd /d \"%~dp0\"\n"
        "echo 正在启动 Cdocs 预览服务器... （按 Ctrl+C 停止）\n"
        "Cdocs.exe serve %*\n"
        "if errorlevel 1 pause\n";

    // 7) 自动构建：建完框架立即生成 dist/，让 style.css / app.js 等资源直接就位、开箱即看
    bool built = false;
    if (haveEngine) {
        fs::path saved = fs::current_path(ec);
        fs::current_path(dir, ec);
        if (!ec) {
            int rc = run_build(fs::path("md"), fs::path("dist"), false, false);
            built = (rc == 0);
            fs::current_path(saved, ec);
        }
    }

    std::cout << color::green("已创建新站点: ") << dir << "\n";
    if (copyExe && haveEngine)
        std::cout << color::green("已复制运行资源(.Cdocs)与 Cdocs.exe。\n");
    else if (haveEngine)
        std::cout << color::green("已复制运行资源(.Cdocs)。\n");
    else
        std::cout << color::warn("注意: ") << "未找到引擎资源(.Cdocs)，仅生成内容骨架，未构建。\n";
    if (built)
        std::cout << color::green("✔ 已自动构建 → dist/（含 style.css / app.js 等前端资源，可直接 serve 预览）。\n");
    std::cout << color::muted("下一步:") << "\n  cd " << dir.string()
              << "\n  " << color::cyan("Cdocs serve")     << color::muted("    # 本地预览（可加 -o 自动开浏览器、--watch 热重载）")
              << "\n  " << color::cyan("Cdocs add <名>")   << color::muted("   # 新建一篇文档并加入导航")
              << "\n  " << color::cyan("Cdocs section <名>") << color::muted(" # 添加内容区（blog / docs / md-v<版本>）") << "\n";
    return 0;
}

// section：添加一个内容区文件夹（分类）。合法名字：blog / docs / md-v<数字>
//   - blog  只能存在一份（已存在 → 拒绝）；创建 md/blog/
//   - md  只能存在一份（已存在 → 拒绝）；创建 md/ + sidebar/md.json 示例
//   - md-v<数字>  版本目录（如 md-v1、md-v2），可多个；创建目录 + sidebar/<name>.json + 映射
//   所有情况都同步 config.json 的 site.sidebar 映射与 head.nav。
int cmd_section(const std::string& name) {
    // ---- 1) 名字校验：必须是 blog / docs / md-v<数字> ----
    std::string n = name;
    for (auto& c : n) c = (char)::tolower((unsigned char)c);
    bool isBlog = (n == "blog");
    bool isDocs = (n == "docs");
    bool isVersion = false;
    if (n.rfind("md-v", 0) == 0 && n.size() > 6) {
        std::string v = n.substr(6);
        bool digits = !v.empty() && std::all_of(v.begin(), v.end(), ::isdigit);
        if (digits) isVersion = true;
    }
    if (!isBlog && !isDocs && !isVersion) {
        std::cerr << color::error("无效的内容区名 '") << name
                  << color::error("'：仅支持 blog / docs / md-v<数字>（如 md-v1、md-v2）\n");
        return 2;
    }

    std::error_code ec;
    // ---- 2) 唯一性检查（blog/docs 只能一份，版本可多个但目录不可重复） ----
    // 结构约定：md/ 下三个内容区 = docs/（文档）+ blog/（博客）+ static/（静态资源）
    fs::path docsDir = fs::path("md/docs");
    fs::path blogDir = fs::path("md/blog");
    if (isBlog && fs::exists(blogDir, ec)) {
        std::cerr << color::error("博客区已存在（md/blog/）：blog 只能有一份，不能重复添加。\n");
        return 1;
    }
    if (isDocs && fs::exists(docsDir / "intro.md", ec)) {
        std::cerr << color::error("文档区已存在（md/docs/）：docs 只能有一份。\n");
        return 1;
    }
    if (isVersion && fs::exists(fs::path(n), ec)) {
        std::cerr << color::error("版本目录已存在: ") << n << "\n";
        return 1;
    }

    // ---- 3) 读取现有 config.json（保留其他配置） ----
    json cfg = json::object();
    fs::path cfgPath = ".Cdocs/config/config.json";
    if (fs::exists(cfgPath, ec)) {
        try { cfg = json::parse(read_file(cfgPath)); } catch (...) {}
    }
    if (!cfg.contains("site") || !cfg["site"].is_object()) cfg["site"] = json::object();
    auto& sbMap = cfg["site"]["sidebar"];

    if (isBlog) {
        fs::create_directories(blogDir, ec);
        std::ofstream(blogDir / "hello-cdocs.md") <<
            "---\ntitle: 你好，Cdocs\ndate: 2026-01-01\ntags: [cdocs]\n---\n\n# 你好，Cdocs\n\n博客区（md/blog/）第一篇。\n";
        std::ofstream(blogDir / "hello-cdocs.en.md") <<
            "---\ntitle: Hello, Cdocs\ndate: 2026-01-01\ntags: [cdocs]\n---\n\n# Hello, Cdocs\n\nBlog section (`md/blog/`).\n";
        fs::create_directories(".Cdocs/config/sidebar", ec);
        std::ofstream(".Cdocs/config/sidebar/blog.json")
            << "{\n  \"sidebar\": [\n    {\n      \"title\": \"博客\",\n      \"items\": [\n"
            << "        { \"title\": \"示例博文\", \"file\": \"blog/hello-cdocs\" }\n"
            << "      ]\n    }\n  ]\n}\n";
        sbMap["blog"] = "sidebar/blog.json";
        auto& nav = cfg["head"]["nav"];
        bool has = false;
        for (auto& x : nav) if (x.value("file", "") == "blog/index") has = true;
        if (!has) nav.push_back({{"title", "{{navBlog}}"}, {"file", "blog/index"}});
        // 博客数据查询必须走插件：同步生成内置查询插件（blog-query/tags-query）
        write_query_plugins(".");
        std::cout << color::green("已创建博客区: ") << blogDir << "\n";
    } else if (isDocs) {
        fs::create_directories(docsDir, ec);
        std::ofstream(docsDir / "intro.md") << "# 欢迎\n\n在 `md/docs/` 下写 Markdown。\n";
        std::ofstream(docsDir / "guide.md") << "# 使用指南\n\n文档示例。\n";
        fs::create_directories(".Cdocs/config/sidebar", ec);
        std::ofstream(".Cdocs/config/sidebar/md.json")
            << "{\n  \"sidebar\": [\n    {\n      \"title\": \"{{navGettingStarted}}\",\n      \"items\": [\n"
            << "        { \"title\": \"{{navIntro}}\", \"file\": \"docs/intro\" },\n"
            << "        { \"title\": \"{{navGuide}}\", \"file\": \"docs/guide\" }\n"
            << "      ]\n    }\n  ]\n}\n";
        sbMap["md"] = "sidebar/md.json";
        auto& nav = cfg["head"]["nav"];
        bool has = false;
        for (auto& x : nav) if (x.value("file", "") == "docs/intro") has = true;
        if (!has) nav.push_back({{"title", "{{navDocs}}"}, {"file", "docs/intro"}});
        std::cout << color::green("已创建文档区: ") << docsDir << "\n";
    } else {   // isVersion
        fs::create_directories(fs::path(n), ec);
        fs::create_directories(fs::path(n) / "docs", ec);
        std::ofstream(fs::path(n) / "docs/index.md") <<
            "---\ntitle: " << n << " 快照\n---\n\n# " << n << "\n\n历史版本快照目录。\n";
        fs::create_directories(".Cdocs/config/sidebar", ec);
        std::ofstream(".Cdocs/config/sidebar/" + n + ".json")
            << "{\n  \"sidebar\": [\n    {\n      \"title\": \"" << n << "\",\n      \"items\": [\n"
            << "        { \"title\": \"快照首页\", \"file\": \"docs/index\" }\n"
            << "      ]\n    }\n  ]\n}\n";
        sbMap[n] = "sidebar/" + n + ".json";
        std::cout << color::green("已创建版本区: ") << n << "\n";
    }

    // ---- 4) 写回 config.json ----
    if (!cfg.empty()) {
        std::ofstream(cfgPath) << cfg.dump(2, ' ', false, json::error_handler_t::replace) << "\n";
    }
    std::cout << color::muted("已同步 config.json（site.sidebar 映射）。\n")
              << color::muted("构建预览: ") << color::cyan("Cdocs build && Cdocs serve") << "\n";
    return 0;
}

// add：新建一篇文档（对标 hugo new content/...），并自动登记到当前版本的侧边栏（sidebar/md.json）
static std::string current_date() {
    std::time_t now = std::time(nullptr);
    struct tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tmv);
    return std::string(buf);
}

int cmd_add(const std::string& name) {
    fs::path p(name);
    std::string stem = p.stem().string();
    if (stem.empty()) {
        std::cerr << color::error("无效的页面名: ") << name << "\n";
        return 1;
    }
    std::string title = pretty_title(stem);

    // 读取原型模板（若站点提供 archetypes/default.md），替换 {{title}}
    std::string body = "# " + title + "\n\n在这里开始写作…\n";
    fs::path arch = "archetypes/default.md";
    if (fs::exists(arch)) {
        std::string t = read_file(arch);
        auto sub = [&](const std::string& tok, const std::string& val) {
            size_t pos = 0;
            while ((pos = t.find(tok, pos)) != std::string::npos) {
                t.replace(pos, tok.size(), val);
                pos += val.size();
            }
        };
        sub("{{title}}", title);
        sub("{{date}}", current_date());
        sub("{{slug}}", stem);
        body = t;
    }

    fs::path doc = "md/docs/" + stem + ".md";
    fs::path docEn = "md/docs/" + stem + ".en.md";
    if (fs::exists(doc)) {
        std::cerr << color::error("文档已存在: ") << doc << "\n";
        return 1;
    }
    std::ofstream(doc)   << body;
    std::ofstream(docEn) << "# " << title << "\n\nStart writing…\n";

    // 登记到当前版本文档侧边栏 sidebar/md.json（追加到第一个分组，无分组则新建）
    // 旧站点的全局 route.json 兼容：仅当 sidebar/md.json 不存在时回退登记到 route.json
    fs::path sp = ".Cdocs/config/sidebar/md.json";
    fs::path rp = ".Cdocs/config/route.json";
    fs::path target = fs::exists(sp) ? sp : rp;
    json rj = json::object();
    if (fs::exists(target)) {
        try { rj = json::parse(read_file(target)); } catch (...) { rj = json::object(); }
    }
    if (!rj.contains("sidebar") || !rj["sidebar"].is_array()) rj["sidebar"] = json::array();
    auto& sb = rj["sidebar"];
    json item = json::object();
    item["title"] = title;
    item["file"]  = stem;
    if (!sb.empty() && sb[0].is_object() && sb[0].contains("items") && sb[0]["items"].is_array())
        sb[0]["items"].push_back(item);
    else {
        json grp = json::object();
        grp["title"] = "文档";
        grp["items"] = json::array();
        grp["items"].push_back(item);
        sb.push_back(grp);
    }
    std::ofstream(target) << rj.dump(2, ' ', false, json::error_handler_t::replace) << "\n";

    std::cout << color::green("已创建文档: ") << doc << color::muted(" (+ ") << docEn << ")\n"
              << color::green("已加入导航: ") << title
              << color::muted("（" + target.string() + "）\n")
              << color::muted("构建预览: ") << color::cyan("Cdocs serve --watch") << "\n";
    return 0;
}

// clean：清空输出目录（对标 jekyll clean / docusaurus clear）
int cmd_clean() {
    std::error_code ec;
    if (!fs::exists(g_dest, ec)) {
        std::cout << color::muted("无需清理：") << g_dest << color::muted(" 不存在。\n");
        return 0;
    }
    uintmax_t n = fs::remove_all(g_dest, ec);
    if (ec) {
        std::cerr << color::error("清理失败: ") << ec.message() << "\n";
        return 1;
    }
    std::cout << color::green("已清理 ") << g_dest << color::muted("（移除 ") << n
              << color::muted(" 项）\n");
    return 0;
}
