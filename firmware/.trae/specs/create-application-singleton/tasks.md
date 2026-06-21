# Tasks

- [x] Task 1: 创建 Application 类头文件
  - [x] SubTask 1.1: 定义单例模式（instance() 静态方法）
  - [x] SubTask 1.2: 定义组件成员变量（LedController、LedStateManager、BleServer、BatteryMonitor、PowerManager）
  - [x] SubTask 1.3: 定义生命周期方法（init、run）
  - [x] SubTask 1.4: 定义私有回调方法

- [x] Task 2: 实现 Application 类
  - [x] SubTask 2.1: 实现单例模式
  - [x] SubTask 2.2: 实现 init() 方法（NVS 初始化、组件初始化、自检序列）
  - [x] SubTask 2.3: 实现 run() 方法（BLE 启动、主循环）
  - [x] SubTask 2.4: 实现 BLE 消息处理回调
  - [x] SubTask 2.5: 实现 BLE 连接状态回调
  - [x] SubTask 2.6: 实现电源控制回调
  - [x] SubTask 2.7: 实现休眠回调
  - [x] SubTask 2.8: 实现电池低电量回调
  - [x] SubTask 2.9: 实现 JSON 解析逻辑

- [x] Task 3: 重构 main.cpp
  - [x] SubTask 3.1: 移除所有全局变量和静态函数
  - [x] SubTask 3.2: 简化为仅调用 Application::instance().run()
  - [x] SubTask 3.3: 更新 CMakeLists.txt 添加新源文件

- [x] Task 4: 验证功能
  - [x] SubTask 4.1: 检查代码无诊断错误
  - [x] SubTask 4.2: 检查代码风格一致性

# Task Dependencies
- [Task 2] depends on [Task 1]
- [Task 3] depends on [Task 2]
- [Task 4] depends on [Task 3]