/***************************************************************************
 * Damage-diffed screen buffer for the full-screen editor.
 *
 * Copyright (c) 2026, Randy Hollines
 * All rights reserved.
 ***************************************************************************/

#ifndef __TUI_SCREEN_H__
#define __TUI_SCREEN_H__

#include "term.h"
#include <vector>
#include <string>

// Direct writes flicker and feel laggy over SSH. The editor draws into a back
// buffer of cells; Flush() diffs it against what is already on the terminal
// and emits only the runs that changed, with one cursor move per run. Resizing
// forgets the front buffer, forcing the one legitimate full repaint.

namespace Tui {

  // Columns a character occupies: 2 for East Asian wide and emoji, 0 for
  // combining marks, else 1. Cursor math is wrong for non-ASCII text without
  // this, and Windows has no wcwidth(), so the ranges are carried here.
  inline int CharWidth(wchar_t wch) {
    const unsigned int cp = (unsigned int)wch;

    // combining marks and zero-width characters
    if((cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x1AB0 && cp <= 0x1AFF) ||
       (cp >= 0x1DC0 && cp <= 0x1DFF) || (cp >= 0x20D0 && cp <= 0x20FF) ||
       (cp >= 0xFE20 && cp <= 0xFE2F) || (cp >= 0x200B && cp <= 0x200F) ||
       cp == 0xFEFF) {
      return 0;
    }

    // East Asian wide and fullwidth, plus common emoji blocks
    if((cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0x303E) ||
       (cp >= 0x3041 && cp <= 0x33FF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
       (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xA000 && cp <= 0xA4CF) ||
       (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
       (cp >= 0xFE30 && cp <= 0xFE4F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
       (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x1F300 && cp <= 0x1F64F) ||
       (cp >= 0x1F900 && cp <= 0x1FAFF) || (cp >= 0x20000 && cp <= 0x3FFFD)) {
      return 2;
    }

    return 1;
  }

  // display attributes; a fg color index in the low bits plus style flags
  enum Attr {
    ATTR_DEFAULT = 0,
    ATTR_RED = 1,
    ATTR_GREEN = 2,
    ATTR_YELLOW = 3,
    ATTR_BLUE = 4,
    ATTR_CYAN = 5,
    ATTR_GRAY = 6,
    ATTR_COLOR_MASK = 0x0F,
    ATTR_BOLD = 0x10,
    ATTR_REVERSE = 0x20
  };

  struct Cell {
    wchar_t ch;
    unsigned char attr;

    // 0xFF never occurs as a real attribute, so a front buffer full of it
    // compares unequal everywhere and forces a full repaint
    Cell() : ch(L' '), attr(0xFF) {
    }

    bool operator==(const Cell& other) const {
      return ch == other.ch && attr == other.attr;
    }
  };

  class Screen {
    int rows;
    int cols;
    std::vector<Cell> front;   // what the terminal is showing
    std::vector<Cell> back;    // what the next Flush should show

    Cell& At(std::vector<Cell>& grid, int row, int col) {
      return grid[(size_t)row * cols + col];
    }

    static std::wstring AttrSequence(unsigned char attr) {
      std::wstring seq = L"\x1b[0";
      if(attr & ATTR_BOLD) {
        seq += L";1";
      }
      if(attr & ATTR_REVERSE) {
        seq += L";7";
      }
      switch(attr & ATTR_COLOR_MASK) {
      // BRIGHT codes (91-96), not the dim 31-36 set. The basic colours render
      // as muddy low-contrast tones over a dark background -- syntax painted
      // with them reads as faded rather than as structure. Gray stays 90: it
      // marks the read-only frame and SHOULD recede.
      case ATTR_RED: seq += L";91"; break;
      case ATTR_GREEN: seq += L";92"; break;
      case ATTR_YELLOW: seq += L";93"; break;
      case ATTR_BLUE: seq += L";94"; break;
      case ATTR_CYAN: seq += L";96"; break;
      case ATTR_GRAY: seq += L";90"; break;
      }
      return seq + L"m";
    }

  public:
    Screen() : rows(0), cols(0) {
    }

    int Rows() const {
      return rows;
    }

    int Cols() const {
      return cols;
    }

    // Forget what is believed to be on screen, so the next Flush repaints
    // every cell. Needed after anything else writes to the terminal behind
    // the diff's back -- a run whose program printed straight to the console
    // leaves `front` describing an editor that is no longer there, and the
    // diff then emits almost nothing, which reads as a frozen UI.
    void Invalidate() {
      front.assign((size_t)rows * cols, Cell());   // same sentinel as Resize
    }

    void Resize(int new_rows, int new_cols) {
      rows = new_rows;
      cols = new_cols;
      Invalidate();                                // sentinel: repaint all
      back.assign((size_t)rows * cols, Cell());
      Clear();
    }

    void Clear(unsigned char attr = ATTR_DEFAULT) {
      Cell blank;
      blank.ch = L' ';
      blank.attr = attr;
      back.assign((size_t)rows * cols, blank);
    }

    // Writes text into the back buffer, clipped to the row. A wide character
    // occupies its cell plus a continuation cell marked with ch=0; zero-width
    // characters are dropped rather than mispositioning everything after them.
    // Returns the column after the last cell written.
    int Put(int row, int col, const std::wstring& text, unsigned char attr = ATTR_DEFAULT) {
      if(row < 0 || row >= rows) {
        return col;
      }

      for(const wchar_t wch : text) {
        const int width = CharWidth(wch);
        if(width == 0) {
          continue;
        }
        if(col + width > cols) {
          break;
        }

        Cell& cell = At(back, row, col);
        cell.ch = wch;
        cell.attr = attr;
        if(width == 2) {
          Cell& cont = At(back, row, col + 1);
          cont.ch = 0;
          cont.attr = attr;
        }
        col += width;
      }
      return col;
    }

    // fill a horizontal run with one character; handy for bars and rules
    void Fill(int row, int col, int count, wchar_t wch, unsigned char attr = ATTR_DEFAULT) {
      for(int i = 0; i < count && col + i < cols; ++i) {
        if(row < 0 || row >= rows || col + i < 0) {
          continue;
        }
        Cell& cell = At(back, row, col + i);
        cell.ch = wch;
        cell.attr = attr;
      }
    }

    // Emits the difference between the buffers: one cursor move per changed
    // run, attribute changes only when they differ from the previous cell.
    void Flush(Term& term) {
      std::wstring out;
      out.reserve(256);

      int last_attr = -1;
      for(int row = 0; row < rows; ++row) {
        int col = 0;
        while(col < cols) {
          if(At(back, row, col) == At(front, row, col)) {
            ++col;
            continue;
          }

          // a run of changed cells starts here
          out += L"\x1b[" + std::to_wstring(row + 1) + L";" + std::to_wstring(col + 1) + L"H";
          while(col < cols && !(At(back, row, col) == At(front, row, col))) {
            const Cell& cell = At(back, row, col);
            if(cell.ch != 0) {   // continuation cells are covered by the wide char before them
              if(cell.attr != last_attr) {
                out += AttrSequence(cell.attr);
                last_attr = cell.attr;
              }
              out += cell.ch;
            }
            At(front, row, col) = cell;
            ++col;
          }
        }
      }

      if(!out.empty()) {
        out += L"\x1b[0m";
        term.Write(out);
        // the wstring is sent in one Write so the terminal never shows a
        // half-painted frame
      }
    }
  };
}

#endif
