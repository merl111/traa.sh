-- Tab 1: horizontal split. Tab 2: left column split, right full height.
layout = {
  name = "dev",
  windows = {
    {
      name = "1",
      active = 1,
      panes = {
        { x = 0, y = 0, w = 1, h = 0.5 },
        { x = 0, y = 0.5, w = 1, h = 0.5 },
      },
    },
    {
      name = "2",
      active = 1,
      panes = {
        { x = 0, y = 0, w = 0.5, h = 0.5 },
        { x = 0, y = 0.5, w = 0.5, h = 0.5 },
        { x = 0.5, y = 0, w = 0.5, h = 1 },
      },
    },
  },
}
return layout
