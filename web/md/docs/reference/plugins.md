# 插件开发

插件让 Cdocs 在**不修改引擎**的前提下扩展构建流程。协议采用**外部进程 + JSON 文件交换**：构建器在关键节点广播钩子，调用你的脚本（任意语言），脚本读写 JSON 即可参与构建。零 DLL、零依赖、失败自动隔离。

## 目录结构

```
.Cdocs/plugins/<插件名>/
├── plugin.json        # 插件声明（必填）
└── (脚本)             # 任意语言实现的脚本/可执行文件
```

## plugin.json —— 插件声明

```json
{
  "name": "build-report",
  "hooks": {
    "on_done": { "cmd": "python report.py", "timeout": 30 }
  }
}
```

| 字段 | 说明 |
| --- | --- |
| `name` | 插件名（目录名需一致） |
| `hooks` | 钩子表：`{ 钩子名: { "cmd": 命令, "timeout": 秒 } }` |
| `cmd` | 调用命令。构建器会**切到插件目录**再执行：`<cmd> <ctx.json 绝对路径> <out.json 绝对路径>` |
| `timeout` | 超时秒数（默认 30）。超时会被杀死并记为失败（退出码 124），不阻塞构建 |

## 钩子与上下文

构建器在 4 个时机广播钩子，每个钩子收到一个上下文 JSON（`ctx.json`）：

| 钩子 | 触发时机 | ctx 关键字段 |
| --- | --- | --- |
| `on_config` | 配置加载后 | `engine` / `source` / `dest` |
| `on_page_collected` | 页面收集完成后 | `count`、`pages[]`（file/title/draft/tags） |
| `on_data_query` | **全部页面收集后、渲染前** | `count`、`pages[]`（file/title/date/dateT_iso/tags/weight/draft） |
| `on_page_rendered` | **每页**写盘后 | `file` / `locale` / `path`（绝对路径） |
| `on_done` | 全部产物生成后 | `engine` / `source` / `dest` |
| `setup` | `Cdocs deploy --setup` | `source`（**项目根**，非源目录）/ `engine` |

`on_data_query` 是**数据查询钩子**——引擎把全部页面（文档 + 博客）的元数据快照交给插件，插件做聚合/排序/分页后把结果写回 out.json，引擎用插件结果渲染页面。**查询逻辑 100% 由插件实现，引擎不内置任何查询**（博客流排序、标签聚合、首页文章流均由此驱动）。输出约定：

```json
{
  "ok": true,
  "blog_order": ["blog/hello", "blog/world"],
  "blog_pages": [["blog/hello"], ["blog/world"]],
  "home_posts": ["blog/hello"],
  "tags": [{"name": "C++", "href": "c.html"}],
  "tag_pages": {"C++": ["blog/hello", "docs/intro"]}
}
```

- `blog_order`：博客文章渲染顺序（有序 file 列表，决定文章页与上下篇）
- `blog_pages`：列表页分页（每页一个 file 数组；引擎据此生成 blog/index.html + blog/page/N.html）
- `home_posts`：首页文章流（取哪些、取几条由插件定）
- `tags` / `tag_pages`：标签总览 + 每标签文章列表（file 以 `blog/` 前缀区分博客）
- 插件**无输出或输出为空** → 对应页面不生成（纯文档站行为）

引擎内置两个查询插件：`blog-query`（博客流排序/分页/首页流）、`tags-query`（标签聚合），`Cdocs init` / `Cdocs add blog` 时自动生成到 `.Cdocs/plugins/`。改脚本 = 自定义查询（每页条数、排序规则、首页条数、标签排序都能改）。

`setup` 钩子是**非构建期**钩子，仅由 `Cdocs deploy --setup` 触发——用于生成平台部署配置（`.github/workflows/*.yml`、`vercel.json` 等），插件以 `ctx.source`（项目根）为基准写文件，幂等（内容一致则跳过）。引擎内置两个部署插件：`github-pages`、`vercel`。

所有路径字段均为**绝对路径**（构建器已把插件 cwd 切到插件目录，相对路径会解析错误）。

## 输出协议

脚本把结果写入 `out.json`（绝对路径，第二个参数）：

```json
{ "ok": true, "message": "已注入页脚" }
```

| 字段 | 说明 |
| --- | --- |
| `ok` | `true` 成功 / `false` 业务失败 |
| `message` | 展示给构建日志的文本（构建器打印 `[plugin <名>] <message>`） |

## 失败隔离语义

插件**永远不阻断构建**：

- 启动失败（脚本不存在/不可执行）→ 警告，退出码 `-1`；
- 脚本退出码非 0 → 警告；
- 超时被杀死 → 警告，退出码 `124`；
- `out.json` 里 `ok: false` → 普通提示。

以上任一情况，`Cdocs build` 都继续执行并正常退出 0。

## 完整示例（Python）

一个在每页正文末尾追加页脚、并在结束时统计产物体积的插件：

```python
import json, os, sys

ctx_path, out_path = sys.argv[1], sys.argv[2]
ctx = json.load(open(ctx_path, encoding="utf-8"))

def done(result):
    json.dump(result, open(out_path, "w", encoding="utf-8"))

# 文件级钩子：注入页脚（用幂等 marker 防止重复注入）
if "path" in ctx:
    p = ctx["path"]
    text = open(p, encoding="utf-8").read()
    marker = "<!-- footer-by-plugin -->"
    if marker not in text:
        text = text.replace("</main>", marker + "\n<p>由插件注入</p>\n</main>", 1)
        open(p, "w", encoding="utf-8").write(text)
    done({"ok": True, "message": "已注入页脚"})
else:
    # 全站钩子：统计 dist 体积
    total = sum(os.path.getsize(os.path.join(root, f))
                for root, _, files in os.walk(ctx["dest"]) for f in files)
    done({"ok": True, "message": f"dist 总大小 {total//1024} KB"})
```

## 与内置功能的取舍

以下能力**引擎内置**（约定优于配置，无需插件）：版本化文档（`docs-*` 快照自动识别）、RSS/PWA/sitemap/SEO、图片与代码压缩、正文末尾通用注入（`on_config` 返回 `inject` 片段，任意组件/评论系统可用）。

**评论系统已插件化**（示例：`.Cdocs/plugins/giscus/`）：giscus 插件在 `on_config` 钩子读取 `config.json` 的 `center.comments`，返回 `{ inject: { 语言: HTML } }`，引擎按语言注入每个页面正文末尾。想换评论系统（utterances / Valine / Gitalk 等）只需新建插件并在 `on_config` 输出 `inject`，引擎无需改动。

**部署自动化已插件化**（示例：`.Cdocs/plugins/github-pages/`、`.Cdocs/plugins/vercel/`）：运行 `Cdocs deploy --setup`，引擎扫描声明 `setup` 钩子的插件，插件在项目根生成 `.github/workflows/*.yml` 与 `vercel.json`。这些文件提交进 git 后，push 即由 GitHub Actions / Vercel 平台自动部署（云端机制，不依赖本地编译）。换/加部署平台 = 增删一个部署插件目录，引擎不感知平台细节。

插件适合做**站点特有**的收尾工作：评论/注入组件、部署配置、自定义统计、告警通知、内容校验等。
