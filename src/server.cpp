// server.cpp —— HTTP 预览服务器 + 文件监听（自 main.cpp 原样搬迁）

#include "server.hpp"
#include "builder.hpp"   // run_build
#include <zlib.h>        // gzip 传输压缩（成熟库，静态链接）
#include <atomic>
#include <cstdio>

// 构建纪元号：watch 每重建一次 +1；浏览器轮询 /__cdocs_epoch 感知变化后自动刷新。
// 仅 serve -w 时递增；普通请求返回 0（不触发刷新逻辑）。
volatile std::atomic<int> g_build_epoch{0};
// watch 模式标志：handle_conn（子线程）据此决定是否注入自动刷新脚本
static bool watch_mode = false;

// 自动刷新脚本（注入 HTML 响应）：轮询 /__cdocs_epoch，变化则 location.reload()
static const char* kLiveReload = R"CDOCS(<script data-cdocs-livereload>!function(){var e=null,n=function(){try{fetch('/__cdocs_epoch').then(function(r){return r.text()}).then(function(t){if(e&&e!==t)location.reload();e=t;setTimeout(n,600)}).catch(function(){setTimeout(n,1500)})}catch(x){setTimeout(n,1500)}};n()}();</script>)CDOCS";

// 按扩展名猜测 Content-Type（预览服务器用）
static std::string content_type(const fs::path& p) {
    std::string e = p.extension().string();
    for (auto& c : e) c = (char)std::tolower((unsigned char)c);
    if (e == ".html" || e == ".htm")  return "text/html; charset=utf-8";
    if (e == ".css")                  return "text/css; charset=utf-8";
    if (e == ".js"  || e == ".mjs")   return "text/javascript; charset=utf-8";
    if (e == ".json")                 return "application/json; charset=utf-8";
    if (e == ".xml")                  return "application/xml; charset=utf-8";
    if (e == ".svg")                  return "image/svg+xml";
    if (e == ".png")                  return "image/png";
    if (e == ".jpg" || e == ".jpeg")  return "image/jpeg";
    if (e == ".gif")                  return "image/gif";
    if (e == ".webp")                 return "image/webp";
    if (e == ".ico")                  return "image/x-icon";
    if (e == ".woff2")                return "font/woff2";
    if (e == ".woff")                 return "font/woff";
    if (e == ".ttf")                  return "font/ttf";
    if (e == ".txt" || e == ".md")    return "text/plain; charset=utf-8";
    if (e == ".webmanifest")          return "application/manifest+json; charset=utf-8";
    return "application/octet-stream";
}

static std::string url_decode(const std::string& s) {
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::string r; r.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int h = hex(s[i+1]), l = hex(s[i+2]);
            if (h >= 0 && l >= 0) { r += (char)((h << 4) | l); i += 2; continue; }
        }
        r += (s[i] == '+') ? ' ' : s[i];
    }
    return r;
}

// 前向声明：cmd_serve 用到（定义在文件后面）
static std::time_t max_mtime(const fs::path& dir);
static void open_browser(const std::string& url);

// gzip 压缩（RFC 1952 gzip 格式，deflateInit2 windowBits=15+16）；返回是否成功
static bool gzip_compress(const std::string& in, std::string& out) {
    z_stream zs{};
    zs.next_in  = (Bytef*)const_cast<char*>(in.data());
    zs.avail_in = (uInt)in.size();
    if (deflateInit2(&zs, 6, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) return false;
    out.resize(compressBound(in.size()));
    zs.next_out  = (Bytef*)&out[0];
    zs.avail_out = (uInt)out.size();
    int r = deflate(&zs, Z_FINISH);
    bool ok = (r == Z_STREAM_END);
    out.resize(zs.total_out);
    deflateEnd(&zs);
    return ok;
}

// 请求头里查找字段（如 "Accept-Encoding:"）并判断是否包含目标值（逗号分隔列表）
static bool header_has(const std::string& req, const char* field, const char* value) {
    std::string key = std::string(field) + ":";
    size_t p = req.find(key);
    if (p == std::string::npos) return false;
    size_t e = req.find("\r\n", p);
    std::string v = req.substr(p + key.size(), (e == std::string::npos) ? std::string::npos : e - p - key.size());
    // 精确匹配或逗号列表包含（忽略大小写与空白；不处理 q=0 等权重细节）
    for (auto& c : v) c = (char)std::tolower((unsigned char)c);
    std::string target = value;
    for (auto& c : target) c = (char)std::tolower((unsigned char)c);
    if (v.find(target) == std::string::npos) return false;
    return true;
}

// 处理单个 HTTP 连接：解析请求行 → 定位文件 → 返回 200/404
static void handle_conn(sock_t c, fs::path root) {
    try {
    char buf[8192];
    int n = recv(c, buf, sizeof(buf) - 1, 0);
    if (n <= 0) return;                 // 客户端断开：外层统一关 socket
    buf[n] = 0;
    std::string req(buf, n);
    std::string method, target;
    { std::istringstream ls(req); ls >> method >> target; }
    if (target.empty()) target = "/";
    auto qpos = target.find_first_of("?#");
    if (qpos != std::string::npos) target = target.substr(0, qpos);
    std::string path = url_decode(target);
    if (path.empty() || path.back() == '/') path += "index.html";
    while (!path.empty() && path.front() == '/') path.erase(0, 1);

    fs::path fp = root / path;
    std::error_code ec;
    fs::path canonRoot = fs::weakly_canonical(root, ec);
    fs::path canon     = fs::weakly_canonical(fp, ec);
    bool inside = !ec && canon.string().rfind(canonRoot.string(), 0) == 0;  // 防目录穿越

    std::string status = "200 OK", ctype, body;
    bool isHtml = false;

    // 实时刷新端点：返回当前构建纪元号（watch 重建后递增，浏览器据此自动刷新）
    if (path == "__cdocs_epoch") {
        body  = std::to_string(g_build_epoch.load());
        ctype = "text/plain; charset=utf-8";
    } else if (!inside || !fs::exists(fp, ec) || fs::is_directory(fp, ec)) {
        status = "404 Not Found";
        fs::path nf = root / "404.html";
        if (fs::exists(nf, ec)) { body = read_file(nf); ctype = "text/html; charset=utf-8"; isHtml = true; }
        else { body = "404 Not Found"; ctype = "text/plain; charset=utf-8"; }
    } else {
        body = read_file(fp);
        ctype = content_type(fp);
        isHtml = (ctype.find("text/html") == 0);
        // watch 模式：HTML 响应注入自动刷新脚本（改内存副本，不动 dist 文件）
        if (watch_mode && isHtml && body.find("</body>") != std::string::npos) {
            body.insert(body.find("</body>"), kLiveReload);
        }
    }

    // ---- 传输层（行业标准）：ETag + Cache-Control 分级 + gzip ----
    std::string etag;
    std::string cacheCtl = "no-cache";
    if (status == "200 OK") {
        // 带内容哈希指纹的资源（assets/ 下，URL 内容变则变）→ 长缓存；HTML 等 no-cache
        bool isAsset = (path.compare(0, 7, "assets/") == 0 || path.find("/assets/") != std::string::npos);
        std::string fn = fp.filename().string();
        if (isAsset || fn == "sw.js" || fn == "manifest.webmanifest" || fn == "icon.svg")
            cacheCtl = "public, max-age=31536000, immutable";
        struct stat st;
        if (stat(fp.string().c_str(), &st) == 0) {
            char eb[64];
            std::snprintf(eb, sizeof(eb), "\"%llx-%llx\"",
                          (unsigned long long)st.st_mtime, (unsigned long long)st.st_size);
            etag = eb;
            // 条件请求：If-None-Match 命中 → 304
            if (header_has(req, "If-None-Match", etag.c_str())) {
                std::ostringstream h304;
                h304 << "HTTP/1.1 304 Not Modified\r\n"
                     << "ETag: " << etag << "\r\n"
                     << "Cache-Control: " << cacheCtl << "\r\n"
                     << "Connection: close\r\n\r\n";
                std::string s = h304.str();
                send(c, s.data(), (int)s.size(), 0);
                if (std::cout) std::cout << "  " << color::cyan(method) << " " << target
                                         << color::muted(" -> ") << color::wrap(color::CYAN, "304") << "\n";
                return;
            }
        }
        // gzip：浏览器声明支持、文本类资源、且大于 1KB 才压缩（避免小文件无收益）
        bool textLike = (ctype.find("text/") == 0 || ctype.find("application/json") == 0 ||
                         ctype.find("javascript") != std::string::npos ||
                         ctype.find("image/svg") == 0 || ctype.find("manifest+json") != std::string::npos);
        if (textLike && body.size() > 1024 && header_has(req, "Accept-Encoding", "gzip")) {
            std::string gz;
            if (gzip_compress(body, gz) && gz.size() < body.size()) { body = gz; ctype += "\r\nContent-Encoding: gzip"; }
        }
    }

    std::ostringstream head;
    head << "HTTP/1.1 " << status << "\r\n"
         << "Content-Type: " << ctype << "\r\n"
         << "Content-Length: " << body.size() << "\r\n"
         << "Cache-Control: " << cacheCtl << "\r\n";
    if (!etag.empty()) head << "ETag: " << etag << "\r\n";
    if (ctype.find("Content-Encoding") != std::string::npos) head << "Vary: Accept-Encoding\r\n";
    head << "Connection: close\r\n\r\n";
    std::string h = head.str();
    send(c, h.data(), (int)h.size(), 0);
    size_t off = 0;
    while (off < body.size()) {
        int chunk = (int)std::min<size_t>(body.size() - off, 65536);
        int s = send(c, body.data() + off, chunk, 0);
        if (s <= 0) break;
        off += (size_t)s;
    }
    color::Code stc = (status[0] == '2') ? color::GREEN
                     : (status[0] == '3') ? color::CYAN : color::RED;
    if (std::cout) {   // stdout 管道断开后不再尝试写入（SIGPIPE 已忽略）
        std::cout << "  " << color::cyan(method) << " " << target << color::muted(" -> ")
                  << color::wrap(stc, status) << "\n";
    }
    } catch (const std::exception& e) {
        if (std::cout) std::cout << color::muted("  [连接异常] ") << e.what() << "\n";
    } catch (...) {
        if (std::cout) std::cout << color::muted("  [连接异常] 未知错误\n");
    }
    CDOCS_CLOSESOCK(c);   // 无论成败，确保本连接 socket 关闭
}

// serve：启动本地静态预览服务器（仅监听 127.0.0.1）
//   in    —— 文档源目录（--watch 热重载时重新构建的输入）
//   build —— 启动前是否先构建（serve 默认会；--no-build 跳过）
//   watch —— 轮询 docs/ 与 .Cdocs/config，变化即自动重新构建
//   open  —— 启动后调用系统默认浏览器打开预览地址
int cmd_serve(fs::path root, fs::path in, int port, bool build, bool watch, bool open) {
    if (build) {
        int rc = run_build(in, root, false, false);
        if (rc != 0) return rc;
    } else if (!fs::exists(root)) {
        std::cerr << color::error("预览目录不存在: ") << root
                  << color::error("（先 build，或去掉 --no-build）\n");
        return 1;
    }
#ifdef _WIN32
    WSADATA w;
    if (WSAStartup(MAKEWORD(2, 2), &w) != 0) { std::cerr << color::error("WSAStartup 失败\n"); return 1; }
#endif
    sock_t s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) { std::cerr << color::error("socket 创建失败\n"); return 1; }
    // Windows 用 SO_EXCLUSIVEADDRUSE 独占端口，使“端口被占用”时 bind 真正失败、
    // 进而触发下方自动顺延；*nix 用 SO_REUSEADDR 以容忍 TIME_WAIT。
#ifdef _WIN32
    int excl = 1;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&excl, sizeof(excl));
#else
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((unsigned short)port);

    // 端口占用时自动顺延（最多 20 个），避免“端口被占 → 直接退出”
    int used_port = port;
    bool bound = false;
    for (int attempt = 0; attempt < 20; ++attempt) {
        addr.sin_port = htons((unsigned short)used_port);
        if (bind(s, (sockaddr*)&addr, sizeof(addr)) == 0) { bound = true; break; }
        ++used_port;
    }
    if (!bound) {
        std::cerr << color::error("端口 " + std::to_string(port) + " 及后续 20 个端口均绑定失败（可能被占用或无权限）\n");
        CDOCS_CLOSESOCK(s);
        return 1;
    }
    if (used_port != port) {
        std::cout << color::warn("端口 " + std::to_string(port) + " 被占用，自动改用 ")
                  << color::cyan(std::to_string(used_port)) << "\n";
        port = used_port;
    }
    if (listen(s, 32) != 0) { std::cerr << color::error("listen 失败\n"); CDOCS_CLOSESOCK(s); return 1; }

    // 只认人工信号：忽略 SIGPIPE（管道/客户端断开不杀死服务器），
    // 注册 SIGINT/SIGTERM 处理器（Ctrl+C → 置位 g_serve_running → 优雅退出）。
#ifndef _WIN32
    std::signal(SIGPIPE, SIG_IGN);
#endif
    std::signal(SIGINT, serve_signal_handler);
    std::signal(SIGTERM, serve_signal_handler);

    std::cout << std::unitbuf;   // 服务器输出实时刷新（横幅/请求日志即时可见）
    std::cout << "\n" << color::green("Cdocs 预览服务器已启动") << color::muted(" → ")
              << color::underline(color::cyan("http://localhost:" + std::to_string(port) + "/")) << "\n"
              << color::muted("  根目录: ") << root << "\n"
              << (watch ? color::muted("  热重载: 开（改动 docs/ 或 .Cdocs/config 自动重建）\n")
                        : color::muted("  "))
              << color::muted("  按 Ctrl+C 停止（仅人工可退出）。\n\n");

    if (open) open_browser("http://localhost:" + std::to_string(port) + "/");

    watch_mode = watch;   // 子线程注入自动刷新脚本的依据

    // 记录初始输入时间戳；--watch 时据此判断是否需要重建
    std::time_t last_mtime = 0;
    if (build) last_mtime = std::max(max_mtime(in), max_mtime(g_engine / "config"));

    // 用带超时的 select 轮询 accept：既保持常驻，又能在 0.2s 内响应退出信号；
    // 不会因阻塞在 accept 上而收不到 Ctrl+C。
    while (g_serve_running) {
        if (watch) {
            std::time_t now = std::max(max_mtime(in), max_mtime(g_engine / "config"));
            if (now > last_mtime) {
                last_mtime = now;
                std::cout << color::cyan("[watch] ") << "检测到文档变化，重新构建…\n";
                g_incremental = true;              // 增量构建：未变页面跳过渲染
                int rc = run_build(in, root, false, false);
                g_incremental = false;
                ++g_build_epoch;                   // 通知浏览器自动刷新
                (void)rc;
            }
        }
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(s, &rfds);
        struct timeval tv;
        tv.tv_sec = 0; tv.tv_usec = 200000;   // 0.2s 轮询
        int r = ::select((int)s + 1, &rfds, nullptr, nullptr, &tv);
        if (!g_serve_running) break;          // 信号已置位，跳出
        if (r > 0) {
            sock_t c = accept(s, nullptr, nullptr);
            if (c == INVALID_SOCKET) continue; // 被信号中断则下一轮重查标志
            std::thread(handle_conn, c, root).detach();
        }
        // r == 0（超时）或 r < 0（select 错误）：继续循环，重查退出标志
    }

    // 优雅退出：关闭监听套接字、释放 Winsock，打印提示
    std::cout << "\n" << color::warn("收到停止信号，正在关闭预览服务器…\n");
    CDOCS_CLOSESOCK(s);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}

// ============ 辅助：文件变化检测 / 浏览器唤起 ============

// 取目录内（含子目录）最新修改时间（秒），用于 --watch 热重载判定。
// 注意：本平台 std::filesystem::last_write_time 取不到预期值，这里改用 POSIX stat 的
// st_mtime（与 format_mtime 一致，已验证可靠）；遍历用 error_code 版 increment，
// 避免个别文件无权限时抛 filesystem_error 导致整次返回 0。
static std::time_t max_mtime(const fs::path& dir) {
    std::error_code ec;
    std::time_t m = 0;
    if (!fs::exists(dir, ec)) return m;
    struct stat st;
    for (auto it = fs::recursive_directory_iterator(dir, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (stat(it->path().string().c_str(), &st) == 0 && st.st_mtime > m)
            m = st.st_mtime;
    }
    return m;
}

// 尝试用系统默认浏览器打开 URL（best-effort，失败静默忽略）
static void open_browser(const std::string& url) {
#ifdef _WIN32
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    const char* ops[] = { "xdg-open", "open", "start" };
    for (const char* op : ops) {
        std::string cmd = std::string(op) + " '" + url + "' >/dev/null 2>&1";
        if (std::system(cmd.c_str()) == 0) break;
    }
#endif
}
