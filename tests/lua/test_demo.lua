local root = os.getenv("TRAASH_LUA_PATH") or "./lua"
package.path = root .. "/?.lua;" .. package.path
local M = assert(loadfile(root .. "/demo/run.lua"))()
assert(type(M.steps) == "table" and #M.steps > 0)
for i, step in ipairs(M.steps) do
  assert(type(step) == "function", "step " .. i)
  local out = step(i)
  assert(type(out) == "string" and #out > 0, "step out " .. i)
end
print("demo steps ok: " .. #M.steps)
