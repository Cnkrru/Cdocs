---
title: "Building Cdocs: from a single-file generator to a shippable product"
date: 2026-08-04
tags: [C++, static-site, engineering]
---

Cdocs is a static documentation site generator written in C++17: a single executable, zero runtime dependencies, and fully data-driven. This post documents the key design decisions that turned it from "a script that runs" into "a product that deploys" — the engineering story behind every page on this site.

## Why write a doc generator in C++?

Hugo, Docusaurus and VitePress are all mature. The core requirement was simple: **zero runtime dependencies**.

- Hugo is a Go binary — but its theme language (templates + shortcodes) is its own DSL;
- Docusaurus/VitePress need a Node runtime and a heavy build chain;
- We wanted: `Cdocs.exe build` runs on any machine — **no Node, no Python, no Go**.

Static linking (`-static -static-libgcc -static-libstdc++`) delivers exactly that: [md4c](https://github.com/mity/md4c) for Markdown (C library), [nlohmann/json](https://github.com/nlohmann/json) (header-only) for JSON, libwebp for image compression, zlib for gzip — all statically linked into one 10MB executable.

## Data-driven: the engine hardcodes nothing

The core design is **JSON map-driven rendering**. Page structure lives in JSON, not C++:

```json
{ "component": "TopNav", "if": "header.topnav",
  "sections": [ { "component": "TopNavLink", "each": "header.topnav" } ] }
```

The engine does two things: **produce data** (nav tree, TOC, body, pagination — all JSON) and **compose pages** (combine components per the map). Conditions, loops and nesting are all JSON fields; components are plain HTML + `{{data holes}}` — no template control-flow syntax.

The payoff: **switching themes = swapping one folder** (`themes/ink` → `themes/frost`); adding a page type = one entry in `map.json`. No C++ changes.

## Queries are 100% plugin-driven

The most counter-intuitive decision: **query logic (blog feed ordering, tag aggregation) is not in C++ at all** — it lives in Python plugins (`.Cdocs/plugins/*/scripts/*.py`).

The engine dumps a full data snapshot (`ctx.json`), invokes the plugin script, the plugin writes results back to `out.json`, and the engine renders from that:

```python
# blog-query plugin: filter blog/* posts, newest first, paginate
posts = [p for p in ctx.get("pages", [])
         if p.get("file", "").startswith("blog/") and not p.get("draft")]
posts.sort(key=lambda p: p.get("dateT_iso") or "", reverse=True)
order = [p["file"] for p in posts]
out = {"ok": True, "blog_order": order, "blog_pages": [...]}
```

Why? Because **query rules change** (items per page, home feed size, pinned posts). In-engine, every change means a recompile; as a plugin, it's a script edit. The protocol is "external process + JSON file exchange", failures are isolated, and a plugin never blocks a build.

## Versioning: explicit config first, convention as fallback

For multi-version docs (v1/v2), Cdocs follows the Docusaurus style:

- **Explicit**: `site.versions` in `config.json` (name/label/source/default), the engine dispatches per version;
- **Convention**: without config, sibling `<source>-*` snapshot dirs are auto-detected as historical versions.

Each version builds as a complete independent site (own i18n / RSS / PWA), with a header version switcher that keeps the current locale. The pitfall: **multi-version shares one config, and old versions legitimately lack new pages** — the nav filter must drop links to non-existent pages, or you get dead links everywhere (our v1 is just a placeholder page, a classic case).

## Directory refactor: separating web/ and bin/

As the project matured, we reorganized the repo into two clean zones:

```
Cdocs/
├── src/    # engine C++ sources
├── web/    # site root: md/ + .Cdocs/ + themes/ + vercel.json
└── bin/    # distro package: Cdocs.exe + serve.bat + .Cdocs (clone & use)
```

- `web/` is a **self-contained site root**: `cd web && Cdocs build`;
- `bin/` is a **ready-to-use distro**: add to PATH, then `Cdocs init/build/serve` anywhere;
- CI does `bash web/.Cdocs/tools/build.sh` — compile the generator + build the site in one step.

The biggest lesson from this refactor is **path references**: every `src/`, `Cdocs`, `dist` reference in build scripts, deployment workflows and plugin templates had to follow the new layout — miss one, and CI fails one step at a time (we hit three in a row: missing `../Cdocs`, missing `build` arg, wrong `cp Cdocs` source path).

## The CI pitfall: the "missing CSS" mystery

At launch we hit a weird bug: **CSS styles vanished after deployment**. The root-cause chain:

1. `build.sh` ran `./Cdocs` with no args — prints help, **doesn't build** (exit 0 but no dist output);
2. `deploy.yml` added `cd web && ../Cdocs build`, but `../Cdocs` didn't exist → exit 127;
3. So **every deployment failed**, and GitHub Pages kept serving the oldest successful single-version artifact — which looked like "CSS is gone".

Lesson: **CI "success" must verify artifacts exist, not just exit codes**. Now `build.sh` runs `./Cdocs build` explicitly, the workflow uploads `web/dist`, and production matches local.

## Where it stands today

- 25 C++ modules, no function over 150 lines;
- 3 themes (ink / paper / frost);
- 6 plugins (query / comments / analytics / deployment);
- Two versions (v2 current + v1 snapshot), bilingual zh-CN/en;
- GitHub Actions auto-deploys to Pages on every push.

If you're also building a "small but beautiful" tool, Cdocs' source and docs are on GitHub — feel free to look around.
