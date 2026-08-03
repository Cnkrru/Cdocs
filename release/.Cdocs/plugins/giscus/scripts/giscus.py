# -*- coding: utf-8 -*-
"""giscus 评论插件（Cdocs 插件架构：外部脚本 + JSON 文件交换）。

钩子：on_config —— 构建配置阶段调用一次，返回按语言的「正文末尾注入片段」。
引擎只提供通用注入能力（on_config 输出 inject: {语言: HTML}），不感知评论细节。

配置：读取 .Cdocs/config/config.json 的 center.comments（giscus 参数）。
自定义主题：site.url 非空时 data-theme 指向站内 giscus-light/dark.css 并带
data-custom-theme 标记（前端 theme.js 据此在明暗切换时同步 iframe）。
"""
import json
import os
import sys


def load_json(path):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def main():
    if len(sys.argv) < 3:
        return 1
    ctx_path, out_path = sys.argv[1], sys.argv[2]
    try:
        ctx = load_json(ctx_path)
        engine = ctx.get("engine", "")
        cfg_path = os.path.join(engine, "config", "config.json")
        if not os.path.exists(cfg_path):
            return 0  # 无引擎配置，静默跳过
        cfg = load_json(cfg_path)
        site = cfg.get("site", cfg) if isinstance(cfg, dict) else {}
        center = cfg.get("center", {})
        cm = center.get("comments") if isinstance(center, dict) else None

        # 评论未配置或配置不完整 → 不注入（ok=true 静默）
        if not isinstance(cm, dict):
            return write_out(out_path, {"ok": True, "message": "未配置 center.comments，跳过"})
        repo, repo_id = cm.get("repo", ""), cm.get("repoId", "")
        cat_id = cm.get("categoryId", "")
        if not (repo and repo_id and cat_id):
            return write_out(out_path, {"ok": True, "message": "comments 配置不完整，跳过"})

        category = cm.get("category", "Announcements")
        mapping = cm.get("mapping", "pathname")
        url = (site.get("url") or "").rstrip("/")
        theme_mode = site.get("theme", "dark")
        locales = list((site.get("i18n", {}).get("locales", {}) or {}).keys()) or ["zh-CN"]

        inject = {}
        for loc in locales:
            theme = "preferred_color_scheme"
            custom = ""
            if url:
                tone = "dark" if theme_mode == "dark" else "light"
                theme = "{}/{}/assets/css/giscus-{}.css".format(url, loc, tone)
                custom = ' data-custom-theme="1"'
            html = (
                "\n<!-- giscus 评论（插件注入） -->\n"
                '<section class="giscus-wrap" data-plugin="giscus">\n'
                '  <div class="giscus"></div>\n'
                '  <script src="https://giscus.app/client.js"\n'
                '    data-repo="{}"\n'
                '    data-repo-id="{}"\n'
                '    data-category="{}"\n'
                '    data-category-id="{}"\n'
                '    data-mapping="{}"\n'
                '    data-strict="0"\n'
                '    data-reactions-enabled="1"\n'
                '    data-emit-metadata="0"\n'
                '    data-input-position="bottom"\n'
                '    data-theme="{}"{} \n'
                '    data-lang="{}"\n'
                '    crossorigin="anonymous" async>\n'
                "  </script>\n"
                "</section>\n"
            ).format(repo, repo_id, category, cat_id, mapping, theme, custom, loc)
            inject[loc] = html

        return write_out(out_path, {
            "ok": True,
            "message": "已注入 giscus 评论（{}）".format(", ".join(inject.keys())),
            "inject": inject,
        })
    except Exception as e:  # 失败隔离：任何异常只报 warning，不阻断构建
        try:
            return write_out(out_path, {"ok": False, "message": "giscus.py: {}".format(e)})
        except Exception:
            return 1


def write_out(path, obj):
    with open(path, "w", encoding="utf-8") as f:
        json.dump(obj, f, ensure_ascii=False, indent=2)
    return 0


if __name__ == "__main__":
    sys.exit(main())
