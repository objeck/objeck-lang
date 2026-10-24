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
#include <share.h>
#include <tchar.h>
#include <userenv.h>
#include <sys/stat.h>
#include <stdio.h>
#include <strsafe.h>
#include <accctrl.h>
#include <aclapi.h>
#include <versionhelpers.h>
#include <filesystem>

#ifndef _MSYS2
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "User32.lib")
#pragma comment(lib, "UserEnv.lib")
#pragma comment(lib, "shlwapi.lib")
#endif

typedef void (WINAPI *PGNSI)(LPSYSTEM_INFO);
typedef BOOL (WINAPI *PGPI)(DWORD, DWORD, DWORD, DWORD, PDWORD);

/****************************
 * File-system names
 *
 * Objeck strings are UTF-16 here, as Windows file-system names are, so a name
 * goes to the W APIs unchanged. Narrowing it to UTF-8 bytes for the ANSI APIs
 * (fopen, _stat, FindFirstFileA, ...) had them read the bytes in the active code
 * page, so any non-ASCII name -- 'données.txt', a user folder with an accent --
 * missed its file or hit another one. posix.h maps the same two calls to UTF-8.
 ****************************/
typedef std::wstring NativePath;

inline NativePath ToNativePath(const wchar_t* name) {
  return name;
}

inline std::wstring FromNativePath(const NativePath& name) {
  return name;
}

/****************************
 * File support class
 ****************************/
class File {
  static bool GetAccountGroupOwner(const wchar_t* name, std::wstring &account, std::wstring &group) {
    PSID sid_owner = nullptr;
    PSECURITY_DESCRIPTOR security = nullptr;

    HANDLE handle = CreateFileW(name, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if(handle == INVALID_HANDLE_VALUE) {
      return false;
    }

    const DWORD flag = GetSecurityInfo(handle, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
                                       &sid_owner, nullptr, nullptr, nullptr, &security);
    CloseHandle(handle);
    if(flag != ERROR_SUCCESS) {
      return false;
    }

    // sizes first, then the names
    DWORD owner_size = 0;
    DWORD domain_size = 0;
    SID_NAME_USE use;
    LookupAccountSidW(nullptr, sid_owner, nullptr, &owner_size, nullptr, &domain_size, &use);

    std::vector<wchar_t> account_name(owner_size + 1, L'\0');
    std::vector<wchar_t> domain_name(domain_size + 1, L'\0');
    const BOOL found = LookupAccountSidW(nullptr, sid_owner, account_name.data(), &owner_size,
                                         domain_name.data(), &domain_size, &use);
    LocalFree(security);
    if(!found) {
      return false;
    }

    account = account_name.data();
    group = domain_name.data();

    return true;
  }

 public:
  static std::wstring TempName() {
    // Create the file, as mkstemp does on POSIX: File->GetTempName() used to
    // name a file here and create one there (#1180). CREATE_NEW also closes
    // the window in which another process could take the name.
    wchar_t buffer[MAX_PATH + 1];
    for(int attempt = 0; attempt < 8; ++attempt) {
      if(_wtmpnam_s(buffer, MAX_PATH)) {
        return L"";
      }

      HANDLE handle = CreateFileW(buffer, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
      if(handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
        return std::wstring(buffer);
      }
    }

    return L"";
  }

  static std::wstring FullPathName(const std::wstring& name) {
    const DWORD size = GetFullPathNameW(name.c_str(), 0, nullptr, nullptr);
    if(!size) {
      return L"";
    }

    std::vector<wchar_t> buffer(size + 1, L'\0');
    const DWORD length = GetFullPathNameW(name.c_str(), (DWORD)buffer.size(), buffer.data(), nullptr);
    if(!length || length >= buffer.size()) {
      return L"";
    }

    return std::wstring(buffer.data(), length);
  }

  static int64_t FileSize(const wchar_t* name) {
    struct _stat64 buf;
    if(_wstat64(name, &buf)) {
      return -1;
    }

    return buf.st_size;
  }

  static time_t FileCreatedTime(const wchar_t* name) {
    struct _stat64 buf;
    if(_wstat64(name, &buf)) {
      return -1;
    }

    return buf.st_ctime;
  }

  static time_t FileModifiedTime(const wchar_t* name) {
    struct _stat64 buf;
    if(_wstat64(name, &buf)) {
      return -1;
    }

    return buf.st_mtime;
  }

  static time_t FileAccessedTime(const wchar_t* name) {
    struct _stat64 buf;
    if(_wstat64(name, &buf)) {
      return -1;
    }

    return buf.st_atime;
  }

  static bool FileExists(const wchar_t* name) {
    struct _stat64 buf;
    if(_wstat64(name, &buf)) {
      return false;
    }

    return _S_IFREG & buf.st_mode;
  }

  static bool FileReadOnly(const wchar_t* name) {
    if(!_waccess(name, 4)) {
      return true;
    }

    return false;
  }

  static bool FileWriteOnly(const wchar_t* name) {
    if (!_waccess(name, 2)) {
      return true;
    }

    return false;
  }

  static bool FileReadWrite(const wchar_t* name) {
    if (!_waccess(name, 6)) {
      return true;
    }

    return false;
  }

  static FILE* FileOpen(const wchar_t* name, const char* mode) {
    // the mode is ASCII ("rb", "w+b", ...)
    const std::wstring wmode(mode, mode + strlen(mode));
    FILE* file;

#ifdef _MINGW
    file = _wfopen(name, wmode.c_str());
#else
    // Shared, as files are on POSIX: fopen_s opens them unshared, so a file
    // open for writing could not be read by another handle (#1180)
    file = _wfsopen(name, wmode.c_str(), _SH_DENYNO);
    if(!file) {
      return nullptr;
    }
#endif

    return file;
  }

  static bool MakeDir(const wchar_t* name) {
    if(CreateDirectoryW(name, nullptr) == 0) {
      return false;
    }

    return true;
  }

  static std::wstring FileOwner(const wchar_t* name, bool is_account) {
    std::wstring account;  std::wstring group;

    if(GetAccountGroupOwner(name, account, group)) {
      if(is_account) {
        return account;
      }

      return group;
    }

    return L"";
  }

  static bool DirExists(const wchar_t* name) {
    struct _stat64 buf;
    if(_wstat64(name, &buf)) {
      return false;
    }

    return _S_IFDIR & buf.st_mode;
  }

  static std::vector<std::wstring> ListDir(const wchar_t* p) {
    std::vector<std::wstring> files;

    std::wstring path = p;
    if(path.size() > 0 && (path[path.size() - 1] == L'\\' || path[path.size() - 1] == L'/')) {
      path += L"*";
    }
    else {
      path += L"\\*";
    }

    WIN32_FIND_DATAW file_data;
    HANDLE find = FindFirstFileW(path.c_str(), &file_data);
    if(find == INVALID_HANDLE_VALUE) {
      return files;
    }

    // '.' and '..' are not entries; POSIX's ListDir has always left them out,
    // so Directory->List gave a different answer on each platform
    do {
      if(wcscmp(file_data.cFileName, L".") != 0 && wcscmp(file_data.cFileName, L"..") != 0) {
        files.push_back(file_data.cFileName);
      }
    }
    while(FindNextFileW(find, &file_data));
    FindClose(find);

    return files;
  }
};

/****************************
 * Pipe support class
 ****************************/
class Pipe {
  // One read or write that waits for its own completion. A pipe created here
  // is OVERLAPPED so that one thread can write while another is blocked
  // reading: Windows serializes synchronous I/O on a handle, so with a
  // synchronous pipe a reader parked in ReadFile held every WriteFile until the
  // peer sent something -- a language server reading on one thread could not
  // answer on another. Callers still see a blocking call. An OVERLAPPED
  // structure is also valid on a synchronous handle (a pipe from Open), where
  // the call simply completes before returning.
  static BOOL Transfer(HANDLE pipe, bool is_read, void* buffer, DWORD num, DWORD& done) {
    done = 0;

    OVERLAPPED overlapped;
    ZeroMemory(&overlapped, sizeof(overlapped));
    overlapped.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if(!overlapped.hEvent) {
      return FALSE;
    }

    BOOL status = is_read ? ReadFile(pipe, buffer, num, nullptr, &overlapped) : WriteFile(pipe, buffer, num, nullptr, &overlapped);
    DWORD error = status ? ERROR_SUCCESS : GetLastError();
    if(status || error == ERROR_IO_PENDING || error == ERROR_MORE_DATA) {
      status = GetOverlappedResult(pipe, &overlapped, &done, TRUE);
      error = status ? ERROR_SUCCESS : GetLastError();
    }

    CloseHandle(overlapped.hEvent);
    SetLastError(error);

    return status;
  }

  // A pipe created here is message-mode. Reading less than a whole message
  // fails with ERROR_MORE_DATA, but the bytes WERE read and the rest of the
  // message is returned by the next read. Treating that as a failure dropped
  // the data: any message larger than the caller's buffer -- an 8 KB read
  // against a 56 KB LSP didOpen -- was lost.
  static bool ReadSucceeded(BOOL status) {
    return status || GetLastError() == ERROR_MORE_DATA;
  }

public:
  static bool Create(const wchar_t* name, HANDLE& pipe) {
    pipe = CreateNamedPipeW(name,
                           PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                           PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                           PIPE_UNLIMITED_INSTANCES,
                           65536, // output buffer size
                           65536, // input buffer size
                           0,
                           nullptr);
    if(pipe == INVALID_HANDLE_VALUE) {
      return false;
    }

    // an overlapped handle needs an OVERLAPPED connect, too
    OVERLAPPED overlapped;
    ZeroMemory(&overlapped, sizeof(overlapped));
    overlapped.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if(!overlapped.hEvent) {
      CloseHandle(pipe);
      return false;
    }

    bool connected = false;
    if(ConnectNamedPipe(pipe, &overlapped)) {
      connected = true;
    }
    else {
      const DWORD error = GetLastError();
      if(error == ERROR_PIPE_CONNECTED) {
        connected = true;
      }
      else if(error == ERROR_IO_PENDING) {
        DWORD ignored = 0;
        connected = GetOverlappedResult(pipe, &overlapped, &ignored, TRUE) ? true : false;
      }
    }
    CloseHandle(overlapped.hEvent);

    if(connected) {
      return true;
    }

    CloseHandle(pipe);
    return false;
  }

  static bool Open(const wchar_t* name, HANDLE& pipe) {
    while(true) {
      pipe = CreateFileW(
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
      if(GetLastError() != ERROR_PIPE_BUSY || !WaitNamedPipeW(name, 15000)) {
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
    DWORD read = 0;

    if(ReadSucceeded(Transfer(pipe, true, &value, 1, read))) {
      return read == 1;
    }

    return false;
  }

  static size_t ReadByteArray(char* buffer, size_t offset, size_t num, HANDLE pipe) {
    DWORD read = 0;
    if(ReadSucceeded(Transfer(pipe, true, buffer + offset, (DWORD)num, read))) {
      return read;
    }

    return 0;
  }

  static size_t WriteByteArray(const char* buffer, size_t offset, size_t num, HANDLE pipe) {
    DWORD written = 0;
    if(Transfer(pipe, false, (void*)(buffer + offset), (DWORD)num, written)) {
      return written;
    }

    return 0;
  }

  static bool WriteByte(char value, HANDLE pipe) {
    DWORD written = 0;
    if(Transfer(pipe, false, &value, 1, written)) {
      return written == 1;
    }

    return false;
  }

  static std::string ReadString(HANDLE pipe) {
    char buffer[MID_BUFFER_MAX];
    DWORD read = 0;

    if(ReadSucceeded(Transfer(pipe, true, buffer, MID_BUFFER_MAX - 1, read)) && read > 0) {
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
    DWORD written = 0;
    const DWORD len = (DWORD)line.size();
    if(Transfer(pipe, false, (void*)line.c_str(), len, written)) {
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
    // send() on a blocking socket is only obliged to accept SOME of the buffer.
    // Once the kernel send buffer fills it returns a short count, and a single
    // unlooped send() drops the remainder on the floor -- no error, no retry,
    // and callers here either push that partial count into Objeck or discard it
    // entirely (SockTcpOutString ignores the return). ReadBytes below has always
    // looped, and so has the TLS WriteBytes; only plain TCP did not.
    int total = 0;
    while(total < len) {
      const int status = send(sock, values + total, len - total, 0);
      if(status == SOCKET_ERROR) {
        return total > 0 ? total : -1;
      }
      if(status == 0) {
        break;
      }
      total += status;
    }

    return total;
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
    // A bare closesocket() straight after send() discards whatever is still
    // queued in the kernel send buffer, so the HTTP server's "write the
    // response, close the socket" sequence delivered nothing at all on a good
    // fraction of connections -- the peer saw a reset with zero bytes read.
    // shutdown() hands the queued bytes off and sends FIN before the handle
    // goes away. Errors are ignored on purpose: the socket may already be
    // torn down by the peer, and there is nothing useful to do about it here.
    shutdown(sock, 1 /* SD_SEND; winsock2.h is not in this header's include set */);
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

  //
  // A wildcard server socket that takes IPv4 and IPv6 clients alike: an IPv6
  // socket on [::] with IPV6_V6ONLY off, so IPv4 clients arrive as mapped
  // addresses. Falls back to IPv4 0.0.0.0 where there is no IPv6 stack. This is
  // what every server binds, plain, TLS and DTLS, on every platform: the plain
  // server was IPv4 only everywhere, while the TLS and DTLS servers took the
  // first passive address getaddrinfo offered -- [::] with IPV6_V6ONLY on, so
  // IPv6 only, on Windows, and 0.0.0.0 on Linux -- so which clients could
  // reach a server depended on its kind and the platform.
  //
  static SOCKET BindAny(int port, int type);

  //
  // A server socket bound to one local address: a literal ("127.0.0.1",
  // "::1") or a name resolved with getaddrinfo. An empty or null host is the
  // dual-stack wildcard (BindAny). A name can resolve to several addresses
  // and one socket binds one, so this binds the first IPv4 address that
  // binds, else the first IPv6 one: "localhost" is 127.0.0.1 on every
  // platform, whatever order the resolver lists its addresses in. An IPv6
  // address is bound IPv6-only, so "::1" takes ::1 clients and nothing else.
  // On failure returns INVALID_SOCKET with the reason in WSAGetLastError(). A
  // name lookup can block: the caller parks around it.
  //
  static SOCKET BindAddress(const char* host, int port, int type);

  //
  // A peer's numeric address and port. An IPv4 client of a dual-stack server
  // arrives as ::ffff:a.b.c.d and is reported as a.b.c.d, as it always was.
  //
  static void FormatAddress(const struct sockaddr* addr, char* buffer, size_t buffer_size, int& port);

  static SOCKET Bind(int port) {
    return BindAny(port, SOCK_STREAM);
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
    ret = TlsParseCertFile(&sctx->cacert, cert_path);
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
    TlsVerifyIpSan(&sctx->conf, &sctx->ip_name, address);
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

    ret = TlsParseCertFile(&sctx->cacert, cert_path);
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
    TlsVerifyIpSan(&sctx->conf, &sctx->ip_name, address);
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
        if(ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
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
    do {
      status = mbedtls_ssl_read(&sctx->ssl, &value, 1);
    } while(status == MBEDTLS_ERR_SSL_WANT_READ || status == MBEDTLS_ERR_SSL_WANT_WRITE);
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
        if(status == MBEDTLS_ERR_SSL_WANT_READ || status == MBEDTLS_ERR_SSL_WANT_WRITE) {
          continue;
        }
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
      sctx->SetError(ret);
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

    ret = TlsParseCertFile(&sctx->cacert, cert_path);
    if(ret < 0) {
      sctx->SetError(ret);
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
      sctx->SetError(ret);
      delete sctx;
      sctx = nullptr;
      return false;
    }

    ret = mbedtls_ssl_config_defaults(&sctx->conf, MBEDTLS_SSL_IS_CLIENT,
                                       MBEDTLS_SSL_TRANSPORT_DATAGRAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if(ret != 0) {
      sctx->SetError(ret);
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
    TlsVerifyIpSan(&sctx->conf, &sctx->ip_name, address);
    mbedtls_ssl_conf_rng(&sctx->conf, mbedtls_ctr_drbg_random, &sctx->ctr_drbg);

    ret = mbedtls_ssl_setup(&sctx->ssl, &sctx->conf);
    if(ret != 0) {
      sctx->SetError(ret);
      delete sctx;
      sctx = nullptr;
      return false;
    }

    ret = mbedtls_ssl_set_hostname(&sctx->ssl, address);
    if(ret != 0) {
      sctx->SetError(ret);
      delete sctx;
      sctx = nullptr;
      return false;
    }

    mbedtls_ssl_set_bio(&sctx->ssl, &sctx->net, mbedtls_net_send, mbedtls_net_recv, mbedtls_net_recv_timeout);
    mbedtls_ssl_set_timer_cb(&sctx->ssl, &sctx->timer, mbedtls_timing_set_delay, mbedtls_timing_get_delay);

    while((ret = mbedtls_ssl_handshake(&sctx->ssl)) != 0) {
      if(ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
        sctx->SetError(ret);
        delete sctx;
        sctx = nullptr;
        return false;
      }
    }

    uint32_t flags = mbedtls_ssl_get_verify_result(&sctx->ssl);
    if(require_cert && flags != 0) {
      sctx->SetError(MBEDTLS_ERR_X509_CERT_VERIFY_FAILED);
      delete sctx;
      sctx = nullptr;
      return false;
    }

    return true;
  }

  static void WriteByte(char value, DtlsSocketCtx* sctx) {
    int ret = mbedtls_ssl_write(&sctx->ssl, (const unsigned char*)&value, 1);
    if(ret < 0) {
      sctx->SetError(ret);
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
        sctx->SetError(ret);
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
        sctx->SetError(status);
      }
      return '\0';
    }
    return (char)value;
  }

  // One datagram's worth. DTLS keeps message boundaries, so a read returns
  // what one record carried, as a UDP read does: looping to fill 'len', the
  // way the TLS stream does, waited forever for records the peer never sent.
  static int ReadBytes(char* values, int len, DtlsSocketCtx* sctx) {
    int status;
    do {
      status = mbedtls_ssl_read(&sctx->ssl, (unsigned char*)values, len);
    }
    while(status == MBEDTLS_ERR_SSL_WANT_READ || status == MBEDTLS_ERR_SSL_WANT_WRITE);

    if(status == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
      return 0;
    }
    if(status < 0) {
      sctx->SetError(status);
      return -1;
    }
    return status;
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

     // create temporary file; its name is UTF-16, as is the command once the
     // caller's UTF-8 is decoded, so _wsystem sees both as written rather than
     // read through the ANSI code page
     const std::wstring tmp_file_name = File::TempName();
     FILE* file = File::FileOpen(tmp_file_name.c_str(), "wb");
     if(file) {
       fclose(file);

       // The temp path is quoted, so a temp directory with a space (a user
       // folder such as 'C:\Users\Ana Lima') stays one word. Then the whole
       // line is quoted once more: cmd /c drops a line's first and last quote
       // whenever it has more than two, so without the outer pair a command
       // that began with a quoted program path lost its opening quote.
       std::wstring str_cmd = L"\"";
       str_cmd += BytesToUnicode(c);
       str_cmd += L" > \"";
       str_cmd += tmp_file_name;
       str_cmd += L"\" 2>&1\"";
       status = _wsystem(str_cmd.c_str());

       // read file output
       std::ifstream file_out{std::filesystem::path(tmp_file_name)};
       if(file_out.is_open()) {
         std::string line_out;
         while(getline(file_out, line_out)) {
           output.push_back(line_out);
         }
         file_out.close();

         // delete file
         _wremove(tmp_file_name.c_str());
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
