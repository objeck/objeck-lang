#!/usr/bin/env python3
"""Tests for check_test_exit_codes.py's marker handling.

Run:  python tools/cicd/test_check_test_exit_codes.py
"""
import contextlib
import io
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_test_exit_codes as lint  # noqa: E402


class MarkerProblemTests(unittest.TestCase):
    def test_named_and_plain_markers_are_accepted(self):
        self.assertEqual(lint.marker_problems("# EXPECT_COMPILE_ERROR: Expected '}'\nclass A {}\n"), [])
        self.assertEqual(lint.marker_problems("# EXPECT_COMPILE_ERROR:   padded\r\nclass A {}\r\n"), [])
        self.assertEqual(lint.marker_problems("# EXPECT_COMPILE_ERROR\nclass A {}\n"), [])

    def test_mid_line_marker_is_not_a_marker(self):
        source = "# a comment naming # EXPECT_COMPILE_ERROR: \"quoted\" \n"
        self.assertEqual(lint.marker_problems(source), [])
        self.assertIsNone(lint.COMPILE_ERROR_LINE.search(source))

    def test_empty_message_is_rejected(self):
        self.assertEqual(len(lint.marker_problems("# EXPECT_COMPILE_ERROR:\n")), 1)
        self.assertEqual(len(lint.marker_problems("# EXPECT_COMPILE_ERROR:   \n")), 1)

    def test_trailing_blanks_are_rejected(self):
        self.assertEqual(len(lint.marker_problems("# EXPECT_COMPILE_ERROR: text \n")), 1)

    def test_characters_cmd_cannot_carry_are_rejected(self):
        for text in ('say "hi"', "bang!", "a\\b"):
            self.assertEqual(len(lint.marker_problems("# EXPECT_COMPILE_ERROR: %s\n" % text)), 1, text)

    def test_misspelled_marker_is_rejected(self):
        self.assertEqual(len(lint.marker_problems("# EXPECT_COMPILE_ERRORS\n")), 1)

    def test_marker_behind_bom_is_rejected(self):
        self.assertEqual(len(lint.marker_problems("﻿# EXPECT_RUNTIME_ERROR\n")), 1)
        self.assertEqual(len(lint.marker_problems("﻿# EXPECT_COMPILE_ERROR: x\n")), 1)


class RepositoryTests(unittest.TestCase):
    def test_regression_tree_is_clean(self):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = lint.main()
        self.assertEqual(code, 0, err.getvalue())


if __name__ == "__main__":
    unittest.main(verbosity=2)
