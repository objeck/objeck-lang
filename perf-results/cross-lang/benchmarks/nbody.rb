PI = 3.141592653589793
SOLAR_MASS = 4 * PI * PI
DAYS_PER_YEAR = 365.24

class Body
  attr_accessor :x, :y, :z, :vx, :vy, :vz, :mass
  def initialize(x, y, z, vx, vy, vz, mass)
    @x, @y, @z = x, y, z
    @vx, @vy, @vz = vx, vy, vz
    @mass = mass
  end
end

def offset_momentum(bodies)
  px = py = pz = 0.0
  bodies.each do |b|
    px += b.vx * b.mass; py += b.vy * b.mass; pz += b.vz * b.mass
  end
  b = bodies[0]
  b.vx = -px / SOLAR_MASS; b.vy = -py / SOLAR_MASS; b.vz = -pz / SOLAR_MASS
end

def energy(bodies)
  e = 0.0
  bodies.each_with_index do |bi, i|
    e += 0.5 * bi.mass * (bi.vx*bi.vx + bi.vy*bi.vy + bi.vz*bi.vz)
    (i+1...bodies.size).each do |j|
      bj = bodies[j]
      dx = bi.x - bj.x; dy = bi.y - bj.y; dz = bi.z - bj.z
      e -= (bi.mass * bj.mass) / Math.sqrt(dx*dx + dy*dy + dz*dz)
    end
  end
  e
end

def advance(bodies, dt, n)
  n.times do
    bodies.each_with_index do |bi, i|
      (i+1...bodies.size).each do |j|
        bj = bodies[j]
        dx = bi.x - bj.x; dy = bi.y - bj.y; dz = bi.z - bj.z
        dsq = dx*dx + dy*dy + dz*dz
        mag = dt / (dsq * Math.sqrt(dsq))
        bm = bj.mass * mag; bi.vx -= dx * bm; bi.vy -= dy * bm; bi.vz -= dz * bm
        bm = bi.mass * mag; bj.vx += dx * bm; bj.vy += dy * bm; bj.vz += dz * bm
      end
    end
    bodies.each do |b|
      b.x += dt * b.vx; b.y += dt * b.vy; b.z += dt * b.vz
    end
  end
end

bodies = [
  Body.new(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, SOLAR_MASS),
  Body.new(4.84143144246472090e+00, -1.16032004402742839e+00, -1.03622044471123109e-01,
           1.66007664274403694e-03*DAYS_PER_YEAR, 7.69901118419740425e-03*DAYS_PER_YEAR, -6.90460016972063023e-05*DAYS_PER_YEAR,
           9.54791938424326609e-04*SOLAR_MASS),
  Body.new(8.34336671824457987e+00, 4.12479856412430479e+00, -4.03523417114321381e-01,
           -2.76742510726862411e-03*DAYS_PER_YEAR, 4.99852801234917238e-03*DAYS_PER_YEAR, 2.30417297573763929e-05*DAYS_PER_YEAR,
           2.85885980666130812e-04*SOLAR_MASS),
  Body.new(1.28943695621391310e+01, -1.51111514016986312e+01, -2.23307578892655734e-01,
           2.96460137564761618e-03*DAYS_PER_YEAR, 2.37847173959480950e-03*DAYS_PER_YEAR, -2.96589568540237556e-05*DAYS_PER_YEAR,
           4.36624404335156298e-05*SOLAR_MASS),
  Body.new(1.53796971148509165e+01, -2.59193146099879641e+01, 1.79258772950371181e-01,
           2.68067772490389322e-03*DAYS_PER_YEAR, 1.62824170038242295e-03*DAYS_PER_YEAR, -9.51592254519715870e-05*DAYS_PER_YEAR,
           5.15138902046611451e-05*SOLAR_MASS),
]

n = (ARGV[0] || 1000).to_i
offset_momentum(bodies)
puts "%.9f" % energy(bodies)
advance(bodies, 0.01, n)
puts "%.9f" % energy(bodies)
