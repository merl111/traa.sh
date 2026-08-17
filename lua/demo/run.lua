local M = {}

M.steps = {
  function()
    return "clear; printf '\\n  \\033[1mtraa.sh demo\\033[0m — truecolor & attributes\\n\\n'\n"
  end,
  function()
    local s = "printf '"
    for i=0,40 do
      local r=255-i*5; local g=i*4; local b=120
      s = s .. string.format("\\033[48;2;%d;%d;%dm ", r, g, b)
    end
    s = s .. "\\033[0m\\n'\n"
    return s
  end,
  function()
    return "printf '\\033[1mbold\\033[0m \\033[3mitalic\\033[0m \\033[4munderline\\033[0m \\033[9mstrike\\033[0m\\n'\n"
  end,
  function()
    return "printf 'Unicode: αβγ 中文 🚀  ★  ♥\\n'\n"
  end,
  function()
    return "printf 'Ligatures sample: != >= <= => ->\\n'\n"
  end,
  function()
    return "printf '\\033]8;;https://traa.sh\\033\\\\hyperlink: traa.sh\\033]8;;\\033\\\\\\n'\n"
  end,
  function()
    return "printf '\\nMux: use Ctrl-b %% / \" / o / z  — theme cycle: Ctrl-Shift-T\\n'\n"
  end,
  function()
    return "printf '\\nStatus bar styles & plugins are live in the bottom bar.\\n'\n"
  end,
  function()
    return "printf '\\nDemo complete. Press q to exit demo mode.\\n'\n"
  end,
}

return M
