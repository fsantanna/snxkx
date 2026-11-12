-- Bench mode of the original SNKRX: deterministic runs with no human.
-- Copied into the checkout of the original by `tools/bench/run.sh`, and
-- loaded by `original.patch` when `SNKRX_BENCH` is set.
--
--   SNKRX_SEED=n            seed of every generator (default 1)
--   SNKRX_FRAMES=n          runs n ticks of 1/60 s, then quits
--   SNKRX_INPUT=f:op:a:b,.. at tick f: `mv:x:y` moves the mouse (game
--                           pixels), `dn:k` and `up:k` press and release
--                           a key or a mouse button (`m1`, `m2`)
--   SNKRX_SHOT=f:file,..    saves the screen at tick f
--   SNKRX_TRACE=file        writes the trace there (default stdout)
--   SNKRX_TRACE_RANDOM=1    also traces the calls to the generator
--   SNKRX_DUMP=file         dumps the data of the game (see dump.lua), quits
--   SNKRX_TRACE_POS=Class   traces the position and velocity of the objects
--                           of a class of the main group, in every tick
--   SNKRX_STRESS=n          n more seekers when an arena starts, and the
--                           units take no damage
--
-- Time is simulated: every loop runs exactly one tick, and
-- `love.timer.getTime` returns ticks * 1/60, so that runs repeat.
--
-- The trace has one line per event, `tick<TAB>event<TAB>details`:
--
--   go      a state becomes current            name
--   add     an object is added to a group      class x y
--   fire    a timer runs its action            kind delay file:line
--   rnd     a call to the generator            method result file:line
--   pos     an object, before the tick         class id x y vx vy
--   end     the run ends                       -

bench = {}

local DT = 1/60
local tick = 0

local function getenv (k, d) return os.getenv(k) or d end

local function split (s, sep)
  local t = {}
  for x in string.gmatch(s, "[^" .. sep .. "]+") do t[#t+1] = x end
  return t
end

local out = io.stdout
if os.getenv("SNKRX_TRACE") then out = assert(io.open(os.getenv("SNKRX_TRACE"), "w")) end

function bench.trace (ev, details)
  out:write(tick, "\t", ev, "\t", details or "-", "\n")
  out:flush()
end

-- An error ends the run, instead of showing the screen of errors.
function love.errorhandler (msg)
  io.stderr:write(debug.traceback("Error: " .. tostring(msg), 3), "\n")
  os.exit(1)
end

-- Steam is not available: every call is a no-op.
local nop = setmetatable({}, {__index = function (t) return t end, __call = function () end})
package.preload["luasteam"] = function () return nop end

local seed  = tonumber(getenv("SNKRX_SEED", "1"))
local last  = tonumber(getenv("SNKRX_FRAMES", "-1"))
local input = {}
for _, e in ipairs(split(getenv("SNKRX_INPUT", ""), ",")) do
  local p = split(e, ":")
  local f = tonumber(p[1])
  input[f] = input[f] or {}
  table.insert(input[f], p)
end
local shots = {}
for _, e in ipairs(split(getenv("SNKRX_SHOT", ""), ",")) do
  local p = split(e, ":")
  shots[tonumber(p[1])] = p[2]
end
local mx, my = 0, 0

local function where (level)
  local i = debug.getinfo(level, "Sl")
  return i.short_src .. ":" .. i.currentline
end

local function wrap (kind, delay, f, loc)
  if type(f) ~= "function" then return f end
  local d = type(delay) == "number" and string.format("%.3f", delay) or type(delay)
  return function (...)
    bench.trace("fire", kind .. " " .. d .. " " .. loc)
    return f(...)
  end
end

-- LuaJIT seeds its hash of strings at random, so `pairs` over the timers
-- (keyed by random strings) runs them in another order in each process,
-- and so the random numbers go to other places.  In bench mode, timers
-- run in the order they were created.
local seq = 0
local function number (triggers)
  for _, t in pairs(triggers) do
    if not t.bench_seq then seq = seq + 1; t.bench_seq = seq end
  end
end

function bench.spairs (t)
  local ks = {}
  for k in pairs(t) do ks[#ks+1] = k end
  table.sort(ks, function (a, b) return (t[a].bench_seq or 0) < (t[b].bench_seq or 0) end)
  local i = 0
  return function ()
    -- skips the timers cancelled by an earlier action in the same tick
    repeat i = i + 1 until ks[i] == nil or t[ks[i]] ~= nil
    local k = ks[i]
    if k ~= nil then return k, t[k] end
  end
end

-- (The tracing wrappers below are wrapped again by the numbering ones, so
-- the caller of a timer is 4 levels up from `where`.)

-- Called at the start of `engine_run`, when the engine is loaded.
function bench.install ()
  love.math.setRandomSeed(seed)
  local init = Random.init
  function Random:init (s) init(self, s or seed) end

  function love.timer.getTime () return tick * DT end
  function love.timer.step () return DT end
  function love.timer.sleep () end
  function love.mouse.getPosition () return mx*sx, my*sy end

  function system.load_state () state = {} end
  function system.save_state () end
  -- no saved run: "arena run" always starts a new one
  function system.load_run () return {} end
  function system.save_run () end

  local after = Trigger.after
  function Trigger:after (delay, action, tag)
    return after(self, delay, wrap("after", delay, action, where(4)), tag)
  end
  local every = Trigger.every
  function Trigger:every (delay, action, times, after, tag)
    return every(self, delay, wrap("every", delay, action, where(4)), times, after, tag)
  end
  local every_immediate = Trigger.every_immediate
  function Trigger:every_immediate (delay, action, times, after, tag)
    return every_immediate(self, delay, wrap("every", delay, action, where(4)), times, after, tag)
  end
  local cooldown = Trigger.cooldown
  function Trigger:cooldown (delay, cond, action, times, after, tag)
    return cooldown(self, delay, cond, wrap("cooldown", delay, action, where(4)), times, after, tag)
  end
  local tween = Trigger.tween
  function Trigger:tween (delay, target, source, method, after, tag)
    return tween(self, delay, target, source, method, wrap("tween", delay, after, where(4)), tag)
  end

  for _, m in ipairs{"run", "after", "cooldown", "every", "every_immediate", "during", "tween"} do
    local f = Trigger[m]
    Trigger[m] = function (self, ...)
      local r = f(self, ...)
      number(self.triggers)
      return r
    end
  end

  if os.getenv("SNKRX_TRACE_RANDOM") then
    for _, m in ipairs{"bool", "float", "int", "table", "table_remove", "sign", "weighted_pick", "uid"} do
      local f = Random[m]
      Random[m] = function (self, ...)
        local r = f(self, ...)
        if m ~= "uid" then bench.trace("rnd", m .. " " .. (type(r) == "table" and "{}" or tostring(r)) .. " " .. where(3)) end
        return r
      end
    end
  end

  local go_to = Main.go_to
  function Main:go_to (s, ...)
    local name = type(s) == "string" and s or s.name
    bench.trace("go", name)
    return go_to(self, s, ...)
  end

  -- SNKRX_STRESS=n: n more seekers when an arena starts, in a grid, and
  -- the units take no damage (for the benchmark; the port does the same
  -- with SNX_STRESS)
  local stress = tonumber(getenv("SNKRX_STRESS", ""))
  if stress then
    local hit = Player.hit
    function Player:hit (damage, ...) return hit(self, 0, ...) end
    local on_enter = Arena.on_enter
    function Arena:on_enter (...)
      on_enter(self, ...)
      for i = 0, stress-1 do
        Seeker{group = self.main, x = 60 + (i%20)*18, y = 40 + math.floor(i/20)*19, character = 'seeker', level = self.level}
      end
    end
  end

  local add = Group.add
  function Group:add (o)
    bench.trace("add", string.format("%s %.1f %.1f", system.type_name(o), o.x or 0, o.y or 0))
    return add(self, o)
  end
end

-- Called once per loop, before the events are read.
function bench.frame ()
  tick = tick + 1
  for _, p in ipairs(input[tick] or {}) do
    if p[2] == "mv" then
      mx, my = tonumber(p[3]), tonumber(p[4])
    else
      local k = p[3]
      local b = k:match("^m(%d)$")
      if b then
        love.event.push(p[2] == "dn" and "mousepressed" or "mousereleased", mx*sx, my*sy, tonumber(b))
      else
        love.event.push(p[2] == "dn" and "keypressed" or "keyreleased", k)
      end
    end
  end
  if os.getenv("SNKRX_TRACE_POS") and main and main.current and main.current.main and main.current.main.objects then
    local cls = _G[os.getenv("SNKRX_TRACE_POS")]
    for _, o in ipairs(main.current.main.objects) do
      if getmetatable(o) == cls then
        local vx, vy = 0, 0
        if o.body then vx, vy = o.body:getLinearVelocity() end
        bench.trace("pos", string.format("%s %s %.2f %.2f %.2f %.2f", os.getenv("SNKRX_TRACE_POS"), o.id:sub(1, 4), o.x, o.y, vx, vy))
      end
    end
  end
  if tick == 2 and os.getenv("SNKRX_DUMP") then
    require("dump")(os.getenv("SNKRX_DUMP"))
    love.event.quit(0)
  end
  if tick == last then
    bench.trace("end")
    love.event.quit(0)
  end
end

-- Called after a frame is drawn.
function bench.drawn ()
  local file = shots[tick]
  if file then
    love.graphics.captureScreenshot(function (img)
      local f = assert(io.open(file, "wb"))
      f:write(img:encode("png"):getString())
      f:close()
    end)
  end
end
