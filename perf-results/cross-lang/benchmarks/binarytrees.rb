def make_tree(depth)
  return [nil, nil] if depth == 0
  depth -= 1
  [make_tree(depth), make_tree(depth)]
end

def check_tree(node)
  left, right = node
  return 1 if left.nil?
  1 + check_tree(left) + check_tree(right)
end

n = (ARGV[0] || 10).to_i
min_depth = 4
max_depth = [min_depth + 2, n].max
stretch_depth = max_depth + 1

puts "stretch tree of depth #{stretch_depth}\t check: #{check_tree(make_tree(stretch_depth))}"

long_lived_tree = make_tree(max_depth)

(min_depth..max_depth).step(2) do |depth|
  iterations = 2 ** (max_depth - depth + min_depth)
  check = 0
  (1..iterations).each do |i|
    check += check_tree(make_tree(depth))
    check += check_tree(make_tree(depth))
  end
  puts "#{iterations * 2}\t trees of depth #{depth}\t check: #{check}"
end

puts "long lived tree of depth #{max_depth}\t check: #{check_tree(long_lived_tree)}"
