/***************************************************************************
 * Raw terminal access for the full-screen editor: raw mode, size, cursor,
 * and decoded key events.
 *
 * Copyright (c) 2026, Randy Hollines
 * All rights reserved.
 ***************************************************************************/

#ifndef __TUI_TERM_H__
#define __TUI_TERM_H__

#include <string>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <cerrno>
#endif

// This is the only editor unit that knows the platform. Everything above it
// consumes decoded Key events and writes wide strings, so the Windows console
// API and POSIX termios/escape-sequence handling never leak upward.
//
// Output relies on ANSI escapes on every platform; on Windows that means
// enabling ENABLE_VIRTUAL_TERMINAL_PROCESSING, the same approach the debugger
// uses in color.h. A console too old for VT reports EnterRaw() failure and the
// caller falls back to line mode rather than painting garbage.

namespace Tui {

  enum KeyCode {
    KEY_NONE = 0,   // timeout tick; poll again
    KEY_CHAR,       // printable character in ch
    KEY_ENTER,
    KEY_TAB,
    KEY_BACKSPACE,
    KEY_ESC,
    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_HOME,
    KEY_END,
    KEY_PGUP,
    KEY_PGDN,
    KEY_INSERT,
    KEY_DELETE,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
    KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    KEY_RESIZE      // the terminal changed size; re-query and repaint
  };

  struct Key {
    KeyCode code;
    wchar_t ch;
    bool ctrl;
    bool alt;
    bool shift;

    Key() : code(KEY_NONE), ch(0), ctrl(false), alt(false), shift(false) {
    }
  };

#ifndef _WIN32
  // SIGWINCH only sets a flag; ReadKey turns it into a KEY_RESIZE event so the
  // caller never deals with signals
  inline volatile sig_atomic_t& ResizeFlag() {
    static volatile sig_atomic_t flag = 0;
    return flag;
  }

  inline void OnSigwinch(int) {
    ResizeFlag() = 1;
  }
#endif

  class Term {
    bool raw_active;

#ifdef _WIN32
    HANDLE in_handle;
    HANDLE out_handle;
    DWORD saved_in_mode;
    DWORD saved_out_mode;
    // a surrogate pair arrives as one UTF-32 character but leaves as two
    // UTF-16 events
    wchar_t pending_low;
#else
    struct termios saved_termios;
    void (*saved_winch)(int);
#endif

  public:
    Term() : raw_active(false) {
#ifdef _WIN32
      in_handle = GetStdHandle(STD_INPUT_HANDLE);
      out_handle = GetStdHandle(STD_OUTPUT_HANDLE);
      saved_in_mode = saved_out_mode = 0;
      pending_low = 0;
#else
      saved_winch = nullptr;
#endif
    }

    ~Term() {
      ExitRaw();
    }

    // full-screen mode must never engage for a piped or scripted session --
    // `obi --file` and `--inline` run headless in CI
    static bool IsTty() {
#ifdef _WIN32
      return _isatty(_fileno(stdin)) && _isatty(_fileno(stdout));
#else
      return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
#endif
    }

    bool EnterRaw() {
      if(raw_active) {
        return true;
      }
      if(!IsTty()) {
        return false;
      }

#ifdef _WIN32
      if(!GetConsoleMode(in_handle, &saved_in_mode) || !GetConsoleMode(out_handle, &saved_out_mode)) {
        return false;
      }

      // window events deliver resizes; line/echo/processed input all off so
      // keys arrive one event at a time (processed off also frees Ctrl+C)
      DWORD in_mode = saved_in_mode;
      in_mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT | ENABLE_QUICK_EDIT_MODE);
      in_mode |= ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS;
      if(!SetConsoleMode(in_handle, in_mode)) {
        return false;
      }

      DWORD out_mode = saved_out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING | DISABLE_NEWLINE_AUTO_RETURN;
      if(!SetConsoleMode(out_handle, out_mode)) {
        // DISABLE_NEWLINE_AUTO_RETURN is not supported everywhere VT is
        out_mode = saved_out_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        if(!SetConsoleMode(out_handle, out_mode)) {
          SetConsoleMode(in_handle, saved_in_mode);
          return false;
        }
      }
#else
      if(tcgetattr(STDIN_FILENO, &saved_termios) != 0) {
        return false;
      }

      struct termios raw = saved_termios;
      raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
      raw.c_oflag &= ~(OPOST);
      raw.c_cflag |= CS8;
      raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
      // 100ms read tick: lets an ESC that arrives alone be told apart from one
      // that starts a sequence, and lets the resize flag surface promptly
      raw.c_cc[VMIN] = 0;
      raw.c_cc[VTIME] = 1;
      if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
        return false;
      }

      saved_winch = signal(SIGWINCH, OnSigwinch);
#endif

      raw_active = true;
      return true;
    }

    void ExitRaw() {
      if(!raw_active) {
        return;
      }

      // leave the screen usable: cursor on, attributes reset
      Write(L"\x1b[0m\x1b[?25h");

#ifdef _WIN32
      SetConsoleMode(in_handle, saved_in_mode);
      SetConsoleMode(out_handle, saved_out_mode);
#else
      tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_termios);
      if(saved_winch) {
        signal(SIGWINCH, saved_winch);
      }
#endif
      raw_active = false;
    }

    bool Size(int& rows, int& cols) {
#ifdef _WIN32
      CONSOLE_SCREEN_BUFFER_INFO info;
      if(!GetConsoleScreenBufferInfo(out_handle, &info)) {
        return false;
      }
      rows = info.srWindow.Bottom - info.srWindow.Top + 1;
      cols = info.srWindow.Right - info.srWindow.Left + 1;
      return true;
#else
      struct winsize ws;
      if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0 || ws.ws_col == 0) {
        return false;
      }
      rows = ws.ws_row;
      cols = ws.ws_col;
      return true;
#endif
    }

    void Write(const std::wstring& text) {
#ifdef _WIN32
      DWORD written;
      WriteConsoleW(out_handle, text.c_str(), (DWORD)text.size(), &written, nullptr);
#else
      // wchar_t holds a code point on POSIX; narrow to UTF-8 by hand so the
      // C locale cannot mangle it (see the utf8 locale defect this repo hit)
      std::string bytes;
      bytes.reserve(text.size() * 2);
      for(const wchar_t wc : text) {
        const unsigned int cp = (unsigned int)wc;
        if(cp < 0x80) {
          bytes += (char)cp;
        }
        else if(cp < 0x800) {
          bytes += (char)(0xC0 | (cp >> 6));
          bytes += (char)(0x80 | (cp & 0x3F));
        }
        else if(cp < 0x10000) {
          bytes += (char)(0xE0 | (cp >> 12));
          bytes += (char)(0x80 | ((cp >> 6) & 0x3F));
          bytes += (char)(0x80 | (cp & 0x3F));
        }
        else {
          bytes += (char)(0xF0 | (cp >> 18));
          bytes += (char)(0x80 | ((cp >> 12) & 0x3F));
          bytes += (char)(0x80 | ((cp >> 6) & 0x3F));
          bytes += (char)(0x80 | (cp & 0x3F));
        }
      }
      ssize_t ignored = write(STDOUT_FILENO, bytes.data(), bytes.size());
      (void)ignored;
#endif
    }

    // 1-based, matching the escape convention
    void MoveTo(int row, int col) {
      Write(L"\x1b[" + std::to_wstring(row) + L";" + std::to_wstring(col) + L"H");
    }

    void HideCursor() {
      Write(L"\x1b[?25l");
    }

    void ShowCursor() {
      Write(L"\x1b[?25h");
    }

    void Clear() {
      Write(L"\x1b[2J\x1b[H");
    }

    // Blocks until a key, a resize, or a ~100ms tick (KEY_NONE). Ticks let the
    // caller stay responsive without a second thread.
    Key ReadKey() {
      Key key;

#ifdef _WIN32
      // deliver the second half of a surrogate pair before reading again
      if(pending_low) {
        key.code = KEY_CHAR;
        key.ch = pending_low;
        pending_low = 0;
        return key;
      }

      const DWORD waited = WaitForSingleObject(in_handle, 100);
      if(waited != WAIT_OBJECT_0) {
        return key;
      }

      INPUT_RECORD record;
      DWORD count = 0;
      if(!ReadConsoleInputW(in_handle, &record, 1, &count) || count == 0) {
        return key;
      }

      if(record.EventType == WINDOW_BUFFER_SIZE_EVENT) {
        key.code = KEY_RESIZE;
        return key;
      }

      if(record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown) {
        return key;
      }

      const KEY_EVENT_RECORD& ev = record.Event.KeyEvent;
      key.ctrl = (ev.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
      key.alt = (ev.dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;
      key.shift = (ev.dwControlKeyState & SHIFT_PRESSED) != 0;

      switch(ev.wVirtualKeyCode) {
      case VK_UP: key.code = KEY_UP; return key;
      case VK_DOWN: key.code = KEY_DOWN; return key;
      case VK_LEFT: key.code = KEY_LEFT; return key;
      case VK_RIGHT: key.code = KEY_RIGHT; return key;
      case VK_HOME: key.code = KEY_HOME; return key;
      case VK_END: key.code = KEY_END; return key;
      case VK_PRIOR: key.code = KEY_PGUP; return key;
      case VK_NEXT: key.code = KEY_PGDN; return key;
      case VK_INSERT: key.code = KEY_INSERT; return key;
      case VK_DELETE: key.code = KEY_DELETE; return key;
      case VK_RETURN: key.code = KEY_ENTER; return key;
      case VK_TAB: key.code = KEY_TAB; return key;
      case VK_BACK: key.code = KEY_BACKSPACE; return key;
      case VK_ESCAPE: key.code = KEY_ESC; return key;
      default:
        if(ev.wVirtualKeyCode >= VK_F1 && ev.wVirtualKeyCode <= VK_F12) {
          key.code = (KeyCode)(KEY_F1 + (ev.wVirtualKeyCode - VK_F1));
          return key;
        }
        break;
      }

      wchar_t ch = ev.uChar.UnicodeChar;
      if(ch == 0) {
        return key;   // a bare modifier press
      }

      // Ctrl+letter arrives as 0x01..0x1A; report the letter with the flag
      if(key.ctrl && ch < 32) {
        ch = (wchar_t)(ch + L'a' - 1);
      }

      // the high half of a surrogate pair: hold the low half for the next call
      if(ch >= 0xD800 && ch <= 0xDBFF) {
        INPUT_RECORD low;
        DWORD low_count = 0;
        if(ReadConsoleInputW(in_handle, &low, 1, &low_count) && low_count == 1 &&
           low.EventType == KEY_EVENT && low.Event.KeyEvent.bKeyDown) {
          pending_low = low.Event.KeyEvent.uChar.UnicodeChar;
        }
      }

      key.code = KEY_CHAR;
      key.ch = ch;
      return key;
#else
      if(ResizeFlag()) {
        ResizeFlag() = 0;
        key.code = KEY_RESIZE;
        return key;
      }

      unsigned char byte;
      const ssize_t got = read(STDIN_FILENO, &byte, 1);
      if(got != 1) {
        // interrupted by SIGWINCH, or the 100ms tick expired
        if(ResizeFlag()) {
          ResizeFlag() = 0;
          key.code = KEY_RESIZE;
        }
        return key;
      }

      if(byte == 0x1b) {
        return ReadEscape();
      }

      if(byte == L'\r' || byte == L'\n') {
        key.code = KEY_ENTER;
        return key;
      }
      if(byte == L'\t') {
        key.code = KEY_TAB;
        return key;
      }
      if(byte == 127 || byte == 8) {
        key.code = KEY_BACKSPACE;
        return key;
      }

      // Ctrl+letter arrives as 0x01..0x1A
      if(byte < 32) {
        key.code = KEY_CHAR;
        key.ctrl = true;
        key.ch = (wchar_t)(byte + L'a' - 1);
        return key;
      }

      key.code = KEY_CHAR;
      key.ch = ReadUtf8(byte);
      return key;
#endif
    }

#ifndef _WIN32
  private:
    // assemble a code point from a UTF-8 lead byte plus its continuations
    wchar_t ReadUtf8(unsigned char lead) {
      int extra;
      unsigned int cp;
      if(lead < 0xC0) {
        return (wchar_t)lead;
      }
      else if(lead < 0xE0) {
        extra = 1;
        cp = lead & 0x1F;
      }
      else if(lead < 0xF0) {
        extra = 2;
        cp = lead & 0x0F;
      }
      else {
        extra = 3;
        cp = lead & 0x07;
      }

      for(int i = 0; i < extra; ++i) {
        unsigned char cont;
        if(read(STDIN_FILENO, &cont, 1) != 1 || (cont & 0xC0) != 0x80) {
          return L'?';
        }
        cp = (cp << 6) | (cont & 0x3F);
      }
      return (wchar_t)cp;
    }

    // Called with ESC already consumed. A follower that never arrives within
    // the read tick means the user pressed ESC itself.
    Key ReadEscape() {
      Key key;

      unsigned char first;
      if(read(STDIN_FILENO, &first, 1) != 1) {
        key.code = KEY_ESC;
        return key;
      }

      // ESC + printable is Alt+key
      if(first != '[' && first != 'O') {
        key.code = KEY_CHAR;
        key.alt = true;
        key.ch = (first < 32) ? (wchar_t)(first + L'a' - 1) : ReadUtf8(first);
        key.ctrl = first < 32;
        return key;
      }

      // collect parameter bytes up to the final byte (0x40..0x7E)
      std::string params;
      unsigned char final_byte = 0;
      for(int i = 0; i < 16; ++i) {
        unsigned char b;
        if(read(STDIN_FILENO, &b, 1) != 1) {
          key.code = KEY_ESC;
          return key;
        }
        if(b >= 0x40 && b <= 0x7E) {
          final_byte = b;
          break;
        }
        params += (char)b;
      }

      // 'ESC [ 1 ; m X' encodes modifiers as m-1: 1=shift, 2=alt, 4=ctrl
      const size_t semi = params.find(';');
      if(semi != std::string::npos) {
        const int mods = atoi(params.c_str() + semi + 1) - 1;
        key.shift = (mods & 1) != 0;
        key.alt = (mods & 2) != 0;
        key.ctrl = (mods & 4) != 0;
        params.resize(semi);
      }

      switch(final_byte) {
      case 'A': key.code = KEY_UP; return key;
      case 'B': key.code = KEY_DOWN; return key;
      case 'C': key.code = KEY_RIGHT; return key;
      case 'D': key.code = KEY_LEFT; return key;
      case 'H': key.code = KEY_HOME; return key;
      case 'F': key.code = KEY_END; return key;
      case 'P': key.code = KEY_F1; return key;
      case 'Q': key.code = KEY_F2; return key;
      case 'R': key.code = KEY_F3; return key;
      case 'S': key.code = KEY_F4; return key;
      case '~': {
        const int n = atoi(params.c_str());
        switch(n) {
        case 1: case 7: key.code = KEY_HOME; return key;
        case 2: key.code = KEY_INSERT; return key;
        case 3: key.code = KEY_DELETE; return key;
        case 4: case 8: key.code = KEY_END; return key;
        case 5: key.code = KEY_PGUP; return key;
        case 6: key.code = KEY_PGDN; return key;
        case 11: case 12: case 13: case 14:
          key.code = (KeyCode)(KEY_F1 + (n - 11));
          return key;
        case 15: key.code = KEY_F5; return key;
        case 17: case 18: case 19: case 20: case 21:
          key.code = (KeyCode)(KEY_F6 + (n - 17));
          return key;
        case 23: key.code = KEY_F11; return key;
        case 24: key.code = KEY_F12; return key;
        }
        break;
      }
      }

      key.code = KEY_ESC;
      return key;
    }
#endif
  };
}

#endif
