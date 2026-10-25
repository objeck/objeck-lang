/***************************************************************************
 * Provides runtime support for POSIX compliant systems i.e 
 * Linux, OS X, etc.
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

#ifndef __POSIX_H__
#define __POSIX_H__

#include "../../common.h"
#include <stdlib.h>
#include <sys/utsname.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/un.h>
#include <pwd.h>
#include <grp.h>

#define SOCKET int

/****************************
 * File-system names
 *
 * POSIX names are bytes, which Objeck reads and writes as UTF-8; win32.h maps
 * the same two calls to UTF-16 for the W APIs.
 ****************************/
typedef std::string NativePath;

inline NativePath ToNativePath(const wchar_t* name) {
  return UnicodeToBytes(name);
}

inline std::wstring FromNativePath(const NativePath& name) {
  return BytesToUnicode(name);
}

/****************************
 * File support class
 ****************************/
class File {
 public:
  static std::string FullPathName(const std::string name) {
    char buffer[PATH_MAX] = "";
    if(!realpath(name.c_str(), buffer)) {
      return "";
    }
    
    return buffer;
  }
  
  static long FileSize(const char* name) {
    struct stat buf;
    if(stat(name, &buf)) {
      return -1;
    }
    
    return buf.st_size;
  }

  static std::string TempName() {
    char buffer[] = "/tmp/objeck-XXXXXX";
    const int fd = mkstemp(buffer);
    if(fd < 0) {
      return "";
    }
    // mkstemp returns an open descriptor; only the name is wanted here, and
    // every CommandOutput() call leaked one fd.
    close(fd);
    
    return buffer;
  }
  
  static bool FileExists(const char* name) {
    struct stat buf;
    if(stat(name, &buf)) {
      return false;
    }

    return S_IFREG & buf.st_mode;
  }
  
  static bool FileReadOnly(const char* name) {
    if(!access(name, R_OK) && access(name, W_OK) == EACCES) {
      return true;
    }
    
    return false;
  }
  
  static bool FileWriteOnly(const char* name) {
    if(!access(name, W_OK) && access(name, R_OK) == EACCES) {
      return true;
    }
    
    return false;
  }

  static bool FileReadWrite(const char* name) {
    if (!access(name, W_OK) && !access(name, R_OK)) {
      return true;
    }

    return false;
  }

  static time_t FileCreatedTime(const char* name) {
    struct stat buf;
    if(stat(name, &buf)) {
      return -1;
    }

    return buf.st_ctime;
  }

  static time_t FileModifiedTime(const char* name) {
    struct stat buf;
    if(stat(name, &buf)) {
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

  static FILE* FileOpen(const char* name, const char* mode) {
    return fopen(name, mode);
  }

  static std::wstring FileOwner(const char* name, bool is_account) {
    struct stat info;
    if(stat(name, &info)) {
      return L"";
    }

    // No passwd/group entry (containers, NFS, a deleted user) returns NULL;
    // dereferencing it segfaulted. Windows returns an empty owner; match it.
    if(is_account) {
      struct passwd* account = getpwuid(info.st_uid);
      return account ? BytesToUnicode(account->pw_name) : L"";
    }
        
    struct group * group = getgrgid(info.st_gid);
    return group ? BytesToUnicode(group->gr_name) : L"";
  }
  
  static bool MakeDir(const char* name) {
    if(mkdir(name, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) < 0) {
      return false;
    }
    
    return true;
  }

  static bool DirExists(const char* name) {
    struct stat buf;
    if(stat(name, &buf)) {
      return false;
    }
    
    return S_IFDIR & buf.st_mode;
  }

  static  std::vector<std::string> ListDir(const char* path) {
     std::vector<std::string> files;

    struct dirent** names = nullptr;
    int n = scandir(path, &names, 0, alphasort);
    if(n > 0) {
      while(n--) {
        if((strcmp(names[n]->d_name, "..") != 0) && (strcmp(names[n]->d_name, ".") != 0)) {
          files.push_back(names[n]->d_name);
        }
        free(names[n]);
      }
      free(names);
    }

    return files;
  }
};

/****************************
 * Pipe support class
 ****************************/
class Pipe {
public:
  static bool Create(const char* name, int& client_pipe) {
    client_pipe = 0;

    int server_pipe = socket(AF_UNIX, SOCK_STREAM, 0);
    if(server_pipe < 0) {
      return false;
    }

    unlink(name);
    
    struct sockaddr_un server_addr;
    memset(&server_addr, 0, sizeof(struct sockaddr_un));
    server_addr.sun_family = AF_UNIX;   
    strncpy(server_addr.sun_path, name, sizeof(server_addr.sun_path) - 1);
    
    const int len = sizeof(server_addr);
    if(bind(server_pipe, (struct sockaddr*) &server_addr, len) < 0) {
      close(server_pipe);
      return false;
    }

    if(listen(server_pipe, 4) < 0){ 
      close(server_pipe);
      return false;
    }
    
    struct sockaddr_un client_addr;
    memset(&client_addr, 0, sizeof(struct sockaddr_un));
    
    socklen_t client_len = 0;
    client_pipe = accept(server_pipe, (struct sockaddr*)&client_addr, &client_len);
    if(client_pipe < 0) {
      client_pipe = 0;
      close(server_pipe);
      return false;
    }
    close(server_pipe);
    
    return true;
  }
  
  static bool Open(const char* name, int& client_pipe) {
    client_pipe = socket(AF_UNIX, SOCK_STREAM, 0);
    if(client_pipe < 0) {
      return false;
    }
    
    struct sockaddr_un client_addr;
    memset(&client_addr, 0, sizeof(struct sockaddr_un));
    client_addr.sun_family = AF_UNIX;   
    strncpy(client_addr.sun_path, name, sizeof(client_addr.sun_path) - 1);
    
    const int len = sizeof(client_addr);
    if(connect(client_pipe, (struct sockaddr*)&client_addr, len) < 0) {
		close(client_pipe);
      return false;
    }
    
    return true;
  }
  
  static void Close(int pipe) {
    close(pipe);
  }
  
  static bool ReadByte(char &value, int pipe) {
    if(recv(pipe, &value, 1, 0) != 1) {
        return false;
    }
    
    return true;
  }

  static size_t ReadByteArray(char* buffer, size_t offset, size_t num, int pipe) {
    return recv(pipe, buffer + offset, num, 0);
  }

  static size_t WriteByteArray(const char* buffer, size_t offset, size_t num, int pipe) {
    return send(pipe, buffer + offset, num, 0);
  }
  
  static bool WriteByte(char value, int pipe) {
    // send() returns -1 on failure, which converted to bool is TRUE, so a
    // failed write reported success straight onto the Objeck stack. Windows
    // returns written == 1; match it.
    return send(pipe, &value, 1, 0) == 1;
  }
  
  static std::string ReadString(int pipe) {
    char buffer[MID_BUFFER_MAX];
    size_t buffer_index = 0;

    char value;
    bool done = false;
    do {
      if(recv(pipe, &value, 1, 0) != 1) {
        done = true;
      }
      else if(value != '\0' && value != '\r' && value != '\n') {
        buffer[buffer_index++] = value;
      }
      else {
        done = true;
      }
    } 
    while(!done && buffer_index < MID_BUFFER_MAX - 1);
    buffer[buffer_index] = '\0';

    if(value == '\r') {
      recv(pipe, &value, 1, 0);
    }

    return buffer;
  }

  static bool WriteString(const std::string& line, int pipe) {
    const size_t num = line.size();
    return send(pipe, line.c_str(), num, 0) == (int)num;
  }
};

/****************************
 * IP socket support class
 ****************************/
class IPSocket {
public:
  static  std::vector<std::string> Resolve(const char* address) {
    Startup();
    std::vector<std::string> addresses;

    // getaddrinfo does not assign result unless it succeeds, so freeing it on the
    // failure path handed freeaddrinfo an uninitialised pointer.
    struct addrinfo* result = nullptr;
    if(getaddrinfo(address, nullptr, nullptr, &result)) {
      return  std::vector<std::string>();
    }

    struct addrinfo* res;
    for(res = result; res != nullptr; res = res->ai_next) {
      char hostname[NI_MAXHOST] = { 0 };
      if(getnameinfo(res->ai_addr, (socklen_t)res->ai_addrlen, hostname, NI_MAXHOST, nullptr, 0, 0)) {
	freeaddrinfo(result);
	return  std::vector<std::string>();
      }

      if(*hostname != '\0') {
	addresses.push_back(hostname);
      }
    }

    freeaddrinfo(result);
    return addresses;
  }
  
  //
  // Readies the process for sockets; called before each socket the VM opens.
  // A write to a peer that has gone away raises SIGPIPE, which ends the
  // process, rather than failing with EPIPE. mbedtls_net_connect ignores the
  // signal, process-wide, the first time it runs (its net_prepare), and the VM
  // had relied on that without knowing: only a process that had made a TLS
  // client connect survived a write to a closed peer. It is ignored here, once,
  // for every socket -- a client's connect, a server's bind, UDP -- so the
  // write fails and GetLastError says "Broken pipe".
  //
  static void Startup() {
    static const bool ignored = (signal(SIGPIPE, SIG_IGN), true);
    (void)ignored;
  }

  //
  // A connected client socket: each address the name resolves to is tried in
  // turn (IPv6 and IPv4 alike), and the first that connects is returned. A
  // timeout_ms above 0 bounds each attempt; 0 blocks. On failure returns -1
  // with the reason in error, captured as each attempt failed: errno means
  // nothing once the cleanup close() and the next attempt have run. When every
  // address failed the reason is the most informative one: an attempt the
  // network answered (refused, reset, timed out) over one that never left this
  // host (no route, an address family it lacks), and among answers of a kind
  // the first, the address the resolver preferred. A name that does not
  // resolve is a resolver error. type is SOCK_STREAM or SOCK_DGRAM.
  //
  static SOCKET Connect(const char* address, int port, int type, int timeout_ms, SocketError& error) {
    Startup();
    error = { 0, false };

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = type;
    hints.ai_protocol = type == SOCK_DGRAM ? IPPROTO_UDP : IPPROTO_TCP;

    struct addrinfo* result = nullptr;
    const std::string port_str = std::to_string(port);
    const int status = getaddrinfo(address, port_str.c_str(), &hints, &result);
    if(status != 0) {
      // the EAI_* codes are not errno values; EAI_SYSTEM means errno says why
      if(status == EAI_SYSTEM && errno != 0) {
        error = { errno, false };
      }
      else {
        error = { status, true };
      }
      return -1;
    }

    SOCKET sock = -1;
    int kept_rank = 0;
    for(struct addrinfo* ptr = result; ptr != nullptr && sock < 0; ptr = ptr->ai_next) {
      int attempt_error = 0;
      sock = ConnectAttempt(ptr, timeout_ms, attempt_error);
      if(sock < 0) {
        const int rank = ConnectErrorRank(attempt_error);
        if(rank > kept_rank) {
          kept_rank = rank;
          error = { attempt_error, false };
        }
      }
    }
    freeaddrinfo(result);

    if(sock >= 0) {
      // an earlier address that failed is not this connect's failure
      error = { 0, false };
    }
    else if(error.code == 0) {
      error = { EADDRNOTAVAIL, false };
    }

    return sock;
  }

  // A TCP client socket (Connect), with the default 30 second receive timeout
  static SOCKET Open(const char* address, int port, int timeout_ms, SocketError& error) {
    const SOCKET sock = Connect(address, port, SOCK_STREAM, timeout_ms, error);

    // Set default receive timeout (30 seconds)
    if(sock >= 0) {
      struct timeval tv;
      tv.tv_sec = 30;
      tv.tv_usec = 0;
      // Best-effort default, left non-fatal: SO_RCVTIMEO is not honored on every
      // platform and the connection is otherwise usable. The consequence is worth
      // knowing though -- without it a recv here can block indefinitely, so a
      // caller that needs the guarantee should use SetReceiveTimeout, which
      // reports failure.
      if(setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        errno = 0;
      }
    }

    return sock;
  }

  // Closes a socket that failed to bind or listen without losing why: errno
  // is only meaningful straight after the failing call, and the caller reads
  // it after this cleanup.
  static void CloseKeepingError(SOCKET sock) {
    const int error = errno;
    close(sock);
    errno = error;
  }

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
  static SOCKET BindAny(int port, int type) {
    Startup();
    const int protocol = type == SOCK_DGRAM ? IPPROTO_UDP : IPPROTO_TCP;
    int reuse = 1;

    SOCKET server = socket(AF_INET6, type, protocol);
    if(server >= 0) {
      int v6_only = 0;
      if(setsockopt(server, IPPROTO_IPV6, IPV6_V6ONLY, &v6_only, sizeof(v6_only)) < 0) {
        close(server);
        server = -1;
      }
    }

    if(server >= 0) {
      // non-fatal, as in Bind: a restart inside TIME_WAIT then fails at bind()
      if(setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        errno = 0;
      }

      struct sockaddr_in6 sin6;
      memset(&sin6, 0, sizeof(sin6));
      sin6.sin6_family = AF_INET6;
      sin6.sin6_addr = in6addr_any;
      sin6.sin6_port = htons(port);
      if(::bind(server, (struct sockaddr*)&sin6, sizeof(sin6)) < 0) {
        CloseKeepingError(server);
        return -1;
      }

      return server;
    }

    // no IPv6 stack
    server = socket(AF_INET, type, protocol);
    if(server < 0) {
      return -1;
    }
    if(setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
      errno = 0;
    }

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(port);
    if(::bind(server, (struct sockaddr*)&sin, sizeof(sin)) < 0) {
      CloseKeepingError(server);
      return -1;
    }

    return server;
  }

  //
  // A server socket bound to one local address: a literal ("127.0.0.1",
  // "::1") or a name resolved with getaddrinfo. An empty or null host is the
  // dual-stack wildcard (BindAny). A name can resolve to several addresses
  // and one socket binds one, so this binds the first IPv4 address that
  // binds, else the first IPv6 one: "localhost" is 127.0.0.1 on every
  // platform, whatever order the resolver lists its addresses in. An IPv6
  // address is bound IPv6-only, so "::1" takes ::1 clients and nothing else.
  // On failure returns -1 with the reason in error (a name that does not
  // resolve is a resolver error). A name lookup can block: the caller parks
  // around it.
  //
  static SOCKET BindAddress(const char* host, int port, int type, SocketError& error) {
    Startup();
    error = { 0, false };
    if(!host || !*host) {
      // BindAny keeps its failure in errno across its cleanup
      const SOCKET server = BindAny(port, type);
      if(server < 0) {
        error = { errno, false };
      }
      return server;
    }

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = type;
    hints.ai_protocol = type == SOCK_DGRAM ? IPPROTO_UDP : IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    struct addrinfo* result = nullptr;
    const std::string port_str = std::to_string(port);
    const int status = getaddrinfo(host, port_str.c_str(), &hints, &result);
    if(status != 0) {
      // the EAI_* codes are not errno values; EAI_SYSTEM means errno says why
      if(status == EAI_SYSTEM && errno != 0) {
        error = { errno, false };
      }
      else {
        error = { status, true };
      }
      return -1;
    }

    SOCKET server = -1;
    int last_error = EADDRNOTAVAIL;
    const int families[] = { AF_INET, AF_INET6 };
    for(int i = 0; i < 2 && server < 0; ++i) {
      for(struct addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
        if(ptr->ai_family != families[i]) {
          continue;
        }

        SOCKET sock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if(sock < 0) {
          last_error = errno;
          continue;
        }

        if(ptr->ai_family == AF_INET6) {
          // Linux defaults to off: then "::" would take IPv4 clients too
          int v6_only = 1;
          if(setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, &v6_only, sizeof(v6_only)) < 0) {
            errno = 0;
          }
        }

        // non-fatal, as in BindAny
        int reuse = 1;
        if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
          errno = 0;
        }

        if(::bind(sock, ptr->ai_addr, ptr->ai_addrlen) < 0) {
          last_error = errno;
          close(sock);
          continue;
        }

        server = sock;
        break;
      }
    }
    freeaddrinfo(result);

    if(server < 0) {
      error = { last_error, false };
    }

    return server;
  }

  //
  // A peer's numeric address and port. An IPv4 client of a dual-stack server
  // arrives as ::ffff:a.b.c.d and is reported as a.b.c.d, as it always was.
  //
  static void FormatAddress(const struct sockaddr* addr, char* buffer, size_t buffer_size, int& port) {
    buffer[0] = '\0';
    port = -1;
    if(addr->sa_family == AF_INET6) {
      const struct sockaddr_in6* addr6 = (const struct sockaddr_in6*)addr;
      if(IN6_IS_ADDR_V4MAPPED(&addr6->sin6_addr)) {
        inet_ntop(AF_INET, (const void*)&addr6->sin6_addr.s6_addr[12], buffer, buffer_size);
      }
      else {
        inet_ntop(AF_INET6, (const void*)&addr6->sin6_addr, buffer, buffer_size);
      }
      port = ntohs(addr6->sin6_port);
    }
    else if(addr->sa_family == AF_INET) {
      const struct sockaddr_in* addr4 = (const struct sockaddr_in*)addr;
      inet_ntop(AF_INET, (const void*)&addr4->sin_addr, buffer, buffer_size);
      port = ntohs(addr4->sin_port);
    }
  }

  static SOCKET Bind(int port) {
    return BindAny(port, SOCK_STREAM);
  }

  static bool Listen(SOCKET server, int backlog) {
    if(listen(server, backlog) < 0) {
      return false;
    }
    
    return true;
  }
  
  static SOCKET Accept(SOCKET server, char* client_address, int &client_port) {
    struct sockaddr_storage pin;
    socklen_t addrlen = sizeof(pin);

    SOCKET client = accept(server, (struct sockaddr*)&pin, &addrlen);
    if(client < 0) {
      client_address[0] = '\0';
      client_port = -1;
      return -1;
    }
    
    // Same 30s default receive timeout Open() gives a connected socket. An
    // accepted socket had none, so a read on it blocked forever -- harmless
    // while a handler served one request and closed, but a server that holds
    // the connection open for a second request would park its thread on an
    // idle peer permanently. Best-effort: the connection is usable either way.
    struct timeval timeout;
    timeout.tv_sec = 30;
    timeout.tv_usec = 0;
    if(setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
      errno = 0;
    }

    // client_address holds INET6_ADDRSTRLEN (see SockTcpAccept)
    FormatAddress((struct sockaddr*)&pin, client_address, INET6_ADDRSTRLEN, client_port);

    return client;
  }
  
  // Writes, reads: error is the call's errno, or 0 when it worked
  static int WriteByte(const char value, SOCKET sock, int& error) {
    const int status = static_cast<int>(send(sock, &value, 1, 0));
    error = status < 0 ? errno : 0;
    return status;
  }
  
  static int WriteBytes(const char* values, int len, SOCKET sock, int& error) {
    // send() on a blocking socket is only obliged to accept SOME of the buffer.
    // Once the kernel send buffer fills it returns a short count, and a single
    // unlooped send() drops the remainder on the floor -- no error, no retry,
    // and callers here either push that partial count into Objeck or discard it
    // entirely (SockTcpOutString ignores the return). ReadBytes below has always
    // looped, and so has the TLS WriteBytes; only plain TCP did not.
    //
    // The buffer fills exactly when the peer is slow to drain it, which is the
    // normal case for an Objeck client and an Objeck server sharing a process:
    // the reader is not scheduled while the writer runs. That is why this showed
    // up as a large in-process transfer arriving short or not at all.
    //
    // A failure part way still returns what was sent, with the reason in error.
    error = 0;
    int total = 0;
    while(total < len) {
      const int status = static_cast<int>(send(sock, values + total, len - total, 0));
      if(status < 0) {
        error = errno;
        return total > 0 ? total : -1;
      }
      if(status == 0) {
        break;
      }
      total += status;
    }

    return total;
  }
  
  static char ReadByte(SOCKET sock, int &status, int& error) {
    char value;
    status = static_cast<int>(recv(sock, &value, 1, 0));
    if(status < 0) {
      error = errno;
      return '\0';
    }

    error = 0;
    return value;
  }

  // a failure part way still returns what was read, with the reason in error
  static int ReadBytes(char* values, int len, SOCKET sock, int& error) {
    error = 0;
    int total = 0;
    while(total < len) {
      int status = static_cast<int>(recv(sock, values + total, len - total, 0));
      if(status < 0) {
        error = errno;
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
    // A bare close() straight after send() can drop whatever is still queued in
    // the kernel send buffer, so the HTTP server's "write the response, close
    // the socket" sequence delivers nothing and the peer sees a reset with zero
    // bytes read. shutdown() hands the queued bytes off and sends FIN before the
    // descriptor goes away. Errors are ignored on purpose: the peer may already
    // have torn the connection down, and there is nothing useful to do here.
    shutdown(sock, SHUT_WR);
    close(sock);
  }

  static bool SetKeepAlive(SOCKET sock, bool enable) {
    int val = enable ? 1 : 0;
    return setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val)) == 0;
  }

  static bool SetNoDelay(SOCKET sock, bool enable) {
    int val = enable ? 1 : 0;
    return setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val)) == 0;
  }

  static bool SetRecvTimeout(SOCKET sock, int ms) {
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    return setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0;
  }

  static bool SetSendTimeout(SOCKET sock, int ms) {
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    return setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0;
  }

  static bool SetRecvBufSize(SOCKET sock, int bytes) {
    return setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &bytes, sizeof(bytes)) == 0;
  }

  static bool SetSendBufSize(SOCKET sock, int bytes) {
    return setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &bytes, sizeof(bytes)) == 0;
  }

private:
  // One connect attempt to one resolved address; -1 with the reason in error
  // when it fails, taken before the close() that cleans up.
  static SOCKET ConnectAttempt(const struct addrinfo* addr, int timeout_ms, int& error) {
    SOCKET sock = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
    if(sock < 0) {
      error = errno;
      return -1;
    }

    if(timeout_ms <= 0) {
      if(connect(sock, addr->ai_addr, addr->ai_addrlen) < 0) {
        error = errno;
        close(sock);
        return -1;
      }
      return sock;
    }

    // Bounded: connect without blocking, then wait up to timeout_ms. F_GETFL
    // is checked as well: -1 | O_NONBLOCK is still -1, which would request
    // every flag bit here and then be "restored" onto the socket after.
    const int flags = fcntl(sock, F_GETFL, 0);
    if(flags < 0 || fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0) {
      error = errno;
      close(sock);
      return -1;
    }

    if(connect(sock, addr->ai_addr, addr->ai_addrlen) < 0) {
      if(errno != EINPROGRESS) {
        error = errno;
        close(sock);
        return -1;
      }

      // poll, not select: select cannot take a descriptor at or above FD_SETSIZE
      struct pollfd pfd;
      pfd.fd = sock;
      pfd.events = POLLOUT;
      pfd.revents = 0;
      int ready;
      do {
        ready = poll(&pfd, 1, timeout_ms);
      }
      while(ready < 0 && errno == EINTR);

      if(ready == 0) {
        error = ETIMEDOUT;
        close(sock);
        return -1;
      }
      if(ready < 0) {
        error = errno;
        close(sock);
        return -1;
      }

      // SO_ERROR is the connect's outcome. Unchecked, a failing getsockopt
      // leaves so_error at 0 and a connect that never completed reads as
      // successful.
      int so_error = 0;
      socklen_t len = sizeof(so_error);
      if(getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len) < 0) {
        error = errno;
        close(sock);
        return -1;
      }
      if(so_error != 0) {
        error = so_error;
        close(sock);
        return -1;
      }
    }

    // Back to blocking. A failed restore leaves the socket non-blocking, so a
    // caller expecting a blocking read gets spurious EAGAIN instead of data --
    // fail the attempt rather than hand back that socket.
    if(fcntl(sock, F_SETFL, flags) < 0) {
      error = errno;
      close(sock);
      return -1;
    }

    return sock;
  }

  // how much a failed attempt's reason says (see Connect)
  static int ConnectErrorRank(int error) {
    switch(error) {
    case ECONNREFUSED:
    case ECONNRESET:
    case ETIMEDOUT:
      return 2;

    default:
      return 1;
    }
  }
};

/****************************
 * UDP socket support class
 ****************************/
class UDPSocket {
public:
  static bool Bind(int port, SOCKET& sock, struct sockaddr_in*& addr_in) {
    IPSocket::Startup();
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(sock > -1) {
      addr_in = new struct sockaddr_in;
      addr_in->sin_family = AF_INET;
      addr_in->sin_port = htons(port);
      addr_in->sin_addr.s_addr = htonl(INADDR_ANY);

      if(bind(sock, (struct sockaddr*)addr_in, sizeof(struct sockaddr_in)) < 0) {
        IPSocket::CloseKeepingError(sock);
        sock = -1;

        delete addr_in;
        addr_in = nullptr;

        return false;
      }

      return true;
    }

    return false;
  }

  static void Close(SOCKET sock, struct sockaddr_in* addr_in) {
    close(sock);

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
      const char* v = getenv("OBJECK_TLS_INSECURE_SKIP_VERIFY");
      cached = (v && (v[0] == '1' || v[0] == 't' || v[0] == 'T' || v[0] == 'y' || v[0] == 'Y')) ? 1 : 0;
    }
    return cached == 1;
  }

  //
  // Connects net for mbedTLS as mbedtls_net_connect does -- each address the
  // name resolves to, in turn -- returning 0 or the error it would return, with
  // the OS reason in reason. mbedtls_net_connect closes each failed attempt
  // before trying the next, and the reason does not survive it: a refused TLS
  // connect could only say "the connection failed".
  //
  static int ConnectNet(mbedtls_net_context& net, const char* address, int port, int type, SocketError& reason) {
    const SOCKET sock = IPSocket::Connect(address, port, type, 0, reason);
    if(sock < 0) {
      return reason.resolver ? MBEDTLS_ERR_NET_UNKNOWN_HOST : MBEDTLS_ERR_NET_CONNECT_FAILED;
    }

    net.fd = (int)sock;
    return 0;
  }

  static bool OpenH2(const char* address, int port, std::string& pem_file, SecureSocketCtx*& sctx) {
    sctx = new SecureSocketCtx();

    const char* pers = "objeck_ssl_h2_client";
    int ret = mbedtls_ctr_drbg_seed(&sctx->ctr_drbg, mbedtls_entropy_func, &sctx->entropy,
                                     (const unsigned char*)pers, strlen(pers));
    if(ret != 0) { sctx->SetError(ret); delete sctx; sctx = nullptr; return false; }

    std::string cert_path = pem_file.empty()
      ? UnicodeToBytes(GetLibraryPath()) + CACERT_PEM_FILE : pem_file;
    ret = mbedtls_x509_crt_parse_file(&sctx->cacert, cert_path.c_str());
    if(ret < 0) { sctx->SetError(ret); delete sctx; sctx = nullptr; return false; }

    SocketError reason;
    ret = ConnectNet(sctx->net, address, port, SOCK_STREAM, reason);
    if(ret != 0) { sctx->SetError(ret, reason); delete sctx; sctx = nullptr; return false; }

    ret = mbedtls_ssl_config_defaults(&sctx->conf, MBEDTLS_SSL_IS_CLIENT,
                                       MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if(ret != 0) { sctx->SetError(ret); delete sctx; sctx = nullptr; return false; }

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

    // ALPN: prefer h2, fallback to http/1.1
    static const char* alpn_list[] = { "h2", "http/1.1", nullptr };
    mbedtls_ssl_conf_alpn_protocols(&sctx->conf, alpn_list);

    ret = mbedtls_ssl_setup(&sctx->ssl, &sctx->conf);
    if(ret != 0) { sctx->SetError(ret); delete sctx; sctx = nullptr; return false; }

    ret = mbedtls_ssl_set_hostname(&sctx->ssl, address);
    if(ret != 0) { sctx->SetError(ret); delete sctx; sctx = nullptr; return false; }

    mbedtls_ssl_set_bio(&sctx->ssl, &sctx->net, mbedtls_net_send, mbedtls_net_recv, nullptr);

    while((ret = mbedtls_ssl_handshake(&sctx->ssl)) != 0) {
      if(ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
        sctx->SetError(ret); delete sctx; sctx = nullptr; return false;
      }
    }

    // Verify h2 was negotiated
    const char* alpn = mbedtls_ssl_get_alpn_protocol(&sctx->ssl);
    if(!alpn || strcmp(alpn, "h2") != 0) {
      // the server does not speak HTTP/2; this failure was the only one unrecorded
#ifdef MBEDTLS_ERR_SSL_NO_APPLICATION_PROTOCOL
      sctx->SetError(MBEDTLS_ERR_SSL_NO_APPLICATION_PROTOCOL);
#else
      sctx->SetError(MBEDTLS_ERR_SSL_FEATURE_UNAVAILABLE);
#endif
      delete sctx; sctx = nullptr; return false;
    }

    return true;
  }

  static bool Open(const char* address, int port, std::string& pem_file, SecureSocketCtx*& sctx) {
    sctx = new SecureSocketCtx();

    // Seed the random number generator
    const char* pers = "objeck_ssl_client";
    int ret = mbedtls_ctr_drbg_seed(&sctx->ctr_drbg, mbedtls_entropy_func, &sctx->entropy,
                                     (const unsigned char*)pers, strlen(pers));
    if(ret != 0) {
      sctx->SetError(ret);
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
      sctx->SetError(ret);
      delete sctx;
      sctx = nullptr;
      return false;
    }

    // Connect to server
    std::string addr_str = address;
    if(addr_str.size() < 1 || port < 0) {
      // recorded: this failure alone left GetLastError() at Nil
      sctx->SetError(MBEDTLS_ERR_NET_BAD_INPUT_DATA);
      delete sctx;
      sctx = nullptr;
      return false;
    }

    SocketError reason;
    ret = ConnectNet(sctx->net, address, port, SOCK_STREAM, reason);
    if(ret != 0) {
      sctx->SetError(ret, reason);
      delete sctx;
      sctx = nullptr;
      return false;
    }

    // Configure SSL
    ret = mbedtls_ssl_config_defaults(&sctx->conf, MBEDTLS_SSL_IS_CLIENT,
                                       MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if(ret != 0) {
      sctx->SetError(ret);
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
      sctx->SetError(ret);
      delete sctx;
      sctx = nullptr;
      return false;
    }

    // Set SNI hostname
    ret = mbedtls_ssl_set_hostname(&sctx->ssl, address);
    if(ret != 0) {
      sctx->SetError(ret);
      delete sctx;
      sctx = nullptr;
      return false;
    }

    mbedtls_ssl_set_bio(&sctx->ssl, &sctx->net, mbedtls_net_send, mbedtls_net_recv, nullptr);

    // Perform TLS handshake
    while((ret = mbedtls_ssl_handshake(&sctx->ssl)) != 0) {
      if(ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
        sctx->SetError(ret);
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
        sctx->SetError(MBEDTLS_ERR_X509_CERT_VERIFY_FAILED);
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
      sctx->SetError(ret);
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
        sctx->SetError(ret);
        return -1;
      }
      written += ret;
    }
    return written;
  }

  static char ReadByte(SecureSocketCtx* sctx, int &status) {
    unsigned char value;
    status = mbedtls_ssl_read(&sctx->ssl, &value, 1);
    // These sockets are blocking.  When SO_RCVTIMEO expires, mbedtls_net_recv
    // maps EAGAIN/EWOULDBLOCK to WANT_READ; retrying here would turn a bounded
    // read into an infinite loop.  WANT_WRITE can arise from TLS protocol work
    // while reading, so only that condition is safe to retry.
    while(status == MBEDTLS_ERR_SSL_WANT_WRITE) {
      status = mbedtls_ssl_read(&sctx->ssl, &value, 1);
    }
    if(status <= 0) {
      if(status < 0) {
        sctx->SetError(status);
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
        // On a blocking POSIX socket WANT_READ is also how mbedtls_net_recv
        // reports expiry of SO_RCVTIMEO.  Propagate it so callers can honor
        // their timeout instead of spinning forever.
        if(status == MBEDTLS_ERR_SSL_WANT_WRITE) {
          continue;
        }
        sctx->SetError(status);
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
    // ver: 12 = TLS 1.2, 13 = TLS 1.3
#if defined(MBEDTLS_VERSION_NUMBER) && MBEDTLS_VERSION_NUMBER >= 0x03000000
    mbedtls_ssl_conf_min_tls_version(&sctx->conf, (mbedtls_ssl_protocol_version)ver);
#else
    int minor = (ver == 13) ? MBEDTLS_SSL_MINOR_VERSION_4 : MBEDTLS_SSL_MINOR_VERSION_3;
    mbedtls_ssl_conf_min_version(&sctx->conf, MBEDTLS_SSL_MAJOR_VERSION_3, minor);
#endif
    return true;
  }

  // Verification is fixed when the handshake runs, in New(): REQUIRED unless
  // the process is in insecure test mode (OBJECK_TLS_INSECURE_SKIP_VERIFY).
  // This used to set the mode of an already-verified connection, where
  // strict=false read as "verification off" but changed nothing that
  // mattered -- and would have weakened any later handshake on the same
  // context. It now only confirms: true when verification is required,
  // false (and nothing changed) when asked to relax it.
  static bool SetVerifyPeer(SecureSocketCtx* sctx, bool strict) {
    if(!sctx) return false;
    return strict && !InsecureSkipVerify();
  }

  static std::string GetCertFingerprint(SecureSocketCtx* sctx) {
    if(!sctx) return "";
    const mbedtls_x509_crt* cert = mbedtls_ssl_get_peer_cert(&sctx->ssl);
    if(!cert) return "";

    unsigned char hash[32];
    const mbedtls_md_info_t* md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if(!md_info || mbedtls_md(md_info, cert->raw.p, cert->raw.len, hash) != 0) return "";

    char hex[65];
    for(int i = 0; i < 32; i++) {
      snprintf(hex + i * 2, 3, "%02x", hash[i]);
    }
    return std::string(hex, 64);
  }

  static bool SetKeepAlive(SecureSocketCtx* sctx, bool enable) {
    if(!sctx) return false;
    int val = enable ? 1 : 0;
    return setsockopt(sctx->net.fd, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val)) == 0;
  }

  static bool SetNoDelay(SecureSocketCtx* sctx, bool enable) {
    if(!sctx) return false;
    int val = enable ? 1 : 0;
    return setsockopt(sctx->net.fd, IPPROTO_TCP, TCP_NODELAY, &val, sizeof(val)) == 0;
  }

  static bool SetRecvTimeout(SecureSocketCtx* sctx, int ms) {
    if(!sctx) return false;
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    return setsockopt(sctx->net.fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0;
  }

  static bool SetSendTimeout(SecureSocketCtx* sctx, int ms) {
    if(!sctx) return false;
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    return setsockopt(sctx->net.fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0;
  }

  static bool SetRecvBufSize(SecureSocketCtx* sctx, int bytes) {
    if(!sctx) return false;
    return setsockopt(sctx->net.fd, SOL_SOCKET, SO_RCVBUF, &bytes, sizeof(bytes)) == 0;
  }

  static bool SetSendBufSize(SecureSocketCtx* sctx, int bytes) {
    if(!sctx) return false;
    return setsockopt(sctx->net.fd, SOL_SOCKET, SO_SNDBUF, &bytes, sizeof(bytes)) == 0;
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

    ret = mbedtls_x509_crt_parse_file(&sctx->cacert, cert_path.c_str());
    if(ret < 0) {
      sctx->SetError(ret);
      std::wcerr << L">>> Unable to find/read cryptographic PEM file : '" << BytesToUnicode(cert_path) << L"' <<<" << std::endl;
      delete sctx;
      sctx = nullptr;
      return false;
    }

    std::string addr_str = address;
    if(addr_str.size() < 1 || port < 0) {
      // recorded: this failure alone left GetLastError() at Nil
      sctx->SetError(MBEDTLS_ERR_NET_BAD_INPUT_DATA);
      delete sctx;
      sctx = nullptr;
      return false;
    }

    SocketError reason;
    ret = IPSecureSocket::ConnectNet(sctx->net, address, port, SOCK_DGRAM, reason);
    if(ret != 0) {
      sctx->SetError(ret, reason);
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
   static  std::vector<std::string> CommandOutput(const char* c, int& status) {
      std::vector<std::string> output;

     // create temporary file
     const std::string tmp_file_name = File::TempName();
     FILE* file = File::FileOpen(tmp_file_name.c_str(), "wb");
     if(file) {
       fclose(file);

       std::string str_cmd(c);
       // quoted, so a temp path with a space stays one word (mkstemp's
       // template has no quote of its own)
       str_cmd += " > '";
       str_cmd += tmp_file_name;
       str_cmd += "'";
       // Windows appends 2>&1, so Runtime->Command() captured stderr there and
       // silently dropped it on Linux/macOS.
       str_cmd += " 2>&1";

       status = std::system(str_cmd.c_str());
       
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

  static std::string GetPlatform() {
    std::string platform;
    struct utsname uts;
    
    if(uname(&uts) < 0) {
      platform = "Unknown";
    }
    else {
      platform += uts.sysname;
      platform += " ";
      platform += uts.release;
      platform += ", ";
      platform += uts.machine;
    }
    
    return platform;
  }

  static size_t GetTotalSystemMemory()
  {
    const size_t pages = sysconf(_SC_PHYS_PAGES);
    const size_t page_size = sysconf(_SC_PAGE_SIZE);
    return pages * page_size;
  }
};

#endif
