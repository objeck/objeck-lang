local function fannkuch(n)
  local perm1 = {}
  local count = {}
  for i = 0, n-1 do perm1[i] = i end
  for i = 0, n-1 do count[i] = i + 1 end

  local max_flips = 0
  local checksum = 0
  local sign = 1

  while true do
    if perm1[0] ~= 0 then
      local perm = {}
      for i = 0, n-1 do perm[i] = perm1[i] end
      local flips = 0
      local k = perm[0]
      while k ~= 0 do
        local lo, hi = 0, k
        while lo < hi do
          perm[lo], perm[hi] = perm[hi], perm[lo]
          lo = lo + 1; hi = hi - 1
        end
        flips = flips + 1
        k = perm[0]
      end
      if flips > max_flips then max_flips = flips end
      checksum = checksum + sign * flips
    end

    sign = -sign
    local i = 1
    while i < n do
      local v = perm1[i]
      for j = i, 1, -1 do perm1[j] = perm1[j-1] end
      perm1[0] = v
      count[i] = count[i] - 1
      if count[i] > 0 then break end
      count[i] = i + 1
      i = i + 1
    end
    if i >= n then break end
  end

  return max_flips, checksum
end

local N = tonumber(arg and arg[1]) or 7
local max_flips, checksum = fannkuch(N)
io.write(string.format("%d\nPfannkuchen(%d) = %d\n", checksum, N, max_flips))
