#!/usr/bin/env python3
"""
perf_run.py <bin_dir> <work_dir> <startup|json|coll|compile|all> [options]

The measure-first suite for startup, JSON, strings/collections and compile time (Linux; uses wait4 for
the child's CPU time and peak resident size). <bin_dir> holds obc and obr, the libraries are read from
the sibling lib/ directory; <work_dir> receives the compiled programs and generated inputs.

Every number is the best of --runs runs (default 7): wall time, user+sys CPU time, peak RSS. On a shared
machine the CPU time is the steadier of the two; with PERF_IR_PREFIX set to a command prefix that counts
instructions (for example a container running `valgrind --tool=callgrind --callgrind-out-file=/dev/null`,
or `perf stat -x, -e instructions:u`), --ir adds an instruction count, which does not move with load.

  startup   obr hello.obe; obr --mcp to the answer to 'initialize'; the first agent run (ScriptedBackend)
  json      parse / serialize MB/s per document (gen_json.py), with Python and Node on the same files
  coll      coll_bench tests at n = 1e3 .. 1e6 with n * repetitions held constant: ns per element, and the
            ratio between the largest and smallest size (about 1 when linear)
  compile   obc on generated pathological sources (gen_compile.py) and, with --corpus DIR, on every .obs
            of a directory (the regression corpus)

Options: --runs N, --ir, --modes default,jit1,nojit (coll, json), --only SUBSTR, --json-out FILE,
         --coll-sizes 1e3,1e4,1e5 (default up to 1e6), --coll-total N (n * repetitions, default 2e6)
"""
import json, os, re, resource, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))


class Ctx:
    def __init__(self, bin_dir, work, runs, ir):
        self.bin = os.path.abspath(bin_dir)
        self.work = os.path.abspath(work)
        self.obc = os.path.join(self.bin, "obc")
        self.obr = os.path.join(self.bin, "obr")
        self.runs = runs
        self.ir = ir and os.environ.get("PERF_IR_PREFIX")
        self.env = dict(os.environ)
        self.env["OBJECK_LIB_PATH"] = os.path.join(os.path.dirname(self.bin), "lib")
        os.makedirs(self.work, exist_ok=True)


def run_once(cmd, env, stdin_text=None, first_line=False):
    """One run: (wall seconds, cpu seconds, peak RSS KB, stdout text, seconds to the first stdout line)."""
    start = time.perf_counter()
    p = subprocess.Popen(cmd, env=env, stdin=subprocess.PIPE if stdin_text is not None else subprocess.DEVNULL,
                         stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    first = None
    out = b""
    if stdin_text is not None:
        p.stdin.write(stdin_text.encode())
        p.stdin.flush()
        if first_line:
            out = p.stdout.readline()
            first = time.perf_counter() - start
        p.stdin.close()
    out += p.stdout.read()
    _, status, ru = os.wait4(p.pid, 0)
    wall = time.perf_counter() - start
    p.returncode = os.waitstatus_to_exitcode(status)
    if p.returncode != 0:
        raise RuntimeError("exit %d: %s" % (p.returncode, " ".join(cmd)))
    return wall, ru.ru_utime + ru.ru_stime, ru.ru_maxrss, out.decode("utf-8", "replace"), first


def count_ir(ctx, cmd, env, stdin_text=None):
    prefix = ctx.ir.split()
    p = subprocess.run(prefix + cmd, env=env, input=(stdin_text or "").encode(), stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE)
    text = p.stderr.decode("utf-8", "replace")
    m = re.search(r"Collected : (\d+)", text) or re.search(r"^(\d+),,instructions", text, re.M)
    return int(m.group(1)) if m else None


def measure(ctx, cmd, env=None, stdin_text=None, first_line=False, runs=None):
    env = env or ctx.env
    best = None
    for _ in range(runs or ctx.runs):
        wall, cpu, rss, out, first = run_once(cmd, env, stdin_text, first_line)
        if best is None:
            best = {"wall": wall, "cpu": cpu, "rss_kb": rss, "out": out.strip(), "first": first}
        else:
            best["wall"] = min(best["wall"], wall)
            best["cpu"] = min(best["cpu"], cpu)
            best["rss_kb"] = max(best["rss_kb"], rss)
            if first is not None:
                best["first"] = min(best["first"], first)
    best["rss_kb"] = true_rss(ctx, cmd, env, stdin_text, best["rss_kb"])
    if ctx.ir:
        best["ir"] = count_ir(ctx, cmd, env, stdin_text)
    return best


def true_rss(ctx, cmd, env, stdin_text, fallback):
    """Peak RSS through /usr/bin/time: wait4's ru_maxrss also counts this Python process as it was at the fork."""
    if not os.path.exists("/usr/bin/time"):
        return fallback
    note = os.path.join(ctx.work, "rss.txt")
    p = subprocess.run(["/usr/bin/time", "-f", "%M", "-o", note] + cmd, env=env, input=(stdin_text or "").encode(),
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        return int(open(note).read().split()[-1])
    except (OSError, ValueError, IndexError):
        return fallback


def compile_obs(ctx, name, libs, extra=()):
    src = os.path.join(HERE, name + ".obs")
    dest = os.path.join(ctx.work, name + ".obe")
    cmd = [ctx.obc, "-src", src, "-dest", dest] + (["-lib", libs] if libs else []) + list(extra)
    p = subprocess.run(cmd, env=ctx.env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if p.returncode != 0 or not os.path.exists(dest):
        raise RuntimeError("compile failed: %s\n%s" % (name, p.stdout.decode("utf-8", "replace")[-2000:]))
    return dest


def mode_cmd(ctx, mode, obe, args):
    env = dict(ctx.env)
    cmd = [ctx.obr]
    if mode == "jit1":
        env["OBJECK_JIT_THRESHOLD"] = "1"
    elif mode == "nojit":
        cmd.append("--jit=off")
    return cmd + [obe] + [str(a) for a in args], env


def fmt_ir(r):
    return ("  %8.2f M instr" % (r["ir"] / 1e6)) if r.get("ir") else ""


# ---------------------------------------------------------------- startup
def do_startup(ctx, opts, results):
    print("== startup and footprint (best of %d)" % ctx.runs)
    hello = compile_obs(ctx, "hello", "")
    shop = compile_obs(ctx, "mcp_shop", "gen_collect,json,net,net_server,cipher,misc,mcp")
    agent = compile_obs(ctx, "agent_first", "agent,net,json,cipher,misc,gen_collect")
    init = json.dumps({"jsonrpc": "2.0", "id": 1, "method": "initialize",
                       "params": {"protocolVersion": "2025-11-25", "capabilities": {},
                                  "clientInfo": {"name": "t", "version": "1"}}}) + "\n"
    rows = [("hello", [ctx.obr, hello], None, False),
            ("hello --jit=off", [ctx.obr, "--jit=off", hello], None, False),
            ("mcp initialize", [ctx.obr, "--mcp", shop], init, True),
            ("agent first run", [ctx.obr, agent, "1"], None, False),
            ("agent 200 runs", [ctx.obr, agent, "200"], None, False)]
    print("  %-18s %9s %9s %9s %9s %10s" % ("case", "obe KB", "wall ms", "cpu ms", "RSS MB", "first ms"))
    for label, cmd, stdin_text, first in rows:
        if opts.get("only") and opts["only"] not in label:
            continue
        r = measure(ctx, cmd, stdin_text=stdin_text, first_line=first)
        size = os.path.getsize(cmd[-1] if cmd[-1].endswith(".obe") else cmd[-2]) / 1024.0
        print("  %-18s %9.1f %9.2f %9.2f %9.1f %10s%s" % (label, size, r["wall"] * 1e3, r["cpu"] * 1e3, r["rss_kb"] / 1024.0,
                                                         ("%.2f" % (r["first"] * 1e3)) if r["first"] else "-", fmt_ir(r)))
        results["startup/" + label] = {k: v for k, v in r.items() if k != "out"}


# ---------------------------------------------------------------- json
PY_JSON = r'''
import json, sys, time
text = open(sys.argv[1], encoding="ascii").read(); n = int(sys.argv[2])
t = time.process_time()
for _ in range(n): doc = json.loads(text)
p = time.process_time() - t
t = time.process_time()
for _ in range(n): out = json.dumps(doc, separators=(",", ":"))
s = time.process_time() - t
print("%f %f" % (p, s))
'''
NODE_JSON = r'''
const fs = require("fs"); const text = fs.readFileSync(process.argv[2], "latin1"); const n = +process.argv[3];
let doc, out; let t = process.cpuUsage();
for (let i = 0; i < n; i++) doc = JSON.parse(text);
let d = process.cpuUsage(t); const p = (d.user + d.system) / 1e6; t = process.cpuUsage();
for (let i = 0; i < n; i++) out = JSON.stringify(doc);
d = process.cpuUsage(t); console.log(p + " " + (d.user + d.system) / 1e6);
'''


def do_json(ctx, opts, results):
    docs = os.path.join(ctx.work, "json")
    subprocess.run([sys.executable, os.path.join(HERE, "gen_json.py"), docs], check=True, stdout=subprocess.DEVNULL)
    obe = compile_obs(ctx, "json_bench", "json,gen_collect")
    target = float(opts.get("json_mb", 6)) * 1e6
    node = os.environ.get("PERF_NODE")  # a command prefix that runs node, when there is one
    print("== JSON throughput, MB/s of CPU time (best of %d; fixed cost subtracted)" % ctx.runs)
    print("  %-13s %9s %-8s %9s %9s %9s %9s %9s" % ("document", "bytes", "mode", "parse", "serialize", "RSS MB", "py parse", "py ser"))
    for name in ("small", "medium", "large", "deep", "strings", "numbers"):
        if opts.get("only") and opts["only"] not in name:
            continue
        path = os.path.join(docs, name + ".json")
        size = os.path.getsize(path)
        iters = max(1, int(target / size))
        py = subprocess.run([sys.executable, "-c", PY_JSON, path, str(iters)], stdout=subprocess.PIPE).stdout.split()
        py_parse, py_ser = (size * iters / float(py[0]) / 1e6, size * iters / float(py[1]) / 1e6) if len(py) == 2 else (0, 0)
        extra = ""
        if node:
            script = os.path.join(ctx.work, "json_node.js")
            open(script, "w").write(NODE_JSON)
            nd = subprocess.run(node.split() + [script, path, str(iters)], stdout=subprocess.PIPE).stdout.split()
            if len(nd) == 2:
                extra = "  node %.0f / %.0f" % (size * iters / float(nd[0]) / 1e6, size * iters / float(nd[1]) / 1e6)
        for mode in opts["modes"]:
            row = {}
            for op in ("parse", "ser"):
                cmd0, env = mode_cmd(ctx, mode, obe, [op, path, 0])
                cmd1, _ = mode_cmd(ctx, mode, obe, [op, path, iters])
                base = measure(ctx, cmd0, env)
                full = measure(ctx, cmd1, env)
                cpu = max(full["cpu"] - base["cpu"], 1e-9)
                row[op] = size * iters / cpu / 1e6
                row[op + "_rss"] = full["rss_kb"] / 1024.0
                row[op + "_check"] = full["out"]
                if full.get("ir") and base.get("ir"):
                    row[op + "_ir_per_byte"] = (full["ir"] - base["ir"]) / float(size * iters)
            ir = ""
            if "parse_ir_per_byte" in row:
                ir = "  instr/byte %.0f / %.0f" % (row["parse_ir_per_byte"], row["ser_ir_per_byte"])
            print("  %-13s %9d %-8s %9.1f %9.1f %9.1f %9.1f %9.1f%s%s" % (name, size, mode, row["parse"], row["ser"],
                                                                        max(row["parse_rss"], row["ser_rss"]), py_parse, py_ser, extra, ir))
            results["json/%s/%s" % (name, mode)] = row


# ---------------------------------------------------------------- coll
COLL_TESTS = ["str_append_char", "str_append_str", "str_append_long", "str_plus_eq", "str_concat_new", "str_literal", "str_interp",
              "int_to_string", "str_to_int", "str_substring", "str_get", "str_equals", "str_compare", "str_find_char", "str_find_str",
              "str_find_str_common", "str_upper", "str_hash", "str_replace_all", "str_replace_char", "str_split_str", "str_split_char",
              "str_count", "vec_add", "vec_add_get", "vec_sort_int", "vec_sort_str", "arr_sort_int", "hash_int", "hash_str",
              "map_int", "map_str", "map_small", "map_keys", "queue", "stack", "list"]
# quadratic by definition (a new string per step): measured to 1e5 only, as a reference
COLL_CAP = {"str_concat_new": 100000}


def do_coll(ctx, opts, results):
    obe = compile_obs(ctx, "coll_bench", "gen_collect")
    total = int(float(opts.get("coll_total", 2e6)))
    sizes = [int(float(x)) for x in opts.get("coll_sizes", "1e3,1e4,1e5,1e6").split(",")]
    print("== strings and collections: ns of CPU per element at n = 1e3 .. 1e6 (best of %d); ratio = largest / smallest" % ctx.runs)
    print("  %-20s %-7s %s %7s" % ("test", "mode", " ".join("%9.0e" % n for n in sizes), "ratio"))
    for test in COLL_TESTS:
        if opts.get("only") and opts["only"] not in test:
            continue
        for mode in opts["modes"]:
            cells, values = [], []
            for n in sizes:
                if n > COLL_CAP.get(test, n):
                    cells.append("%9s" % "-")
                    continue
                reps = max(1, total // n)
                cmd0, env = mode_cmd(ctx, mode, obe, [test, n, 0])
                cmd1, _ = mode_cmd(ctx, mode, obe, [test, n, reps])
                try:
                    base = measure(ctx, cmd0, env)
                    full = measure(ctx, cmd1, env)
                except RuntimeError as e:
                    cells.append("%9s" % "FAIL")
                    continue
                if full.get("ir") and base.get("ir"):
                    v = (full["ir"] - base["ir"]) / float(n * reps)   # instructions per element
                else:
                    v = max(full["cpu"] - base["cpu"], 0) / (n * reps) * 1e9
                values.append(v)
                cells.append("%9.1f" % v)
                results["coll/%s/%s/%d" % (test, mode, n)] = {"per_elem": v, "check": full["out"], "rss_kb": full["rss_kb"]}
            ratio = (values[-1] / values[0]) if len(values) > 1 and values[0] > 0 else 0
            flag = "  <-- grows with n" if ratio > 4 else ""
            print("  %-20s %-7s %s %7.1f%s" % (test, mode, " ".join(cells), ratio, flag))
            sys.stdout.flush()


# ---------------------------------------------------------------- compile
def do_compile(ctx, opts, results):
    import gen_compile
    src_dir = os.path.join(ctx.work, "compile_src")
    cases = gen_compile.generate(src_dir)
    print("== compile time, obc (best of %d)" % ctx.runs)
    print("  %-28s %9s %9s %9s %9s  %s" % ("case", "src KB", "wall ms", "cpu ms", "RSS MB", "result"))
    for name, path, libs, expect_ok in cases:
        if opts.get("only") and opts["only"] not in name:
            continue
        dest = os.path.join(ctx.work, "compile_out.obe")
        cmd = [ctx.obc, "-src", path, "-dest", dest] + (["-lib", libs] if libs else [])
        # one probing run with a time limit: compiled, refused, or no answer
        if os.path.exists(dest):
            os.remove(dest)
        limit = float(opts.get("compile_timeout", 60))
        p = subprocess.Popen(cmd, env=ctx.env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            p.communicate(timeout=limit)
        except subprocess.TimeoutExpired:
            p.kill()
            p.communicate()
            print("  %-28s %9.1f %9s %9s %9s  %s" % (name, os.path.getsize(path) / 1024.0, "-", "-", "-", "NO ANSWER in %d s" % limit))
            results["compile/" + name] = {"result": "timeout"}
            sys.stdout.flush()
            continue
        if not 0 <= p.returncode <= 9:
            ok = "CRASH (status %d)" % p.returncode
        else:
            ok = "ok" if os.path.exists(dest) else "refused"
        r = measure_compile(ctx, cmd, dest)
        note = "" if expect_ok is None or (ok == "ok") == expect_ok else "  (expected %s)" % ("ok" if expect_ok else "refused")
        print("  %-28s %9.1f %9.1f %9.1f %9.1f  %s%s%s" % (name, os.path.getsize(path) / 1024.0, r["wall"] * 1e3, r["cpu"] * 1e3,
                                                         r["rss_kb"] / 1024.0, ok, note, fmt_ir(r)))
        results["compile/" + name] = {"wall": r["wall"], "cpu": r["cpu"], "rss_kb": r["rss_kb"], "result": ok, "ir": r.get("ir")}
        sys.stdout.flush()

    corpus = opts.get("corpus")
    if corpus:
        compile_corpus(ctx, corpus, results)


def measure_compile(ctx, cmd, dest):
    best = None
    for _ in range(ctx.runs):
        start = time.perf_counter()
        p = subprocess.Popen(cmd, env=ctx.env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        _, status, ru = os.wait4(p.pid, 0)
        wall = time.perf_counter() - start
        cpu = ru.ru_utime + ru.ru_stime
        if best is None:
            best = {"wall": wall, "cpu": cpu, "rss_kb": ru.ru_maxrss}
        else:
            best["wall"] = min(best["wall"], wall)
            best["cpu"] = min(best["cpu"], cpu)
    best["rss_kb"] = true_rss(ctx, cmd, ctx.env, None, best["rss_kb"])
    if ctx.ir:
        best["ir"] = count_ir(ctx, cmd, ctx.env)
    return best


def corpus_libs(path):
    """The library list a regression program asks for (# EXTRA_LIBS:), on top of the suite's default."""
    libs = "gen_collect,xml,json,cipher"
    try:
        head = open(path, encoding="utf-8", errors="replace").read(2000)
    except OSError:
        return libs
    m = re.search(r"#\s*EXTRA_LIBS:\s*([\w,]+)", head)
    if m:
        libs += "," + m.group(1)
    return libs


def compile_corpus(ctx, corpus, results):
    files = sorted(f for f in os.listdir(corpus) if f.endswith(".obs"))
    dest = os.path.join(ctx.work, "corpus_out.obe")
    total_cpu = total_wall = 0.0
    slow = []
    compiled = 0
    for f in files:
        path = os.path.join(corpus, f)
        cmd = [ctx.obc, "-src", path, "-dest", dest, "-lib", corpus_libs(path)]
        best_cpu = best_wall = None
        for _ in range(max(1, min(ctx.runs, 3))):
            if os.path.exists(dest):
                os.remove(dest)
            start = time.perf_counter()
            p = subprocess.Popen(cmd, env=ctx.env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            _, status, ru = os.wait4(p.pid, 0)
            wall = time.perf_counter() - start
            cpu = ru.ru_utime + ru.ru_stime
            best_cpu = cpu if best_cpu is None else min(best_cpu, cpu)
            best_wall = wall if best_wall is None else min(best_wall, wall)
        compiled += os.path.exists(dest)
        total_cpu += best_cpu
        total_wall += best_wall
        slow.append((best_cpu, f))
        results["corpus/" + f] = {"cpu": best_cpu, "wall": best_wall}
    slow.sort(reverse=True)
    print("  corpus %s: %d files (%d compiled), total cpu %.2f s, wall %.2f s" % (corpus, len(files), compiled, total_cpu, total_wall))
    for cpu, f in slow[:8]:
        print("    %7.1f ms  %s" % (cpu * 1e3, f))
    results["corpus_total"] = {"cpu": total_cpu, "wall": total_wall, "files": len(files), "compiled": compiled}


def main():
    args = sys.argv[1:]
    if len(args) < 3:
        print(__doc__)
        return 2
    opts = {"modes": ["default"]}
    runs, ir, json_out = 7, False, None
    pos = []
    i = 0
    while i < len(args):
        a = args[i]
        if a == "--runs":
            runs = int(args[i + 1]); i += 1
        elif a == "--ir":
            ir = True
        elif a == "--modes":
            opts["modes"] = args[i + 1].split(","); i += 1
        elif a == "--only":
            opts["only"] = args[i + 1]; i += 1
        elif a == "--json-out":
            json_out = args[i + 1]; i += 1
        elif a == "--corpus":
            opts["corpus"] = os.path.abspath(args[i + 1]); i += 1
        elif a.startswith("--") and i + 1 < len(args):
            opts[a[2:].replace("-", "_")] = args[i + 1]; i += 1
        else:
            pos.append(a)
        i += 1
    ctx = Ctx(pos[0], pos[1], runs, ir)
    sys.path.insert(0, HERE)
    results = {}
    what = pos[2]
    if what in ("startup", "all"):
        do_startup(ctx, opts, results)
    if what in ("json", "all"):
        do_json(ctx, opts, results)
    if what in ("coll", "all"):
        do_coll(ctx, opts, results)
    if what in ("compile", "all"):
        do_compile(ctx, opts, results)
    if json_out:
        with open(json_out, "w") as f:
            json.dump(results, f, indent=1, sort_keys=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
