"""DAP cancel, progress events, and the invalidated event after a change.

The adapter used to read one request, answer it, and only then read the next, so a
'cancel' could not be seen until the request it named had finished. Requests are
now read on a thread of their own: a cancel is noted the moment it arrives, a
request cancelled before its turn is answered "cancelled" without being started,
and one in flight is stopped at its next check.

The slow request is made with the test hook OBJECK_DAP_TEST_DELAY_MS, which holds
evaluate and variables for that long (or until cancelled).

 1. cancel of a request waiting behind a slow one: it is answered "cancelled" at
    once, not after its own delay
 2. cancel of the request in flight: it ends early, "cancelled"
 3. cancel of a request that already finished, of an unknown id, and one with no id,
    all succeed; the adapter keeps serving
 4. without the hook, requests are unaffected (no false cancellations)
 5. progress: a client that declared supportsProgressReporting gets progressStart /
    progressEnd around launch and around program startup, in order, each id once;
    a client that did not gets none
 6. invalidated after setVariable and setExpression for a client that declared
    supportsInvalidatedEvent; none for one that did not
"""
import shutil
import tempfile
import time

import dap_more_common as c

R = c.Results()
check = R.check

work = tempfile.mkdtemp(prefix="dap-cancel-")
try:
    src, obe = c.compile_fixture(work, "cancel_fixture")
    if not obe:
        print("ERROR: could not compile the fixture")
        raise SystemExit(2)
    MAIN_JUMP = c.line_of("MAIN_JUMP")
    DELAY = 2000   # ms a test-hook request takes unless cancelled

    # ---- 1. a request waiting behind a slow one --------------------------------------
    s = c.Session(env={"OBJECK_DAP_TEST_DELAY_MS": str(DELAY)})
    s.start(obe, work, src, [MAIN_JUMP])
    body, frame = s.stop_at()
    check("stopped at the breakpoint", body is not None and frame.get("line") == MAIN_JUMP, f"got: {frame}")

    started = time.time()
    first = s.send("evaluate", {"expression": "x", "frameId": frame.get("id", 0), "context": "watch"})
    second = s.send("evaluate", {"expression": "x", "frameId": frame.get("id", 0), "context": "watch"})
    cancel = s.send("cancel", {"requestId": second})
    r_cancel = s.wait_response_to(cancel, timeout=30.0)
    check("the cancel itself is answered", r_cancel is not None and r_cancel.get("success") is True, f"got: {r_cancel}")
    r_first = s.wait_response_to(first, timeout=30.0)
    first_done = time.time() - started
    r_second = s.wait_response_to(second, timeout=30.0)
    second_done = time.time() - started
    check("the first request is unaffected and takes its delay",
          r_first is not None and r_first.get("success") is True and first_done >= DELAY / 1000.0 * 0.8,
          f"got: {r_first} after {first_done:.2f}s")
    check("the one behind it, cancelled while waiting, is answered 'cancelled'",
          r_second is not None and r_second.get("success") is False and r_second.get("message") == "cancelled",
          f"got: {r_second}")
    check("and without waiting out its own delay", second_done < first_done + DELAY / 1000.0 * 0.5,
          f"first {first_done:.2f}s, second {second_done:.2f}s")

    # ---- 2. the request in flight ------------------------------------------------------
    started = time.time()
    flight = s.send("variables", {"variablesReference": 1000 + frame.get("id", 0)})
    time.sleep(0.4)
    cancel = s.send("cancel", {"requestId": flight})
    r_flight = s.wait_response_to(flight, timeout=10.0)
    ended = time.time() - started
    check("a request cancelled while it runs is answered 'cancelled'",
          r_flight is not None and r_flight.get("success") is False and r_flight.get("message") == "cancelled",
          f"got: {r_flight}")
    check("and ends early, well before its delay", ended < DELAY / 1000.0 * 0.9, f"took {ended:.2f}s")
    check("its cancel is answered too", s.wait_response_to(cancel, timeout=5.0) is not None)

    # ---- 3. cancels that name nothing live ------------------------------------------------
    for label, arguments in (("a request that already finished", {"requestId": first}),
                             ("a request that never existed", {"requestId": 987654}),
                             ("no requestId (a progress id instead)", {"progressId": "objeck-startup"}),
                             ("no arguments at all", None)):
        resp = s.request("cancel", arguments, timeout=5.0)
        check(f"cancel of {label} succeeds", resp is not None and resp.get("success") is True, f"got: {resp}")
    resp = s.request("threads")
    check("the adapter keeps serving after them", resp is not None and resp.get("success") is True, f"got: {resp}")
    s.close()

    # ---- 4. no hook: nothing is cancelled that was not asked ----------------------------------
    s = c.Session()
    s.start(obe, work, src, [MAIN_JUMP])
    body, frame = s.stop_at()
    resp = s.request("evaluate", {"expression": "x", "frameId": frame.get("id", 0), "context": "watch"})
    check("evaluate answers normally", resp is not None and resp.get("success") is True and
          resp.get("body", {}).get("result") == "12", f"got: {resp}")
    sc = s.request("scopes", {"frameId": frame.get("id", 0)})
    ref = sc["body"]["scopes"][0]["variablesReference"] if sc else 0
    resp = s.request("variables", {"variablesReference": ref})
    check("variables answers normally", resp is not None and resp.get("success") is True and
          len(resp.get("body", {}).get("variables", [])) >= 2, f"got: {resp}")
    # a cancel sent after the fact must not poison a later request that reuses nothing
    s.request("cancel", {"requestId": s.last_seq})
    resp = s.request("evaluate", {"expression": "x", "frameId": frame.get("id", 0), "context": "watch"})
    check("a later request is not caught by an earlier cancel", resp is not None and resp.get("success") is True,
          f"got: {resp}")
    s.close()

    # ---- 6. invalidated ---------------------------------------------------------------------------
    for supported in (True, False):
        s = c.Session(init={"supportsInvalidatedEvent": True} if supported else None)
        s.start(obe, work, src, [MAIN_JUMP])
        body, frame = s.stop_at()
        fid = frame.get("id", 0)
        sc = s.request("scopes", {"frameId": fid})
        ref = sc["body"]["scopes"][0]["variablesReference"] if sc else 0
        resp = s.request("setVariable", {"variablesReference": ref, "name": "x", "value": "40"})
        ok1 = resp is not None and resp.get("success") is True
        resp2 = s.request("setExpression", {"expression": "x", "value": "41", "frameId": fid})
        ok2 = resp2 is not None and resp2.get("success") is True
        time.sleep(0.3)
        inval = s.events_named("invalidated")
        if supported:
            check("setVariable and setExpression succeed", ok1 and ok2, f"got: {resp} {resp2}")
            check("each is followed by an invalidated event for variables",
                  len(inval) == 2 and all("variables" in e.get("body", {}).get("areas", []) for e in inval),
                  f"got: {inval}")
            check("naming the stopped thread", all(e.get("body", {}).get("threadId") == body.get("threadId", 1) for e in inval),
                  f"got: {inval}")
        else:
            check("without supportsInvalidatedEvent no invalidated event is sent", ok1 and ok2 and not inval,
                  f"got: {inval} {resp} {resp2}")
        s.close()

    # ---- 5. progress ----------------------------------------------------------------------------------
    s = c.Session(init={"supportsProgressReporting": True})
    s.start(obe, work, src, [MAIN_JUMP])
    body, frame = s.stop_at()
    with s.lock:
        order = [(m.get("event") or m.get("command"), m.get("body", {}).get("progressId"))
                 for m in s.log if m.get("event", "").startswith("progress") or m.get("command") == "launch"]
    ids = [pid for name, pid in order if pid]
    check("launch is bracketed by progressStart and progressEnd, before its response",
          [name for name, pid in order if pid == "objeck-launch"] == ["progressStart", "progressUpdate", "progressEnd"] and
          order.index(("launch", None)) > order.index(("progressEnd", "objeck-launch")), f"got: {order}")
    check("program startup has its own progressStart and progressEnd",
          [name for name, pid in order if pid == "objeck-startup"] == ["progressStart", "progressEnd"], f"got: {order}")
    check("the startup report ends once the program is running (before it stops)",
          body is not None and ids.count("objeck-startup") == 2, f"got: {order}")
    starts = [m for m in s.events_named("progressStart")]
    check("progressStart carries an id and a title",
          all(m.get("body", {}).get("progressId") and m.get("body", {}).get("title") for m in starts), f"got: {starts}")
    s.finish()
    s.close()

    s = c.Session()
    s.start(obe, work, src, [MAIN_JUMP])
    s.stop_at()
    time.sleep(0.3)
    check("a client that did not declare supportsProgressReporting gets no progress events",
          not any(m.get("event", "").startswith("progress") for m in s.log), f"got: {[m for m in s.log if m.get('event','').startswith('progress')]}")
    s.finish()
    s.close()
finally:
    shutil.rmtree(work, ignore_errors=True)

R.finish()
