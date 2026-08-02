@echo off
REM Cdocs 一键构建：编译生成器（缺失/源码过期则编译）→ 生成静态站 → RSS → PWA
REM 用法：在项目根目录双击，或在根目录执行 .Cdocs\tools\build.cmd
cd /d "%~dp0\..\.."

REM 定位 C++ 工具链（g++/gcc）。优先用 PATH 中的；否则尝试本机常见 MinGW 安装。
where g++ >nul 2>nul || (
  if exist "D:\deps_code\C_C++\mingw64\bin" set "PATH=D:\deps_code\C_C++\mingw64\bin;%PATH%"
  if exist "C:\mingw64\bin" set "PATH=C:\mingw64\bin;%PATH%"
  if exist "C:\Program Files\mingw-w64\x86_64-*\bin" set "PATH=C:\Program Files\mingw-w64\x86_64-*\bin;%PATH%"
)

set "BIN=Cdocs.exe"
set "BUILD=.build"
set "INC=-I .Cdocs\deps\vendor -I .Cdocs\deps\vendor\md4c -I .Cdocs\deps\vendor\libwebp\src -I .Cdocs\deps\vendor\zlib"

REM ASCII 临时目录（规避「用户名含中文」导致 Windows TEMP 路径写入失败）
set "TEMP=%CD%\.build\tmp"
set "TMP=%CD%\.build\tmp"
if not exist "%BUILD%\tmp" mkdir "%BUILD%\tmp" 2>nul

REM 0) 编译（exe 缺失，或任一源文件比 exe 新）
set "NEED=0"
if not exist "%BIN%" (set "NEED=1") else (
  for %%F in ("%BIN%") do set "BT=%%~tF"
  for %%S in (src\main.cpp src\markdown.cpp ^
              src\core.cpp src\frontmatter.cpp ^
              src\i18n.cpp src\config.cpp ^
              src\pages.cpp src\feeds.cpp ^
              src\pwa.cpp src\search.cpp ^
              src\server.cpp src\builder.cpp ^
              src\plugin.cpp ^
              src\compress.cpp ^
              src\linkcheck.cpp ^
              src\deploy.cpp ^
              src\cli.cpp ^
              .Cdocs\deps\vendor\color.hpp ^
              .Cdocs\deps\vendor\stb_image.h ^
              .Cdocs\deps\vendor\libwebp\src\webp\config.h ^
              .Cdocs\deps\vendor\md4c\md4c.c ^
              .Cdocs\deps\vendor\md4c\md4c-html.c ^
              .Cdocs\deps\vendor\md4c\entity.c) do (
    if exist "%%S" if "%%~tS" GTR "%BT%" set "NEED=1"
  )
)
if "%NEED%"=="1" (
  echo [0/1] 编译生成器 %BIN% ...
  if not exist "%BUILD%" mkdir "%BUILD%"
  echo   - libwebp 静态库 ...
  call .Cdocs\tools\build_libwebp.cmd || goto :compile_fail
  echo   - zlib 静态库 ...
  call .Cdocs\tools\build_zlib.cmd || goto :compile_fail
  echo   - md4c 源 (gcc, C) ...
  gcc -c .Cdocs\deps\vendor\md4c\md4c.c %INC% -o %BUILD%\md4c.o    || goto :compile_fail
  gcc -c .Cdocs\deps\vendor\md4c\md4c-html.c %INC% -o %BUILD%\md4c-html.o || goto :compile_fail
  gcc -c .Cdocs\deps\vendor\md4c\entity.c %INC% -o %BUILD%\entity.o || goto :compile_fail
  echo   - 生成器源码 (g++, C++17) ...
  g++ -c src\core.cpp -std=c++17 %INC% -o %BUILD%\core.o        || goto :compile_fail
  g++ -c src\frontmatter.cpp -std=c++17 %INC% -o %BUILD%\frontmatter.o || goto :compile_fail
  g++ -c src\i18n.cpp -std=c++17 %INC% -o %BUILD%\i18n.o        || goto :compile_fail
  g++ -c src\config.cpp -std=c++17 %INC% -o %BUILD%\config.o     || goto :compile_fail
  g++ -c src\pages.cpp -std=c++17 %INC% -o %BUILD%\pages.o       || goto :compile_fail
  g++ -c src\feeds.cpp -std=c++17 %INC% -o %BUILD%\feeds.o       || goto :compile_fail
  g++ -c src\pwa.cpp -std=c++17 %INC% -o %BUILD%\pwa.o           || goto :compile_fail
  g++ -c src\search.cpp -std=c++17 %INC% -o %BUILD%\search.o     || goto :compile_fail
  g++ -c src\server.cpp -std=c++17 %INC% -o %BUILD%\server.o     || goto :compile_fail
  g++ -c src\builder.cpp -std=c++17 %INC% -o %BUILD%\builder.o   || goto :compile_fail
  g++ -c src\plugin.cpp -std=c++17 %INC% -o %BUILD%\plugin.o     || goto :compile_fail
  g++ -c src\compress.cpp -std=c++17 %INC% -o %BUILD%\compress.o  || goto :compile_fail
  g++ -c src\linkcheck.cpp -std=c++17 %INC% -o %BUILD%\linkcheck.o || goto :compile_fail
  g++ -c src\deploy.cpp -std=c++17 %INC% -o %BUILD%\deploy.o || goto :compile_fail
  g++ -c src\cli.cpp -std=c++17 %INC% -o %BUILD%\cli.o           || goto :compile_fail
  g++ -c src\main.cpp -std=c++17 %INC% -o %BUILD%\main.o         || goto :compile_fail
  g++ -c src\markdown.cpp -std=c++17 %INC% -o %BUILD%\markdown.o || goto :compile_fail
  echo   - 链接 ...
  g++ %BUILD%\md4c.o %BUILD%\md4c-html.o %BUILD%\entity.o ^
      %BUILD%\core.o %BUILD%\frontmatter.o %BUILD%\i18n.o %BUILD%\config.o ^
      %BUILD%\pages.o %BUILD%\feeds.o %BUILD%\pwa.o %BUILD%\search.o ^
      %BUILD%\server.o %BUILD%\builder.o %BUILD%\plugin.o %BUILD%\compress.o %BUILD%\linkcheck.o %BUILD%\deploy.o %BUILD%\cli.o %BUILD%\main.o %BUILD%\markdown.o ^
      %BUILD%\libwebp.a %BUILD%\libz.a ^
      -o %BIN% -static -static-libgcc -static-libstdc++ -lws2_32 || goto :compile_fail
  echo 编译完成。
)
goto :run

:compile_fail
echo 编译失败：请确认已安装 g++（MinGW-W64）并加入 PATH。
exit /b 1

:run
echo [1/1] 生成静态站点（Cdocs，内建 RSS / JSON Feed / PWA / SEO）...
call %BIN%
if errorlevel 1 (
  echo Cdocs 执行失败。
  exit /b 1
)

echo 完成。预览：python -m http.server 8088 --directory dist
