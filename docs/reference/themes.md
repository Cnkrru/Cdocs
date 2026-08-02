# 主题开发

Cdocs 的**主题 = 一个文件夹**。引擎只负责生成数据（导航树、目录、正文、分页…），页面骨架与全部前端资源都由主题提供。复制一个主题文件夹、改几行配置，即可整体换肤——这就是 Cdocs 的主题规范，也是将来主题生态的接入点。

## 目录结构

主题位于引擎目录下：`.Cdocs/theme/`，出厂自带「水墨」主题（`ink`）：

```
.Cdocs/theme/
├── theme.json                 # 主题元数据（必填）
├── templates/
│   └── layout.html            # 页面骨架（占位符注入，必填）
└── assets/                    # 前端资源（整目录拷入 dist/assets/）
    ├── css/                   #   style.css / custom.css / pswp-theme.css
    ├── js/                    #   app.js 入口 + core/ + features/（ESM 模块）
    ├── pwa/                   #   sw.js + icon.svg（PWA 离线）
    └── icons/                 #   内联 SVG 图标
```

> **兼容旧结构**：早期版本把 `assets/` 与 `templates/` 直接放在 `.Cdocs/` 根下。构建器会优先读取 `.Cdocs/theme/`；不存在时回退旧位置，旧站点无需改动即可构建。

## theme.json —— 主题元数据

```json
{
  "name": "ink",
  "version": "1.0.0",
  "description": "Cdocs 默认主题（水墨风：宣纸米白 + 朱砂红 + 夜墨）",
  "author": "Cdocs",
  "license": "MIT",
  "assets": "assets",
  "templates": "templates"
}
```

| 字段 | 必填 | 说明 |
| --- | --- | --- |
| `name` | ✔ | 主题名（小写字母/数字/连字符） |
| `version` |  | 主题版本，建议 `x.y.z` 语义化版本 |
| `description` |  | 一句话介绍，展示给使用者 |
| `author` / `license` |  | 作者与许可协议（建议 MIT，便于生态传播） |
| `assets` / `templates` |  | 子目录名，默认即 `assets` / `templates`，一般无需改 |

## templates/layout.html —— 页面骨架

构建器把页面拆成若干**数据子块**，用 `{{key}}` 占位符注入模板。模板里其余内容原样保留。

### 占位符全表

| 占位符 | 类型 | 说明 |
| --- | --- | --- |
| `{{lang}}` | 单值 | 当前语言（`zh-CN` / `en`） |
| `{{theme}}` | 单值 | 主题（亮/暗，由 config 或用户偏好决定） |
| `{{title}}` | 单值 | 页面标题 |
| `{{base}}` | 单值 | 相对站点根的前缀（子目录站点用，一般 `./`） |
| `{{body_class}}` | 单值 | `<body>` 附加类（如首页 `no-sidebar`） |
| `{{meta_desc}}` | 行中块 | `<meta>` 描述 |
| `{{highlight_css}}` | 行中块 | 代码高亮 CSS（未启用时为空） |
| `{{custom_head}}` | 行中块 | 自定义 `<head>` 注入 |
| `{{skip_link}}` | 行尾块 | 无障碍「跳到主要内容」链接 |
| `{{header}}` | 行尾块 | 页眉（logo / 搜索 / 语言切换 / GitHub / 主题切换） |
| `{{left_nav}}` | 行中块 | 左侧边栏导航树 |
| `{{breadcrumb}}` | 行中块 | 面包屑 |
| `{{body}}` | 行中块 | **正文**（模板必须包含，否则回退内置骨架） |
| `{{last_updated}}` | 行中块 | 最后更新时间 |
| `{{edit_link}}` | 行中块 | 编辑本页链接 |
| `{{pager}}` | 行中块 | 上一篇 / 下一篇 |
| `{{toc_sidebar}}` | 行中块 | 右侧目录（未启用时为空） |
| `{{footer}}` | 行尾块 | 页脚 |
| `{{back_to_top}}` | 行尾块 | 返回顶部按钮 |
| `{{highlight_js}}` | 行尾块 | 代码高亮 JS |
| `{{search_js}}` | 行尾块 | 搜索库 JS |
| `{{i18n_json}}` | 行尾块 | 客户端 i18n 字典（`window.__I18N__`） |
| `{{feedback_js}}` | 行尾块 | 反馈按钮数据 |

### 占位符排版规则

模板里 **三个约定**，遵守才能得到干净的 HTML：

1. **单值**占位符可放任意位置；**行中块**自带结尾换行，放在行首即可；
2. **行尾块**（`skip_link` / `header` / `footer` / `back_to_top` / 各 JS 块）**必须独立成行**且自己以占位符收尾——渲染时会被去尾换行，模板行尾负责补 `\n`；
3. **内容区**（`breadcrumb` / `body` / `last_updated` / `edit_link` / `pager`）建议合并在一行，避免空块产生空行。

> 未知 `{{key}}`（如 i18n 键 `{{navIntro}}`）会**原样保留**，由后续 i18n 替换处理，模板无需关心。

## assets/ —— 前端资源

`assets/` 整个目录在构建时原样拷入每个语言目录的 `dist/<loc>/assets/`。因此：

- `css/style.css` → `dist/<loc>/assets/css/style.css`（页面用 `{{base}}assets/css/style.css` 引用）
- 支持**增量跳过**：构建器记录 assets 的 mtime 签名，未变化时跳过复制（大站点提速明显）；
- 运行时 JS 库（Mermaid / KaTeX / PhotoSwipe / FlexSearch 等）放在引擎的 `.Cdocs/deps/`，构建时拷入 `assets/deps/`——它们**不属于主题**，主题只管自己的 css/js/icons/pwa。

### JS 入口约定

`assets/js/app.js` 只做引导，动态 `import('./main.js')` 加载 ESM 模块图。主题可以自由组织 `main.js` 及其依赖，但**入口文件名 `app.js` 与引用路径必须保持**（页面模板硬编码引用 `{{base}}assets/js/app.js`）。

## 如何做一个新主题

1. 复制 `.Cdocs/theme/` 为 `.Cdocs/theme-my/`（或独立站点目录）；
2. 改 `theme.json`（name/version/description…）；
3. 改 `templates/layout.html` 调整页面结构；
4. 改 `assets/css/style.css` 与 `assets/js/` 调整视觉与交互；
5. `Cdocs build` 预览效果；`Cdocs serve -o --watch` 可热重载。

> 提示：主题的 CSS 变量（`--accent` / `--bg` / `--fg` 等）定义在 `style.css` 的 `:root` 中，改配色只需覆盖变量，不必逐条改样式。
