// versions.cpp —— 多版本构建分派
// （自 builder.cpp 拆分：版本探测 + 独立构建循环 + 根重定向）
// 版本化文档（Docusaurus 风格）：config.site.versions = [{name,label,source,default}]，
// source 为空用主 md/；每个版本独立构建到 dist/<name>/，根 index.html 重定向默认版本。
// 约定优于配置：config 未声明 versions 时自动扫描 in_dir 同级 "<源目录名>-*" 快照目录
// （md/ 旁 md-v1/ md-v2/…），md/ 恒为 current（默认版）。

#include "versions.hpp"
#include "builder.hpp"   // run_build（子版本递归构建）
#include <fstream>
#include <algorithm>

namespace {

// 重入锁：仅最外层调用执行版本循环（子版本构建不再分派，避免无限递归）
bool s_version_dispatching = false;

// 从 config.json 探测显式版本列表；未配置返回空
std::vector<VersionCfg> detect_config_versions() {
    std::vector<VersionCfg> vers;
    fs::path cfgp = g_engine / "config/config.json";
    if (!fs::exists(cfgp)) return vers;
    try {
        json j = json::parse(read_file(cfgp));
        json site = j.contains("site") && j["site"].is_object() ? j["site"] : j;
        if (site.contains("versions") && site["versions"].is_array()) {
            for (auto& v : site["versions"]) {
                if (!v.is_object() || !v.contains("name")) continue;
                VersionCfg vc;
                vc.name = v["name"].get<std::string>();
                vc.label = v.value("label", vc.name);
                vc.source = v.value("source", std::string());
                vc.default_v = v.value("default", false);
                vers.push_back(std::move(vc));
            }
            if (!vers.empty() && !vers[0].default_v) {
                bool any = false;
                for (auto& v : vers) if (v.default_v) { any = true; break; }
                if (!any) vers[0].default_v = true;
            }
        }
    } catch (...) { vers.clear(); }
    return vers;
}

// 约定优于配置：扫描 in_dir 同级 "<源目录名>-*" 快照目录识别历史版本
std::vector<VersionCfg> detect_snapshot_versions(fs::path in_dir) {
    std::vector<VersionCfg> vers;
    std::string base = in_dir.filename().string();   // 如 "md"
    fs::path parent = in_dir.parent_path();          // in_dir 可能为相对路径 → 用 "." 表示项目根
    if (parent.empty()) parent = fs::path(".");
    std::error_code sec;
    if (!fs::exists(parent, sec) || !fs::is_directory(parent, sec)) return vers;
    std::vector<std::string> snapshots;
    for (auto& e : fs::directory_iterator(parent, sec)) {
        if (!e.is_directory(sec)) continue;
        std::string name = e.path().filename().string();
        // 匹配 "<base>-<suffix>"，排除 .Cdocs/.build/dist 等隐藏/产物目录
        if (name.size() > base.size() + 1 && name.compare(0, base.size(), base) == 0
            && name[base.size()] == '-' && name[0] != '.')
            snapshots.push_back(name);
    }
    if (snapshots.empty()) return vers;
    // current 恒为首位 + 默认；历史版本按名排序（md-v1 < md-v2 < …）
    std::sort(snapshots.begin(), snapshots.end());
    VersionCfg cur;
    cur.name = "current";
    cur.label = "最新";
    cur.default_v = true;
    vers.push_back(std::move(cur));
    for (auto& s : snapshots) {
        VersionCfg vc;
        vc.name = s.substr(base.size() + 1);   // md-v1 → v1
        vc.label = vc.name;                    // label 默认即版本名
        vc.source = s;
        vers.push_back(std::move(vc));
    }
    if (!g_quiet) {
        std::cout << color::muted("  [versions] 自动识别 ") << snapshots.size()
                  << " 个历史版本: ";
        for (auto& v : vers) std::cout << color::cyan(v.name) << " ";
        std::cout << "\n";
    }
    return vers;
}

}  // namespace

bool dispatch_versions(fs::path in_dir, fs::path out_dir, bool includeDrafts, bool cleanBefore) {
    if (s_version_dispatching) return false;   // 子构建：不再分派，走单版本主流程

    s_version_dispatching = true;
    std::vector<VersionCfg> vers = detect_config_versions();
    if (vers.empty()) vers = detect_snapshot_versions(in_dir);

    if (!vers.empty()) {
        // 多版本模式：dist 整体重建（每个版本独立子目录）
        if (cleanBefore) {
            std::error_code ec2;
            fs::remove_all(out_dir, ec2);
        }
        std::string defName;
        for (const auto& v : vers) if (v.default_v) defName = v.name;

        // 把完整版本列表序列化传给子构建（供 header 版本下拉）
        {
            json va = json::array();
            for (const auto& v : vers) {
                va.push_back({{"name", v.name}, {"label", v.label},
                              {"source", v.source}, {"default", v.default_v}});
            }
            g_versions_json = va.dump();
        }

        for (const auto& v : vers) {
            // 版本源目录：source 为空用主 in_dir（md）；否则取 in_dir 同级下的 <source>
            fs::path parent = in_dir.parent_path();
            if (parent.empty()) parent = fs::path(".");
            fs::path vIn = in_dir;
            if (!v.source.empty() && v.source != in_dir.filename().string())
                vIn = parent / v.source;
            fs::path vOut = out_dir / v.name;
            if (!g_quiet)
                std::cout << color::bold(color::cyan("\n=== 构建版本 "))
                          << color::cyan(v.label) << color::bold(color::cyan(" → ")) << vOut << "\n";
            // 版本信息通过全局传给子构建：内部 load_site_config 读取
            g_cur_version = v.name;
            g_cur_version_label = v.label;
            int rc = run_build(vIn, vOut, includeDrafts, false);
            g_cur_version.clear();
            g_cur_version_label.clear();
            if (rc) { g_versions_json.clear(); s_version_dispatching = false; return true; }
        }
        g_versions_json.clear();
        // 根 index.html：重定向到默认版本（Docusaurus 同款行为）
        if (!defName.empty()) {
            std::ofstream(out_dir / "index.html")
                << "<!DOCTYPE html>\n<html lang=\"zh-CN\">\n<head>\n"
                << "  <meta charset=\"utf-8\">\n  <title>Redirecting…</title>\n"
                << "  <meta http-equiv=\"refresh\" content=\"0; url=./" << esc_attr(defName) << "/index.html\">\n"
                << "  <link rel=\"canonical\" href=\"./" << esc_attr(defName) << "/index.html\">\n"
                << "</head>\n<body>\n  <p>正在跳转到默认版本 <a href=\"./"
                << esc_attr(defName) << "/index.html\">" << esc(defName) << "</a> …</p>\n</body>\n</html>\n";
        }
        s_version_dispatching = false;
        return true;   // 多版本已处理
    }
    s_version_dispatching = false;
    return false;      // 单版本：调用方走主流程
}
