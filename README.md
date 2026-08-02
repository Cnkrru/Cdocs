# Cdocs

一个用 **C++** 编写的**静态文档站生成器**（static documentation site generator）。
它把自己这个项目的文档站也生成出来了（自带 dogfooding）。

> 对标 Docusaurus / VitePress / MkDocs，但核心引擎是纯 C++，**渲染与发布全链路零运行时依赖**：
> RSS 2.0、JSON Feed、PWA（可离线）、robots.txt、sitemap、OG/Twitter 社交卡片、JSON-LD
> 结构化数据等全部由单个 `Cdocs.exe` 在构建时直接产出，**无需 Node、无需任何外部脚本**。
> 产物是**零依赖、可离线、中英双语**的静态站点。

---

## 这是什么

- 输入：你在 `docs/` 下写的 Markdown（中文 `x.md` + 英文 `x.en.md` 配对）+ 一份 JSON 配置。
- 引擎：`Cdocs.exe`（C++17，用 MinGW-w64 的 gcc **静态编译成单文件**，不依赖运行库）。
- 输出：`dist/` 下按语言分目录（`zh-CN/`、`en/`）的静态站点，含首页、各文档页、
  RSS / JSON Feed、PWA（可离线）、sitemap、结构化数据（SEO）。
- Markdown 解析复用成熟的 `md4c`；JSON 解析复用 `nlohmann/json`。

### 技术栈与能力

| 维度 | 实现 |
|------|------|
| 语言 | C++17，gcc 静态链接（`-static -static-libgcc -static-libstdc++`） |
| Markdown | 内置 `md4c`（`.Cdocs/deps/vendor/md4c`） |
| 多语言 | `{{key}}` + `i18n/*.json` 字典替换 |
| 前端 | Mermaid 图、Admonition 提示框、阅读时长、面包屑、脚注、上下标 |
| 发布/SEO | 内置 RSS 2.0 + JSON Feed、PWA 离线（manifest + Service Worker + 图标）、robots.txt、sitemap、OG/Twitter 社交卡片、JSON-LD 结构化数据——全部零运行时依赖 |
| 预览服务 | 内置极简 HTTP 服务器（仅监听 `127.0.0.1`）；**常驻前台，仅人工 Ctrl+C 退出**，并忽略 SIGPIPE（管道/客户端断开不会误杀） |

### 构建管线（两步）

`build.cmd` 一键串联：

1. **`[0]` 编译生成器**：用 gcc 把 `main.cpp` + `markdown.cpp` + `md4c` 编成 `Cdocs.exe`。
2. **`[1]` 生成站点**：`Cdocs.exe build` 读取 `docs/` → 写出 `dist/`，并**一并内置生成**
   RSS 2.0 / JSON Feed、PWA（`manifest.webmanifest` + `sw.js` + `icon.svg`）、`robots.txt`、
   sitemap、每页 OG/Twitter 社交 meta 与 JSON-LD 结构化数据——全部由同一进程完成，**无需 Node、无外部脚本**。

---

## 命令行工具

所有输出在**真实终端**中带 ANSI 颜色（错误红、警告黄、成功绿、URL 青色下划线、
命令名绿、提示灰）；被管道/重定向时自动关闭颜色以保持日志干净。
可用环境变量 `CDOCS_FORCE_COLOR=1` 强制开启。

**纯子命令 CLI（对标 Hugo / MkDocs）**：所有功能都通过显式子命令触发；无参数运行
`Cdocs` 会打印帮助并以退出码 `0` 退出，打错命令会明确报错并以退出码 `2` 退出（严格 CLI，不自动做任何事）。

### 命令一览

| 命令 | 说明 |
|------|------|
| `Cdocs`（无参数） | 打印帮助并退出（exit 0）；双击 exe 无参数时等待 **Ctrl+C** 关闭窗口（行业惯例，不响应回车），避免窗口一闪而过 |
| `Cdocs init <目录> [--no-engine]` | **建站**：在指定目录生成完整站点骨架（config/route/i18n 字典、双语示例文档、`archetypes/` 模板、前端资源），并自动构建 |
| `Cdocs new <名>` / `add` / `page` | **建页**：新建一篇文档（从 `archetypes/default.md` 生成 `<名>.md` + `<名>.en.md`），并自动登记进 `route.json` 侧边栏导航 |
| `Cdocs build [-D/--drafts] [--clean] [源] [目标]` | 构建站点（默认 `docs` → `dist`） |
| `Cdocs serve [-p 端口] [-w] [-o] [--no-build]` | 构建并启动本地预览服务器（默认端口 `8088`，仅监听本机）；常驻运行，按 Ctrl+C 才退出 |
| `Cdocs clean` | 清空输出目录（默认 `dist`）；对标 `jekyll clean` / `docusaurus clear` |
| `Cdocs version` / `-v` / `--version` | 显示版本号 |
| `Cdocs help` / `-h` / `--help` | 显示帮助 |

### 全局旗标（放在子命令之前，对标 Hugo/MkDocs）

| 旗标 | 作用 |
|------|------|
| `-c, --config <目录>` | 引擎/配置根目录（默认 `.Cdocs`） |
| `-s, --source <目录>` | Markdown 源目录（默认 `docs`） |
| `-d, --dest <目录>` | 输出目录（默认 `dist`，`serve` 也用它作预览根） |
| `-q, --quiet` | 静默输出（仅错误/警告） |
| `-V, --verbose` | 详细输出 |
| `-h, --help` | 显示帮助（exit 0） |
| `-v, --version` | 显示版本号（exit 0） |

### 子命令旗标

| 旗标 | 作用 |
|------|------|
| `build -D, --drafts` | 包含草稿页（默认排除 `draft: true` 的页面） |
| `build --clean` | 构建前清空输出目录（清理上一次陈旧的产物） |
| `serve -p, --port <n>` | 指定端口（默认 `8088`，被占用时自动顺延 8089/8090…） |
| `serve -w, --watch` | 监听 `docs/` 与 `.Cdocs/config/` 改动，自动重新构建（HMR 风格） |
| `serve -o, --open` | 启动后自动打开系统默认浏览器 |
| `serve --no-build` | 跳过构建，直接预览现有 `dist` |
| `init --no-engine` | 仅生成内容骨架，不复制引擎与 `Cdocs.exe`（适合把已有项目当模板复用） |

任意子命令后加 `-h/--help` 可查看该命令的用法（如 `Cdocs build --help`、`Cdocs serve --help`）。

### 退出码

| 码 | 含义 |
|----|------|
| `0` | 成功（或 help/version 已打印） |
| `1` | 运行错误（输入目录不存在、构建失败、清理失败等） |
| `2` | 用法错误（未知命令 / 缺必填参数 / 未知旗标 / 旗标缺少值） |

> **`init` 生成的站点即为一个独立项目**：包含自己的 `.Cdocs/`（配置、i18n、引擎）与
> `Cdocs.exe`，进入目录即可 `build` / `serve`。`--no-engine` 可只生成内容骨架、不复制引擎
> （适合把已有 Cdocs 项目当模板复用）。

> **`serve` 是常驻服务进程**：启动后一直运行，只有你手动按 **Ctrl+C** 才会退出；
> 不会因为被管道（如 `Cdocs serve | head`）截断、或客户端断开连接就自行退出（已忽略 SIGPIPE）。
> 收到 Ctrl+C 时会打印「收到停止信号，正在关闭预览服务器…」再干净退出。
> **端口自动顺延**：若默认 `8088`（或 `-p` 指定端口）被占用，会自动尝试 `8089`、`8090`… 直到
> 找到空闲端口，并打印「端口 X 被占用，自动改用 Y」，不会因端口冲突直接退出。
> **自动开浏览器**：`serve -o/--open` 会在服务器就绪后尝试用系统默认浏览器打开预览地址。
> **热重载**：`serve -w/--watch` 会监听 `docs/` 与 `.Cdocs/config/` 的修改，一旦变化自动重新构建并刷新
> （类似 `vite` 的 HMR）；服务端实时打印 `[watch] 检测到文档变化，重新构建…` 日志。

**双击预览（推荐普通用户）**：直接双击项目根目录的 `serve.bat` 即启动常驻预览服务器——窗口一直开着，
**只有你按 Ctrl+C 才关**。若 `dist` 不存在等出错，窗口会停留显示错误而非一闪而过。
（双击 `Cdocs.exe` 本身无参数时会打印帮助并等待 **Ctrl+C** 关闭窗口（不响应回车），避免窗口一闪而过；它不再自动开服务器。）
也可在命令行用 `Cdocs serve -o --watch -p 3000` 传参。

示例：

```bash
Cdocs init mysite            # 新建完整站点骨架（开箱即构建）
cd mysite
Cdocs new faq               # 新建一篇文档并加入导航（别名 add / page）
Cdocs build                 # docs → dist
Cdocs build -D --clean      # 含草稿页并先清空上一次产物
Cdocs serve -p 3000         # 构建后在 3000 端口预览（被占则自动顺延）
Cdocs serve -o -w           # 自动开浏览器 + 文件变化自动重建
Cdocs serve --no-build      # 只预览现有 dist，不重新构建
Cdocs clean                 # 清空 dist（构建前清理用 build --clean 更顺手）
```

---

## 目录结构

```
Cdocs/
├── src/               C++ 生成器源码（17 个模块：core 底座 / 领域模块 / cli / builder / compress / linkcheck / deploy）
├── .Cdocs/
│   ├── config/        config.json（站点配置）+ route.json（侧边栏导航）
│   ├── i18n/          zh-CN.json / en.json（多语言字典）
│   ├── theme/         主题（一个主题=一个文件夹）：assets/ 前端资源 + templates/layout.html 页面骨架 + theme.json
│   ├── deps/          前端运行时库（Mermaid/KaTeX/FlexSearch/PhotoSwipe）+ vendor/ 编译期头文件
│   └── tools/         build.cmd / build.sh（编译 + 生成，无外部脚本依赖）
├── docs/              *.md（中文）+ *.en.md（英文）源文档
├── dist/              生成的静态站点（构建产物，默认开启压缩：图片重编码 + HTML/CSS 紧凑化）
└── Cdocs.exe          编译好的生成器
```

---

## 文档元数据与高级特性

### Front matter（文档头元数据）
每篇文档可在开头用 `---` 包裹的 YAML 块声明元数据，生成器据此驱动标题、排序与发布：

| 字段 | 作用 |
|------|------|
| `title` | 页面标题（优先级高于侧边栏 title 与正文首个 `#`） |
| `date` | 日期（保留，供主题/插件扩展） |
| `draft` | `true` 时不发布：不生成页面、不进导航 / 搜索 / 站点地图 |
| `weight` | 排序权重（自动发现模式下按 weight 升序） |
| `tags`  | 标签数组，自动生成标签聚合页（`/tags/<tag>.html`）与总览页 |

无 front matter 的旧文档完全兼容（标题回退到正文首个 `#`）。`Cdocs add <名>` 生成的文档自带 front matter 模板。

### 静态资源发布
`docs/` 下的图片、附件等非 Markdown 文件会按相对路径自动拷贝到 `dist/`（每个语言目录各一份），
因此文档里用相对路径引用本地图片 / 下载文件即可正常加载，例如 `![图](img/foo.png)` → `dist/zh-CN/img/foo.png`。

### 标签聚合
只要任意页面声明了 `tags`，构建会自动生成 `dist/<loc>/tags/index.html` 总览页与每个标签的列表页，
并在侧边栏末尾追加「标签」入口。

### 富文本增强
- **脚注**：`[^1]` 引用 + `[^1]: 定义`，自动生成脚注区（基于 md4c `FOOTNOTES`）。
- **上下标**：`x^2^` → 上标 x²，`H~2~O` → 下标 H₂O（代码块 `<pre>/<code>` 内不转换，避免误伤）。

### 回顶按钮文案
`config.json` 的 `backToTop.label` 可自定义悬浮按钮文字（留空则用 i18n 字典的「顶部」）。

### 内置订阅源、PWA 与社交 SEO（零运行时依赖）

上述 RSS 2.0 / JSON Feed / PWA / robots.txt / sitemap / OG-Twitter 卡片 / JSON-LD **全部由
`Cdocs.exe` 在构建时直接生成**，不再需要早期版本的 `gen-rss.js` / `gen-pwa.js` 两个 Node 脚本——
产物是真正的单文件生成器，目标机只需 `Cdocs.exe`，**无需安装 Node**。

- **RSS 2.0**：每个语言目录输出 `rss.xml`；且默认语言（第一个 `langs` 项）的订阅源会
  额外写一份到 `dist/` 根目录，方便 `https://站点/rss.xml` 直接订阅。
- **JSON Feed**：同位置输出 `feed.json`（主流阅读器兼容）。
- **PWA 离线**：生成 `manifest.webmanifest`（含名称/起始页/图标）、`sw.js`（Service Worker
  缓存站点壳）与 `icon.svg`；`<head>` 注入 `<link rel="manifest">` 与主题色，
  支持的浏览器可「添加到主屏幕 / 离线访问」。
- **robots.txt**：站点根与每个语言目录写入 `robots.txt`，允许抓取并指向 sitemap。
- **sitemap.xml**：全站 URL 索引（含多语言 `alternate`），供搜索引擎发现。
- **OG / Twitter 卡片**：每篇文档 `<head>` 注入 `og:title/og:description/og:type/
  og:url/og:image` 与 `twitter:card/twitter:title/twitter:description`，分享到社交平台
  呈现富预览；文章页额外带 `article:published_time` / `article:modified_time` 时间戳。
- **JSON-LD 结构化数据**：自动输出 WebSite / Article / BreadcrumbList 三类 JSON-LD，
  提升搜索引擎理解与富结果展示。

> 站点根 URL 与社交图来自 `config.json` 的 `url` 与 `ogImage` 字段：
> ```json
> { "url": "https://docsgen.example.com", "ogImage": "/icon.svg" }
> ```
> 描述（description）支持按语言配置；社交卡片描述会自动去除与标题重复的前缀，避免显示冗余。

## 当前状态

- 生成器可完整编译，构建全链路绿灯。
- 已修复：`render.md`（渲染循环）侧边栏曾被误标为「顶点缓冲」（`navVertexBuffer`）。
- 已重写：`api.md` / `pipeline.md` 原是从另一图形项目污染来的错误内容，已替换为真实文档。
- 已完善：`serve` 改为**常驻前台服务**——仅人工 Ctrl+C 退出；忽略 SIGPIPE（管道/客户端断开不误杀），
  单连接异常（非法/保留路径等）只关该连接、不连累服务器；用带超时 `select()` 轮询 `accept`，
  收到信号后优雅关闭并打印提示；**端口被占用时自动顺延**而非直接退出。
- 新增 `serve.bat` 启动器：双击即可开常驻预览服务器（Ctrl+C 才关），出错时窗口停留显示原因。
- 新增类 Hugo 工作流：
  - `init <目录>`（建站）：生成完整站点骨架（config.json、route.json、中英文 i18n 字典、双语示例文档、
    `archetypes/default.md` 模板、前端资源，并复制引擎使站点开箱即 `build`；`--no-engine` 可只生成内容骨架）。
  - `new <名>` / `add` / `page`（建页）：用 archetype 模板生成 `<名>.md` + `<名>.en.md`，并自动登记进 `route.json` 侧边栏。
  - `serve -o/--open`：服务器就绪后自动打开系统默认浏览器。
  - `serve -w/--watch`：监听 `docs/` 与 `.Cdocs/config/` 修改，自动重新构建（HMR 风格）；
    修改时间采用 POSIX `stat` 取 `st_mtime`，规避 MinGW `filesystem::last_write_time` 不可靠问题。
- 新增文档元数据与高级特性（详见上文章节）：
  - **YAML front matter**：`title`/`date`/`draft`/`weight`/`tags`。`draft: true` 不发布（不生成页面、不进导航/搜索/站点地图）；`weight` 控制自动发现模式排序；`tags` 自动生成聚合页 `dist/<loc>/tags/<tag>.html` 与总览页，并在侧边栏追加「标签」入口。
  - **静态资源发布**：`docs/` 下图片/附件等非 Markdown 文件按相对路径自动拷贝到 `dist/`。
  - **富文本增强**：脚注（`[^1]` + 定义，md4c `FOOTNOTES`）、上下标（`^x^`→上标、`~x~`→下标，跳过代码块）。
  - **`backToTop.label` 配置生效**：自定义回顶按钮文字（留空走 i18n）。
  - `add` 生成的文档自带 front matter 模板（`{{title}}`/`{{date}}`/`{{slug}}` 占位由命令填充）。
  - 已端到端验证：front matter 标题覆盖、draft 不发布、tags 聚合页、静态资源拷贝、脚注/上下标、导航草稿过滤、add 生成 front matter——共 12 项 PASS。
- 已知小瑕疵（未动）：`window.__I18N__` 客户端字典里的 `readingTime` 含未替换的
  `{{minutes}}`/`{{words}}` 死数据，但正文阅读时长是服务端算好注入的，不影响展示。
- 本轮（行业标准完善，用户要求"按照行业标准完善"）：把 RSS 2.0 / JSON Feed / PWA / robots.txt /
  sitemap / OG-Twitter / JSON-LD **全部内联进 C++ 生成器**，删除早期 `tools/gen-rss.js` 与
  `tools/gen-pwa.js` 两个 Node 脚本，实现**零运行时依赖的单文件生成器**（目标机无需 Node）。
  - 新增 C++ 助手：`gen_feeds`（写 `rss.xml` + `feed.json`，默认语言额外写根目录订阅源）、
    `gen_pwa`（拷贝 `sw.js`/`icon.svg` + 写 `manifest.webmanifest`）、`social_head`
    （OG/Twitter/article 时间戳 meta）、`robots.txt` + `sitemap.xml` 生成、增强 `run_build`
    每页 `headExtra` 注入订阅源 link + manifest + 社交 meta + JSON-LD。
  - 配置：`config.json` 新增 `ogImage`（社交图默认 `/icon.svg`）；`url` 用于订阅源/sitemap/OG 绝对地址。
  - `build.cmd` / `build.sh` 移除 `[2/3] node gen-rss.js` 与 `[3/3] node gen-pwa.js` 两步，
    现在只有 **编译 + `Cdocs.exe build`** 两步。
  - 踩坑并修复 5 处：① `cmd_serve` 前向声明缺失（`handle_conn`/`i18n_replace` 提前用）；
    ② `--watch` 用 `fs::last_write_time` 在 MinGW 下不可靠（已改 POSIX `stat` `st_mtime`，见上文）；
    ③ MinGW `<ctime>` 无 `strptime`（日期解析改 `sscanf`）；
    ④ `gen_pwa` 写法中 json 数组括号歧义（改用 `json::array({json::object({...})})`）；
    ⑤ `og:image` 出现 `//icon.svg` 双斜杠（URL 拼接：以 `/` 或 `http` 开头不再补 `/`）；
    ⑥ 社交描述重复标题前缀（先 `i18n_replace` 解析 `{{navX}}` 再比前缀，并输出清洗后的描述）。
  - 验证：重编 `Cdocs.exe` 后构建自身文档站，检查 `dist/zh-CN/rss.xml`、`feed.json`、
    `manifest.webmanifest`、`robots.txt`、`sw.js`、`icon.svg` 及页面 `<head>`（中英双语）meta 均正常、
    无 `//` 双斜杠、描述无重复标题；README 已同步改为「两步构建 + 零运行时依赖」。
- 本轮（命令设计标准化，用户要求"按照行业标准完善命令"）：将 CLI 重构为**纯子命令模式（对标 Hugo / MkDocs）**，
  并明确 `init`=建站、`new`=建页 的语义。改动：
  - **全局旗标前置**：`-c/--config`、`-s/--source`、`-d/--dest`、`-q/--quiet`、`-V/--verbose`、
    `-h/--help`、`-v/--version` 全部放在子命令之前解析（Hugo/MkDocs 风格）；子命令的 `-h/--help` 显示该命令用法。
  - **退出码规范**：`0`=成功/已打印帮助，`1`=运行错误（目录不存在、构建/清理失败），`2`=用法错误（未知命令/缺参数/未知旗标/缺值）；
    无参数运行打印帮助并 exit 0，未知命令 exit 2（严格 CLI，不再任何自动行为）。
  - **新增 `clean` 子命令**：清空输出目录（默认 `dist`），对标 `jekyll clean` / `docusaurus clear`；
    `build --clean` 等价于「先 clean 再 build」，可避免上一次产物的陈旧文件残留。
  - **`build` 增强**：`-D/--drafts` 包含草稿页（默认排除 `draft: true`）；位置参数 `[源] [目标]` 覆盖默认 `docs`/`dist`。
  - **`serve` 增强**：`-w/--watch`（新增短旗标，原 `--watch` 仍可用）监听改动自动重建；`-p/--port`、`-o/--open`、`--no-build` 不变。
  - **双击体验**：双击 `Cdocs.exe` 无参数只打印帮助并等待 **Ctrl+C** 关闭窗口（行业惯例，不响应回车，防窗口一闪而过），不再自动开服务器；
    预览交给根目录 `serve.bat`（双击即开常驻服务器，Ctrl+C 才关）。
  - 验证（均 PASS）：无参数→帮助/exit 0；未知命令→exit 2；`build --help` 打印子命令帮助；`serve -w --help` 识别 `-w`；
    `build` 5 页、`-d` 覆盖目标生效；`build -D` 含草稿（6 页 + `dist/zh-CN/<draft>.html` 存在）、`build` 默认排除（文件不生成）。

- 本轮（主题收口 + 压缩 + 搜索增强，用户要求"把 assets/templates 收口到 theme、补 API 文档、图片代码压缩、搜索补强"）：
  - **主题收口**：`assets/` 与 `templates/` 统一收口到 `.Cdocs/theme/`（一个主题 = 一个文件夹：`theme.json` 元数据 + `templates/layout.html` 骨架 + `assets/` 前端资源）。构建器优先读 `theme/`，旧结构（assets/templates 直放引擎根）自动兼容。`init` 新站点自带主题。
  - **API 文档补全**：新增 `docs/themes.md`（主题目录结构 / theme.json 字段 / layout.html 占位符全表 / 换主题方法）与 `docs/plugins.md`（plugin.json / 4 钩子 / ctx·out JSON 协议 / 失败隔离语义 / Python 示例），双语并加入侧边栏导航。
  - **图片压缩（成熟库，默认开启）**：新增 `compress` 模块，用 **stb_image 解码 + libwebp 编码 WebP**（Google 成熟编码器，静态编译进 exe）为 PNG/JPEG/GIF/BMP/TGA 生成 `.webp` 副本，页面 `<img>` 自动升级 `<picture>` 优先 WebP（原图保留作回退）；仅当 WebP 更小时保留（实测照片类 JPEG -90%、大 PNG -76%）。config `site.compress=false` 关闭、`site.jpegQuality` 调质量。
  - **代码压缩（默认开启）**：HTML 构建期紧凑化（去注释 + 折叠空白，保护 `<pre>/<script>/<style>`，JSON-LD 完好）+ CSS 去注释/空白压缩（style.css -27%）。
  - **搜索补强**：索引加 `tags` 字段（可命中 + 徽标展示）、正文保留更长供前端截取"命中上下文"摘要；前端加 ↑↓ 键盘导航 / Enter 跳转 / 输入防抖 / 命中片段高亮。
  - 验证：`build --clean` 全绿；服务 200；压缩/搜索产物正确；i18n 告警为博客文章有意展示 `{{key}}` 语法的正常现象。
- 本轮（死链检查 + 顺手修复，用户 P0 第一项）：
  - **死链检查（构建期，对标 VitePress/MkDocs）**：新增 `linkcheck` 模块，扫描每个页面站内相对链接（`<a href>/<img src>/<link href>/<source srcset>`），解析到输出目录核对存在性（无扩展名自动补 `.html`/`/index.html`，子目录 `../` 回退，跳过 `<pre>/<code>/<script>/<style>` 内示例）；发现死链在构建末尾黄色告警、不阻断构建。实测：注入坏链接精确报出 `页面 → 目标`，还原后归零。
  - **死链检查立功，修复 3 个真实 bug**：① tags 聚合页（位于 tags/ 目录）标签链接误写 `tags/X.html` → 实际 404（改为相对 `X.html`）；② 博客页（blog/ 子目录）的 RSS/manifest 链接用 `./rss.xml` → 指向 blog/rss.xml 不存在（改为 `../`）；③ 文档示例 `[文字](url)` 被渲染成真实死链 href（示例改 `](#)` 占位）。语言切换链接误报问题通过"全部语言构建完成后统一检查"解决。
- 本轮（成熟度补强：P0 剩余 + P1 + P2 全做）：
  - **front matter 扩展**：`description`（每页 SEO 描述，优先于正文摘要）、`lastmod`（修改时间，优先于文件 mtime）、`aliases`（旧路径 → 自动生成 canonical+meta refresh 重定向页，对标 Hugo aliases）；修复 YAML 列表项缩进解析 bug（tags/aliases 的 `- item` 形式）。
  - **资源指纹（cache busting）**：主题 css/js 按内容算 FNV-1a 哈希，页面引用加 `?v=<hash>`，sw.js 缓存列表同步；内容未变 URL 稳定（增量构建不重复破缓存）。
  - **serve 传输层**：gzip 传输（静态编译 zlib 1.3.1）、`ETag` + 条件请求 304、分级 `Cache-Control`（assets 带指纹资源 immutable 一年、HTML no-cache）。
  - **admonition 构建期渲染**：`> [!tip/warning/...]` 在构建期转成 VitePress 风格 `div.admonition`（不再依赖客户端 JS，SEO/无 JS 可用）。
  - **响应式图片**：图片生成 480/800/1200 多尺寸 WebP（stb_image_resize2 缩放），`<picture srcset>` 按宽度选择 + `<img width/height>` 防 CLS。
  - **config schema 校验**：未知字段/类型错误构建期黄色告警（`site.titl` → "未知字段 site.titl（拼写检查？）"）；config.json 损坏不再崩溃（回退默认并提示）。
  - **目录结构自动导航**：route.json 缺失时按 `docs/` 子目录自动生成导航（子目录=分组）+ 自动路由（`guide/install.md` → `guide/install.html`），子目录页面 relBase（导航/RSS/manifest/hreflang/pager/面包屑）全部按深度修正。
  - **自动化回归测试**：`.Cdocs/tools/test.py`（init→自动导航→front matter→admonition→死链→压缩→指纹→config 校验→serve 传输层，**24 项 PASS**）。
  - 全程死链检查护航，修复多个相对路径 bug（子目录 hreflang/RSS/pager/面包屑）。
