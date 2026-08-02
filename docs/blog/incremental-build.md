---
title: "增量构建：把 watch 重建从 24 秒压到 3 秒"
date: 2026-07-18
tags: [性能, 构建]
---

全局签名 + 页面指纹的双层判定，让 serve -w 的热重载快到感觉不到。

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
