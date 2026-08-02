@echo off
REM libwebp 静态库构建（Cdocs 最小配置：纯 WebP 编解码，无外部图像库/线程）。
REM 由 build.cmd 调用；libwebp.a 缺失或源码更新时重新编译。
setlocal
set "LP=.Cdocs\deps\vendor\libwebp"
set "OUT=.build\libwebp.a"
set "OBJDIR=.build\webp_obj"

set "NEED=0"
if not exist "%OUT%" (set "NEED=1") else (
  for %%F in ("%OUT%") do set "AT=%%~tF"
  for %%S in ("%LP%\src\webp\config.h") do (
    if exist "%%S" if "%%~tS" GTR "%AT%" set "NEED=1"
  )
  for /f %%F in ('dir /s /b "%LP%\src\*.c" "%LP%\sharpyuv\*.c"') do (
    if exist "%%F" if "%%~tF" GTR "%AT%" set "NEED=1"
  )
)
if "%NEED%"=="0" exit /b 0

echo   - libwebp 编译中（静态库 %OUT%）...
if not exist "%OBJDIR%" mkdir "%OBJDIR%"
for /f %%F in ('dir /s /b "%LP%\src\*.c" "%LP%\sharpyuv\*.c"') do (
  if not exist "%OBJDIR%\%%~nxF.o" (
    gcc -c "%%F" -I "%LP%" -O2 -o "%OBJDIR%\%%~nxF.o" || exit /b 1
  )
)
ar rcs "%OUT%" %OBJDIR%\*.o || exit /b 1
exit /b 0
