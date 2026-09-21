<p align="right">
  <strong>简体中文</strong> · <a href="CHANGELOG.md">English</a>
</p>

# Changelog

## Unreleased

- 删除设备主页日期、设备顶部状态栏时间，以及完整的 Web 时间同步设置和接口；密码、
  语言、Wi-Fi、显示及会话功能保持不变。

- 仅在主页和已保存账号列表页新增长按 OK 熄屏快捷操作，并在两个页面底部显示明确
  提示。账号列表使用两行提示区，完整保留上下选择、打开、设置和锁屏操作说明。
  唤醒时会在 LCD 与背光仍关闭的状态下重绘并刷新主页，避免主页出现前短暂闪现
  上次锁屏时的界面。

- 增大已保存账户卡片及其可用文字宽度，并将备注移离下边框。用户输入的中文账户
  标题和备注统一使用完整的静态字形来源，不再使用变换或持续动画，既避免混用字体
  指标，也避免无 PSRAM 设备在保存后出现界面卡顿。删除仅显示一次的 8 位恢复码
  下方小字说明。Web 锁定页会在断开前将 Logo 转为内嵌图片，因此 Wi-Fi 和设备网页
  服务关闭后仍可正常显示。

- 按当前全部设备界面文本重新生成 19 px 标题字库，账号详情等标题不再混用
  19 px 字形与 14 px 回退字形。已授权的 Web 管理会话不再受较短的屏幕自动
  锁定时间影响，并增加 10 分钟无鉴权请求自动退出保护，因此同一会话可连续
  新增多条记录。

- 网页管理页在返回提示旁恢复 `OK START` / `OK STOP` 操作提示；设置页支持长按
  OK 返回；网页在设备授权前先读取设备语言，使首次显示的授权界面跟随设备初始化
  时选择的语言。

- 新增首次启动语言选择，先保存中英文语言再进入设备初始化。网页管理现在可从
  任意状态退出，并返回设置页或空密码库引导页；中英混合控件改用中文回退字体，
  About 信息改用字体支持的 ASCII 分隔符，浏览器确认页不再显示异常方块。

- 启用 LVGL 压缩字体解码，用于设备端 14 px 正文和 19 px 标题中文字体。此前字形
  数据虽已打入固件，但解码器未启用，导致中文标签显示为空白方框。

- 重做设备“关于”页面：名称改为 `Cipherport`，下方显示功能 `Password Manager`，
  并同步 Web 侧栏中的仓库、作者和版本信息。中文界面改为 12 px 小字、14 px 正文、
  19 px 主标题三级字号，使标题层级与英文 20/22 px 主标题保持一致。

- 设备语言菜单在两种模式下均显示双语选项：`语言：中文/English` 与
  `LANGUAGE: ENGLISH/中文`。Web 同步时间现在会把时间值和时区作为一次 NVS
  提交保存；设备重启后恢复最后校准时间，不再回到未同步基准。

- 自动锁定不再直接进入无法由功能键唤醒的深度睡眠；现在会安全关闭密码库和 Web
  会话，熄灭 LCD 与背光，并可通过一次 UP、DOWN 或 OK 操作唤醒，避免自动关屏后
  需要反复长按硬件电源键。

- 修复启动时的时钟初始化：不再覆盖已保存时区，也不再用固件编译时间替代。
  未同步时间的设备现在从本地时间 `2000-01-01 00:00` 开始；Web 管理端可同步
  当前手机或电脑时间，也可保存 2000 至 2100 年之间的自定义本地时间。

- 新设备采用分段刷写的 4 MB 应用布局，现有设备使用独立的 3 MB 仅应用升级包。
  两种布局均保留 `cardid@0x356000`；打包校验拒绝写入受保护区域并强制 6.5 MB
  上限，分区识别刷写工具在写入前拒绝不匹配设备。

- 新增网页端设备时间读取、按当前手机或电脑时间同步、手动设定本地时间；设备保存时区偏移并及时刷新时间显示。整包交付校验限制为 6.5 MB，保留 8 MB Flash 与受保护的 3 MB 应用分区布局。

- Web 记录生成器改为 12 位混合字符；Wi-Fi 管理连接密码仍是每次新生成的 8 位数字。

- Web 欢迎语和语言设置现在等待设备 NVS 写入成功后才报告保存成功；写入失败会返回给网页。

- 新增 Web 端可持久保存的英文／简体中文切换，并与设备语言设置连通。欢迎语
  现在会立即更新运行时内容、回读校验保存的 UTF-8 文本，并优先使用 LVGL
  Source Han 中文字体、完整 Noto CJK 字体兜底，避免中文字形显示为方框。

- 修复设备首页无法保存有效 UTF-8 中文内容的问题，并让用户输入的中文内容自动使用中文
  字体。Web 显示设置现在会持久保存到设备；达到设定的闲置时间后，设备会安全锁定密码库、
  关闭面板和背光，同时保留功能键唤醒能力。

- 新增可持久保存的设备英文／简体中文语言设置，并包含完整的中文字形支持。账号详情
  现在会显示标题名称、账号和可选备注；备注为空时不再显示 `NO NOTE` 占位文字。

- 修复固件打包流程，使嵌入式压缩网页与 HTML 源码一同重新生成并进行一致性校验，
  避免已修复页面仍编译进旧的损坏脚本。设备连接成功页面现在显示完整的
  `http://192.168.4.1/` 管理地址。

- 让嵌入式网页在启动阶段独立请求设备授权，并将原版 Logo 改为单独资源，使可执行
  页面压缩后仅约 16 KB，同时补充移动浏览器兼容处理。修复打包替换破坏 JavaScript
  选择器函数、导致全部网页控件失效的问题。设备等待连接窗口为 120 秒，授权后的
  Web 管理会话仍为 5 分钟。

- 重新恢复专门 CipherPort Web 设计任务中的原始网页版式；提高手机浏览器 HTTP
  请求头上限；拆分 Wi-Fi 接入与浏览器授权状态，避免新增记录审批事件丢失；唯一
  客户端接入热点后设备倒计时立即停止。

- 正常输入 PIN 时不再显示剩余次数，仅在错误页显示实际剩余次数；Web 管理倒计时与
  已连接页面的 `STOP` 统一为其他页面所用的实心选中按钮样式。

- 修复连接设备热点后网页无法操作的问题：固件内网页现在始终使用真实设备接口，
  captive portal 等非根路径会跳转到管理页，浏览器经设备授权后同步设备时间。
  顶部时间在尚未同步时使用固定的 2000-01-01 基准，并按分钟刷新，不再空白或停滞。

- 恢复此前已确认的 CipherPort Admin 网页源码，并将同一套响应式布局、交互和 Logo 完整打包进固件。新增设备真实备注字段和 Web 账户增删改、5 分钟连接时间、由网页发起的设备端 PIN 修改/清除流程，同时简化设备账号页并隔离各页面的按键事件。

- 修复刷入合并镜像后首次建档无法解锁的问题：创建新保险库密钥前，后台存储任务会清除失去密钥的旧保险库密文。PIN 输入页和错误页现在使用可完整显示的简短动态剩余次数提示。

- 修复 Web Management 连接文字与倒计时区域重叠的问题。SoftAP 现在固定使用
  `CIPHERPORT` SSID，每次进入管理页面时重新生成 8 位数字密码。已授权 Web
  会话可承受客户端短暂重连，保存账户后仍可继续管理；从设备或浏览器明确退出时
  仍会关闭 Wi-Fi 并销毁仅存于内存的令牌。同时恢复既定的固定页头和双栏
  CipherPort Admin 设置布局。

- 已将量产界面与 Figma 的 `02 Device Screens` 同步：电池帽贴合外框，取消空白
  按键提示框，首次账户引导自动换行，放大仅显示一次的 8 位恢复码，并保证单操作
  与双操作页面只响应画面中可见的按键。浏览器授权现只需设备端按一次 `OK`，不再
  二次输入 PIN。嵌入式英文 Web Manager 已恢复为既定 CipherPort Admin 布局，
  同时保留真实账户、欢迎语设置及设备物理批准写入接口。

- 完善仅英文的设备端与 Web Manager：取消语言切换，去除重复或被裁切的提示，
  分离电池填充与外框，并将 PIN 输入框改为无边框指示。数字、`CLEAR` 和
  `CONTINUE` 现在位于同一个焦点循环中；输入不完整时继续会显示明确错误。
  Web Wi-Fi 改为 8 位随机数字密码，只能从 Web Management 页面启动，仅允许
  一台设备连接；授权后取消等待倒计时，退出设备端管理页面即关闭 Wi-Fi。
  生成的 8 位超级权限码仅显示一次，不再要求重新输入确认。

- 修复所有 PIN、恢复码和网页授权数字输入页：使用设备可靠字体显示完整
  0–9 键盘，补齐 CLEAR/CONTINUE 焦点、双击 OK 删除和长按 OK 返回。
  电池状态改为仅显示高、中、低、不可用四档图标；新增空密码库首次网页录入
  引导、可由网页设置并上下滚动的双语欢迎文案，以及基于绝对截止时间的统一
  密码与网页会话倒计时条。

- 按更新后的 67 状态 Figma 流程重建设备界面，修复中文字库和标题字号，并补齐首次
  PIN/恢复码设置、锁定、设置、电量/时间、网页授权、设备审批和清除状态。新增由 PIN
  与恢复码分别包装的随机密码库密钥、带认证的双代原子密码库存储、真实账户浏览与密码
  显示，以及由设备逐次批准账户增删改的中英双语 Web Manager。

- 整合精简的 CipherPort 双语固件界面与网页管理：英文/简体中文偏好通过有边界的 NVS
  worker 共用并持久化；SoftAP 与 HTTP 仅在设备端开启后运行；内嵌网页压缩为 2.4 KiB，
  不包含演示凭据；生产构建移除蓝牙，并在 `cardid` 之后预留经分区校验的 2 MiB 密码库。

- 完善 CipherPort 网页管理预览：竖屏导航可点击外部关闭，竖屏记录列表未进入操作时不显示选中高亮，会话时间可点击延长，帮助按钮改为圆形；记录支持备注与设备显示顺序调整；SoftAP 密码默认每次启动随机生成，也可改为固定密码；自动锁定仅选择时间，不再提供开关；按键音覆盖全部按键；并新增设备首页内容、设备端修改 PIN 和实体确认后清除数据。

- 明确密码管理器固件需求并对齐新的 Figma 设备流程和基础 Web Manager：固定四位 PIN 的初始化与修改、账号/显示确认/设置/网页管理导航、PIN 错误 5 次锁定、恢复码错误 3 次后关闭恢复入口、密码限时显示与滚动、设备实体确认后的网页原子修改、运行时所有权、安全边界，以及尚待解决的页面、硬件、存储和升级决策。首版范围不包含验证器、操作记录、分类、收藏、保存日期和备注。

- 加入厂家为优特利 520mAh 电芯生成的 80 字节 CW2017 profile，并实现内容与更新标志检查、写入后校验、规定的重启时序以及有上限的 SOC 就绪等待。

- 扩充环境引导文档：新增乐鑫 Git 服务镜像（`git.espressif.com.cn`）作为中国大陆首选线路，覆盖 ESP-IDF v5.5.3 及其子模块；补充子模块长等待/超时处理、原地修复，以及 `esp32-wifi-lib` 等大仓的按钉死 commit 浅取；提示按仓库残留的 Jihulab `insteadOf` 旧配置；并把官方离线 release 压缩包加入兜底方案（经验来自 `esp-mosaico/esp-mosaico-vibe`）。

- 按功能域整理文档并采用双入口：根目录 `AGENTS.md` 变为薄路由（只保留硬约束与任务路由），详细的 AI 开发工作流下沉到 `docs/development/ai-guide.md`，`agent-guide.md` 并入其中。为 `docs/development/` 增加二级分区（`engineering/`、`ci/`、`release/`），把 `plays/` 应用档案与 `experiences/` 移入带专属 README 的 `docs/reference/` 参考区；删除 `docs/software-design/`（空脚手架）；把 `assets/{fonts,images,music}/README` 三个叶子 README 并入 `assets/` README；把 `project-completion` 的六个子文档压平为单文件；并把每个目录统一为单一 README，消除所有 `INDEX` 文件与一处重复经验索引。所有交叉引用与文献链接已更新；未丢弃任何内容。

- 删除位于 `0x700000` 的旧 app/test 分区，以及相关的 bootloader、校验和
  文档要求；固定的 `cardid` 保护分区及其 CI 校验保持不变。
- 规定多应用发布的 Release 标题约定：tag 按 `v<版本>-<应用名>`（如 `v0.1.0-voice-keychain`）命名，让 Release 标题同时带版本与应用名；发布成功后核对标题，保证一眼扫 Release 列表就能区分是哪个应用。
- 新增发布后收尾流程：`issue-suggestions` skill 用于把用户反馈作为 issue 提交到上游项目；`experience-pr` skill 用于把可复用的开发经验作为文档 PR 提交；新增 `docs/experiences/` 目录保存单条经验文件；并配套 `project-completion`、`file-issues` 与经验索引文档。
- 精简仓库根目录：将 GitHub 可识别的社区治理文档迁入 `.github/`，将变更记录迁入 `docs/`，同步全部引用，并在仓库检查中加入根目录文档白名单。
- 全仓库文档语言规范：所有维护中的 Markdown 默认 `.md` 文件使用英文，简体中文使用配对的 `.zh_CN.md`，双方提供语言切换；静态检查会阻止缺失配对、缺失切换链接或英文默认页混入中文正文。
- AI 开发流程一期：精简按任务加载的上下文入口，统一本地/CI 验证脚本，新增 PR 自动构建与模板，并提交依赖锁文件以提高构建可复现性。
- PR 审查修复：GitHub Actions 固定到完整 commit SHA，构建与发布 job 按最小权限拆分，同步 checkout 关闭凭证持久化；补充 Feature Request / Usage Question issue 表单；启用并修正私密安全报告兜底说明；清理 README 路径、CI 触发条件与历史分支描述漂移。
- 语言规范变更：commit 标题、PR 标题与 body 由"默认中文"改为**使用英文**（`docs/contribution/commit-and-pr.md` 更新）；中文写作规范（全角标点）适用范围剔除 PR/MR 描述（`doc-conventions.md` 更新）。
- CI 构建改造：`build-firmware.yml` 显式传入 `SDKCONFIG_DEFAULTS=sdkconfig.defaults` 再 `idf.py build`，由 defaults 启用自定义分区表（`CONFIG_PARTITION_TABLE_CUSTOM=y`，文件名为 `partitions.csv`）；`CONFIG_ESPTOOLPY_HEADER_FLASHSIZE_UPDATE` 改为 `n`，再用 `idf.py merge-bin -o build/FoloToy-AI-Passport-full.bin` 合并可直刷完整固件；产物精简为仅 full.bin；`actions/cache` 升级到 v5 以消除 GitHub Actions Node.js 20 弃用警告；CI 文档同步更新。
- 合并上游 PR #6（wireless-low-power-demos）以解决 PR #4 冲突：引入无线/低功耗 demo（`main/demo_wifi.c`、`demo_ble.c`、`demo_radio.c`、`demo_low_power.c`）、`partitions.csv`（NVS/PHY/3 MB factory-app 分区）、`main/CMakeLists.txt`/`main.c`/`demo.h`/`sdkconfig.defaults` 更新；同步硬件指南的 Wi-Fi/BLE/低功耗章节；README 能力契约表补充 Wi-Fi/Bluetooth LE/Low power 三项（中英双语）。
- 提交规范补充：`docs/contribution/commit-and-pr.md` 明确 PR 标题与 commit 标题使用相同的 Conventional Commit 格式和英文祈使句，不用名词短语当标题。
- CI 与文档清理：`sync-main.yml` 移除 `test_mode` 残留模板注释；`docs/development/coding-conventions.md` 将「Redis TTL」条目泛化为「缓存组件」条目（当前固件无 TTL 约束需求，消除从模板带入的无关约定）。
- 补充通用规范（借鉴 Shinku）：`docs/contribution/doc-conventions.md` 新增中文全角标点规范（正文 `，`；`（`）`，代码/命令/路径保留英文原样）、凭证不入仓规范（token/密钥/私钥绝不入仓，提交前 git diff 扫描敏感前缀）、文件删除安全规范（删除走系统回收站，不用 rm -rf/git clean -fd）。
- 代码注释规范强化：`docs/development/coding-conventions.md` 补充完善注释要求——函数说明（用途/参数/返回值/副作用/线程上下文/内存所有权/初始化顺序）、变量说明（语义/取值范围/生命周期/同步要求）、逻辑注释（状态机/时序/寄存器/魔数依据），覆盖范围宁多勿少，中文注释保留英文技术术语。
- 文档去 AI 化：`docs/README.md` / `docs/README.zh_CN.md` 移除 AI 专属章节（Entry point、Source-of-truth、提需求格式、BSP 边界、Runtime invariants、验收交付格式、构建命令），README 只保留给人看的项目介绍、硬件能力契约、demo 案例与项目结构；构建命令章节删除（与 `docs/development/build-and-test.md` 重复）。
- 新增 `docs/development/agent-guide.md`：集中承载"AI 如何在本仓库工作"（上下文建立顺序、事实来源优先级、提需求格式、BSP 边界、运行时规则、交付格式），并链接 build-and-test 与硬件指南，不重复构建命令与验收矩阵。
- 同步更新索引：`AGENTS.md` 规则索引新增 agent-guide 条目；`docs/INDEX.md` 与 `docs/development/README.md` 新增 agent-guide 索引行。
- 文档补充：`docs/fork-guide.md` 说明「为什么根目录不放置 README」——根目录 README 预留给 fork 开发者自行放置（上游留空），fork 后可将自己的内容写入根目录 `README.md` 介绍 fork 后的项目；GitHub 显示优先级（根 README > docs/README.md）契合该预留意图。
- 分支合并：创建 `main-update` 分支（基于与上游一致的 main），将 `feature/repo-structure`、`ci/build-firmware`、`ci/sync-main` 三个分支合并进来，统一 docs 结构（CI 文档归入 `docs/development/`，workflow 文件随 ci 分支引入 `.github/workflows/`）；解决 development/software-design README 的 add/add 冲突。
- 合并后审查修复：`docs/INDEX.md` 补充 CI 文档索引；`docs/fork-guide.md` 修正 workflow 引用为 `.github/workflows/sync-main.yml`；`docs/README` 双语项目结构块补充 `.github/workflows/` 与 CI 文档说明。
- ci 分支 CI 文档路径调整：`ci/build-firmware` 的 `docs/software-design/CI-build-and-release.md` 与 `ci/sync-main` 的 `docs/software-design/CI-sync-main.md` 均移入各分支的 `docs/development/`（CI 属工程规范）；`docs/software-design/README.md` 保留为软件设计索引；feature 分支的 software-design 索引同步更新引用。
- fork 补充文档目录迁移：`assets/docs/` 移至 `docs/assets/`（文档素材归入 docs/ 更合理），新增 `docs/assets/.gitkeep` 空目录占位；同步更新 AGENTS.md / INDEX / doc-conventions / fork-guide 的路径引用。
- 文档结构调整：根目录不再放 README——上游英文 README 移入 `docs/README.md`、中文移入 `docs/README.zh_CN.md`（GitHub 从 docs/ 识别主 README）；原 `docs/README.md` 根总索引更名为 `docs/INDEX.md`；同步更新 AGENTS.md / CONTRIBUTING / SUPPORT / fork-guide / doc-conventions 的路径引用。
- 初始化项目文档：新增 `AGENTS.md`、`CLAUDE.md` 和 `CHANGELOG.md`。
- 仓库结构规整：上游英文 `README.md` 更名为 `README.en_US.md`，保留 `README.zh_CN.md`。
- 新增目录骨架：`docs/`（software-design / hardware-design）、`assets/`（fonts / images / music，各含 `README.md`）、`skills/`。
- 将上游硬件开发指南归位到 `docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md`。
- 文档规范：子目录 readme 统一为大写 `README.md`；补充 fork 用户约定（main 只动根 README）。
- 扩展 fork 用户约定：`main` 分支允许修改根目录 `README.md` 和 `assets/docs/`（README 不足以说明项目时存放补充文档与素材）。
- 新增 `assets/docs/` 目录约定：上游 main 只保留空目录 `.gitkeep`，内容文件仅存在于 fork；使用方法规范写入 AGENTS.md「给 fork 用户」约定。
- CI 文档迁移：`docs/software-design/CI.md` 从本分支移除，迁至 `ci/build-firmware` 分支并改名为 `docs/software-design/CI-build-and-release.md`。
- 补充 `main` 分支策略说明：解释 `main` 保持干净的两大原因（与上游同步无冲突 + 多小项目按分支整理）；例外——执意 main 开发需停用 CI 自动同步；提醒 fork 用户默认 action 关闭需手动启用（此条为整个 CI 的通用要求，统一写入 AGENTS.md）。
- 文档拆分：将 `AGENTS.md` 按主题拆为公共文档——新增 `docs/contribution/`（doc-conventions.md、commit-and-pr.md）与 `docs/development/`（build-and-test.md、coding-conventions.md），新增 `docs/fork-guide.md`；`AGENTS.md` 精简为简介 + 项目概述 + 必读文档索引。
- 同步更新索引：`docs/software-design/README.md`、`README.en_US.md` / `README.zh_CN.md` 的 `docs/` 目录说明。
- 参考 cindy 仓库文档组织完善索引：新增 `docs/README.md` 根总索引；AGENTS.md 规则索引按触发场景改写（附触发条件）；`docs/contribution/` 与 `docs/development/` 的 README 补充收录标准。
- 引入社区治理文档（参照 cindy 改写，放仓库根目录）：新增 `CONTRIBUTING.md` / `.zh_CN.md`（贡献指南，针对 ESP-IDF/AI agent/fork 场景改写）、`CODE_OF_CONDUCT.md` / `.zh_CN.md`（贡献者公约）、`SECURITY.md` / `.zh_CN.md`（安全报告流程）、`SUPPORT.md` / `.zh_CN.md`（支持渠道）；AGENTS.md 与 docs/README.md 同步引用。
