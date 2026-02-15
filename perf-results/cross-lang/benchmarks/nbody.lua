local PI = 3.141592653589793
local SOLAR_MASS = 4 * PI * PI
local DAYS_PER_YEAR = 365.24
local sqrt = math.sqrt

local bodies = {
  { x=0, y=0, z=0, vx=0, vy=0, vz=0, mass=SOLAR_MASS },
  { x=4.84143144246472090e+00, y=-1.16032004402742839e+00, z=-1.03622044471123109e-01,
    vx=1.66007664274403694e-03*DAYS_PER_YEAR, vy=7.69901118419740425e-03*DAYS_PER_YEAR, vz=-6.90460016972063023e-05*DAYS_PER_YEAR,
    mass=9.54791938424326609e-04*SOLAR_MASS },
  { x=8.34336671824457987e+00, y=4.12479856412430479e+00, z=-4.03523417114321381e-01,
    vx=-2.76742510726862411e-03*DAYS_PER_YEAR, vy=4.99852801234917238e-03*DAYS_PER_YEAR, vz=2.30417297573763929e-05*DAYS_PER_YEAR,
    mass=2.85885980666130812e-04*SOLAR_MASS },
  { x=1.28943695621391310e+01, y=-1.51111514016986312e+01, z=-2.23307578892655734e-01,
    vx=2.96460137564761618e-03*DAYS_PER_YEAR, vy=2.37847173959480950e-03*DAYS_PER_YEAR, vz=-2.96589568540237556e-05*DAYS_PER_YEAR,
    mass=4.36624404335156298e-05*SOLAR_MASS },
  { x=1.53796971148509165e+01, y=-2.59193146099879641e+01, z=1.79258772950371181e-01,
    vx=2.68067772490389322e-03*DAYS_PER_YEAR, vy=1.62824170038242295e-03*DAYS_PER_YEAR, vz=-9.51592254519715870e-05*DAYS_PER_YEAR,
    mass=5.15138902046611451e-05*SOLAR_MASS },
}

local function offset_momentum(b)
  local px, py, pz = 0, 0, 0
  for i=1,#b do px=px+b[i].vx*b[i].mass; py=py+b[i].vy*b[i].mass; pz=pz+b[i].vz*b[i].mass end
  b[1].vx=-px/SOLAR_MASS; b[1].vy=-py/SOLAR_MASS; b[1].vz=-pz/SOLAR_MASS
end

local function energy(b)
  local e = 0
  for i=1,#b do
    local bi = b[i]
    e = e + 0.5 * bi.mass * (bi.vx*bi.vx + bi.vy*bi.vy + bi.vz*bi.vz)
    for j=i+1,#b do
      local bj = b[j]
      local dx, dy, dz = bi.x-bj.x, bi.y-bj.y, bi.z-bj.z
      e = e - (bi.mass * bj.mass) / sqrt(dx*dx + dy*dy + dz*dz)
    end
  end
  return e
end

local function advance(b, dt, n)
  for _=1,n do
    for i=1,#b do
      local bi = b[i]
      for j=i+1,#b do
        local bj = b[j]
        local dx, dy, dz = bi.x-bj.x, bi.y-bj.y, bi.z-bj.z
        local dsq = dx*dx + dy*dy + dz*dz
        local mag = dt / (dsq * sqrt(dsq))
        local bm = bj.mass * mag
        bi.vx = bi.vx - dx*bm; bi.vy = bi.vy - dy*bm; bi.vz = bi.vz - dz*bm
        bm = bi.mass * mag
        bj.vx = bj.vx + dx*bm; bj.vy = bj.vy + dy*bm; bj.vz = bj.vz + dz*bm
      end
    end
    for i=1,#b do
      local bi = b[i]
      bi.x = bi.x + dt*bi.vx; bi.y = bi.y + dt*bi.vy; bi.z = bi.z + dt*bi.vz
    end
  end
end

local N = tonumber(arg and arg[1]) or 1000
offset_momentum(bodies)
io.write(string.format("%.9f\n", energy(bodies)))
advance(bodies, 0.01, N)
io.write(string.format("%.9f\n", energy(bodies)))
