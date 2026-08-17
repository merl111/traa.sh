-- SSH badge in the status bar; notify only for clear SSH titles
local last_ssh = false

-- Avoid matching normal shell titles like "user@host:~/path".
local function title_is_ssh(t)
  t = (t or ""):lower()
  if t == "" then
    return false
  end
  -- Process / command forms: "ssh", "ssh host", "… ssh user@host …"
  if t == "ssh" or t:match("^ssh%s") or t:match("%sssh%s") or t:match("%sssh$") then
    return true
  end
  if t:find("ssh@", 1, true) or t:find("ssh:", 1, true) then
    return true
  end
  if t:find("(ssh)", 1, true) or t:find("[ssh]", 1, true) or t:find(" via ssh", 1, true) then
    return true
  end
  return false
end

return {
  setup = function()
    traash.segments.ssh = function()
      local t = (traash.ctx and traash.ctx.title) or ""
      return title_is_ssh(t) and "\u{f023}  SSH" or ""
    end
    traash.on("on_pane_focus", function(title)
      local ssh = title_is_ssh(title)
      if ssh and not last_ssh then
        traash.notify("SSH session focused")
      end
      last_ssh = ssh
    end)
    traash.log("ssh-hint: SSH badge enabled")
  end,
}
