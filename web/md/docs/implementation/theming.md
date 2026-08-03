# 主题系统是如何实现的

主题 = `themes/<name>/` 一个文件夹。本文说明它如何被引擎解析、组合与回退。

## 主题定位：两级查找

`src/component.cpp` 的 `theme_root()` 按顺序查找当前主题：

1. `web/.Cdocs/themes/<themeName>/`（引擎内主题）
2. `web/themes/<themeName>/`（站点根主题仓库，多主题并存于此）

`themeName` 由 `config.json` 的 `site.themeName` 指定（缺省 = `ink`）。找不到时回退默认主题。

## 主题四件套

| 部分 | 作用 | 引擎如何用 |
|---|---|---|
| `theme.json` | 元数据（name/version） | 主题名标识 |
| `map/*.json` | 页面地图 | **每个页面类型一个**，注册在 `.Cdocs/config/map.json`（`maps` 数组 + `templates.base`） |
| `components/**` | 组件 | HTML + 内嵌 `<style>` + 内嵌 `<script>`，`{{key}}` 数据孔接收引擎数据 |
| `assets/` | 前端资源 | 整目录拷入 `dist/<loc>/assets/` |

## map.json：注册表 + 继承

```json:.Cdocs/config/map.json
{
  "maps": [
    { "type": "home", "map": "map/home.json", "mode": "home" },
    { "type": "doc",  "map": "map/doc.json",  "mode": "pages" }
  ],
  "templates": { "base": "map/base.json" }
}
```

- `type`：页面类型标识；`map`：地图文件相对主题目录；`mode`：决定**数据来源与输出方式**（home/pages/blog-list/blog-post/tags/tag-page/single）；
- **extends 继承**：`base.json` 定义公共骨架（head/header/footer），子地图用 `{"slot": X}` 槽位替换局部——首页/文档页/404 共享同一套页眉页脚，只换正文区。

## 组件：结构 + 样式 + 交互 自包含

组件文件 = HTML 结构 + `<style>`（样式）+ `<script>`（交互）三位一体：

```html
<!-- components/center/Card.html -->
<a class="card" href="{{href}}"><h3>{{title}}</h3>{{slot}}</a>
<style>
  .card { border: 1px solid var(--border); border-radius: var(--radius); }
</style>
```

引擎处理要点：

- **样式去重**：同一组件在页面出现多次，`<style>` 只注入一次（`shortcode.cpp` 的 style 去重）；
- **数据孔** `{{field}}` / `{{slot}}` / `{{a.b.c}}`：缺失原样保留并警告（`fill_data_holes`）；
- **图标规则例外**：含 `url()` 的图标规则留在 `style.css`（内嵌 style 无法承载相对路径），组件样式一律引用 CSS 变量。

## 主题回退：机制组件共享

**机制组件**（与视觉无关的交互组件：搜索框、灯箱、命令面板等）在当前主题缺失时，会**从默认主题回退**加载——换肤不需要复制全套组件，主题只需覆盖自己改动的部分。

## 换肤 = 改一个字段

```json
"site": { "themeName": "frost" }
```

引擎按 `themeName` 定位主题 → 读其 map/components/assets → 构建时整体切换。主题间不共享状态，换肤是纯目录切换，**零迁移成本**。
