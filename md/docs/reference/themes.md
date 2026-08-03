# 主题开发

Cdocs 的**主题 = 一个文件夹**。引擎负责生成数据（导航树、目录、正文、分页…），页面结构由 **JSON 地图（map）** 描述、页面元素由 **组件（components）** 提供、视觉由**主题变量 + 组件内嵌样式**承载。复制一个主题文件夹、改几行配置，即可整体换肤——这就是 Cdocs 的主题规范。

## 多主题仓库

主题收口在 **`themes/` 多主题仓库**（引擎根目录下），出厂自带 3 套主题：

```
themes/
├── ink/       # 水墨风（默认）：宣纸米白 + 朱砂红 + 夜墨
├── paper/     # 纸质风：窄栏、默认浅色、顶部导航、首页卡片流
└── frost/     # 玻璃拟态：毛玻璃卡片、渐变背景、sticky 头
```

当前生效主题由 `config.json` 的 `site.themeName` 指定（缺省 = `ink`）：

```json
"site": {
  "themeName": "frost",      // themes/ 下的子目录名；省略 = 默认主题 ink
  "theme": "dark"            // 默认明暗（light/dark）
}
```

## 目录结构

每个主题都是自包含的文件夹 `themes/<name>/`：

```
themes/ink/
├── theme.json                 # 主题元数据（name/version/description…）
├── map/                       # 页面地图：每个页面类型一个 JSON（核心！）
│   ├── home.json              #   首页
│   ├── doc.json               #   文档页
│   ├── blog.json              #   博客列表页
│   ├── blog-post.json         #   博客详情页
│   ├── tags.json / tag-page.json / 404.json …
│   └── base.json              #   公共骨架（被各页面 extends）
├── components/                # 组件：页面元素与正文短代码
│   ├── header/                #   页眉组件族（topbar/logo/nav/search…）
│   ├── footer/                #   页脚组件族
│   ├── center/                #   正文区组件族（sidebar/toc/pager/cards…）
│   └── shortcodes/            #   正文短代码（<Tabs/> <Expand/> <Badge/>…）
└── assets/                    # 前端资源（整目录拷入 dist/assets/）
    ├── css/                   #   style.css（主题变量 + 共用样式）
    ├── js/                    #   app.js 入口 + core/ + features/（ESM 模块）
    ├── pwa/                   #   sw.js + icon.svg（PWA 离线）
    └── icons/                 #   内联 SVG 图标
```

> 机制组件（机制主题组件，如搜索/灯箱等与具体视觉无关的交互组件）在主题自身缺失时，会从**默认主题**回退共享——换肤无需复制全套组件。

## theme.json —— 主题元数据

```json
{
  "name": "ink",
  "version": "1.0.0",
  "description": "Cdocs 默认主题（水墨风：宣纸米白 + 朱砂红 + 夜墨）",
  "author": "Cdocs",
  "license": "MIT"
}
```

| 字段 | 必填 | 说明 |
| --- | --- | --- |
| `name` | ✔ | 主题名（小写字母/数字/连字符） |
| `version` |  | 主题版本，建议 `x.y.z` 语义化版本 |
| `description` |  | 一句话介绍 |
| `author` / `license` |  | 作者与许可协议（建议 MIT，便于生态传播） |

## map/ —— 页面地图（JSON 驱动，主题核心）

**每个页面类型一个 JSON 文件**，页面 = 按序执行的一组「章节」。地图由 `.Cdocs/config/map.json` 注册（`maps` 数组：`{type, map, mode}`），也支持 `extends` 继承（`base.json` 定义公共骨架，子地图用 `{"slot": X}` 槽位展开替换）。

```json:.Cdocs/config/map.json
{
  "maps": [
    { "type": "home", "map": "map/home.json", "mode": "home" },
    { "type": "doc",  "map": "map/doc.json",  "mode": "pages" },
    { "type": "blog", "map": "map/blog.json", "mode": "blog-list" }
    // … blog-post / tags / tag-page / 404 …
  ],
  "templates": { "base": "map/base.json" }
}
```

章节有五种约定：

| 约定 | 作用 |
| --- | --- |
| `{ "html": "…" }` | 静态 HTML 片段原样输出（可含数据孔 `{{lang}}` 等） |
| `{ "component": "Name" }` | 渲染组件 `components/**/<Name>.html` |
| `{ "component": "Name", "if": "path" }` | 数据路径真值才渲染 |
| `{ "component": "Name", "each": "path" }` | 数组循环渲染（每项合并进数据作用域） |
| `{ "component": "Name", "sections": […], "props": {…} }` | 子章节渲染结果填进组件 `{{slot}}`；props 传参（优先级最高） |

数据作用域优先级：**全局页面数据 < each 当前项 < props**。

> 设计要点：地图 JSON 里没有「语法」——条件、循环、嵌套全部是 JSON 字段，组件本身是纯 HTML + 数据孔，无控制流。页面结构 = 数据 + 组件组合，C++ 不硬编码任何页面骨架。

## components/ —— 组件（结构 + 样式 + 交互 三位一体）

组件 = 一个 HTML 文件，可含 `<style>`（样式）与 `<script>`（交互），全部内嵌自包含：

```html
<!-- components/center/Card.html -->
<a class="card" href="{{href}}"><h3>{{title}}</h3>{{slot}}</a>
<style>
  .card { border: 1px solid var(--border); border-radius: var(--radius); … }
  .card:hover { border-color: var(--accent); … }
</style>
```

- **数据孔** `{{field}}` / `{{slot}}` / `{{a.b.c}}` 由引擎填充（缺失原样保留并警告）；
- **样式组件化**：样式内嵌在组件文件里，复制组件文件即带走全套样式（含双主题变量适配）；
- **组件图标**：含 `url()` 的图标规则（相对主题资产目录解析）留在 `style.css` 统一管理，内嵌 style 无法承载；
- **正文短代码**：`components/shortcodes/` 下的组件可被正文 `<组件/>` 标签直接调用（见 [shortcode 参考](./shortcodes)）。

## assets/ —— 前端资源与主题变量

`assets/` 整目录拷入 `dist/<loc>/assets/`。主题的视觉契约由 `css/style.css` 的 CSS 变量定义：

- `:root` / `[data-theme="light"]` 定义亮色变量（`--bg` 宣纸米白 / `--accent` 朱砂 / `--info` 黛蓝…）；
- `[data-theme="dark"]` 覆盖暗色（夜墨）；
- 组件与页面样式一律引用变量，**换配色只需改变量**；
- `config.site.themeVars` 可覆盖公开变量；`config.site.customCss` 追加自定义样式（双主题共享覆盖层）。

### JS 入口约定

`assets/js/app.js` 只做引导，动态 `import('./main.js')` 加载 ESM 模块图。交互增强（主题 / 代码 / 提示框 / 图表 / 搜索 / 命令面板 / 灯箱 / PWA）都是 `features/*.js` 的 `initX()` 模块——**无需修改 C++ 生成器即可加前端功能**。

> 注意：Mermaid / KaTeX 是客户端懒加载升级；Admonitions（`> [!type]`）与 shortcode 组件是**构建期渲染**（静态 HTML 里直接可见）。

## 如何做一个新主题

1. 复制 `themes/ink/` 为 `themes/my-theme/`；
2. 改 `theme.json`（name/version/description）；
3. 在 `config.json` 的 `site.themeName` 指向新主题名；
4. 改 `map/*.json` 调整页面结构（组件组合）；
5. 改 `components/` 与 `assets/css/style.css` 调整视觉；
6. `Cdocs build` 预览；`Cdocs serve -o --watch` 热重载。

> 提示：想加页面类型（如「案例展示」），在 `config/map.json` 的 `maps` 数组加一项 `{type, map}`，再写 `theme/map/<type>.json` 即可——不改 C++。

## 内置主题清单

| 主题 | 风格 | 特性 |
| --- | --- | --- |
| `ink` | 国风水墨（默认） | 宣纸米白 + 朱砂红，侧边栏布局，含语言下拉 |
| `paper` | 纸质文档 | 窄栏阅读、默认浅色、顶部导航、首页卡片流 |
| `frost` | 玻璃拟态 | 毛玻璃卡片、渐变背景、sticky 头、按钮推右 |
