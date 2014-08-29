/***************************************************************************
* Provides runtime support for Windows (Win32) based systems.
*
* Copyright (c) 2008-2013, Randy Hollines
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
* - Neither the name of the Objeck Team nor the names of its
* contributors may be used to endorse or promote products derived
* from this software without specific prior written permission.
*
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

#define BUFSIZE 256

#include "../../common.h"
#include <windows.h>
#include <tchar.h>
#include <sys/stat.h>
#include <stdio.h>
#include <strsafe.h>

#ifndef _MINGW
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "User32.lib")
#endif

typedef void (WINAPI *PGNSI)(LPSYSTEM_INFO);
typedef BOOL (WINAPI *PGPI)(DWORD, DWORD, DWORD, DWORD, PDWORD);

class File {
public:
  static long FileSize(const char* name) {
    struct _stat buf;
    if(_stat(name, &buf)) {
      return -1;
    }

    return buf.st_size;
  }

  static time_t FileCreatedTime(const char* name) {
    struct _stat buf;
    if(_stat(name, &buf)) {
      return -1;
    }

    return buf.st_ctime;
  }

  static time_t FileModifiedTime(const char* name) {
    struct _stat buf;
    if(_stat(name, &buf)) {
      return -1;
    }

    return buf.st_mtime;
  }

  static time_t FileAccessedTime(const char* name) {
    struct stat buf;
    if(stat(name, &buf)) {
      return -1;
    }

    return buf.st_atime;
  }

  static bool FileExists(const char* name) {
    struct _stat buf;
    if(_stat(name, &buf)) {
      return false;
    }

    return true;
  }

  static FILE* FileOpen(const char* name, const char* mode) {
    FILE* file;

#ifdef _MINGW
    file = fopen(name, mode);
#else
    if(fopen_s(&file, name, mode) != 0) {
      return NULL;
    }
#endif

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

  static vector<string> ListDir(const char* p) {
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

      BOOL b = FindNextFile(find, &file_data);
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
    if(sock == INVALID_SOCKET) {
      return -1;
    }

    struct hostent* host_info = gethostbyname(address);
    if(!host_info) {
      closesocket(sock);
      return -1;
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

    closesocket(sock);
    return -1;
  }

  static void WriteByte(char value, SOCKET sock) {
    send(sock, &value, 1, 0);
  }

  static int WriteBytes(const char* values, int len, SOCKET sock) {
    return send(sock, values, len, 0);
  }

  static char ReadByte(SOCKET sock) {
    char value;
    recv(sock, &value, 1, 0);

    return value;
  }

  static char ReadByte(SOCKET sock, int &status) {
    char value;
    status = recv(sock, &value, 1, 0);

    return value;
  }

  static int ReadBytes(char* values, int len, SOCKET sock) {
    return recv(sock, values, len, 0);
  }

  static void Close(SOCKET sock) {
    closesocket(sock);
  }

  static SOCKET Bind(int port) {
    SOCKET server = socket(AF_INET, SOCK_STREAM, 0);
    if(server == INVALID_SOCKET) {
      return -1;
    }

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(port);

    if(::bind(server, (SOCKADDR*)&sin, sizeof(sin)) ==  SOCKET_ERROR) {
      closesocket(server);
      return -1;
    }

    return server;
  }

  static bool Listen(SOCKET server, int backlog) {
    if(listen(server, backlog) < 0) {
      return false;
    }

    return true;
  }

  static SOCKET Accept(SOCKET server, char* client_address, int &client_port) {
    struct sockaddr_in pin;
    int addrlen = sizeof(pin);

    SOCKET client = accept(server, (struct sockaddr *)&pin, &addrlen);
    if(client == INVALID_SOCKET) {
      client_address[0] = '\0';
      client_port = -1;
      return -1;
    }
    strncpy(client_address, inet_ntoa(pin.sin_addr), 255);
    client_port = ntohs(pin.sin_port);

    return client;
  }
};

/****************************
* IP socket support class
****************************/
class IPSecureSocket {
public:
  static bool Open(const char* address, int port, SSL_CTX* &ctx, BIO* &bio) {
    ctx = SSL_CTX_new(SSLv23_client_method());
    bio = BIO_new_ssl_connect(ctx);
    if(!bio) {
      SSL_CTX_free(ctx);
      return false;
    }

    SSL* ssl;
    BIO_get_ssl(bio, &ssl);
    SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);
    string ssl_address = address;
    if(ssl_address.size() < 1 || port < 0) {
      BIO_free_all(bio);
      SSL_CTX_free(ctx);
      return false;
    }    
    ssl_address += ":";
    ssl_address += UnicodeToBytes(IntToString(port));
    BIO_set_conn_hostname(bio, ssl_address.c_str());

    if(BIO_do_connect(bio) <= 0) {
      BIO_free_all(bio);
      SSL_CTX_free(ctx);
      return false;
    }

    if(BIO_do_handshake(bio) <= 0) {
      BIO_free_all(bio);
      SSL_CTX_free(ctx);
      return false;
    }

    return true;
  }

  static void WriteByte(char value, SSL_CTX* ctx, BIO* bio) {
    BIO_write(bio, &value, 1);
  }

  static int WriteBytes(const char* values, int len, SSL_CTX* ctx, BIO* bio) {
    return BIO_write(bio, values, len);
  }

  static char ReadByte(SSL_CTX* ctx, BIO* bio, int &status) {
    char value;
    status = BIO_read(bio, &value, 1);
    return value;
  }

  static int ReadBytes(char* values, int len, SSL_CTX* ctx, BIO* bio) {
    return BIO_read(bio, values, len);
  }

  static void Close(SSL_CTX* ctx, BIO* bio) {
    BIO_free_all(bio);
    SSL_CTX_free(ctx);
  }
};

/****************************
* System operations
****************************/
class System {
public:
  static string GetPlatform() {
    string platform;

    TCHAR buffer[BUFSIZE];
    if(GetOSDisplayString(buffer)) {
      platform = buffer;
    }
    else {
      platform = "Windows 2000 or earlier";
    }

    return platform;
  }


  static BOOL CompareWindowsVersion(DWORD dwMajorVersion, DWORD dwMinorVersion)
  {
    OSVERSIONINFOEX ver;
    DWORDLONG dwlConditionMask = 0;

    ZeroMemory(&ver, sizeof(OSVERSIONINFOEX));
    ver.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
    ver.dwMajorVersion = dwMajorVersion;
    ver.dwMinorVersion = dwMinorVersion;

    VER_SET_CONDITION(dwlConditionMask, VER_MAJORVERSION, VER_EQUAL);
    VER_SET_CONDITION(dwlConditionMask, VER_MINORVERSION, VER_EQUAL);

    return VerifyVersionInfo(&ver, VER_MAJORVERSION | VER_MINORVERSION, dwlConditionMask);
  }

  static BOOL GetOSDisplayString(LPTSTR buffer)
  {
    if (CompareWindowsVersion(6, 3)) {
      StringCchCopy(buffer, BUFSIZE, TEXT("Windows 8.1"));
      return TRUE;
    }
    else if (CompareWindowsVersion(6, 2)) {
      StringCchCopy(buffer, BUFSIZE, TEXT("Windows 8"));
      return TRUE;
    }
    else if (CompareWindowsVersion(6, 1)) {
      StringCchCopy(buffer, BUFSIZE, TEXT("Windows 7"));
      return TRUE;
    }
    else if (CompareWindowsVersion(6, 0)) {
      StringCchCopy(buffer, BUFSIZE, TEXT("Windows Vista"));
      return TRUE;
    }
    else if (CompareWindowsVersion(5, 2)) {
      StringCchCopy(buffer, BUFSIZE, TEXT("Windows XP 64-Bit Edition"));
      return TRUE;
    }
    else if (CompareWindowsVersion(5, 1)) {
      StringCchCopy(buffer, BUFSIZE, TEXT("Windows XP"));
      return TRUE;
    }

    return FALSE;
  }
};

#endif
