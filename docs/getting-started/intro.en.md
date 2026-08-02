# Introduction

Welcome to **Cdocs** — a minimal, data-driven static documentation site generator (SSG) written in C++.

It compiles the Markdown sources in `docs/` together with the config, navigation, i18n strings and front-end assets in `.Cdocs/` into a **purely static, offline-capable, zero-runtime-dependency** site under `dist/`.

## What problem it solves

Traditional doc tools either require a Node/Python runtime or scatter their configuration so widely that restyling means touching source code. Cdocs is built around a different set of goals:

- **Single-binary CLI**: one `Cdocs.exe` (or `Cdocs`) does "compile → generate → RSS → PWA"; no Node/Python needed to `serve` a preview.
- **Data-driven**: site title, navigation, theme and languages are all expressed in JSON — change copy/structure without touching C++.
- **Offline-capable**: third-party libraries (Mermaid / KaTeX / highlight.js / FlexSearch) are shipped with the site, so it works without a network.
- **SEO-friendly**: JSON-LD, canonical, sitemap `hreflang` and per-language search indexes are emitted at build time.

## Core features

- **CLI**: `build` / `serve` / `new` / `version` / `help` subcommands
- **Content**: Markdown (md4c based, with GFM extensions: tables, task lists, strikethrough)
- **Navigation**: config-driven sidebar, up to 6 nesting levels, collapsible groups, mobile drawer
- **Search**: client-side full-text search (FlexSearch) with title/body fields and hit highlighting
- **Theme**: light/dark dual theme, follows system on first visit, remembers preference
- **Enhancements**: code highlighting / copy button, Admonitions, `Mermaid` diagrams, KaTeX math
- **Experience**: ⌘K command palette, image lightbox, print / export PDF, "was this page helpful?" feedback
- **i18n**: `{{key}}` + flat JSON dictionaries
- **Publishing**: per-page SEO, sitemap, robots, RSS 2.0 / JSON Feed, PWA offline

## Three steps to get started

```bash
# 1) One-shot build (auto-compile generator -> generate site -> RSS -> PWA)
.Cdocs\tools\build.cmd          # Windows
bash .Cdocs/tools/build.sh      # Linux / macOS

# 2) Local preview (built-in C++ static server, default http://localhost:8088)
Cdocs serve

# 3) Scaffold a standalone site (copies engine + Cdocs.exe + sample docs/intro.md)
Cdocs new my-docs
```

> Tip: `serve` ships a C++ HTTP server, so **no Python or Node install is required**; it only listens on `127.0.0.1`. Re-run after editing `docs/` to refresh the preview.

## Where to go next

- For command usage and authoring syntax, read the [Guide](guide.html).
- For CLI flags and config-file fields, read the [API Reference](../reference/api.html).
- For the full "Markdown to live site" pipeline, read the [Render Pipeline](../generator/pipeline.html).
