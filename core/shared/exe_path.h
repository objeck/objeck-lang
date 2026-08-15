/***************************************************************************
 * Directory of the running executable.
 *
 * Resolved from the OS rather than from argv[0] or PATH, so a tool launched
 * through PATH still finds the sibling binaries of the tree it belongs to and
 * never operates on an attacker-chosen directory. obi (F5 locating obc/obr)
 * and obu (locating the install root) both need exactly this, and both used
 * to carry their own copy -- so the MAX_PATH guard, the macOS two-call
 * _NSGetExecutablePath convention and the /proc/self/exe symlink live here
 * once instead of drifting apart.
 *
 * Header-only and dependency-free on purpose: obu links neither zlib nor the
 * rest of sys.h, so this cannot live there.
 *
 * Copyright (c) 2026, Randy Hollines
 * All rights reserved.
 ***************************************************************************/

#ifndef __EXE_PATH_H__
#define __EXE_PATH_H__

#include <filesystem>
#include <string>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <cstdint>
#endif

// Empty path on failure; every caller must treat that as "cannot locate".
inline std::filesystem::path ExecutableDir()
{
#if defined(_WIN32)
  wchar_t buffer[MAX_PATH];
  const DWORD len = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
  if(len == 0 || len == MAX_PATH) {
    return std::filesystem::path();
  }
  return std::filesystem::path(std::wstring(buffer, len)).parent_path();
#elif defined(__APPLE__)
  char buffer[4096];
  uint32_t size = sizeof(buffer);
  if(_NSGetExecutablePath(buffer, &size) != 0) {
    return std::filesystem::path();
  }
  std::error_code ec;
  const std::filesystem::path resolved = std::filesystem::canonical(std::filesystem::path(buffer), ec);
  return (ec ? std::filesystem::path(buffer) : resolved).parent_path();
#else
  std::error_code ec;
  const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", ec);
  if(ec) {
    return std::filesystem::path();
  }
  return self.parent_path();
#endif
}

#endif
