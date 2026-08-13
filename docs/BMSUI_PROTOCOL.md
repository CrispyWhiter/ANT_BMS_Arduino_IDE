# BMSUI v7 协议

BMSUI 是 BMS UI Designer 与 ESP32-S3 动态 UI Runtime 之间的设备传输格式。v1.1.0 Designer 默认生成 **BMSUI v7**；固件解析器继续兼容 v1–v6。

## 文件头

```text
BMSUI|version|width|height|backgroundColor|title
```

当前 Designer 固定输出 320×240。

## v7 主要能力

### 图片资源自动优化

Designer 根据最终像素内容自动选择：

- `A8`：单色/近单色透明资源，约 1 byte/pixel。
- `RGB565`：完全不透明彩色资源，约 2 bytes/pixel。
- `RGB565A8`：需要保留彩色与透明度的资源，约 3 bytes/pixel。

相同格式、尺寸和内容的资源只保存一份。

### 新控件记录

```text
MASKIMG|id|x|y|w|h|asset|opacity|tint
STATEMASK|id|x|y|w|h|binding|onAsset|offAsset|onTint|offTint|opacity
```

`MASKIMG` 用 A8 蒙版 + 颜色表示普通单色图；`STATEMASK` 用两份 A8 蒙版表示 ON/OFF 双状态图。

### 资源传输

```text
ASSET|name|width|height|format|bytes|crc32
ASSETB64|name|base64-data
```

v7 新包使用 `ASSETB64`。解析器仍接受旧 `ASSETDATA` 十六进制数据行。

## v6：动态字体一致性

v6 新增 `GLYPHVAL`：

```text
GLYPHVAL|id|x|y|w|h|asset|opacity|color|align|binding|decimals|prefix|suffix|font|fontSize|bold|cellWidth|columns|codepoints|advances
```

Designer 按 Windows 当前字体、字号、粗体和元素高度生成 A8 字形图集。ESP32 只组合实时数据，不再替换为另一套设备字体。

因此动态数值、`°C`、`V`、`A`、`W`、`Ah`、`km`、`%` 等字符在 PC 与设备端使用同一套字形资源。

## v5：基础矢量图形

`VSHAPE` 将普通矩形、圆角矩形和正圆交给设备端 LVGL 原生绘制，避免把大面积卡片栅格化为 RGB565A8 位图。

```text
VSHAPE|id|x|y|w|h|shape|corner|fill|fillColor|borderColor|borderWidth|radius|opacity
```

## v4：渐变

v4 为 `ARC` 和 `BAR` 增加渐变色参数。圆环沿进度方向渐变；条形进度可选择自动、横向或纵向方向。

## v3：透明资源与系统字体

v3 增加：

- `IMAGE`
- `TEXTIMG`
- `SHAPE`
- 元素透明度
- 进度端点样式

## v2 / v1

- v2：资源和 `STATEIMG` 双状态图片。
- v1：基础 `LABEL`、`RECT`、`LINE`、`ARC`、`BAR`。

## 当前可解析记录

固件 v1.1.0 parser 支持：

```text
LABEL
RECT
LINE
ARC
BAR
STATEIMG
STATEMASK
IMAGE
MASKIMG
TEXTIMG
SHAPE
VSHAPE
GLYPHVAL
ASSET
ASSETDATA
ASSETB64
```

具体字段数量和合法范围以 `firmware/src/ui_runtime/bmsui_parser.cpp` 与 `bmsui_protocol.h` 为权威实现。

## 资源与安全限制

当前协议限制集中在 `bmsui_protocol.h`，包括最大控件数、最大资源数、最大包体、最大文本长度和运行时资源预算。Designer 发送前读取设备 `/api/ui/info` 中公布的 `runtimeAssetLimit`，避免仅按 PC 端估算。

主题写入不是直接覆盖正式文件，而是由设备执行暂存、解析、CRC 校验、提交和重挂载验证。失败时保持或回退到安全主题。

## 兼容原则

- 旧主题：v1–v6 可继续读取。
- 新主题：v1.1.0 Designer 默认生成 v7。
- 从 v1.0.0 首次升级：必须先 USB 烧录一次 v1.1.0 固件。
- 后续只修改主题：直接通过 Designer “发送 UI”，无需再次刷固件。
