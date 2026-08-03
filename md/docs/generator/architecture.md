# 架构一览

Cdocs 的核心设计是**数据驱动 + 引擎收口**：所有"引擎"相关文件统一收口在隐藏目录 `.Cdocs/` 内，用户侧只维护 `docs/`（内容）与 `dist/`（产物）。

## 整体数据流

```mermaid
flowchart LR
  C["config.json"] --> G["Cdocs.exe"]
  R["route.json"] --> G
  L["i18n 字典"] --> G
  D["md/docs/*.md"] --> G
  A["assets/ 前端"] --> G
  G --> DIST["dist/ 纯静态（HTML · SEO · RSS · PWA · search.json）"]
```

## 生成器：22 个 C++ 模块

生成器源码在 `src/`，按职责拆成模块（每个含 `.hpp` + `.cpp`），依赖方向严格向下：

| 层 | 模块 | 职责 |
|----|------|------|
| 装配 | `main.cpp` | 纯装配：include 全部头 + 定义全局 + `main()` |
| 底座 | `core` | 共享类型 / 全局 extern / 工具函数 / 信号处理 |
| 领域 | `frontmatter` | YAML front matter 解析 |
| 领域 | `i18n` | 字典加载 + `{{key}}` 替换 |
| 领域 | `config` | config.json / route.json 解析 |
| 领域 | `pages` | 导航树 / 收集 / 面包屑 / 草稿过滤 / TOC |
| 领域 | `feeds` | RSS 2.0 + JSON Feed |
| 领域 | `pwa` | manifest + service worker |
| 领域 | `search` | search.json 索引 |
| 领域 | `server` | 内置 HTTP 预览 + 文件 watch |
| 领域 | `linkcheck` | 死链检查（构建期扫描站内链接） |
| 领域 | `compress` | 构建期压缩（WebP 图片 + HTML/CSS） |
| 领域 | `deploy` | git 推送 / Vercel / 自动化部署配置生成 |
| 领域 | `plugin` | 外部脚本钩子（on_config / on_done …） |
| 渲染 | `markdown` | md4c 封装 + Admonitions 展开 |
| 渲染 | `component` | 组件加载 / 数据孔填充 / 地图 sections 组合 / 站点数据 |
| 渲染 | `shortcode` | 正文 shortcode 引擎（预扫描 / 展开 / style 去重 / 转义） |
| 构建 | `builder` | `run_build` 纯编排（配置→收集→渲染），`render_one_locale` 单语言渲染子函数 |
| 构建 | `versions` | 多版本分派（config.versions 探测 + md-* 快照约定 + 根重定向） |
| 构建 | `output` | 构建收尾产物（根重定向 / feed+PWA / sitemap / robots / 汇总 / 残留检测） |
| 脚手架 | `scaffold` | `init` / `section` / `new` / `clean` 站点骨架命令 |
| 编排 | `cli` | 命令注册表分发 / 统一 flag 解析 / 帮助 / 退出码 |
| 诊断 | `diag` | `doctor` / `check` / `config` / `routes` / `theme` / `plugins` / `versions` 诊断命令 |

> 演进：早期把渲染部件与脚手架命令都堆在 `builder.cpp`（一度 3200 行）；v2 拆出 `component` / `shortcode` / `scaffold` / `diag`；v3 再拆 `versions`（多版本分派）与 `output`（构建收尾），并把 `render_locales`（762 行）的单语言渲染循环体提取为 `render_one_locale` 子函数，`builder` 最终只留编排主流程。

## run_build：纯编排

`builder.cpp` 的核心 `run_build()` 是「BuildContext + 8 个阶段函数」的纯编排：共享状态集中在 `BuildContext` 结构体，各阶段函数用局部引用别名绑定成员，逻辑逐字保留。

1. `load_site_config` — 配置 + 导航 + 插件渲染开关
2. `prepare_pages` — 输入检查 + 收集页面 + 预扫描 front matter
3. `render_locales` — 多语言构建循环（每语言调 `render_one_locale`）
   - `render_one_locale` — 单语言渲染（assets/压缩/指纹/首页/文档页/博客/标签/404/RSS/PWA）
4. `write_root_redirect` — 多语言根 index.html 重定向
5. `write_root_feeds_pwa` — 根目录默认语言 feed / PWA
6. `write_sitemap` — sitemap.xml
7. `write_robots` — robots.txt
8. `print_summary` — 汇总输出

> `versions.cpp` 在 `run_build` 最外层执行版本分派（`dispatch_versions`）：命中多版本（config.versions 或 md-* 快照约定）则对每个版本独立调 `run_build` 并生成根重定向；单版本/子构建重入时不处理，走下方主流程。`output.cpp` 承载 4-9 阶段的收尾产物函数。

## 前端：引导 + ESM 模块图

`assets/app.js` 只做一件事：动态 `import('./js/main.js')` 加载 ESM 模块图。交互增强（主题 / 代码块 / 提示框 / 图表 / 搜索 / 命令面板 / 灯箱 / PWA）都是 `features/*.js` 里的 `initX()` 模块——**无需修改 C++ 生成器即可加前端功能**。

> 约定：Admonitions（`> [!type]`）与 shortcode 组件是**构建期渲染**，静态 HTML 里直接可见；Mermaid、KaTeX 是**客户端 JS 懒加载升级**。

## 构建链：两步，零依赖

```text
build.cmd → ① 编译 20 个 C++ 源（g++ -std=c++17）+ 3 个 vendor C 源（md4c，用 gcc）
           → ② Cdocs.exe build 生成完整 dist/（RSS / Feed / PWA / SEO 全内建）
```

编译要点：md4c 是 C 源必须用 `gcc`；链接必须 `-static -static-libgcc -static-libstdc++`（自带运行时）；Windows 加 `-lws2_32`（serve 用 winsock）。

详细说明见项目根 `ARCHITECTURE.md`。
