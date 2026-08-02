# Theme Development

A Cdocs **theme is a folder**. The engine only generates data (nav tree, TOC, body, pager…); the page skeleton and all front-end assets come from the theme. Copy a theme folder, tweak a few lines, and the whole look changes — this is Cdocs' theme contract, and the integration point for a future theme ecosystem.

## Directory layout

The theme lives under the engine directory: `.Cdocs/theme/`. The default "ink" (ink-wash) theme ships with every site:

```
.Cdocs/theme/
├── theme.json                 # Theme metadata (required)
├── templates/
│   └── layout.html            # Page skeleton (placeholder injection, required)
└── assets/                    # Front-end assets (copied verbatim to dist/assets/)
    ├── css/                   #   style.css / custom.css / pswp-theme.css
    ├── js/                    #   app.js entry + core/ + features/ (ESM modules)
    ├── pwa/                   #   sw.js + icon.svg (PWA offline)
    └── icons/                 #   inline SVG icons
```

> **Legacy compat**: older versions kept `assets/` and `templates/` directly under `.Cdocs/`. The builder prefers `.Cdocs/theme/` and falls back to the old locations, so existing sites build unchanged.

## theme.json — metadata

```json
{
  "name": "ink",
  "version": "1.0.0",
  "description": "Cdocs default theme (ink-wash style: rice paper + vermilion + night ink)",
  "author": "Cdocs",
  "license": "MIT",
  "assets": "assets",
  "templates": "templates"
}
```

| Field | Required | Meaning |
| --- | --- | --- |
| `name` | ✔ | Theme name (lowercase letters / digits / hyphens) |
| `version` |  | Semver, e.g. `1.0.0` |
| `description` |  | One-line intro shown to users |
| `author` / `license` |  | Author & license (MIT recommended for ecosystem spread) |
| `assets` / `templates` |  | Sub-directory names, default `assets` / `templates` |

## templates/layout.html — page skeleton

The builder splits each page into **data blocks** and injects them into the template via `{{key}}` placeholders. Everything else in the template is kept verbatim.

### Placeholder reference

| Placeholder | Kind | Meaning |
| --- | --- | --- |
| `{{lang}}` | scalar | Current locale (`zh-CN` / `en`) |
| `{{theme}}` | scalar | Theme (light/dark, from config or user preference) |
| `{{title}}` | scalar | Page title |
| `{{base}}` | scalar | Prefix to site root (used by subdirectory sites, usually `./`) |
| `{{body_class}}` | scalar | Extra `<body>` classes (e.g. `no-sidebar` on home) |
| `{{meta_desc}}` | inline | `<meta>` description |
| `{{highlight_css}}` | inline | Code-highlight CSS (empty when disabled) |
| `{{custom_head}}` | inline | Custom `<head>` injection |
| `{{skip_link}}` | line-end | Accessible "skip to content" link |
| `{{header}}` | line-end | Header (logo / search / locale / GitHub / theme toggle) |
| `{{left_nav}}` | inline | Left sidebar nav tree |
| `{{breadcrumb}}` | inline | Breadcrumb |
| `{{body}}` | inline | **Main content** (must be present, else built-in fallback) |
| `{{last_updated}}` | inline | Last-updated timestamp |
| `{{edit_link}}` | inline | "Edit this page" link |
| `{{pager}}` | inline | Prev / Next |
| `{{toc_sidebar}}` | inline | Right TOC (empty when disabled) |
| `{{footer}}` | line-end | Footer |
| `{{back_to_top}}` | line-end | Back-to-top button |
| `{{highlight_js}}` | line-end | Highlight JS |
| `{{search_js}}` | line-end | Search library JS |
| `{{i18n_json}}` | line-end | Client i18n dict (`window.__I18N__`) |
| `{{feedback_js}}` | line-end | Feedback button data |

### Layout rules

Three conventions keep the output HTML clean:

1. **Scalar** placeholders can go anywhere; **inline** blocks carry a trailing newline — place them at line start;
2. **Line-end** blocks (`skip_link` / `header` / `footer` / `back_to_top` / JS blocks) **must sit on their own line ending with the placeholder** — the renderer strips the trailing newline and the template line supplies it;
3. The **content row** (`breadcrumb` / `body` / `last_updated` / `edit_link` / `pager`) should share one line so empty blocks produce no blank lines.

> Unknown `{{key}}`s (e.g. i18n keys like `{{navIntro}}`) are kept **verbatim** and resolved later by i18n — the template does not need to care.

## assets/ — front-end assets

The whole `assets/` directory is copied verbatim into each locale's `dist/<loc>/assets/`. Therefore:

- `css/style.css` → `dist/<loc>/assets/css/style.css` (referenced as `{{base}}assets/css/style.css`);
- **Incremental skip**: the builder records an mtime signature of assets and skips copying when unchanged (a big win on large sites);
- Runtime JS libs (Mermaid / KaTeX / PhotoSwipe / FlexSearch…) live in `.Cdocs/deps/` and are copied to `assets/deps/` — they are **not** part of a theme; themes own css/js/icons/pwa only.

### JS entry convention

`assets/js/app.js` only bootstraps and dynamically `import('./main.js')` to load the ESM graph. Themes may freely reorganize `main.js` and its deps, but the **entry file name `app.js` and its reference path must stay** (the page template hard-codes `{{base}}assets/js/app.js`).

## Make a new theme

1. Copy `.Cdocs/theme/` to `.Cdocs/theme-my/` (or a standalone site);
2. Edit `theme.json` (name/version/description…);
3. Edit `templates/layout.html` to change page structure;
4. Edit `assets/css/style.css` and `assets/js/` to restyle;
5. `Cdocs build` to preview; `Cdocs serve -o --watch` for hot reload.

> Tip: theme CSS variables (`--accent` / `--bg` / `--fg` …) are defined in `style.css` under `:root`. To re-color, override the variables instead of editing every rule.
