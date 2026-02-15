local function make_tree(depth)
  if depth > 0 then
    depth = depth - 1
    return { make_tree(depth), make_tree(depth) }
  end
  return { false, false }
end

local function check_tree(node)
  if node[1] then
    return 1 + check_tree(node[1]) + check_tree(node[2])
  end
  return 1
end

local N = tonumber(arg and arg[1]) or 10
local min_depth = 4
local max_depth = math.max(min_depth + 2, N)
local stretch_depth = max_depth + 1

io.write(string.format("stretch tree of depth %d\t check: %d\n", stretch_depth, check_tree(make_tree(stretch_depth))))

local long_lived_tree = make_tree(max_depth)

for depth = min_depth, max_depth, 2 do
  local iterations = 2 ^ (max_depth - depth + min_depth)
  local check = 0
  for i = 1, iterations do
    check = check + check_tree(make_tree(depth))
    check = check + check_tree(make_tree(depth))
  end
  io.write(string.format("%d\t trees of depth %d\t check: %d\n", iterations * 2, depth, check))
end

io.write(string.format("long lived tree of depth %d\t check: %d\n", max_depth, check_tree(long_lived_tree)))
