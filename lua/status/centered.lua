-- Balanced triad — session · focus · clock
return function(ctx)
  local sess = ctx.session or "traash"
  local title = ctx.title or "traa.sh"
  if title == "" then
    title = "traa.sh"
  end
  if #title > 48 then
    title = "…" .. title:sub(-45)
  end
  local time = ctx.time or ""
  return {
    style = "pills",
    gap = 12,
    radius = 12,
    pad_x = 14,
    v_pad = 6,
    { text = "\u{f013}  " .. sess, align = "left", fg = "#0d1117", bg = "#7aa2f7" },
    { text = "\u{f005}  " .. title, align = "center", fg = "#c0caf5", bg = "#292e42" },
    { text = "\u{f017}  " .. time, align = "right", fg = "#0d1117", bg = "#bb9af7" },
  }
end
