# Versioning

Cdocs supports **multi-version documentation sites** (Docusaurus-style): one engine builds multiple versions of your site, with a **version switcher** in the header that keeps the current locale. Two ways to drive it: **explicit config wins** (recommended) and **convention auto-detection** (zero-config fallback).

## Content layout: md/ as the single root

Since v2 all content lives under `md/`; versions are **sub-directories** of `md/`:

```
md/
├── docs/        current version (source = md/docs)
├── docs-v1/     historical snapshot (source = md/docs-v1)
└── blog/        blog (shared globally across all versions)
```

| Directory | Version | Note |
|-----------|---------|------|
| `md/docs` | `v2` (example) | Current version, labeled "Latest" in the header |
| `md/docs-v1` | `v1` | Historical snapshot |

> Version naming is free: `md/docs`, `md/docs-v1`, `md/docs-2024` all work — the **name comes from config** (below), no fixed naming required.

## Usage (three steps)

```bash
# ① Choose "with version" at init time (interactive prompt, or create dirs manually)
Cdocs init mysite        # interactive: pick "docs+blog" → "with version snapshots?" → y

# ② Lock a snapshot when shipping a new release
cp -r md/docs md/docs-v1   # copy current v2 as a v1 snapshot

# ③ Build: dispatched per config.site.versions, each version independent output
Cdocs build
```

Build output:

```
=== 构建版本 2.x → "dist\v2"
已生成 17 篇文档到 "dist\v2"
=== 构建版本 1.x → "dist\v1"
已生成 1 篇文档到 "dist\v1"
```

Output layout (each version is a complete independent site with its own i18n / RSS / PWA):

```
dist/
├── index.html      # root redirect → default version (v2/)
├── v2/             # current (zh-CN/ + en/)
└── v1/             # v1 snapshot (zh-CN/ + en/)
```

The version switcher shows the current version as non-clickable (Latest) and other versions as links. **Switching keeps the current locale** (a v2/zh-CN page switches to v1/zh-CN/).

## Explicit config (recommended): the site.versions array

Declare the version list in the `site` section of `config.json`:

```json
"site": {
  "versions": [
    { "name": "v2", "label": "2.x", "source": "md/docs",   "default": true },
    { "name": "v1", "label": "1.x", "source": "md/docs-v1", "default": false }
  ]
}
```

| Field | Meaning |
| --- | --- |
| `name` | Version id (URL directory, e.g. `dist/v2/`) |
| `label` | Display name in the switcher (defaults to `name`) |
| `source` | Source directory for this version (relative to project root, e.g. `md/docs`) |
| `default` | Mark the default version (root index redirects here; falls back to the first entry) |

Pair it with `site.route` for per-version / per-area sidebars:

```json
"route": {
  "docs":    "route/docs.json",     // current version docs (source md/docs)
  "docs-v1": "route/docs-v1.json",  // historical version (source md/docs-v1)
  "blog":    "route/blog.json"      // blog (shared globally)
}
```

When the `versions` array is present, the explicit declaration wins (auto-scan is disabled) — handy for ordering, custom labels, or hiding versions.

## Convention fallback: snapshot auto-detection

Without `site.versions`, the builder scans `md`'s sibling `<md>-*` snapshot directories (e.g. `md-v1`, `md-v2`) and detects them as historical versions:

| Directory | Version | Note |
|-----------|---------|------|
| `md` | `current` | Latest, always first + default |
| `md-v1` | `v1` | Historical (`md-<name>` is auto-detected) |

```bash
cp -r md md-v1     # lock a snapshot when shipping v1
Cdocs build        # auto-detects current + v1
```

```
[versions] 自动识别 1 个历史版本: current v1
构建版本 最新 → "dist\current"
构建版本 v1 → "dist\v1"
```

## When to use

- **Keep old docs after a release**: readers of API changes / migration guides can still reach previous docs
- **Multi-language sites work too**: each version carries its own full `zh-CN` / `en` pair
- **Compare versions**: versions are built independently and cross-link via `../<name>/`

> Note: snapshot dirs are content copies — track or ship them as you like; delete `docs-v1` (or drop it from config) and rebuild to return to single-version with **zero residue**.
