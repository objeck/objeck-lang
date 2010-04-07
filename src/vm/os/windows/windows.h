/***************************************************************************
 * Provides runtime support for Windows (Win32) based systems.
 *
 * Copyright (c) 2008-2009, Randy Hollines
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in
 * the documentation and/or other materials provided with the distribution.
 * - Neither the name of the StackVM Team nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *%
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 *  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ***************************************************************************/

#ifndef __WINDOWS_H__
#define __WINDOWS_H__

#include "../../common.h"
#include <windows.h>
#pragma comment(lib, "Ws2_32.lib")

class File {
 public:
  static long FileSize(const char* name) {
    HANDLE file = CreateFile(name, GENERIC_READ, 
			     FILE_SHARE_READ, NULL, OPEN_EXISTING, 
			     FILE_ATTRIBUTE_NORMAL, NULL);
    
    if(file == INVALID_HANDLE_VALUE) {
      return -1;
    }
    
    long size = GetFileSize(file, NULL);
    CloseHandle(file);
    if(size < 0) {
      return -1;
    }
    
    return size;  
  }

  static bool FileExists(const char* name) {
    HANDLE file = CreateFile(name, GENERIC_READ, 
			     FILE_SHARE_READ, NULL, OPEN_EXISTING, 
			     FILE_ATTRIBUTE_NORMAL, NULL);    
    
    if(file == INVALID_HANDLE_VALUE) {
      return false;
    }
    CloseHandle(file);

    return true;
  }

  static FILE* FileOpen(const char* name, const char* mode) {
    FILE* file;
    if(fopen_s(&file, name, mode) != 0) {
      return NULL;
    }

    return file;
  }

  static bool MakeDir(const char* name) {
    if(CreateDirectory(name, NULL) == 0) {
      return false;
    }

    return true;
  }

  static bool IsDir(const char* name) {
    WIN32_FIND_DATA data;
    HANDLE find = FindFirstFile(name, &data);
    if(find == INVALID_HANDLE_VALUE) {
      return false;
    }
    FindClose(find);
    
    return true;
  }

  static vector<string> ListDir(char* p) {
    vector<string> files;
    
    string path = p;
    if(path.size() > 0 && path[path.size() - 1] == '\\') {
      path += "*";
    }
    else {
      path += "\\*";
    }

    WIN32_FIND_DATA file_data;
    HANDLE find = FindFirstFile(path.c_str(), &file_data);
    if(find == INVALID_HANDLE_VALUE) {
      return files;
    } 
    else {
      files.push_back(file_data.cFileName);

      bool b = FindNextFile(find, &file_data);
      while(b) {
        files.push_back(file_data.cFileName);
        b = FindNextFile(find, &file_data);
      }
      FindClose(find);
   }
    return files;
  }
};

/****************************
 * IP socket support class
 ****************************/
class IPSocket {
 public:
  static SOCKET Open(const char* address, int port) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(socket < 0) {
      return 0;
    }

    struct hostent* host_info = gethostbyname(address);
    if(!host_info) {
      return 0;
    }
    
    long host_addr;
    memcpy(&host_addr, host_info->h_addr, host_info->h_length);
    
    struct sockaddr_in ip_addr;
    ip_addr.sin_addr.s_addr = host_addr;;
    ip_addr.sin_port=htons(port);
    ip_addr.sin_family = AF_INET;
    
    if(!connect(sock, (struct sockaddr*)&ip_addr,sizeof(ip_addr))) {
      return sock;
    }
    
    return 0;
  }

  static void WriteByte(char value, SOCKET sock) {
    send(sock, &value, 1, 0);
  }

  static int WriteBytes(char* values, int len, SOCKET sock) {
    return send(sock, values, len, 0);
  }

  static char ReadByte(SOCKET sock) {
    char value;
    recv(sock, &value, 1, 0);

    return value;
  }
  
  static char ReadBytes(char* values, int len, SOCKET sock) {
    return recv(sock, values, len, 0);
  }
  
  static void Close(SOCKET sock) {
    closesocket(sock);
  }
};

#endif
