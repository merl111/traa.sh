-- Desktop / log notification when the terminal rings BEL
local last_notification = 0

return {
  setup = function()
    traash.on("on_bell", function()
      local now = os.time()
      if now - last_notification >= 15 then
        last_notification = now
        traash.notify("Bell in traa.sh")
      end
    end)
    traash.log("notify-on-bell: listening for background BEL")
  end,
}
