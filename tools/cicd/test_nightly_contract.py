#!/usr/bin/env python3
"""The contract between .github/workflows/nightly-hardening.yml and the
scripts it calls (standard library only; PyYAML is used when installed and a
line scanner otherwise, and the two must agree).

Every invocation in a `run:` block is checked against the repository:

  * a script it names exists;
  * every --flag it passes is one the script's --help lists (for
    nightly_triage.py, the subcommand's), including the command after `--`;
  * a --cwd-relative runner (run_regression.sh / .cmd) exists;
  * --require PATH exists unless it is inside the downloaded deploy tree, and
    --require-text PATH:TEXT finds TEXT in PATH -- or, for the tree's obr, in
    the VM sources it is built from;
  * an env var set on a step and not read by the run block itself is read by
    something the step runs: the called scripts, or the VM sources when the
    step runs obr (both regression runners are checked separately, since a
    Windows leg runs one and a POSIX leg the other);
  * steps.<id>.outputs.<key> is written by that step (its shell or the
    script it runs with --github-output);
  * a local action (uses: ./.github/actions/X) declares every `with:` input.

Run:  python3 -m unittest tools/cicd/test_nightly_contract.py
"""
import functools
import glob
import os
import re
import shlex
import subprocess
import sys
import unittest

TOOLS_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(TOOLS_DIR))
WORKFLOW = os.path.join(REPO_ROOT, ".github", "workflows", "nightly-hardening.yml")

TREE_MARK = "@TREE@"
RUNNERS = ("run_regression.sh", "run_regression.cmd")
# scripts whose runs execute obr, so an OBJECK_* variable may be read by the VM
RUNS_VM = ("run_regression.sh", "run_regression.cmd", "run_fuzz.py", "run_differential.py",
           "nightly_triage.py")


# ---------------------------------------------------------------------------
# Workflow model: [{"job", "name", "id", "env", "run", "uses", "with"}]
# ---------------------------------------------------------------------------

def _scalar(value):
    value = value.strip()
    if len(value) >= 2 and value[0] == value[-1] and value[0] in "'\"":
        return value[1:-1]
    return value


def scan_workflow(text):
    """A line scanner for the subset of YAML the workflow uses: jobs at two
    spaces, job keys at four, steps as '      - ' items with keys at eight,
    env/with maps one level deeper, and `run: |` blocks."""
    lines = text.splitlines()
    steps = []
    job = None
    job_env = {}
    step = None
    block = None          # (dict to fill, indent of its keys) for env/with
    run_indent = None     # indent of the key that opened a `|` block scalar
    scalar = None         # (dict, key) the open block scalar fills
    in_jobs = False
    section = None        # "env" or "steps" inside a job
    for raw in lines:
        if run_indent is not None:
            if raw.strip() == "" or (len(raw) - len(raw.lstrip())) > run_indent:
                scalar[0][scalar[1]] += raw[run_indent + 2:] + "\n" if raw.strip() else "\n"
                continue
            run_indent = None
            target, key = scalar
            target[key] = target[key].rstrip("\n") + "\n"
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        indent = len(raw) - len(raw.lstrip())
        body = raw.strip()
        if indent == 0:
            in_jobs = body == "jobs:"
            job = None
            continue
        if not in_jobs:
            continue
        if indent == 2 and body.endswith(":"):
            job, job_env, section, step, block = body[:-1], {}, None, None, None
            continue
        if job is None:
            continue
        if indent == 4:
            block = None
            step = None
            section = body[:-1] if body.endswith(":") else None
            if section == "env":
                block = (job_env, 6)
            continue
        if section == "env" and block and indent == block[1] and ":" in body:
            k, v = body.split(":", 1)
            job_env[k.strip()] = _scalar(v)
            continue
        if section != "steps":
            continue
        if indent == 6 and body.startswith("- "):
            step = {"job": job, "name": "", "id": "", "env": dict(job_env), "run": "",
                    "uses": "", "with": {}}
            steps.append(step)
            body = body[2:]
            indent = 8
        if step is None:
            continue
        if indent == 8 and ":" in body:
            block = None
            k, v = body.split(":", 1)
            k, v = k.strip(), v.strip()
            if k in ("env", "with") and v == "":
                block = (step[k], 10)
            elif k == "run":
                if v in ("|", "|-", ">", ">-"):
                    run_indent, scalar = 8, (step, "run")
                else:
                    step["run"] = _scalar(v) + "\n"
            elif k in ("name", "id", "uses"):
                step[k] = _scalar(v)
            continue
        if block and indent == block[1] and ":" in body:
            k, v = body.split(":", 1)
            k, v = k.strip(), v.strip()
            if v in ("|", "|-", ">", ">-"):
                block[0][k] = ""
                run_indent, scalar = block[1], (block[0], k)
            else:
                block[0][k] = _scalar(v)
    return steps


def _text(v):
    # the scanner keeps the workflow's spelling; YAML turns true into True
    return ("true" if v else "false") if isinstance(v, bool) else str(v)


def yaml_workflow(text):
    import yaml
    doc = yaml.safe_load(text)
    steps = []
    for job, spec in (doc.get("jobs") or {}).items():
        job_env = {k: _text(v) for k, v in (spec.get("env") or {}).items()}
        for s in spec.get("steps") or []:
            env = dict(job_env)
            env.update({k: _text(v) for k, v in (s.get("env") or {}).items()})
            run = s.get("run") or ""
            if run and not run.endswith("\n"):
                run += "\n"
            steps.append({"job": job, "name": s.get("name", ""), "id": s.get("id", ""),
                          "env": env, "run": run, "uses": s.get("uses", ""),
                          "with": {k: _text(v) for k, v in (s.get("with") or {}).items()}})
    return steps


def load_workflow(text):
    try:
        return yaml_workflow(text)
    except ImportError:
        return scan_workflow(text)


# ---------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------

EXPR_RE = re.compile(r"\$\{\{\s*([^}]*?)\s*\}\}")
VAR_RE = re.compile(r"\$\{?([A-Za-z_][A-Za-z0-9_]*)\}?")


def expand(text, env, runner):
    """Substitute workflow expressions and env vars as a leg would see them."""
    def expr(m):
        e = m.group(1)
        if e == "steps.prep.outputs.runner":
            return runner
        if e == "steps.prep.outputs.exe":
            return ""
        if e == "matrix.arch":
            return "x64"
        if e == "matrix.platform":
            return "linux-x64"
        return "EXPR"
    text = EXPR_RE.sub(expr, text)

    def var(m):
        name = m.group(1)
        if name == "TREE":
            return TREE_MARK
        if name == "PY":
            return "python"
        if name in env:
            return EXPR_RE.sub(expr, env[name]).replace("$", "")
        return "$" + name
    return VAR_RE.sub(var, text)


def commands(run, env, runner):
    """argv lists for every command line of a run block."""
    joined = re.sub(r"\\\n\s*", " ", run)
    out = []
    for line in joined.split("\n"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        try:
            argv = shlex.split(expand(line, env, runner), comments=True)
        except ValueError:
            continue
        if argv:
            out.append(argv)
    return out


def referenced_vars(run):
    return set(VAR_RE.findall(EXPR_RE.sub("", run)))


@functools.lru_cache(maxsize=None)
def help_options(script, sub=None):
    cmd = [sys.executable, script] + ([sub] if sub else []) + ["--help"]
    p = subprocess.run(cmd, cwd=REPO_ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
    text = p.stdout.decode("utf-8", "replace")
    if p.returncode != 0:
        return None, text
    return set(re.findall(r"(?<![\w-])(--?[A-Za-z][\w-]*)", text)), text


@functools.lru_cache(maxsize=None)
def read(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return f.read()


@functools.lru_cache(maxsize=None)
def vm_sources():
    parts = []
    for ext in ("cpp", "h"):
        for path in glob.glob(os.path.join(REPO_ROOT, "core", "vm", "**", "*." + ext), recursive=True):
            parts.append(read(path))
    return "\n".join(parts)


def repo_path(path, cwd=None):
    return os.path.normpath(os.path.join(REPO_ROOT, cwd or "", path))


class Invocation(object):
    def __init__(self, script, args):
        self.script = script      # repository-relative path
        self.args = args


def check_command(argv, where, problems, called, cwd=None):
    """Check one argv; record problems and the scripts it calls in `called`."""
    if not argv:
        return
    first = argv[0]
    if os.path.basename(first) in ("python", "python3"):
        rest = argv[1:]
        if rest[:2] == ["-m", "unittest"]:
            targets = rest[2:]
            if targets[:1] == ["discover"]:
                opts = dict(zip(targets[1::2], targets[2::2]))
                start = opts.get("-s", ".")
                pattern = opts.get("-p", "test*.py")
                if not glob.glob(os.path.join(repo_path(start), pattern)):
                    problems.append("%s: unittest discover finds no %s in %s" % (where, pattern, start))
                return
            for arg in targets:
                if arg.endswith(".py") and not os.path.isfile(repo_path(arg)):
                    problems.append("%s: unittest target %s does not exist" % (where, arg))
            return
        if not rest or not rest[0].endswith(".py"):
            return
        check_script(rest[0], rest[1:], where, problems, called, cwd)
        return
    if os.path.basename(first) in RUNNERS:
        path = repo_path(first, cwd)
        if not os.path.isfile(path):
            problems.append("%s: runner %s not found (cwd %s)" % (where, first, cwd or "."))
        else:
            called.append(path)


def check_script(script, args, where, problems, called, cwd=None):
    path = repo_path(script, cwd)
    if TREE_MARK in script:
        return
    if not os.path.isfile(path):
        problems.append("%s: script %s does not exist" % (where, script))
        return
    called.append(path)
    pre, post = args, []
    if "--" in args:
        i = args.index("--")
        pre, post = args[:i], args[i + 1:]
    sub = None
    if os.path.basename(script) == "nightly_triage.py" and pre and not pre[0].startswith("-"):
        sub = pre[0]
    options, text = help_options(path, sub)
    if options is None:
        problems.append("%s: %s %s--help failed: %s" % (where, script, (sub + " ") if sub else "",
                                                        text.strip()[-200:]))
        return
    step_cwd = None
    i = 0
    while i < len(pre):
        tok = pre[i]
        if tok.startswith("-") and len(tok) > 1 and not re.match(r"^-\d", tok):
            flag = tok.split("=", 1)[0]
            if flag not in options:
                problems.append("%s: %s%s has no option %s" % (where, script, (" " + sub) if sub else "", flag))
            value = pre[i + 1] if i + 1 < len(pre) and "=" not in tok else None
            if flag == "--cwd" and value:
                step_cwd = value
            if flag == "--require" and value and TREE_MARK not in value:
                if not os.path.exists(repo_path(value)):
                    problems.append("%s: --require %s does not exist" % (where, value))
            if flag == "--require-text" and value:
                check_require_text(value, where, problems)
        i += 1
    if post:
        check_command(post, where, problems, called, step_cwd)


def check_require_text(spec, where, problems):
    if ":" not in spec:
        problems.append("%s: --require-text %s is not PATH:TEXT" % (where, spec))
        return
    path, text = spec.rsplit(":", 1)
    if TREE_MARK in path:
        if os.path.basename(path).startswith("obr") and text not in vm_sources():
            problems.append("%s: --require-text %s: no VM source mentions %s" % (where, spec, text))
        return
    full = repo_path(path)
    if not os.path.isfile(full):
        problems.append("%s: --require-text file %s does not exist" % (where, path))
    elif text not in read(full):
        problems.append("%s: --require-text %s: the file does not contain %s" % (where, path, text))


def action_inputs(uses):
    rel = uses[2:] if uses.startswith("./") else uses
    path = os.path.join(REPO_ROOT, rel.replace("/", os.sep), "action.yml")
    if not os.path.isfile(path):
        return None
    inputs, inside = set(), False
    for line in read(path).splitlines():
        if re.match(r"^inputs:\s*$", line):
            inside = True
            continue
        if inside:
            if re.match(r"^\S", line):
                break
            m = re.match(r"^  ([\w-]+):\s*$", line)
            if m:
                inputs.add(m.group(1))
    return inputs


def check_workflow(text, runners=RUNNERS):
    problems = []
    steps = load_workflow(text)
    by_id = {}
    for s in steps:
        if s["id"]:
            by_id[(s["job"], s["id"])] = s

    for s in steps:
        where = "%s / %s" % (s["job"], s["name"] or s["uses"] or "(step)")
        if s["uses"].startswith("./"):
            inputs = action_inputs(s["uses"])
            if inputs is None:
                problems.append("%s: local action %s has no action.yml" % (where, s["uses"]))
            else:
                for key in s["with"]:
                    if key not in inputs:
                        problems.append("%s: action %s has no input %s" % (where, s["uses"], key))
        if not s["run"]:
            continue
        mentions_runner = "steps.prep.outputs.runner" in s["run"]
        for runner in (runners if mentions_runner else runners[:1]):
            called = []
            leg_where = where + (" [%s]" % runner if mentions_runner else "")
            for argv in commands(s["run"], s["env"], runner):
                check_command(argv, leg_where, problems, called)
            if not called:
                continue
            step_only = set(s["env"]) - set(job_env_of(steps, s))
            unread = step_only - referenced_vars(s["run"])
            for name in sorted(unread):
                readers = [p for p in called if name in read(p)]
                runs_vm = any(os.path.basename(p) in RUNS_VM for p in called)
                if not readers and not (runs_vm and name.startswith("OBJECK_") and name in vm_sources()):
                    problems.append("%s: env %s is set but nothing the step runs reads it (%s)"
                                    % (leg_where, name, ", ".join(os.path.relpath(p, REPO_ROOT)
                                                                  for p in called)))

    for m in re.finditer(r"steps\.([\w-]+)\.outputs\.([\w-]+)", text):
        sid, key = m.group(1), m.group(2)
        owners = [st for (job, i), st in by_id.items() if i == sid]
        if not owners:
            problems.append("steps.%s.outputs.%s: no step has id %s" % (sid, key, sid))
            continue
        if not any(writes_output(st, key) for st in owners):
            problems.append("steps.%s.outputs.%s: step %s never writes %s" % (sid, key, sid, key))
    return sorted(set(problems))


def job_env_of(steps, step):
    # the env every step of the job shares (job-level env); a step's own keys differ
    same = [s["env"] for s in steps if s["job"] == step["job"]]
    common = dict(same[0]) if same else {}
    for env in same[1:]:
        common = {k: v for k, v in common.items() if env.get(k) == v}
    return common


def writes_output(step, key):
    if re.search(r"(?:^|[\s\"'])%s=" % re.escape(key), step["run"]):
        return True
    if "--github-output" not in step["run"]:
        return False
    for runner in RUNNERS[:1]:
        for argv in commands(step["run"], step["env"], runner):
            for tok in argv:
                if tok.endswith(".py") and os.path.isfile(repo_path(tok)):
                    if re.search(r"[\"']%s=" % re.escape(key), read(repo_path(tok))):
                        return True
    return False


# ---------------------------------------------------------------------------

def workflow_text():
    with open(WORKFLOW, "r", encoding="utf-8") as f:
        return f.read()


HEAD = """name: T
on: workflow_dispatch
jobs:
  harden:
    runs-on: ubuntu-latest
    env:
      PY: python3
      TREE: core/release/deploy-${{ matrix.arch }}
      TRIAGE: tools/cicd/nightly_triage.py
    steps:
      - name: Prepare
        id: prep
        run: |
          echo "exe=" >> "$GITHUB_OUTPUT"
          echo "runner=run_regression.sh" >> "$GITHUB_OUTPUT"
"""


class NightlyContractTests(unittest.TestCase):
    def test_nightly_workflow_honours_its_scripts(self):
        self.assertEqual(check_workflow(workflow_text()), [])

    def test_scanner_reads_the_workflow_steps(self):
        steps = scan_workflow(workflow_text())
        names = [s["name"] for s in steps]
        for name in ("Fuzzer", "Regression under the heap verifier (minor stress)", "Classify failures"):
            self.assertIn(name, names)
        fuzz = [s for s in steps if s["name"] == "Fuzzer"][0]
        self.assertIn("run_fuzz.py", fuzz["run"])
        self.assertIn("OBJECK_FUZZ_OUT", fuzz["env"])
        self.assertEqual(fuzz["env"]["TRIAGE"], "tools/cicd/nightly_triage.py")

    def test_scanner_agrees_with_yaml(self):
        try:
            import yaml  # noqa: F401
        except ImportError:
            self.skipTest("PyYAML not installed; the scanner is what runs")
        text = workflow_text()
        key = lambda s: (s["job"], s["name"], s["uses"])
        a = sorted(scan_workflow(text), key=key)
        b = sorted(yaml_workflow(text), key=key)
        self.assertEqual([key(s) for s in a], [key(s) for s in b])
        for sa, sb in zip(a, b):
            self.assertEqual(sa["run"].strip(), sb["run"].strip(), sa["name"])
            self.assertEqual(sa["env"], sb["env"], sa["name"])
            self.assertEqual(sa["with"], sb["with"], sa["name"])
            self.assertEqual(sa["id"], sb["id"], sa["name"])

    def test_scanner_fallback_finds_the_same_problems(self):
        text = HEAD + """      - name: Fuzzer
        env:
          OBJECK_FUZZ_TYPO: x
        run: |
          "$PY" "$TRIAGE" run --leg a --step fuzz --out r --bogus \\
            -- "$PY" tools/fuzz/run_fuzz.py --bin "$TREE/bin" --no-such-flag
"""
        with_yaml = check_workflow(text)
        import builtins
        real_import = builtins.__import__

        def no_yaml(name, *a, **k):
            if name == "yaml":
                raise ImportError("hidden")
            return real_import(name, *a, **k)
        builtins.__import__ = no_yaml
        try:
            without = check_workflow(text)
        finally:
            builtins.__import__ = real_import
        self.assertEqual(with_yaml, without)
        self.assertTrue(without)

    # -- proof each check can fail ---------------------------------------------
    def problems(self, steps_yaml):
        return "\n".join(check_workflow(HEAD + steps_yaml))

    def test_unknown_flags_fail(self):
        p = self.problems("""      - name: Fuzzer
        run: |
          "$PY" "$TRIAGE" run --leg a --step fuzz --out r --bogus \\
            -- "$PY" tools/fuzz/run_fuzz.py --bin "$TREE/bin" --no-such-flag
""")
        self.assertIn("nightly_triage.py run has no option --bogus", p)
        self.assertIn("run_fuzz.py has no option --no-such-flag", p)

    def test_subcommand_flags_are_per_subcommand(self):
        # --tests exists on `loop`, not on `run`
        p = self.problems("""      - name: Loop
        run: |
          "$PY" "$TRIAGE" run --leg a --step s --out r --tests x -- echo
""")
        self.assertIn("nightly_triage.py run has no option --tests", p)

    def test_missing_scripts_and_requires_fail(self):
        p = self.problems("""      - name: Gone
        run: |
          "$PY" "$TRIAGE" run --leg a --step s --out r --require tools/nope.py \\
            --require-text "programs/regression/run_regression.sh:NO_SUCH_KNOB" \\
            --require-text "$TREE/bin/obr${{ steps.prep.outputs.exe }}:OBJECK_NO_SUCH_VM_KNOB" \\
            -- "$PY" tools/fuzz/no_such_script.py
      - name: Unit
        run: python3 -m unittest tools/cicd/test_no_such_file.py
""")
        self.assertIn("--require tools/nope.py does not exist", p)
        self.assertIn("does not contain NO_SUCH_KNOB", p)
        self.assertIn("no VM source mentions OBJECK_NO_SUCH_VM_KNOB", p)
        self.assertIn("script tools/fuzz/no_such_script.py does not exist", p)
        self.assertIn("unittest target tools/cicd/test_no_such_file.py does not exist", p)

    def test_env_nothing_reads_fails(self):
        p = self.problems("""      - name: Regression
        env:
          OBJECK_VM_ARGS: --gc-threshold=64k
          OBJECK_NOT_A_KNOB: '1'
          NOT_READ_ANYWHERE: '1'
        run: |
          "$PY" "$TRIAGE" run --leg a --step s --out r --cwd programs/regression \\
            -- "${{ steps.prep.outputs.runner }}" x64
""")
        self.assertIn("env NOT_READ_ANYWHERE is set but nothing the step runs reads it", p)
        self.assertIn("env OBJECK_NOT_A_KNOB is set", p)
        self.assertNotIn("OBJECK_VM_ARGS", p)
        # checked once per runner: a Windows leg runs the .cmd
        self.assertIn("[run_regression.cmd]", p)

    def test_env_read_by_one_runner_only_fails(self):
        runners = ("run_regression.sh", "no_timeout_runner.cmd")
        text = HEAD + """      - name: Regression
        env:
          TEST_TIMEOUT: '300'
        run: |
          "$PY" "$TRIAGE" run --leg a --step s --out r --cwd programs/regression \\
            -- "${{ steps.prep.outputs.runner }}" x64
"""
        p = "\n".join(check_workflow(text, runners))
        self.assertIn("[no_timeout_runner.cmd]", p)
        self.assertNotIn("[run_regression.sh]: env TEST_TIMEOUT", p)

    def test_outputs_and_action_inputs_fail(self):
        p = self.problems("""      - name: Classify
        id: triage
        run: |
          python3 tools/cicd/nightly_triage.py triage --results r --known k --legs a \\
            --steps s --out o --github-output "$GITHUB_OUTPUT"
      - name: Report
        if: steps.triage.outputs.no_such_key == 'x' && steps.prep.outputs.runner != '' && steps.gone.outputs.x
        uses: ./.github/actions/notify-failure
        with:
          workflow: T
          no-such-input: y
""")
        self.assertIn("steps.triage.outputs.no_such_key: step triage never writes no_such_key", p)
        self.assertIn("steps.gone.outputs.x: no step has id gone", p)
        self.assertNotIn("steps.prep.outputs.runner:", p)
        self.assertIn("action ./.github/actions/notify-failure has no input no-such-input", p)
        self.assertNotIn("has no input workflow", p)


if __name__ == "__main__":
    unittest.main()
