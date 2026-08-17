-- Trim trailing whitespace on copy and notify with size
return {
  setup = function()
    traash.on("on_copy", function(text)
      text = text or ""
      -- Trim trailing spaces/tabs on each line; keep final newline if present
      local had_nl = text:sub(-1) == "\n"
      local lines = {}
      for line in (text .. "\n"):gmatch("(.-)\n") do
        lines[#lines + 1] = line:gsub("[ \t]+$", "")
      end
      local out = table.concat(lines, "\n")
      if had_nl and out:sub(-1) ~= "\n" then
        out = out .. "\n"
      end
      if traash.set_clipboard then
        traash.set_clipboard(out)
      end
      local n = #out
      traash.notify(string.format("Copied %d characters", n))
    end)
    traash.log("copy-enhancements: trim + notify on copy")
  end,
}
