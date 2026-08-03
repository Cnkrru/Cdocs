# -*- coding: utf-8 -*-
"""Vercel Analytics 插件（Cdocs 插件架构：外部脚本 + JSON 文件交换）。

钩子：on_config —— 构建配置阶段调用一次，返回按语言的「正文末尾注入片段」。

静态站点接入 Vercel Web Analytics 的官方方式（无需 npm 打包器）：
    <script defer src="/_vercel/insights/script.js"></script>
该端点由 Vercel 托管环境自动提供；非 Vercel 平台（本地/GitHub Pages）请求会 404，无害。

启用：config.json 的 center.analytics.vercel = true。
"""
import json
import os
import sys

SCRIPT = '<script defer src="/_vercel/insights/script.js"></script>\n'


def load_json(path):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def write_out(path, obj):
    with open(path, "w", encoding="utf-8") as f:
        json.dump(obj, f, ensure_ascii=False, indent=2)
    return 0


def main():
    if len(sys.argv) < 3:
        return 1
    ctx_path, out_path = sys.argv[1], sys.argv[2]
    try:
        ctx = load_json(ctx_path)
        engine = ctx.get("engine", "")
        cfg_path = os.path.join(engine, "config", "config.json")
        if not os.path.exists(cfg_path):
            return 0
        cfg = load_json(cfg_path)
        site = cfg.get("site", cfg) if isinstance(cfg, dict) else {}
        center = cfg.get("center", {}) if isinstance(cfg, dict) else {}
        an = center.get("analytics") if isinstance(center, dict) else None
        enabled = isinstance(an, dict) and an.get("vercel") is True
        if not enabled:
            return write_out(out_path, {"ok": True, "message": "center.analytics.vercel 未启用，跳过"})

        locales = list((site.get("i18n", {}).get("locales", {}) or {}).keys()) or ["zh-CN"]
        inject = {loc: "\n<!-- Vercel Analytics（插件注入） -->\n" + SCRIPT for loc in locales}
        return write_out(out_path, {
            "ok": True,
            "message": "已注入 Vercel Analytics（{}）".format(", ".join(locales)),
            "inject": inject,
        })
    except Exception as e:  # 失败隔离
        try:
            return write_out(out_path, {"ok": False, "message": "vercel_analytics.py: {}".format(e)})
        except Exception:
            return 1


if __name__ == "__main__":
    sys.exit(main())
