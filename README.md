# Cdocs

用 **C++** 写的**静态文档站生成器**。单文件可执行程序，零 Node、零运行时依赖——把 Markdown 变成可离线、中英双语、SEO 完备的静态站点。

> 本仓库的文档站就是由 Cdocs 自己生成的（dogfooding）。

## 特性

- **单文件引擎**：C++17 静态编译，目标机只需一个 `Cdocs.exe`，无任何运行时依赖
- **开箱即用**：`init` 建站、`serve` 预览（带热重载）、多语言、博客、版本化文档、主题与插件
- **全内置发布**：RSS / JSON Feed、PWA 离线、sitemap、OG/Twitter 卡片、JSON-LD、全文搜索
- **构建期优化**：WebP 图片压缩、HTML/CSS 紧凑化、资源指纹、gzip 传输、死链检查、增量构建
- **一键部署**：GitHub Pages（Actions 自动）与 Vercel

## 快速开始

```bash
Cdocs init mysite        # 新建站点（自动构建）
cd mysite
Cdocs serve -o           # 本地预览并打开浏览器
```

Windows 也可以直接双击项目里的 `serve.bat` 预览。

## 文档

完整文档（安装 / 使用 / 配置 / 主题 / 插件 / 部署）：**[https://cdocs.cnkrru.top](https://cdocs.cnkrru.top)**

## 技术栈

C++17（gcc 静态链接）· [md4c](https://github.com/mity/md4c)（Markdown）· [nlohmann/json](https://github.com/nlohmann/json) · [libwebp](https://developers.google.com/speed/webp)（图片压缩）· [zlib](https://zlib.net/)（gzip）——全部静态编译进单文件，源码随仓库。
