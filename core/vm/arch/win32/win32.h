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
                           65536, // output buffer size
                           65536, // input buffer size
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
    DWORD read = 0;

    if(ReadFile(pipe, buffer, MID_BUFFER_MAX - 1, &read, nullptr) && read > 0) {
      // trim trailing \r\n
      while(read > 0 && (buffer[read - 1] == '\r' || buffer[read - 1] == '\n')) {
        read--;
      }
      buffer[read] = '\0';
      return buffer;
    }

    buffer[0] = '\0';
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
    int total = 0;
    while(total < len) {
      int status = recv(sock, values + total, len - total, 0);
      if(status == SOCKET_ERROR) {
        return total > 0 ? total : -1;
      }
      if(status == 0) {
        break; // connection closed
      }
      total += status;
    }
    return total;
  }

  static void Close(SOCKET sock) {
    closesocket(sock);
  }

  static bool SetKeepAlive(SOCKET sock, bool enable) {
    BOOL val = enable ? TRUE : FALSE;
    return setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (const char*)&val, sizeof(val)) == 0;
  }

  static bool SetNoDelay(SOCKET sock, bool enable) {
    BOOL val = enable ? TRUE : FALSE;
    return setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&val, sizeof(val)) == 0;
  }

  static bool SetRecvTimeout(SOCKET sock, int ms) {
    DWORD timeout = (DWORD)ms;
    return setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout)) == 0;
  }

  static bool SetSendTimeout(SOCKET sock, int ms) {
    DWORD timeout = (DWORD)ms;
    return setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout)) == 0;
  }

  static bool SetRecvBufSize(SOCKET sock, int bytes) {
    return setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (const char*)&bytes, sizeof(bytes)) == 0;
  }

  static bool SetSendBufSize(SOCKET sock, int bytes) {
    return setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (const char*)&bytes, sizeof(bytes)) == 0;
  }

  static SOCKET OpenWithTimeout(const char* address, int port, int timeout_ms);

  static SOCKET Bind(int port) {
    SOCKET server = socket(AF_INET, SOCK_STREAM, 0);
    if(server == INVALID_SOCKET) {
      return -1;
    }

    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

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
  // Operator opt-out of TLS certificate verification for secure CLIENT sockets,
  // intended ONLY for testing against self-signed/dev servers. Default is OFF —
  // certificates are verified (chain + hostname). Enable for a test run with
  // OBJECK_TLS_INSECURE_SKIP_VERIFY=1. Cached on first read.
  static bool InsecureSkipVerify() {
    static int cached = -1;
    if(cached < 0) {
      size_t len = 0;
      char val[8] = { 0 };
      cached = (!getenv_s(&len, val, sizeof(val), "OBJECK_TLS_INSECURE_SKIP_VERIFY") && len > 0 &&
                (val[0] == '1' || val[0] == 't' || val[0] == 'T' || val[0] == 'y' || val[0] == 'Y')) ? 1 : 0;
    }
    return cached == 1;
  }

  static bool OpenH2(const char* address, int port, const std::string& pem_file, SecureSocketCtx*& sctx) {
    sctx = new SecureSocketCtx();

    const char* pers = "objeck_ssl_h2_client";
    int ret = mbedtls_ctr_drbg_seed(&sctx->ctr_drbg, mbedtls_entropy_func, &sctx->entropy,
                                     (const unsigned char*)pers, strlen(pers));
    if(ret != 0) { sctx->last_error = ret; delete sctx; sctx = nullptr; return false; }

    std::string cert_path = pem_file.empty()
      ? UnicodeToBytes(GetLibraryPath()) + CACERT_PEM_FILE : pem_file;
    ret = mbedtls_x509_crt_parse_file(&sctx->cacert, cert_path.c_str());
    if(ret < 0) { sctx->last_error = ret; delete sctx; sctx = nullptr; return false; }

    std::string port_str = std::to_string(port);
    ret = mbedtls_net_connect(&sctx->net, address, port_str.c_str(), MBEDTLS_NET_PROTO_TCP);
    if(ret != 0) { sctx->last_error = ret; delete sctx; sctx = nullptr; return false; }

    ret = mbedtls_ssl_config_defaults(&sctx->conf, MBEDTLS_SSL_IS_CLIENT,
                                       MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if(ret != 0) { sctx->last_error = ret; delete sctx; sctx = nullptr; return false; }

    // REQUIRED by default: abort the handshake on any certificate failure
    // (untrusted CA, expired, hostname mismatch). With OPTIONAL the handshake
    // completes and the result must be checked manually via
    // mbedtls_ssl_get_verify_result(); that check was absent here, so any
    // certificate was accepted (MITM). OPTIONAL is used only when the operator
    // opts into insecure mode for testing (see InsecureSkipVerify()).
    mbedtls_ssl_conf_authmode(&sctx->conf,
      InsecureSkipVerify() ? MBEDTLS_SSL_VERIFY_OPTIONAL : MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&sctx->conf, &sctx->cacert, nullptr);
    mbedtls_ssl_conf_rng(&sctx->conf, mbedtls_ctr_drbg_random, &sctx->ctr_drbg);

    static const char* alpn_list[] = { "h2", "http/1.1", nullptr };
    mbedtls_ssl_conf_alpn_protocols(&sctx->conf, alpn_list);

    ret = mbedtls_ssl_setup(&sctx->ssl, &sctx->conf);
    if(ret != 0) { sctx->last_error = ret; delete sctx; sctx = nullptr; return false; }

    ret = mbedtls_ssl_set_hostname(&sctx->ssl, address);
    if(ret != 0) { sctx->last_error = ret; delete sctx; sctx = nullptr; return false; }

    mbedtls_ssl_set_bio(&sctx->ssl, &sctx->net, mbedtls_net_send, mbedtls_net_recv, nullptr);

    while((ret = mbedtls_ssl_handshake(&sctx->ssl)) != 0) {
      if(ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
        sctx->last_error = ret; delete sctx; sctx = nullptr; return false;
      }
    }

    const char* alpn = mbedtls_ssl_get_alpn_protocol(&sctx->ssl);
    if(!alpn || strcmp(alpn, "h2") != 0) {
      delete sctx; sctx = nullptr; return false;
    }

    return true;
  }

  static bool Open(const char* address, int port, const std::string &pem_file, SecureSocketCtx* &sctx) {
    sctx = new SecureSocketCtx();

    // Seed the random number generator
    const char* pers = "objeck_ssl_client";
    int ret = mbedtls_ctr_drbg_seed(&sctx->ctr_drbg, mbedtls_entropy_func, &sctx->entropy,
                                     (const unsigned char*)pers, strlen(pers));
    if(ret != 0) {
      sctx->last_error = ret;
      delete sctx;
      sctx = nullptr;
      return false;
    }

    // Load CA certificates
    std::string cert_path;
    if(!pem_file.empty()) {
      cert_path = pem_file;
    }
    else {
      cert_path = UnicodeToBytes(GetLibraryPath()) + CACERT_PEM_FILE;
    }

    ret = mbedtls_x509_crt_parse_file(&sctx->cacert, cert_path.c_str());
    if(ret < 0) {
      sctx->last_error = ret;
      std::wcerr << L">>> Unable to find/read cryptographic PEM file : '" << BytesToUnicode(cert_path) << L"' <<<" << std::endl;
      delete sctx;
      sctx = nullptr;
      return false;
    }

    // Connect to server
    std::string addr_str = address;
    if(addr_str.size() < 1 || port < 0) {
      delete sctx;
      sctx = nullptr;
      return false;
    }

    std::string port_str = std::to_string(port);
    ret = mbedtls_net_connect(&sctx->net, address, port_str.c_str(), MBEDTLS_NET_PROTO_TCP);
    if(ret != 0) {
      sctx->last_error = ret;
      delete sctx;
      sctx = nullptr;
      return false;
    }

    // Configure SSL
    ret = mbedtls_ssl_config_defaults(&sctx->conf, MBEDTLS_SSL_IS_CLIENT,
                                       MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if(ret != 0) {
      sctx->last_error = ret;
      delete sctx;
      sctx = nullptr;
      return false;
    }

    // REQUIRED by default so an untrusted/self-signed certificate aborts the
    // handshake (prevents MITM). Operators can opt into accepting self-signed
    // certs for testing via OBJECK_TLS_INSECURE_SKIP_VERIFY (see below).
    const bool insecure_skip_verify = InsecureSkipVerify();
    mbedtls_ssl_conf_authmode(&sctx->conf,
      insecure_skip_verify ? MBEDTLS_SSL_VERIFY_OPTIONAL : MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&sctx->conf, &sctx->cacert, nullptr);
    mbedtls_ssl_conf_rng(&sctx->conf, mbedtls_ctr_drbg_random, &sctx->ctr_drbg);

    ret = mbedtls_ssl_setup(&sctx->ssl, &sctx->conf);
    if(ret != 0) {
      sctx->last_error = ret;
      delete sctx;
      sctx = nullptr;
      return false;
    }

    // Set SNI hostname
    ret = mbedtls_ssl_set_hostname(&sctx->ssl, address);
    if(ret != 0) {
      sctx->last_error = ret;
      delete sctx;
      sctx = nullptr;
      return false;
    }

    mbedtls_ssl_set_bio(&sctx->ssl, &sctx->net, mbedtls_net_send, mbedtls_net_recv, nullptr);

    // Perform TLS handshake
    while((ret = mbedtls_ssl_handshake(&sctx->ssl)) != 0) {
      if(ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
        sctx->last_error = ret;
        delete sctx;
        sctx = nullptr;
        return false;
      }
    }

    // In the default (secure) mode the handshake already enforced verification
    // via VERIFY_REQUIRED, so reaching here means the cert is valid. In insecure
    // test mode we used VERIFY_OPTIONAL, so accept an untrusted/self-signed cert
    // but still reject other failures (e.g. hostname mismatch, expiry).
    if(insecure_skip_verify) {
      uint32_t flags = mbedtls_ssl_get_verify_result(&sctx->ssl);
      if(flags != 0 && flags != MBEDTLS_X509_BADCERT_NOT_TRUSTED) {
        sctx->last_error = MBEDTLS_ERR_X509_CERT_VERIFY_FAILED;
        delete sctx;
        sctx = nullptr;
        return false;
      }
    }

    return true;
  }

  static void WriteByte(char value, SecureSocketCtx* sctx) {
    int ret = mbedtls_ssl_write(&sctx->ssl, (const unsigned char*)&value, 1);
    if(ret < 0) {
      sctx->last_error = ret;
    }
  }

  static int WriteBytes(const char* values, int len, SecureSocketCtx* sctx) {
    int written = 0;
    while(written < len) {
      int ret = mbedtls_ssl_write(&sctx->ssl, (const unsigned char*)values + written, len - written);
      if(ret < 0) {
        if(ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
          continue;
        }
        sctx->last_error = ret;
        return -1;
      }
      written += ret;
    }
    return written;
  }

  static char ReadByte(SecureSocketCtx* sctx, int &status) {
    unsigned char value;
    status = mbedtls_ssl_read(&sctx->ssl, &value, 1);
    if(status <= 0) {
      if(status < 0) {
        sctx->last_error = status;
      }
      return '\0';
    }
    return (char)value;
  }

  static int ReadBytes(char* values, int len, SecureSocketCtx* sctx) {
    int total = 0;
    while(total < len) {
      int status = mbedtls_ssl_read(&sctx->ssl, (unsigned char*)values + total, len - total);
      if(status < 0) {
        sctx->last_error = status;
        return total > 0 ? total : -1;
      }
      if(status == 0) {
        break; // connection closed
      }
      total += status;
    }
    return total;
  }

  static void Close(SecureSocketCtx* sctx) {
    if(sctx) {
      mbedtls_ssl_close_notify(&sctx->ssl);
      delete sctx;
    }
  }

  static bool SetMinTLSVersion(SecureSocketCtx* sctx, int ver) {
    if(!sctx) return false;
    mbedtls_ssl_conf_min_tls_version(&sctx->conf, (mbedtls_ssl_protocol_version)ver);
    return true;
  }

  static bool SetVerifyPeer(SecureSocketCtx* sctx, bool strict) {
    if(!sctx) return false;
    mbedtls_ssl_conf_authmode(&sctx->conf,
      strict ? MBEDTLS_SSL_VERIFY_REQUIRED : MBEDTLS_SSL_VERIFY_OPTIONAL);
    return true;
  }

  static std::string GetCertFingerprint(SecureSocketCtx* sctx) {
    if(!sctx) return "";
    const mbedtls_x509_crt* cert = mbedtls_ssl_get_peer_cert(&sctx->ssl);
    if(!cert) return "";
    unsigned char hash[32];
    if(mbedtls_sha256(cert->raw.p, cert->raw.len, hash, 0) != 0) return "";
    char hex[65];
    for(int i = 0; i < 32; i++) snprintf(hex + i * 2, 3, "%02x", hash[i]);
    return std::string(hex, 64);
  }

  static bool SetKeepAlive(SecureSocketCtx* sctx, bool enable) {
    if(!sctx) return false;
    BOOL val = enable ? TRUE : FALSE;
    return setsockopt((SOCKET)sctx->net.fd, SOL_SOCKET, SO_KEEPALIVE, (const char*)&val, sizeof(val)) == 0;
  }

  static bool SetNoDelay(SecureSocketCtx* sctx, bool enable) {
    if(!sctx) return false;
    BOOL val = enable ? TRUE : FALSE;
    return setsockopt((SOCKET)sctx->net.fd, IPPROTO_TCP, TCP_NODELAY, (const char*)&val, sizeof(val)) == 0;
  }

  static bool SetRecvTimeout(SecureSocketCtx* sctx, int ms) {
    if(!sctx) return false;
    DWORD timeout = (DWORD)ms;
    return setsockopt((SOCKET)sctx->net.fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout)) == 0;
  }

  static bool SetSendTimeout(SecureSocketCtx* sctx, int ms) {
    if(!sctx) return false;
    DWORD timeout = (DWORD)ms;
    return setsockopt((SOCKET)sctx->net.fd, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout)) == 0;
  }

  static bool SetRecvBufSize(SecureSocketCtx* sctx, int bytes) {
    if(!sctx) return false;
    return setsockopt((SOCKET)sctx->net.fd, SOL_SOCKET, SO_RCVBUF, (const char*)&bytes, sizeof(bytes)) == 0;
  }

  static bool SetSendBufSize(SecureSocketCtx* sctx, int bytes) {
    if(!sctx) return false;
    return setsockopt((SOCKET)sctx->net.fd, SOL_SOCKET, SO_SNDBUF, (const char*)&bytes, sizeof(bytes)) == 0;
  }
};

class IPDtlsSocket {
 public:
  static bool Open(const char* address, int port, const std::string &pem_file, DtlsSocketCtx* &sctx, bool verify = true) {
    sctx = new DtlsSocketCtx();

    const char* pers = "objeck_dtls_client";
    int ret = mbedtls_ctr_drbg_seed(&sctx->ctr_drbg, mbedtls_entropy_func, &sctx->entropy,
                                     (const unsigned char*)pers, strlen(pers));
    if(ret != 0) {
      sctx->last_error = ret;
      delete sctx;
      sctx = nullptr;
      return false;
    }

    std::string cert_path;
    if(!pem_file.empty()) {
      cert_path = pem_file;
    }
    else {
      cert_path = UnicodeToBytes(GetLibraryPath()) + CACERT_PEM_FILE;
    }

    ret = mbedtls_x509_crt_parse_file(&sctx->cacert, cert_path.c_str());
    if(ret < 0) {
      sctx->last_error = ret;
      std::wcerr << L">>> Unable to find/read cryptographic PEM file : '" << BytesToUnicode(cert_path) << L"' <<<" << std::endl;
      delete sctx;
      sctx = nullptr;
      return false;
    }

    std::string addr_str = address;
    if(addr_str.size() < 1 || port < 0) {
      delete sctx;
      sctx = nullptr;
      return false;
    }

    std::string port_str = std::to_string(port);
    ret = mbedtls_net_connect(&sctx->net, address, port_str.c_str(), MBEDTLS_NET_PROTO_UDP);
    if(ret != 0) {
      sctx->last_error = ret;
      delete sctx;
      sctx = nullptr;
      return false;
    }

    ret = mbedtls_ssl_config_defaults(&sctx->conf, MBEDTLS_SSL_IS_CLIENT,
                                       MBEDTLS_SSL_TRANSPORT_DATAGRAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if(ret != 0) {
      sctx->last_error = ret;
      delete sctx;
      sctx = nullptr;
      return false;
    }

    // Verify by default (chain + hostname), consistent with the TCP/HTTP-2/3
    // clients. An explicit verify=true always enforces it; otherwise it is still
    // enforced unless the operator opts into insecure mode for testing.
    const bool require_cert = verify || !IPSecureSocket::InsecureSkipVerify();
    mbedtls_ssl_conf_authmode(&sctx->conf,
      require_cert ? MBEDTLS_SSL_VERIFY_REQUIRED : MBEDTLS_SSL_VERIFY_OPTIONAL);
    mbedtls_ssl_conf_ca_chain(&sctx->conf, &sctx->cacert, nullptr);
    mbedtls_ssl_conf_rng(&sctx->conf, mbedtls_ctr_drbg_random, &sctx->ctr_drbg);

    ret = mbedtls_ssl_setup(&sctx->ssl, &sctx->conf);
    if(ret != 0) {
      sctx->last_error = ret;
      delete sctx;
      sctx = nullptr;
      return false;
    }

    ret = mbedtls_ssl_set_hostname(&sctx->ssl, address);
    if(ret != 0) {
      sctx->last_error = ret;
      delete sctx;
      sctx = nullptr;
      return false;
    }

    mbedtls_ssl_set_bio(&sctx->ssl, &sctx->net, mbedtls_net_send, mbedtls_net_recv, mbedtls_net_recv_timeout);
    mbedtls_ssl_set_timer_cb(&sctx->ssl, &sctx->timer, mbedtls_timing_set_delay, mbedtls_timing_get_delay);

    while((ret = mbedtls_ssl_handshake(&sctx->ssl)) != 0) {
      if(ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
        sctx->last_error = ret;
        delete sctx;
        sctx = nullptr;
        return false;
      }
    }

    uint32_t flags = mbedtls_ssl_get_verify_result(&sctx->ssl);
    if(require_cert && flags != 0) {
      sctx->last_error = MBEDTLS_ERR_X509_CERT_VERIFY_FAILED;
      delete sctx;
      sctx = nullptr;
      return false;
    }

    return true;
  }

  static void WriteByte(char value, DtlsSocketCtx* sctx) {
    int ret = mbedtls_ssl_write(&sctx->ssl, (const unsigned char*)&value, 1);
    if(ret < 0) {
      sctx->last_error = ret;
    }
  }

  static int WriteBytes(const char* values, int len, DtlsSocketCtx* sctx) {
    int written = 0;
    while(written < len) {
      int ret = mbedtls_ssl_write(&sctx->ssl, (const unsigned char*)values + written, len - written);
      if(ret < 0) {
        if(ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
          continue;
        }
        sctx->last_error = ret;
        return -1;
      }
      written += ret;
    }
    return written;
  }

  static char ReadByte(DtlsSocketCtx* sctx, int &status) {
    unsigned char value;
    status = mbedtls_ssl_read(&sctx->ssl, &value, 1);
    if(status <= 0) {
      if(status < 0) {
        sctx->last_error = status;
      }
      return '\0';
    }
    return (char)value;
  }

  static int ReadBytes(char* values, int len, DtlsSocketCtx* sctx) {
    int total = 0;
    while(total < len) {
      int status = mbedtls_ssl_read(&sctx->ssl, (unsigned char*)values + total, len - total);
      if(status < 0) {
        sctx->last_error = status;
        return total > 0 ? total : -1;
      }
      if(status == 0) {
        break;
      }
      total += status;
    }
    return total;
  }

  static void Close(DtlsSocketCtx* sctx) {
    if(sctx) {
      mbedtls_ssl_close_notify(&sctx->ssl);
      delete sctx;
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
