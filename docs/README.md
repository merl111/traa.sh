# GitHub Pages

This folder is the static site for **traa.sh** developer docs. GitHub Pages
serves it from the `/docs` folder on the default branch.

## Pages

| File | Role |
|------|------|
| [`index.html`](index.html) | Product / configuration overview |
| [`getting-started.html`](getting-started.html) | Build, install, CLI, sessions |
| [`keyboard-ui.html`](keyboard-ui.html) | Keymap, overview, overlays, mouse, quit |
| [`plugins.html`](plugins.html) | Plugin development and host API |
| [`llms.txt`](llms.txt) | Agent-oriented digest |
| [`assets/site.css`](assets/site.css) | Shared visual / navigation styles |
| [`assets/traash.svg`](assets/traash.svg) | Logo |

The repository-side plugin API (`lua/api/README.md`) must stay aligned with
`plugins.html` and `src/plugin/host.c`.

## Local preview

From the repository root (required so relative assets resolve the same way Pages does):

```bash
make docs-check      # internal HTML / asset / anchor links
make docs-preview    # http://localhost:8080/
```

`docs-preview` runs `python3 -m http.server -d docs 8080` (override with `DOCS_PORT=…`). Prefer that over
`file://` so `assets/site.css` and cross-page links work. If 8080 is already taken, use another port.

## Enable on GitHub

Repository → Settings → Pages → Build from branch → Folder: `/docs`.
