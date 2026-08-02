---
title: "插件系统：用外部进程打破 C++ 的封闭"
date: 2026-07-10
tags: [插件, 架构]
---

零 DLL、语言无关的插件钩子设计，让 Python 脚本也能参与 C++ 构建管线。

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
