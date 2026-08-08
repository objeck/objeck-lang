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

          if(x + width > left_x) {
            const int screen_x = gutter + (x - left_x);
            if(line[i] == L'\t' || x < left_x) {
              // tabs, and wide characters cut by the left edge, pad as spaces
              for(int k = (x < left_x) ? left_x : x; k < x + width && k < left_x + TextCols(); ++k) {
                screen.Put(1 + screen_row, gutter + (k - left_x), L" ", text_attr);
              }
            }
            else if(x - left_x + width <= TextCols()) {
              screen.Put(1 + screen_row, screen_x, std::wstring(1, line[i]), text_attr);
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
      const std::wstring pos = L" Ln " + std::to_wstring(cur_row + 1) + L", Col " + std::to_wstring(cur_col + 1) + L" ";
      if(!status.empty()) {
        screen.Put(rows - 1, 0, L" " + status, ATTR_REVERSE | ATTR_BOLD);
      }
      else {
        screen.Put(rows - 1, 0, L" Ctrl+S save   Ctrl+Q quit   F5 run   Ctrl+K delete line", ATTR_REVERSE);
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

    void InsertChar(wchar_t ch) {
      if(IsReadOnly(cur_row)) {
        status = L"read-only shell line";
        return;
      }
      std::wstring line = doc.GetLine(cur_row);
      line.insert(cur_col, 1, ch);
      doc.SetLine(cur_row, line);
      ++cur_col;
      dirty = true;
    }

    void DoNewline() {
      if(IsReadOnly(cur_row)) {
        // a new line above the read-only line: on the closing brace this is
        // "insert inside the function", the placement the user meant
        if(doc.InsertLine(cur_row, L"")) {
          cur_col = 0;
          dirty = true;
        }
        return;
      }

      const std::wstring line = doc.GetLine(cur_row);
      doc.SetLine(cur_row, line.substr(0, cur_col));
      doc.InsertLine(cur_row + 1, line.substr(cur_col));
      ++cur_row;
      cur_col = 0;
      dirty = true;
    }

    void DoBackspace() {
      if(cur_col > 0) {
        if(IsReadOnly(cur_row)) {
          status = L"read-only shell line";
          return;
        }
        std::wstring line = doc.GetLine(cur_row);
        line.erase(cur_col - 1, 1);
        doc.SetLine(cur_row, line);
        --cur_col;
        dirty = true;
        return;
      }

      // column zero: merge into the previous line when both sides are editable
      if(cur_row == 0 || IsReadOnly(cur_row) || IsReadOnly(cur_row - 1)) {
        return;
      }
      const std::wstring prev = doc.GetLine(cur_row - 1);
      const std::wstring line = doc.GetLine(cur_row);
      doc.SetLine(cur_row - 1, prev + line);
      doc.DeleteLine(cur_row);
      --cur_row;
      cur_col = prev.size();
      dirty = true;
    }

    void DoDelete() {
      const std::wstring line = doc.GetLine(cur_row);
      if(cur_col < line.size()) {
        if(IsReadOnly(cur_row)) {
          status = L"read-only shell line";
          return;
        }
        std::wstring edited = line;
        edited.erase(cur_col, 1);
        doc.SetLine(cur_row, edited);
        dirty = true;
        return;
      }

      // end of line: pull the next line up
      if(cur_row + 1 >= doc.Size() || IsReadOnly(cur_row) || IsReadOnly(cur_row + 1)) {
        return;
      }
      doc.SetLine(cur_row, line + doc.GetLine(cur_row + 1));
      doc.DeleteLine(cur_row + 1);
      dirty = true;
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

      std::wstring output;
      const bool ok = runner(output);

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
      if(!doc.DeleteLine(cur_row)) {   // refuses read-only lines itself
        status = L"read-only shell line";
        return;
      }
      dirty = true;
      ClampCursor();
      cur_col = 0;
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

        const Action action = Resolve(key);
        if(action != ACT_QUIT) {
          quit_armed = false;
        }

        switch(action) {
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

        case ACT_UP:
          if(cur_row > 0) {
            --cur_row;
            cur_col = want_col;
            ClampCursor();
          }
          break;

        case ACT_DOWN:
          if(cur_row + 1 < doc.Size()) {
            ++cur_row;
            cur_col = want_col;
            ClampCursor();
          }
          break;

        case ACT_LINE_START:
          cur_col = want_col = 0;
          break;

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
