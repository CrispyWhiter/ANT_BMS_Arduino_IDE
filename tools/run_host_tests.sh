#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TESTS="$ROOT/firmware/tests_host"
OUT="${TMPDIR:-/tmp}/bms_dynamic_ui_tests"
mkdir -p "$OUT"
cd "$TESTS"
CXX=(g++ -std=c++17 -Wall -Wextra -Werror -Istubs -I../src)
"${CXX[@]}" test_ant_protocol.cpp ../src/protocol/ant_protocol.cpp -o "$OUT/ant"; "$OUT/ant"
"${CXX[@]}" test_jk_protocol.cpp ../src/protocol/jk_protocol.cpp -o "$OUT/jk"; "$OUT/jk"
"${CXX[@]}" test_jbd_protocol.cpp ../src/protocol/jbd_protocol.cpp -o "$OUT/jbd"; "$OUT/jbd"
"${CXX[@]}" test_yanyang_protocol.cpp ../src/protocol/yanyang_protocol.cpp -o "$OUT/yy"; "$OUT/yy"
"${CXX[@]}" test_dashboard_status_logic.cpp -o "$OUT/dashboard_status"; "$OUT/dashboard_status"
"${CXX[@]}" test_dashboard_test_mode.cpp ../src/presentation/dashboard_test_mode.cpp -o "$OUT/dashboard_mode"; "$OUT/dashboard_mode"
"${CXX[@]}" test_display_power_logic.cpp ../src/power/display_power_logic.cpp -o "$OUT/display_power"; "$OUT/display_power"
"${CXX[@]}" test_bmsui_parser.cpp ../src/ui_runtime/bmsui_parser.cpp -o "$OUT/bmsui"; "$OUT/bmsui"
"${CXX[@]}" test_ui_data_provider.cpp ../src/ui_runtime/ui_data_provider.cpp -o "$OUT/ui_data"; "$OUT/ui_data"
python "$ROOT/tools/validate_bmsui.py" "$ROOT/examples/dashboard_demo.bmsui"
python "$ROOT/tools/validate_bmsui.py" "$ROOT/examples/status_icons_demo.bmsui"
python "$ROOT/tools/validate_bmsui.py" "$ROOT/examples/v3_opacity_demo.bmsui"
python "$ROOT/tools/validate_bmsui.py" "$ROOT/examples/progress_logic_demo.bmsui"
python "$ROOT/tools/validate_bmsui.py" "$ROOT/examples/v4_gradient_demo.bmsui"
python "$ROOT/tools/validate_bmsui.py" "$ROOT/examples/v5_vector_shape_demo.bmsui"
python "$ROOT/tools/validate_bmsui.py" "$ROOT/examples/v6_font_fidelity_demo.bmsui"
python "$ROOT/tools/validate_bmsui.py" "$ROOT/examples/v7_image_optimization_demo.bmsui"
python "$ROOT/tools/check_designer.py"
python "$ROOT/tools/audit_source.py"
if command -v go >/dev/null 2>&1; then
  (cd "$ROOT/designer" && GOOS=windows GOARCH=amd64 go build -trimpath -ldflags='-H=windowsgui -s -w -buildid=' -o "$OUT/BMS_UI_Designer_v1.1.0.exe" .)
  echo "WIN64 GO BUILD PASS"
else
  echo "Go not installed: skipped Win64 cross-build"
fi
echo "ALL CHECKS PASS"
