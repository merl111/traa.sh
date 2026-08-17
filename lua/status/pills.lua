-- Soft pill segments — default everyday look
return function(ctx)
  local sess = ctx.session or "sess"
  local seg = ctx.seg or {}
  local title = seg.cwd or ctx.title or "~"
  if title == "" then
    title = "~"
  end
  if #title > 48 then
    title = "…" .. title:sub(-45)
  end
  local host = ctx.host or "host"
  local time = ctx.time or ""
  local win = tostring(ctx.window or 1)
  local out = {
    style = "pills",
    gap = 8,
    radius = 11,
    pad_x = 12,
    v_pad = 6,
    { text = "\u{f120}  " .. sess, align = "left", fg = "#14161b", bg = "#9ece6a" },
    { text = "\u{f2d0}  " .. win, align = "left", fg = "#c0caf5", bg = "#3b4261" },
    { text = "\u{f07b}  " .. title, align = "left", fg = "#14161b", bg = "#7aa2f7" },
  }
  if seg.git and seg.git ~= "" then
    out[#out + 1] = { text = seg.git, align = "left", fg = "#14161b", bg = "#e0af68" }
  end
  if seg.ssh and seg.ssh ~= "" then
    out[#out + 1] = { text = seg.ssh, align = "left", fg = "#14161b", bg = "#f7768e" }
  end
  if seg.sessions and seg.sessions ~= "" then
    out[#out + 1] = { text = seg.sessions, align = "left", fg = "#c0caf5", bg = "#292e42" }
  end
  if seg.battery and seg.battery ~= "" then
    out[#out + 1] = { text = seg.battery, align = "right", fg = "#14161b", bg = "#73daca" }
  end
  if seg.hint and seg.hint ~= "" then
    out[#out + 1] = { text = seg.hint, align = "right", fg = "#a9b1d6", bg = "#24283b" }
  end
  out[#out + 1] = { text = "\u{f109}  " .. host, align = "right", fg = "#14161b", bg = "#bb9af7" }
  out[#out + 1] = { text = "\u{f017}  " .. time, align = "right", fg = "#14161b", bg = "#e0af68" }
  return out
end
