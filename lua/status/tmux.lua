-- Mux-flavored: session badge + window index, soft green accents
return function(ctx)
  local sess = ctx.session or "traash"
  local win = tostring(ctx.window or 1)
  local seg = ctx.seg or {}
  local title = seg.cwd or ctx.title or ""
  if title == "" then
    title = "~"
  end
  local host = ctx.host or "localhost"
  local time = ctx.time or ""
  local out = {
    style = "pills",
    gap = 7,
    radius = 8,
    pad_x = 10,
    v_pad = 6,
    { text = "\u{f2db}  " .. sess, align = "left", fg = "#0d1117", bg = "#73daca" },
    { text = "\u{f2d0}  " .. win, align = "left", fg = "#c0caf5", bg = "#3b4261" },
    { text = "\u{f07b}  " .. title, align = "left", fg = "#a9b1d6", bg = "#24283b" },
  }
  if seg.git and seg.git ~= "" then
    out[#out + 1] = { text = seg.git, align = "left", fg = "#0d1117", bg = "#e0af68" }
  end
  if seg.ssh and seg.ssh ~= "" then
    out[#out + 1] = { text = seg.ssh, align = "left", fg = "#0d1117", bg = "#f7768e" }
  end
  if seg.battery and seg.battery ~= "" then
    out[#out + 1] = { text = seg.battery, align = "right", fg = "#0d1117", bg = "#9ece6a" }
  end
  out[#out + 1] = { text = "\u{f233}  " .. host, align = "right", fg = "#c0caf5", bg = "#33467c" }
  out[#out + 1] = { text = "\u{f017}  " .. time, align = "right", fg = "#0d1117", bg = "#7dcfff" }
  return out
end
