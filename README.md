# traa.sh

Modern, GPU-accelerated terminal emulator written in C for Linux and macOS — **built for humans and automation agents**.

**Website / brand:** [traa.sh](https://traa.sh)

## Agent-compatible terminal

traa.sh exposes a **stable JSON API** over its mux socket so scripts and AI agents can drive sessions without parsing ANSI or opening the GUI:

```bash
traash --server --create dev &
traash agent state dev                    # pane text, cwd, cursor, busy flag
traash agent send dev --pane 1 --literal $'make test\n'
traash agent wait dev --pane 1            # blocks until OSC 133;D (shell integration)
traash agent subscribe dev                # JSONL events: cwd, title, command_finished
eval "$(traash shell-init bash)"          # add to shell rc for reliable wait/send
```

Full protocol reference: **[docs/agents.html](docs/agents.html)** · agent digest: **[docs/llms.txt](docs/llms.txt)**

## Features

- **Headless agent API** — `traash agent state|send|wait|subscribe|run-action` over Unix/TCP mux
- OpenGL text rendering (GLFW + FreeType + HarfBuzz deps)
- Built-in tmux-like multiplexing (sessions / windows / panes, detach/attach)
- Two-level session overview with live tab and pane previews
- Named session layouts (picker overlay + optional default on startup)
- Lua configuration, themes, status bars, and plugins
- 11 built-in themes and 7 status bar styles
- Example plugins wired into the status bar and hooks (git, cwd, battery, SSH, bell, …)
- Demo mode: `traash --demo` (optional `--auto`)
- Automated C + Lua test suite (CTest) and CI

## Build

Dependencies: CMake, Ninja, GLFW (fetched), OpenGL, FreeType, HarfBuzz, Lua, Fontconfig, `libutil` (pty on Linux).

**Linux (Debian/Ubuntu):**

```bash
sudo apt-get install ninja-build pkg-config libfreetype6-dev libharfbuzz-dev \
  liblua5.4-dev libfontconfig1-dev libssl-dev libgl1-mesa-dev \
  libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
  libwayland-dev libxkbcommon-dev
```

**macOS (Homebrew):**

```bash
brew install cmake ninja pkg-config freetype harfbuzz lua fontconfig
```

```bash
cmake -B build -G Ninja
cmake --build build
./build/traash
```

Tests:

```bash
ctest --test-dir build --output-on-failure
```

Headless smoke (no display):

```bash
./build/traash --headless-test
./build/traash --headless-test --demo --auto
```

## Config

User config: `~/.config/traash/config.lua` (overrides `lua/defaults/config.lua`).

```lua
config = {
  theme = "dracula",
  status_bar = "tmux",
  font_size = 14,
  default_layout = "", -- e.g. "dev"; empty = single pane
  plugins = { "git-status", "hints" },
}
```

Themes: `lua/themes/` or `~/.config/traash/themes/`.  
Status styles: `lua/status/{pills,minimal,tmux,powerline,dev,compact,centered}.lua`.  
Layouts: `lua/layouts/` or `~/.config/traash/layouts/` (`single`, `h-split`, `v-split`, `dev`).  
Plugin API: `lua/api/README.md`.

**Docs site (GitHub Pages):** [`docs/`](docs/) — **[agent API](docs/agents.html)** (start here for automation), [getting started](docs/getting-started.html), [keyboard & UI](docs/keyboard-ui.html), [plugins](docs/plugins.html), [`llms.txt`](docs/llms.txt).

Local preview: `make docs-preview` (http://localhost:8080/ by default; `DOCS_PORT=8081` if that port is taken). Validate links with `make docs-check`.

## Releases

Push a version tag to build Linux (`x86_64`) and macOS (`arm64`) archives and attach them to a GitHub Release:

```bash
git tag v0.1.0
git push origin v0.1.0
```

Each tarball is relocatable (`bin/traash`, bundled `lib/`, `share/traash/lua`). Run `./bin/traash` from the extracted directory.

## Mux keys (defaults)

Prefix: `Ctrl-b` then:

| Key | Action |
|-----|--------|
| `%` | vertical split |
| `"` | horizontal split |
| `o` | next pane |
| `h` `j` `k` `l` | focus pane |
| `z` | zoom |
| `c` | new window |
| `n` / `Tab` | next window |
| `p` | previous window |
| `0`–`9` | go to window by number |
| `d` | detach |
| `[` | copy mode (reserved; unimplemented) |
| `?` | shortcuts overlay |

Also (no prefix): `Ctrl-h/j/k/l` pane focus, `Ctrl-Shift-+` / `Ctrl--` font size (8–48 px), `Ctrl-Shift-P` command palette, `Ctrl-Shift-O` session overview, `Ctrl-Shift-L` layout picker, `Ctrl-Shift-T` theme cycle, `Ctrl-Shift-F` search, `Ctrl-Shift-,` settings, `Ctrl-Shift-/` shortcuts, `Ctrl-Shift-Q` quit.

`status_cycle` and `demo` have no default chord (command palette).

### Session overview

`Ctrl-Shift-O` opens a two-level grid: live composite previews of each tab, then drill into per-pane previews. Arrows or `hjkl` move; Enter or click focuses (or drills into a multi-pane tab); Esc goes back then closes; `×` / Delete closes the selected tab or pane (with the same process confirmation as quit). The session always keeps at least one tab.

### Saved layouts

`Ctrl-Shift-L` opens a cheatsheet-style picker over bundled + user layouts. Enter applies (replaces all tabs/panes with fresh shells), `s` saves the current session under a name, Del removes a user layout, Esc closes. Set `default_layout` in config or Settings → Appearance to apply a layout on startup.

## CLI

```
traash --demo [--auto]
traash --server [--bind ADDR:PORT]
traash --list-sessions
traash --attach NAME [--read-only] [--host H --port P]
traash --create NAME [--encrypt]
traash --password-fd FD
traash --headless-test
traash agent state|send|wait|subscribe|run-action SESSION ...
traash shell-init bash|zsh|fish
```

See **[docs/agents.html](docs/agents.html)** for JSON message types, events, shell setup, exit codes, and encrypted read-only roles.

### Encrypted sessions

Create an encrypted session with separate write and read-only passwords:

```bash
traash --create myvault --encrypt
traash --server --bind 127.0.0.1:9477
traash --attach myvault          # prompts for password; role from password
traash --attach myvault --read-only
traash --attach myvault --host 127.0.0.1 --port 9477
```

Encrypted snapshots are stored at `~/.local/share/traash/sessions/<name>.tsn` (AES-256-GCM, PBKDF2). Passwords are never taken on the command line — use the tty prompt or `--password-fd`. TCP attach sends the password in the clear (no TLS in v1); use Unix socket or a trusted LAN. Agents use the same dual-password model: read-only attach can query state and subscribe but cannot send input or wait-for-idle.

## Icon

App icon: `assets/icons/traash.png` / `traash.svg`.

- **X11:** RGBA sizes are baked into the binary (`src/assets/icon_embedded.c`) and applied with `glfwSetWindowIcon`.
- **Wayland / launchers:** install the `.desktop` file and hicolor icons (`cmake --install`), which map `Icon=sh.traa.traash`.

Regenerate the embedded pixels after changing the PNG:

```bash
make embed-icon
# or: python3 tools/embed_icon.py
```

Install (binary, `.desktop`, hicolor icons) to `~/.local` by default:

```bash
make install
# system-wide: make install PREFIX=/usr/local
```

## License

Apache License 2.0 — see [LICENSE](LICENSE).
