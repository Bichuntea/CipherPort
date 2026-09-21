<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# CipherPort 网页管理器

此目录包含由 AI Passport 固件托管的无依赖管理界面。`dist/` 是已确认的
CipherPort Admin 网页源码，`firmware/index.html` 是将其样式、交互和 Logo 原样
内联后的量产单文件页面，采用既定的 CipherPort Admin 侧栏、固定页头、密码库
详情和双栏设备设置布局。

使用以下命令将页面打包进固件：

```text
node tools/build_web_firmware.mjs
node tools/pack_web_assets.mjs web-manager/firmware/index.html main/web_assets/index.html.gz
```

固件固定广播名为 `CIPHERPORT` 的 WPA2 SoftAP，每次进入 Web Management 时重新
生成 8 位数字密码，并且只允许一个客户端连接。浏览器授权只需设备端按一次
`OK`；账户修改先暂存在内存中，用户必须在设备上选择 `APPROVE` 或 `CANCEL`
后按 `OK`。

安全约束：

- 不使用 localStorage、IndexedDB、Cookie、统计服务或远程资源；
- 浏览器不输入 PIN 或恢复码；
- 会话令牌仅存在内存中，明确退出时立即失效；
- Wi-Fi 短暂重连不会结束已经授权的管理会话；
- 响应使用 `Cache-Control: no-store` 和严格安全响应头。
