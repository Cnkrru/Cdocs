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

## 测试保障

`.Cdocs/tools/test.py` 自动化回归 24 项，覆盖：init 建站、目录自动导航、front matter、admonition、死链检测、WebP 压缩、资源指纹、config 校验、serve 传输层（gzip/ETag/304）。`Cdocs doctor` / `Cdocs check` 提供交互式自检与质量检查。
