-- One sleek capsule — everything in a single soft bar
return function(ctx)
  local sess = ctx.session or "s"
  local win = tostring(ctx.window or 1)
  local title = ctx.title or ""
  if title == "" then
    title = "~"
  end
  if #title > 36 then
    title = "…" .. title:sub(-33)
  end
  local time = ctx.time or ""
  local line = string.format("\u{f120}  %s   \u{f2d0}  %s   \u{f07b}  %s   \u{f017}  %s",
    sess, win, title, time)
  return {
    style = "pills",
    gap = 0,
    radius = 14,
    pad_x = 16,
    v_pad = 6,
    { text = line, align = "left", fg = "#c0caf5", bg = "#1f2335" },
  }
end
