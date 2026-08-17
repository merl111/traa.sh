-- One-shot welcome notification on startup
return {
  setup = function()
    traash.notify("Welcome to traa.sh — Ctrl-Shift-/ for shortcuts, Ctrl-Shift-, for settings")
    traash.log("welcome: hello")
  end,
}
