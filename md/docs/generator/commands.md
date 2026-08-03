# 命令行参考

Cdocs 采用 **纯子命令 CLI**（对标 Hugo / MkDocs），语法：

```text
Cdocs [全局旗标] <子命令> [参数]
```

## 全局旗标（放在子命令之前）

| 旗标 | 说明 |
|------|------|
| `-c, --config <目录>` | 引擎目录（默认 `.Cdocs`） |
| `-s, --source <目录>` | 内容目录（默认 `md`） |
| `-d, --dest <目录>` | 输出目录（默认 `dist`） |
| `-q, --quiet` | 静默模式（只输出错误） |
| `-V, --verbose` | 详细输出 |
| `-h, --help` | 帮助 |
| `-v, --version` | 版本号 |

## 子命令

| 子命令 | 作用 | 常用旗标 |
|--------|------|----------|
| `init <目录>` | **建站**：生成完整骨架（配置/i18n/示例文档/引擎+exe），自动构建 | `--no-engine` 只出内容骨架；`--defaults`/`-y` 跳过交互 |
| `new <名>`（别名 `add`/`page`） | **建页**：用 archetype 生成文档并登记导航 | — |
| `section <blog\|docs\|docs-v<n>>` | **加内容区**：添加分类（blog/docs 唯一，历史版本 docs-v<n>） | — |
| `build` | 构建站点（位置参数可覆盖输入/输出目录） | `-D/--drafts`、`--clean`、`-q/-V` |
| `serve` | 构建并启动内置 HTTP 预览（默认 8088，端口被占自动顺延） | `-p/--port`、`-o/--open`、`-w/--watch`、`--no-build` |
| `deploy` | 构建并发布（gh-pages 分支推送 / Vercel） | `--remote <url>`、`--branch <b>`、`-m <msg>`、`--force`、`--vercel`、`--setup` |
| `clean` | 清空 `dist` | — |
| `doctor` | **环境自检**：config/内容区/主题/工具探测 | — |
| `check` | **质量检查**：死链 + 组件 token 残留 + 未渲染数据孔（跳过 script/pre/code 保护块） | — |
| `config` | 打印解析后的配置摘要（诊断用） | — |
| `routes` | 列出 route 登记的页面路由清单 | — |
| `theme` | 列出可用主题（themes/ 目录）+ 当前生效主题 | — |
| `plugins` | 列出已注册插件（.Cdocs/plugins/）+ 各自钩子 | — |
| `versions` | 列出配置的版本（site.versions；未配置时扫描 md-* 快照约定） | — |
| `version` / `help [命令]` | 版本 / 帮助（可指定命令查看子帮助） | — |

## 退出码

| 码 | 含义 |
|----|------|
| `0` | 成功 |
| `1` | 运行错误（输入目录不存在、无文档等） |
| `2` | 用法错误（未知命令 / 未知旗标） |

## 行为约定

- **无参数**运行打印帮助并退出（exit 0）；双击 exe 无参数打印帮助并等待 **Ctrl+C**（防窗口一闪而过）。
- **`serve` 是常驻进程**：按 **Ctrl+C** 干净退出；`-w/--watch` 监听 `md/` 与 `.Cdocs/config` 变化自动重建。
- **`check` 在构建后运行**（检查 `dist/`）；`doctor` 随时可跑（不构建）。
- **路径相对当前终端目录（CWD）解析**：挂到 PATH 后，进到哪个项目目录跑 `Cdocs`，处理的就是哪个项目的 `md/` 与 `.Cdocs/`。
- **`init` 从 exe 旁的 `.Cdocs/` 复制引擎资源**（theme/deps）到新站，`--no-engine` 时仅生成内容骨架——因此发布包（`release/` = `Cdocs.exe` + `.Cdocs/`）应整体放入 PATH，保证 `init` 开箱即看。
- 在非 Cdocs 项目目录跑 `build` 会报「找不到 `.Cdocs/config`」——与在非 Hugo 站点里跑 `hugo` 行为一致。

## 示例

```bash
Cdocs -h                      # 帮助
Cdocs help build              # build 子命令帮助
Cdocs init mysite             # 建站
Cdocs new faq                 # 建页
Cdocs section blog            # 加博客内容区
Cdocs build                   # 构建
Cdocs build -D --clean        # 含草稿 + 先清空
Cdocs -d /tmp/out build       # 输出到指定目录
Cdocs serve -o -w -p 3000     # 预览 + 开浏览器 + 热重载 + 3000 端口
Cdocs doctor                  # 环境自检
Cdocs check                   # 质量检查（死链/token/数据孔）
Cdocs theme                   # 列出可用主题 + 当前主题
Cdocs plugins                 # 列出已注册插件 + 钩子
Cdocs versions                # 列出配置的版本
Cdocs config                  # 配置摘要
Cdocs routes                  # 页面路由清单
Cdocs clean                   # 清空 dist
```
