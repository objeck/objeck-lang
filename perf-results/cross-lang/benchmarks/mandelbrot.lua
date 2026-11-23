-- CLBG mandelbrot, single-threaded, same algorithm as programs/tests/clbg/mandelbrot.obs
local n = tonumber(arg and arg[1]) or 200
local out = io.stdout
out:write("P4\n" .. n .. " " .. n .. "\n")
local inv = 2.0 / n
local tail = 8 - (n % 8)
local char, unpack = string.char, unpack or table.unpack
for y = 0, n - 1 do
  local civ = y * inv - 1.0
  local row = {}
  local nrow = 0
  local acc = 0
  local bit = 0
  for x = 0, n - 1 do
    local crv = x * inv - 1.5
    local zr, zi = crv, civ
    local tr, ti = crv * crv, civ * civ
    local i = 49
    local t
    repeat
      zi = zr * zi + zr * zi + civ
      zr = tr - ti + crv
      tr = zr * zr
      ti = zi * zi
      t = tr + ti
      if t <= 4.0 then i = i - 1 end
    until not (t <= 4.0 and i > 0)
    acc = acc * 2
    if i == 0 then acc = acc + 1 end
    bit = bit + 1
    if bit == 8 then
      nrow = nrow + 1; row[nrow] = acc
      acc = 0; bit = 0
    end
  end
  if bit ~= 0 then
    acc = acc * 2 ^ tail
    nrow = nrow + 1; row[nrow] = acc
  end
  out:write(char(unpack(row, 1, nrow)))
end
out:flush()
