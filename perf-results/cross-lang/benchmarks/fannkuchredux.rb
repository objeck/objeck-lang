def fannkuch(n)
  perm1 = (0...n).to_a
  count = (1..n).to_a
  max_flips = 0
  checksum = 0
  sign = 1

  loop do
    if perm1[0] != 0
      perm = perm1.dup
      flips = 0
      k = perm[0]
      while k != 0
        perm[0..k] = perm[0..k].reverse
        flips += 1
        k = perm[0]
      end
      max_flips = flips if flips > max_flips
      checksum += sign * flips
    end

    sign = -sign
    i = 1
    while i < n
      perm1.insert(0, perm1.delete_at(i))
      count[i] -= 1
      break if count[i] > 0
      count[i] = i + 1
      i += 1
    end
    break if i >= n
  end

  [max_flips, checksum]
end

n = (ARGV[0] || 7).to_i
max_flips, checksum = fannkuch(n)
puts "#{checksum}\nPfannkuchen(#{n}) = #{max_flips}"
