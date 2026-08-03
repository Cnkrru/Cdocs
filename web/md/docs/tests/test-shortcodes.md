# Shortcodes 内容组件测试

本页覆盖正文短代码组件：`<Tabs>` / `<Tab>` / `<Expand>` / `<CodeGroup>` / `<CodeBlock>` / `<Badge>`，以及转义与嵌套。看到的效果即最终产物效果。

## Tabs 标签页

<Tabs>
  <Tab label="Windows">在 Windows 上使用 **build.cmd** 编译，支持 `--clean` 参数。</Tab>
  <Tab label="macOS">在 macOS 上运行 `./build.sh`，用法相同。</Tab>
</Tabs>

## Expand 折叠

<Expand title="点击展开答案">这里是**折叠内容**，内部支持 Markdown 与行内代码 `cdocs build`。</Expand>

## CodeGroup 代码组

<CodeGroup>
  <CodeBlock lang="bash">cd dist && python -m http.server 8080</CodeBlock>
  <CodeBlock lang="js">console.log("hello");  // 注释里的 <tag> 原样保留</CodeBlock>
</CodeGroup>

## Badge 徽章

这是一个 <Badge type="new">新功能</Badge> 徽章，另一个 <Badge type="default">默认样式</Badge>。

## 转义：\<Tabs> 显示字面量

上面这行的 `<Tabs>` 是字面量文本（`\<` 转义），不会被解析成组件。

## 代码围栏内不解析

```
<Callout type="tip">代码块里的标签原样输出</Callout>
<Tabs><Tab label="x">不生效</Tab></Tabs>
```

## 嵌套 shortcode

<Tabs>
  <Tab label="A">A 面板，内含折叠：<Expand title="嵌套折叠">嵌套内容正常渲染。</Expand></Tab>
  <Tab label="B">B 面板，无嵌套。</Tab>
</Tabs>
