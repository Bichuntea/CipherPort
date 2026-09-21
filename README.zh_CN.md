<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# CipherPort

**面向 FoloToy AI Passport 的本地优先密码管理器。**

![运行在 240 x 320 设备界面上的 CipherPort](docs/assets/github/cipherport-social-preview-readme-1280x640.png)

CipherPort 将开源的 [FoloToy AI Passport](https://github.com/FoloToy/ai-passport) 变成一台口袋大小的密码保险库。记录保存在设备中；密码只有在用户明确确认后才会显示；临时本地 Web 管理器也只能由用户在设备上主动开启。

浏览器只是编辑器，不是密码库。打开会话、新增或修改记录、修改 PIN，以及清除数据，始终受实体设备控制。

## 可以做什么

- 最多保存 16 条受长度限制的记录，字段包括平台、网址、用户名、密码和备注。
- 使用固定四位 PIN 和只显示一次的八位恢复码保护访问。
- 每完成一位机密数字输入，都会重新随机排列设备上的数字顺序。
- PIN 连续错误五次后锁定；恢复码累计错误三次后，当前密码库的恢复功能将永久停用。
- 显示密码前要求设备端二次确认，并在可配置的 5–30 秒窗口结束后自动隐藏。
- 提供英文和简体中文设备界面。
- Web 管理器完全由设备托管，不依赖云服务、分析服务、远程资源、Cookie 或浏览器持久化存储。

## 本地 Web 管理

只有在密码库已解锁且用户从设备上主动请求后，Web 管理才会启动：

1. CipherPort 创建名为 `CIPHERPORT` 的 WPA2 网络，并为本次会话生成新的八位随机密码。
2. 一台客户端连接网络，并打开设备屏幕显示的私有地址。
3. 用户在设备上按 `OK`，授权浏览器会话。
4. 账号变更先暂存在 RAM 中，直到用户在设备上选择 `APPROVE` 或 `CANCEL` 并确认。
5. 离开 Web 管理、锁定密码库或会话超时后，Wi-Fi 会停止，内存中的会话令牌也会失效。

浏览器不会要求输入密码库 PIN 或恢复码。响应带有 `Cache-Control: no-store` 和限制严格的安全响应头。

## 存储与安全模型

- 随机生成的 256 位密码库主密钥使用 AES-256-GCM 加密记录。
- PIN 与恢复码使用独立盐值，各自封装密码库主密钥。
- 密码库使用两代认证提交：先写入并验证非活动代，再切换活动标记。
- PIN 与恢复码的失败次数在重启和断电后仍会保留。
- 离开密码显示、解锁、恢复、Web 授权、锁定和休眠准备状态时，会擦除敏感缓冲区。
- 2 MiB 密码库分区与应用程序及受保护的设备专属 `cardid` 区域相互分离。

CipherPort 仍是开发阶段的安全固件，并非通过认证的硬件安全模块。当前本地页面在 WPA2 链路上使用 HTTP，因此没有独立的应用层传输加密。生产部署还需要经过批准的 Flash Encryption、Secure Boot、NVS 加密、调试限制和防回滚配置方案。在保存真实凭证之前，请阅读[完整安全边界](docs/password-manager/README.zh_CN.md#安全架构)。

## 设备操作

| 输入 | 默认行为 |
| --- | --- |
| `UP` / `DOWN` | 移动项目、选项或机密数字选择 |
| 短按 `OK` | 确认、进入、显示或执行当前操作 |
| 双击 `OK` | 从账号列表打开设置；在支持的机密输入页删除上一位数字 |
| 长按 `OK` | 返回、取消、在支持的页面锁定；只有出现明确提示时才用于长按破坏性确认 |

三个按键共用一组 ADC 电阻梯，因此项目有意不使用同时按下 `UP+DOWN` 的手势。

## 硬件目标

| 项目 | 当前目标 |
| --- | --- |
| 设备 | FoloToy AI Passport |
| MCU | ESP32-C3 |
| Flash | 8 MB |
| PSRAM | 无 |
| 屏幕 | 240 x 320 竖屏 RGB565 |
| 框架 | ESP-IDF 5.5.3 + LVGL |
| 输入 | 三个实体按键：`UP`、`DOWN`、`OK` |

当前版本的 CipherPort **不提供**云同步、USB 或蓝牙密码输入、指纹解锁、USB 网络或身份验证器功能。

## 构建与验证

激活 ESP-IDF 5.5.3 后运行仓库验证入口：

```bash
source <path-to-esp-idf-v5.5.3>/export.sh
idf.py --version
./tools/validate.sh --static
./tools/validate.sh --firmware
```

`--firmware` 会分别构建和验证两种受支持的 Flash 布局，并在 `build/` 下生成三个与分区匹配的 ZIP 包。完整验证命令为：

```bash
./tools/validate.sh
```

构建成功不等于实机验证成功。屏幕可读性、按键行为、写入时断电、反复启停 Wi-Fi、内存余量及日志中不存在敏感信息，仍需在设备上测试。

## 安全刷写

> [!CAUTION]
> `cardid` 固定在 `0x356000`。绝不能从 `0x0` 刷写或分发跨过该区域的连续镜像，也不要在已配置设备上运行 `erase-flash`。

必须使用与设备分区表匹配的包：

- `AI-Passport-factory-4m.zip`——面向空白分区表新设备的分段工厂包。
- `AI-Passport-upgrade-legacy-3m.zip`——面向现有旧布局设备、写入 `0x10000` 的纯应用升级包。
- `AI-Passport-upgrade-factory-4m.zip`——面向已经采用 4 MB 应用布局设备、写入 `0x360000` 的纯应用升级包。

仓库刷写工具会在写入前检查设备分区表：

```bash
python tools/flash_package.py build/<包名>.zip --port <串口>
```

刷写前请阅读[受保护 Flash 布局](docs/development/engineering/protected-flash-layout.zh_CN.md)。旧布局和工厂布局之间不存在自动迁移流程。

## 项目结构

```text
main/password_manager_app.c    设备界面与交互流程
main/password_manager_model.c  可测试的安全与计时状态
main/vault_store.c             带认证的两代密码库存储
main/web_manager.c             SoftAP、HTTP API、会话与审批
web-manager/                   浏览器管理界面
tests/                         主机端状态与固件包检查
tools/                         验证、打包和安全刷写工具
docs/password-manager/         产品需求与安全边界
```

建议从以下文档开始：

- [CipherPort 产品需求与状态模型](docs/password-manager/README.zh_CN.md)
- [Web 管理器架构](web-manager/README.zh_CN.md)
- [构建与测试指南](docs/development/engineering/build-and-test.zh_CN.md)
- [受保护 Flash 布局](docs/development/engineering/protected-flash-layout.zh_CN.md)
- [AI Passport 硬件能力约定](docs/README.zh_CN.md)
- [贡献指南](.github/CONTRIBUTING.zh_CN.md)
- [安全政策](.github/SECURITY.zh_CN.md)

## 许可证

本仓库采用 [MIT License](LICENSE)。CipherPort 构建于开源的 FoloToy AI Passport 硬件与固件基线之上。
