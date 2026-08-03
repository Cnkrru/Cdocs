#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""blog-query 插件：博客流查询（on_data_query 钩子）。
引擎只产原始数据（数据快照），本文档决定"取哪些、什么顺序、怎么分页"。
输入 ctx.json：{pages: [{file, title, date, dateT_iso, tags, weight, draft}]}
输出 out.json：{
  ok, blog_order: [有序 file], blog_pages: [[每页 file]], home_posts: [首页流前 N]
}
file 以 "blog/" 前缀 = 博客文章（引擎收集时已带前缀）。"""
import json
import sys

PER_PAGE = 10      # 列表页每篇数
HOME_TOP = 8       # 首页文章流条数


def main():
    if len(sys.argv) < 3:
        print("用法: blog_query.py <ctx.json> <out.json>", file=sys.stderr)
        sys.exit(1)
    with open(sys.argv[1], encoding='utf-8') as f:
        ctx = json.load(f)

    posts = [p for p in ctx.get("pages", [])
             if p.get("file", "").startswith("blog/") and not p.get("draft")]

    # 按发布日期倒序；dateT_iso 缺失（无 date 且无 mtime）回退 date 字符串再回退 file
    posts.sort(key=lambda p: (p.get("dateT_iso") or "", p.get("date") or "", p.get("file") or ""),
               reverse=True)
    order = [p["file"] for p in posts]

    out = {"ok": True}
    out["blog_order"] = order
    out["blog_pages"] = [order[i:i + PER_PAGE] for i in range(0, len(order), PER_PAGE)]
    out["home_posts"] = order[:HOME_TOP]

    with open(sys.argv[2], 'w', encoding='utf-8') as f:
        json.dump(out, f, ensure_ascii=False, indent=2)


if __name__ == "__main__":
    main()
