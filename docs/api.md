# API 参考

Cdocs 是一个**命令行驱动的静态文档站生成器**，不对外暴露 C++ 库接口；所有扩展能力通过三个层面暴露：

| 层面 | 位置 | 说明 |
| --- | --- | --- |
| 配置 | `.Cdocs/config/config.json` + `route.json` | 站点元数据、功能开关、导航 |
| 主题 | `.Cdocs/theme/` | 页面骨架（layout.html）+ 前端资源（assets/），换主题 = 换文件夹 |
| 插件 | `.Cdocs/plugins/<name>/plugin.json` | 外部脚本挂接构建生命周期钩子 |

本文档依次覆盖：**命令**、**配置**、**主题**、**插件**、**内容 front matter**、**多语言**、**版本化**、**博客流**、**导航**、**增量构建**。

---

## 1. 命令参考

| 命令 | 说明 | 常用旗标 |
| --- | --- | --- |
| `Cdocs init <目录>` | 新建完整站点骨架（config/route/i18n/示例文档/主题资源）并自动构建 | `--no-engine` 仅生成内容骨架 |
| `Cdocs new <页面名>` | 新建内容页（从 `archetypes/default.md`）并登记导航（别名 `add`/`page`） | — |
| `Cdocs build [源] [目标]` | 构建站点，默认 `docs` → `dist` | `-D/--drafts` 含草稿、`--clean` 构建前清空 |
| `Cdocs serve` | 构建并启动本地预览服务器（内置 C++ HTTP，默认 `8088`） | `-p/--port`、`-o/--open`、`-w/--watch` 热重载、`--no-build` |
| `Cdocs deploy` | 构建并推送到远端分支（默认 `gh-pages`），对标 `mkdocs gh-deploy` | `--remote <url>`、`--branch <b>`、`-m <msg>`、`--force` |
| `Cdocs clean` | 清空输出目录（对标 `jekyll clean`） | — |
| `Cdocs version` / `-v` | 显示版本号 | — |
| `Cdocs help` / `-h` | 显示帮助 | — |

### deploy 详解

```
Cdocs deploy [--remote <url>] [--branch <b>] [-m <msg>] [--force] [--vercel]
```

**GitHub Pages（默认）**——内置 git 调用，零外部脚本依赖：

1. 检查 `git` 可用（需先安装 Git 并加入 PATH）。
2. **构建站点**：默认全量构建；`dist` 已有 `.git` 时不清空（保留部署历史），否则首次部署自动清空保证产物干净。
3. 写入 `dist/.nojekyll`（GitHub Pages 不经过 Jekyll）。
4. 在 `dist` 内执行 git：未初始化则 `git init` + 孤儿分支；已初始化则切换到目标分支；`git add -A` + `git commit`（无变更时跳过）。
5. **远端解析顺序**：`--remote` > config `site.deploy.remote` > 已有 `origin` > 从 `site.url` 推断（`https://user.github.io/repo/` → `https://github.com/user/repo.git`）。
6. `git push -u origin <branch>`。

**Vercel（`--vercel`）**——构建后调 vercel CLI 发布生产环境：

- 执行 `vercel --prod --yes dist`（自动检测：PATH 里的 `vercel`，缺失则用 `npx --yes vercel@latest`）。
- 依赖 Node.js + Vercel CLI：首次 `npm i -g vercel && vercel login`（或设 `VERCEL_TOKEN`）。
- 首次部署 vercel 会提示关联项目，之后一条命令即发布；部署 URL 由 vercel CLI 输出。

```bash
# 首次部署到 GitHub Pages（自动推断远端）
Cdocs deploy

# 显式指定远端与分支
Cdocs deploy --remote https://github.com/me/docs.git --branch gh-pages -m "docs: v2.0"

# 构建前清空输出目录
Cdocs deploy --force

# 发布到 Vercel 生产环境（需已安装并登录 vercel CLI）
Cdocs deploy --vercel
```

### 全局旗标（放在子命令前）

| 旗标 | 说明 |
| --- | --- |
| `-c, --config <目录>` | 引擎/配置根目录（默认 `.Cdocs`） |
| `-s, --source <目录>` | Markdown 源目录（默认 `docs`） |
| `-d, --dest <目录>` | 输出目录（默认 `dist`） |
| `-q, --quiet` / `-V, --verbose` | 静默 / 详细输出 |

退出码：`0` 成功，`1` 运行错误，`2` 用法错误。

---

## 2. 站点配置 config.json

配置位于 `.Cdocs/config/config.json`，按**三区块**组织：`site`（全局）/ `head`（页眉）/ `center`（内容与功能）/ `footer`（页脚）。旧式顶层结构（无 `site` 键）仍兼容解析。

```json:.Cdocs/config/config.json
{
  "site": {
    "title": "{{siteTitle}}",
    "description": "{{siteDesc}}",
    "theme": "dark",
    "url": "https://docs.example.com",
    "ogImage": "/icon.svg",
    "i18n": {
      "defaultLocale": "zh-CN",
      "dir": ".Cdocs/i18n",
      "locales": { "zh-CN": { "label": "简体中文" }, "en": { "label": "English" } }
    },
    "editLink": { "base": "https://github.com/me/docs/edit/main", "docsDir": "docs" },
    "themeVars": { "--radius": "12px", "--sidebar-left-w": "248px" },
    "customCss": ".Cdocs/theme/assets/css/custom.css",
    "compress": true,
    "jpegQuality": 82,
    "deploy": { "remote": "", "branch": "gh-pages", "message": "" }
  },
  "head": {
    "logo": "{{brand}}",
    "showSearch": true,
    "showThemeToggle": true,
    "github": "https://github.com/me/docs",
    "links": [ { "title": "{{navProject}}", "url": "https://github.com/me/docs" } ],
    "nav": [ { "title": "{{navIntro}}", "file": "intro" } ]
  },
  "center": {
    "plugins": ["search", "dark-mode", "pager", "back-to-top", "toc", "code-highlight"],
    "backToTop": { "threshold": 300, "label": "顶部" },
    "comments": {
      "provider": "giscus",
      "repo": "me/docs",
      "repoId": "...",
      "category": "Announcements",
      "categoryId": "...",
      "mapping": "pathname",
      "theme": "preferred_color_scheme"
    }
  },
  "footer": {
    "text": "{{footerText}}",
    "links": [ { "title": "{{navProject}}", "url": "https://github.com/me/docs" } ]
  }
}
```

### site（全局）

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `title` / `description` | string | 站点标题/描述，支持 `{{key}}` 走 i18n |
| `theme` | `light`/`dark` | 默认主题色 |
| `url` | string | 站点真实域名（canonical / sitemap / RSS / deploy 远端推断的依据） |
| `ogImage` | string | 社交分享封面图（og:image / twitter:image） |
| `i18n` | object | 多语言：`defaultLocale` 默认语言、`dir` 字典目录、`locales` 各语言 `label` |
| `editLink` | object | 「编辑此页」链接 = `base` + `docsDir` + `<文件>.md` |
| `themeVars` | object | CSS 变量覆盖，注入 `<style>`（见主题 API） |
| `customCss` | string | 自定义 CSS 文件路径，存在则复制并链接 |
| `compress` | bool | 构建期压缩（图片 + HTML/CSS），默认 `true` |
| `jpegQuality` | int | JPEG 重压质量（1-100），默认 `82` |
| `deploy` | object | `Cdocs deploy` 默认值：`remote`/`branch`/`message`（均可被 CLI 旗标覆盖） |
| `home` | object | 首页：`hero`（title/subtitle/cta）与 `cards` 白名单（`file`/`title`/`desc`），空 cards = 自动列出全部页面 |

### head（页眉）

| 字段 | 说明 |
| --- | --- |
| `logo` | 品牌标识文本（可 `{{key}}`） |
| `showSearch` | 显示搜索框（默认 true） |
| `showThemeToggle` | 显示明暗切换按钮 |
| `github` | GitHub 仓库地址，显示经典图标按钮（空则不显示） |
| `links` | logo 旁导航链接：`{title, file|url}` |
| `nav` | 页眉右侧导航（i18n 按钮左侧，最多渲染 6 个） |

### center（内容与功能）

| 字段 | 说明 |
| --- | --- |
| `plugins` | 功能开关数组：`search` / `dark-mode` / `pager` / `back-to-top` / `toc` / `code-highlight`，空 = 全部启用 |
| `backToTop` | `threshold` 滚动触发阈值、`label` 文案 |
| `comments` | 评论系统：内置 **Giscus**（GitHub Discussions 驱动，无后端）。`repo` + `repoId` + `categoryId` 配齐才注入；缺任一 = 不启用 |

### footer（页脚）

| 字段 | 说明 |
| --- | --- |
| `text` | 页脚文案（可 `{{key}}`） |
| `links` | 页脚链接列表 |

---

## 3. 主题 API

一个主题 = 一个文件夹 `.Cdocs/theme/`，**换主题 = 替换整个文件夹**（完整规范见 [主题开发](./themes)）。

```
.Cdocs/theme/
├── theme.json          主题元数据（name/version/description…）
├── templates/
│   └── layout.html     页面骨架模板（占位符注入，缺失回退内置骨架）
└── assets/             前端资源（整目录拷入 dist/assets/）
```

- `layout.html` 用 `{{key}}` 占位符接收引擎生成的子块：`{{header}}` / `{{left_nav}}` / `{{body}}` / `{{pager}}` / `{{footer}}` 等（全表见主题文档）。
- `assets/` 原样复制到 `dist/<loc>/assets/`，浏览器 URL 不变；构建产物额外生成 `assets/deps/`（运行时依赖）与 `assets/search.json`（搜索索引）。
- 旧结构（`.Cdocs/assets/` + `.Cdocs/templates/` 直放引擎根）自动兼容。
- `config.site.themeVars` 覆盖主题公开的 CSS 变量；`config.site.customCss` 追加自定义样式。

---

## 4. 插件 API

外部脚本通过 JSON 文件交换协议挂接构建生命周期，失败自动隔离（完整规范见 [插件开发](./plugins)）。

```
.Cdocs/plugins/<插件名>/plugin.json
→  { "name": "...", "hooks": { "<钩子>": { "cmd": "...", "timeout": 30 } } }
```

| 钩子 | 时机 |
| --- | --- |
| `on_config` | 配置加载完成后 |
| `on_page_collected` | 页面收集完成后（上下文含 `pages[]`） |
| `on_page_rendered` | 每个页面渲染后 |
| `on_done` | 全部产物生成后（部署/通知收尾） |

调用协议：`<cmd> <ctx.json> <out.json>`，工作目录 = 插件目录；插件可写 `{ "ok": bool, "message": "…" }` 展示构建结果。

---

## 5. 内容 front matter

`docs/` 下的 Markdown 文件可选 YAML front matter（`---` 包裹）：

```markdown
---
title: "我的页面"        # 显示标题（缺省用文件名美化）
date: 2026-08-01         # 发布时间（feed/博客排序用；缺省用文件 mtime）
draft: true              # 草稿：true 时不发布（build 需 -D 才包含）
weight: 10               # 排序权重（越小越靠前）
tags: [入门, 指南]        # 标签（聚合到 tags 页）
lastmod: 2026-08-02      # 修改时间（优先于文件 mtime）
aliases: [old-path]      # 旧路径，自动生成重定向页
---
```

多语言正文约定：`xxx.md`（默认语言）+ `xxx.en.md`（英文版）并存时按语言配对；缺失的语言回退默认语言。

---

## 6. 多语言（i18n）

- **字典**：`.Cdocs/i18n/<locale>.json`，扁平 `key → value`；`config.site.i18n.locales` 登记语言与显示名。
- **引用**：配置 / 模板 / 正文里写 `{{key}}`，构建时按当前语言查表替换（`<pre>`/`<script>`/`<style>` 内跳过，避免破坏代码与 JSON-LD）。
- **输出**：每种语言独立目录 `dist/<loc>/`，根 `index.html` 重定向到默认语言；未开启 i18n 时单语言输出到根。
- **子目录**：`blog/`、`tags/`、`page/N` 等子目录页面自动加 `../` 相对基址（relBase），贯穿模板/导航/面包屑/上下篇/语言切换。

---

## 7. 版本化文档

Docusaurus 风格多版本：**显式配置优先，约定优于配置**。

- **显式**：`config.json` 的 `site.versions` 声明版本列表、label、源目录与默认版本。
- **约定**：`docs-*` 目录自动识别为历史版本（如 `docs-v1`），当前版本 = `docs`。每个版本独立构建产物，根目录重定向到当前版本，页头版本下拉切换。

---

## 8. 博客流

`docs/blog/` 目录约定启用（类似 Hugo 的 `content/blog`）：

- `docs/blog/xxx.md`（+ `xxx.en.md`）→ 博客列表 + 详情页，日期倒序，分页（10 篇/页）。
- 详情页含日期、阅读时长、上下篇导航。
- 博客文章并入 RSS / JSON Feed、搜索索引与标签聚合。
- front matter `date` 决定排序；`draft: true` 默认不发布。

---

## 9. 导航 route.json

`.Cdocs/config/route.json` 的 `sidebar` 数组定义侧边栏，最多 6 层嵌套：

```json
{
  "sidebar": [
    {
      "title": "{{navGettingStarted}}",
      "items": [
        { "title": "{{navIntro}}", "file": "intro" },
        { "title": "{{navGuide}}", "file": "guide" },
        { "title": "外部链接", "url": "https://example.com" }
      ]
    }
  ]
}
```

- `title`：分组或条目标题，支持 `{{key}}`。
- `file`：对应 `docs/<file>.md`，生成 `dist/<loc>/<file>.html`。
- `url`：外链（与 `file` 二选一）。
- `items`：下一层导航。

---

## 10. 增量构建与开发体验

- **增量**：`serve -w` 下按指纹跳过未变页面。全局签名（config/route/i18n/theme 的 mtime 峰值，存 `.Cdocs/.build/.sig`）+ 页面指纹（`mtime:size`，存 `.pages.sig`）+ 资源签名（`.assets.sig`）三层判定。
- **自动刷新**：构建完成会更新 `/__cdocs_epoch` 端点，浏览器轮询后整页 reload，改文件即所见即所得。
- **新建站点**：`Cdocs init <目录>` 一次生成完整骨架（含主题资源与 `Cdocs.exe`），开箱即构建。
