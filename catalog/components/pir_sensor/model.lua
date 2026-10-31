-- HC-SR501 passive-infrared motion module (its BISS0001 controller), as its
-- datasheet describes it:
--   * after power-up it warms up (about a minute, `warmup`): OUT stays low
--     and motion is not seen;
--   * motion raises OUT for the hold time Tx (the module's "time" trimmer,
--     about 3 s to 300 s, `hold`);
--   * with the jumper at H (`retrigger`, the module's default) motion during
--     Tx restarts it, so OUT falls Tx after the last motion; at L (single
--     trigger) OUT falls Tx after it rose, whatever happens meanwhile;
--   * after OUT falls the BISS0001 blocks for Ti (about 2.5 s, `lockout`):
--     motion then is not seen, and motion still present when it ends
--     triggers again.
-- Motion is the `motion` input, set by hand or, left unset, read from the
-- world: a warm body (a person, an animal) moving within the lens's field
-- of view (`fov`, about 110 degrees) and range (`range`, 3 to 7 m by the
-- sensitivity trimmer), looked at every 100 ms (the pyroelectric element and
-- its amplifier respond to changes slower than that).
local M = {}
local POLL = 100e9 -- ps
local OFF, POLL_TOKEN, RECHECK = 0, 1, 2

local function ps(s) return math.floor(s * 1e12 + 0.5) end
local function seen(v) return v == true or (type(v) == "number" and v ~= 0) end

local function blocked(ctx)
  local s = ctx.state
  return ctx.now() < s.ready_at or ctx.now() < s.lock_until
end

local function trigger(ctx)
  local s = ctx.state
  if s.on then
    if ctx.params.retrigger ~= false then ctx.cancel(OFF) end -- H: Tx starts again when motion ends
    return
  end
  s.on = true
  ctx.drive("OUT", 1)
  if ctx.params.retrigger == false then ctx.wake_at(ctx.now() + ps(ctx.params.hold), OFF) end -- L: Tx from now
end

-- motion starting or ending
local function motion(ctx, m)
  local s = ctx.state
  if m == s.motion then return end
  s.motion = m
  if m then
    if not blocked(ctx) then trigger(ctx) end
  elseif s.on and ctx.params.retrigger ~= false then
    ctx.wake_at(ctx.now() + ps(ctx.params.hold), OFF)
  end
end

function M.reset(ctx)
  local s = ctx.state
  ctx.drive("OUT", 0)
  s.on = false
  s.motion = false
  s.ready_at = ctx.now() + ps(ctx.params.warmup or 0)
  s.lock_until = 0
  if s.ready_at > ctx.now() then ctx.wake_at(s.ready_at, RECHECK) end
  ctx.wake_at(ctx.now() + POLL, POLL_TOKEN)
end

function M.on_set(ctx, name, value)
  if name ~= "motion" then return false end
  motion(ctx, seen(value))
end

function M.on_time(ctx, token)
  local s = ctx.state
  if token == OFF then
    s.on = false
    ctx.drive("OUT", 0)
    s.lock_until = ctx.now() + ps(ctx.params.lockout or 0)
    if s.lock_until > ctx.now() then ctx.wake_at(s.lock_until, RECHECK) else token = RECHECK end
  end
  if token == RECHECK then
    -- warmed up, or the block has ended: motion still there triggers
    if s.motion and not blocked(ctx) and not s.on then trigger(ctx) end -- (at H, Tx starts when the motion ends)
  elseif token == POLL_TOKEN then
    motion(ctx, seen(ctx.input("motion")))
    ctx.wake_at(ctx.now() + POLL, POLL_TOKEN)
  end
end

function M.observe(ctx) return { active = ctx.state.on } end
return M
