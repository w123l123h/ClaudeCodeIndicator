# Tasks

- [x] Task 1: 创建 LedStateManager 类头文件
  - [x] SubTask 1.1: 定义 LedState 内部结构体
  - [x] SubTask 1.2: 定义公共接口方法（init, apply_command, tick 等）
  - [x] SubTask 1.3: 定义回调设置方法
  - [x] SubTask 1.4: 定义状态查询方法

- [x] Task 2: 实现 LedStateManager 类
  - [x] SubTask 2.1: 实现初始化逻辑
  - [x] SubTask 2.2: 实现 LED 指令执行逻辑（apply_command）
  - [x] SubTask 2.3: 实现定时器回调处理
  - [x] SubTask 2.4: 实现闪烁逻辑（tick）
  - [x] SubTask 2.5: 实现配对处理方法
  - [x] SubTask 2.6: 实现休眠准备方法
  - [x] SubTask 2.7: 实现连接状态处理方法
  - [x] SubTask 2.8: 实现低电量处理方法

- [x] Task 3: 重构 main.cpp
  - [x] SubTask 3.1: 移除 LedState 结构体定义
  - [x] SubTask 3.2: 移除 g_led_states 全局变量
  - [x] SubTask 3.3: 移除相关的静态回调函数
  - [x] SubTask 3.4: 创建 LedStateManager 实例
  - [x] SubTask 3.5: 更新 app_main 使用新类
  - [x] SubTask 3.6: 更新 CMakeLists.txt 添加新源文件

- [x] Task 4: 验证功能
  - [x] SubTask 4.1: 编译项目确保无错误
  - [x] SubTask 4.2: 检查代码风格一致性

# Task Dependencies
- [Task 2] depends on [Task 1]
- [Task 3] depends on [Task 2]
- [Task 4] depends on [Task 3]