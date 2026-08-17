# traa.sh Lua API

Canonical host API for plugins and config. Keep this file synchronized with
[`docs/plugins.html`](../../docs/plugins.html) and `src/plugin/host.c`.

Global `config` is loaded from `lua/defaults/config.lua`, then merged with
`~/.config/traash/config.lua`. Settings writes the user file.

## Plugin file

Plugins load only from `lua/plugins/examples/<id>/init.lua` (or the same path
under `TRAASH_LUA_PATH`). There is no user plugin directory. Enable an id in
`config.plugins` (max 16). Restart the app after plugin code or list changes —
`reload_config` does not re-run setup.

```lua
return {
  setup = function()
    traash.segments.mychip = function()
      return "text" -- empty string hides the chip
    end
    traash.on("on_bell", function()
      traash.notify("Bell")
    end)
  end,
}
```

The file may instead return a function; that function is called as setup.

Lua is trusted and unsandboxed. Plugins cannot draw UI.

## Plugin API

Plugins receive global `traash`:

| API | Purpose |
|-----|---------|
| `traash.log(msg)` | Log to the traa.sh log |
| `traash.notify(msg)` | Desktop notification + log. Linux: `notify-send` (transient 3.5s, replaces previous). macOS: `osascript` notification |
| `traash.on(event, fn)` | Register a hook (multiple listeners OK) |
| `traash.segments.name = fn` | Status chip; `fn()` → string (empty = hidden). Becomes `ctx.seg.name` |
| `traash.ctx.title` / `traash.ctx.cwd` | Active pane title and OSC-7 cwd |
| `traash.set_clipboard(text)` | Override clipboard from `on_copy` |
| `traash.theme_path()` | Path to the active theme Lua file (bundled lua dir) |
| `traash.reload_theme()` | Request a theme reload from the host |
| `traash.sessions()` | List of mux session names |
| `traash.run_action(name)` | Queue one action id for the next frame (e.g. `"overview"`, `"command_palette"`) |

## Hooks

| Event | When | Argument |
|-------|------|----------|
| `on_tick` | ~every 0.5s (with status refresh) | — |
| `on_bell` | Terminal BEL, only while the window is unfocused | — |
| `on_command_finished` | OSC 133 command-done (prompt returned) | — |
| `on_pane_focus` | Active pane changes | pane title |
| `on_copy` | Selection copied | clipboard text |

## Status segments

Status styles read plugin chips from `ctx.seg.<name>` (filled by calling each
`traash.segments.*` function). Keep that work cheap (~2 Hz).

`ctx` fields: `session`, `window`, `title`, `cwd`, `host`, `time`, `seg.*`.

Styles that consume chips: `pills`, `minimal`, `powerline`, `dev`, `tmux`.
`compact` and `centered` ignore `ctx.seg`.

## Constraints

- No direct UI drawing; use segments, hooks, notify, clipboard override, or `run_action`.
- One queued `run_action` per frame (later calls overwrite).
- Max 16 plugin ids in config.
- No plugin hot reload; no sandbox; no user plugin dir.
- Desktop notifications are transient and replace the previous traa.sh notification.
- `on_bell` does not fire while the window is focused.

## Config, themes, layouts

Themes: `lua/themes/<name>.lua` (or `~/.config/traash/themes/`) returning a color table.

Status: `lua/status/<name>.lua` returning `function(ctx) return {segments...} end`.

Layouts: `lua/layouts/<name>.lua` (or `~/.config/traash/layouts/`) returning a
session snapshot; open with action `"layout_picker"` (Ctrl-Shift-L). Set
`config.default_layout` to apply on startup (skipped when attaching). Apply
replaces tabs/panes with fresh shells (geometry only).

Action names: `split_h`, `split_v`, `pane_next`, `pane_left`, `pane_down`,
`pane_up`, `pane_right`, `zoom`, `new_window`, `next_window`, `prev_window`,
`goto_window`, `detach`, `reload_config`, `command_palette`, `theme_cycle`,
`status_cycle`, `font_increase`, `font_decrease`, `demo`, `copy_mode`, `copy`,
`paste`, `search`, `settings`, `shortcuts`, `layout_picker`, `overview`, `quit`.

`status_cycle` and `demo` have no default chord. `copy_mode` is bound
(prefix `[`) but currently unimplemented.

## Example plugins

Default-enabled: `git-status`, `cwd-short`, `battery`, `ssh-hint`,
`notify-on-bell`, `autoreload-theme`, `session-picker`, `copy-enhancements`.

Optional examples (add to `config.plugins`): `hints`, `welcome`.
