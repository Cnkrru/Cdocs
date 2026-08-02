// core.hpp —— Cdocs 共享前置头：类型定义、全局状态 extern 声明、跨切面工具函数声明
//
// 本头文件汇聚原 main.cpp 顶部的全部 include，并提供各模块共享的类型与工具函数声明。
// 每个模块的 .cpp 都应先 #include "core.hpp"。
// 纯代码搬迁：内容与原 main.cpp 保持一致，仅从单一 TU 拆分而来。

#ifndef CDOCS_CORE_HPP
#define CDOCS_CORE_HPP

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <map>
#include <set>
#include <utility>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <chrono>
#include <sys/stat.h>
#include <filesystem>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>
#endif
#include <thread>
#include <chrono>
#include <cstdlib>
#include <csignal>

#include "nlohmann/json.hpp"
#include "markdown.hpp"
#include "color.hpp"   // 跨平台 ANSI 颜色库

namespace fs = std::filesystem;
using json = nlohmann::json;

#define CDOCS_VERSION "0.1.0"

// 跨平台 socket 别名
#ifdef _WIN32
using sock_t = SOCKET;
#define CDOCS_CLOSESOCK closesocket
#else
using sock_t = int;
#define CDOCS_CLOSESOCK ::close
#ifndef INVALID_SOCKET
#define INVALID_SOCKET (-1)
#endif
#endif

// ---------------- 数据结构 ----------------

struct Link {
    std::string title;
    std::string file;   // 站内链接，指向 file.html
    std::string url;    // 外链
    bool external() const { return !url.empty(); }
};

struct HeaderCfg {
    std::string logo;
    bool showSearch = true;
    bool showThemeToggle = true;
    std::vector<Link> links;   // 页眉左侧导航（logo 旁，topnav）
    std::vector<Link> nav;     // 页眉右侧导航（i18n 按钮左侧，topbar-nav，最多渲染 6 个）
    std::string github;        // GitHub 仓库地址：页眉经典图标按钮（空则不显示）
};

struct FooterCfg {
    std::string text;
    std::vector<Link> links;
};

struct Page {
    std::string file;   // 文件名（无扩展）
    std::string title;  // 显示标题
    std::string html;   // 渲染后的正文 HTML
    std::string desc;   // 摘要（用于 meta description 与搜索索引）
    bool draft = false; // front matter: draft: true 不发布
    int weight = 0;     // front matter: weight 控制排序
    std::string date;   // front matter: date（可选）
    std::time_t dateT = 0;          // 发布时间（time_t）：date 解析失败则退回文件 mtime，供 feed/SEO
    std::string lastmod;            // front matter: lastmod（修改时间，优先于文件 mtime）
    std::vector<std::string> aliases; // front matter: aliases（旧路径，生成重定向页）
    std::vector<std::string> tags; // front matter: tags（标签聚合用）
};

// 侧边栏导航节点：分组节点含 children；叶子节点含 file 或 url
struct NavNode {
    std::string title;
    std::string file;
    std::string url;
    int weight = 0;     // 分组内排序权重（front matter / route 均可给）
    std::vector<NavNode> children;
    bool is_group() const { return !children.empty(); }
};

// 首页卡片：用户白名单（config.home.cards），file 必填，title/desc 可选覆盖
struct HomeCardCfg {
    std::string file;
    std::string title;
    std::string desc;
};

// 版本化文档（Docusaurus 风格）：每个版本一个源目录 + 独立构建产物
struct VersionCfg {
    std::string name;      // 版本标识（目录名/URL 段，如 "v2"）
    std::string label;     // 下拉显示名（如 "2.0"）
    std::string source;    // 源目录（默认 docs；历史版本如 docs-v1）
    bool default_v = false; // 是否为默认版本（根 index 重定向目标）
};

struct SiteConfig {
    std::string title = "文档";
    std::string description;
    std::string theme = "dark";
    std::string url;                 // 站点基址，用于 sitemap.xml（SEO 标配）
    std::string ogImage;             // 社交分享封面图（og:image / twitter:image），相对/绝对 URL
    std::string editBase;            // 编辑此页：仓库编辑基址（如 .../edit/main）
    std::string editDocsDir;         // 编辑此页：仓库内文档目录（如 docs）
    // 侧边栏映射：key=版本源目录名（docs / docs-v1 ...）或 "blog"，value=相对 .Cdocs/config/ 的 JSON 路径
    // 如 { "docs": "sidebar/docs.json", "docs-v1": "sidebar/v1.json", "blog": "sidebar/blog.json" }
    std::map<std::string, std::string> sidebarMap;
    HeaderCfg header;
    FooterCfg footer;
    std::vector<std::string> plugins;   // 功能开关，空 = 全部启用
    int backToTopThreshold = 300;
    std::string backToTopLabel = "↑ 顶部";
    std::vector<NavNode> nav;           // 来自 route.json（嵌套树）
    std::string themeVars;              // 注入 <style> 的 CSS 变量覆盖
    std::string customCssHref;          // 用户自定义 CSS 的 href（数据，MetaHead 组件用）
    std::string customCssLink;          // 用户自定义 CSS 的 <link>（fallback 用）
    // 首页（config.home）：hero 可选覆盖 + cards 白名单
    //   homeCards 非空 = 只显示白名单页面（顺序即展示顺序）；空 = 自动列出全部非草稿页
    std::string homeTitle, homeSubtitle;   // hero 覆盖（空 = 回退 title/description）
    std::string homeCtaText, homeCtaFile;  // CTA 覆盖（空 = 第一篇文档 + {{getStarted}}）
    std::vector<HomeCardCfg> homeCards;
    std::string feedbackEndpoint;          // 反馈上报端点（config.feedback.endpoint，空 = 仅本地记忆）
    bool compress = true;                // 构建期压缩（config site.compress，默认开启：图片 + HTML/CSS）
    int  jpegQuality = 82;               // JPEG 重压质量（config site.jpegQuality，1-100）
    // 版本化文档（versions.json，空 = 单版本普通站点）
    std::vector<VersionCfg> versions;
    std::string curVersion;                // 当前构建的版本名（空 = 单版本）
    std::string curVersionLabel;           // 当前版本显示名
};

struct RenderOpts {
    bool showSearch = true;
    bool showThemeToggle = true;
    bool showPager = true;
    bool showToc = true;
    bool showBackToTop = true;
    bool showCodeHighlight = true;
};

// i18n（多语言）：行业标准 {{key}}+json 实现
//   - 每个语言一份扁平 JSON 字典（.Cdocs/i18n/zh-CN.json 等），key→string 映射
//   - UI 文案与正文里的 {{key}} 在构建时按当前语言字典替换
//   - 字典 value 内可用 {{minutes}} 这类令牌做动态插值（由代码回填，如阅读时长）
typedef std::map<std::string, std::string> I18nDict;
struct I18nCfg {
    std::string defaultLocale = "zh-CN";
    std::map<std::string, std::string> labels;                       // loc -> 显示名（如 简体中文）
    std::map<std::string, I18nDict> dicts;                           // loc -> 字典
    bool enabled = false;
};

// ---------------- 全局状态（在 main.cpp 定义，此处 extern 声明） ----------------

// 预览服务器优雅退出标志：仅响应人工信号（Ctrl+C / SIGTERM）
extern volatile std::sig_atomic_t g_serve_running;
// 双击无参数运行时的等待标志
extern volatile std::sig_atomic_t g_idle_running;

// ============ 全局 CLI 选项（子命令前解析，对标 Hugo/MkDocs 的全局旗标）============
extern fs::path g_source;    // -s/--source：Markdown 源目录
extern fs::path g_dest;      // -d/--dest：输出目录（serve 也用作预览根）
extern fs::path g_engine;    // -c/--config：引擎/配置根目录
extern bool     g_quiet;     // -q/--quiet：静默输出
extern bool     g_verbose;   // -V/--verbose：详细输出
extern bool     g_include_drafts; // build -D/--drafts：包含草稿
extern bool     g_clean_before;   // build --clean：构建前清空输出
extern bool     g_incremental;    // serve -w：增量构建（仅 watch 循环置位）
extern std::string g_cur_version;         // 多版本构建：当前版本名（空 = 单版本）
extern std::string g_cur_version_label;   // 多版本构建：当前版本显示名
extern std::string g_versions_json;       // 多版本构建：完整版本列表（JSON 数组，子构建读入 cfg.versions）
extern std::map<std::string, std::string> g_body_ends;  // 正文末尾注入（插件 on_config 提供，key=语言 → HTML 片段）
extern std::vector<std::string> g_i18n_missing;  // 构建期收集：i18n 替换未命中的键（供末尾告警）
extern std::vector<std::string> g_link_broken;   // 构建期收集：死链（页面 → 不存在的站内目标，供末尾告警）
extern std::map<std::string, std::string> g_fp;  // 资源指纹：相对路径（assets/css/style.css）→ 内容哈希（8 hex），供 ?v= 引用与 sw 缓存同步

// ---------------- 信号处理 ----------------
void serve_signal_handler(int sig);
void idle_signal_handler(int sig);

// ---------------- 工具函数 ----------------

std::string read_file(const fs::path& p);
std::string esc(const std::string& s);
std::string esc_attr(const std::string& s);
std::string strip_tags(const std::string& s);
std::string truncate_utf8(const std::string& s, size_t max_bytes);
std::string extract_title(const std::string& md, const std::string& fallback);
std::string trim(const std::string& s);
std::string collapse_ws(const std::string& s);
std::string slugify(const std::string& s);
bool has_plugin(const std::vector<std::string>& plugins, const std::string& name);
void copy_doc_assets(const fs::path& in_dir, const fs::path& out_dir, std::error_code& ec);
std::string format_mtime(const fs::path& p);
std::time_t file_mtime_t(const fs::path& p);
std::time_t parse_date_str(const std::string& s);
std::string iso8601(std::time_t t);
std::string fmt822(std::time_t t);
// 本地日期（YYYY-MM-DD）：博客/文章展示用（parse_date_str 按本地解析，iso8601 按 UTC 输出，
// UTC+8 下直接 substr 会倒退一天，故显示一律走本地时区）
std::string format_date_local(std::time_t t);
std::pair<int,int> count_words(const std::string& text);

// 平台/生命周期辅助
fs::path exe_dir();
bool launched_by_doubleclick();

#endif  // CDOCS_CORE_HPP
