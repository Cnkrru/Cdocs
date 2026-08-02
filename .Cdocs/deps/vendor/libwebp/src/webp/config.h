/* src/webp/config.h —— Cdocs 最小构建配置（libwebp v1.4.0）
 * 仅启用 WebP 编解码核心；关闭外部图像库（GIF/JPEG/PNG/TIFF）与线程依赖。
 */
#define PACKAGE "libwebp"
#define PACKAGE_VERSION "1.4.0"
#define VERSION "1.4.0"

#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_WCHAR_H 1

#define WEBP_USE_THREAD 0

/* 未启用平台 SIMD 优化（默认 C 实现，保证 MinGW 可移植） */
/* #undef WEBP_USE_SSE2 */
