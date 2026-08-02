# Plugin Development

Plugins let Cdocs extend the build pipeline **without touching the engine**. The contract is **external process + JSON file exchange**: the builder fires hooks at key points, invokes your script (any language), and the script participates by reading/writing JSON. Zero DLL, zero deps, failures are isolated automatically.

## Directory layout

```
.Cdocs/plugins/<plugin-name>/
├── plugin.json        # Plugin manifest (required)
└── (script)           # A script or executable in any language
```

## plugin.json — manifest

```json
{
  "name": "build-report",
  "hooks": {
    "on_done": { "cmd": "python report.py", "timeout": 30 }
  }
}
```

| Field | Meaning |
| --- | --- |
| `name` | Plugin name (must match the directory name) |
| `hooks` | Hook table: `{ hookName: { "cmd": command, "timeout": seconds } }` |
| `cmd` | Command to run. The builder **chdirs into the plugin dir** first: `<cmd> <ctx.json abs path> <out.json abs path>` |
| `timeout` | Timeout in seconds (default 30). On timeout the process is killed and marked failed (exit 124), build continues |

## Hooks & context

The builder broadcasts 4 hooks. Each receives a context JSON (`ctx.json`):

| Hook | When | Key ctx fields |
| --- | --- | --- |
| `on_config` | after config loaded | `engine` / `source` / `dest` |
| `on_page_collected` | after pages collected | `count`, `pages[]` (file/title/draft/tags) |
| `on_page_rendered` | after **each page** written | `file` / `locale` / `path` (absolute) |
| `on_done` | after all output generated | `engine` / `source` / `dest` |

All path fields are **absolute** (the builder chdirs into the plugin dir, so relative paths would resolve wrongly).

## Output protocol

The script writes the result to `out.json` (the second argument, absolute path):

```json
{ "ok": true, "message": "Footer injected" }
```

| Field | Meaning |
| --- | --- |
| `ok` | `true` success / `false` business failure |
| `message` | Text shown in the build log (`[plugin <name>] <message>`) |

## Failure isolation

Plugins **never block the build**:

- spawn failure (missing script) → warning, exit code `-1`;
- non-zero script exit → warning;
- timeout kill → warning, exit code `124`;
- `ok: false` in `out.json` → plain notice.

In every case `Cdocs build` continues and exits 0.

## Full example (Python)

A plugin that appends a footer to every page and reports total `dist` size at the end:

```python
import json, os, sys

ctx_path, out_path = sys.argv[1], sys.argv[2]
ctx = json.load(open(ctx_path, encoding="utf-8"))

def done(result):
    json.dump(result, open(out_path, "w", encoding="utf-8"))

# Page-level hook: inject footer (idempotent marker prevents double injection)
if "path" in ctx:
    p = ctx["path"]
    text = open(p, encoding="utf-8").read()
    marker = "<!-- footer-by-plugin -->"
    if marker not in text:
        text = text.replace("</main>", marker + "\n<p>Injected by plugin</p>\n</main>", 1)
        open(p, "w", encoding="utf-8").write(text)
    done({"ok": True, "message": "Footer injected"})
else:
    # Site-level hook: sum dist size
    total = sum(os.path.getsize(os.path.join(root, f))
                for root, _, files in os.walk(ctx["dest"]) for f in files)
    done({"ok": True, "message": f"dist total {total//1024} KB"})
```

## Built-in vs plugins

These features are **built into the engine** (convention over configuration, no plugin needed): giscus comments (`center.comments`), versioned docs (`docs-*` snapshot auto-detection), RSS/PWA/sitemap/SEO, image & code compression. Plugins best fit **site-specific** finishing work: deploy push (gh-pages), custom analytics, alerting, content validation, and so on.
