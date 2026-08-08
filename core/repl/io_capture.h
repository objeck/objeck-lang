/***************************************************************************
 * Captures process stdout/stderr while the editor's run pane executes the
 * program in-process (editor phase 3).
 *
 * Copyright (c) 2026, Randy Hollines
 * All rights reserved.
 ***************************************************************************/

#ifndef __TUI_IO_CAPTURE_H__
#define __TUI_IO_CAPTURE_H__

#include <string>
#include <cstdio>
#include <cwchar>
#include <iostream>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <windows.h>
#else
#include <unistd.h>
#include <cstdlib>
#endif

// The VM runs in-process, so a program's output would land straight on the
// editor's screen. Redirection happens at the file-descriptor level -- the
// same approach obd's DAP mode uses -- because that is the one place both C
// stdio and the C++ wide streams converge. stdin is pointed at the null
// device for the duration, so a program that reads input sees EOF instead of
// stealing raw key bytes from the editor.

namespace Tui {

  class IoCapture {
    bool active;
    int saved_out;
    int saved_err;
    int saved_in;
    std::string path;
    FILE* sink;

    static void FlushAll() {
      std::wcout.flush();
      std::wcerr.flush();
      fflush(stdout);
      fflush(stderr);
    }

  public:
    IoCapture() : active(false), saved_out(-1), saved_err(-1), saved_in(-1), sink(nullptr) {
    }

    ~IoCapture() {
      std::wstring dropped;
      Finish(dropped);
    }

    bool Start() {
      if(active) {
        return true;
      }

      FlushAll();

#ifdef _WIN32
      char temp_dir[MAX_PATH];
      char temp_name[MAX_PATH];
      if(!GetTempPathA(MAX_PATH, temp_dir) || !GetTempFileNameA(temp_dir, "obi", 0, temp_name)) {
        return false;
      }
      path = temp_name;
      if(fopen_s(&sink, path.c_str(), "w+b") != 0 || !sink) {
        return false;
      }

      saved_out = _dup(1);
      saved_err = _dup(2);
      saved_in = _dup(0);
      if(_dup2(_fileno(sink), 1) != 0 || _dup2(_fileno(sink), 2) != 0) {
        Restore();
        return false;
      }
      FILE* null_in = nullptr;
      fopen_s(&null_in, "NUL", "r");
      if(null_in) {
        _dup2(_fileno(null_in), 0);
        fclose(null_in);
      }
#else
      char temp_name[] = "/tmp/obi-run-XXXXXX";
      const int fd = mkstemp(temp_name);
      if(fd < 0) {
        return false;
      }
      path = temp_name;
      sink = fdopen(fd, "w+b");
      if(!sink) {
        close(fd);
        return false;
      }

      saved_out = dup(1);
      saved_err = dup(2);
      saved_in = dup(0);
      if(dup2(fileno(sink), 1) < 0 || dup2(fileno(sink), 2) < 0) {
        Restore();
        return false;
      }
      FILE* null_in = fopen("/dev/null", "r");
      if(null_in) {
        dup2(fileno(null_in), 0);
        fclose(null_in);
      }
#endif

      active = true;
      return true;
    }

    // restores the descriptors and returns everything the program wrote
    bool Finish(std::wstring& out) {
      if(!active) {
        return false;
      }
      FlushAll();
      Restore();
      active = false;

      // read the sink back as UTF-8 bytes
      std::string bytes;
      if(sink) {
        fseek(sink, 0, SEEK_END);
        const long size = ftell(sink);
        if(size > 0) {
          bytes.resize((size_t)size);
          fseek(sink, 0, SEEK_SET);
          const size_t got = fread(&bytes[0], 1, (size_t)size, sink);
          bytes.resize(got);
        }
        fclose(sink);
        sink = nullptr;
        remove(path.c_str());
      }

      // decode UTF-8 by hand -- the locale must not get a say (this repo has
      // been bitten by locale-dependent conversions before)
      out.clear();
      out.reserve(bytes.size());
      for(size_t i = 0; i < bytes.size();) {
        const unsigned char lead = (unsigned char)bytes[i];
        unsigned int cp;
        int extra;
        if(lead < 0x80) {
          cp = lead;
          extra = 0;
        }
        else if(lead >= 0xF0) {
          cp = lead & 0x07;
          extra = 3;
        }
        else if(lead >= 0xE0) {
          cp = lead & 0x0F;
          extra = 2;
        }
        else if(lead >= 0xC0) {
          cp = lead & 0x1F;
          extra = 1;
        }
        else {   // stray continuation byte
          ++i;
          continue;
        }

        if(i + extra >= bytes.size() + (extra ? 0 : 1)) {
          break;
        }
        bool valid = true;
        for(int k = 1; k <= extra; ++k) {
          const unsigned char cont = (unsigned char)bytes[i + k];
          if((cont & 0xC0) != 0x80) {
            valid = false;
            break;
          }
          cp = (cp << 6) | (cont & 0x3F);
        }
        if(!valid) {
          ++i;
          continue;
        }
        i += extra + 1;
        out += (wchar_t)cp;
      }
      return true;
    }

  private:
    void Restore() {
#ifdef _WIN32
      if(saved_out >= 0) { _dup2(saved_out, 1); _close(saved_out); saved_out = -1; }
      if(saved_err >= 0) { _dup2(saved_err, 2); _close(saved_err); saved_err = -1; }
      if(saved_in >= 0) { _dup2(saved_in, 0); _close(saved_in); saved_in = -1; }
#else
      if(saved_out >= 0) { dup2(saved_out, 1); close(saved_out); saved_out = -1; }
      if(saved_err >= 0) { dup2(saved_err, 2); close(saved_err); saved_err = -1; }
      if(saved_in >= 0) { dup2(saved_in, 0); close(saved_in); saved_in = -1; }
#endif
    }
  };
}

#endif
