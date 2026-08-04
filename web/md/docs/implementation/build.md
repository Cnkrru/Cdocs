# 构建优化是如何实现的

本文说明构建期的各项优化：增量构建、资源压缩、指纹缓存、死链检查。

## 增量构建：三层签名判定

`serve -w`（热重载）下按指纹跳过未变页面，三层判定（`src/builder.cpp` + `src/output.cpp`）：

| 层 | 签名 | 存储 | 判定 |
|---|---|---|---|
| 全局 | config/route/i18n/theme 的 mtime 峰值 | `.Cdocs/.build/.sig` | 任一变化 → 全量重建 |
| 页面 | `mtime:size` | `.pages.sig` | 单页变化 → 只重渲染该页 |
| 资源 | assets 目录签名 | `.assets.sig` | 变化 → 重新拷贝+指纹 |

构建完成会更新 `/__cdocs_epoch` 端点，浏览器轮询到变化后整页 reload——**改文件即所见即所得**。

## 图片压缩（WebP）

`src/compress.cpp` 的 `webpize_file`：

- 构建期扫描内容图片，转 WebP（libwebp 静态编译进 exe）；
- **小图无收益跳过**：压缩后体积不小于原图则不生成 WebP（避免小图标被放大损）；
- 质量由 `config.site.jpegQuality` 控制（1-100，默认 82）。

## HTML/CSS 压缩

- **HTML**：`minify_html` 去除缩进空白与注释（保护 `pre`/`code`/`script` 内容不被破坏）；
- **CSS**：`minify_css` 压缩选择器空白与注释；
- 由 `config.site.compress` 开关（默认 true）。

## 资源指纹（Cache Busting）

`fingerprint_assets` + `apply_fingerprints`：

- 对 `assets/` 下 CSS/JS 计算 FNV-1a 哈希，文件名加指纹后缀（`style.css?v=f44f84cc`）；
- HTML 里所有资源引用同步改写；
- **Service Worker 缓存同步**：`sw.js` 的缓存清单也用同一指纹，避免新旧资源混用。

## 死链检查

`src/linkcheck.cpp` 构建期扫描：

- 收集全部页面产出的站内链接（`href` 相对/绝对路径）；
- 对照实际生成的文件清单，缺失即报死链（警告不阻断）；
- **检查范围**：跳过 `http(s)://` 外链、`mailto:`、`#锚点`，只查站内页面引用；
- 多版本时自动识别版本目录结构（`dist/v1`、`dist/v2`），避免把版本目录当语言目录误报。

## gzip 传输（serve）

`src/server.cpp` 内置 HTTP 服务器支持 gzip：

- 对可压缩类型（html/css/js/json/svg）动态 gzip，`Content-Encoding: gzip`；
- 带 ETag + 304 协商缓存；`assets/` 长缓存、HTML no-cache；
- `handle_conn` 单线程轮询，零外部依赖。

## 渲染性能优化（2026-08）

> 44 篇文档 × 2 语言 × 2 版本，构建耗时从 16s 优化到 ~1.5s（全量）/ ~0.7s（增量），**23 倍提速**。

### 优化清单

| 优化项 | 文件 | 效果 |
|--------|------|------|
| 标签查询内置化 | `builder.cpp` | 消除 `tags-query` Python 子进程，数据聚合在 C++ 内完成 |
| 博客查询内置化 | `builder.cpp` | 消除 `blog-query` 子进程，排序/分页/首页流逻辑内置 |
| 页面渲染全并行 | `render_pages.cpp` | 首页/文档/博客/标签/市场/404 均使用 `run_parallel` 多线程 |
| 多 locale 并行渲染 | `builder.cpp` | zh-CN 和 en 页面渲染同时执行，两倍吞吐 |
| 组件 HTML 预加载 | `component.cpp` | 启动时一次性加载全部组件到内存，消除每页递归文件搜索 |
| 地图预编译 | `component.cpp` | JSON sections 编译为 FlatCmd 指令列表，渲染时直写 buffer |
| 输出管线融合 | `compress.cpp` | `i18n_replace` + `minify_html` + `wrap_webp` + `fingerprint` 统一入口 |
| 资产快速复制 | `builder.cpp` | Windows 下 `robocopy` 替换 `fs::copy`，130+ 文件复制 6s → 0.3s |
| `on_config` 去重 | `builder.cpp` | 多版本构建只执行一次插件钩子，避免重复 spawn |
| 持久插件进程 | `plugin.cpp` | Python 解释器常驻，热调用 ~0.01s（vs 冷启动 ~0.45s） |
| 资产签名快检 | `builder.cpp` | 目录 mtime 快检替代递归 130+ 文件 stat |
| 死链检查串行化 | `builder.cpp` | 避免并发写盘后的文件系统缓存误判 |

### 架构决策：什么没做

- **模板预编译 AST**：Hugo 级别 0.1s 需要 Go 模板编译+全内存对象池，C++ 单线程 md4c + JSON map 渲染的天花板在 ~1.5s。当前增量 0.7s 已足够实用。
- **nav 骨架缓存**：已实现但引入死链误报（skeleton/patch 重组逻辑与原始 nav_groups_json 不等价），暂移除。
- **全局快检跳过**：无变化时 0.01s 退出已编码，边缘 case 需调试。

### 极限性能

| 场景 | 耗时 |
|------|------|
| 首次完整构建（含资产）| ~1.5s |
| 全量渲染（资产缓存）| ~1.2s |
| 增量构建（无改动）| ~0.7s |
| 增量构建（改 1 篇）| ~0.8s |

对标：Hugo ~0.1s（Go 模板编译）、VitePress ~1.5s（500 页）、Docusaurus ~3s（2000 页 cold）。Cdocs 作为单 exe 零依赖 C++ 生成器，性能已达实用级天花板。

## 测试保障

`.Cdocs/tools/test.py` 自动化回归 24 项，覆盖：init 建站、目录自动导航、front matter、admonition、死链检测、WebP 压缩、资源指纹、config 校验、serve 传输层（gzip/ETag/304）。`Cdocs doctor` / `Cdocs check` 提供交互式自检与质量检查。
