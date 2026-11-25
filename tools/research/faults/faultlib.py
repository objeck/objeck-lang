"""Shared deterministic fault schedule (MINSTD) used by the fake LLM server,
the Python baseline and the driver. fault_agents.obs implements the same hash,
so a (seed, agent, run, salt) tuple gives the same decision in both runtimes."""

MOD = 2147483647
MUL = 48271


def mix(seed, a, r, salt=0):
    """Pure function of its arguments (no shared state, so thread order is irrelevant)."""
    x = (seed * 7919 + a * 104729 + r * 1299709 + salt * 15485863 + 12345) % MOD
    if x == 0:
        x = 1
    for _ in range(4):
        x = (x * MUL) % MOD
    return x


def unit(seed, a, r, salt=0):
    """Uniform in [0,1)."""
    return mix(seed, a, r, salt) / float(MOD)


def is_victim(a):
    """Only every 4th agent/worker is a fault target; the others are the bystanders
    whose behaviour measures containment."""
    return a % 4 == 0


def faulted(seed, a, r, rate):
    return is_victim(a) and unit(seed, a, r, 1) < rate


def expected_result(a, r, call):
    n = (a * 31 + r * 7 + call) % 97
    return n, "R:%d" % (n * n + 7)


def expected_answer(a, r):
    return "ANSWER:%d" % ((a * 1000 + r) * 3 + 1)
