#!/usr/bin/env python3
# github-pages 部署插件：生成 .github/workflows/deploy.yml（GitHub Actions 自动构建发布到 Pages）
#
# 用法（由 Cdocs deploy --setup 调用）：setup.py <ctx.json> <out.json>
#   ctx.source = 项目根目录（生成文件写到这里）
# 幂等：文件已存在且内容一致则跳过；不一致则覆盖（文件由本插件管理）。
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

DEPLOY_YML = """# Cdocs 自动部署：源码 push 后自动构建并发布到 GitHub Pages
# 由 Cdocs deploy --setup 的 github-pages 插件生成（引擎插件化，改引擎不涉及本文件）
# 依赖：仓库 Settings → Pages → Build and deployment → Source 选 "GitHub Actions"
name: Deploy

on:
  push:
    branches: [main]
  workflow_dispatch:

permissions:
  contents: read
  pages: write
  id-token: write

concurrency:
  group: pages
  cancel-in-progress: true

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - name: 安装构建工具（g++ 等）
        run: sudo apt-get update && sudo apt-get install -y build-essential

      - name: 编译 Cdocs 生成器（build.sh 全模块 + libwebp + zlib）
        run: bash web/.Cdocs/tools/build.sh

      - name: 生成静态站点
        run: cd web && ../Cdocs build

      - name: 上传产物
        uses: actions/upload-pages-artifact@v3
        with:
          path: web/dist

  deploy:
    needs: build
    runs-on: ubuntu-latest
    environment:
      name: github-pages
      url: ${{ steps.deployment.outputs.page_url }}
    steps:
      - id: deployment
        uses: actions/deploy-pages@v4
"""

def main():
    if len(sys.argv) < 3:
        return
    ctx_path, out_path = sys.argv[1], sys.argv[2]
    ctx = read_json(ctx_path)
    source = ctx.get("source", ".")
    target = os.path.join(source, ".github", "workflows", "deploy.yml")

    os.makedirs(os.path.dirname(target), exist_ok=True)

    # 幂等：内容一致跳过
    if os.path.exists(target):
        with open(target, encoding="utf-8") as f:
            if f.read() == DEPLOY_YML:
                write_out(out_path, True, "deploy.yml 已存在且一致，跳过")
                return

    with open(target, "w", encoding="utf-8", newline="\n") as f:
        f.write(DEPLOY_YML)
    write_out(out_path, True, f"已生成 .github/workflows/deploy.yml（GitHub Pages 自动部署）")

if __name__ == "__main__":
    main()
