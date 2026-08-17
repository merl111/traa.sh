-- List mux sessions in the status bar; open the command palette on demand
return {
  setup = function()
    traash.segments.sessions = function()
      local list = traash.sessions and traash.sessions() or {}
      local n = #list
      if n <= 0 then
        return ""
      end
      if n == 1 then
        return "\u{f1c0}  " .. tostring(list[1])
      end
      return string.format("\u{f1c0}  %d sessions", n)
    end
    traash.log("session-picker: session chip enabled (Ctrl-Shift-P for palette)")
  end,
}
