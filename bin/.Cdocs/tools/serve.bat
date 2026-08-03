@echo off
chcp 65001 >nul 2>&1
rem bin/serve.bat —— 全局预览启动器（bin/ 已加入 PATH）
rem 用法：在任意站点根目录（含 .Cdocs/ + md/ 的目录）运行：
rem     serve            （PATH 内直接调用）
rem     bin\serve.bat    （相对路径）
rem 或双击本文件（会在当前所在站点目录启动预览）
rem 注意：不切换目录 —— 预览的就是你运行时的所在目录（Cdocs 相对 CWD 解析站点）

set "CDOCS=%~dp0Cdocs.exe"
if not exist "%CDOCS%" (
    echo [错误] 未找到 %CDOCS%，请确认 bin\serve.bat 与 Cdocs.exe 在同一目录。
    pause
    exit /b 1
)
if not exist ".Cdocs\config" (
    echo [警告] 当前目录不是站点根（缺少 .Cdocs\config），预览可能失败。
    echo   请先 cd 到站点目录（含 .Cdocs\ 与 md\）再运行 serve。
    echo.
)

rem 检查默认端口 8088 是否被占用（可能残留旧的预览服务）
netstat -ano 2>nul | findstr ":8088" | findstr "LISTENING" >nul
if not errorlevel 1 (
    echo [警告] 端口 8088 已被其他程序占用，Cdocs 预览服务器可能启动失败！
    echo   试试换个端口：serve -p 8090
    echo.
)

echo 正在启动 Cdocs 预览服务器... （自动打开浏览器，按 Ctrl+C 停止）
"%CDOCS%" serve -o %*
if errorlevel 1 pause
