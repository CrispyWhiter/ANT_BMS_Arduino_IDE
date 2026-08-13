#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "designer"

required = [
    "designer.go",
    "config.go",
    "render.go",
    "bmsui_device.go",
    "theme.go",
    "main.go",
    "go.mod",
    "identity.go",
    "glyph_font.go",
]
for name in required:
    if not (SRC / name).exists():
        raise SystemExit(f"MISSING: {name}")

designer = (SRC / "designer.go").read_text()
config = (SRC / "config.go").read_text()
render = (SRC / "render.go").read_text()
device = (SRC / "bmsui_device.go").read_text()
theme = (SRC / "theme.go").read_text()
main = (SRC / "main.go").read_text()
identity = (SRC / "identity.go").read_text()
image_opt = (SRC / "image_opt.go").read_text()

checks = {
    "v1.1.0": 'appVersion = "v1.1.0"' in main and 'appTitle   = "BMS UI Designer " + appVersion' in main,
    "developer integrity": "xMetaLabel()" in main and "xH" in identity and "xS0" in identity,
    "device left": '"ESP32 设备部署"' in designer,
    "factory restore": '"恢复原始 UI"' in designer and "IDC_DESIGN_DEVICE_RESET" in designer,
    "no arrow buttons": all(
        text not in designer
        for text in ("↺  恢复原始 UI", "↑  发送 UI", "↓  读取 UI")
    ),
    "file workflow": all(text in designer for text in ('"新建"', '"打开"', '"保存"', '"另存为"', '"重置画布"')) and all(text not in designer for text in ('"导入主题 UI"', '"导出主题 UI"', '"保存本地工程"', '"重置编辑布局"')),
    "theme format": 'Format: "BMS-UI-Theme"' in theme and "ESP32S3-PC-Theme" in theme,
    "no visible bmsui export": 'createControl("BUTTON", "导出 .bmsui"' not in designer,
    "opacity": "IDC_DESIGN_OPACITY" in designer and "Opacity" in config,
    "live opacity preview": "case IDC_DESIGN_OPACITY:" in designer and "invalidateDesignerPreviews()" in designer,
    "no maximize": "WS_OVERLAPPEDWINDOW &^ WS_MAXIMIZEBOX" in main and "SC_MAXIMIZE" in main,
    "zero opacity persistence": "legacyOpacity" in config and "if e.Opacity == 0" not in designer,
    "system fonts": "IDC_DESIGN_CHOOSE_FONT" in designer and "chooseFont" in designer and "renderTextMask" in render,
    "double click text": "WM_LBUTTONDBLCLK" in designer and "EM_SETSEL" in designer,
    "image alpha protocol": "RGB565A8" in device and 'case "image"' in device,
    "shapes": all(token in designer for token in ("__shape_rect", "__shape_ellipse", "__shape_circle")) and "ShapeType" in config,
    "shape styles": all(token in config for token in ("CornerStyle", "Chamfer", "BorderWidth", "Fill")),
    "progress cap": "CapStyle" in config and "IDC_DESIGN_CAP_STYLE" in designer,
    "shortcuts": all(token in designer for token in ("VK_DELETE", "VK_N", "VK_O", "VK_S", "VK_A", "VK_C", "VK_X", "VK_V", "VK_D", "VK_Z", "VK_Y", "VK_LEFT", "VK_RIGHT", "VK_UP", "VK_DOWN")),
    "multi select": all(token in designer for token in ("designerSelection", "designerBeginMarquee", "designerUpdateMarquee", "designerMoveGroupTo", "designerSelectionFromList", "designerSelectAll")),
    "keyboard nudge": "designerMoveSelectionBy" in designer and "step = 5" in designer,
    "unsaved guard": "designerConfirmSaveBeforeReplace" in designer and "designerDirty" in designer and "designerCurrentPath" in designer,
    "resize handles": all(token in designer for token in ("resizeNW", "resizeN", "resizeNE", "resizeE", "resizeSE", "resizeS", "resizeSW", "resizeW")),
    "client resize hit zones": "designerResizeHandleAtClient" in designer and "edgeTol" in designer and "handleTol" in designer,
    "independent resize": "designerResizeSelected" in designer and "resizeE" in designer and "resizeS" in designer,
    "proportional resize": "VK_SHIFT" in designer and "keepRatio && corner" in designer,
    "text hit priority": 'e.Type == "text" || e.Type == "value"' in designer,
    "property panel groups": all(text in designer for text in ('"基础"', '"位置与尺寸"', '"内容与外观"', '"类型属性"')),
    "true layer rendering": "for _, e := range layout.Elements" in render and render.count("for _, e := range layout.Elements") == 1,
    "current schema constants": "currentThemeVersion  = 110" in config and "currentSchemaVersion = 8" in config and "SchemaVersion: 6" not in designer,
    "v7 optimized image serialization": "bmsUiProtocolVersion" in device and "= 7" in device and all(token in device for token in ("TEXTIMG|", "GLYPHVAL|", "VSHAPE|", "IMAGE|", "MASKIMG|", "STATEMASK|", "glyphMetaRunes")) and "optimizeImageAsset" in image_opt,
    "gradient controls": all(token in designer for token in ("IDC_DESIGN_GRADIENT_ENABLED", "IDC_DESIGN_GRADIENT_COLOR", "IDC_DESIGN_GRADIENT_DIRECTION")),
}

for label, ok in checks.items():
    if not ok:
        raise SystemExit(f"FAILED: {label}")

field_block = render.split("var fieldKeys = []string{", 1)[1].split("}", 1)[0]
for forbidden in (
    "soh",
    "balancer",
    "max_cell_v",
    "min_cell_v",
    "cell_count",
    "max_cell_index",
    "min_cell_index",
):
    if f'"{forbidden}"' in field_block:
        raise SystemExit(f"FAILED forbidden field {forbidden}")

if "单体电芯" in designer:
    raise SystemExit("FAILED: 单体电芯 exposed")

print("BMS UI DESIGNER v1.1.0 SOURCE CHECK OK")
