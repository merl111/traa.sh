-- Shortened working directory chip (OSC 7 cwd when available)
local function shorten(pwd)
  if not pwd or pwd == "" then
    return ""
  end
  local home = os.getenv("HOME") or ""
  if home ~= "" and pwd:sub(1, #home) == home then
    pwd = "~" .. pwd:sub(#home + 1)
  end
  local parts = {}
  for p in pwd:gmatch("[^/]+") do
    parts[#parts + 1] = p
  end
  if #parts > 3 then
    return ".../" .. table.concat({ parts[#parts - 1], parts[#parts] }, "/")
  end
  return pwd
end

return {
  setup = function()
    traash.segments.cwd = function()
      local cwd = traash.ctx and traash.ctx.cwd
      if cwd and cwd ~= "" then
        return shorten(cwd)
      end
      return shorten(os.getenv("PWD") or "")
    end
    traash.log("cwd-short: path chip enabled")
  end,
}
