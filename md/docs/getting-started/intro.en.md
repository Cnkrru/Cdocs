# Introduction

Welcome to **Cdocs** — a minimal, data-driven static documentation site generator (SSG) written in C++.

It compiles the Markdown sources in `md/docs/` together with the config, navigation, i18n strings and front-end assets in `.Cdocs/` into a **purely static, offline-capable, zero-runtime-dependency** site under `dist/`.

## What problem it solves

Traditional doc tools either require a Node/Python runtime or scatter their configuration so widely that restyling means touching source code. Cdocs is built around a different set of goals:

- **Single-binary CLI**: one `Cdocs.exe` (or `Cdocs`) does "compile → generate → RSS → PWA"; no Node/Python needed to `serve` a preview.
- **Data-driven**: site title, navigation, theme and languages are all expressed in JSON — change copy/structure without touching C++.
- **Offline-capable**: third-party libraries (Mermaid / KaTeX / highlight.js / FlexSearch) are shipped with the site, so it works without a network.
- **SEO-friendly**: JSON-LD, canonical, sitemap `hreflang` and per-language search indexes are emitted at build time.

## Core features

- **CLI**: `init` / `new` / `section` / `build` / `serve` / `deploy` / `clean` + diagnostics `doctor` / `check` / `config` / `routes` / `theme` / `plugins` / `versions`
- **Content**: Markdown (md4c based, with GFM extensions: tables, task lists, strikethrough); single `md/` root (docs + docs-v<v> versions + blog)
- **Navigation**: config-driven per-area sidebar files (`route/`), up to 6 nesting levels, collapsible groups, mobile drawer
- **Search**: client-side full-text search (FlexSearch) with title/body fields and hit highlighting
- **Themes**: multi-theme repository (`themes/`: ink / paper / frost), light/dark dual theme, one-click skinning via `themeName`
- **Enhancements**: code highlighting / copy button, Admonitions, `Mermaid` diagrams, KaTeX math
- **Shortcodes**: `<Tabs/>` `<Expand/>` `<CodeGroup/>` `<Badge/>` inline components (self-contained styles)
- **Experience**: ⌘K command palette, image lightbox, print / export PDF, "was this page helpful?" feedback
- **i18n**: `{{key}}` + flat JSON dictionaries
- **Versioning**: multi-version docs (`site.versions` explicit + snapshot convention), switcher keeps locale
- **Plugins**: data queries (blog feed / tag aggregation) 100% via Python plugins; `on_config` / `on_data_query` / `on_page_rendered` / `on_done` / `setup` hooks
- **Publishing**: per-page SEO, sitemap, robots, RSS 2.0 / JSON Feed, PWA offline

## Three steps to get started

```bash
# Option A: global install (add release/ package to PATH)
Cdocs init mysite         # scaffold site (copies engine + auto-builds, ready to view)
cd mysite
Cdocs serve               # local preview (built-in C++ server, default http://localhost:8088)

# Option B: build from source
.Cdocs\tools\build.cmd    # Windows one-shot build (compile -> generate -> RSS -> PWA)
bash .Cdocs/tools/build.sh  # Linux / macOS
```

> Tip: `serve` ships a C++ HTTP server, so **no Python or Node install is required**; it only listens on `127.0.0.1`. Re-run after editing `md/` to refresh the preview.

## Where to go next

- For command usage and authoring syntax, read the [Guide](guide.html).
- For CLI flags and config-file fields, read the [API Reference](../reference/api.html).
- For the full "Markdown to live site" pipeline, read the [Render Pipeline](../generator/pipeline.html).
