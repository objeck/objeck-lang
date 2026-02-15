import sys

def fannkuch(n):
    maxFlips = 0
    checksum = 0
    perm1 = list(range(n))
    count = list(range(1, n + 1))

    while True:
        if perm1[0]:
            perm = perm1[:]
            flips = 0
            k = perm[0]
            while k:
                perm[:k+1] = perm[k::-1]
                flips += 1
                k = perm[0]
            if flips > maxFlips:
                maxFlips = flips
            checksum += flips if (checksum & 1 == 0) else -flips
            # Simpler: alternate sign based on permutation index

        # Generate next permutation
        i = 1
        while i < n:
            perm1.insert(0, perm1.pop(i))
            count[i] -= 1
            if count[i] > 0:
                break
            count[i] = i + 1
            i += 1
        else:
            break

    return maxFlips, checksum

n = int(sys.argv[1]) if len(sys.argv) > 1 else 7
maxFlips, checksum = fannkuch(n)
print("%d\nPfannkuchen(%d) = %d" % (checksum, n, maxFlips))
