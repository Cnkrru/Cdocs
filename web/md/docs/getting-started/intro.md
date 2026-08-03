# 介绍

欢迎使用 **Cdocs** —— 一个用 C++ 编写、数据驱动的极简静态文档站点生成器（SSG）。

它把 `md/docs/` 下的 Markdown 源文，连同 `.Cdocs/` 里的配置、导航、多语言文案与前端资源，编译成一个**纯静态、可离线、零运行时依赖**的站点 `dist/`。

## 它解决什么问题

传统文档工具要么依赖 Node/Python 运行时，要么配置分散、改样式要动源码。Cdocs 的设计目标是：

- **单文件 CLI**：一个 `Cdocs.exe`（或 `Cdocs`）搞定「编译 → 生成 → RSS → PWA」，无需安装 Node/Python 即可 `serve` 预览。
- **数据驱动**：站点标题、导航、主题、多语言全部由 JSON 表达，改文案/结构不动 C++。
- **离线可用**：Mermaid / KaTeX / highlight.js / FlexSearch 等第三方库随站发布，断网也能用。
- **SEO 友好**：构建时落地 JSON-LD、canonical、sitemap `hreflang`、各语言搜索索引。

## 核心特性

- **命令行**：`init` / `new` / `section` / `build` / `serve` / `deploy` / `clean` + 诊断命令 `doctor` / `check` / `config` / `routes` / `theme` / `plugins` / `versions`
- **内容**：Markdown（基于 md4c，支持 GFM 扩展：表格、任务列表、删除线）；`md/` 唯一根（docs + docs-v<v> 版本 + blog）
- **导航**：配置驱动的分文件侧边栏（`route/`），支持最多 6 层嵌套与分组折叠、移动端抽屉
- **搜索**：客户端全文搜索（FlexSearch），标题/正文分域 + 命中高亮
- **主题**：多主题仓库（`themes/`：ink 水墨 / paper 纸质 / frost 玻璃拟态），明暗双主题，`themeName` 一键换肤
- **增强**：代码高亮 / 复制按钮、Admonitions 提示框、`Mermaid` 流程图、KaTeX 公式
- **正文组件**：`<Tabs/>` `<Expand/>` `<CodeGroup/>` `<Badge/>` 短代码（标签语法，组件样式自包含）
- **体验**：⌘K 命令面板、图片灯箱、打印 / 导出 PDF、「本页有帮助吗？」反馈
- **多语言**：i18n（`{{key}}` + 扁平 JSON 字典）
- **版本化**：多版本文档站（`site.versions` 显式声明 + 快照约定），版本下拉切换、保持语言
- **插件化**：数据查询（博客流/标签聚合）100% 走 Python 插件，`on_config` / `on_data_query` / `on_page_rendered` / `on_done` / `setup` 钩子
- **发布**：每页 SEO、sitemap、robots、RSS 2.0 / JSON Feed、PWA 离线

## 三步上手

```bash
# 方式 A：全局安装（发布包 release/ 加入 PATH 后）
Cdocs init mysite         # 建站（自动复制引擎 + 自动构建，开箱即看）
cd mysite
Cdocs serve               # 本地预览（内置 C++ 服务器，默认 http://localhost:8088）

# 方式 B：源码构建
.Cdocs\tools\build.cmd    # Windows 一键构建（编译 → 生成 → RSS → PWA）
bash .Cdocs/tools/build.sh  # Linux / macOS
```

> 提示：`serve` 内置一个 C++ 写的 HTTP 服务器，**无需安装 Python 或 Node**，仅监听本机 `127.0.0.1`；改完 `md/` 重跑即可刷新预览。

## 接下来

- 想了解命令用法与写作语法，读 [使用指南](guide.html)。
- 想看命令行与配置文件字段，读 [接口说明](../reference/api.html)。
- 想了解「从 Markdown 到上线」的完整链路，读 [渲染管线](../generator/pipeline.html)。
