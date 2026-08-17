-- Battery percentage chip (Linux sysfs)
local function read_bat(name)
  local base = "/sys/class/power_supply/" .. name
  local cap = io.open(base .. "/capacity")
  if not cap then
    return nil
  end
  local pct = cap:read("*l") or "?"
  cap:close()
  local st = io.open(base .. "/status")
  local status = st and (st:read("*l") or "") or ""
  if st then
    st:close()
  end
  local icon = "\u{f240}"
  if status:find("Charging") then
    icon = "\u{f0e7}"
  elseif tonumber(pct) and tonumber(pct) <= 20 then
    icon = "\u{f244}"
  end
  return icon .. "  " .. pct .. "%"
end

return {
  setup = function()
    traash.segments.battery = function()
      return read_bat("BAT0") or read_bat("BAT1") or ""
    end
    traash.log("battery: status chip enabled")
  end,
}
