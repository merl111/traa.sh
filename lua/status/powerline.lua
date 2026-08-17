-- Soft “powerline” colors as separate chips (no sharp chevrons)
return function(ctx)
  local sess = ctx.session or "sess"
  local seg = ctx.seg or {}
  local title = seg.cwd or ctx.title or "~"
  if title == "" then
    title = "~"
  end
  local host = ctx.host or "host"
  local time = ctx.time or ""
  local out = {
    style = "pills",
    gap = 6,
    radius = 6,
    pad_x = 11,
    v_pad = 5,
    { text = "\u{f120}  " .. sess, align = "left", fg = "#1a1b26", bg = "#9ece6a" },
    { text = "\u{f07b}  " .. title, align = "left", fg = "#1a1b26", bg = "#7aa2f7" },
  }
  if seg.git and seg.git ~= "" then
    out[#out + 1] = { text = seg.git, align = "left", fg = "#1a1b26", bg = "#e0af68" }
  end
  if seg.battery and seg.battery ~= "" then
    out[#out + 1] = { text = seg.battery, align = "right", fg = "#1a1b26", bg = "#73daca" }
  end
  out[#out + 1] = { text = "\u{f109}  " .. host, align = "right", fg = "#1a1b26", bg = "#bb9af7" }
  out[#out + 1] = { text = "\u{f017}  " .. time, align = "right", fg = "#1a1b26", bg = "#e0af68" }
  return out
end
