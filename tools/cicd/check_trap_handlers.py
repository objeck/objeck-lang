#!/usr/bin/env python3
"""Every trap the compiler can emit has a handler in the VM, and every intrinsic
the scanner knows is parsed and lowered.

WHY THIS EXISTS
---------------
An intrinsic (a bare keyword such as GC_TEST_ADDRESS in core/compiler/lib_src/*.obs)
is wired through up to five places, and nothing connects them:

  core/shared/traps.h             the `Traps` enum: the ordinal IS the wire id
  core/compiler/scanner.{h,cpp}   a token, and its `ident_map` entry
  core/compiler/parser.cpp        a `case TOKEN:` in Parser::ParseStatement
  core/compiler/intermediate.cpp  a `case` in EmitSystemDirective, which lowers it
  core/vm/common.cpp              a `case` in TrapProcessor::ProcessTrap

Forget one and the failure is late and quiet. A missing ProcessTrap case compiles
and links; the VM reaches the end of the switch, returns false, and the program
dies with a stack unwind (dispatch.cpp, Handle_TRAP) the first time the trap runs.
A missing parser case leaves the keyword an unknown identifier. And for years
Parser::ParseSystemStatement, a 1,900 line duplicate of the live parsing that had no
caller, collected edits that changed nothing (deleted in the change that added this
check; docs/ADDING_AN_INTRINSIC.md has the list).

WHAT IT CHECKS
--------------
1. Each enumerator of `Traps` has a `case NAME:` in ProcessTrap, unless it is in
   NOT_VM_TRAPS below with a written reason. The allowlist cannot rot:
     * an entry that has gained a handler, or is no longer in the enum, fails;
     * a "dead" entry (never emitted) fails once anything else mentions it;
     * a "pseudo-op" entry (rewritten by the compiler) fails if the code
       generator emits it as an operand, which is precisely a trap the VM rejects.
2. Each intrinsic in the `_SYSTEM` part of Scanner::LoadKeywords has a
   `case NAME:` in ParseStatement that builds a SystemStatement for the SAME name
   (a pasted case that names its neighbour is caught), and a `case` in
   EmitSystemDirective. NOT_PARSED_INTRINSICS lists the exceptions, kept honest the
   same way.
3. Each id EmitSystemDirective writes ahead of a TRAP has a `case` in ProcessTrap.
   A few traps (SET_SIGNAL, RAISE_SIGNAL, ...) take their id from instrs.h rather
   than from `Traps`; they are fine so long as the VM handles them.
4. The loader's verification pass (core/vm/loader.cpp) accepts every trap id a
   program can carry: its LAST_TRAP names the last enumerator of `Traps`, and
   IsTrapId has a `case` for each id EmitSystemDirective writes that is not in
   `Traps`. Otherwise a new trap compiles and then every program using it is
   refused at load ("trap without a known trap id").

Usage: check_trap_handlers.py          (from anywhere in the tree)
       check_trap_handlers.py --test   (negative controls)
"""

import os
import re
import sys

TRAPS_H = "core/shared/traps.h"
SCANNER_CPP = "core/compiler/scanner.cpp"
PARSER_CPP = "core/compiler/parser.cpp"
EMITTER_CPP = "core/compiler/intermediate.cpp"
VM_CPP = "core/vm/common.cpp"
LOADER_CPP = "core/vm/loader.cpp"

# Enumerators of `Traps` the VM does not implement, and why. "dead" means nothing
# emits or handles it; "pseudo-op" means the compiler rewrites it before the VM sees
# it. Neither can simply be deleted: the enum is unnumbered, so removing an
# enumerator renumbers every trap after it and breaks every compiled .obl.
NOT_VM_TRAPS = {
    "LOAD_CLS_BY_NAME": (
        "dead",
        "no parser, emitter or VM reference since the enumerator was added; kept so "
        "later ordinals do not shift"),
    "FILE_LOGGER_DIR": (
        "dead",
        "no parser, emitter or VM reference; kept so later ordinals do not shift"),
    "LOAD_INST_UID": (
        "pseudo-op",
        "the keyword behind Base->GetInstanceID(); EmitSystemDirective lowers it to "
        "LOAD_INST_MEM + INST_STABLE_ID, so the VM only ever sees INST_STABLE_ID"),
}


# Intrinsics the scanner, emitter and VM know but the parser cannot build, and why. The
# keyword lexes, and then ParseStatement has no case for it, so a program that writes it
# gets a parse error: unreachable, not a run-time failure.
NOT_PARSED_INTRINSICS = {
    "SOCK_TCP_SSL_SRV_CERT": (
        "scanner token, EmitSystemDirective case and TrapProcessor::SockTcpSslCertSrv exist, but "
        "neither parser copy ever had the case and no lib_src method uses it; the server "
        "certificate is not reachable from Objeck. Add the parser case together with a "
        "lib_src method, or delete the three other sites"),
}


def repo_root():
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.abspath(os.path.join(here, "..", ".."))


def read(root, rel):
    with open(os.path.join(root, rel), encoding="utf-8", errors="replace") as f:
        return f.read()


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def function_body(text, header_re):
    """Text of the function whose definition line matches header_re, up to the first
    closing brace in column 0 (this tree's functions are all written that way)."""
    m = re.search(header_re, text, flags=re.M)
    if not m:
        return None
    end = re.search(r"^}", text[m.end():], flags=re.M)
    return text[m.end():m.end() + end.start()] if end else text[m.end():]


def enum_names(text, enum_re):
    m = re.search(enum_re, text)
    if not m:
        return None
    body = strip_comments(text[m.end():text.index("}", m.end())])
    names = []
    for item in body.split(","):
        item = item.strip().split("=")[0].strip()
        if item:
            names.append(item)
    return names


def case_labels(body):
    return set(re.findall(r"\bcase\s+(?:instructions::)?(\w+)\s*:", body))


def system_keywords(scanner_text):
    """The intrinsics: ident_map entries between `#ifdef _SYSTEM` and `#endif` in
    Scanner::LoadKeywords."""
    body = function_body(scanner_text, r"^void Scanner::LoadKeywords\(\)\s*\n?\{")
    if body is None or "#ifdef _SYSTEM" not in body:
        return None
    body = body[body.index("#ifdef _SYSTEM"):]
    return re.findall(r'ident_map\[L"(\w+)"\]\s*=\s*(\w+)\s*;', body)


def analyze(sources):
    """sources: dict of the five files' text. Returns (problems, stats)."""
    problems = []

    traps = enum_names(sources[TRAPS_H], r"enum\s+Traps\s*\{")
    vm_body = function_body(sources[VM_CPP], r"^bool TrapProcessor::ProcessTrap\([^)]*\)\s*\{")
    parse_body = function_body(sources[PARSER_CPP], r"^Statement\* Parser::ParseStatement\([^)]*\)\s*\n?\{")
    emit_body = function_body(sources[EMITTER_CPP], r"^void IntermediateEmitter::EmitSystemDirective\([^)]*\)\s*\n?\{")
    keywords = system_keywords(sources[SCANNER_CPP])
    loader_last = re.search(r"\bLAST_TRAP\s*=\s*(?:instructions::)?(\w+)\s*;", sources[LOADER_CPP])
    loader_ids = function_body(sources[LOADER_CPP], r"^static bool IsTrapId\([^)]*\)\s*\n?\{")
    for what, value in (("the Traps enum in " + TRAPS_H, traps),
                        ("TrapProcessor::ProcessTrap in " + VM_CPP, vm_body),
                        ("Parser::ParseStatement in " + PARSER_CPP, parse_body),
                        ("EmitSystemDirective in " + EMITTER_CPP, emit_body),
                        ("the _SYSTEM keywords in " + SCANNER_CPP, keywords),
                        ("LAST_TRAP in " + LOADER_CPP, loader_last),
                        ("IsTrapId in " + LOADER_CPP, loader_ids)):
        if not value:
            problems.append("could not find %s: the check's patterns no longer match "
                            "the source, so it would pass vacuously" % what)
    if problems:
        return problems, {}

    handled = case_labels(vm_body)
    emit_cases = case_labels(emit_body)
    parse_cases = {}
    for m in re.finditer(r"\bcase\s+(\w+)\s*:\s*statement\s*=\s*TreeFactory::Instance\(\)->MakeSystemStatement"
                         r"\([^;]*?instructions::(\w+)\s*\)\s*;", parse_body, flags=re.S):
        parse_cases[m.group(1)] = m.group(2)
    parse_labels = case_labels(parse_body)

    # the compiler's other mentions of a trap: everything but `case X:` labels
    emitted = set()
    for m in re.finditer(r"MakeIntLitInstruction\([^;]*?instructions::(\w+)\s*\)", emit_body, flags=re.S):
        emitted.add(m.group(1))
    outside_compiler = "\n".join(strip_comments(sources[k]) for k in sources if k != TRAPS_H)

    # 1. every trap has a handler or a written reason
    trap_set = set(traps)
    for name in traps:
        if name in NOT_VM_TRAPS:
            continue
        if name not in handled:
            problems.append("trap %s has no `case %s:` in TrapProcessor::ProcessTrap (%s): a program "
                            "that reaches it dies at run time. Add the handler, or list it in "
                            "NOT_VM_TRAPS with the reason it needs none" % (name, name, VM_CPP))
    for name, (kind, why) in sorted(NOT_VM_TRAPS.items()):
        if name not in trap_set:
            problems.append("NOT_VM_TRAPS lists %s, which is not in the Traps enum: remove the entry" % name)
            continue
        if name in handled:
            problems.append("NOT_VM_TRAPS lists %s but ProcessTrap handles it: remove the entry" % name)
        if name in emitted:
            problems.append("%s is on NOT_VM_TRAPS yet EmitSystemDirective emits it as a trap id: the "
                            "VM rejects it at run time" % name)
        if kind == "dead":
            others = [k for k in sources if k != TRAPS_H
                      and re.search(r"\b%s\b" % name, strip_comments(sources[k]))]
            if others:
                problems.append("%s is listed as dead but %s mentions it: give it a handler or change "
                                "its entry" % (name, ", ".join(others)))
        elif kind != "pseudo-op":
            problems.append("NOT_VM_TRAPS entry %s has unknown kind %r" % (name, kind))
        if not why.strip():
            problems.append("NOT_VM_TRAPS entry %s has no reason" % name)

    # 2. every intrinsic is parsed and lowered
    for name, token in keywords:
        if name != token:
            problems.append("scanner maps %r to the token %s: keep the two names identical so the "
                            "other sites can be found by grep" % (name, token))
        if name in NOT_PARSED_INTRINSICS:
            if name in parse_labels:
                problems.append("NOT_PARSED_INTRINSICS lists %s but ParseStatement handles it: remove the entry"
                                % name)
        elif name not in parse_labels:
            problems.append("intrinsic %s (scanner.cpp ident_map) has no `case %s:` in Parser::ParseStatement"
                            % (name, name))
        elif name not in parse_cases:
            problems.append("intrinsic %s: its `case %s:` in ParseStatement does not build a SystemStatement "
                            "the usual way (statement = ...MakeSystemStatement(... instructions::%s))"
                            % (name, name, name))
        elif parse_cases[name] != name:
            problems.append("intrinsic %s: ParseStatement builds the SystemStatement for instructions::%s "
                            "(a pasted case that kept its neighbour's name)" % (name, parse_cases[name]))
        if name not in emit_cases:
            problems.append("intrinsic %s has no `case` in IntermediateEmitter::EmitSystemDirective (%s): "
                            "it parses and then emits nothing" % (name, EMITTER_CPP))

    # 3. every id the emitter writes ahead of a TRAP is one the VM handles. (Some come from instrs.h,
    # not Traps; what matters is the case label, so this is the check on what the compiler can emit.)
    for name in sorted(emitted - handled):
        problems.append("EmitSystemDirective writes instructions::%s as a trap id, but ProcessTrap has no "
                        "`case %s:` for it: the VM rejects it at run time" % (name, name))

    # 4. the loader's verification pass accepts every trap id
    if loader_last.group(1) != traps[-1]:
        problems.append("LAST_TRAP in %s is %s, but the last enumerator of Traps is %s: the loader "
                        "refuses every program that uses a trap after LAST_TRAP" % (LOADER_CPP, loader_last.group(1),
                                                                                   traps[-1]))
    for name in sorted(emitted - trap_set - case_labels(loader_ids)):
        problems.append("EmitSystemDirective writes instructions::%s as a trap id, but IsTrapId in %s has no "
                        "`case instructions::%s:`: the loader refuses every program that uses it"
                        % (name, LOADER_CPP, name))

    known = set(n for n, _ in keywords)
    for name, why in sorted(NOT_PARSED_INTRINSICS.items()):
        if name not in known:
            problems.append("NOT_PARSED_INTRINSICS lists %s, which is not a _SYSTEM keyword: remove the entry"
                            % name)
        if not why.strip():
            problems.append("NOT_PARSED_INTRINSICS entry %s has no reason" % name)

    stats = {"traps": len(traps), "handled": len(trap_set & handled), "allowlisted": len(NOT_VM_TRAPS),
             "intrinsics": len(keywords)}
    return problems, stats


def load(root):
    return {k: read(root, k) for k in (TRAPS_H, SCANNER_CPP, PARSER_CPP, EMITTER_CPP, VM_CPP, LOADER_CPP)}


def report(problems, stats):
    if problems:
        print("%d problem(s):" % len(problems))
        for p in problems:
            print("  " + p)
        return 1
    print("%(traps)d traps: %(handled)d handled by ProcessTrap, %(allowlisted)d allowlisted with a reason; "
          "%(intrinsics)d intrinsics scanned, parsed and lowered (some allowlisted with a reason)" % stats)
    return 0


def self_test(root):
    """Negative controls: each mutation of the real sources must be reported, or the
    check's patterns have stopped seeing the thing they guard."""
    base = load(root)
    problems, stats = analyze(base)
    if problems:
        print("self-test: the unmodified tree must pass first:")
        for p in problems:
            print("  " + p)
        return 1

    def mutate(key, old, new, count=1):
        out = dict(base)
        if old not in out[key]:
            raise SystemExit("self-test: %r not found in %s; update the control" % (old, key))
        out[key] = out[key].replace(old, new, count)
        return out

    controls = [
        ("a trap with its VM case removed",
         mutate(VM_CPP, "  case GC_TEST_ADDRESS: {", "  case NO_SUCH_TRAP_FOR_TEST: {"),
         "GC_TEST_ADDRESS has no `case"),
        ("a new trap nobody handles",
         mutate(TRAPS_H, "GC_TEST_ADDRESS\n", "GC_TEST_ADDRESS,\n    A_NEW_TRAP_FOR_TEST\n"),
         "A_NEW_TRAP_FOR_TEST has no `case"),
        ("an intrinsic with no parser case",
         mutate(PARSER_CPP, "    case GC_TEST_ADDRESS:\n", "    case NO_SUCH_TOKEN_FOR_TEST:\n"),
         "intrinsic GC_TEST_ADDRESS (scanner.cpp ident_map) has no"),
        ("an intrinsic whose parser case builds its neighbour's statement",
         mutate(PARSER_CPP, "instructions::GC_TEST_ADDRESS);", "instructions::THREAD_IS_ALIVE);"),
         "builds the SystemStatement for instructions::THREAD_IS_ALIVE"),
        ("an intrinsic with no emitter case",
         mutate(EMITTER_CPP, "  case instructions::GC_TEST_ADDRESS:", "  case instructions::NO_SUCH_FOR_TEST:"),
         "intrinsic GC_TEST_ADDRESS has no `case` in IntermediateEmitter::EmitSystemDirective"),
        ("a stale allowlist entry (the trap gained a handler)",
         mutate(VM_CPP, "  case GC_TEST_ADDRESS: {", "  case GC_TEST_ADDRESS: case FILE_LOGGER_DIR: {"),
         "NOT_VM_TRAPS lists FILE_LOGGER_DIR but ProcessTrap handles it"),
        ("a 'dead' trap that something started to use",
         mutate(PARSER_CPP, "    case GC_TEST_ADDRESS:\n", "    case GC_TEST_ADDRESS:\n      (void)LOAD_CLS_BY_NAME;\n"),
         "LOAD_CLS_BY_NAME is listed as dead"),
        ("an allowlisted trap the code generator emits",
         mutate(EMITTER_CPP, "instructions::INST_STABLE_ID))", "instructions::LOAD_INST_UID))"),
         "LOAD_INST_UID is on NOT_VM_TRAPS yet EmitSystemDirective emits it"),
        ("an id the emitter writes that the VM does not handle",
         mutate(EMITTER_CPP, "instructions::INST_STABLE_ID))", "instructions::LOAD_ARY_SIZE))"),
         "writes instructions::LOAD_ARY_SIZE as a trap id"),
        ("a stale NOT_PARSED_INTRINSICS entry (the parser gained the case)",
         mutate(PARSER_CPP, "    case GC_TEST_ADDRESS:\n", "    case GC_TEST_ADDRESS: case SOCK_TCP_SSL_SRV_CERT:\n"),
         "NOT_PARSED_INTRINSICS lists SOCK_TCP_SSL_SRV_CERT but ParseStatement handles it"),
        ("a new last trap the loader's LAST_TRAP does not name",
         mutate(TRAPS_H, "GC_TEST_ADDRESS\n", "GC_TEST_ADDRESS,\n    A_NEW_TRAP_FOR_TEST\n"),
         "LAST_TRAP in core/vm/loader.cpp is GC_TEST_ADDRESS, but the last enumerator of Traps is A_NEW_TRAP_FOR_TEST"),
        ("an instruction id written as a trap id that the loader does not accept",
         mutate(LOADER_CPP, "  case instructions::RAISE_SIGNAL:\n", ""),
         "IsTrapId in core/vm/loader.cpp has no `case instructions::RAISE_SIGNAL:`"),
        ("patterns that no longer match the source",
         mutate(VM_CPP, "TrapProcessor::ProcessTrap(", "TrapProcessor::ProcessTrapRenamed("),
         "could not find TrapProcessor::ProcessTrap"),
    ]
    failed = 0
    for label, sources, expect in controls:
        found, _ = analyze(sources)
        if any(expect in p for p in found):
            print("ok   caught: " + label)
        else:
            failed += 1
            print("FAIL not caught: %s (expected %r; got %r)" % (label, expect, found))
    if failed:
        return 1
    print("self-test: all %d negative controls caught" % len(controls))
    return 0


def main(argv):
    root = repo_root()
    if "--test" in argv:
        return self_test(root)
    problems, stats = analyze(load(root))
    return report(problems, stats)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
