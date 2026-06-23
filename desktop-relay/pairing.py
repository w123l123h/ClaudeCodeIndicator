import tkinter as tk
from tkinter import messagebox


def show_pairing_dialog(device_name, name):
    root = tk.Tk()
    root.withdraw()

    result = messagebox.askyesno(
        "Claude Code Indicator — 配对确认",
        f"已连接到设备: {device_name}, {name}\n\n"
        "请确认指示器上 3 个 LED 是否都亮绿色？\n\n"
        "选择「是」完成配对\n选择「否」取消"
    )

    root.destroy()
    return result
