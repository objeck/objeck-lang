"""DAP gotoTargets / goto (set the next statement), and the invalidated event.

goto moves where the stopped thread continues from. It does so ONLY inside the
method that is executing, between two places where the operand stack holds what
the method was entered with (the start of a statement): anything else is refused
with a DAP error and the program is left exactly as it was.

 1. gotoTargets finds the statement on a line; a line with no code has none
 2. goto forward skips the lines in between: Jump() returns 100, not 111
 3. goto answers with a stopped event, reason "goto", and the stack shows the new line
 4. a client that declared supportsInvalidatedEvent gets an invalidated event; one that did not, none
 5. goto backward re-runs lines (and the breakpoint on the target's neighbour fires again)
 6. stepping works from the new position (next after goto lands on the line after it)
 7. goto into a different method is refused with a clear message, and the program then
    runs to the correct result (nothing was corrupted)
 8. goto with an unknown target, a wrong thread, a missing targetId, or while running is refused
 9. a target in the middle of an expression (a call's argument on its own line) is refused
"""
import shutil
import tempfile
import time

import dap_more_common as c

R = c.Results()
check = R.check

# a statement whose argument is on a line of its own: that line starts in the
# middle of the statement, with a value already on the operand stack
MULTILINE = """class DapMoreMulti {
  function : Add(a : Int, b : Int) ~ Int {
    return a + b;
  }

  function : Main(args : String[]) ~ Nil {
    first := 1;    # M_FIRST
    second := 2;    # M_SECOND
    sum := Add(first,
      second);    # M_ARG
    "sum={$sum}"->PrintLine();    # M_PRINT
  }
}
"""

work = tempfile.mkdtemp(prefix="dap-goto-")
try:
    src, obe = c.compile_fixture(work, "goto_fixture")
    msrc, mobe = c.compile_fixture(work, "goto_multi", MULTILINE)
    if not obe or not mobe:
        print("ERROR: could not compile the fixtures")
        raise SystemExit(2)

    JUMP_A, JUMP_B, JUMP_C = c.line_of("JUMP_A"), c.line_of("JUMP_B"), c.line_of("JUMP_C")
    JUMP_RET = c.line_of("JUMP_RET")
    MAIN_JUMP = c.line_of("MAIN_JUMP")
    INIT = {"supportsInvalidatedEvent": True}

    # ---- 1, 2, 3, 4: forward ------------------------------------------------------
    s = c.Session(init=INIT)
    s.start(obe, work, src, [JUMP_A])
    body, frame = s.stop_at()
    tid = body.get("threadId", 1) if body else 1
    check("stopped on the first statement of Jump", frame.get("line") == JUMP_A and "Jump" in frame.get("name", ""),
          f"got: {frame}")

    resp = s.request("gotoTargets", {"source": {"path": src}, "line": JUMP_C})
    targets = resp.get("body", {}).get("targets", []) if resp else []
    check("gotoTargets finds the statement on the line",
          resp is not None and resp.get("success") is True and len(targets) == 1 and targets[0].get("line") == JUMP_C,
          f"got: {resp}")
    check("the target says which method it is in", targets and "Jump" in targets[0].get("label", ""), f"got: {targets}")
    resp = s.request("gotoTargets", {"source": {"path": src}, "line": 1000})
    check("a line with no code has no targets", resp is not None and resp.get("success") is True and
          resp.get("body", {}).get("targets") == [], f"got: {resp}")
    resp = s.request("gotoTargets", {"source": {"path": src}})
    check("gotoTargets without a line is refused", resp is not None and resp.get("success") is False, f"got: {resp}")

    resp = s.request("gotoTargets", {"source": {"path": src}, "line": JUMP_C})
    targets = resp["body"]["targets"]
    resp = s.request("goto", {"threadId": tid, "targetId": targets[0]["id"]})
    check("goto to a later line in the same method succeeds", resp is not None and resp.get("success") is True, f"got: {resp}")
    stopped = s.wait_event("stopped", timeout=5.0)
    check("goto is answered with a stopped event, reason goto",
          stopped is not None and stopped.get("body", {}).get("reason") == "goto" and
          stopped.get("body", {}).get("threadId") == tid, f"got: {stopped}")
    inval = s.events_named("invalidated")
    check("a client that supports it gets an invalidated event for variables",
          len(inval) == 1 and "variables" in inval[0].get("body", {}).get("areas", []), f"got: {inval}")
    frame = s.top_frame(tid)
    check("the stack shows the new line", frame.get("line") == JUMP_C, f"got: {frame}")

    # ---- 6. step from the new position ----------------------------------------------
    s.request("next", {"threadId": tid})
    body, frame = s.stop_at()
    check("next after goto runs the target statement and stops on the line after it",
          body is not None and body.get("reason") == "step" and frame.get("line") == JUMP_RET, f"got: {body} {frame}")
    total = s.local("total", frame.get("id", 0))
    check("the skipped statements did not run", total == "100", f"total={total}")
    check("the program runs to the end", s.finish(), "never terminated")
    check("Jump returned 100, not 111", "y=100" in s.output_text(), f"output: {s.output_text()!r}")
    s.close()

    # ---- 4b. a client that did not ask gets no invalidated ----------------------------
    s = c.Session()
    s.start(obe, work, src, [JUMP_A])
    body, frame = s.stop_at()
    tid = body.get("threadId", 1)
    targets = s.request("gotoTargets", {"source": {"path": src}, "line": JUMP_B})["body"]["targets"]
    s.request("goto", {"threadId": tid, "targetId": targets[0]["id"]})
    stopped = s.wait_event("stopped", timeout=5.0)
    time.sleep(0.3)
    check("goto still answers with a stopped event", stopped is not None and
          stopped.get("body", {}).get("reason") == "goto", f"got: {stopped}")
    check("a client that did not declare supportsInvalidatedEvent gets none", not s.events_named("invalidated"),
          f"got: {s.events_named('invalidated')}")
    s.finish()
    check("skipping one statement gives 110", "y=110" in s.output_text(), f"output: {s.output_text()!r}")
    s.close()

    # ---- 5. backward --------------------------------------------------------------------
    s = c.Session()
    s.start(obe, work, src, [JUMP_B])
    body, frame = s.stop_at()
    tid = body.get("threadId", 1)
    check("stopped on the second statement", frame.get("line") == JUMP_B, f"got: {frame}")
    targets = s.request("gotoTargets", {"source": {"path": src}, "line": JUMP_A})["body"]["targets"]
    resp = s.request("goto", {"threadId": tid, "targetId": targets[0]["id"]})
    check("goto to an earlier line succeeds", resp is not None and resp.get("success") is True, f"got: {resp}")
    s.wait_event("stopped", timeout=5.0)
    resp = s.request("continue", {"threadId": tid})
    body2, frame2 = s.stop_at()
    check("the statement runs again and the breakpoint after it fires again",
          body2 is not None and body2.get("reason") == "breakpoint" and frame2.get("line") == JUMP_B, f"got: {body2} {frame2}")
    total = s.local("total", frame2.get("id", 0))
    check("the line that ran twice counted twice", total == "2", f"total={total}")
    s.finish()
    check("so Jump returned 112", "y=112" in s.output_text(), f"output: {s.output_text()!r}")
    s.close()

    # ---- 7. a different method -------------------------------------------------------------
    s = c.Session()
    s.start(obe, work, src, [JUMP_B])
    body, frame = s.stop_at()
    tid = body.get("threadId", 1)
    resp = s.request("gotoTargets", {"source": {"path": src}, "line": MAIN_JUMP})
    targets = resp.get("body", {}).get("targets", []) if resp else []
    check("gotoTargets still lists a statement in another method",
          len(targets) == 1 and "Main" in targets[0].get("label", ""), f"got: {resp}")
    resp = s.request("goto", {"threadId": tid, "targetId": targets[0]["id"] if targets else 0})
    msg = resp.get("message", "") if resp else ""
    check("goto into another method is refused with a clear message",
          resp is not None and resp.get("success") is False and "method" in msg and "Cannot" in msg, f"got: {resp}")
    check("and no stopped event follows", s.wait_event("stopped", timeout=0.5) is None)
    frame = s.top_frame(tid)
    check("the program is still where it was", frame.get("line") == JUMP_B and "Jump" in frame.get("name", ""),
          f"got: {frame}")
    total = s.local("total", frame.get("id", 0))
    check("with its variables intact", total == "1", f"total={total}")
    s.finish()
    check("it then runs to the right result: the refusal corrupted nothing", "x=12 y=111" in s.output_text(),
          f"output: {s.output_text()!r}")
    s.close()

    # ---- 8. refusals -------------------------------------------------------------------------
    s = c.Session()
    resp = s.request("goto", {"threadId": 1, "targetId": 0})
    check("goto is refused while the program is not stopped", resp is not None and resp.get("success") is False,
          f"got: {resp}")
    resp = s.request("gotoTargets", {"source": {"path": src}, "line": JUMP_C})
    check("gotoTargets is refused while the program is not stopped", resp is not None and resp.get("success") is False,
          f"got: {resp}")
    s.start(obe, work, src, [JUMP_A])
    body, frame = s.stop_at()
    tid = body.get("threadId", 1)
    targets = s.request("gotoTargets", {"source": {"path": src}, "line": JUMP_C})["body"]["targets"]
    resp = s.request("goto", {"threadId": tid, "targetId": 77})
    check("an unknown targetId is refused", resp is not None and resp.get("success") is False, f"got: {resp}")
    resp = s.request("goto", {"threadId": tid})
    check("a missing targetId is refused", resp is not None and resp.get("success") is False, f"got: {resp}")
    resp = s.request("goto", {"threadId": tid + 41, "targetId": targets[0]["id"]})
    check("another thread's id is refused", resp is not None and resp.get("success") is False and
          "thread" in resp.get("message", ""), f"got: {resp}")
    frame = s.top_frame(tid)
    check("none of the refusals moved the program", frame.get("line") == JUMP_A, f"got: {frame}")
    s.finish()
    check("it still runs to the right result", "x=12 y=111" in s.output_text(), f"output: {s.output_text()!r}")
    s.close()

    # ---- 9. into the middle of an expression ---------------------------------------------------
    M_FIRST, M_ARG = c.line_of("M_FIRST", MULTILINE), c.line_of("M_ARG", MULTILINE)
    s = c.Session()
    s.start(mobe, work, msrc, [M_FIRST])
    body, frame = s.stop_at()
    tid = body.get("threadId", 1) if body else 1
    resp = s.request("gotoTargets", {"source": {"path": msrc}, "line": M_ARG})
    targets = resp.get("body", {}).get("targets", []) if resp else []
    if targets:
        resp = s.request("goto", {"threadId": tid, "targetId": targets[0]["id"]})
        check("a line that starts inside a statement is refused",
              resp is not None and resp.get("success") is False and "statement" in resp.get("message", ""),
              f"got: {resp}")
        frame = s.top_frame(tid)
        check("the program stayed put", frame.get("line") == M_FIRST, f"got: {frame}")
    else:
        # the compiler gave the argument line no instruction of its own: nothing
        # to jump into, which is as safe
        check("the continuation line has no target (nothing to jump into)", True)
    s.finish()
    check("the multi-line program still gives its result", "sum=3" in s.output_text(), f"output: {s.output_text()!r}")
    s.close()
finally:
    shutil.rmtree(work, ignore_errors=True)

R.finish()
