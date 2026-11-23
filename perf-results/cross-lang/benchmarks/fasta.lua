-- CLBG fasta, single-threaded, same algorithm as programs/tests/clbg/fasta.obs
local IM, IA, IC = 139968, 3877, 29573
local ALU = "GGCCGGGCGCGGTGGCTCACGCCTGTAATCCCAGCACTTTGGGAGGCCGAGGCGGGCGGATCACCTGAGGTCAGGAGTTCGAGACCAGCCTGGCCAACATGGTGAAACCCCGTCTCTACTAAAAATACAAAAATTAGCCGGGCGTGGTGGCGCGCGCCTGTAATCCCAGCTACTCGGGAGGCTGAGGCAGGAGAATCGCTTGAACCCGGGAGGCGGAGGTTGCAGTGAGCCGAGATCGCGCCACTGCACTCCAGCCTGGGCGACAGAGCGAGACTCCGTCTCAAAAA"
local IUB = { {"a",0.27},{"c",0.12},{"g",0.12},{"t",0.27} }
for c in ("BDHKMNRSVWY"):gmatch(".") do IUB[#IUB + 1] = { c, 0.02 } end
local HOMO = { {"a",0.3029549426680},{"c",0.1979883004921},{"g",0.1975473066391},{"t",0.3015094502008} }
local last = 42

local function cumulative(t)
  local cp, o = 0.0, {}
  for i, e in ipairs(t) do cp = cp + e[2]; o[i] = { e[1], cp } end
  return o
end

local out = io.stdout
local sub, concat = string.sub, table.concat

local function repeat_fasta(id, desc, n)
  out:write(">" .. id .. " " .. desc .. "\n")
  local k, kn = 0, #ALU
  local buf = ALU:rep(math.floor(60 / kn) + 3)
  while n > 0 do
    local m = n >= 60 and 60 or n
    out:write(sub(buf, k + 1, k + m), "\n")
    k = (k + m) % kn
    n = n - 60
  end
end

local function random_fasta(id, desc, t, n)
  out:write(">" .. id .. " " .. desc .. "\n")
  local size = #t
  local chars, probs = {}, {}
  for i = 1, size do chars[i] = t[i][1]; probs[i] = t[i][2] end
  local lines, nl = {}, 0
  while n > 0 do
    local m = n >= 60 and 60 or n
    local line = {}
    for j = 1, m do
      last = (last * IA + IC) % IM
      local r = 1.0 * last / IM
      local pick = chars[size]
      for i = 1, size do
        if r < probs[i] then pick = chars[i]; break end
      end
      line[j] = pick
    end
    nl = nl + 1; lines[nl] = concat(line)
    if nl == 512 then out:write(concat(lines, "\n"), "\n"); lines = {}; nl = 0 end
    n = n - 60
  end
  if nl > 0 then out:write(concat(lines, "\n"), "\n") end
end

local n = tonumber(arg and arg[1]) or 1000
repeat_fasta("ONE", "Homo sapiens alu", n * 2)
random_fasta("TWO", "IUB ambiguity codes", cumulative(IUB), n * 3)
random_fasta("THREE", "Homo sapiens frequency", cumulative(HOMO), n * 5)
out:flush()
