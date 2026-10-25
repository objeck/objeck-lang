/***************************************************************************
 * SetObjEnv.cmd, the script the launcher's "Command Prompt" button runs with
 * cmd /k: it puts the install's bin on PATH and its lib in OBJECK_LIB_PATH.
 *
 * cmd reads a batch file in the console's code page (437, 850, ...), line by
 * line as it runs it. The script used to be written as UTF-8 and read that
 * way, so an install directory with a non-ASCII name -- 'C:\Users\Zoë\...',
 * a CJK folder -- set both variables to mojibake, and nothing ran from that
 * prompt. The script now switches the console to UTF-8 (chcp 65001) for the
 * lines that hold paths, and back to the code page it found before handing
 * the prompt over; the variables keep their values, which cmd holds as
 * UTF-16. Values are quoted ("set "NAME=value"") so a path with '&' or ')'
 * cannot end the command.
 *
 * Header-only so test_set_obj_env.py can build and run the same code.
 *
 * Copyright (c) 2026, Randy Hollines
 * All rights reserved.
 ***************************************************************************/

#ifndef __SET_OBJ_ENV_H__
#define __SET_OBJ_ENV_H__

#include <windows.h>
#include <string>

/**
 * The script's text. 'banner' is the version line it echoes;
 * 'application_path' is the launcher's own directory (the install's app
 * directory, beside bin and lib).
 */
inline std::wstring SetObjEnvScript(const std::wstring& banner, const std::wstring& application_path)
{
  std::wstring script;
  script += L"@echo off\r\n";
  // the code page this console started in ("Active code page: 437", with a
  // trailing '.' in some languages), restored below
  script += L"for /f \"tokens=2 delims=:.\" %%c in ('chcp') do set \"OBJK_PREVIOUS_CP=%%c\"\r\n";
  script += L"chcp 65001 >nul\r\n";
  script += L"echo =========================================\r\n";
  script += L"echo " + banner + L"\r\n";
  script += L"echo Copyright(c) 2026, Randy Hollines\r\n";
  script += L"echo =========================================\r\n";
  // SDL2 ships its DLLs in bin, beside the tools that load them, which is
  // where Windows resolves a loaded library's imports from. lib\sdl holds only
  // the bundled font, so it does not belong on PATH.
  script += L"set \"PATH=%PATH%;" + application_path + L"\\..\\bin\"\r\n";
  script += L"set \"OBJECK_LIB_PATH=" + application_path + L"\\..\\lib\"\r\n";
  script += L"if defined OBJK_PREVIOUS_CP chcp %OBJK_PREVIOUS_CP: =% >nul\r\n";
  script += L"set OBJK_PREVIOUS_CP=\r\n";
  script += L"title Objeck Prompt\r\n";
  script += L"cd %HOMEDRIVE%\\%HOMEPATH%\\Documents\r\n";
  script += L"%HOMEDRIVE%\r\n";
  return script;
}

/**
 * Writes the script to 'file' as UTF-8 (no byte-order mark: cmd would run it
 * as part of the first command). False if it could not be written.
 */
inline bool WriteSetObjEnv(const std::wstring& file, const std::wstring& banner, const std::wstring& application_path)
{
  const std::wstring script = SetObjEnvScript(banner, application_path);
  const int size = WideCharToMultiByte(CP_UTF8, 0, script.data(), (int)script.size(), nullptr, 0, nullptr, nullptr);
  if(size <= 0) {
    return false;
  }
  std::string bytes((size_t)size, '\0');
  WideCharToMultiByte(CP_UTF8, 0, script.data(), (int)script.size(), &bytes[0], size, nullptr, nullptr);

  HANDLE handle = CreateFileW(file.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
  if(handle == INVALID_HANDLE_VALUE) {
    return false;
  }
  DWORD written = 0;
  const BOOL ok = WriteFile(handle, bytes.data(), (DWORD)bytes.size(), &written, nullptr);
  CloseHandle(handle);
  return ok && written == (DWORD)bytes.size();
}

#endif
