# Mermaid 图表测试

本页验证 Mermaid 图表渲染（客户端懒加载 `mermaid.min.js`）。图表写在与代码块相同的围栏里，语言标记为 `mermaid`。

## 流程图（flowchart）

```mermaid
flowchart TD
  A[开始] --> B{有 .md 吗?}
  B -- 是 --> C[解析 front matter]
  B -- 否 --> D[跳过]
  C --> E[渲染 HTML]
  E --> F[写 dist/]
```

```mermaid
flowchart LR
  subgraph 输入
    MD[Markdown]
    CFG[config.json]
  end
  subgraph 处理
    GEN[生成器]
  end
  subgraph 输出
    DIST[dist/]
  end
  MD --> GEN --> DIST
  CFG --> GEN
```

## 时序图（sequence）

```mermaid
sequenceDiagram
  participant 用户
  participant CLI
  participant 生成器
  用户->>CLI: Cdocs build
  CLI->>生成器: run_build()
  生成器->>生成器: 收集页面 + 渲染
  生成器-->>CLI: 退出码 0
  CLI-->>用户: 已生成 5 篇文档
```

## 类图（class）

```mermaid
classDiagram
  class BuildContext {
    +fs::path in_dir
    +SiteConfig cfg
    +vector~Page~ pages
    +load_site_config() void
    +prepare_pages() int
  }
  class Page {
    +string file
    +string title
    +bool draft
  }
  BuildContext --> Page : 包含
```

## 状态图（state）

```mermaid
stateDiagram-v2
  [*] --> 草稿
  草稿 --> 已发布 : build -D
  草稿 --> 已排除 : build
  已发布 --> 已废弃
  已废弃 --> [*]
```

## 甘特图（gantt）

```mermaid
gantt
  title Cdocs 开发里程碑
  dateFormat YYYY-MM-DD
  section 架构
  模块化拆分     :done, a1, 2026-08-01, 1d
  run_build 编排 :done, a2, 2026-08-02, 1d
  section 功能
  测试站点       :active, b1, 2026-08-02, 2d
```

## 饼图（pie）

```mermaid
pie title dist/ 文件构成（示意）
  "HTML" : 15
  "JS" : 47
  "SVG" : 63
  "字体" : 80
```

## 旅程图（journey）

```mermaid
journey
  title 一次构建的用户体验
  section 写文档
    编辑 Markdown: 5: 用户
  section 构建
    运行 Cdocs: 4: 用户
    生成 dist: 5: 生成器
  section 预览
    打开浏览器: 5: 用户
```
