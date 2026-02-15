local function A(i, j)
  local ij = i + j
  return 1.0 / (ij * (ij + 1) * 0.5 + i + 1)
end

local function Av(x, y, n)
  for i = 0, n-1 do
    local val = 0
    for j = 0, n-1 do val = val + A(i, j) * x[j] end
    y[i] = val
  end
end

local function Atv(x, y, n)
  for i = 0, n-1 do
    local val = 0
    for j = 0, n-1 do val = val + A(j, i) * x[j] end
    y[i] = val
  end
end

local function AtAv(x, y, n)
  local u = {}
  for i = 0, n-1 do u[i] = 0 end
  Av(x, u, n)
  Atv(u, y, n)
end

local N = tonumber(arg and arg[1]) or 100
local u, v = {}, {}
for i = 0, N-1 do u[i] = 1.0; v[i] = 0.0 end

for _ = 1, 10 do
  AtAv(u, v, N)
  AtAv(v, u, N)
end

local vBv, vv = 0, 0
for i = 0, N-1 do
  vBv = vBv + u[i] * v[i]
  vv = vv + v[i] * v[i]
end

io.write(string.format("%.9f\n", math.sqrt(vBv / vv)))
