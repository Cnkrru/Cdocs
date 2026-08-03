# 版本化

Cdocs 支持**多版本文档站**（对标 Docusaurus）：同一套引擎构建出多个版本的站点，页眉出现**版本下拉**一键切换，切换时保持当前语言。两种驱动方式：**显式配置优先**（推荐）、**约定自动识别**（零配置兜底）。

## 内容架构：md/ 唯一根

自 v2 起内容统一收口到 `md/`，版本 = `md/` 下的**子目录**：

```
md/
├── docs/        当前版本文档（源目录 = md/docs）
├── docs-v1/     历史版本快照（源目录 = md/docs-v1）
└── blog/        博客（全局共享，所有版本同一份）
```

| 目录 | 版本名 | 说明 |
|------|--------|------|
| `md/docs` | `v2`（示例） | 当前版本，页眉标「最新」 |
| `md/docs-v1` | `v1` | 历史版本快照 |

> 版本命名自由：`md/docs`、`md/docs-v1`、`md/docs-2024` 均可，**name 由配置声明**（见下），不要求固定命名。

## 用法（三步）

```bash
# ① 初始化时选择带版本（init 交互询问，或手动建目录）
Cdocs init mysite        # 交互选「文档+博客」→ 询问是否带历史版本 → y

# ② 发布新版时锁定快照
cp -r md/docs md/docs-v1   # v2 → 复制一份为 v1 快照

# ③ 构建：按 config.site.versions 分派，每个版本独立产物
Cdocs build
```

构建输出：

```
=== 构建版本 2.x → "dist\v2"
已生成 17 篇文档到 "dist\v2"
=== 构建版本 1.x → "dist\v1"
已生成 1 篇文档到 "dist\v1"
```

产物结构（每个版本都是完整独立站点，含各自的 i18n / RSS / PWA）：

```
dist/
├── index.html      # 根重定向 → 默认版本（v2/）
├── v2/             # 当前版（zh-CN/ + en/ 双语）
└── v1/             # v1 快照（zh-CN/ + en/ 双语）
```

页眉版本下拉：当前版本显示为「最新」（不可点），其他版本为链接，**切换保持当前语言**（v2/zh-CN 页切到 v1 仍是 v1/zh-CN/）。

## 显式配置（推荐）：site.versions 数组

在 `config.json` 的 `site` 段声明版本列表：

```json
"site": {
  "versions": [
    { "name": "v2", "label": "2.x", "source": "md/docs",   "default": true },
    { "name": "v1", "label": "1.x", "source": "md/docs-v1", "default": false }
  ]
}
```

| 字段 | 说明 |
| --- | --- |
| `name` | 版本标识（URL 目录名，如 `dist/v2/`） |
| `label` | 下拉显示名（默认取 name） |
| `source` | 该版本的源目录（相对项目根，如 `md/docs`） |
| `default` | 标记默认版本（根 index 重定向目标；不配则取第一个） |

同时用 `site.route` 给每个版本/博客区配置独立侧边栏：

```json
"route": {
  "docs":    "route/docs.json",     // 当前版本文档（源目录 md/docs）
  "docs-v1": "route/docs-v1.json",  // 历史版本（源目录 md/docs-v1）
  "blog":    "route/blog.json"      // 博客区（全局共享）
}
```

配置了 `versions` 数组时**以显式声明为准**（自动扫描不再生效），适合控制顺序、自定义 label、隐藏某些版本。

## 约定兜底：快照目录自动识别

未配置 `site.versions` 时，构建器自动扫描 `md` 的同级 `<md>-*` 快照目录（如 `md-v1`、`md-v2`），识别为历史版本：

| 目录 | 版本名 | 说明 |
|------|--------|------|
| `md` | `current` | 最新版本，恒为首位 + 默认 |
| `md-v1` | `v1` | 历史版本（`md-<名字>` 都会被识别） |

```bash
cp -r md md-v1     # 发布 v1 时锁定一份快照
Cdocs build        # 自动识别 current + v1
```

```
[versions] 自动识别 1 个历史版本: current v1
构建版本 最新 → "dist\current"
构建版本 v1 → "dist\v1"
```

## 适用场景

- **发新版保留旧文档**：API 变更、迁移指南的读者仍能访问旧版本文档
- **多语言站点同样支持**：每个版本各自带 `zh-CN` / `en` 完整双语
- **版本间差异对比**：各版本独立构建，`../<name>/` 链接互跳

> 注意：快照目录是内容副本，纳入版本管理或随站点发布均可；删除 `docs-v1`（或配置中移除该项）后重新构建即回到单版本，**零残留**。
