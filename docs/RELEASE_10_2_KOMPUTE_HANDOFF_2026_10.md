# Kompute handoff: the v2026.10.2 release and what follows (2026-10-31)

**Audience:** the next working session on Kompute, the x64 Windows home server (Ryzen 9
7950X3D, 128 GB, RTX 4090). **Why:** the laptop crashed twice during this release, and the user
said "use kompute it is more stable". The coordinating session on the laptop was running the
10.2 release; this document hands it, and the work right behind it, to Kompute.

Run `hostname` first, then `git fetch origin` and compare to `origin/master` before you
investigate anything: the local tree is often dozens of commits behind.

## 1. Where the release stands

The release candidate is master at **`3c196845ff`** (the merge of
[#1478](https://github.com/objeck/objeck-lang/pull/1478)). After it, master has only the
bot's TESTS.md regeneration (`77fc18bacb`), which changes no code.

| Gate | Commit | Result |
|---|---|---|
| CI Build (all five legs) | `3c196845ff`, run 41203794756 | success |
| Verify Platforms, version 2026.10.2 | `15480e94ed`, run 41185538474 | success |
| Nightly Hardening | `3c196845ff`, run 41207692964 | **running at handoff**: check it first |

Verify Platforms ran on the previous candidate. Between that commit and this one only #1478
changed code: a GC verifier/root-scan fix, a new test and a test output line. It doesn't
touch packaging, so Verify Platforms need not run again unless the user asks for it.

The previous Nightly Hardening run, on `15480e94ed` (run 41189026900), failed on all five
legs with three new issues. #1478 fixed them:

- [#1476](https://github.com/objeck/objeck-lang/issues/1476) and
  [#1475](https://github.com/objeck/objeck-lang/issues/1475): heap-verifier B3 violations (a
  typed reference into the nursery after a collection) in `query_index_differential` and
  `query_expr_differential`. Cause: GC roots phase 2
  ([#1470](https://github.com/objeck/objeck-lang/pull/1470)). When compiled code calls back
  into the interpreter, the nested interpreter's frames sit directly above the compiled
  method's caller, so the callee's reference map typed the compiled method's pending words.
  A live nursery String came out as an Int and was never relocated. Fix: `AttributeThread`
  stops typing words at the first frame compiled code sits under. A new
  `run_vm_flag_tests.py` section 27 (`gc_roots_jit_bridge.obs`) audits roots with the JIT on.
- [#1477](https://github.com/objeck/objeck-lang/issues/1477): `trap_args_leave_stack` printed
  a byte count that differs between `-opt s0` and `s3`. The count is no longer printed.

### Reading the new Nightly Hardening result

```bash
gh run view 41207692964 --json status,conclusion,jobs --jq '.jobs[]|"\(.name): \(.conclusion)"'
gh issue list --state open --limit 10
```

- **Clean**, or failing only on `query_url_cache_processes` / `query_net_read` on Windows
  (#1443/#1461, the documented known issue below): the release can go. Tell the user, then
  run `/release`.
- **Anything new**: the triage job files one issue per failure. The owner of GC and JIT
  failures is the session "JIT/GC: verify spill and inline-root tests, fix for 10.2"
  (`local_95e1ddf9-dce3-4efc-8185-2cefe1206ec2`, on the laptop); the owner of net and AI
  failures is "Design: Python-style BERT for Objeck"
  (`local_1c9f03db-2266-49b6-8e32-51a181e71d5c`). If those sessions can't be reached from
  Kompute, fix the failure here. Never add a failure to `tools/fuzz/known.json` to get a run
  green. After the fix merges, rerun the nightly against master's new CI Build:
  `gh workflow run nightly-hardening.yml --ref master -f ci_run_id=<ci-build run id>`.

### Cutting the release

- `/release` is **cloud-only**. It never bumps the version or builds locally. `version.h`
  already reads `2026.10.2`.
- The Windows MSI is signed **locally by the user** with the eToken, and SHA256SUMS is
  regenerated afterwards. This is the one step the user does by hand.
- The README's four download markers stay on the published version until the tag exists.
- The playground deploy follows the release and is the user's call.

## 2. What 10.2 contains, and what it does not

**In:**
- the cross-origin redirect credential fix (#1454);
- GC roots phases 0-2 (#1455, #1466, #1470, plus #1478), fixing #1452 for interpreted code,
  with the kill switch `OBJECK_GC_ROOTS=conservative`;
- the inlined-callee locals fix (#1467, closes #1464);
- AMD64 JIT entry zeroing of all spill slots (#1468);
- stable instance IDs (#1472);
- the HTTP reset-mid-reply fix (#1469) and the TLS client receive timeout of 300 s (#1471);
- the trap stack-leak fix (#1458) and library interface names (#1465);
- AI foundation pieces: TextClassifier (#1456), streaming downloads (#1459), the Hugging Face
  hub client (#1463) and the BPE tokenizer (#1462).

The release notes are in CHANGELOG.md (#1474).

**Out, for 10.3:**
- GC roots phases 3-6: compiled frames and removing `JIT_TMP_LOOK_BACK` (design
  `docs/GC_ROOTS_DESIGN.md`);
- #1473, a test-only way to make an Int equal to an object's address (phase 2's negative
  control lost its trigger when IDs became counters);
- the rest of AI foundation Phase A: the Unigram tokenizer (branch
  `feat/unigram-tokenizer`, not merged), tokenizer.json and pipelines (design
  `docs/AI_FOUNDATION_DESIGN.md`, #1451);
- [#1435](https://github.com/objeck/objeck-lang/pull/1435), LSP edit timing.

## 3. The known issue: Windows loopback connect stalls (#1443, #1461)

CHANGELOG.md documents it. Nothing was masked with retries or longer timeouts, and nothing
should be.

What is established (full findings on
[#1443](https://github.com/objeck/objeck-lang/issues/1443)):

- A laptop soak of `run_differential.py query_url_cache_processes query_net_read` diverged in
  6 of 60 iterations. Every failure was 1-3 queries reporting `no response from the server
  (after 21.0 s)`.
- Mechanism, from a netstat watcher:
  1. A child process's end of a connection disappears with no FIN_WAIT or TIME_WAIT.
  2. The server's end of that port pair stays ESTABLISHED.
  3. Loopback keeps no TIME_WAIT, so a later child gets the same client port at once.
  4. The SYN is dropped against the stale entry until Windows gives up at 21 s.
- Ruled out: port exhaustion, cache file locking, a client path that leaves a socket open, and
  Windows failing to notify on exit (a Python client that closes, shuts down or exits with
  the socket open is seen by the server at once).
- Still open: why the client's end vanishes without the server's stack hearing of it, and why
  CI fails far more often (97-447 of 1,440 queries in `s3/jit1`). The nightly artifact keeps
  only summary JSON, so CI's failure kinds are not recorded.

**Next step, well suited to Kompute** (admin rights; it doesn't have the laptop's crash
history): capture packets with `pktmon` during a soak, then match the stalled port pair's
last packets on both ends. A useful side change is for the nightly to upload per-config
output for these two tests. If a fix comes out of this, it is its own PR. Whether it goes
into 10.2 is the user's call, and it doesn't block the release.

## 4. Kompute notes

- Visual Studio 2026 (v18) and vcpkg at `C:\vcpkg`. It never sleeps on AC power.
- Pin multi-threaded GC stress to 4 cores (`start /affinity F`); extra cores hide those bugs.
- Use this machine for full-suite, verifier, differential and stress runs, and for the CUDA
  and libtorch work in the AI foundation.
- `run_vm_flag_tests.py` and the regression suite read the live tree: never check out,
  rebase or edit tests while a pass runs.

## 5. Rules that bit this release

- `gh pr merge --auto` merges **immediately** here. Merge by hand when every leg is green.
- `gh run watch` exits 0 on network errors. Poll `gh run view --json status,conclusion`.
- Never commit `programs/regression/TESTS.md` in a PR; the bot regenerates it on master.
- Never put an `@` before a session name in GitHub text: "komp" is a real GitHub user.
- Don't build on deprecated libraries, even as a fallback: no ONNX Runtime training, no
  TorchScript.
- Fix the toolchain; never work around it in tests.
- Before any `ssh-keygen`, list `~/.ssh` and give the new key its own name. A default-path
  key generation on 2026-10-30 overwrote the laptop's playground key.

## 6. Open with the user, not Objeck

The user asked to make a CARLA simulation report the real city's lat/lon on the map, so that
simulated footage can be scored for accuracy. That work is in the `lvs` repository, not this
one. The user said "wrong repo" when it was started here; ask them where it goes before
doing anything.
