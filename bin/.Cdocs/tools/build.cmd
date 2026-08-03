@echo off
REM Cdocs 一键构建：编译生成器（缺失/源码过期则编译）→ 生成静态站 → RSS → PWA
REM 用法：在站点根（web/）执行 .Cdocs\tools\build.cmd，或双击 web\serve.bat
REM 结构：web/ 是站点根（.Cdocs/ + md/），引擎源码在上级目录 src/（git 仓库根）
cd /d "%~dp0\..\.."

REM 定位 C++ 工具链（g++/gcc）。优先用 PATH 中的；否则尝试本机常见 MinGW 安装。
where g++ >nul 2>nul || (
  if exist "D:\deps_code\C_C++\mingw64\bin" set "PATH=D:\deps_code\C_C++\mingw64\bin;%PATH%"
  if exist "C:\mingw64\bin" set "PATH=C:\mingw64\bin;%PATH%"
  if exist "C:\Program Files\mingw-w64\x86_64-*\bin" set "PATH=C:\Program Files\mingw-w64\x86_64-*\bin;%PATH%"
)

set "BIN=Cdocs.exe"
set "BUILD=.build"
set "SRC=..\src"
set "INC=-I .Cdocs\deps\vendor -I .Cdocs\deps\vendor\md4c -I .Cdocs\deps\vendor\libwebp\src -I .Cdocs\deps\vendor\zlib"

REM ASCII 临时目录（规避「用户名含中文」导致 Windows TEMP 路径写入失败）
set "TEMP=%CD%\.build\tmp"
set "TMP=%CD%\.build\tmp"
if not exist "%BUILD%\tmp" mkdir "%BUILD%\tmp" 2>nul

REM 0) 编译（exe 缺失，或任一源文件比 exe 新）
set "NEED=0"
if not exist "%BIN%" (set "NEED=1") else (
  for %%F in ("%BIN%") do set "BT=%%~tF"
  for %%S in ("%SRC%\main.cpp" "%SRC%\markdown.cpp" ^
              "%SRC%\core.cpp" "%SRC%\frontmatter.cpp" ^
              "%SRC%\i18n.cpp" "%SRC%\config.cpp" ^
              "%SRC%\pages.cpp" "%SRC%\feeds.cpp" ^
              "%SRC%\pwa.cpp" "%SRC%\search.cpp" ^
              "%SRC%\server.cpp" "%SRC%\builder.cpp" ^
              "%SRC%\plugin.cpp" "%SRC%\compress.cpp" ^
              "%SRC%\linkcheck.cpp" "%SRC%\deploy.cpp" ^
              "%SRC%\cli.cpp" "%SRC%\component.cpp" ^
              "%SRC%\shortcode.cpp" "%SRC%\scaffold.cpp" ^
              "%SRC%\diag.cpp" "%SRC%\versions.cpp" ^
              "%SRC%\output.cpp" "%SRC%\ctxdata.cpp" ^
              "%SRC%\site_config.cpp" "%SRC%\render_pages.cpp" ^
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
  g++ -c %SRC%\core.cpp -std=c++17 %INC% -o %BUILD%\core.o        || goto :compile_fail
  g++ -c %SRC%\frontmatter.cpp -std=c++17 %INC% -o %BUILD%\frontmatter.o || goto :compile_fail
  g++ -c %SRC%\i18n.cpp -std=c++17 %INC% -o %BUILD%\i18n.o        || goto :compile_fail
  g++ -c %SRC%\config.cpp -std=c++17 %INC% -o %BUILD%\config.o     || goto :compile_fail
  g++ -c %SRC%\pages.cpp -std=c++17 %INC% -o %BUILD%\pages.o       || goto :compile_fail
  g++ -c %SRC%\feeds.cpp -std=c++17 %INC% -o %BUILD%\feeds.o       || goto :compile_fail
  g++ -c %SRC%\pwa.cpp -std=c++17 %INC% -o %BUILD%\pwa.o           || goto :compile_fail
  g++ -c %SRC%\search.cpp -std=c++17 %INC% -o %BUILD%\search.o     || goto :compile_fail
  g++ -c %SRC%\server.cpp -std=c++17 %INC% -o %BUILD%\server.o     || goto :compile_fail
  g++ -c %SRC%\builder.cpp -std=c++17 %INC% -o %BUILD%\builder.o   || goto :compile_fail
  g++ -c %SRC%\plugin.cpp -std=c++17 %INC% -o %BUILD%\plugin.o     || goto :compile_fail
  g++ -c %SRC%\compress.cpp -std=c++17 %INC% -o %BUILD%\compress.o  || goto :compile_fail
  g++ -c %SRC%\linkcheck.cpp -std=c++17 %INC% -o %BUILD%\linkcheck.o || goto :compile_fail
  g++ -c %SRC%\deploy.cpp -std=c++17 %INC% -o %BUILD%\deploy.o || goto :compile_fail
  g++ -c %SRC%\cli.cpp -std=c++17 %INC% -o %BUILD%\cli.o           || goto :compile_fail
  g++ -c %SRC%\component.cpp -std=c++17 %INC% -o %BUILD%\component.o || goto :compile_fail
  g++ -c %SRC%\shortcode.cpp -std=c++17 %INC% -o %BUILD%\shortcode.o || goto :compile_fail
  g++ -c %SRC%\scaffold.cpp -std=c++17 %INC% -o %BUILD%\scaffold.o || goto :compile_fail
  g++ -c %SRC%\diag.cpp -std=c++17 %INC% -o %BUILD%\diag.o         || goto :compile_fail
  g++ -c %SRC%\versions.cpp -std=c++17 %INC% -o %BUILD%\versions.o || goto :compile_fail
  g++ -c %SRC%\output.cpp -std=c++17 %INC% -o %BUILD%\output.o     || goto :compile_fail
  g++ -c %SRC%\ctxdata.cpp -std=c++17 %INC% -o %BUILD%\ctxdata.o   || goto :compile_fail
  g++ -c %SRC%\site_config.cpp -std=c++17 %INC% -o %BUILD%\site_config.o || goto :compile_fail
  g++ -c %SRC%\render_pages.cpp -std=c++17 %INC% -o %BUILD%\render_pages.o || goto :compile_fail
  g++ -c %SRC%\main.cpp -std=c++17 %INC% -o %BUILD%\main.o         || goto :compile_fail
  g++ -c %SRC%\markdown.cpp -std=c++17 %INC% -o %BUILD%\markdown.o || goto :compile_fail
  echo   - 链接 ...
  g++ %BUILD%\md4c.o %BUILD%\md4c-html.o %BUILD%\entity.o ^
      %BUILD%\core.o %BUILD%\frontmatter.o %BUILD%\i18n.o %BUILD%\config.o ^
      %BUILD%\pages.o %BUILD%\feeds.o %BUILD%\pwa.o %BUILD%\search.o ^
      %BUILD%\server.o %BUILD%\builder.o %BUILD%\plugin.o %BUILD%\compress.o %BUILD%\linkcheck.o %BUILD%\deploy.o %BUILD%\cli.o ^
      %BUILD%\component.o %BUILD%\shortcode.o %BUILD%\scaffold.o %BUILD%\diag.o ^
      %BUILD%\versions.o %BUILD%\output.o %BUILD%\ctxdata.o %BUILD%\site_config.o %BUILD%\render_pages.o ^
      %BUILD%\main.o %BUILD%\markdown.o ^
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
call %BIN% build
if errorlevel 1 (
  echo Cdocs 执行失败。
  exit /b 1
)

echo 完成。预览：python -m http.server 8088 --directory dist
