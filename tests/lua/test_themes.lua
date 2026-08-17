local root = os.getenv("TRAASH_LUA_PATH") or "./lua"
package.path = root .. "/?.lua;" .. root .. "/?/init.lua;" .. package.path

local themes = {
  "tokyo-night", "catppuccin-mocha", "dracula", "gruvbox-dark", "nord",
  "one-dark", "solarized-dark", "solarized-light", "rose-pine", "github-dark",
  "traash-dark"
}

local required = {
  "foreground", "background", "cursor", "cursor_text", "selection_fg", "selection_bg", "ansi"
}

for _, name in ipairs(themes) do
  local t = assert(loadfile(root .. "/themes/" .. name .. ".lua"))()
  assert(type(t) == "table", name .. " not table")
  for _, k in ipairs(required) do
    assert(t[k] ~= nil, name .. " missing " .. k)
  end
  assert(type(t.ansi) == "table", name .. " ansi")
  for i = 0, 15 do
    assert(type(t.ansi[i]) == "string" and t.ansi[i]:match("^#%x%x%x%x%x%x$"),
           name .. " ansi[" .. i .. "]")
  end
  print("ok theme " .. name)
end
print("all themes ok")
