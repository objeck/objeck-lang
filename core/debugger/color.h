/***************************************************************************
* ANSI color support for the Objeck debugger
*
* Copyright (c) 2026, Randy Hollines
* All rights reserved.
*
* See LICENSE file for full copyright notice.
***************************************************************************/

#ifndef __COLOR_H__
#define __COLOR_H__

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <cstdlib>
#endif

namespace Runtime {
  // ANSI escape codes
  static const wchar_t* CLR_RESET  = L"\033[0m";
  static const wchar_t* CLR_BOLD   = L"\033[1m";
  static const wchar_t* CLR_RED    = L"\033[31m";
  static const wchar_t* CLR_GREEN  = L"\033[32m";
  static const wchar_t* CLR_YELLOW = L"\033[33m";
  static const wchar_t* CLR_BLUE   = L"\033[34m";
  static const wchar_t* CLR_CYAN   = L"\033[36m";
  static const wchar_t* CLR_GRAY   = L"\033[90m";

  static bool color_enabled = false;

  static void ColorInit() {
#ifdef _WIN32
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if(hOut != INVALID_HANDLE_VALUE) {
      DWORD dwMode = 0;
      if(GetConsoleMode(hOut, &dwMode)) {
        if(dwMode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) {
          color_enabled = true;
        }
        else {
          dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
          color_enabled = (SetConsoleMode(hOut, dwMode) != 0);
        }
      }
    }
#else
    if(isatty(STDOUT_FILENO)) {
      const char* term = getenv("TERM");
      color_enabled = !(term && std::string(term) == "dumb");
    }
#endif
  }

  // Returns the color code if color is enabled, empty string otherwise
  static const wchar_t* C(const wchar_t* code) {
    return color_enabled ? code : L"";
  }
}

#endif
