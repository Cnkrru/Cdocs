// compress.hpp —— 构建期压缩（图片：stb_image 读 + libwebp 编 WebP，成熟开源库）
//
// 图片压缩策略（行业最佳）：把 docs/ 拷入的 PNG/JPEG/GIF/BMP/TGA 用 stb_image 解码、
// libwebp（Google 成熟编码器）编码为 WebP 副本（<原图去扩展名>.webp，原图保留作回退），
// 页面 <img> 自动升级为 <picture> 优先加载 WebP（现代浏览器 -25~35% 体积）。
// 代码压缩策略：HTML/CSS 构建期保守紧凑化（去注释 + 折叠多余空白，保护 <pre>/<script>/
// <style>/字符串等有语义区域），只减小体积、不改变任何渲染语义。
#pragma once

#include <filesystem>
#include <map>
#include <string>
namespace fs = std::filesystem;

// 为单个图片生成 WebP 副本（<p 去扩展名>.webp），仅当 WebP 更小时保留，否则删除。
// 返回生成的 WebP 路径（失败或无收益返回空路径）。非图片 / 太小 / 超限跳过。
fs::path webpize_file(const fs::path& p, int quality);

// 递归为目录下所有 png/jpg/jpeg/gif/bmp/tga 生成 WebP 副本（构建期对 docs 静态资源调用；
// 幂等：已存在且同参数时跳过）。返回生成的 WebP 数量。
int webpize_dir(const fs::path& dir, int quality);

// HTML <picture> 升级：页面 HTML 中 <img src="X"> 若同目录存在 X.webp，
// 包装为 <picture><source type="image/webp" srcset="X.webp"><img …></picture>。
// locOut 用于确认 .webp 真实存在（只升级有 WebP 副本的图，其余原样）。
std::string wrap_webp(const std::string& html, const fs::path& locOut);

// HTML 紧凑化：删除 <!-- --> 注释、把标签间/文本内连续空白折叠为单空格；
// <pre>/<textarea>/<script>/<style> 内容原样保留（其中空白有语义）。保守实现，不改变渲染结果。
std::string minify_html(const std::string& html);

// CSS 紧凑化：删除 /* */ 注释（保留 /*! 版权注释）、折叠空白（字符串值内不动）。
// 保守实现：只去注释与多余空白，不改任何属性值/选择器语义。
std::string minify_css(const std::string& css);

// 递归压缩目录下所有 .css（主题 css/ 目录用；已压缩文件幂等不变）。
void compress_dir_css(const fs::path& dir);

// ---- 资源指纹（cache busting，对标 Hugo resources / VitePress hashed assets）----
// 为目录下主题自己的 css/js 计算内容哈希（FNV-1a 64 位 → 8 位 hex），写入全局 g_fp
// （key = 相对路径如 "assets/css/style.css"；内容未变则哈希稳定，增量构建 URL 不变）。
void fingerprint_assets(const fs::path& assetsDir);

// 给 HTML 中引用的指纹资源追加 ?v=<hash>（href="/src=" 后跟闭合引号处）。
std::string apply_fingerprints(const std::string& html);

// JS 模块导入指纹：扫描 assets/js/ 中所有 .js 的 import 语句追加 ?v=hash
void fingerprint_js_imports(const fs::path& assetsDir);

// 输出管线融合：i18n_replace → minify_html → wrap_webp → apply_fingerprints
// 减少中间字符串分配（4 次拷贝 → 统一入口），compress=false 时跳过 minify+webp
std::string finalize_html(const std::string& raw, const std::map<std::string, std::string>& dict,
                          const fs::path& locOut, bool compress);
