local root = os.getenv("TRAASH_LUA_PATH") or "./lua"
package.path = root .. "/?.lua;" .. package.path

traash = {
  log = function(m) end,
  notify = function(m) end,
  segments = {},
  hooks = {},
  ctx = { title = "", cwd = "" },
  on = function(name, fn)
    local h = traash.hooks[name]
    if type(h) == "function" then
      traash.hooks[name] = { h, fn }
    elseif type(h) == "table" then
      h[#h + 1] = fn
    else
      traash.hooks[name] = { fn }
    end
  end,
  set_clipboard = function(s) end,
  theme_path = function() return root .. "/themes/tokyo-night.lua" end,
  reload_theme = function() end,
  sessions = function() return { "default" } end,
  run_action = function(name) end,
}

local plugins = {
  "git-status", "cwd-short", "battery", "ssh-hint", "notify-on-bell",
  "autoreload-theme", "session-picker", "copy-enhancements"
}

for _, name in ipairs(plugins) do
  local path = root .. "/plugins/examples/" .. name .. "/init.lua"
  local mod = assert(loadfile(path))()
  assert(type(mod) == "table" and type(mod.setup) == "function", name)
  mod.setup()
  print("ok plugin " .. name)
end

-- Smoke: segments return strings
assert(type(traash.segments.git) == "function")
assert(type(traash.segments.cwd) == "function")
assert(type(traash.segments.battery) == "function")
assert(type(traash.segments.ssh) == "function")
assert(type(traash.hooks.on_bell) == "table")
assert(type(traash.hooks.on_copy) == "table")
print("all plugins ok")
