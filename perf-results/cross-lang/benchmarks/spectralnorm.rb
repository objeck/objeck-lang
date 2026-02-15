def a(i, j)
  1.0 / ((i + j) * (i + j + 1) / 2 + i + 1)
end

def av(x, y, n)
  n.times do |i|
    val = 0.0
    n.times { |j| val += a(i, j) * x[j] }
    y[i] = val
  end
end

def atv(x, y, n)
  n.times do |i|
    val = 0.0
    n.times { |j| val += a(j, i) * x[j] }
    y[i] = val
  end
end

def atav(x, y, n)
  u = Array.new(n, 0.0)
  av(x, u, n)
  atv(u, y, n)
end

n = (ARGV[0] || 100).to_i
u = Array.new(n, 1.0)
v = Array.new(n, 0.0)

10.times do
  atav(u, v, n)
  atav(v, u, n)
end

vBv = vv = 0.0
n.times do |i|
  vBv += u[i] * v[i]
  vv += v[i] * v[i]
end

printf("%.9f\n", Math.sqrt(vBv / vv))
