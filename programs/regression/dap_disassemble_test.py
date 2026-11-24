"""DAP disassemble: the VM bytecode of the method a frame is in.

Every stack frame now carries an instructionPointerReference (the address of the
instruction it is at), and 'disassemble' turns an address into the method's
instructions: opcode and operands as the VM table (shared/instr_info.h) names
them, the calls annotated with the method they call, and the source line each
instruction came from.

 1. supportsDisassembleRequest is advertised
 2. the top frame and a caller frame carry an instructionPointerReference
 3. disassemble answers exactly instructionCount instructions with consecutive addresses
 4. the instruction at the frame's reference has the frame's source line
 5. the calls on the stopped line are there, naming the methods they call
 6. instructionOffset moves the window; positions before the method are "invalid" placeholders
 7. the first instruction of a method carries the method's name as its symbol
 8. a bad or unknown address, a missing count, and a program not yet loaded are DAP errors
"""
import os
import shutil
import tempfile

import dap_more_common as c

R = c.Results()
check = R.check

work = tempfile.mkdtemp(prefix="dap-disassemble-")
try:
    src, obe = c.compile_fixture(work, "disassemble_fixture")
    if not obe:
        print("ERROR: could not compile the fixture")
        raise SystemExit(2)
    CALLS = c.line_of("MAIN_CALLS")
    LEFT = c.line_of("LEFT_BODY")

    s = c.Session()
    caps = s.init_response.get("body", {}) if s.init_response else {}
    check("supportsDisassembleRequest is advertised", caps.get("supportsDisassembleRequest") is True, f"got: {caps}")
    resp = s.request("disassemble", {"memoryReference": "0x0", "instructionCount": 4})
    check("disassemble before the program is loaded is an error", resp is not None and resp.get("success") is False,
          f"got: {resp}")
    s.start(obe, work, src, [CALLS, LEFT])
    body, frame = s.stop_at()
    tid = body.get("threadId", 1) if body else 1
    check("stopped on the line with the calls", frame.get("line") == CALLS, f"got: {frame}")
    ref = frame.get("instructionPointerReference")
    check("the top frame has an instructionPointerReference", isinstance(ref, str) and ref.startswith("0x"), f"got: {frame}")

    resp = s.request("disassemble", {"memoryReference": ref, "instructionCount": 12})
    listing = resp.get("body", {}).get("instructions", []) if resp else []
    check("disassemble answers the requested number of instructions",
          resp is not None and resp.get("success") is True and len(listing) == 12, f"got: {resp}")
    addresses = [int(i["address"], 16) for i in listing]
    check("their addresses are consecutive", all(b - a == 1 for a, b in zip(addresses, addresses[1:])), f"got: {addresses}")
    check("the first is at the reference, on the frame's line",
          listing and listing[0]["address"] == ref and listing[0].get("line") == CALLS, f"got: {listing[:1]}")
    check("each instruction is text with an opcode",
          all(i.get("instruction", "").split(" ")[0].isupper() or i["instruction"] == "??" for i in listing),
          f"got: {[i['instruction'] for i in listing]}")
    check("each names the file", all(os.path.realpath(i.get("location", {}).get("path", "")) == os.path.realpath(src) for i in listing if "line" in i), f"got: {listing[:2]}")

    # the whole statement's worth: the three calls, each naming its callee
    resp = s.request("disassemble", {"memoryReference": ref, "instructionCount": 40})
    listing = resp["body"]["instructions"]
    calls = [i["instruction"] for i in listing if i["instruction"].startswith("MTHD_CALL")]
    check("the calls on the line are there", len([x for x in calls if "Left" in x]) >= 2 and any("Right" in x for x in calls),
          f"got: {calls}")
    check("a call's operands are its class, method and native flag",
          any(x.startswith("MTHD_CALL ") and x.count(",") == 2 for x in calls), f"got: {calls}")

    # ---- 6. windows and placeholders ------------------------------------------------------
    resp = s.request("disassemble", {"memoryReference": ref, "instructionOffset": 3, "instructionCount": 2})
    moved = resp["body"]["instructions"] if resp and resp.get("success") else []
    check("instructionOffset moves the window", len(moved) == 2 and int(moved[0]["address"], 16) == int(ref, 16) + 3,
          f"got: {resp}")
    resp = s.request("disassemble", {"memoryReference": ref, "offset": 2, "instructionCount": 1})
    byte_moved = resp["body"]["instructions"] if resp and resp.get("success") else []
    check("offset moves it too, one address per instruction", len(byte_moved) == 1 and
          int(byte_moved[0]["address"], 16) == int(ref, 16) + 2, f"got: {resp}")

    # ---- 7. a method's first instruction and its symbol ----------------------------------------
    resp = s.request("disassemble", {"memoryReference": ref, "instructionOffset": -1000, "instructionCount": 3})
    before = resp["body"]["instructions"] if resp and resp.get("success") else []
    check("positions before the method are invalid placeholders",
          len(before) == 3 and all(i.get("presentationHint") == "invalid" for i in before), f"got: {resp}")

    s.request("continue", {"threadId": tid})
    body2, frame2 = s.stop_at()
    check("continued to the breakpoint in Left", frame2.get("line") in (LEFT, LEFT - 1) or "Left" in frame2.get("name", ""),
          f"got: {frame2}")
    frames = s.frames(body2.get("threadId", 1))
    caller = frames[1] if len(frames) > 1 else {}
    check("a caller frame has an instructionPointerReference too",
          isinstance(caller.get("instructionPointerReference"), str), f"got: {frames}")
    ref2 = frames[0].get("instructionPointerReference")
    resp = s.request("disassemble", {"memoryReference": ref2, "instructionOffset": -int(ref2, 16) % (1 << 24),
                                     "instructionCount": 2})
    head = resp["body"]["instructions"] if resp and resp.get("success") else []
    check("the first instruction of a method carries its name as the symbol",
          len(head) == 2 and "Left" in head[0].get("symbol", ""), f"got: {resp}")
    resp = s.request("disassemble", {"memoryReference": caller.get("instructionPointerReference"), "instructionCount": 3})
    in_caller = resp["body"]["instructions"] if resp and resp.get("success") else []
    check("the caller's reference disassembles the caller (Main)", len(in_caller) == 3 and in_caller[0].get("line") == CALLS,
          f"got: {resp}")

    # ---- 8. errors ----------------------------------------------------------------------------------
    for label, arguments in (("a reference that is not an address", {"memoryReference": "main", "instructionCount": 3}),
                             ("no reference", {"instructionCount": 3}),
                             ("an address in no method", {"memoryReference": "0x7fffffffffffffff", "instructionCount": 3}),
                             ("no instructionCount", {"memoryReference": ref}),
                             ("an instructionCount of zero", {"memoryReference": ref, "instructionCount": 0})):
        resp = s.request("disassemble", arguments)
        check(f"{label} is refused", resp is not None and resp.get("success") is False, f"got: {resp}")
    check("the program runs to the end", s.finish(), "never terminated")
    s.close()
finally:
    shutil.rmtree(work, ignore_errors=True)

R.finish()
