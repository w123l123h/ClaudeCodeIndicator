import subprocess
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).parent
main_script = SCRIPT_DIR / "desktop-relay" / "main.py"

# 用 pythonw 调用 main.py（确保无窗口）
subprocess.Popen(
    [sys.executable, str(main_script)],
    creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0,
    start_new_session=True,
)