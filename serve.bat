@echo off
chcp 65001 >nul 2>&1
cd /d "%~dp0"

rem 检查默认端口 8088 是否被占用（可能残留旧的预览服务）
netstat -ano 2>nul | findstr ":8088" | findstr "LISTENING" >nul
if not errorlevel 1 (
    echo [警告] 端口 8088 已被其他程序占用，Cdocs 预览服务器可能启动失败！
    echo   试试换个端口：serve.bat -p 8090
    echo.
)

echo 正在启动 Cdocs 预览服务器... （自动打开浏览器，按 Ctrl+C 停止）
Cdocs.exe serve -o %*
if errorlevel 1 pause
