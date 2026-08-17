local root = os.getenv("TRAASH_LUA_PATH") or "./lua"
package.path = root .. "/?.lua;" .. package.path

local styles = {"minimal", "tmux", "powerline", "dev", "compact", "centered"}
local ctx = { session = "demo", window = 1, title = "shell", time = "12:00", host = "host" }

for _, name in ipairs(styles) do
  local mod = assert(loadfile(root .. "/status/" .. name .. ".lua"))()
  assert(type(mod) == "function", name)
  local segs = mod(ctx)
  assert(type(segs) == "table" and #segs > 0, name .. " segments")
  for _, s in ipairs(segs) do
    assert(type(s.text) == "string", name .. " text")
  end
  print("ok status " .. name)
end
print("all status styles ok")
