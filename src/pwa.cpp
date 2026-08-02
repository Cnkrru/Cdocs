// pwa.cpp —— PWA manifest / service worker 生成实现（自 main.cpp 原样搬迁）

#include "pwa.hpp"
#include "core.hpp"   // g_fp（资源指纹 map）

// PWA：复制 sw.js + icon.svg，并写 manifest.webmanifest（替代外部 gen-pwa.js）
void gen_pwa(const fs::path& out, const SiteConfig& cfg, const std::string& siteName,
                    const fs::path& assetsSrc) {
    std::error_code ec;
    for (const char* f : {"sw.js", "icon.svg"}) {
        fs::path src = assetsSrc / "pwa" / f;   // PWA 文件收口在 assets/pwa/
        if (fs::exists(src)) fs::copy(src, out / f, fs::copy_options::overwrite_existing, ec);
    }
    // 资源指纹同步：sw.js 的 CORE 缓存列表里主题 css/js 加 ?v=<hash>（与页面引用一致，
    // 内容变更时 URL 变化 → 缓存自动失效；内容未变则 URL 稳定不重复下载）
    {
        fs::path swf = out / "sw.js";
        if (fs::exists(swf, ec) && !g_fp.empty()) {
            std::string sw = read_file(swf);
            bool changed = false;
            for (const auto& kv : g_fp) {
                std::string needle = kv.first + "'";
                std::string repl   = kv.first + "?v=" + kv.second + "'";
                size_t pos = 0;
                while ((pos = sw.find(needle, pos)) != std::string::npos) {
                    sw.replace(pos, needle.size(), repl);
                    pos += repl.size();
                    changed = true;
                }
            }
            if (changed) std::ofstream(swf) << sw;
        }
    }
    json m = json::object();
    m["name"] = siteName.empty() ? std::string("Cdocs 文档") : siteName;
    m["short_name"] = "Cdocs";
    m["description"] = cfg.description;
    m["start_url"] = "./";
    m["scope"] = "./";
    m["display"] = "standalone";
    m["background_color"] = "#f5f1e8";
    m["theme_color"] = "#a8332a";
    json icon;
    icon["src"] = "./icon.svg";
    icon["sizes"] = "any";
    icon["type"] = "image/svg+xml";
    icon["purpose"] = "any";
    m["icons"] = json::array({ icon });
    std::ofstream(out / "manifest.webmanifest") << m.dump(2);
}
