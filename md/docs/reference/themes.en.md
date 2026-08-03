# Theme Development

A Cdocs **theme is a folder**. The engine produces data (nav tree, TOC, body, pager…); page structure is described by **JSON maps**, page elements are provided by **components**, and the look & feel is carried by **theme variables + component-embedded styles**. Copy a theme folder, tweak a few config keys — the whole skin changes. That is the theme contract (and the future ecosystem entry point).

## Directory layout

Themes live in the **multi-theme repository** `themes/` (at the engine root), shipped with 3 built-ins:

```
themes/
├── ink/       # ink-wash style (default): rice-paper white + cinnabar red + night ink
├── paper/     # paper style: narrow reading column, light default, top nav, card home
└── frost/     # glassmorphism: frosted cards, gradient background, sticky header
```

The active theme is set by `site.themeName` in `config.json` (defaults to `ink`):

```json
"site": { "themeName": "frost", "theme": "dark" }
```

Each theme is a self-contained folder `themes/<name>/`:

```
themes/ink/
├── theme.json                 # theme metadata (name/version/description…)
├── map/                       # page maps: one JSON per page type (core!)
│   ├── home.json              #   home page
│   ├── doc.json               #   doc page
│   ├── blog.json              #   blog list page
│   ├── blog-post.json         #   blog detail page
│   ├── tags.json / tag-page.json / 404.json …
│   └── base.json              #   shared skeleton (extended by pages)
├── components/                # components: page elements & body shortcodes
│   ├── header/                #   header family (topbar/logo/nav/search…)
│   ├── footer/                #   footer family
│   ├── center/                #   body area family (sidebar/toc/pager/cards…)
│   └── shortcodes/            #   body shortcodes (<Tabs/> <Expand/> <Badge/>…)
└── assets/                    # front-end assets (copied verbatim into dist/assets/)
    ├── css/                   #   style.css (theme variables + shared styles)
    ├── js/                    #   app.js entry + core/ + features/ (ESM modules)
    ├── pwa/                   #   sw.js + icon.svg (PWA offline)
    └── icons/                 #   inline SVG icons
```

> Mechanical components (theme-independent interactive widgets like search/lightbox) fall back to the **default theme** when missing in the active theme — skinning doesn't require copying the whole component set.

## theme.json — metadata

```json
{
  "name": "ink",
  "version": "1.0.0",
  "description": "Cdocs default theme (ink wash: rice-paper white + cinnabar red + night ink)",
  "author": "Cdocs",
  "license": "MIT"
}
```

| Field | Required | Notes |
| --- | --- | --- |
| `name` | ✔ | theme name (lowercase letters/digits/hyphens) |
| `version` |  | theme version, semantic `x.y.z` recommended |
| `description` |  | one-line intro |
| `author` / `license` |  | author & license (MIT recommended for ecosystem) |

## map/ — page maps (JSON-driven, the theme core)

**One JSON file per page type**; a page = an ordered list of "sections". Maps are registered by `config/map.json` (`maps` array: `{type, map}`) and support `extends` inheritance (`base.json` defines the shared skeleton; child maps expand via `{"slot": X}`).

Five section kinds:

| Kind | Effect |
| --- | --- |
| `{ "html": "…" }` | static HTML fragment, output verbatim (may contain data holes like `{{lang}}`) |
| `{ "component": "Name" }` | render component `components/**/<Name>.html` |
| `{ "component": "Name", "if": "path" }` | render only when the data path is truthy |
| `{ "component": "Name", "each": "path" }` | loop over an array (each item merged into the data scope) |
| `{ "component": "Name", "sections": […], "props": {…} }` | sub-sections rendered into the component's `{{slot}}`; props passed in (highest priority) |

Data-scope priority: **global page data < each item < props**.

> Design note: map JSON has no "syntax" — conditionals, loops and nesting are plain JSON fields; components themselves are pure HTML + data holes with no control flow. Page structure = data + component composition; C++ hardcodes no page skeleton.

## components/ — structure + style + interaction in one file

A component is one HTML file that may embed `<style>` and `<script>`, fully self-contained:

```html
<!-- components/center/Card.html -->
<a class="card" href="{{href}}"><h3>{{title}}</h3>{{slot}}</a>
<style>
  .card { border: 1px solid var(--border); border-radius: var(--radius); … }
  .card:hover { border-color: var(--accent); … }
</style>
```

- **Data holes** `{{field}}` / `{{slot}}` / `{{a.b.c}}` are filled by the engine (missing ones stay verbatim and warn);
- **Componentized styles**: styles live inside the component file — copying a component carries its full styling (including dual-theme variable adaptation);
- **Component icons**: rules containing `url()` (resolved relative to the theme asset dir) stay in `style.css` — embedded styles cannot host them;
- **Body shortcodes**: components under `components/shortcodes/` can be called from the body with `<Component/>` tags (see the [shortcode reference](./shortcodes)).

## assets/ — front-end assets & theme variables

`assets/` is copied verbatim into `dist/<loc>/assets/`. The visual contract lives in `css/style.css` CSS variables:

- `:root` / `[data-theme="light"]` define light-mode variables (`--bg` rice-paper white / `--accent` cinnabar / `--info` indigo…);
- `[data-theme="dark"]` overrides for dark mode (night ink);
- components and page styles reference variables only — **re-skin by changing variables**;
- `config.site.themeVars` can override public variables; `config.site.customCss` appends custom styles (dual-theme shared overlay).

### JS entry convention

`assets/js/app.js` only bootstraps: `import('./main.js')` loads the ESM module graph. Interaction enhancements (theme / code / callouts / diagrams / search / command palette / lightbox / PWA) are `initX()` modules under `features/*.js` — **add front-end features without touching the C++ generator**.

> Note: Mermaid / KaTeX are lazy client-side upgrades; Admonitions (`> [!type]`) and shortcode components are **build-time rendered** (directly visible in the static HTML).

## How to make a new theme

1. Copy `themes/ink/` to `themes/my-theme/`;
2. Edit `theme.json` (name/version/description);
3. Point `site.themeName` in `config.json` at your new theme;
4. Edit `map/*.json` to reshape page structure (component composition);
5. Edit `components/` and `assets/css/style.css` for visuals;
6. `Cdocs build` to preview; `Cdocs serve -o --watch` for hot reload.

> Tip: want a new page type (e.g. "showcase")? Add `{type, map}` to the `maps` array in `config/map.json`, then write `theme/map/<type>.json` — no C++ changes needed.
