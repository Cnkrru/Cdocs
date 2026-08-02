---
title: "多语言方案选型：占位符 字典 vs 目录副本"
date: 2026-07-22
tags: [i18n, 设计]
---

对比了业界常见的 i18n 做法，最终选择了扁平 JSON 字典 + 独立目录输出的组合。

这是一篇用于验证 Cdocs 博客流功能的示例文章。正文支持完整的 Markdown 语法：

## 小节示例

- 列表项一
- 列表项二

> 引用块：Cdocs 的博客流由 `blog/` 目录约定启用，自动按日期倒序生成列表页与分页。

## 代码示例

```cpp
int main() {
    std::cout << "Hello Cdocs" << std::endl;
    return 0;
}
```
