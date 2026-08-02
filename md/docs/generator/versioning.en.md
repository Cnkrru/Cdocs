# Versioning

Cdocs supports **multi-version documentation sites**: copy your current `md/` into a snapshot like `md-v1/` or `md-v2/`, and the build automatically detects it as a historical version, producing `current` (latest) plus each historical version with a **version switcher** in the header. The feature is **on by default — convention over configuration** (no config needed).

## Convention: directory = version

| Directory | Version | Note |
|-----------|---------|------|
| `md/` | `current` | Latest, labeled "最新 / Latest" in the header |
| `md-v1/` | `v1` | Historical (`md-<name>` is auto-detected) |
| `md-v2/` | `v2` | And so on |

## Usage (three steps)

```bash
cp -r md md-v1     # ① lock a snapshot when shipping v1
Cdocs build            # ② build: auto-detects current + v1
```

Build output:

```
[versions] 自动识别 1 个历史版本: current v1
构建版本 最新 → "dist\current"
构建版本 v1 → "dist\v1"
```

Output layout (each version is a complete independent site with its own i18n / RSS / PWA):

```
dist/
├── index.html      # root redirect → current/
├── current/        # latest (zh-CN/ + en/)
└── v1/             # v1 snapshot (zh-CN/ + en/)
```

The version switcher shows the current version as non-clickable (latest) and other versions as links. **Switching keeps the current locale** (a zh-CN page switches to v1/zh-CN/).

## Overriding defaults: the versions array in config.json

To customize labels / order / default version, declare in the `site` section of `config.json`:

```json
"versions": [
  { "name": "current", "label": "Latest", "default": true },
  { "name": "v2",      "label": "2.0 stable" },
  { "name": "v1",      "label": "1.x legacy" }
]
```

- `name`: version directory name (`md-<name>`, or `current` for the live `md/`)
- `label`: display name in the switcher (defaults to `name`)
- `default`: mark the default version (auto-assigned to the latest when omitted)

When the `versions` array is present, the explicit declaration wins (auto-scan is disabled) — handy for ordering or hiding versions.

## When to use

- **Keep old docs after a release**: readers of API changes / migration guides can still reach previous docs
- **Multi-language sites work too**: each version carries its own full `zh-CN` / `en` pair
- **Compare versions**: versions are built independently and cross-link via `../<name>/`

> Note: `md-v1` is a content snapshot — track or ship it as you like; delete it and rebuild to return to single-version with **zero residue**.
