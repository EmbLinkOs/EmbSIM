-- SPDX-License-Identifier: Apache-2.0
-- Copyright 2026 Junior Deogracias
--
-- ULN2003A seven-channel Darlington array, as Texas Instruments' data sheet
-- describes it.
--
--   - each input drives its Darlington pair through the part's 2.7 kOhm base
--     resistor: high (2.4 V or more, the data sheet's on-state input voltage
--     at 200 mA) the output sinks to GND, low or open it is off;
--   - an output only sinks: off, it is released and the load pulls it up;
--     on, it holds the saturation voltage (0.9 V at 100 mA, 1.1 V at
--     200 mA), modelled as 0.7 V behind 2 Ohm;
--   - the outputs switch 0.25 us after their input (the data sheet's typical
--     propagation delay, either way) except under the functional policy;
--   - there is no supply pin: an input works from its own current.
--
-- Not modelled: the clamp diodes from each output to COM (an inductive
-- load's kick is not simulated), the current limits (500 mA an output, the
-- package's dissipation), the input current, outputs paralleled for more
-- current sharing it.
local M = {}

local TPD_PS = 250000 -- 0.25 us

local function update(ctx, n)
  local d = ctx.policy == "functional" and 0 or TPD_PS
  if ctx.pins["IN" .. n].value == 1 then
    ctx.drive("OUT" .. n, 0, "strong", d)
  else
    ctx.drive("OUT" .. n, "z", "strong", d)
  end
  ctx.state.on[n] = ctx.pins["IN" .. n].value == 1
end

function M.reset(ctx)
  ctx.state.on = {}
  for n = 1, 7 do update(ctx, n) end
end

function M.on_input(ctx, pin)
  local n = tonumber(pin:match("^IN(%d)$"))
  if n then update(ctx, n) end
end

function M.observe(ctx)
  local o = {}
  for n = 1, 7 do o["on" .. n] = ctx.state.on[n] or false end
  return o
end

return M
