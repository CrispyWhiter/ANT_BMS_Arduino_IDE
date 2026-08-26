# -*- coding: utf-8 -*-
"""
ensure_lv_conf.py —— 每次构建前把仓库 firmware/lv_conf.h 同步到 lvgl 库目录。

背景：
  LVGL 8.4 需要 lv_conf.h 位于 lvgl 库目录内（library.json 的 includeDir "." +
  src/lv_conf_internal.h 内部的 "../../lv_conf.h" 相对路径）才能被正确加载。
  但 .pio/libdeps 是 PlatformIO 依赖缓存目录，执行 pio pkg update / 删除 .pio /
  新机器首次构建时会被整体替换，用户手动放入的 lv_conf.h 会丢失。
  本脚本在每次 pio run 构建开始时自动把仓库 firmware/lv_conf.h 复制回去。

用法：
  platformio.ini 的 [env:xxx] 段添加：
      extra_scripts = scripts/ensure_lv_conf.py
"""
Import("env")  # noqa: F821  (PlatformIO 注入的 SCons 环境)

import os
import shutil


def _sync_lv_conf():
    project_dir = os.path.abspath(env.subst("$PROJECT_DIR"))  # noqa: F821
    # 仓库权威版：firmware_pio 的上一级是仓库根，lv_conf.h 在 firmware/ 下（git 追踪）。
    source = os.path.abspath(os.path.join(project_dir, "..", "firmware", "lv_conf.h"))

    if not os.path.isfile(source):
        print("[ensure_lv_conf] SKIP: source not found: %s" % source)
        return

    # libdeps 根目录：优先用 PlatformIO 变量，缺失时回退到项目内默认位置。
    libdeps_root = env.subst("$PROJECT_LIBDEPS_DIR")  # noqa: F821
    if not libdeps_root or libdeps_root.startswith("$"):
        libdeps_root = os.path.join(project_dir, ".pio", "libdeps")

    if not os.path.isdir(libdeps_root):
        print("[ensure_lv_conf] SKIP: libdeps not installed yet: %s" % libdeps_root)
        return

    for env_name in sorted(os.listdir(libdeps_root)):
        lvgl_dir = os.path.join(libdeps_root, env_name, "lvgl")
        if not os.path.isdir(lvgl_dir):
            continue
        target = os.path.join(lvgl_dir, "lv_conf.h")
        try:
            shutil.copy2(source, target)
            print("[ensure_lv_conf] OK: %s -> %s" % (source, target))
            return
        except OSError as exc:
            print("[ensure_lv_conf] ERROR: %s" % exc)

    print("[ensure_lv_conf] SKIP: lvgl lib not installed yet")


_sync_lv_conf()
