#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""制作 release 发布包：Cdocs.exe + .Cdocs（引擎资源），排除编译中间产物与编译期头文件。"""
import os, shutil, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))  # Cdocs 项目根
REL  = os.path.join(ROOT, "release")
SRC  = os.path.join(ROOT, ".Cdocs")

# 发布包排除：.build（编译中间产物）、deps/vendor（编译期头文件，运行不需要）
EXCLUDE_DIRS = {".build"}
EXCLUDE_REL = {os.path.join("deps", "vendor")}

def should_skip(rel):
    for ex in EXCLUDE_DIRS:
        if rel == ex or rel.startswith(ex + os.sep):
            return True
    for ex in EXCLUDE_REL:
        if rel == ex or rel.startswith(ex + os.sep):
            return True
    return False

def copy_tree(src, dst, base_rel):
    for name in os.listdir(src):
        s = os.path.join(src, name)
        rel = os.path.join(base_rel, name) if base_rel else name
        if os.path.isdir(s):
            if should_skip(rel):
                print(f"  跳过 {rel}/")
                continue
            d = os.path.join(dst, name)
            os.makedirs(d, exist_ok=True)
            copy_tree(s, d, rel)
        else:
            shutil.copy2(s, os.path.join(dst, name))

def main():
    # 覆盖合并模式：不整目录删除（避免 safe-delete 回收站不可用时中断），
    # 逐文件 copy2 覆盖，目录 makedirs(exist_ok) 合并。排除项本就该不存在，
    # 若历史残留（如旧 .build），额外尝试清理（失败不影响发布包正确性）。
    os.makedirs(REL, exist_ok=True)

    # 1) Cdocs.exe
    exe = os.path.join(ROOT, "Cdocs.exe")
    if not os.path.exists(exe):
        print("错误：未找到 Cdocs.exe，请先编译"); sys.exit(1)
    shutil.copy2(exe, os.path.join(REL, "Cdocs.exe"))
    print(f"✔ Cdocs.exe ({os.path.getsize(exe)/1024/1024:.1f} MB)")

    # 2) .Cdocs 引擎资源
    os.makedirs(os.path.join(REL, ".Cdocs"), exist_ok=True)
    copy_tree(SRC, os.path.join(REL, ".Cdocs"), "")

    # 3) 尽力清理历史排除残留（存在才删；被安全钩子拦则跳过）
    for rel in list(EXCLUDE_REL) + list(EXCLUDE_DIRS):
        p = os.path.join(REL, ".Cdocs", rel)
        if os.path.exists(p):
            try:
                if os.path.isdir(p):
                    shutil.rmtree(p)
                else:
                    os.remove(p)
            except OSError:
                print(f"  （跳过清理 {rel}：被安全钩子拦截）")

    # 统计发布包体积
    total = 0
    for r, _, fs in os.walk(REL):
        for f in fs:
            total += os.path.getsize(os.path.join(r, f))
    print(f"✔ release/ 就绪，共 {total/1024/1024:.1f} MB")
    for sub in sorted(os.listdir(REL)):
        print(f"   - {sub}")

if __name__ == "__main__":
    main()
