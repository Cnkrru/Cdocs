# Shortcode 参考

Shortcode（短代码）是 Cdocs 的**正文组件系统**：在 Markdown 正文里用 `<组件/>` 标签语法直接嵌入组件，与「地图里挂组件」同一套心智（首字母大写 = 组件）。

```markdown
注意：升级前先备份。

<Expand title="备份">升级前先复制 .Cdocs 文件夹</Expand>

<Tabs>
  <Tab label="Windows">用 build.cmd</Tab>
  <Tab label="macOS">用 build.sh</Tab>
</Tabs>
```

## 语法规则

| 规则 | 说明 |
|------|------|
| **首字母大写 = shortcode** | `<Callout>` → 组件；`<div>`/`<p>` 小写 → 原生 HTML，md4c 原样处理，零冲突 |
| **参数 → props** | `type="tip"` 自动进组件数据作用域（组件里 `{{type}}` 可取） |
| **子内容 → slot** | `<Tabs>…</Tabs>` 内容进 `{{slot}}`，slot 内 Markdown 正常渲染，支持嵌套 |
| **自闭合** | `<Badge type="new"/>` 无子内容时用自闭合写法 |
| **配对闭合** | 双标签必须闭合，不闭合 → 构建警告 |
| **转义** | `\<Tabs>`（反斜杠前缀）→ 显示字面量 `<Tabs>`（教学场景） |
| **代码块安全** | 代码围栏 / 行内代码里的标签被转义为 `&lt;`，不会误触发 |
| **未定义组件** | 警告（`components/Callout.html 不存在`），正文原样保留 |

## 内置组件

内置组件位于 `theme/components/shortcodes/`，**样式与交互全部内嵌组件文件**（结构 + `<style>` + `<script>` 三位一体），复制文件即带走全套。

### Tabs / Tab —— 标签页切换

```markdown
<Tabs>
  <Tab label="Windows">用 build.cmd</Tab>
  <Tab label="macOS">用 build.sh</Tab>
</Tabs>
```

- `Tab` 的 `label` 是按钮名，子内容是面板（Markdown 生效）；
- 切换脚本内嵌在 Tabs 组件文件内（客户端从面板 `data-label` 生成按钮），构建期零 JS 逻辑。

### Expand —— 折叠面板

```markdown
<Expand title="点击展开答案">折叠内容</Expand>
```

- 渲染为原生 `<details>/<summary>`，**零 JS**；
- `title` 是折叠标题，子内容是展开区。

### CodeGroup / CodeBlock —— 代码块 tab 化

```markdown
<CodeGroup>
  <CodeBlock lang="bash">cd docs && build.sh</CodeBlock>
  <CodeBlock lang="js">npm run build</CodeBlock>
</CodeGroup>
```

- 每个 `CodeBlock` 的语言名显示为切换按钮，代码区只显示当前块；
- **`{{slot_raw}}`**：CodeBlock 的子内容原样转义进 `<code>`（代码里的 `<`/`>` 不会被浏览器当标签）；
- `CodeBlock` 单独使用（不在组内）时显示语言标签页样式。

### Badge —— 行内徽章

```markdown
<Badge type="new">v1.0</Badge> <Badge type="warning">实验性</Badge>
```

- `type`：`new`/`tip`/`success`（竹青）、`info`（黛蓝）、`warning`（赭石）、`danger`/`important`（朱砂），缺省为中性；
- 行内元素，可嵌在句子中间。

### 提示框（Callout）

`<Callout>` **未内置**——`> [!tip]` Admonitions（11 种类型）已覆盖该场景，避免重复建设：

```markdown
> [!tip] 备份
> 升级前先复制 .Cdocs 文件夹
```

## 转义与边界

```markdown
<!-- 显示字面量 <Tabs>（教学场景） -->
使用 \<Tabs> 之前请阅读文档。

<!-- 代码块里的标签不会被解析 -->
```markdown
<Tabs><Tab label="x">y</Tab></Tabs>
```

<!-- 行内代码同理 -->
用 `<Badge/>` 的写法…
```

## 自定义 shortcode = 写一个组件文件

主题作者在 `theme/components/shortcodes/` 加一个 `<Name>.html` 即注册一个新 shortcode，**零引擎改动**：

```html
<!-- components/shortcodes/Figure.html -->
<figure class="figure">
  <img src="{{src}}" alt="{{alt}}">
  <figcaption>{{slot}}</figcaption>
</figure>
<style>
  .figure { margin: 20px 0; text-align: center; }
  .figure figcaption { color: var(--muted); font-size: 13px; margin-top: 6px; }
</style>
```

正文即可用：

```markdown
<Figure src="diagram.png" alt="架构图">整体数据流</Figure>
```

规范要点：

1. **文件名 = 标签名**（首字母大写）；
2. 参数 `src="…"` → 组件内 `{{src}}`；子内容 → `{{slot}}`（Markdown 渲染后）；
3. **样式内嵌 `<style>`**（用主题变量，双主题自动适配）；含 `url()` 的图标规则放 `style.css`；
4. 交互脚本内嵌 `<script>`（如需）；
5. 同文档同组件多次使用，`<style>` 只输出一份（引擎自动去重），`<script>` 每处保留。

## 渲染管线

正文渲染链：**shortcode 预扫描（md4c 前，替换为占位 token）→ md4c → Admonitions → shortcode 展开（token → 组件渲染，子内容递归渲染为 slot）→ style 去重**。详见 [正文渲染](../generator/render)。
