-- The test `tst/eng/steer.atm` on LÖVE 11 (Box2D 2.3), with the formulas
-- of `seek_point` and `steering_update` of the original
-- (`engine/game/steering.lua`), to compare the two versions of Box2D.
--   xvfb-run love tools/bench/steer

local function truncate (x, y, max)
  local l2 = x*x + y*y
  local s = max*max/l2
  s = (s > 1 and 1) or math.sqrt(s)
  return x*s, y*s
end

function love.load ()
  love.physics.setMeter(32)
  local world = love.physics.newWorld(0, 0)
  local body = love.physics.newBody(world, 100, 100, "dynamic")
  love.physics.newFixture(body, love.physics.newRectangleShape(14, 6))
  local max_v, max_f, turn = 100, 2000, 2
  local fx, fy = 0, 0
  for tick = 1, 180 do
    -- steering_update: the forces of the previous tick
    local sx, sy = truncate(fx, fy, max_f)
    body:applyForce(sx, sy)
    local vx, vy = body:getLinearVelocity()
    vx, vy = truncate(vx, vy, max_v)
    body:setLinearVelocity(vx, vy)
    -- seek_point(300, 150)
    local x, y = body:getPosition()
    local tx, ty = 300 - x, 150 - y
    local d = math.sqrt(tx*tx + ty*ty)
    fx, fy = 0, 0
    if d > 0 then
      local v = math.min(d/0.08, max_v)
      local dvx, dvy = v*tx/d, v*ty/d
      local cvx, cvy = body:getLinearVelocity()
      fx, fy = (dvx - cvx)*turn, (dvy - cvy)*turn
    end
    world:update(1/60)
    if tick % 30 == 0 then
      x, y = body:getPosition()
      print(string.format("%d\t%.1f\t%.1f", tick, x, y))
    end
  end
  love.event.quit()
end
