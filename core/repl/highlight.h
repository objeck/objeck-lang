/***************************************************************************
 * Syntax coloring for the full-screen editor, driven by the compiler's own
 * Scanner (editor phase 4).
 *
 * Copyright (c) 2026, Randy Hollines
 * All rights reserved.
 ***************************************************************************/

#ifndef __TUI_HIGHLIGHT_H__
#define __TUI_HIGHLIGHT_H__

#include "editor.h"                 // Document
#include "screen.h"                 // Attr colors
#include "../compiler/scanner.h"    // the compiler Scanner, already linked into obi
#include <vector>
#include <string>

namespace Tui {

  // 0xFF is not a real attribute (Cell uses it as its repaint sentinel), so it
  // doubles here as "no syntax color for this cell" -- the Draw loop then falls
  // back to the line's default/gray attribute.
  static const unsigned char HL_NONE = 0xFF;

  // Colors the buffer using the language's real Scanner (linked from the
  // compiler) so the keyword set and number grammar can never drift from the
  // compiler itself. The whole document is scanned in one pass, which is what
  // lets the interior of a multi-line string be skipped over rather than
  // mis-read as code on the lines it spans.
  //
  // Colored: reserved words -> cyan, integer/float literals -> yellow, string
  // literals -> green. Deliberately left alone (see the class comment cases
  // below) rather than guessed at: comments, char literals, and multi-line
  // strings. Rescans on every document change; incremental caching is a later
  // optimization noted in EDITOR_DESIGN.md.
  class Highlighter {
  public:
    // Returns one attribute per character per line (HL_NONE where uncolored).
    std::vector<std::vector<unsigned char>> Scan(Document& doc) {
      const size_t line_count = doc.Size();

      std::vector<std::wstring> lines;
      lines.reserve(line_count);
      for(size_t i = 0; i < line_count; ++i) {
        lines.push_back(doc.GetLine(i));
      }

      std::vector<std::vector<unsigned char>> attrs(line_count);
      for(size_t i = 0; i < line_count; ++i) {
        attrs[i].assign(lines[i].size(), HL_NONE);
      }
      if(line_count == 0) {
        return attrs;
      }

      // rejoin with '\n' so each token's line/column maps straight back into
      // `lines` (the scanner counts newlines exactly as the document splits on)
      std::wstring source;
      for(size_t i = 0; i < line_count; ++i) {
        source += lines[i];
        source += L'\n';
      }

      try {
        Scanner scanner(L"repl://highlight", false, source);
        // a token is always at least one buffer character, so this bound can
        // never truncate real output yet guarantees we stop even if a future
        // scanner change forgot to emit end-of-stream
        const size_t limit = source.size() + 16;
        for(size_t n = 0; n < limit; ++n) {
          scanner.NextToken();
          Token* token = scanner.GetToken();
          if(!token) {
            break;
          }
          const ScannerTokenType type = token->GetType();
          if(type == TOKEN_END_OF_STREAM || type == TOKEN_NO_INPUT) {
            break;
          }
          Paint(lines, attrs, token);
        }
      }
      catch(...) {
        // a half-typed buffer must never take the editor down; whatever was
        // painted before the throw is fine to keep
      }

      return attrs;
    }

  private:
    // reserved words occupy one contiguous enum range (in a non-_SYSTEM build,
    // as obi is), plus the four word-operators that live in the factor range
    static bool IsKeyword(ScannerTokenType t) {
      if(t >= TOKEN_NATIVE_ID && t <= TOKEN_CRITICAL_ID) {
        return true;
      }
      switch(t) {
      case TOKEN_AND_ID:
      case TOKEN_OR_ID:
      case TOKEN_XOR_ID:
      case TOKEN_NOT_ID:
        return true;
      default:
        return false;
      }
    }

    static unsigned char AttrFor(ScannerTokenType t) {
      if(IsKeyword(t)) {
        // Bold as well as bright: keywords are the structure a dev scans for,
        // so they should be the strongest thing on the line.
        return (unsigned char)(ATTR_CYAN | ATTR_BOLD);
      }
      switch(t) {
      case TOKEN_INT_LIT:
      case TOKEN_FLOAT_LIT:
        return ATTR_YELLOW;
      case TOKEN_CHAR_STRING_LIT:
      case TOKEN_BAD_CHAR_STRING_LIT:
        return ATTR_GREEN;
      // TOKEN_CHAR_LIT is intentionally absent: several of the scanner's char
      // paths never set a line position, so its anchor is not trustworthy.
      default:
        return HL_NONE;
      }
    }

    static bool IsIdentChar(wchar_t c) {
      return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
             (c >= L'0' && c <= L'9') || c == L'_' || c == L'@';
    }

    // the character class the number scanner accepts (digits, radix markers,
    // hex digits, grouping '_', and the unsigned suffix)
    static bool IsNumberChar(wchar_t c) {
      return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F') ||
             c == L'.' || c == L'_' || c == L'x' || c == L'X' || c == L'u' || c == L'U';
    }

    static void Paint(const std::vector<std::wstring>& lines,
                      std::vector<std::vector<unsigned char>>& attrs, Token* token) {
      const ScannerTokenType type = token->GetType();
      const unsigned char attr = AttrFor(type);
      if(attr == HL_NONE) {
        return;
      }

      const int line_nbr = token->GetLineNumber();    // 1-based
      const int line_col = token->GetLinePosition();  // 1-based start column
      if(line_nbr < 1 || (size_t)line_nbr > lines.size() || line_col < 1) {
        return;
      }
      const size_t li = (size_t)(line_nbr - 1);
      const size_t start = (size_t)(line_col - 1);
      const std::wstring& line = lines[li];
      if(start >= line.size()) {
        return;
      }

      if(type == TOKEN_CHAR_STRING_LIT || type == TOKEN_BAD_CHAR_STRING_LIT) {
        // Anchor on the opening quote. The scanner derives a string's column as
        // line_pos - length - 2 (identifiers use - length - 1), and that
        // arithmetic lands a cell or two off in practice, so demanding the quote
        // exactly at the reported column silently skipped every string.
        //
        // Search a SMALL window instead of trusting the number outright. A
        // multi-line string is reported at its closing line with a meaningless
        // column and still finds no quote nearby, so it stays uncolored rather
        // than mis-painted -- the property that guard was protecting.
        size_t quote = std::wstring::npos;
        const size_t lo = (start > 2) ? start - 2 : 0;
        const size_t hi = (start + 2 < line.size()) ? start + 2 : line.size() - 1;
        for(size_t i = lo; i <= hi; ++i) {
          if(line[i] == L'"') {
            quote = i;
            break;
          }
        }
        if(quote == std::wstring::npos) {
          return;
        }
        PaintString(line, attrs[li], quote, attr);
      }
      else {
        // keyword or number: a run on this one line. Verify the anchor so a
        // stray position can never paint the wrong cells.
        const bool numeric = (type == TOKEN_INT_LIT || type == TOKEN_FLOAT_LIT);
        if(numeric ? !IsNumberChar(line[start]) : !IsIdentChar(line[start])) {
          return;
        }
        size_t end = start;
        while(end < line.size() && (numeric ? IsNumberChar(line[end]) : IsIdentChar(line[end]))) {
          ++end;
        }
        for(size_t i = start; i < end; ++i) {
          attrs[li][i] = attr;
        }
      }
    }

    // Paints a single-line string from its opening quote through the matching
    // close, treating the char after a '\' as literal exactly as the scanner
    // does (so an escaped quote never ends the run early).
    static void PaintString(const std::wstring& line, std::vector<unsigned char>& row,
                            size_t start, unsigned char attr) {
      size_t i = start;
      row[i] = attr;   // opening quote
      ++i;
      while(i < line.size()) {
        const wchar_t c = line[i];
        row[i] = attr;
        if(c == L'\\') {
          ++i;
          if(i < line.size()) {
            row[i] = attr;
            ++i;
          }
          continue;
        }
        if(c == L'"') {
          return;   // closing quote painted
        }
        ++i;
      }
    }
  };
}

#endif
