<p align="right">
  <strong>简体中文</strong> · <a href="protected-flash-layout.md">English</a>
</p>

# 受保护的 Flash 布局

ESP32-C3 配备 8 MB Flash。单机身份 `cardid` 固定在 `0x356000`；可分发固件包
不得包含用户密码库内容或设备身份数据。

## 支持的两种布局

| 分区 | 新设备（`factory-4m`） | 现有设备（`legacy-3m`） |
| --- | --- | --- |
| NVS | `0x9000`，大小 `0x6000` | 相同 |
| PHY | `0xF000`，大小 `0x1000` | 相同 |
| 密码库 | `0x10000`，大小 `0x200000` | `0x35A000`，大小 `0x200000` |
| `cardid` | `0x356000`，大小 `0x4000` | 相同 |
| 应用 | `0x360000`，大小 `0x400000` | `0x10000`，大小 `0x300000` |

两种分区表均须有有效的 MD5 标记，所有分区不得重叠。最终 ZIP 包须小于
6,500,000 字节；8 MB 是物理 Flash 容量，不代表可以覆盖受保护分区。

新应用位于 `cardid` 之后，因此从 `0x0` 连续写入的合并 BIN 即使填充内容为
`0xFF`，也会擦除 `cardid` 和密码库区域。合并镜像只用于构建校验，**不得对新
布局设备刷写或分发**。

## 交付与升级

`./tools/validate.sh --firmware` 分别构建并校验两种布局，随后生成：

- `AI-Passport-factory-4m.zip`：新设备分段写入 bootloader（`0x0`）、
  分区表（`0x8000`）及应用（`0x360000`）；要求设备分区表为空。
- `AI-Passport-upgrade-legacy-3m.zip`：旧设备仅在 `0x10000` 写入应用，
  不触碰现有分区表、NVS、`cardid` 和密码库。
- `AI-Passport-upgrade-factory-4m.zip`：已采用新分区表的设备后续仅在
  `0x360000` 写入应用。

使用 `python tools/flash_package.py <zip> --port <串口>`，刷写前先读取并
核对设备分区表。出厂包拒绝已有分区表，升级包拒绝不匹配的布局。已写入数据的
设备严禁执行 `erase-flash`。目前不自动把旧设备迁移到 4 MB：旧密码库位置与
新应用重叠，无损迁移需要单独经过实机验证的备份和迁移流程。
