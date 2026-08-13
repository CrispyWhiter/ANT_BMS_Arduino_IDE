# GitHub Release v1.1.0

v1.1.0 在 v1.0.0 基础上升级了 Windows UI Designer、动态主题协议、字体一致性、图片资源占用和 ESP32-S3 自定义 UI 稳定性。

## 主要更新

- 大尺寸编辑画布 + 320×240 1:1 实际尺寸预览。
- 框选多选、整组拖动、方向键微调、四边/八方向缩放和 `Shift` 等比例缩放。
- Windows 系统字体、PNG/JPG、透明度、双状态图片、几何图形和渐变进度。
- 动态数值使用共享 A8 字形图集，PC 与设备显示更一致。
- 单位位置固定，数据位数变化时向左扩展。
- 圆环进度不再内置中心数值，可用独立动态数值自由叠放。
- BMSUI 当前升级到 **v7**，固件继续兼容读取 v1–v6。
- 图片自动选择 A8 / RGB565 / RGB565A8，重复资源自动去重。
- BMSUI v7 使用 `ASSETB64` 资源传输，减少设备包文本体积。
- 基础矩形、圆角矩形和正圆使用 `VSHAPE`，减少大面积卡片位图内存。
- 动态 UI 资源共享，避免多个控件重复分配同一图片或字体图集。
- PSRAM 按运行时实际可用容量工作，不再硬编码 12/16 MiB 门槛。
- 自定义 UI 继续采用事务式上传和启动保护，失败自动回退 Factory UI。
- 代码版本/Schema 常量集中管理，删除开发阶段 Hotfix 文档和重复说明。

## 升级说明

从 v1.0.0 升级到 v1.1.0 时，需要通过 USB 重新烧录一次 v1.1.0 固件。之后只修改主题时，可直接通过 Designer “发送 UI”。

建议保留原 `v1.0.0` Tag/Release，新建 `v1.1.0` Tag/Release，不覆盖旧版本。

## Release 附件

建议上传：

- `BMS_UI_Designer_v1.1.0.exe`
- `SHA256SUMS.txt`

## 已验证

主机侧 `bash tools/run_host_tests.sh` 全部通过，包括四类 BMS 协议、BMSUI v2–v7 示例、Designer 源码检查、源码审计和 Win64 构建。

ESP32-S3 最终发布前仍需在目标 Arduino 环境完成一次完整编译、烧录和真实硬件回归。

## 通过 GitHub 网页更新仓库

适用于已经存在 v1.0.0 仓库、这次继续发布 v1.1.0：

1. 解压本次整理后的 GitHub 源码包。
2. 进入原 GitHub 仓库主页，切换到准备发布的主分支。
3. 点击 `Add file` → `Upload files`。
4. 将解压后仓库目录中的**内容**拖入上传区，不要把外层 ZIP 当成源码上传。
5. 检查 `README.md`、`CHANGELOG.md`、`.github/`、`designer/`、`firmware/`、`docs/`、`tools/` 等目录都已进入待提交列表。
6. Commit message 建议写：`Release v1.1.0`。
7. 提交后打开仓库的 Actions 页面，确认 `host-tests` 工作流通过。

如果仓库设置了主分支保护，应按仓库规则创建新分支并通过 Pull Request 合并，不要绕过保护规则。

## 通过 GitHub 网页创建 v1.1.0 Release

1. 进入仓库主页 → `Releases`。
2. 点击 `Draft a new release`。
3. `Choose a tag` 中新建 `v1.1.0`，Target 选择已经包含本次代码的主分支。
4. Previous tag 选择 `v1.0.0`。
5. Release title 填 `v1.1.0`。
6. 将本文件“主要更新 / 升级说明”复制到 Release 描述。
7. 在二进制附件区域上传：
   - `BMS_UI_Designer_v1.1.0.exe`
   - `SHA256SUMS.txt`
8. 正式版不要勾选 Pre-release；需要时勾选 Set as latest release。
9. 最后点击 `Publish release`。

GitHub 会自动为 Tag 提供 Source code ZIP/TAR，因此无需再手工上传一份重复的源码压缩包作为 Release 附件。
