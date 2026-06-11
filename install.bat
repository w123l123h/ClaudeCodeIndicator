@echo off
set SCRIPT_DIR=%~dp0
cd /d "%SCRIPT_DIR%"

REM 自动查找 Python（py 启动器 > python > python3）
py -3 "%SCRIPT_DIR%installer\install.py" 2>nul || python "%SCRIPT_DIR%installer\install.py" 2>nul || python3 "%SCRIPT_DIR%installer\install.py"
pause
