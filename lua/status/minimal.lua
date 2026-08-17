-- Quiet, airy chips — low contrast, lots of breathing room
return function(ctx)
  local sess = ctx.session or "?"
  local win = tostring(ctx.window or 1)
  local seg = ctx.seg or {}
  local title = seg.cwd or ctx.title or ""
  if title == "" then
    title = "~"
  end
  local time = ctx.time or ""
  local out = {
    style = "pills",
    gap = 10,
    radius = 9,
    pad_x = 11,
    v_pad = 7,
    { text = "\u{f111}  " .. sess, align = "left", fg = "#a9b1d6", bg = "#1f2335" },
    { text = win, align = "left", fg = "#c0caf5", bg = "#292e42" },
    { text = "\u{f07c}  " .. title, align = "left", fg = "#565f89", bg = "#16161e" },
  }
  if seg.git and seg.git ~= "" then
    out[#out + 1] = { text = seg.git, align = "left", fg = "#565f89", bg = "#1f2335" }
  end
  if seg.battery and seg.battery ~= "" then
    out[#out + 1] = { text = seg.battery, align = "right", fg = "#565f89", bg = "#1f2335" }
  end
  out[#out + 1] = { text = "\u{f017}  " .. time, align = "right", fg = "#565f89", bg = "#1f2335" }
  return out
end
