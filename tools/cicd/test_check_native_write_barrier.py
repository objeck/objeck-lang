#!/usr/bin/env python3
"""Unit tests for check_native_write_barrier.py.

Run: python -m unittest tools.cicd.test_check_native_write_barrier
  or: python tools/cicd/test_check_native_write_barrier.py

The checker's whole value is that it fails on an unbarriered store, so most of
these build a tree with one and require a non-zero exit. The false-positive cases
matter just as much: a check that flags every integer store gets switched off, and
a switched-off check guards nothing.
"""

import os
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from io import StringIO

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import check_native_write_barrier as lint  # noqa: E402

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# The store that must be reported: a label read out of a received array, into an
# object the library allocated.
UNBARRIERED = """
void thing(VMContext& context) {
  size_t* labels_array = APITools_GetArray(context, 6);
  size_t* labels_objs = APITools_GetArray(labels_array);
  size_t* result_obj = APITools_CreateObject(context, L"Some.Result");
  result_obj[1] = labels_objs[2];
}
"""

BARRIERED = """
void thing(VMContext& context) {
  size_t* labels_array = APITools_GetArray(context, 6);
  size_t* labels_objs = APITools_GetArray(labels_array);
  size_t* result_obj = APITools_CreateObject(context, L"Some.Result");
  result_obj[1] = labels_objs[2];
  APITools_WriteBarrier(context, result_obj);
}
"""

# Integers into object slots. sdl.cpp does exactly this and must stay quiet.
INTEGERS_ONLY = """
void thing(VMContext& context) {
  SDL_Texture* texture = (SDL_Texture*)APITools_GetIntValue(context, 1);
  int width, height;
  SDL_QueryTexture(texture, nullptr, nullptr, &width, &height);
  size_t* pixel_obj = APITools_CreateObject(context, L"Game.SDL2.PixelData");
  pixel_obj[2] = (size_t)height;
  pixel_obj[0] = box.x;
  pixel_obj[1] = bundle->GetLineNumber();
}
"""

# A received ARRAY stored directly. Arrays never live in the nursery, so this
# needs no barrier -- flagging it would be wrong, not merely noisy.
RECEIVED_ARRAY_STORED = """
void thing(VMContext& context) {
  size_t* input_array = APITools_GetArray(context, 1);
  size_t* result_obj = APITools_CreateObject(context, L"Some.Result");
  result_obj[0] = (size_t)input_array;
}
"""

# A value the library allocated itself, into another object it allocated.
OWN_ALLOCATION = """
void thing(VMContext& context) {
  size_t* result_obj = APITools_CreateObject(context, L"Some.Result");
  size_t* name_obj = APITools_CreateStringObject(context, L"hello");
  result_obj[0] = (size_t)name_obj;
}
"""


class ScanTest(unittest.TestCase):
    """scan_file on a single synthetic source."""

    def scan(self, text, name="probe.cpp"):
        with tempfile.TemporaryDirectory() as work:
            path = os.path.join(work, name)
            with open(path, "w", encoding="utf-8") as handle:
                handle.write(text)
            return lint.scan_file(path, work)

    def test_unbarriered_store_is_found(self):
        hits = self.scan(UNBARRIERED)
        self.assertEqual(len(hits), 1, hits)
        self.assertEqual(hits[0][2], "result_obj")
        self.assertEqual(hits[0][3], "labels_objs[...]")
        self.assertFalse(hits[0][4], "should be reported as unbarriered")

    def test_barriered_store_is_found_and_marked(self):
        hits = self.scan(BARRIERED)
        self.assertEqual(len(hits), 1, hits)
        self.assertTrue(hits[0][4], "the barrier on the next line should count")

    def test_integer_stores_are_not_flagged(self):
        self.assertEqual(self.scan(INTEGERS_ONLY), [])

    def test_a_received_array_stored_directly_is_not_flagged(self):
        self.assertEqual(self.scan(RECEIVED_ARRAY_STORED), [])

    def test_own_allocation_is_not_flagged(self):
        self.assertEqual(self.scan(OWN_ALLOCATION), [])

    def test_a_barrier_too_far_away_does_not_count(self):
        far = BARRIERED.replace(
            "  APITools_WriteBarrier(context, result_obj);",
            "\n".join(["  // one", "  // two", "  // three", "  // four",
                       "  APITools_WriteBarrier(context, result_obj);"]))
        hits = self.scan(far)
        self.assertEqual(len(hits), 1, hits)
        self.assertFalse(hits[0][4], "a barrier five lines down is not this store's")

    def test_a_barrier_for_a_different_object_does_not_count(self):
        wrong = BARRIERED.replace("APITools_WriteBarrier(context, result_obj)",
                                  "APITools_WriteBarrier(context, other_obj)")
        hits = self.scan(wrong)
        self.assertEqual(len(hits), 1, hits)
        self.assertFalse(hits[0][4])


class ControlTest(unittest.TestCase):
    """The positive control is what stops a broken scan reporting a clean tree."""

    def test_control_fails_when_a_site_is_not_found(self):
        errors = lint.check_control([])
        self.assertEqual(len(errors), len(lint.CONTROL))
        self.assertIn("no longer finds", errors[0])

    def test_control_fails_when_a_site_is_unbarriered(self):
        hits = [(rel.replace("\\", "/"), 100 + i, target, source, False)
                for i, (rel, target, source) in enumerate(lint.CONTROL)]
        errors = lint.check_control(hits)
        self.assertEqual(len(errors), len(lint.CONTROL))
        self.assertIn("has no barrier", errors[0])

    def test_control_passes_when_all_sites_are_barriered(self):
        hits = [(rel.replace("\\", "/"), 100 + i, target, source, True)
                for i, (rel, target, source) in enumerate(lint.CONTROL)]
        self.assertEqual(lint.check_control(hits), [])


class VmWiringTest(unittest.TestCase):
    """The VM must hand the barrier to every native call.

    A VMContext missing the assignment turns every barrier in every library into a
    no-op with no symptom at all, so this half needs to be able to fail too.
    """

    WIRED = """
void StackInterpreter::CallNative() {
  VMContext context{};
  context.data_array = args;
  context.alloc_managed_obj = MemoryManager::AllocateObjectNative;
  context.write_barrier = MemoryManager::JitWriteBarrier;
  (*ext_func)(context);
}
"""

    UNWIRED = """
void StackInterpreter::CallNative() {
  VMContext context{};
  context.data_array = args;
  context.alloc_managed_obj = MemoryManager::AllocateObjectNative;
  (*ext_func)(context);
}
"""

    def wiring(self, text):
        with tempfile.TemporaryDirectory() as work:
            vm = os.path.join(work, "core", "vm")
            os.makedirs(vm)
            with open(os.path.join(vm, "interpreter.cpp"), "w", encoding="utf-8") as handle:
                handle.write(text)
            return lint.check_vm_wiring(work)

    def test_a_wired_context_is_accepted(self):
        self.assertEqual(self.wiring(self.WIRED), [])

    def test_a_context_missing_the_barrier_is_reported(self):
        errors = self.wiring(self.UNWIRED)
        self.assertEqual(len(errors), 1, errors)
        self.assertIn("silently does nothing", errors[0])

    def test_one_unwired_among_several_is_reported(self):
        errors = self.wiring(self.WIRED + self.UNWIRED + self.WIRED)
        self.assertEqual(len(errors), 1, errors)

    def test_no_construction_at_all_is_reported(self):
        # A rename or a move must fail rather than quietly check nothing.
        errors = self.wiring("int main() { return 0; }\n")
        self.assertEqual(len(errors), 1, errors)
        self.assertIn("no longer works", errors[0])

    def test_a_missing_file_is_reported(self):
        with tempfile.TemporaryDirectory() as work:
            errors = lint.check_vm_wiring(work)
        self.assertEqual(len(errors), 1, errors)
        self.assertIn("cannot find", errors[0])

    def test_the_real_vm_is_wired(self):
        self.assertEqual(lint.check_vm_wiring(REPO_ROOT), [])


class MainTest(unittest.TestCase):
    """End to end, including against the real tree."""

    def test_an_empty_tree_fails(self):
        # A check that passes by finding nothing to look at is worthless. This is
        # the failure mode a wrong --root, a rename or a moved directory produces.
        with tempfile.TemporaryDirectory() as work:
            argv = sys.argv
            sys.argv = ["check_native_write_barrier.py", work]
            try:
                buf = StringIO()
                with redirect_stdout(buf):
                    code = lint.main()
            finally:
                sys.argv = argv
            self.assertEqual(code, 1, buf.getvalue())
            self.assertIn("no native library sources found", buf.getvalue())

    def test_the_real_tree_passes(self):
        argv = sys.argv
        sys.argv = ["check_native_write_barrier.py", REPO_ROOT]
        try:
            buf = StringIO()
            with redirect_stdout(buf):
                code = lint.main()
        finally:
            sys.argv = argv
        self.assertEqual(code, 0, buf.getvalue())
        # and it saw the control, rather than passing on an empty result
        self.assertIn("control: %d/%d" % (len(lint.CONTROL), len(lint.CONTROL)), buf.getvalue())


if __name__ == "__main__":
    unittest.main(verbosity=2)
