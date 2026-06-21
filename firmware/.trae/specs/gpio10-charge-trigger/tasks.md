# Tasks

- [x] Task 1: 更新 config.h 添加 GPIO10 定义
  - [x] SubTask 1.1: 添加 CHARGE_DETECT_GPIO 宏定义

- [x] Task 2: 更新 Application 类实现充电检测功能
  - [x] SubTask 2.1: 在 application.h 添加 GPIO 相关声明
  - [x] SubTask 2.2: 在 application.cpp 添加 GPIO 初始化函数
  - [x] SubTask 2.3: 实现 GPIO 中断处理回调函数
  - [x] SubTask 2.4: 在 init() 中调用 GPIO 初始化

- [x] Task 3: 验证功能
  - [x] SubTask 3.1: 检查代码无诊断错误

# Task Dependencies
- [Task 2] depends on [Task 1]
- [Task 3] depends on [Task 2]