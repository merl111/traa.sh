-- traa.sh default configuration
config = {
  theme = "tokyo-night",
  status_bar = "pills",
  font = "Hack Nerd Font Mono",
  font_size = 14,
  opacity = 1.0,
  cursor_style = 1, -- beam cursor (0=block, 1=beam, 2=underline)
  scrollback = 5000,
  default_layout = "", -- e.g. "dev", "h-split"; empty = single pane
  plugins = {
    "git-status",
    "cwd-short",
    "battery",
    "ssh-hint",
    "notify-on-bell",
    "autoreload-theme",
    "session-picker",
    "copy-enhancements",
  },
}
return config
