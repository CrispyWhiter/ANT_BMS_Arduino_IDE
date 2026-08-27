# -*- coding: utf-8 -*-
"""
ensure_lv_conf.py —— 每次构建前把本工程 include/lv_conf.h 同步到 lvgl 库目录。

背景：
  LVGL 8.4 需要 lv_conf.h 位于 lvgl 库目录内（library.json 的 includeDir "." +
  src/lv_conf_internal.h 内部的 "../../lv_conf.h" 相对路径）才能被正确加载。
  但 .pio/libdeps 是 PlatformIO 依赖缓存目录，执行 pio pkg update / 删除 .pio /
  新机器首次构建时会被整体替换，用户手动放入的 lv_conf.h 会丢失。
  本脚本在每次 pio run 构建开始时自动把本工程 include/lv_conf.h（权威副本，
  git 追踪）复制回去。

用法：
  platformio.ini 的 [env:xxx] 段添加：
      extra_scripts = scripts/ensure_lv_conf.py
"""
Import("env")  # noqa: F821  (PlatformIO 注入的 SCons 环境)

import os
import shutil


def _sync_lv_conf():
    project_dir = os.path.abspath(env.subst("$PROJECT_DIR"))  # noqa: F821
    # 权威副本：本工程 include/lv_conf.h（不依赖仓库中老的 firmware/ 目录）。
    source = os.path.join(project_dir, "include", "lv_conf.h")

    if not os.path.isfile(source):
        print("[ensure_lv_conf] SKIP: source not found: %s" % source)
        return

    # 只同步【当前正在构建的 env】对应的 libdeps 目录：
    #   $PROJECT_LIBDEPS_DIR/$PIOENV/lvgl/lv_conf.h
    # 不能遍历 libdeps 根下所有 env（sorted 后 n16r8 总排最前，会导致
    # 构建 n4r2 时误把 lv_conf.h 同步到 n16r8 的目录）。
    libdeps_dir = os.path.join(
        env.subst("$PROJECT_LIBDEPS_DIR"),  # noqa: F821
        env.subst("$PIOENV"),               # noqa: F821
    )
    if not libdeps_dir or libdeps_dir.startswith("$"):
        libdeps_dir = os.path.join(project_dir, ".pio", "libdeps", env.subst("$PIOENV"))  # noqa: F821

    lvgl_dir = os.path.join(libdeps_dir, "lvgl")
    if not os.path.isdir(lvgl_dir):
        print("[ensure_lv_conf] SKIP: lvgl lib not installed in %s" % lvgl_dir)
        return

    target = os.path.join(lvgl_dir, "lv_conf.h")
    try:
        shutil.copy2(source, target)
        print("[ensure_lv_conf] OK: %s -> %s" % (source, target))
    except OSError as exc:
        print("[ensure_lv_conf] ERROR: %s" % exc)


_sync_lv_conf()
