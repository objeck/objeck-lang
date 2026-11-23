# CLBG mandelbrot, single-threaded, same algorithm as programs/tests/clbg/mandelbrot.obs
n = (ARGV[0] || 200).to_i
out = $stdout
out.binmode
out.write("P4\n#{n} #{n}\n")
inv = 2.0 / n
tail = 8 - (n & 7)
n.times do |y|
  civ = y * inv - 1.0
  row = []
  acc = 0
  bit = 0
  n.times do |x|
    crv = x * inv - 1.5
    zr = crv
    zi = civ
    tr = crv * crv
    ti = civ * civ
    i = 49
    loop do
      zi = zr * zi + zr * zi + civ
      zr = tr - ti + crv
      tr = zr * zr
      ti = zi * zi
      t = tr + ti
      i -= 1 if t <= 4.0
      break unless t <= 4.0 && i > 0
    end
    acc <<= 1
    acc += 1 if i == 0
    bit += 1
    if bit == 8
      row << acc
      acc = 0
      bit = 0
    end
  end
  if bit != 0
    acc <<= tail
    row << acc
  end
  out.write(row.pack("C*"))
end
out.flush
