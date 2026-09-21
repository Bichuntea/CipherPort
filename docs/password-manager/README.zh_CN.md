<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

运行时交互说明：欢迎页仅响应短按 `OK` 进入；网页配置的欢迎文案超出自适应框时，
使用 `UP/DOWN` 上下滚动。所有数字输入页使用完整 0–9 键盘：双击 `OK` 删除上一位，
长按 `OK` 返回；输入完成后用 `UP/DOWN` 选择 CLEAR 或 CONTINUE/CONFIRM，短按
`OK` 执行。解锁后的空密码库会直接显示首次账号引导，短按 `OK` 启动网页管理。

# CipherPort 密码管理器产品需求

## 当前固件交互

设备端和 Web Manager 仅使用英文，不提供语言切换。PIN 页面使用无边框输入
指示；`UP/DOWN` 在 10 个数字、`CLEAR` 和 `CONTINUE` 之间循环，`CLEAR`
可随时清空全部输入，输入未满时选择 `CONTINUE` 会显示明确错误。8 位超级
权限码只显示一次，用户确认已记住即可，不需要再次输入。

Wi-Fi 只能在设备的 Web Management 页面按 `OK` 启动，密码为 8 位随机数字，
仅允许一台设备连接。连接并完成设备 PIN 授权后取消倒计时并显示已连接；退出
任一设备端 Web 管理状态都会关闭 Wi-Fi，顶部 Wi-Fi 标识随之消失。

## 产品范围

本分支将 AI Passport 改造为本地密码查看器。加密密码库保存在设备中；临时 Wi-Fi 网页用于编辑记录，但浏览器不是持久存储位置。

更新后的 Figma 文件也包含恢复、首次初始化、锁定、网页授权与修改审批、长内容和清除结果等状态。

ESP32-C3 版本不提供 USB HID 自动输入、USB 网络、蓝牙自动输入、指纹解锁或云同步。原生 USB 继续用于 USB Serial/JTAG 控制台和烧录。

## 硬件硬约束

- 使用 ESP-IDF 5.5.3、ESP32-C3、8 MB Flash，无 PSRAM。
- 新设备在 `0x360000` 使用 4 MB 应用分区；旧设备保留 `0x10000` 起的 3 MB
  应用分区。必须保留 `0x356000`、大小 `0x4000` 的 `cardid`，不得通过直接刷写迁移已有数据。
- 显示、按键、音频、电池和共享 I2C 必须使用 BSP；不得创建第二个 ADC1 unit 或 I2C0 bus。
- 按键回调只入队。存储、音频和网络在 worker 中运行；非 LVGL 上下文访问 UI 必须持有 BSP LVGL 锁。
- 每次启动或唤醒后 Wi-Fi 和蓝牙默认关闭。

## 按键

应用替换 demo 菜单，启动后直接进入密码管理器。

| 输入 | 行为 |
| --- | --- |
| `UP` / `DOWN` | 上一项、下一项或数值选择 |
| 短按 `OK` | 确认、进入或执行 |
| 长按 `OK` | 返回或取消 |
| 短按电源键 | 仅在确认 MCU 可读取后用于锁定熄屏 |
| 长按电源键 | 仅在确认 MCU 唤醒路径后用于 Deep Sleep |

选择、确认、返回、成功、警告和写入结果使用不同提示音。秘密输入时 `UP/DOWN` 声音相同；自动滚动不播放声音。

Figma 中的 `HOLD UP+DOWN SETTINGS` 和清空页 `HOLD UP + DOWN` 不能直接视为可实现：当前三个按键共用单通道 ADC 电阻梯，同时按下 `UP+DOWN` 会塌缩为一个模拟电压。在实测并批准双键电压窗口前，账号列表使用双击 `OK` 进入设置，清空页使用仅在该页面有效的长按 `OK` 5 秒。发布前，界面按键提示必须与实际手势一致。

## Figma 界面契约

所有页面使用 240 x 320 的安全终端外壳：黑色背景，状态栏位于 `y=10`，标题位于 `y=34`，主要内容从 `y=72` 开始，操作区位于 `y=246`，固定按键提示位于 `y=288`。状态栏始终为时间和电量保留位置；Wi-Fi 和蓝牙只在启用时显示。电量颜色分级为良好（不低于 50%）、中等（20%～49%）、低（不高于 19%）。时间无效时显示 `--:--`，不能显示看似可信的时间。

```text
WELCOME -> ENTER PIN -> SAVED ACCOUNTS -> ACCOUNT
ACCOUNT -> REVEAL CONFIRM -> PASSWORD REVEALED -> ACCOUNT
SAVED ACCOUNTS -> SETTINGS
SETTINGS -> WEB MANAGEMENT OFF / ACTIVE
SETTINGS -> CURRENT PIN -> NEW PIN -> CONFIRM PIN
SETTINGS -> LOCK CONFIRM -> WELCOME
SETTINGS -> CLEAR ALL DATA HOLD
SETTINGS -> ABOUT
```

`WELCOME` 接受任意功能键并进入 PIN 页面。账号列表用 `UP/DOWN` 选择，短按 `OK` 打开账号。账号详情使用完整字段名和有界内容，密码始终先保持遮罩。查看密码必须经过有 `CANCEL` 和 `REVEAL` 的二选一警告页。明文页独占 15 秒截止时间、可见范围、进度条、暂停/继续和立即隐藏操作。

## 初始化与秘密输入

1. 在设备上输入固定四位 PIN，不提供 PIN 位数设置。
2. 输入两遍 PIN。
3. 生成保留开头 `0` 的 8 位数字恢复码。
4. 恢复码只显示一次，并要求用户在设备上重新输入。
5. 随机生成密码库主密钥；只保存封装材料和验证值，不保存明文 PIN 或恢复码。

每输入一位 PIN 数字，都以安全 Fisher-Yates 洗牌生成新的 `0..9` 全排列，并在两行选择器中完整显示。`UP/DOWN` 移动，短按 `OK` 追加所选数字，每确认一位后重新洗牌；确认后的数字立即隐藏。`CLEAR` 清除整份草稿；达到四位前 `CONFIRM` 保持禁用，四位时才可提交。长按 `OK` 取消当前页面，不再隐式删除上一位。

修改 PIN 固定分为三步：验证当前四位 PIN、输入新四位 PIN、再次输入确认。只有最后的 `SAVE` 能替换 PIN 密钥封装；两次不一致时返回新 PIN 步骤，并清除两份草稿。

## 锁定、恢复和清空

- PIN 错误 5 次后锁定，计数跨重启和断电保留。
- 锁定后只显示恢复码、清空和休眠。
- 恢复码总计允许错误 3 次，计数跨重启和断电保留。
- 恢复成功后必须设置新 PIN 和新恢复码，旧封装立即失效。
- 恢复码错误 3 次后永久关闭当前密码库的恢复入口。
- 原型的 Flash 计数不能抵抗回滚；量产需要批准的 eFuse 或额外安全硬件。

清空只能在设备端专用警告页面发起。按照当前按键硬件，应在该页面长按 `OK` 5 秒，并持续显示进度，完成后才允许最终提交。先销毁密码库密钥封装，再擦除密码管理器数据，绝不能触碰 `cardid`。

## 账号与密码显示

首版记录只包含有明确长度上限的 UTF-8 平台、备注、网址、用户名和密码字段，不提供分类、收藏或保存日期。网页管理器可以保持设备原始顺序，或按平台名称升序、降序排列。设备账号列表和详情只显示较大的名称及其下方备注；详情页不滚动，只有查看密码和退出操作。密码默认隐藏。选择 `Reveal password` 后必须进入带 `CANCEL` 和 `REVEAL` 的警告页；第二次确认前不得生成明文。明确确认后默认显示 15 秒；设置范围为 5～30 秒，不允许永不隐藏。锁定、超时或离开页面都会清除显示缓冲区。

长密码先停留 1.5 秒，再以约每秒 5 个字符滚动；末尾停留 1 秒后循环。显示可见范围和总长度；`UP/DOWN` 手动移动，短按 `OK` 暂停或继续，但不延长截止时间。使用经过实机验证、能区分 `0/O` 与 `1/I/l` 的受限等宽字体。

## Wi-Fi 网页管理

设备解锁并由用户明确开启后：

1. 启动有密码、最多一个客户端的 SoftAP。
2. 显示 SSID、私有 IPv4 地址和剩余时间。
3. 浏览器连接后，再次要求用户在设备输入 PIN。
4. 验证成功后发放随机短期会话 token。
5. 每项有严格长度上限的修改先放入 RAM，并要求设备短按 `OK`。
6. 锁定、断开、网页空闲 5 分钟、总计 10 分钟或 2 分钟无人连接时停止。

关闭页只显示 `Web access` 开关和说明。启动后进入两分钟连接窗口。活动页显示运行时 IP、SSID、Wi-Fi 密码以及适用的连接/会话倒计时；启动成功前不得显示运行时网络值。

默认地址为 80 端口的 `http://192.168.4.1`。自定义地址必须是私有单播地址并显示在设备上。服务只绑定 SoftAP。响应使用 `Cache-Control: no-store`，网页不持久保存秘密。请求必须有长度限制、逐请求 nonce、会话绑定、超时与限速；秘密和请求正文不得进入日志。

明文 HTTP 依赖 WPA 链路保护，没有独立应用层机密性，只在原型阶段接受；量产前必须重新进行威胁评审。

## 存储与原子提交

现有 24 KiB NVS 只保存有边界的配置和安全元数据，不作为密码库。新设备从
`0x10000` 起、旧设备从 `0x35a000` 起预留 2 MiB `vault` 数据分区。分区带有加密标记，
但只有已配置 Flash Encryption 的设备才会获得静态数据保密性。固件另外使用由 PIN/
恢复码分别包装的随机主密钥和 AES-GCM 双代认证提交，最多保存 16 条有长度边界的记录；
断电写入行为仍需在实机验证。

每代密码库包含格式版本、逻辑代数、唯一 nonce、加密 payload 和认证标签。先写入并验证非活动代，再原子切换活动标记，最后使旧代失效。切换前发生取消、断开、错误或重启时继续使用旧代。

## 安全架构

- 随机生成主密钥，不能直接用短 PIN 作为密码库密钥。
- 使用随机 salt 和设备绑定秘密，分别封装 PIN 与恢复路径。
- 使用经过评审的认证加密模式，同一密钥下 nonce 绝不重复；常量时间比较并及时清除秘密。
- 尽量只解密当前选中的记录。
- Flash Encryption、Secure Boot、NVS Encryption、签名固件、调试限制和防回滚必须区分开发与量产流程；普通开发构建不得烧写不可逆 eFuse。
- 备份使用独立高熵秘密，不能使用 8 位恢复码。

## 运行时所有权

| 所有者 | 职责 |
| --- | --- |
| LVGL task | 页面和非秘密显示状态 |
| button callback | 无阻塞事件入队 |
| app reducer | 纯状态转换与授权 |
| security worker | 验证、密钥解封、加解密与清零 |
| storage worker | 原子代、计数和备份流 |
| network worker | SoftAP/STA、HTTP、会话和请求边界 |
| audio worker | 队列提示音和阻塞 PCM 写入 |

worker 使用有界队列。自动关屏会清除秘密、关闭 Web 会话并熄灭 LCD 与背光，同时保留按键监听；
任意功能键操作一次即可唤醒显示。硬件关机仍是独立流程。

## 顶层状态

```text
UNENROLLED -> PIN_SETUP(4) -> PIN_CONFIRM(4) -> RECOVERY_CONFIRM -> WELCOME
WELCOME -> PIN_ENTRY(4) -> ACCOUNT_LIST
PIN_ENTRY -- five failures --> PIN_LOCKED
PIN_LOCKED -> RECOVERY_ENTRY -> NEW_PIN_SETUP(4) -> LOCKED
RECOVERY_ENTRY -- three failures --> RECOVERY_DISABLED
ACCOUNT_LIST -> ACCOUNT_DETAIL -> REVEAL_CONFIRM -> PASSWORD_REVEAL
ACCOUNT_LIST -> SETTINGS -> CHANGE_PIN_CURRENT -> CHANGE_PIN_NEW -> CHANGE_PIN_CONFIRM
SETTINGS -> LOCK_CONFIRM -> WELCOME
SETTINGS -> ERASE_HOLD / ABOUT / WEB_MANAGEMENT_OFF
WEB_MANAGEMENT_OFF -> WIFI_STARTING -> WEB_AUTH_PENDING -> WEB_ACTIVE
WEB_ACTIVE -> DEVICE_APPROVAL -> ATOMIC_COMMIT -> WEB_ACTIVE
any sensitive state -> LOCKING -> LOCKED / SLEEP_PREP
```

离开敏感内容的每个转换都必须明确清零缓冲区；所有锁定状态均禁止通信。

## 交付阶段

1. 纯 reducer、乱序秘密输入、边界和主机测试。
2. 初始化/锁屏 UI、账号外壳、音频队列和电量状态。
3. 经批准的密码库分区、加密存储、恢复、清空和原子测试。
4. 有严格资源上限的 SoftAP/HTTP 服务和内嵌网页。
5. 备份/恢复和经批准的签名升级路径。
6. 量产安全配置和实机验证。

## 必须获得证据的决策

1. BSP 没有 MCU 可读取的左侧电源键信号；实现手势前必须确认原理图和唤醒能力。
2. 批准密码库容量、字段上限、备份上限和分区布局。
3. 单 factory-app 布局没有非活动 OTA 槽；网页升级前必须定义断电安全的签名升级路径。
4. 确认接受原型 HTTP 风险，或选择应用层加密/HTTPS。
5. 分别定义开发、试产和量产 eFuse 流程。
6. 替换 Figma 中两个不可可靠实现的 `UP+DOWN` 提示，或批准经过实测的 ADC 解释。当前回退方案是双击 `OK` 进入设置、在清空页长按 `OK` 5 秒。
7. 补齐初始化/恢复、网页修改确认、PIN 锁定/恢复禁用结果和写入成功/失败的最终 Figma 页面。

## 验收基线

主机测试覆盖状态转换、排列不变量、尝试边界、超时、密码滚动、解析边界、中断原子提交和清零 hook。实机测试覆盖按键、显示、电池降级、音频延迟、heap、反复网络启停、Flash 中断、畸形 HTTP、单客户端限制和无敏感日志。
