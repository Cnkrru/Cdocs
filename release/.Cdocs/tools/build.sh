#!/usr/bin/env bash
# Cdocs 一键构建：编译生成器（缺失/源码过期则编译）→ 生成静态站（内建 RSS / JSON Feed / PWA / SEO）
# 用法：在项目根目录执行 bash .Cdocs/tools/build.sh
set -e
cd "$(dirname "$0")/../.."

BIN=Cdocs
BUILD=.build
INC="-I .Cdocs/deps/vendor -I .Cdocs/deps/vendor/md4c -I .Cdocs/deps/vendor/libwebp/src -I .Cdocs/deps/vendor/zlib"

# 0) 编译（二进制缺失，或任一源文件比二进制新）
NEED=0
if [ ! -x "$BIN" ]; then NEED=1; else
  for s in src/main.cpp src/markdown.cpp \
           src/core.cpp src/frontmatter.cpp src/i18n.cpp src/config.cpp \
           src/pages.cpp src/feeds.cpp src/pwa.cpp src/search.cpp \
           src/server.cpp src/builder.cpp src/plugin.cpp \
           src/compress.cpp src/linkcheck.cpp src/deploy.cpp src/cli.cpp \
           .Cdocs/deps/vendor/color.hpp \
           .Cdocs/deps/vendor/stb_image.h \
           .Cdocs/deps/vendor/libwebp/src/webp/config.h \
           .Cdocs/deps/vendor/md4c/md4c.c \
           .Cdocs/deps/vendor/md4c/md4c-html.c \
           .Cdocs/deps/vendor/md4c/entity.c; do
    [ -f "$s" ] && [ "$s" -nt "$BIN" ] && NEED=1
  done
fi

# libwebp 静态库（vendor 源码编译，含 sharpyuv 子模块；产物比源码旧时重编）
build_libwebp() {
  local OUT=.build/libwebp.a
  local NEWER=""
  for f in $(find .Cdocs/deps/vendor/libwebp/src .Cdocs/deps/vendor/libwebp/sharpyuv -name '*.c' 2>/dev/null); do
    [ "$f" -nt "$OUT" ] && NEWER=1
  done
  [ -z "$NEWER" ] && [ -f "$OUT" ] && return 0
  echo "  - libwebp 静态库 ..."
  mkdir -p .build/webp_obj
  for f in $(find .Cdocs/deps/vendor/libwebp/src .Cdocs/deps/vendor/libwebp/sharpyuv -name '*.c' 2>/dev/null); do
    o=".build/webp_obj/$(basename "$f" .c).o"
    [ -f "$o" ] || gcc -c "$f" -I .Cdocs/deps/vendor/libwebp -O2 -o "$o"
  done
  ar rcs "$OUT" .build/webp_obj/*.o
}

# zlib 静态库（serve gzip 传输压缩用）
build_zlib() {
  local OUT=.build/libz.a
  local NEWER=""
  for f in .Cdocs/deps/vendor/zlib/*.c; do
    [ "$f" -nt "$OUT" ] && NEWER=1
  done
  [ -z "$NEWER" ] && [ -f "$OUT" ] && return 0
  echo "  - zlib 静态库 ..."
  mkdir -p .build/zlib_obj
  for f in .Cdocs/deps/vendor/zlib/*.c; do
    o=".build/zlib_obj/$(basename "$f" .c).o"
    [ -f "$o" ] || gcc -c "$f" -I .Cdocs/deps/vendor/zlib -O2 -o "$o"
  done
  ar rcs "$OUT" .build/zlib_obj/*.o
}

if [ "$NEED" -eq 1 ]; then
  echo "[0/1] 编译生成器 $BIN ..."
  mkdir -p "$BUILD"
  build_libwebp
  build_zlib
  echo "  - md4c 源 (gcc, C) ..."
  gcc -c .Cdocs/deps/vendor/md4c/md4c.c      $INC -o "$BUILD/md4c.o"
  gcc -c .Cdocs/deps/vendor/md4c/md4c-html.c $INC -o "$BUILD/md4c-html.o"
  gcc -c .Cdocs/deps/vendor/md4c/entity.c    $INC -o "$BUILD/entity.o"
  echo "  - 生成器源码 (g++, C++17) ..."
  for f in core frontmatter i18n config pages feeds pwa search server builder plugin compress linkcheck deploy cli main markdown; do
    g++ -c src/$f.cpp -std=c++17 $INC -o "$BUILD/$f.o"
  done
  echo "  - 链接 ..."
  # Windows(MinGW) 需链接 ws2_32(winsock)；Linux/macOS 用 pthread
  case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) NETLIB="-lws2_32" ;;
    *)                    NETLIB="-pthread" ;;
  esac
  g++ "$BUILD"/md4c.o "$BUILD"/md4c-html.o "$BUILD"/entity.o \
      "$BUILD"/core.o "$BUILD"/frontmatter.o "$BUILD"/i18n.o "$BUILD"/config.o \
      "$BUILD"/pages.o "$BUILD"/feeds.o "$BUILD"/pwa.o "$BUILD"/search.o \
      "$BUILD"/server.o "$BUILD"/builder.o "$BUILD"/plugin.o "$BUILD"/compress.o \
      "$BUILD"/linkcheck.o "$BUILD"/deploy.o "$BUILD"/cli.o "$BUILD"/main.o "$BUILD"/markdown.o \
      .build/libwebp.a .build/libz.a \
      -o "$BIN" -static -static-libgcc -static-libstdc++ $NETLIB
  echo "编译完成。"
fi

echo "[1/1] 生成静态站点（Cdocs，内建 RSS / JSON Feed / PWA / SEO）..."
./"$BIN"

echo "完成。预览：python3 -m http.server 8088 --directory dist"
