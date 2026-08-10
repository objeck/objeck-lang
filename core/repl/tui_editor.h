/***************************************************************************
 * Full-screen editing over the REPL's Document (editor phase 2).
 *
 * Copyright (c) 2026, Randy Hollines
 * All rights reserved.
 ***************************************************************************/

#ifndef __TUI_EDITOR_H__
#define __TUI_EDITOR_H__

#include "editor.h"
#include "term.h"
#include "screen.h"
#include "keymap.h"
#include <functional>
#include <vector>
#include <cwctype>

// A viewport over Document -- the same buffer the line commands edit, so
// '/l', '/s' and friends keep working on whatever this editor produced. The
// REPL shell's read-only lines (the class/function frame) render dimmed and
// refuse edits with a status message rather than beeping or being skipped;
// pressing Enter on one inserts a new line ABOVE it, which on the closing
// brace means "give me a fresh line inside the function" -- the thing the
// user almost always meant.
//
// Tabs render at the house indent of three columns, matching the REPL's own
// List/Save formatting. Cursor arithmetic goes through DisplayX so CJK and
// fullwidth text stay aligned.

namespace Tui {

  class EditorView {
    Document& doc;
    Term& term;
    Screen screen;

    int rows;
    int cols;
    size_t top;          // first visible document line
    int left_x;          // first visible display column of the text area
    size_t cur_row;      // cursor: document line
    size_t cur_col;      // cursor: character index within the line
    size_t want_col;     // sticky column for vertical movement
    bool dirty;
    bool quit_armed;     // set by a first Ctrl+Q with unsaved changes
    std::wstring status; // transient message; cleared by the next key

    // run pane: compile-and-execute output, shown below the text area
    std::function<bool(std::wstring&)> runner;
    std::vector<std::wstring> pane;
    size_t pane_top;
    bool pane_visible;
    std::vector<size_t> error_lines;   // 1-based document lines from diagnostics
    size_t error_index;

    // Undo: every mutation goes through a line-granular operation so it can be
    // played backward. A group is one user action (a split is two ops); plain
    // typing coalesces into the previous group so undo takes back a run of
    // characters, not one keystroke at a time.
    struct EditOp {
      int kind;   // 0 = line replaced, 1 = line inserted, 2 = line deleted
      size_t row;
      std::wstring before;
      std::wstring after;
    };
    struct EditGroup {
      std::vector<EditOp> ops;
      size_t row_before, col_before;
      size_t row_after, col_after;
    };
    std::vector<EditGroup> undo_stack;
    std::vector<EditGroup> redo_stack;
    bool typing_run;   // the last committed group was coalescable typing

    // selection: an anchor plus the live cursor; cleared by any plain movement
    bool sel_active;
    size_t sel_row, sel_col;

    // internal clipboard; linewise means whole lines (no OS clipboard yet)
    std::vector<std::wstring> clip;
    bool clip_linewise;

    // binding profile and vi mode; simple/insert unless the user opts in
    KeymapState keys;

    static const int TAB_STOP = 3;

    // display column of character index `col`, honoring tabs and wide chars
    static int DisplayX(const std::wstring& line, size_t col) {
      int x = 0;
      for(size_t i = 0; i < col && i < line.size(); ++i) {
        if(line[i] == L'\t') {
          x += TAB_STOP - (x % TAB_STOP);
        }
        else {
          x += CharWidth(line[i]);
        }
      }
      return x;
    }

    bool IsReadOnly(size_t row) {
      return doc.GetLineType(row) != Line::Type::RW_LINE;
    }

    // pane height: a third of the terminal, never starving the text area
    int PaneRows() const {
      if(!pane_visible || rows < 10) {
        return 0;
      }
      int height = rows / 3;
      if(height < 4) {
        height = 4;
      }
      return height;
    }

    int TextRows() const {
      return rows - 2 - PaneRows();   // title bar, hint bar, pane
    }

    int GutterWidth() {
      int digits = 1;
      for(size_t n = doc.Size(); n >= 10; n /= 10) {
        ++digits;
      }
      return digits + 1;   // number plus a space
    }

    int TextCols() {
      return cols - GutterWidth();
    }

    void ClampCursor() {
      if(doc.Size() == 0) {
        cur_row = cur_col = 0;
        return;
      }
      if(cur_row >= doc.Size()) {
        cur_row = doc.Size() - 1;
      }
      const size_t len = doc.GetLine(cur_row).size();
      if(cur_col > len) {
        cur_col = len;
      }
    }

    // keep the cursor inside the viewport, vertically and horizontally
    void ScrollToCursor() {
      if(cur_row < top) {
        top = cur_row;
      }
      if(cur_row >= top + (size_t)TextRows()) {
        top = cur_row - TextRows() + 1;
      }

      const int x = DisplayX(doc.GetLine(cur_row), cur_col);
      if(x < left_x) {
        left_x = x;
      }
      if(x >= left_x + TextCols()) {
        left_x = x - TextCols() + 1;
      }
    }

    void Draw() {
      screen.Clear();

      // title bar: name, modified marker, dimensions
      std::wstring title = L" " + doc.GetName();
      if(dirty) {
        title += L" [+]";
      }
      const std::wstring dims = std::to_wstring(cols) + L"x" + std::to_wstring(rows) + L" ";
      screen.Fill(0, 0, cols, L' ', ATTR_REVERSE);
      screen.Put(0, 0, title, ATTR_REVERSE | ATTR_BOLD);
      screen.Put(0, cols - (int)dims.size(), dims, ATTR_REVERSE);

      // text area
      const int gutter = GutterWidth();
      for(int screen_row = 0; screen_row < TextRows(); ++screen_row) {
        const size_t row = top + screen_row;
        if(row >= doc.Size()) {
          screen.Put(1 + screen_row, 0, L"~", ATTR_GRAY);
          continue;
        }

        const bool read_only = IsReadOnly(row);
        const unsigned char text_attr = read_only ? ATTR_GRAY : ATTR_DEFAULT;
        const unsigned char num_attr = (row == cur_row) ? (ATTR_CYAN | ATTR_BOLD) : ATTR_GRAY;

        std::wstring num = std::to_wstring(row + 1);
        while((int)num.size() < gutter - 1) {
          num = L" " + num;
        }
        screen.Put(1 + screen_row, 0, num + L" ", num_attr);

        // walk the line character by character so tabs and wide characters
        // land on the correct display columns under horizontal scroll
        const std::wstring line = doc.GetLine(row);
        int x = 0;
        for(size_t i = 0; i < line.size() && x < left_x + TextCols(); ++i) {
          int width;
          if(line[i] == L'\t') {
            width = TAB_STOP - (x % TAB_STOP);
          }
          else {
            width = CharWidth(line[i]);
          }

          const unsigned char cell_attr =
            InSelection(row, i) ? (unsigned char)(text_attr | ATTR_REVERSE) : text_attr;

          if(x + width > left_x) {
            const int screen_x = gutter + (x - left_x);
            if(line[i] == L'\t' || x < left_x) {
              // tabs, and wide characters cut by the left edge, pad as spaces
              for(int k = (x < left_x) ? left_x : x; k < x + width && k < left_x + TextCols(); ++k) {
                screen.Put(1 + screen_row, gutter + (k - left_x), L" ", cell_attr);
              }
            }
            else if(x - left_x + width <= TextCols()) {
              screen.Put(1 + screen_row, screen_x, std::wstring(1, line[i]), cell_attr);
            }
          }
          x += width;
        }
      }

      // run pane: a labeled rule, then output with error lines highlighted
      if(PaneRows() > 0) {
        const int pane_start = 1 + TextRows();
        screen.Fill(pane_start, 0, cols, L'-', ATTR_GRAY);
        screen.Put(pane_start, 1, L" output · F6 hide · Alt+Up/Down scroll ", ATTR_GRAY | ATTR_BOLD);
        for(int i = 0; i < PaneRows() - 1; ++i) {
          const size_t index = pane_top + i;
          if(index >= pane.size()) {
            break;
          }
          const bool is_error = pane[index].find(L":(") != std::wstring::npos;
          screen.Put(pane_start + 1 + i, 1, pane[index], is_error ? ATTR_RED : ATTR_DEFAULT);
        }
      }

      // hint bar; a transient status message takes its place until a key
      screen.Fill(rows - 1, 0, cols, L' ', ATTR_REVERSE);
      std::wstring pos = L" Ln " + std::to_wstring(cur_row + 1) + L", Col " + std::to_wstring(cur_col + 1) + L" ";
      if(keys.profile == PROFILE_VI) {
        pos = (keys.mode == MODE_NORMAL ? L" NORMAL ·" :
               keys.mode == MODE_VISUAL ? L" VISUAL ·" : L" INSERT ·") + pos;
      }
      if(!status.empty()) {
        screen.Put(rows - 1, 0, L" " + status, ATTR_REVERSE | ATTR_BOLD);
      }
      else {
        screen.Put(rows - 1, 0, L" ^S save  ^Q quit  ^Z undo  ^Y redo  ^C/^X/^V clipboard  F5 run", ATTR_REVERSE);
      }
      screen.Put(rows - 1, cols - (int)pos.size(), pos, ATTR_REVERSE);

      screen.Flush(term);

      // park the real cursor on the edit point
      const int x = DisplayX(doc.GetLine(cur_row), cur_col);
      term.MoveTo(2 + (int)(cur_row - top), 1 + GutterWidth() + (x - left_x));
      term.ShowCursor();
    }

    // a one-line modal prompt on the hint bar; empty result means cancelled
    std::wstring Prompt(const std::wstring& label) {
      std::wstring input;
      for(;;) {
        screen.Fill(rows - 1, 0, cols, L' ', ATTR_REVERSE);
        screen.Put(rows - 1, 0, L" " + label + input, ATTR_REVERSE | ATTR_BOLD);
        screen.Flush(term);
        term.MoveTo(rows, 2 + (int)(label.size() + input.size()));

        const Key key = term.ReadKey();
        if(key.code == KEY_NONE) {
          continue;
        }
        if(key.code == KEY_ENTER) {
          return input;
        }
        if(key.code == KEY_ESC) {
          return L"";
        }
        if(key.code == KEY_BACKSPACE) {
          if(!input.empty()) {
            input.pop_back();
          }
        }
        else if(key.code == KEY_CHAR && !key.ctrl && key.ch >= 32) {
          input += key.ch;
        }
      }
    }

    void DoSave() {
      std::wstring name = doc.GetName();
      if(name == DEFAULT_FILE_NAME) {
        name = Prompt(L"save as: ");
        if(name.empty()) {
          status = L"save cancelled";
          return;
        }
      }

      if(doc.Save(name)) {
        doc.SetName(name);
        dirty = false;
        status = L"saved '" + name + L"'";
      }
      else {
        status = L"unable to write '" + name + L"'";
      }
    }

    // ----- undo plumbing: mutations funnel through these -----

    EditGroup Begin() {
      EditGroup group;
      group.row_before = cur_row;
      group.col_before = cur_col;
      group.row_after = group.col_after = 0;
      return group;
    }

    void OpSet(EditGroup& group, size_t row, const std::wstring& after) {
      EditOp op;
      op.kind = 0;
      op.row = row;
      op.before = doc.GetLine(row);
      op.after = after;
      doc.SetLine(row, after);
      group.ops.push_back(op);
    }

    void OpIns(EditGroup& group, size_t row, const std::wstring& text) {
      EditOp op;
      op.kind = 1;
      op.row = row;
      op.after = text;
      doc.InsertLine(row, text);
      group.ops.push_back(op);
    }

    void OpDel(EditGroup& group, size_t row) {
      EditOp op;
      op.kind = 2;
      op.row = row;
      op.before = doc.GetLine(row);
      doc.DeleteLine(row);
      group.ops.push_back(op);
    }

    void Commit(EditGroup& group, bool coalesce = false) {
      if(group.ops.empty()) {
        return;
      }
      group.row_after = cur_row;
      group.col_after = cur_col;
      dirty = true;
      redo_stack.clear();

      if(coalesce && typing_run && !undo_stack.empty()) {
        EditGroup& last = undo_stack.back();
        if(last.ops.size() == 1 && group.ops.size() == 1 &&
           last.ops[0].kind == 0 && group.ops[0].kind == 0 &&
           last.ops[0].row == group.ops[0].row) {
          last.ops[0].after = group.ops[0].after;
          last.row_after = cur_row;
          last.col_after = cur_col;
          return;
        }
      }

      undo_stack.push_back(group);
      typing_run = coalesce;
      if(undo_stack.size() > 512) {
        undo_stack.erase(undo_stack.begin());
      }
    }

    void DoUndo() {
      if(undo_stack.empty()) {
        status = L"nothing to undo";
        return;
      }
      EditGroup group = undo_stack.back();
      undo_stack.pop_back();
      for(auto op = group.ops.rbegin(); op != group.ops.rend(); ++op) {
        if(op->kind == 0) {
          doc.SetLine(op->row, op->before);
        }
        else if(op->kind == 1) {
          doc.DeleteLine(op->row);
        }
        else {
          doc.InsertLine(op->row, op->before);
        }
      }
      cur_row = group.row_before;
      cur_col = group.col_before;
      ClampCursor();
      redo_stack.push_back(group);
      typing_run = false;
      dirty = true;
    }

    void DoRedo() {
      if(redo_stack.empty()) {
        status = L"nothing to redo";
        return;
      }
      EditGroup group = redo_stack.back();
      redo_stack.pop_back();
      for(auto op = group.ops.begin(); op != group.ops.end(); ++op) {
        if(op->kind == 0) {
          doc.SetLine(op->row, op->after);
        }
        else if(op->kind == 1) {
          doc.InsertLine(op->row, op->after);
        }
        else {
          doc.DeleteLine(op->row);
        }
      }
      cur_row = group.row_after;
      cur_col = group.col_after;
      ClampCursor();
      undo_stack.push_back(group);
      typing_run = false;
      dirty = true;
    }

    // ----- selection -----

    void SelAnchor() {
      if(!sel_active) {
        sel_active = true;
        sel_row = cur_row;
        sel_col = cur_col;
      }
    }

    void SelBounds(size_t& r1, size_t& c1, size_t& r2, size_t& c2) {
      if(sel_row < cur_row || (sel_row == cur_row && sel_col <= cur_col)) {
        r1 = sel_row; c1 = sel_col; r2 = cur_row; c2 = cur_col;
      }
      else {
        r1 = cur_row; c1 = cur_col; r2 = sel_row; c2 = sel_col;
      }
    }

    bool InSelection(size_t row, size_t col) {
      if(!sel_active) {
        return false;
      }
      size_t r1, c1, r2, c2;
      SelBounds(r1, c1, r2, c2);
      if(row < r1 || row > r2) {
        return false;
      }
      if(r1 == r2) {
        return col >= c1 && col < c2;
      }
      if(row == r1) {
        return col >= c1;
      }
      if(row == r2) {
        return col < c2;
      }
      return true;
    }

    std::vector<std::wstring> SelectionText() {
      std::vector<std::wstring> text;
      size_t r1, c1, r2, c2;
      SelBounds(r1, c1, r2, c2);
      if(r1 == r2) {
        const std::wstring line = doc.GetLine(r1);
        text.push_back(line.substr(c1, c2 - c1));
      }
      else {
        text.push_back(doc.GetLine(r1).substr(c1));
        for(size_t row = r1 + 1; row < r2; ++row) {
          text.push_back(doc.GetLine(row));
        }
        text.push_back(doc.GetLine(r2).substr(0, c2));
      }
      return text;
    }

    // removes the selected span through the op layer; refuses if any line in
    // the span is read-only, since a partial removal would be worse
    bool DeleteSelectionOps(EditGroup& group) {
      size_t r1, c1, r2, c2;
      SelBounds(r1, c1, r2, c2);
      for(size_t row = r1; row <= r2; ++row) {
        if(IsReadOnly(row)) {
          status = L"selection crosses a read-only line";
          return false;
        }
      }

      if(r1 == r2) {
        std::wstring line = doc.GetLine(r1);
        line.erase(c1, c2 - c1);
        OpSet(group, r1, line);
      }
      else {
        const std::wstring joined = doc.GetLine(r1).substr(0, c1) + doc.GetLine(r2).substr(c2);
        OpSet(group, r1, joined);
        for(size_t row = r2; row > r1; --row) {
          OpDel(group, r1 + 1);
        }
      }
      cur_row = r1;
      cur_col = c1;
      sel_active = false;
      return true;
    }

    void DoCopy() {
      if(sel_active) {
        clip = SelectionText();
        clip_linewise = false;
        const size_t chars = clip.size();
        status = L"copied " + std::to_wstring(chars) + L" segment(s)";
        sel_active = false;
      }
      else {
        clip.clear();
        clip.push_back(doc.GetLine(cur_row));
        clip_linewise = true;
        status = L"copied line";
      }
    }

    void DoCut() {
      if(!sel_active) {
        if(IsReadOnly(cur_row)) {
          status = L"read-only shell line";
          return;
        }
        clip.clear();
        clip.push_back(doc.GetLine(cur_row));
        clip_linewise = true;
        EditGroup group = Begin();
        OpDel(group, cur_row);
        ClampCursor();
        cur_col = 0;
        Commit(group);
        status = L"cut line";
        return;
      }

      clip = SelectionText();
      clip_linewise = false;
      EditGroup group = Begin();
      if(DeleteSelectionOps(group)) {
        Commit(group);
        status = L"cut selection";
      }
    }

    void DoPaste() {
      if(clip.empty()) {
        status = L"clipboard is empty";
        return;
      }

      EditGroup group = Begin();
      if(sel_active && !DeleteSelectionOps(group)) {
        return;
      }

      if(clip_linewise) {
        // whole lines land above the cursor's line, cursor staying put
        for(size_t i = 0; i < clip.size(); ++i) {
          OpIns(group, cur_row + i, clip[i]);
        }
        cur_row += clip.size();
        Commit(group);
        return;
      }

      if(IsReadOnly(cur_row)) {
        status = L"read-only shell line";
        return;
      }
      const std::wstring line = doc.GetLine(cur_row);
      const std::wstring left = line.substr(0, cur_col);
      const std::wstring right = line.substr(cur_col);
      if(clip.size() == 1) {
        OpSet(group, cur_row, left + clip[0] + right);
        cur_col += clip[0].size();
      }
      else {
        OpSet(group, cur_row, left + clip[0]);
        for(size_t i = 1; i + 1 < clip.size(); ++i) {
          OpIns(group, cur_row + i, clip[i]);
        }
        OpIns(group, cur_row + clip.size() - 1, clip.back() + right);
        cur_row += clip.size() - 1;
        cur_col = clip.back().size();
      }
      Commit(group);
    }

    void InsertChar(wchar_t ch) {
      EditGroup group = Begin();
      if(sel_active && !DeleteSelectionOps(group)) {
        return;
      }
      if(IsReadOnly(cur_row)) {
        status = L"read-only shell line";
        return;
      }
      std::wstring line = doc.GetLine(cur_row);
      line.insert(cur_col, 1, ch);
      OpSet(group, cur_row, line);
      ++cur_col;
      // a fresh group after a selection delete; plain typing coalesces
      Commit(group, group.ops.size() == 1);
    }

    void DoNewline() {
      EditGroup group = Begin();
      if(sel_active && !DeleteSelectionOps(group)) {
        return;
      }

      if(IsReadOnly(cur_row)) {
        // a new line above the read-only line: on the closing brace this is
        // "insert inside the function", the placement the user meant
        OpIns(group, cur_row, L"");
        cur_col = 0;
        Commit(group);
        return;
      }

      const std::wstring line = doc.GetLine(cur_row);
      OpSet(group, cur_row, line.substr(0, cur_col));
      OpIns(group, cur_row + 1, line.substr(cur_col));
      ++cur_row;
      cur_col = 0;
      Commit(group);
    }

    void DoBackspace() {
      if(sel_active) {
        EditGroup group = Begin();
        if(DeleteSelectionOps(group)) {
          Commit(group);
        }
        return;
      }

      if(cur_col > 0) {
        if(IsReadOnly(cur_row)) {
          status = L"read-only shell line";
          return;
        }
        EditGroup group = Begin();
        std::wstring line = doc.GetLine(cur_row);
        line.erase(cur_col - 1, 1);
        OpSet(group, cur_row, line);
        --cur_col;
        Commit(group);
        return;
      }

      // column zero: merge into the previous line when both sides are editable
      if(cur_row == 0 || IsReadOnly(cur_row) || IsReadOnly(cur_row - 1)) {
        return;
      }
      EditGroup group = Begin();
      const std::wstring prev = doc.GetLine(cur_row - 1);
      const std::wstring line = doc.GetLine(cur_row);
      OpSet(group, cur_row - 1, prev + line);
      OpDel(group, cur_row);
      --cur_row;
      cur_col = prev.size();
      Commit(group);
    }

    void DoDelete() {
      if(sel_active) {
        EditGroup group = Begin();
        if(DeleteSelectionOps(group)) {
          Commit(group);
        }
        return;
      }

      const std::wstring line = doc.GetLine(cur_row);
      if(cur_col < line.size()) {
        if(IsReadOnly(cur_row)) {
          status = L"read-only shell line";
          return;
        }
        EditGroup group = Begin();
        std::wstring edited = line;
        edited.erase(cur_col, 1);
        OpSet(group, cur_row, edited);
        Commit(group);
        return;
      }

      // end of line: pull the next line up
      if(cur_row + 1 >= doc.Size() || IsReadOnly(cur_row) || IsReadOnly(cur_row + 1)) {
        return;
      }
      EditGroup group = Begin();
      OpSet(group, cur_row, line + doc.GetLine(cur_row + 1));
      OpDel(group, cur_row + 1);
      Commit(group);
    }

    // strip ANSI color sequences the REPL's own error printing embeds
    static std::wstring StripAnsi(const std::wstring& text) {
      std::wstring out;
      out.reserve(text.size());
      for(size_t i = 0; i < text.size(); ++i) {
        if(text[i] == L'\x1b' && i + 1 < text.size() && text[i + 1] == L'[') {
          i += 2;
          while(i < text.size() && !iswalpha(text[i])) {
            ++i;
          }
          continue;
        }
        out += text[i];
      }
      return out;
    }

    void DoRun() {
      if(!runner) {
        status = L"running is not available here";
        return;
      }

      // Reverted to a synchronous call. Moving the run to a worker thread
      // deadlocked even a trivial program: the in-process VM has main-thread
      // affinity (process-global Loader/MemoryManager/StackInterpreter state
      // plus its own GC threads), so it cannot be driven off the UI thread.
      // A looping program still freezes obi here -- the real fix is running
      // the program as a SUBPROCESS (docs/OBI_EDITOR_RUN_PLAN.md). What IS
      // kept: the console-capture fix in io_capture.h (so output reaches the
      // pane on Windows) and the screen invalidation below (so the editor
      // repaints instead of appearing frozen after a run).
      std::wstring output;
      const bool ok = runner(output);

      // The program may have written to the terminal behind the diff's back;
      // forget the front buffer and repaint everything.
      term.Clear();
      screen.Invalidate();


      // split into pane lines; diagnostics look like 'name:(line,col): text'
      pane.clear();
      error_lines.clear();
      std::wstring line;
      for(const wchar_t ch : StripAnsi(output) + L"\n") {
        if(ch == L'\n') {
          if(!line.empty() && line.back() == L'\r') {
            line.pop_back();
          }
          const size_t mark = line.find(L":(");
          if(mark != std::wstring::npos) {
            const long parsed = wcstol(line.c_str() + mark + 2, nullptr, 10);
            if(parsed > 0) {
              error_lines.push_back((size_t)parsed);
            }
          }
          pane.push_back(line);
          line.clear();
        }
        else {
          line += ch;
        }
      }
      while(!pane.empty() && pane.back().empty()) {
        pane.pop_back();
      }

      pane_visible = true;
      pane_top = 0;
      error_index = 0;
      if(ok) {
        status = L"program finished · " + std::to_wstring(pane.size()) + L" line(s) of output";
      }
      else {
        status = std::to_wstring(error_lines.size()) + L" error(s) · F8 jumps to the next one";
        if(!error_lines.empty()) {
          cur_row = error_lines[0] > 0 ? error_lines[0] - 1 : 0;
          ClampCursor();
          cur_col = 0;
        }
      }
    }

    void DoNextError() {
      if(error_lines.empty()) {
        status = L"no errors to visit";
        return;
      }
      error_index = (error_index + 1) % error_lines.size();
      cur_row = error_lines[error_index] > 0 ? error_lines[error_index] - 1 : 0;
      ClampCursor();
      cur_col = 0;
      status = L"error " + std::to_wstring(error_index + 1) + L" of " + std::to_wstring(error_lines.size());
    }

    void DoDeleteLine() {
      if(IsReadOnly(cur_row)) {
        status = L"read-only shell line";
        return;
      }
      EditGroup group = Begin();
      OpDel(group, cur_row);
      ClampCursor();
      cur_col = 0;
      Commit(group);
    }

  public:
    EditorView(Document& document, Term& terminal,
               std::function<bool(std::wstring&)> run_program = nullptr)
      : doc(document), term(terminal), runner(run_program) {
      rows = 24;
      cols = 80;
      top = 0;
      left_x = 0;
      cur_row = cur_col = want_col = 0;
      dirty = false;
      quit_armed = false;
      pane_top = 0;
      pane_visible = false;
      error_index = 0;
      typing_run = false;
      sel_active = false;
      sel_row = sel_col = 0;
      clip_linewise = false;
    }

    // returns the cursor's document line, so the REPL can keep its own
    // position in step with where the user left off
    size_t Run(size_t start_row) {
      term.Size(rows, cols);
      screen.Resize(rows, cols);

      cur_row = start_row;
      ClampCursor();
      cur_col = doc.GetLine(cur_row).size();
      want_col = cur_col;

      for(;;) {
        ScrollToCursor();
        term.HideCursor();
        Draw();

        const Key key = term.ReadKey();
        if(key.code == KEY_NONE) {
          continue;
        }
        status.clear();

        if(key.code == KEY_RESIZE) {
          term.Size(rows, cols);
          screen.Resize(rows, cols);
          continue;
        }

        const Action action = Resolve(key, keys);
        if(action != ACT_QUIT) {
          quit_armed = false;
        }

        // plain movement drops the selection and shifted movement extends it,
        // except in vi visual mode, where every movement extends
        switch(action) {
        case ACT_LEFT: case ACT_RIGHT: case ACT_UP: case ACT_DOWN:
        case ACT_LINE_START: case ACT_LINE_END: case ACT_PAGE_UP:
        case ACT_PAGE_DOWN: case ACT_DOC_START: case ACT_DOC_END:
          if(keys.mode == MODE_VISUAL) {
            SelAnchor();
          }
          else {
            sel_active = false;
          }
          break;
        case ACT_SEL_LEFT: case ACT_SEL_RIGHT: case ACT_SEL_UP:
        case ACT_SEL_DOWN: case ACT_SEL_HOME: case ACT_SEL_END:
          SelAnchor();
          break;
        default:
          break;
        }

        switch(action) {
        case ACT_SEL_LEFT:
        case ACT_LEFT:
          if(cur_col > 0) {
            --cur_col;
          }
          else if(cur_row > 0) {
            --cur_row;
            cur_col = doc.GetLine(cur_row).size();
          }
          want_col = cur_col;
          break;

        case ACT_SEL_RIGHT:
        case ACT_RIGHT:
          if(cur_col < doc.GetLine(cur_row).size()) {
            ++cur_col;
          }
          else if(cur_row + 1 < doc.Size()) {
            ++cur_row;
            cur_col = 0;
          }
          want_col = cur_col;
          break;

        case ACT_SEL_UP:
        case ACT_UP:
          if(cur_row > 0) {
            --cur_row;
            cur_col = want_col;
            ClampCursor();
          }
          break;

        case ACT_SEL_DOWN:
        case ACT_DOWN:
          if(cur_row + 1 < doc.Size()) {
            ++cur_row;
            cur_col = want_col;
            ClampCursor();
          }
          break;

        case ACT_SEL_HOME:
        case ACT_LINE_START:
          cur_col = want_col = 0;
          break;

        case ACT_SEL_END:
        case ACT_LINE_END:
          cur_col = want_col = doc.GetLine(cur_row).size();
          break;

        case ACT_PAGE_UP:
          cur_row = (cur_row > (size_t)TextRows()) ? cur_row - TextRows() : 0;
          cur_col = want_col;
          ClampCursor();
          break;

        case ACT_PAGE_DOWN:
          cur_row += TextRows();
          cur_col = want_col;
          ClampCursor();
          break;

        case ACT_DOC_START:
          cur_row = 0;
          cur_col = want_col = 0;
          break;

        case ACT_DOC_END:
          cur_row = doc.Size() ? doc.Size() - 1 : 0;
          cur_col = want_col = doc.GetLine(cur_row).size();
          break;

        case ACT_NEWLINE:
          DoNewline();
          want_col = cur_col;
          break;

        case ACT_BACKSPACE:
          DoBackspace();
          want_col = cur_col;
          break;

        case ACT_DELETE:
          DoDelete();
          break;

        case ACT_DELETE_LINE:
          DoDeleteLine();
          want_col = cur_col;
          break;

        case ACT_TAB:
          InsertChar(L'\t');
          want_col = cur_col;
          break;

        case ACT_INSERT_CHAR:
          InsertChar(key.ch);
          want_col = cur_col;
          break;

        case ACT_SAVE:
          DoSave();
          break;

        case ACT_RUN:
          DoRun();
          break;

        case ACT_TOGGLE_PANE:
          pane_visible = !pane_visible && !pane.empty();
          ClampCursor();
          break;

        case ACT_NEXT_ERROR:
          DoNextError();
          break;

        case ACT_PANE_UP:
          if(pane_top > 0) {
            --pane_top;
          }
          break;

        case ACT_PANE_DOWN:
          if(pane_top + 1 < pane.size()) {
            ++pane_top;
          }
          break;

        case ACT_UNDO:
          DoUndo();
          want_col = cur_col;
          break;

        case ACT_REDO:
          DoRedo();
          want_col = cur_col;
          break;

        case ACT_COPY:
          DoCopy();
          if(keys.mode == MODE_VISUAL) {
            keys.mode = MODE_NORMAL;
          }
          break;

        case ACT_CUT:
          DoCut();
          want_col = cur_col;
          if(keys.mode == MODE_VISUAL) {
            keys.mode = MODE_NORMAL;
          }
          break;

        case ACT_PASTE:
          DoPaste();
          want_col = cur_col;
          break;

        case ACT_MODE_INSERT:
          keys.mode = MODE_INSERT;
          sel_active = false;
          break;

        case ACT_MODE_APPEND:
          keys.mode = MODE_INSERT;
          sel_active = false;
          if(cur_col < doc.GetLine(cur_row).size()) {
            ++cur_col;
          }
          want_col = cur_col;
          break;

        case ACT_OPEN_BELOW: {
          EditGroup group = Begin();
          OpIns(group, cur_row + 1, L"");
          ++cur_row;
          cur_col = want_col = 0;
          Commit(group);
          keys.mode = MODE_INSERT;
          sel_active = false;
          break;
        }

        case ACT_MODE_NORMAL:
          keys.mode = MODE_NORMAL;
          sel_active = false;
          break;

        case ACT_MODE_VISUAL:
          keys.mode = MODE_VISUAL;
          SelAnchor();
          break;

        case ACT_TOGGLE_PROFILE:
          if(keys.profile == PROFILE_SIMPLE) {
            keys.profile = PROFILE_VI;
            keys.mode = MODE_NORMAL;
            status = L"vi bindings · i inserts · F2 returns to simple";
          }
          else {
            keys.profile = PROFILE_SIMPLE;
            keys.mode = MODE_INSERT;
            status = L"simple bindings";
          }
          sel_active = false;
          break;

        case ACT_QUIT:
          if(dirty && !quit_armed) {
            quit_armed = true;
            status = L"unsaved changes -- Ctrl+Q again to leave, Ctrl+S to save";
            break;
          }
          return cur_row;

        default:
          break;
        }
      }
    }
  };
}

#endif
