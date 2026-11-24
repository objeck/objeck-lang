"""DAP stepInTargets, and stepIn with a targetId.

A statement with several calls (Left() + Right(Left())) used to be steppable only
one way: stepIn entered the first call, and getting to Right meant stepping in,
out, and over. Now stepInTargets lists the calls on the stopped statement in the
order they run, and stepIn with a targetId enters just that one, over the rest.

 1. the three new capabilities are advertised
 2. stepInTargets lists every call on the stopped line, in execution order
 3. stepIn with the second target stops in Right, over the Left() that runs first
 4. stepIn with the last target stops in Left, and the program runs on correctly
 5. stepIn with no targetId still enters the first call
 6. an unknown or non-numeric targetId is refused, and the program is left where it was
 7. a line with no calls lists none; a caller frame lists none
 8. stepInTargets is refused while the program is not stopped
 9. a statement with a single call lists it and steps into it
"""
import shutil
import tempfile

import dap_more_common as c

R = c.Results()
check = R.check

work = tempfile.mkdtemp(prefix="dap-stepin-targets-")
try:
    src, obe = c.compile_fixture(work, "stepin_targets")
    if not obe:
        print("ERROR: could not compile the fixture")
        raise SystemExit(2)
    CALLS = c.line_of("MAIN_CALLS")
    INIT = c.line_of("MAIN_INIT")
    RIGHT = c.line_of("RIGHT_BODY")
    LEFT = c.line_of("LEFT_BODY")

    # ---- 1. capabilities -------------------------------------------------
    s = c.Session()
    caps = s.init_response.get("body", {}) if s.init_response else {}
    check("supportsStepInTargetsRequest is advertised", caps.get("supportsStepInTargetsRequest") is True, f"got: {caps}")
    check("supportsGotoTargetsRequest is advertised", caps.get("supportsGotoTargetsRequest") is True, f"got: {caps}")
    check("supportsCancelRequest is advertised", caps.get("supportsCancelRequest") is True, f"got: {caps}")

    # ---- 8. not stopped (before the program runs) --------------------------
    resp = s.request("stepInTargets", {"frameId": 0})
    check("stepInTargets is refused while the program is not stopped",
          resp is not None and resp.get("success") is False, f"got: {resp}")
    s.close()

    # ---- 2. the list --------------------------------------------------------
    s = c.Session()
    s.start(obe, work, src, [CALLS])
    body, frame = s.stop_at()
    check("stopped at the line with three calls", body is not None and frame.get("line") == CALLS, f"got: {frame}")
    fid = frame.get("id", 0)
    resp = s.request("stepInTargets", {"frameId": fid})
    targets = resp.get("body", {}).get("targets", []) if resp else []
    check("stepInTargets answers with three targets", resp is not None and resp.get("success") is True and len(targets) == 3,
          f"got: {resp}")
    labels = [t.get("label", "") for t in targets]
    # The compiler evaluates the right operand of + first: Right's argument Left()
    # runs, then Right, then the Left() on the left of the +. The list is the
    # bytecode's order, which is the order the program runs them in.
    check("the targets are Left, Right, Left in the order they run",
          len(labels) == 3 and "Left" in labels[0] and "Right" in labels[1] and "Left" in labels[2], f"got: {labels}")
    check("each target has a distinct id and the statement's line",
          len({t.get("id") for t in targets}) == 3 and all(t.get("line") == CALLS for t in targets), f"got: {targets}")

    # ---- 3. step into Right (the second target) --------------------------------
    right_id = targets[1]["id"] if len(targets) == 3 else 1
    resp = s.request("stepIn", {"threadId": body.get("threadId", 1), "targetId": right_id})
    check("stepIn with a targetId is accepted", resp is not None and resp.get("success") is True, f"got: {resp}")
    body2, frame2 = s.stop_at()
    check("the step stops in Right, over the Left() before it",
          body2 is not None and "Right" in frame2.get("name", ""), f"got: {body2} {frame2}")
    check("the step is reported as a step", body2 is not None and body2.get("reason") == "step", f"got: {body2}")
    s.request("next", {"threadId": body2.get("threadId", 1)})
    body2, frame2 = s.stop_at()
    check("Right's first statement is next", frame2.get("line") == RIGHT, f"got: {frame2}")
    a = s.local("a", frame2.get("id", 0))
    check("Right received the result of the Left() before it", a == "1", f"a={a}")

    # ---- 7b. a caller frame lists none ----------------------------------------
    frames = s.frames(body2.get("threadId", 1))
    caller = frames[1]["id"] if len(frames) > 1 else -1
    resp = s.request("stepInTargets", {"frameId": caller})
    check("a caller frame has no targets", resp is not None and resp.get("success") is True and
          resp.get("body", {}).get("targets") == [], f"got: {resp}")
    check("the program runs to the end after a targeted step", s.finish(), "never terminated")
    check("and its result is unchanged by the step", "x=12 y=111" in s.output_text(), f"output: {s.output_text()!r}")
    s.close()

    # ---- 4. step into the last target: the Left() that runs after Right ---------
    s = c.Session()
    s.start(obe, work, src, [CALLS])
    body, frame = s.stop_at()
    resp = s.request("stepInTargets", {"frameId": frame.get("id", 0)})
    targets = resp.get("body", {}).get("targets", []) if resp else []
    second = targets[2]["id"] if len(targets) == 3 else 2
    s.request("stepIn", {"threadId": body.get("threadId", 1), "targetId": second})
    body2, frame2 = s.stop_at()
    check("the last target stops in Left", body2 is not None and "Left" in frame2.get("name", "") and
          frame2.get("line") == LEFT, f"got: {frame2}")
    frames = s.frames(body2.get("threadId", 1))
    check("it was entered from the statement, past the calls before it",
          len(frames) > 1 and frames[1].get("line") == CALLS, f"got: {frames}")
    check("the program runs to the end", s.finish(), "never terminated")
    check("with the right result", "x=12 y=111" in s.output_text(), f"output: {s.output_text()!r}")
    s.close()

    # ---- 5. plain stepIn, 6. unknown id ----------------------------------------
    s = c.Session()
    s.start(obe, work, src, [CALLS])
    body, frame = s.stop_at()
    tid = body.get("threadId", 1)
    resp = s.request("stepIn", {"threadId": tid, "targetId": 99})
    check("an unknown targetId is refused", resp is not None and resp.get("success") is False and
          "target" in resp.get("message", "").lower(), f"got: {resp}")
    resp = s.request("stepIn", {"threadId": tid, "targetId": "first"})
    check("a targetId that is not a number is refused", resp is not None and resp.get("success") is False, f"got: {resp}")
    frame_again = s.top_frame(tid)
    check("the refused steps left the program stopped where it was", frame_again.get("line") == CALLS,
          f"got: {frame_again}")
    s.request("stepIn", {"threadId": tid})
    body2, frame2 = s.stop_at()
    check("stepIn with no targetId still enters the first call", body2 is not None and "Left" in frame2.get("name", ""),
          f"got: {frame2}")
    s.finish()
    s.close()

    # ---- 7. no calls on the line --------------------------------------------------
    s = c.Session()
    s.start(obe, work, src, [INIT])
    body, frame = s.stop_at()
    resp = s.request("stepInTargets", {"frameId": frame.get("id", 0)})
    check("a line with no calls has no targets", resp is not None and resp.get("success") is True and
          resp.get("body", {}).get("targets") == [], f"got: {resp}")
    s.finish()
    s.close()

    # ---- 9. one call ------------------------------------------------------------
    JUMPCALL = c.line_of("MAIN_JUMP")
    s = c.Session()
    s.start(obe, work, src, [JUMPCALL])
    body, frame = s.stop_at()
    resp = s.request("stepInTargets", {"frameId": frame.get("id", 0)})
    targets = resp.get("body", {}).get("targets", []) if resp else []
    check("y := Jump() has one target, Jump", len(targets) == 1 and "Jump" in targets[0].get("label", ""), f"got: {targets}")
    s.request("stepIn", {"threadId": body.get("threadId", 1), "targetId": targets[0]["id"] if targets else 0})
    body2, frame2 = s.stop_at()
    check("the targeted step enters Jump", body2 is not None and "Jump" in frame2.get("name", ""), f"got: {frame2}")
    s.finish()
    s.close()
finally:
    shutil.rmtree(work, ignore_errors=True)

R.finish()
