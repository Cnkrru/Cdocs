@echo off
REM zlib 静态库构建（serve gzip 传输压缩用，v1.3.1 源码在 vendor/zlib）。
REM 由 build.cmd 调用；libz.a 缺失或源码更新时重新编译。
setlocal
set "ZP=.Cdocs\deps\vendor\zlib"
set "OUT=.build\libz.a"
set "OBJDIR=.build\zlib_obj"

set "NEED=0"
if not exist "%OUT%" (set "NEED=1") else (
  for %%F in ("%OUT%") do set "AT=%%~tF"
  for /f %%F in ('dir /s /b "%ZP%\*.c"') do (
    if exist "%%F" if "%%~tF" GTR "%AT%" set "NEED=1"
  )
)
if "%NEED%"=="0" exit /b 0

echo   - zlib 编译中（静态库 %OUT%）...
if not exist "%OBJDIR%" mkdir "%OBJDIR%"
for /f %%F in ('dir /s /b "%ZP%\*.c"') do (
  if not exist "%OBJDIR%\%%~nxF.o" (
    gcc -c "%%F" -I "%ZP%" -O2 -o "%OBJDIR%\%%~nxF.o" || exit /b 1
  )
)
ar rcs "%OUT%" %OBJDIR%\*.o || exit /b 1
exit /b 0
