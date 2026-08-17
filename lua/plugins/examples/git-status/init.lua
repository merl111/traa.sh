-- Git branch chip for the status bar (ctx.seg.git)
local cache = { branch = "", t = 0 }

local function refresh()
  local now = os.time()
  if now == cache.t then
    return cache.branch
  end
  cache.t = now
  local cwd = (traash.ctx and traash.ctx.cwd) or nil
  local cmd = "git rev-parse --abbrev-ref HEAD 2>/dev/null"
  if cwd and cwd ~= "" then
    cmd = string.format("git -C %q rev-parse --abbrev-ref HEAD 2>/dev/null", cwd)
  end
  local f = io.popen(cmd)
  if not f then
    cache.branch = ""
    return ""
  end
  local branch = f:read("*l") or ""
  f:close()
  if branch ~= "" and branch ~= "HEAD" then
    cache.branch = "\u{f126}  " .. branch
  else
    cache.branch = ""
  end
  return cache.branch
end

return {
  setup = function()
    traash.segments.git = refresh
    traash.on("on_tick", function()
      cache.t = 0 -- allow refresh next status paint
    end)
    traash.log("git-status: branch chip enabled")
  end,
}
