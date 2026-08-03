#!/usr/bin/env python3
# vercel 部署插件：生成 web/vercel.json + .github/workflows/build-linux-binary.yml
#
# 背景：Vercel 云端构建环境没有 C++ 编译器（现场编译失败过），
# 方案 = GitHub Actions 预编译 Linux 版 Cdocs-linux 二进制提交回仓库（web/Cdocs-linux），
# Vercel 只运行现成二进制（chmod +x Cdocs-linux && ./Cdocs-linux build）。
# 注意：Vercel 项目 Root Directory 需设为 web/，vercel.json 因此生成到 web/ 下，
#       buildCommand / outputDirectory 均相对 web/ 解析。
#
# 用法（由 Cdocs deploy --setup 调用）：setup.py <ctx.json> <out.json>
#   ctx.source = 项目根目录
# 幂等：文件已存在且内容一致则跳过；不一致则覆盖。
import json
import os
import sys

def read_json(p):
    try:
        with open(p, encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        return {}

def write_out(out_path, ok, msg):
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump({"ok": ok, "message": msg}, f, ensure_ascii=False)

VERCEL_JSON = """{
  "$schema": "https://openapi.vercel.sh/vercel.json",
  "framework": null,
  "buildCommand": "chmod +x Cdocs-linux && ./Cdocs-linux build",
  "outputDirectory": "dist"
}
"""

BUILD_LINUX_YML = """# 自动编译 Linux 版 Cdocs 二进制并提交回仓库（供 Vercel 等云端直接运行，无需现场编译）
# 由 Cdocs deploy --setup 的 vercel 插件生成。产物提交带 [skip ci] 避免循环触发。
name: Build Linux Binary

on:
  push:
    branches: [main]
  workflow_dispatch:

permissions:
  contents: write

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - name: 安装构建工具（g++ 等）
        run: sudo apt-get update && sudo apt-get install -y build-essential

      - name: 编译 Linux 版 Cdocs（全静态链接，跨发行版可运行）
        run: bash web/.Cdocs/tools/build.sh

      - name: 提交 Cdocs-linux 到仓库
        run: |
          cp Cdocs web/Cdocs-linux
          chmod +x web/Cdocs-linux
          git config user.name "github-actions[bot]"
          git config user.email "github-actions[bot]@users.noreply.github.com"
          git add web/Cdocs-linux
          if git diff --cached --quiet; then
            echo "二进制无变化，跳过提交"
          else
            git commit -m "chore: 更新 Linux 构建产物 [skip ci]"
            git push "https://x-access-token:${{ github.token }}@github.com/${{ github.repository }}.git" HEAD:main
          fi
"""

def ensure(path, content, out, name):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            if f.read() == content:
                return f"  · {name} 已存在且一致，跳过\n"
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(content)
    return f"  · {name} 已生成\n"

def main():
    if len(sys.argv) < 3:
        return
    ctx_path, out_path = sys.argv[1], sys.argv[2]
    ctx = read_json(ctx_path)
    source = ctx.get("source", ".")

    msgs = []
    # vercel.json 生成到 web/ 下（Vercel Root Directory = web/）
    msgs.append(ensure(os.path.join(source, "web", "vercel.json"), VERCEL_JSON, out_path, "web/vercel.json"))
    msgs.append(ensure(os.path.join(source, ".github", "workflows", "build-linux-binary.yml"),
                       BUILD_LINUX_YML, out_path, ".github/workflows/build-linux-binary.yml"))

    # 检查 web/Cdocs-linux 二进制是否已存在（首次 setup 时提示）
    binp = os.path.join(source, "web", "Cdocs-linux")
    hint = ""
    if not os.path.exists(binp):
        hint = "（注意：web/Cdocs-linux 不存在——push 后 Actions 会编译生成并提交，首次 Vercel 部署会失败一次，随后自动成功）"

    write_out(out_path, True, "".join(msgs).strip() + hint)

if __name__ == "__main__":
    main()
