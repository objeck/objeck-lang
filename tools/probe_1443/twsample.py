# Samples TCP state counts every second until <stopfile> exists; prints the
# maximum of each state and a line every 10 s. usage: twsample.py <stopfile>
import os, subprocess, sys, time
from collections import Counter

stop = sys.argv[1]
peak = Counter(); t0 = time.time(); last = 0
while not os.path.exists(stop):
    out = subprocess.run(["netstat", "-ano", "-p", "tcp"], capture_output=True, text=True).stdout
    c = Counter(l.split()[3] for l in out.splitlines() if len(l.split()) >= 4 and l.split()[0] == "TCP")
    for k, v in c.items(): peak[k] = max(peak[k], v)
    if time.time() - last >= 10:
        print("t=%4.0fs %s" % (time.time() - t0, dict(c)), flush=True); last = time.time()
    time.sleep(1)
print("peak", dict(peak), flush=True)
