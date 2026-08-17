-- Single full-window pane
layout = {
  name = "single",
  windows = {
    {
      name = "1",
      active = 1,
      panes = {
        { x = 0, y = 0, w = 1, h = 1 },
      },
    },
  },
}
return layout
