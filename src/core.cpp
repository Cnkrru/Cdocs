// core.cpp —— 跨切面工具函数与信号处理的定义（自 main.cpp 原样搬迁）

#include "core.hpp"

// 预览服务器优雅退出标志处理：只置位标志，真正的清理在 accept 循环外做
void serve_signal_handler(int sig) {
    (void)sig;            // 只置位标志，真正的清理在 accept 循环外做
    g_serve_running = 0;
}

void idle_signal_handler(int sig) {
    (void)sig;
    g_idle_running = 0;
}

// ---------------- 工具函数 ----------------

std::string read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '&')      o += "&amp;";
        else if (c == '<') o += "&lt;";
        else if (c == '>') o += "&gt;";
        else               o += c;
    }
    return o;
}

std::string esc_attr(const std::string& s) {
    std::string o = esc(s);
    for (size_t i = 0; i < o.size(); i++)
        if (o[i] == '"') { o.replace(i, 1, "&quot;"); i += 5; }
    return o;
}

// 去掉 HTML 标签，用于搜索摘要 / 目录文字
std::string strip_tags(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    bool in_tag = false;
    for (char c : s) {
        if (c == '<') in_tag = true;
        else if (c == '>') in_tag = false;
        else if (!in_tag) out += c;
    }
    return out;
}

// 按 UTF-8 字符边界截断，避免把多字节字符从中间切断导致乱码/非法序列
std::string truncate_utf8(const std::string& s, size_t max_bytes) {
    if (s.size() <= max_bytes) return s;
    size_t i = 0, last_good = 0;
    while (i < max_bytes) {
        unsigned char c = (unsigned char)s[i];
        int len = 1;
        if      (c < 0x80)       len = 1;
        else if ((c >> 5) == 0x6)  len = 2;   // 110xxxxx
        else if ((c >> 4) == 0xE)  len = 3;   // 1110xxxx
        else if ((c >> 3) == 0x1E) len = 4;   // 11110xxx
        else { i++; continue; }               // 非法续字节，跳过一个字节
        if (i + (size_t)len > max_bytes) break;
        last_good = i + (size_t)len;
        i += (size_t)len;
    }
    return s.substr(0, last_good);
}

// 从 Markdown 正文取首个 # 标题（route 未提供标题时的兜底）
std::string extract_title(const std::string& md, const std::string& fallback) {
    std::istringstream iss(md);
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line[0] == '#') {
            size_t s = 1;
            while (s < line.size() && line[s] == '#') s++;
            if (s < line.size() && line[s] == ' ') s++;
            return line.substr(s);
        }
    }
    return fallback;
}

// 去掉首尾空白（含换行/制表），用于 front matter 字段清洗
std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

// 把连续空白（含换行/制表）折叠为单个空格，首尾去空格
// 用于 meta description、搜索摘要、面包屑等纯文本字段
std::string collapse_ws(const std::string& s) {
    std::string out; out.reserve(s.size());
    bool sp = false;
    for (char c : s) {
        if (c == '\n' || c == '\r' || c == '\t' || c == ' ') {
            if (sp) continue; out += ' '; sp = true;
        } else { out += c; sp = false; }
    }
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

// 由标题文本生成稳定的 URL slug（保留中英文字符，空格/下划线转连字符，去重号）
// 用于 heading 的 id 与 TOC 锚点，保证刷新/分享链接稳定。
std::string slugify(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = (unsigned char)s[i];
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c >= 0x80)
            out += (char)c;                          // 小写字母 / 数字 / 非 ASCII（中文）
        else if (c >= 'A' && c <= 'Z')
            out += (char)(c - 'A' + 'a');            // 大写转小写
        else if (c == ' ' || c == '-' || c == '_' || c == '/')
            out += '-';                              // 空白/分隔符 → 连字符
        // 其余标点忽略
    }
    std::string c2;
    for (size_t i = 0; i < out.size(); i++) {
        if (i > 0 && out[i] == '-' && out[i - 1] == '-') continue;
        c2 += out[i];
    }
    while (!c2.empty() && c2.back() == '-') c2.pop_back();
    while (!c2.empty() && c2.front() == '-') c2 = c2.substr(1);
    return c2;
}

bool has_plugin(const std::vector<std::string>& plugins, const std::string& name) {
    if (plugins.empty()) return true;  // 缺省 = 全部启用
    return std::find(plugins.begin(), plugins.end(), name) != plugins.end();
}

// 静态资源发布：把 docs/ 下非 Markdown 文件（图片/附件等）按相对路径拷到输出目录，
// 使文档以相对路径引用本地图片与下载文件即可加载。多语言模式下每个语言目录各拷一份。
void copy_doc_assets(const fs::path& in_dir, const fs::path& out_dir, std::error_code& ec) {
    if (!fs::exists(in_dir, ec)) return;
    for (auto it = fs::recursive_directory_iterator(in_dir, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        std::error_code e2;
        if (!fs::is_regular_file(*it, e2)) continue;
        std::string ext = it->path().extension().string();
        if (ext == ".md") continue;                     // 跳过 Markdown 源（含 .en.md 变体）
        std::string name = it->path().filename().string();
        if (!name.empty() && name[0] == '.') continue; // 跳过隐藏文件（.DS_Store 等）
        fs::path rel = fs::relative(it->path(), in_dir, e2);
        if (e2) continue;
        fs::path dst = out_dir / rel;
        fs::create_directories(dst.parent_path(), e2);
        fs::copy_file(it->path(), dst, fs::copy_options::overwrite_existing, e2);
    }
}

// 取文件最后修改时间，格式化为 "YYYY-MM-DD HH:MM"（"最后更新"用）
// 用 POSIX stat 直接拿到 time_t，避免 file_clock 与各平台 system_clock 基不一致的问题
std::string format_mtime(const fs::path& p) {
    struct stat st;
    if (stat(p.string().c_str(), &st) != 0) return "";
    std::time_t t = st.st_mtime;
    std::tm* tm = std::localtime(&t);
    if (!tm) return "";
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", tm);
    return std::string(buf);
}

// 文件最后修改时间（time_t），供 feed 发布时间与文章时间使用
std::time_t file_mtime_t(const fs::path& p) {
    struct stat st;
    if (stat(p.string().c_str(), &st) != 0) return std::time(nullptr);
    return (std::time_t)st.st_mtime;
}
// 解析 front matter 的 date（"2026-08-01" 或带时间 "2026-08-01T12:00:00"），失败返回 0
std::time_t parse_date_str(const std::string& s) {
    if (s.empty()) return 0;
    int Y = 0, M = 0, D = 0, h = 0, m = 0, sec = 0;
    if (std::sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &m, &sec) >= 3) {
        // 含时间
    } else if (std::sscanf(s.c_str(), "%d-%d-%d", &Y, &M, &D) >= 3) {
        // 仅日期
    } else {
        return 0;
    }
    std::tm tm{};
    tm.tm_year = Y - 1900;
    tm.tm_mon  = M - 1;
    tm.tm_mday = D;
    tm.tm_hour = h; tm.tm_min = m; tm.tm_sec = sec;
    tm.tm_isdst = -1;
    std::time_t t = std::mktime(&tm);
    return t;
}
// UTC 的 ISO8601（社交/结构化数据时间格式），如 2026-08-01T00:00:00Z
std::string iso8601(std::time_t t) {
    if (t == 0) t = std::time(nullptr);
    std::tm* ut = std::gmtime(&t);
    if (!ut) return "";
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", ut);
    return std::string(buf);
}
// 本地日期（YYYY-MM-DD）：展示层用（RSS/ISO8601 走 UTC，显示走本地时区避免倒退一天）
std::string format_date_local(std::time_t t) {
    if (t == 0) t = std::time(nullptr);
    std::tm* lt = std::localtime(&t);
    if (!lt) return "";
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", lt);
    return std::string(buf);
}
// RFC 822（RSS pubDate 格式），如 Sat, 01 Aug 2026 00:00:00 GMT
std::string fmt822(std::time_t t) {
    std::tm* ut = std::gmtime(&t);
    if (!ut) return "";
    char buf[64];
    std::strftime(buf, sizeof(buf), "%a, %d %b %Y %H:%M:%S GMT", ut);
    return std::string(buf);
}

// 字数统计：返回 {CJK 字符数, 拉丁词数}，用于估算阅读时长
std::pair<int,int> count_words(const std::string& text) {
    int cjk = 0, words = 0; bool in_word = false;
    for (size_t i = 0; i < text.size(); ) {
        unsigned char c = (unsigned char)text[i];
        int len = 1;
        if (c >= 0x80) {
            if      ((c >> 5) == 0x6)  len = 2;
            else if ((c >> 4) == 0xE)  len = 3;
            else if ((c >> 3) == 0x1E) len = 4;
            else { i++; continue; }
            if (len >= 3) cjk++;                 // 中日韩等表意文字按字符计
            i += len; in_word = false; continue;
        }
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            if (!in_word) { words++; in_word = true; }
        } else in_word = false;
        i++;
    }
    return { cjk, words };
}

// 定位当前可执行文件所在目录（用于 new 命令定位引擎 .Cdocs）
fs::path exe_dir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return fs::current_path();
    return fs::path(std::wstring(buf, n)).parent_path();
#else
    std::error_code ec;
    fs::path p = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) return p.parent_path();
    return fs::current_path();
#endif
}

// 判断进程是否由资源管理器（explorer.exe）双击启动：
// 双击 exe 时父进程是 explorer.exe；在终端里运行时父进程是 cmd/bash/pwsh 等。
// 用于在无参数双击时「等待回车退出」，避免控制台窗口一闪而过（纯 CLI，不做任何自动行为）。
bool launched_by_doubleclick() {
#ifdef _WIN32
    DWORD pid = GetCurrentProcessId();
    DWORD ppid = 0;
    std::map<DWORD, std::string> names;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32 pe; pe.dwSize = sizeof(pe);
    if (Process32First(snap, &pe)) {
        do {
            names[pe.th32ProcessID] = pe.szExeFile;
            if (pe.th32ProcessID == pid) ppid = pe.th32ParentProcessID;
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    if (ppid && names.count(ppid)) {
        std::string parent = names[ppid];
        size_t p = parent.find_last_of("\\/");
        if (p != std::string::npos) parent = parent.substr(p + 1);
        return parent == "explorer.exe";
    }
#endif
    return false;
}
