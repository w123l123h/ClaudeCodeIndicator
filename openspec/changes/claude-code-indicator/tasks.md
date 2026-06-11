## 1. ESP32 固件项目搭建

- [ ] 1.1 创建 ESP-IDF 项目结构（CMakeLists.txt, main/, config.h），定义所有参数宏
- [ ] 1.2 实现 led_controller（led_strip 驱动 WS2812B，set_led/all_off/all_on）
- [ ] 1.3 实现启动自检序列（依次点亮红/橙/绿 LED，间隔 200ms，最后全灭）

## 2. ESP32 BLE 与通信

- [ ] 2.1 初始化 NimBLE，创建 GATT Service + Characteristic（Write + Notify）
- [ ] 2.2 实现 BLE 广播（名称格式：ClaudeCodeIndicator_<蓝牙MAC>）
- [ ] 2.3 实现 BLE 消息解析与 LED 状态映射（WORKING/WAITING_USER/COMPLETED/ERROR/PAIR_CONFIRM/PAIR_SUCCESS/KEEPALIVE）

## 3. ESP32 LED 状态机

- [ ] 3.1 实现 CC 消息超时机制（10 分钟定时器自动复位 LED 状态）
- [ ] 3.2 实现 WAITING_USER 闪烁逻辑（500ms 周期）
- [ ] 3.3 实现电池检测：5s 间隔 ADC 采样，10 次移动平均，≤3.5V 时 LED1 红色常亮（永久保持，无超时）
- [ ] 3.4 实现 Light-sleep 省电模式（空闲 5s 进入，4s 唤醒）
- [ ] 3.5 编译验证（使用 /idf-build 技能）

## 4. 电脑端 Python 主程序

- [ ] 4.1 创建项目结构 + config.py（所有常量）
- [ ] 4.2 实现 BLE 客户端（bleak 扫描/连接/收发/重连，按名称过滤）
- [ ] 4.3 实现配对流程（PAIR_CONFIRM → Tkinter 对话框 → PAIR_SUCCESS → 保存 device.json 到当前目录）
- [ ] 4.4 实现 TCP 服务器（127.0.0.1:54321），接收消息转发 BLE
- [ ] 4.5 实现保活机制（10s KEEPALIVE，3s 超时等 ALIVE，3 次失败重连）
- [ ] 4.6 创建根目录 start.bat（单实例保护，调用 desktop-relay/main.py）
- [ ] 4.7 运行测试：BLE 连接 + TCP 端口监听

## 5. Claude Code Hook 通知脚本

- [ ] 5.1 实现 notify.py（事件→消息映射：WORKING/WAITING_USER/COMPLETED/ERROR）
- [ ] 5.2 TCP 客户端：连接 127.0.0.1:54321，发送消息，超时 2s 静默忽略
- [ ] 5.3 测试：手动运行验证 TCP 通信

## 6. 安装程序

- [ ] 6.1 实现 install.py：检测安装 bleak，写入 ~/.claude/settings.json hooks（保留已有）
- [ ] 6.2 实现安装验证（检查配置 + 依赖）
- [ ] 6.3 创建根目录 install.bat（调用 installer/install.py）

## 7. 打包与验证

- [ ] 7.1 实现 package.py（打包桌面中继 + Hook + 安装程序为 ClaudeCodeIndicator.zip，不含固件）
- [ ] 7.2 端到端验证：固件烧录 → BLE 连接 → TCP 消息 → LED 状态正确 → 超时复位 → 电池告警 → Light-sleep
