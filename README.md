# Cdocs

用 **C++** 写的**静态文档站生成器**。单文件可执行程序，零 Node、零运行时依赖——把 Markdown 变成可离线、中英双语、SEO 完备的静态站点。

> 本仓库的文档站就是由 Cdocs 自己生成的（dogfooding），源码在 `web/` 下。

## 仓库结构

```
Cdocs/
├── src/            # ★ C++ 生成器源码（25 个模块）
├── web/            # ★ 站点根：md/（内容）+ .Cdocs/（引擎+配置）+ themes/（主题）+ vercel.json
│   └── dist/       #   构建产物（cd web && Cdocs build 生成）
├── bin/            # ★ 分发包：Cdocs.exe + .Cdocs 引擎资源（make_release.py 生成，入 PATH 即用）
├── .github/        # 自动部署（GitHub Pages / Linux 二进制编译）
└── README.md / .gitignore / .gitattributes
```

- **构建文档站**：`cd web && Cdocs build`（或 `Cdocs serve` 预览）
- **生成分发包**：`python web/.Cdocs/tools/make_release.py` → 产出 `bin/`
- **全局安装**：把 `bin/` 加入 PATH，任意目录直接 `Cdocs init/build/serve`

## 特性

- **单文件引擎**：C++17 静态编译，目标机只需一个 `Cdocs.exe`，无任何运行时依赖
- **开箱即用**：`init` 建站、`serve` 预览（带热重载）、多语言、博客、版本化文档、主题与插件
- **全内置发布**：RSS / JSON Feed、PWA 离线、sitemap、OG/Twitter 卡片、JSON-LD、全文搜索
- **构建期优化**：WebP 图片压缩、HTML/CSS 紧凑化、资源指纹、gzip 传输、死链检查、增量构建
- **一键部署**：GitHub Pages（Actions 自动）与 Vercel（Root Directory 设为 `web/`）

## 快速开始

```bash
# 全局安装（发布包）
Cdocs init mysite        # 新建站点（自动构建）
cd mysite
Cdocs serve -o           # 本地预览并打开浏览器

# 或从源码开发
cd web
Cdocs build              # 构建文档站到 web/dist
```

## 文档

完整文档（安装 / 使用 / 配置 / 主题 / 插件 / 部署）：**[https://cdocs.cnkrru.top](https://cdocs.cnkrru.top)**

## 技术栈

C++17（gcc 静态链接）· [md4c](https://github.com/mity/md4c)（Markdown）· [nlohmann/json](https://github.com/nlohmann/json) · [libwebp](https://developers.google.com/speed/webp)（图片压缩）· [zlib](https://zlib.net/)（gzip）——全部静态编译进单文件，源码随仓库。
