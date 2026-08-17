-- One tab, horizontal split (top / bottom)
layout = {
  name = "h-split",
  windows = {
    {
      name = "1",
      active = 1,
      panes = {
        { x = 0, y = 0, w = 1, h = 0.5 },
        { x = 0, y = 0.5, w = 1, h = 0.5 },
      },
    },
  },
}
return layout
