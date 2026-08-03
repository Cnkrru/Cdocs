# API Reference

Cdocs is a **CLI-driven static documentation site generator** with no public C++ library API. All extensibility is exposed through three layers:

| Layer | Location | Purpose |
| --- | --- | --- |
| Config | `.Cdocs/config/config.json` + `route/` (per-area sidebars) | Site metadata, feature toggles, navigation |
| Theme | `themes/<name>/` | Page maps (`map/*.json`) + components (`components/`) + frontend assets (`assets/`); switch theme = change folder + set `themeName` |
| Plugin | `.Cdocs/plugins/<name>/plugin.json` | External scripts hooked into the build lifecycle |

---

## 1. Commands

| Command | Description | Common flags |
| --- | --- | --- |
| `Cdocs init <dir>` | Scaffold a complete site (config/route/i18n/sample docs/theme assets) and build it | `--no-engine` content-only skeleton |
| `Cdocs new <page>` | Create a page from `archetypes/default.md` and register it in navigation (alias `add`/`page`) | — |
| `Cdocs build [src] [dst]` | Build the site (default `md` → `dist`) | `-D/--drafts`, `--clean` |
| `Cdocs serve` | Build and start the built-in preview server (default port `8088`) | `-p/--port`, `-o/--open`, `-w/--watch`, `--no-build` |
| `Cdocs deploy` | Build & publish (gh-pages branch / Vercel) | `--remote <url>`, `--branch <b>`, `-m <msg>`, `--force`, `--vercel`, `--setup` |
| `Cdocs clean` | Wipe the output dir (like `jekyll clean`) | — |
| `Cdocs section <blog|docs|md-v<n>>` | Add a content area (category); blog/docs unique, versions multiple | — |
| `Cdocs doctor` | Environment & config self-check (config/areas/theme/tools) | — |
| `Cdocs check` | Site quality check: broken links + token leftovers + unrendered data holes | — |
| `Cdocs config` | Print parsed config summary (diagnostic) | — |
| `Cdocs routes` | List page routes registered in sidebar | — |
| `Cdocs version` / `-v` | Print version | — |
| `Cdocs help` / `-h` | Print help | — |

### deploy

```
Cdocs deploy [--remote <url>] [--branch <b>] [-m <msg>] [--force] [--vercel]
```

**GitHub Pages (default)** — built-in git calls, zero external scripts:

1. Checks `git` availability.
2. **Builds** the site (full build; if `dist/.git` exists it is not cleaned so deployment history is preserved).
3. Writes `dist/.nojekyll` (bypass Jekyll on GitHub Pages).
4. Git ops inside `dist`: `init` (orphan branch) if needed, switch to target branch, `add -A` + `commit` (skipped when nothing changed).
5. **Remote resolution**: `--remote` > config `site.deploy.remote` > existing `origin` > inferred from `site.url` (`https://user.github.io/repo/` → `https://github.com/user/repo.git`).
6. `git push -u origin <branch>`.

**Vercel (`--vercel`)** — build then publish via the Vercel CLI:

- Runs `vercel --prod --yes dist` (auto-detects `vercel` on PATH, falls back to `npx --yes vercel@latest`).
- Requires Node.js + Vercel CLI: first-time `npm i -g vercel && vercel login` (or set `VERCEL_TOKEN`).
- First deploy prompts to link a project; afterwards one command publishes. The deploy URL is printed by the CLI.

```bash
Cdocs deploy
Cdocs deploy --remote https://github.com/me/docs.git --branch gh-pages -m "docs: v2.0"
Cdocs deploy --force
Cdocs deploy --vercel
Cdocs deploy --setup
```

`--setup` generates platform deployment files via plugins (see `plugins.en.md` → Deployment plugins): scans `.Cdocs/plugins/` for the `setup` hook, each plugin writes `.github/workflows/*.yml` / `vercel.json` to the project root (idempotent). Commit them and pushes auto-deploy via GitHub Actions / Vercel.

Global flags (before the subcommand): `-c/--config`, `-s/--source`, `-d/--dest`, `-q/--quiet`, `-V/--verbose`. Exit codes: `0` ok, `1` runtime error, `2` usage error.

---

## 2. Config (config.json)

Located at `.Cdocs/config/config.json`, organized in blocks: `site` (global) / `head` / `center` / `footer`. Legacy flat top-level config is still parsed.

```json
{
  "site": {
    "title": "{{siteTitle}}",
    "description": "{{siteDesc}}",
    "theme": "dark",
    "url": "https://docs.example.com",
    "ogImage": "/icon.svg",
    "i18n": {
      "defaultLocale": "zh-CN",
      "dir": ".Cdocs/i18n",
      "locales": { "zh-CN": { "label": "简体中文" }, "en": { "label": "English" } }
    },
    "editLink": { "base": "https://github.com/me/docs/edit/main", "docsDir": "md" },
    "themeVars": { "--radius": "12px", "--sidebar-left-w": "248px" },
    "customCss": "themes/ink/assets/css/custom.css",
    "compress": true,
    "jpegQuality": 82,
    "deploy": { "remote": "", "branch": "gh-pages", "message": "" }
  },
  "head": {
    "logo": "{{brand}}",
    "showSearch": true,
    "showThemeToggle": true,
    "github": "https://github.com/me/docs",
    "links": [ { "title": "{{navProject}}", "url": "https://github.com/me/docs" } ],
    "nav": [ { "title": "{{navIntro}}", "file": "intro" } ]
  },
  "center": {
    "plugins": ["search", "dark-mode", "pager", "back-to-top", "toc", "code-highlight"],
    "backToTop": { "threshold": 300, "label": "顶部" },
    "comments": {
      "provider": "giscus",
      "repo": "me/docs",
      "repoId": "...",
      "category": "Announcements",
      "categoryId": "...",
      "mapping": "pathname",
      "theme": "preferred_color_scheme"
    }
  },
  "footer": { "text": "{{footerText}}", "links": [ { "title": "{{navProject}}", "url": "https://github.com" } ] }
}
```

**site**: `title`/`description` (support `{{key}}`), `theme` (`light`/`dark`), `url` (canonical/sitemap/RSS/deploy inference), `ogImage`, `i18n` (`defaultLocale`/`dir`/`locales`), `editLink` (`base`+`docsDir`), `themeVars` (CSS variable overrides), `customCss`, `compress` (build-time compression, default true), `jpegQuality` (default 82), `deploy` (`remote`/`branch`/`message`), `home` (hero + `cards` whitelist).

**head**: `logo`, `showSearch`, `showThemeToggle`, `github` (repo URL for the GitHub icon button), `links` (nav next to logo), `nav` (right-side nav, max 6).

**center**: `plugins` (`search`/`dark-mode`/`pager`/`back-to-top`/`toc`/`code-highlight`; empty = all enabled), `backToTop` (`threshold`/`label`), `comments` (built-in **Giscus**; enabled only when `repo` + `repoId` + `categoryId` are all set).

**footer**: `text`, `links`.

---

## 3. Theme API

A theme is a folder `themes/<name>/` — **switching themes = changing folder + setting `site.themeName`** (full spec: [themes](./themes)).

```
themes/ink/
├── theme.json          theme metadata (name/version/description/…)
├── map/                page map JSON (one per page type, extends inheritance)
├── components/         components (structure+style+interaction self-contained, incl. shortcodes/)
└── assets/             frontend assets (copied as a whole to dist/assets/)
```

- Page structure is described by `map/*.json` (five section conventions: html/component/if/each/sections); page elements come from `components/`.
- Components receive engine data via `{{key}}` holes: `{{header}}` / `{{body}}` / `{{slot}}` / `{{a.b.c}}`, etc. (full table in the themes doc).
- **Styling is componentized**: components embed their `<style>` (structure+style+interaction in one file); copying a component file brings its styles along. Icon rules with `url()` and theme variables stay in `style.css`.
- `assets/` is copied verbatim to `dist/<loc>/assets/`; build output also adds `assets/deps/` (runtime deps) and `assets/search.json` (search index).
- Map registry lives in `.Cdocs/config/map.json` (`maps` array + `templates.base`).
- `config.site.themeVars` overrides theme CSS variables; `config.site.customCss` appends custom styles.

## 4. Plugin API

External scripts hook the build lifecycle via a JSON file-exchange protocol; failures are isolated (full spec: [plugins](./plugins)).

```
.Cdocs/plugins/<name>/plugin.json
→  { "name": "...", "hooks": { "<hook>": { "cmd": "...", "timeout": 30 } } }
```

Hooks: `on_config` (after config load), `on_page_collected` (after page collection; context has `pages[]`), `on_page_rendered` (per page), `on_done` (after all artifacts). Invocation: `<cmd> <ctx.json> <out.json>`, cwd = plugin dir; the plugin may write `{ "ok": bool, "message": "…" }` to show build results.

---

## 5. Front matter

```markdown
---
title: "My Page"        # display title (default: beautified filename)
date: 2026-08-01         # publish date (feed/blog sort; default: file mtime)
draft: true              # draft pages are not published (build -D includes them)
weight: 10               # sort weight (lower first)
tags: [guide, i18n]      # aggregated into the tags page
lastmod: 2026-08-02      # modification date (overrides file mtime)
aliases: [old-path]      # old paths → redirect pages
---
```

Bilingual convention: `xxx.md` (default locale) + `xxx.en.md` (English) are paired per locale.

---

## 6. i18n

- Dictionaries: `.Cdocs/i18n/<locale>.json`, flat `key → value`; locales registered in `config.site.i18n.locales`.
- References: `{{key}}` in config/templates/body, replaced per locale at build time (skipped inside `<pre>`/`<script>`/`<style>`).
- Output: one directory per locale `dist/<loc>/`; root `index.html` redirects to the default locale.
- Subdirectory pages (`blog/`, `tags/`, `page/N`) automatically get `../` relative base (relBase) across templates/nav/breadcrumb/pager/locale switch.

## 7. Versioning

Docusaurus-style multi-version docs: **explicit config wins, convention over configuration**.

- Explicit: `site.versions` in config declares versions (`name`/`label`/`source`/`default`); content lives under `md/` — current = `md/docs`, snapshots = `md/docs-<v>`.
- Convention: without config, sibling `<md>-*` snapshot dirs (e.g. `md-v1`) are auto-detected as historical versions. Each version builds independently; root redirects to the default version; a version dropdown switches between them.

## 8. Blog stream

`md/blog/` directory convention (like Hugo's `content/blog`):

- `md/blog/xxx.md` (+ `xxx.en.md`) → blog list + detail pages, newest first, paginated (10/page).
- Detail pages show date, reading time, prev/next navigation.
- Posts are merged into RSS/JSON Feed, search index, and tag aggregation.
- `date` front matter controls ordering; `draft: true` hides by default.
- **Query logic is 100% plugin-driven**: ordering/pagination/home feed come from the `blog-query` Python plugin via the `on_data_query` hook — edit the script to customize.

## 9. Navigation (sidebar)

Sidebars can be **split per version / area** (recommended): drop multiple JSON files under `.Cdocs/config/route/`
(file names are free), and declare the mapping in `config.json` `site.route` — file path → which folder:

```json
"site": {
  "route": {
    "docs":    "route/docs.json",   // current version (source dir md/docs)
    "docs-v1": "route/docs-v1.json",// historical version (source dir md/docs-v1)
    "blog":    "route/blog.json"    // blog area
  }
}
```

- key = version source dir name (`docs`, `docs-v1`..., matching `site.versions[].source`) or `blog`; value = path relative to `.Cdocs/config/`.
- Each version loads its own sidebar when built; blog pages (list/detail) load the `blog` one.
- Missing mapping / missing file → falls back to the global `.Cdocs/config/route.json` (zero regression for old layouts).
- Legacy field name `site.sidebar` is still auto-detected.

JSON format — `sidebar` array, up to 6 levels:

```json
{ "sidebar": [ { "title": "{{navGettingStarted}}", "items": [
  { "title": "{{navIntro}}", "file": "intro" },
  { "title": "External", "url": "https://example.com" }
] } ] }
```

`title` supports `{{key}}`; `file` maps to `md/<file>.md` (relative to the source root, e.g. `docs/intro`); `url` for external links (mutually exclusive with `file`);
in a blog sidebar use `file: "blog/xxx"` to point at a post.

## 10. Incremental build & DX

- **Incremental**: under `serve -w`, unchanged pages are skipped via three signature layers: global (config/route/i18n/theme mtime peak, `.Cdocs/.build/.sig`), page fingerprints (`mtime:size`, `.pages.sig`), asset signature (`.assets.sig`).
- **Auto-refresh**: after each build the `/__cdocs_epoch` endpoint updates; the browser polls and reloads — WYSIWYG editing.
- **Scaffold**: `Cdocs init <dir>` generates a complete skeleton (theme assets + `Cdocs.exe`) and builds it right away.
