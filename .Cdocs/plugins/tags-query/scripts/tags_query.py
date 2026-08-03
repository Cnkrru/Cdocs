#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""tags-query 插件：标签聚合查询（on_data_query 钩子）。
从数据快照聚合全部文章（文档 + 博客）的 frontmatter tags，排除草稿。
输出 out.json：{
  ok,
  tags: [{name, href}],                    # 标签总览（href = slugify(name)+.html，相对 tags/ 目录）
  tag_pages: {name: [有序 file 列表]}       # 每标签文章列表（博客按 date 倒序在前，文档按原序在后）
}
slugify 与引擎 C++ 版一致：小写/数字/非 ASCII 保留、大写转小写、空白/分隔符转 '-'、其余标点忽略、折叠连续连字符。"""
import json
import sys


def slugify(s):
    out = []
    for ch in s:
        o = ord(ch)
        if o >= 0x80:
            out.append(ch)
        elif 'a' <= ch <= 'z' or '0' <= ch <= '9':
            out.append(ch)
        elif 'A' <= ch <= 'Z':
            out.append(ch.lower())
        elif ch in ' -_/':
            out.append('-')
        # 其余标点忽略
    res = []
    prev = ''
    for ch in out:
        if ch == '-' and prev == '-':
            continue
        res.append(ch)
        prev = ch
    return ''.join(res).strip('-')


def main():
    if len(sys.argv) < 3:
        print("用法: tags_query.py <ctx.json> <out.json>", file=sys.stderr)
        sys.exit(1)
    with open(sys.argv[1], encoding='utf-8') as f:
        ctx = json.load(f)

    pages = [p for p in ctx.get("pages", []) if not p.get("draft")]
    by_file = {p["file"]: p for p in pages}

    tag_map = {}
    for p in pages:
        for t in p.get("tags", []):
            tag_map.setdefault(t, []).append(p["file"])

    out = {"ok": True}
    out["tags"] = [{"name": k, "href": slugify(k) + ".html"} for k in sorted(tag_map)]

    out["tag_pages"] = {}
    for name, files in tag_map.items():
        blog = sorted((f for f in files if f.startswith("blog/")),
                      key=lambda f: by_file.get(f, {}).get("dateT_iso") or "",
                      reverse=True)
        docs = [f for f in files if not f.startswith("blog/")]
        out["tag_pages"][name] = blog + docs

    with open(sys.argv[2], 'w', encoding='utf-8') as f:
        json.dump(out, f, ensure_ascii=False, indent=2)


if __name__ == "__main__":
    main()
