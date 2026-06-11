@echo off
set SCRIPT_DIR=%~dp0
cd /d "%SCRIPT_DIR%"

REM 检查 TCP 54321 是否已被占用（单实例保护）
netstat -ano 2>nul | findstr ":54321.*LISTENING" >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo Another instance is already running (port 54321 in use).
    exit /b 0
)

REM 自动查找 Python（py 启动器 > python > python3）
py -3 "%SCRIPT_DIR%desktop-relay\main.py" 2>nul || python "%SCRIPT_DIR%desktop-relay\main.py" 2>nul || python3 "%SCRIPT_DIR%desktop-relay\main.py"
pause
