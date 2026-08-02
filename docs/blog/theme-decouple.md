---
title: "主题解耦：把 HTML 骨架从 C++ 里拆出来"
date: 2026-07-06
tags: [主题, 重构]
---

一行模板文件 + 占位符替换，换主题从此只换文件夹。

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
