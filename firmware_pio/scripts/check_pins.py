# -*- coding: utf-8 -*-
"""
check_pins.py —— 构建开始前打印当前 env 的屏幕引脚分配，并检查配置来源。

功能：
  1. 从 src/app_config.h 自动读取 ANT_BMS_PIN_* 宏的默认值；
  2. 从当前 env 的 build_flags 解析显式定义的引脚宏（-DANT_BMS_PIN_xxx=值）；
  3. 打印每个引脚的最终值及其来源（"宏定义" / "默认值回退"）；
  4. 若有引脚未显式定义（回退到 app_config.h 默认值），给出警告。

用法（platformio.ini [env] 公共段）：
      extra_scripts =
          scripts/ensure_lv_conf.py
          scripts/check_pins.py
"""
Import("env")  # noqa: F821

import os
import re


def _read_default_pins(project_dir):
    """从 src/app_config.h 提取 ANT_BMS_PIN_* 宏的默认值（自动同步）。"""
    path = os.path.join(project_dir, "src", "app_config.h")
    defaults = {}
    if os.path.isfile(path):
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            for line in fh:
                m = re.match(
                    r"\s*#define\s+(ANT_BMS_PIN_[A-Z0-9_]+)\s+(\S+)", line
                )
                if m:
                    defaults[m.group(1)] = m.group(2)
    return defaults


def _defined_pins_from_flags(env):
    """从当前 env 的 build_flags 解析显式定义的 ANT_BMS_PIN_* 宏。"""
    raw = env.GetProjectOption("build_flags", "")
    if raw is None:
        raw = ""
    if not isinstance(raw, (list, tuple)):
        raw = [raw]
    flags_text = "\n".join(str(x) for x in raw)

    defined = {}
    for m in re.finditer(r"-D(ANT_BMS_PIN_[A-Z0-9_]+)=([^\s;]+)", flags_text):
        defined[m.group(1)] = m.group(2)
    return defined


def _check_pins():
    project_dir = os.path.abspath(env.subst("$PROJECT_DIR"))  # noqa: F821
    pioenv = env.subst("$PIOENV")  # noqa: F821

    defaults = _read_default_pins(project_dir)
    defined = _defined_pins_from_flags(env)

    print("[pin-check] ====================== 引脚分配检查 ======================")
    print("[pin-check] env: %s" % pioenv)

    if not defaults:
        print("[pin-check] [WARN] 未能从 src/app_config.h 读取默认引脚配置。")
        return

    missing = []
    for name in sorted(defaults):
        if name in defined:
            value = defined[name]
            source = "宏定义"
        else:
            value = defaults[name]
            source = "默认值回退"
            missing.append(name)
        print("[pin-check]   %-34s %-6s  (%s)" % (name, value, source))

    if missing:
        print(
            "[pin-check] [WARN] 以下 %d 个引脚未在 build_flags 中显式定义，"
            "回退到 src/app_config.h 的默认值："
            % len(missing)
        )
        for name in missing:
            print("[pin-check]   - %s = %s（默认）" % (name, defaults[name]))
    else:
        print("[pin-check] [OK] 所有引脚均在 build_flags 中显式定义。")
    print("[pin-check] ==========================================================")


_check_pins()