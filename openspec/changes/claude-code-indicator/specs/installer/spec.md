## ADDED Requirements

### Requirement: 依赖安装
安装程序 SHALL 自动检测并安装所需的 Python 依赖库（bleak）。使用 `pip install` 自动安装缺失的库。

#### Scenario: 依赖自动安装
- **WHEN** 运行安装程序
- **THEN** 检测 bleak 是否已安装，如未安装则执行 `pip install bleak`

### Requirement: Hook 配置自动写入
安装程序 SHALL 读取或创建 `~/.claude/settings.json`，将通知脚本路径写入 `hooks` 段，不覆盖已有的其他 hook 配置。

#### Scenario: 新安装
- **WHEN** settings.json 中无现有 hooks 配置
- **THEN** 创建 hooks 段并添加通知脚本配置

#### Scenario: 追加安装
- **WHEN** settings.json 中已有其他 hooks 配置
- **THEN** 在原 hooks 配置基础上追加通知脚本，保留已有配置

### Requirement: 启动脚本
安装程序 SHALL 提供 `.bat` 启动脚本，使用指定 Python 路径运行安装程序。

#### Scenario: 一键安装
- **WHEN** 双击 install.bat
- **THEN** 运行安装程序完成依赖安装和 Hook 配置

### Requirement: 安装验证
安装程序完成后 SHALL 验证 settings.json 中 hook 配置已正确写入，依赖库已正确安装。

#### Scenario: 安装成功确认
- **WHEN** 安装和配置均成功
- **THEN** 输出成功信息告知用户安装完成

#### Scenario: 安装失败提示
- **WHEN** 任何步骤失败
- **THEN** 输出明确错误信息，告知用户失败原因
