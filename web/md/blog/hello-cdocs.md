---
title: "Cdocs 的开发手记：从单文件生成器到可部署的产品"
date: 2026-08-04
tags: [C++, 静态站点, 工程实践]
---

Cdocs 是一个用 C++17 编写的静态文档站生成器：单文件 exe、零运行时依赖、数据驱动。这篇博客记录它从"一个能跑的脚本"变成"一个能部署的产品"过程中的关键设计决策——也是本网站所有文档背后的工程故事。

## 为什么用 C++ 写一个文档生成器

市面上的 SSG（Hugo、Docusaurus、VitePress）都很成熟，为什么还要自己写一个？核心诉求只有一条：**零运行时依赖**。

- Hugo 是 Go 单二进制——但它内置了主题语言（模板 + shortcode），扩展要学它的 DSL；
- Docusaurus/VitePress 需要 Node 运行时，构建链重；
- 我们想要的是：`Cdocs.exe build` 一条命令，任何机器都能跑，**不装 Node、不装 Python、不装 Go**。

C++ 静态链接（`-static -static-libgcc -static-libstdc++`）天然满足这个诉求：Markdown 渲染用成熟的 [md4c](https://github.com/mity/md4c)（C 库），JSON 解析用 [nlohmann/json](https://github.com/nlohmann/json)（header-only），图片压缩用 libwebp，gzip 用 zlib——全部静态编译进一个 10MB 的 exe。

## 数据驱动：引擎不硬编码任何页面

Cdocs 的核心设计是 **JSON 地图（map）驱动渲染**。页面结构不是写在 C++ 里，而是用 JSON 描述：

```json
{ "component": "TopNav", "if": "header.topnav",
  "sections": [ { "component": "TopNavLink", "each": "header.topnav" } ] }
```

引擎只做两件事：**产数据**（导航树、目录、正文、分页，全部是 JSON）和**拼页面**（按地图把组件组合起来）。条件、循环、嵌套都是 JSON 字段，组件本身是纯 HTML + `{{数据孔}}`——没有任何模板控制流语法。

好处：**换主题 = 换一个文件夹**（`themes/ink` → `themes/frost`），加页面类型 = 在 `map.json` 注册一项，全程不改 C++。

## 查询 100% 插件化

最反直觉的一个决策：**博客流排序、标签聚合等"查询"逻辑完全不在 C++ 里**，而是放到 Python 插件（`.Cdocs/plugins/*/scripts/*.py`）。

引擎在构建时产一份全量数据快照（`ctx.json`），调用插件脚本，插件把结果写回 `out.json`，引擎据此渲染：

```python
# blog-query 插件：筛选 blog/* 文章，按日期倒序，分页
posts = [p for p in ctx.get("pages", [])
         if p.get("file", "").startswith("blog/") and not p.get("draft")]
posts.sort(key=lambda p: p.get("dateT_iso") or "", reverse=True)
order = [p["file"] for p in posts]
out = {"ok": True, "blog_order": order, "blog_pages": [...]}
```

为什么？因为**查询规则会变**（每页几条、首页放几篇、要不要置顶），放引擎里每改一次都要重编译；放插件里改个 Python 脚本就行。协议是"外部进程 + JSON 文件交换"，失败自动隔离、永不阻断构建。

## 版本化：显式配置优先，约定兜底

文档站要做多版本（v1/v2），Cdocs 用 Docusaurus 风格：

- **显式**：`config.json` 声明 `site.versions`（name/label/source/default），引擎按列表分派构建；
- **约定**：没配置时自动扫描 `<源目录>-*` 快照目录，识别为历史版本。

每个版本独立构建成完整站点（含各自的 i18n / RSS / PWA），页头版本下拉切换、保持当前语言。这个功能踩过的坑是：**多版本共用一个 config，旧版本没有新页面属正常**——导航过滤器必须剔除指向不存在页面的链接，否则全是死链（本站在 v1 只有占位页就是典型场景）。

## 目录重构：web/ 与 bin/ 的分离

项目做到后期，我们把仓库整理成两个清晰区域：

```
Cdocs/
├── src/    # 引擎 C++ 源码
├── web/    # 站点根：md/ + .Cdocs/ + themes/ + vercel.json
└── bin/    # 分发包：Cdocs.exe + serve.bat + .Cdocs（克隆即可用）
```

- `web/` 是**可独立构建的站点根**：`cd web && Cdocs build`；
- `bin/` 是**开箱分发包**：加入 PATH 后任意目录 `Cdocs init/build/serve`，等价 Hugo 的 release 包；
- CI 里 `bash web/.Cdocs/tools/build.sh` 一步完成"编译生成器 + 构建站点"。

这个重构最大的教训是**路径引用**：build 脚本、部署 workflow、插件模板里凡是引用 `src/`、`Cdocs`、`dist` 的地方，全部要跟着新结构改一遍——漏一处，CI 就挂一处（我们连续踩了 3 个：`../Cdocs` 不存在、`./Cdocs` 少了 build 参数、`cp Cdocs` 源路径错）。

## CI 部署踩坑：CSS"丢失"的真相

上线时遇到一个诡异问题：**部署后 CSS 样式丢失**。排查后根因链条是：

1. `build.sh` 里 `./Cdocs` 无参数运行只打印帮助、**不构建**（exit 0 但不产出 dist）；
2. `deploy.yml` 又补了一步 `cd web && ../Cdocs build`，但 `../Cdocs` 不存在 → exit 127；
3. 于是**每次部署都失败**，GitHub Pages 一直保留最早一次成功的旧单版本产物 → 看起来"CSS 丢了"。

教训：**CI 脚本的"成功"要验证产物存在，不能只看 exit code**。现在 build.sh 用 `./Cdocs build` 显式构建，workflow 上传 `web/dist`，线上才与本地一致。

## 现在的样子

- 25 个 C++ 模块，无超 150 行函数；
- 3 套主题（水墨 ink / 纸质 paper / 玻璃拟态 frost）；
- 6 个插件（查询/评论/统计/部署）；
- 双版本（v2 当前 + v1 快照）、中英双语；
- GitHub Actions 自动部署到 Pages，push 即上线。

如果你也想写一个"小而美"的工具，Cdocs 的源码和文档都在 GitHub 上，欢迎围观。
