# 插件机制是如何实现的

插件让 Cdocs 在不修改引擎的前提下扩展构建流程。本文说明它的协议、钩子时序与失败隔离。

## 协议：外部进程 + JSON 文件交换

插件 = `.Cdocs/plugins/<name>/plugin.json`（声明）+ 任意语言脚本（实现）。引擎在关键节点调用脚本：

```text
<cmd> <ctx.json 绝对路径> <out.json 绝对路径>
```

- **ctx.json**：引擎产的数据快照（绝对路径字段，如 source/dest/engine/pages[]）；
- **out.json**：脚本写回的结果（`{ok, message, 业务数据...}`）；
- **工作目录 = 插件目录**：脚本内的相对路径基于插件自身。

## 钩子时序（src/builder.cpp）

| 钩子 | 触发时机 | ctx 关键字段 | 典型用途 |
|---|---|---|---|
| `on_config` | 配置加载后 | title/plugins/source/dest | 返回 `inject: {语言: HTML}` 注入正文末尾（评论/统计） |
| `on_page_collected` | 页面收集后 | count/pages[] | 内容校验、批处理元数据 |
| `on_data_query` | 全部页面收集后、渲染前 | count/pages[]（完整元数据快照） | **数据查询**：排序/分页/聚合，写回业务数据 |
| `on_page_rendered` | 每页写盘后 | file/locale/path | 逐页后处理 |
| `on_done` | 全部产物生成后 | source/dest/engine | 部署/通知/压缩收尾 |
| `setup` | `Cdocs deploy --setup` | source（项目根） | 生成平台部署配置（非构建期） |

**关键设计**：`on_data_query` 是查询钩子——引擎把全部页面元数据快照交给插件，插件做聚合/排序/分页后写回 `out.json`，引擎用插件结果渲染页面。**查询逻辑 100% 由插件实现，引擎不内置任何查询**（博客流排序、标签聚合、首页文章流均由此驱动）。

## 数据交换示例（blog-query 插件）

```python
import json, sys

ctx = json.load(open(sys.argv[1], encoding="utf-8"))
posts = [p for p in ctx.get("pages", [])
         if p.get("file", "").startswith("blog/") and not p.get("draft")]
posts.sort(key=lambda p: (p.get("dateT_iso") or "", p.get("file") or ""), reverse=True)
order = [p["file"] for p in posts]

out = {"ok": True,
       "blog_order": order,                    # 文章渲染顺序（上下篇）
       "blog_pages": [order[i:i+10] for i in range(0, len(order), 10)],  # 分页
       "home_posts": order[:8]}                # 首页文章流
json.dump(out, open(sys.argv[2], "w", encoding="utf-8"), ensure_ascii=False)
```

引擎把 out.json 的 `blog_order` / `blog_pages` / `home_posts` / `tags` / `tag_pages` 合并进查询结果，驱动博客列表、分页、首页流、标签页渲染。**改脚本 = 自定义查询**（每页条数、排序规则、首页条数都能改）。

## 注入机制（on_config）

`on_config` 钩子可返回 `inject` 对象：`{ 语言: HTML 片段 }`。引擎在渲染时把对应语言的片段追加到**每个页面正文末尾**——这是评论系统（giscus）、统计（vercel-analytics）的通用注入通道，引擎不感知具体插件。

## 失败隔离语义

插件**永远不阻断构建**：

- 启动失败（脚本不存在）→ 警告，exit -1；
- 脚本 exit 非 0 → 警告；
- 超时被杀死 → 警告（exit 124，默认 30s）；
- `out.json` 里 `ok: false` → 普通提示。

以上任一情况，`Cdocs build` 都继续执行并正常退出 0——**构建链对插件故障是弹性的**。

## 插件与内置功能的边界

| 能力 | 归属 | 原因 |
|---|---|---|
| 版本化/RSS/PWA/SEO/搜索索引/压缩 | 引擎内置 | 通用、性能敏感，C++ 直产 |
| 评论/统计/告警/自定义统计 | 插件 | 站点特有，协议注入，引擎零感知 |
| 部署配置生成（`--setup`） | 插件 | 平台细节（GitHub/Vercel）引擎不关心 |
| 数据查询（博客/标签） | 插件 | 规则常变，脚本可改免重编译 |
