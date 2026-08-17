-- Status tip pointing at the shortcuts cheatsheet
return {
  setup = function()
    traash.segments.hint = function()
      return "Ctrl-Shift-/"
    end
    traash.notify("Tip: Ctrl-Shift-/ opens the keyboard shortcuts cheatsheet")
    traash.log("hints: cheatsheet tip in status bar")
  end,
}
