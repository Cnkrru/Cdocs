---
title: "插件与主题市场"
description: "从市场页一键下载插件 / 主题文件夹，正文也可用 PluginMarket / ThemeMarket 短代码嵌入市场列表"
date: "2026-08-04"
tags: [插件, 主题, 市场]
---

# 插件与主题市场

市场页从 `.Cdocs/data/` 的 JSON 数据文件读取「仓库 + 文件夹地址」，点击**下载文件夹**即可通过 DownGit 直接获取仓库中的对应子目录。

## 市场页面

- **插件市场**：[market/plugin-market.html](../plugin-market/index.html) —— 6 个官方插件（评论 / 部署 / 查询）
- **主题市场**：[market/theme-market.html](../theme-market/index.html) —— 3 个官方主题（ink / paper / frost）

## 正文嵌入短代码

在任意文档正文中插入 `<PluginMarket/>` 或 `<ThemeMarket/>`，即可内嵌市场列表（数据来自站点数据，随 `.Cdocs/data/*.json` 维护）：

<PluginMarket/>

<ThemeMarket/>

## 数据格式

市场条目定义在 `.Cdocs/data/plugin-market.json` / `theme-market.json`，每个条目含仓库地址与文件夹路径：

```json
{
  "plugin_market": [
    {
      "name": "giscus 评论",
      "desc": "GitHub Discussions 驱动的评论系统",
      "repo": "Cnkrru/Cdocs",
      "folder": "web/.Cdocs/plugins/giscus"
    }
  ]
}
```

- `repo`：GitHub 仓库（`owner/name`）
- `folder`：仓库内目标文件夹路径（下载按钮据此生成 DownGit 链接）
- 增删条目即可更新市场，无需改动引擎
