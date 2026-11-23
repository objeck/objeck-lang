# CLBG fasta, single-threaded, same algorithm as programs/tests/clbg/fasta.obs
IM = 139968
IA = 3877
IC = 29573
ALU = "GGCCGGGCGCGGTGGCTCACGCCTGTAATCCCAGCACTTTGGGAGGCCGAGGCGGGCGGATCACCTGAGGTCAGGAGTTCGAGACCAGCCTGGCCAACATGGTGAAACCCCGTCTCTACTAAAAATACAAAAATTAGCCGGGCGTGGTGGCGCGCGCCTGTAATCCCAGCTACTCGGGAGGCTGAGGCAGGAGAATCGCTTGAACCCGGGAGGCGGAGGTTGCAGTGAGCCGAGATCGCGCCACTGCACTCCAGCCTGGGCGACAGAGCGAGACTCCGTCTCAAAAA"
IUB = [["a", 0.27], ["c", 0.12], ["g", 0.12], ["t", 0.27]] + "BDHKMNRSVWY".chars.map { |c| [c, 0.02] }
HOMO = [["a", 0.3029549426680], ["c", 0.1979883004921], ["g", 0.1975473066391], ["t", 0.3015094502008]]
$last = 42

def cumulative(table)
  cp = 0.0
  table.map { |c, p| cp += p; [c, cp] }
end

def repeat_fasta(out, id, desc, n)
  out.write(">#{id} #{desc}\n")
  k = 0
  kn = ALU.size
  buf = ALU * (60 / kn + 3)
  while n > 0
    m = n >= 60 ? 60 : n
    out.write(buf[k, m] + "\n")
    k = (k + m) % kn
    n -= 60
  end
end

def random_fasta(out, id, desc, table, n)
  out.write(">#{id} #{desc}\n")
  chars = table.map { |c, _| c }
  probs = table.map { |_, p| p }
  size = table.size
  lines = []
  while n > 0
    m = n >= 60 ? 60 : n
    line = +""
    m.times do
      $last = ($last * IA + IC) % IM
      r = 1.0 * $last / IM
      pick = chars[size - 1]
      i = 0
      while i < size
        if r < probs[i]
          pick = chars[i]
          break
        end
        i += 1
      end
      line << pick
    end
    lines << line
    if lines.size == 512
      out.write(lines.join("\n") + "\n")
      lines = []
    end
    n -= 60
  end
  out.write(lines.join("\n") + "\n") unless lines.empty?
end

n = (ARGV[0] || 1000).to_i
out = $stdout
repeat_fasta(out, "ONE", "Homo sapiens alu", n * 2)
random_fasta(out, "TWO", "IUB ambiguity codes", cumulative(IUB), n * 3)
random_fasta(out, "THREE", "Homo sapiens frequency", cumulative(HOMO), n * 5)
out.flush
