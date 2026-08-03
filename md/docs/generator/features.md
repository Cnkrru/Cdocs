# 功能特性

Cdocs 的功能分两类：**构建期内建**（生成器 C++ 直接产出）与**客户端增强**（浏览器里懒加载 JS 升级）。

## 构建期内建

| 功能 | 产物 | 说明 |
|------|------|------|
| HTML 渲染 | 各页 `.html` | Markdown → HTML（md4c），TOC / 面包屑 / 上下篇 |
| 全文搜索 | `assets/search.json` | 每语言独立索引（FlexSearch 客户端检索） |
| SEO | `<head>` meta | JSON-LD、canonical、Open Graph、Twitter Card、`hreflang` 交替链接 |
| 站点地图 | `sitemap.xml` | 多语言 URL + hreflang 交替 |
| 爬虫协议 | `robots.txt` | 允许抓取 + sitemap 地址 |
| 订阅源 | `rss.xml` / `feed.json` | RSS 2.0 + JSON Feed 1.1，每语言一份 + 根默认语言 |
| PWA | `manifest.webmanifest` / `sw.js` / `icon.svg` | 离线缓存、主题色 |
| 标签聚合 | `tags/*.html` | 按 front matter `tags` 自动生成标签页 + 总览 |
| 提示框 | 正文 HTML | `> [!tip]` Admonitions 11 种类型（构建期展开） |
| 短代码组件 | 正文 HTML | `<Tabs/>` `<Expand/>` `<CodeGroup/>` `<Badge/>` 等（标签语法，见 [shortcode 参考](../reference/shortcodes)） |
| 草稿 | — | `draft: true` 默认排除，`build -D` 包含 |
| 多语言 | `dist/<loc>/` | 每语言独立目录，根页面自动重定向默认语言 |
| 多版本 | `dist/v2/` + `dist/v1/` | `site.versions` 显式声明 + 快照约定，版本下拉切换（见 [版本化](./versioning)） |
| 多主题 | `themes/<name>/` | `themeName` 一键换肤（ink / paper / frost），主题 = map + components + assets（见 [主题开发](../reference/themes)） |
| 查询插件化 | — | 博客流排序/分页、标签聚合由 Python 插件在 `on_data_query` 钩子实现，引擎零硬编码查询（见 [插件开发](../reference/plugins)） |

## 客户端增强（features/）

| 模块 | 功能 |
|------|------|
| `theme.js` | 明暗主题切换（记忆 localStorage，首次跟随系统） |
| `code.js` | 代码块增强：文件名栏 / 行号 / 高亮行 / 语言标签 / 复制按钮 |
| `diagrams.js` | Mermaid 图表 + KaTeX 公式渲染（懒加载，含图/公式才加载库） |
| `nav.js` | 移动端抽屉、目录滚动高亮 |
| `search.js` | 顶栏搜索下拉（FlexSearch） |
| `command-palette.js` | `Ctrl+K` 命令面板 |
| `footer.js` | 页脚 RSS 入口 + 打印按钮 |
| `feedback.js` | 「本页有帮助吗？」点赞反馈 |
| `lightbox.js` | 图片点击放大 |
| `jump.js` | 跳转定位 + 高亮闪烁 |
| `pwa.js` | 注册 service worker（仅 http/https） |

## 想验证什么？

本网站左侧「功能测试」分组就是为此准备的：

- **Markdown 渲染** → 标题/表格/代码块/引用/Admonition 等
- **Mermaid 图表** → 流程图/时序图/甘特图/饼图等
- **KaTeX 公式** → 行内/块级数学公式

去翻翻那三页，顺便体验搜索（顶栏）、明暗切换（右上角）、`Ctrl+K` 命令面板。
