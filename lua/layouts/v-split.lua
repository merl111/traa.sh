-- One tab, vertical split (left / right)
layout = {
  name = "v-split",
  windows = {
    {
      name = "1",
      active = 1,
      panes = {
        { x = 0, y = 0, w = 0.5, h = 1 },
        { x = 0.5, y = 0, w = 0.5, h = 1 },
      },
    },
  },
}
return layout
