-- Neon workshop strip — code, path, branch hint, clock
return function(ctx)
  local seg = ctx.seg or {}
  local title = seg.cwd or ctx.title or "~"
  if title == "" then
    title = "~"
  end
  if #title > 42 then
    title = "…" .. title:sub(-39)
  end
  local time = ctx.time or ""
  local win = tostring(ctx.window or 1)
  local branch = (seg.git and seg.git ~= "") and seg.git or "\u{f126}  —"
  local out = {
    style = "pills",
    gap = 8,
    radius = 10,
    pad_x = 12,
    v_pad = 6,
    { text = "\u{f121}  DEV", align = "left", fg = "#0f1115", bg = "#9ece6a" },
    { text = "\u{f1c9}  w" .. win, align = "left", fg = "#c0caf5", bg = "#1a1b26" },
    { text = "\u{f07c}  " .. title, align = "left", fg = "#c0caf5", bg = "#24283b" },
    { text = branch, align = "left", fg = "#e0af68", bg = "#1f2335" },
  }
  if seg.ssh and seg.ssh ~= "" then
    out[#out + 1] = { text = seg.ssh, align = "right", fg = "#0f1115", bg = "#f7768e" }
  end
  if seg.battery and seg.battery ~= "" then
    out[#out + 1] = { text = seg.battery, align = "right", fg = "#0f1115", bg = "#73daca" }
  end
  out[#out + 1] = { text = "\u{f017}  " .. time, align = "right", fg = "#0f1115", bg = "#7aa2f7" }
  return out
end
