-- Reload the active theme when its Lua file changes on disk
local last_mtime = nil

local function file_mtime(path)
  local f = io.popen(string.format("stat -c %%Y %q 2>/dev/null || stat -f %%m %q 2>/dev/null", path, path))
  if not f then
    return nil
  end
  local line = f:read("*l")
  f:close()
  return tonumber(line)
end

return {
  setup = function()
    local path = traash.theme_path and traash.theme_path() or nil
    if path then
      last_mtime = file_mtime(path)
    end
    local ticks = 0
    traash.on("on_tick", function()
      ticks = ticks + 1
      if ticks % 4 ~= 0 then
        return
      end
      local p = traash.theme_path and traash.theme_path() or nil
      if not p then
        return
      end
      local m = file_mtime(p)
      if m and last_mtime and m > last_mtime then
        last_mtime = m
        traash.log("theme file changed — reloading")
        traash.reload_theme()
        traash.notify("Theme reloaded")
      elseif m and not last_mtime then
        last_mtime = m
      end
    end)
    traash.log("autoreload-theme: watching " .. tostring(path))
  end,
}
