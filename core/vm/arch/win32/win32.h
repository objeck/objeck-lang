/***************************************************************************
 * Provides runtime support for Windows (Win32) based systems.
 *
 * Copyright (c) 2025, Randy Hollines
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

#pragma once

#define BUFSIZE 256

#include "../../common.h"
#include <windows.h>
#include <io.h>
#include <tchar.h>
#include <userenv.h>
#include <sys/stat.h>
#include <stdio.h>
#include <strsafe.h>
#include <accctrl.h>
#include <aclapi.h>
#include <versionhelpers.h>

#ifndef _MSYS2
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "User32.lib")
#pragma comment(lib, "UserEnv.lib")
#pragma comment(lib, "shlwapi.lib")
#endif

typedef void (WINAPI *PGNSI)(LPSYSTEM_INFO);
typedef BOOL (WINAPI *PGPI)(DWORD, DWORD, DWORD, DWORD, PDWORD);

/****************************
 * File support class
 ****************************/
class File {
  static bool GetAccountGroupOwner(const char* name, std::wstring &account, std::wstring &group) {
    std::wstring value;

    PSID sid_owner = nullptr;
    PSECURITY_DESCRIPTOR security = nullptr;

    HANDLE handle = CreateFile(name, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if(handle == INVALID_HANDLE_VALUE) {
      CloseHandle(handle);
      return false;
    }

    DWORD flag = GetSecurityInfo(handle, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
               &sid_owner, nullptr, nullptr, nullptr, &security);
    if (flag != ERROR_SUCCESS) {
      CloseHandle(handle);
      return false;
    }

    DWORD owner_size = 1;
    LPTSTR account_name = nullptr;

    DWORD domain_size = 1;
    LPTSTR domain_name = nullptr;

    SID_NAME_USE use;

    LookupAccountSid(nullptr, sid_owner, account_name, (LPDWORD)&owner_size,
                     domain_name, (LPDWORD)&domain_size, &use);

    account_name = new char[owner_size];
    domain_name = new char[domain_size];

    flag = LookupAccountSid(nullptr, sid_owner, account_name, (LPDWORD)&owner_size,
          domain_name, (LPDWORD)&domain_size, &use);
    if (flag == ERROR_SUCCESS) {
      // clean up
      CloseHandle(handle);
      
      delete[] account_name;
      account_name = nullptr;

      delete[] domain_name;
      domain_name = nullptr;

      return false;
    }

    account = BytesToUnicode(account_name);
    group = BytesToUnicode(domain_name);

    // clean up
    CloseHandle(handle);

    delete[] account_name;
    account_name = nullptr;

    delete[] domain_name;
    domain_name = nullptr;

    return true;
  }

 public:
  static std::string TempName() {
    char buffer[FILENAME_MAX + 1];
    if(!tmpnam_s(buffer, FILENAME_MAX)) {
      return std::string(buffer);
    }

    return "";
  }

  static std::string FullPathName(const std::string name) {
    char buffer[BUFSIZE] = "";
    char* part = nullptr;

    if(!GetFullPathName(name.c_str(), BUFSIZE, buffer, &part)) {
      return "";
    }

    return buffer;
  }

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

    return _S_IFREG & buf.st_mode;
  }
  
  static bool FileReadOnly(const char* name) {
    if(!_access(name, 4)) {
      return true;
    }

    return false;
  }
  
  static bool FileWriteOnly(const char* name) {
    if (!_access(name, 2)) {
      return true;
    }

    return false;
  }

  static bool FileReadWrite(const char* name) {
    if (!_access(name, 6)) {
      return true;
    }

    return false;
  }

  static FILE* FileOpen(const char* name, const char* mode) {
    FILE* file;

#ifdef _MINGW
    file = fopen(name, mode);
#else
    if(fopen_s(&file, name, mode) != 0) {
      return nullptr;
    }
#endif

    return file;
  }

  static bool MakeDir(const char* name) {
    if(CreateDirectory(name, nullptr) == 0) {
      return false;
    }

    return true;
  }

  static std::wstring FileOwner(const char* name, bool is_account) {
    std::wstring account;  std::wstring group;

    if(GetAccountGroupOwner(name, account, group)) {
      if(is_account) {
        return account;
      }
      
      return group;
    }

    return L"";
  }

  static bool DirExists(const char* name) {
    struct _stat buf;
    if(_stat(name, &buf)) {
      return false;
    }

    return _S_IFDIR & buf.st_mode;
  }

  static std::vector<std::string> ListDir(const char* p) {
    std::vector<std::string> files;

    std::string path = p;
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
 * Pipe support class
 ****************************/
class Pipe {
public:
  static bool Create(const char* name, HANDLE& pipe) {
    pipe = CreateNamedPipe(name, 
                           PIPE_ACCESS_DUPLEX, 
                           PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 
                           PIPE_UNLIMITED_INSTANCES,
                           4096, // output buffer size
                           4096, // input buffer size
                           0, 
                           nullptr);
    if(pipe == INVALID_HANDLE_VALUE) {
      return false;
    }

    if(ConnectNamedPipe(pipe, nullptr) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED)) {
      return true;
    }
    else {
      CloseHandle(pipe);
      return false;
    }
  }
  
  static bool Open(const char* name, HANDLE& pipe) {
    while(true) {
      pipe = CreateFile(
        name,
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);

      if(pipe != INVALID_HANDLE_VALUE) {
        break;
      }

      // 15 second wait
      if(GetLastError() != ERROR_PIPE_BUSY || !WaitNamedPipe(name, 15000)) {
        CloseHandle(pipe);
        pipe = 0;
        return false;
      }
    }

    return true;
  }

  static bool Close(HANDLE pipe) {
    if(CloseHandle(pipe)) {
      return false;
    }

    return true;
  }
  
  static bool ReadByte(char &value, HANDLE pipe) {
    DWORD read;

    if(ReadFile(pipe, &value, 1, &read, nullptr)) {
      return read == 1;
    }

    return false;
  }

  static size_t ReadByteArray(char* buffer, size_t offset, size_t num, HANDLE pipe) {
    DWORD read;
    if(ReadFile(pipe, buffer + offset, (DWORD)num, &read, nullptr)) {
      return read;
    }

    return 0;
  }

  static size_t WriteByteArray(const char* buffer, size_t offset, size_t num, HANDLE pipe) {
    DWORD written;
    if(WriteFile(pipe, buffer + offset, (DWORD)num, &written, nullptr)) {
      return written;
    }

    return 0;
  }

  static bool WriteByte(char value, HANDLE pipe) {
    DWORD written;
    if(WriteFile(pipe, &value, 1, &written, nullptr)) {
      return written == 1;
    }

    return false;
  }

  static std::string ReadString(HANDLE pipe) {
    char buffer[MID_BUFFER_MAX];
    size_t buffer_index = 0;

    char value;
    bool done = false;
    DWORD read;

    do {
      if(ReadFile(pipe, &value, 1, &read, nullptr)) {
        done = true;
      }
      else if(read == 1 && value != '\0' && value != '\r' && value != '\n') {
        buffer[buffer_index++] = value;
      }
      else {
        done = true;
      }
    }
    while(!done && buffer_index < MID_BUFFER_MAX - 1);    
    buffer[buffer_index] = '\0';

    if(value == '\r') {
      ReadFile(pipe, &value, 1, &read, nullptr);
    }

    return buffer;
  }

  static bool WriteString(const std::string& line, HANDLE pipe) {
    DWORD written;
    const DWORD len = (DWORD)line.size();
    if(WriteFile(pipe, line.c_str(), len, &written, nullptr)) {
      return written == len;
    }

    return false;
  }
};

/****************************
 * IP socket support class
 ****************************/
class IPSocket {
 public:
   static std::vector<std::string> Resolve(const char* address);

   static SOCKET Open(const char* address, const int port);
  
  static int WriteByte(char value, SOCKET sock) {
    int status = send(sock, &value, 1, 0);
    if(status == SOCKET_ERROR) {
      return '\0';
    }

    return status;
  }

  static int WriteBytes(const char* values, int len, SOCKET sock) {
    int status = send(sock, values, len, 0);
    if(status == SOCKET_ERROR) {
      return -1;
    }
    
    return status;
  }

  static char ReadByte(SOCKET sock, int &status) {
    char value;
    status = recv(sock, &value, 1, 0);
    if(status == SOCKET_ERROR) {
      return '\0';
    }
    
    return value;
  }

  static int ReadBytes(char* values, int len, SOCKET sock) {
    int status = recv(sock, values, len, 0);
    if(status == SOCKET_ERROR) {
      return -1;
    }
    
    return status;
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

  static SOCKET Accept(SOCKET server, char* client_address, int& client_port);
};

/****************************
 * UDP socket support class
 ****************************/
class UDPSocket {
public:
  static bool Bind(int port, SOCKET &sock, SOCKADDR_IN* & addr_in) {
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(sock != INVALID_SOCKET) {
      addr_in = new SOCKADDR_IN;
      addr_in->sin_family = AF_INET;
      addr_in->sin_port = htons(port);
      addr_in->sin_addr.s_addr = htonl(INADDR_ANY);

      if(bind(sock, (SOCKADDR*)addr_in, sizeof(SOCKADDR_IN)) == SOCKET_ERROR) {
        closesocket(sock);
        sock = INVALID_SOCKET;

        delete addr_in;
        addr_in = nullptr;

        return false;
      }

      return true;
    }

    return false;
  }

  static void Close(SOCKET sock, SOCKADDR_IN* addr_in) {
    closesocket(sock);

    delete addr_in;
    addr_in = nullptr;
  }
};

/****************************
 * IP socket support class
 ****************************/
class IPSecureSocket {
 public:
  static bool Open(const char* address, int port, const char* pem_file, SSL_CTX* &ctx, BIO* &bio, X509* &cert) {
    ctx = SSL_CTX_new(SSLv23_client_method());
    bio = BIO_new_ssl_connect(ctx);
    if(!bio) {
      SSL_CTX_free(ctx);
      return false;
    }

    std::string cert_path;
    if(pem_file) {
      cert_path = pem_file;
    }
    else {
      cert_path = UnicodeToBytes(GetLibraryPath()) + CACERT_PEM_FILE;
    }

    if(!SSL_CTX_load_verify_locations(ctx, cert_path.c_str(), nullptr)) {
      BIO_free_all(bio);
      SSL_CTX_free(ctx);
      std::wcerr << L">>> Unable to find/read cryptographic PEM file : '" << BytesToUnicode(cert_path) << L"' <<<" << std::endl;
      return false;
    }
    
    SSL* ssl;
    BIO_get_ssl(bio, &ssl);
    SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);
    std::string ssl_address = address;
    if(ssl_address.size() < 1 || port < 0) {
      BIO_free_all(bio);
      SSL_CTX_free(ctx);
      return false;
    }    
    ssl_address += ":";
    ssl_address += UnicodeToBytes(IntToString(port));
    BIO_set_conn_hostname(bio, ssl_address.c_str());
    
    if(!SSL_set_tlsext_host_name(ssl, address)) {
      BIO_free_all(bio);
      SSL_CTX_free(ctx);
      return false;
    }
    
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
    
    cert = SSL_get_peer_certificate(ssl);
    if(!cert) {
      BIO_free_all(bio);
      SSL_CTX_free(ctx);
      return false;
    }

    const int status = SSL_get_verify_result(ssl);
    if(status != X509_V_OK && status != X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT) {
      BIO_free_all(bio);
      SSL_CTX_free(ctx);
      X509_free(cert);
      return false;
    }
    
    return true;
  }
  
  static void WriteByte(char value, SSL_CTX* ctx, BIO* bio) {
    int status = BIO_write(bio, &value, 1);
    if(status < 0) {
      value = '\0';
    }
  }
  
  static int WriteBytes(const char* values, int len, SSL_CTX* ctx, BIO* bio) {
    int status = BIO_write(bio, values, len);
    if(status < 0) {
      return -1;
    } 
    
    return BIO_flush(bio);
  }

  static char ReadByte(SSL_CTX* ctx, BIO* bio, int &status) {
    char value;
    status = BIO_read(bio, &value, 1);
    if(status < 0) {
      return '\0';
    } 
    
    return value;
  }
  
  static int ReadBytes(char* values, int len, SSL_CTX* ctx, BIO* bio) {
    int status = BIO_read(bio, values, len);
    if(status < 0) {
      return -1;
    } 
    
    return status;
  }
  
  static void Close(SSL_CTX* ctx, BIO* bio, X509* cert) {
    if(bio) {
      BIO_free_all(bio);
    }

    if(ctx) {
      SSL_CTX_free(ctx);
    }

    if(cert) {
      X509_free(cert);
    }
  }
};

/****************************
 * System operations
 ****************************/
class System {
 public:
   static std::vector<std::string> CommandOutput(const char* c, int &status) {
     std::vector<std::string> output;

     // create temporary file
     const std::string tmp_file_name = File::TempName();
     FILE* file = File::FileOpen(tmp_file_name.c_str(), "wb");
     if(file) {
       fclose(file);

       std::string str_cmd(c);
       str_cmd += " > ";
       str_cmd += tmp_file_name;
       str_cmd += " 2>&1";
       status = system(str_cmd.c_str());

       // read file output
       std::ifstream file_out(tmp_file_name.c_str());
       if(file_out.is_open()) {
         std::string line_out;
         while(getline(file_out, line_out)) {
           output.push_back(line_out);
         }
         file_out.close();

         // delete file
         remove(tmp_file_name.c_str());
       }
     }
     
     return output;
   }

   static BOOL GetUserDirectory(char* buf, DWORD len) {
    HANDLE handle;

    if(!OpenProcessToken(GetCurrentProcess(), TOKEN_READ, &handle)) {
      return FALSE;
    }

    if(!GetUserProfileDirectory(handle, buf, &len)) {
      return FALSE;
    }

    CloseHandle(handle);
    return TRUE;
  }

  static std::string GetPlatform() {
    std::string platform;

    TCHAR buffer[BUFSIZE];
    if(GetOSDisplayString(buffer)) {
      platform = buffer;
    }
    else {
      platform = "Windows 2000 or earlier";
    }

    return platform;
  }

  static BOOL GetOSDisplayString(LPTSTR buffer)
  {
    if(IsWindows11OrGreater()) {
      StringCchCopy(buffer, BUFSIZE, TEXT("Windows 11"));
      return TRUE;
    }
    else if(IsWindows10OrGreater()) {
      StringCchCopy(buffer, BUFSIZE, TEXT("Windows 10"));
      return TRUE;
    }
    else if(IsWindows8Point1OrGreater()) {
      StringCchCopy(buffer, BUFSIZE, TEXT("Windows 8.1"));
      return TRUE;
    }
    else if(IsWindows8OrGreater()) {
      StringCchCopy(buffer, BUFSIZE, TEXT("Windows 8"));
      return TRUE;
    }
    else if(IsWindows7OrGreater()) {
      StringCchCopy(buffer, BUFSIZE, TEXT("Windows 7"));
      return TRUE;
    }
    else if (IsWindowsVistaOrGreater()) {
      StringCchCopy(buffer, BUFSIZE, TEXT("Windows Vista"));
      return TRUE;
    }
    else if(IsWindowsXPOrGreater()) {
      StringCchCopy(buffer, BUFSIZE, TEXT("Windows XP"));
      return TRUE;
    }

    return FALSE;
  }

  static size_t GetTotalSystemMemory()
  {
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    GlobalMemoryStatusEx(&status);
    return status.ullTotalPhys;
  }
  
  //
  // Chuck Walbourn
  // https://github.com/walbourn/walbourn.github.io
  //
  static BOOL IsWindows11OrGreater()
  {
    OSVERSIONINFOEXW osvi = { sizeof(osvi), 0, 0, 0, 0, {0}, 0, 0 };
    DWORDLONG const dwlConditionMask = VerSetConditionMask(VerSetConditionMask(VerSetConditionMask(0, VER_MAJORVERSION, VER_GREATER_EQUAL), VER_MINORVERSION, VER_GREATER_EQUAL), VER_BUILDNUMBER, VER_GREATER_EQUAL);

    osvi.dwMajorVersion = HIBYTE(_WIN32_WINNT_WIN10);
    osvi.dwMinorVersion = LOBYTE(_WIN32_WINNT_WIN10);
    osvi.dwBuildNumber = 22000;

    return VerifyVersionInfoW(&osvi, VER_MAJORVERSION | VER_MINORVERSION | VER_BUILDNUMBER, dwlConditionMask) != FALSE;
  }
};
