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

#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>

#include "win32.h"

// see the declaration in win32.h
void IPSocket::Startup() {
  static const bool started = [] {
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
  }();
  (void)started;
}

// One connect attempt to one resolved address; INVALID_SOCKET with the reason
// in error when it fails. The reason is taken before the closesocket() that
// cleans up, which resets WSAGetLastError() to 0.
//
// An ephemeral port the system would not give, for the moment
//
// Windows picks the local port of a socket that asks for "any" (a bind to port 0, or the bind a connect
// makes for itself) and now and then fails that with WSAENOBUFS, "the system lacked sufficient buffer
// space or a queue was full", though ports are free: it happens when several processes are asking at
// once. Measured here with plain UDP sockets and no Objeck code: 0 of 4,000 with one process, about 1
// in 10,000 with six. Nothing is wrong with the socket's request and the next attempt gets a port, so
// the call is made again, a few times, a little later each time, before the failure is reported.
// (This is what made dtls_close_races.obs fail one round in a busy regression run: a DTLS client's
// connect, or a server's bind, of a brand new socket.)
//
static const int EPHEMERAL_PORT_TRIES = 5;

// whether attempt number 'tried' (from 1) that failed with 'error' is worth making again; waits first
static bool RetryEphemeralPort(int error, int tried) {
  if(error != WSAENOBUFS || tried >= EPHEMERAL_PORT_TRIES) {
    return false;
  }
  // 5, 10, 20, 40 ms: 75 ms in all before the failure is reported
  Sleep(5 << (tried - 1));
  return true;
}

// bind(), made again when the system would not give an ephemeral port for the moment (port 0 only: a
// named port that cannot be bound is the caller's answer). Leaves the failure in WSAGetLastError().
static int BindEphemeralRetrying(SOCKET sock, const struct sockaddr* addr, int addr_len, int port) {
  for(int tried = 1; ; ++tried) {
    if(::bind(sock, addr, addr_len) != SOCKET_ERROR) {
      return 0;
    }
    const int error = WSAGetLastError();
    if(port != 0 || !RetryEphemeralPort(error, tried)) {
      WSASetLastError(error);
      return SOCKET_ERROR;
    }
  }
}

static SOCKET ConnectAttempt(const struct addrinfo* addr, int timeout_ms, int& error) {
  SOCKET sock = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
  if(sock == INVALID_SOCKET) {
    error = WSAGetLastError();
    return INVALID_SOCKET;
  }

  if(timeout_ms <= 0) {
    if(connect(sock, addr->ai_addr, (int)addr->ai_addrlen) == SOCKET_ERROR) {
      error = WSAGetLastError();
      closesocket(sock);
      return INVALID_SOCKET;
    }
    return sock;
  }

  // bounded: connect without blocking, then wait up to timeout_ms
  u_long mode = 1;
  if(ioctlsocket(sock, FIONBIO, &mode) == SOCKET_ERROR) {
    error = WSAGetLastError();
    closesocket(sock);
    return INVALID_SOCKET;
  }

  if(connect(sock, addr->ai_addr, (int)addr->ai_addrlen) == SOCKET_ERROR) {
    const int connect_error = WSAGetLastError();
    if(connect_error != WSAEWOULDBLOCK) {
      error = connect_error;
      closesocket(sock);
      return INVALID_SOCKET;
    }

    // Winsock reports a connect that failed in the except set, not the write
    // set: waiting on the write set alone sat out the whole timeout after a
    // refusal, and then called it a timeout.
    fd_set wset, eset;
    FD_ZERO(&wset);
    FD_ZERO(&eset);
    FD_SET(sock, &wset);
    FD_SET(sock, &eset);
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    const int ready = select(0, nullptr, &wset, &eset, &tv);
    if(ready == 0) {
      error = WSAETIMEDOUT;
      closesocket(sock);
      return INVALID_SOCKET;
    }
    if(ready == SOCKET_ERROR) {
      error = WSAGetLastError();
      closesocket(sock);
      return INVALID_SOCKET;
    }

    // SO_ERROR is the connect's outcome. Unchecked, a failing getsockopt leaves
    // so_error at 0 and the pending connect would be treated as successful.
    int so_error = 0;
    int len = sizeof(so_error);
    if(getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&so_error, &len) == SOCKET_ERROR) {
      error = WSAGetLastError();
      closesocket(sock);
      return INVALID_SOCKET;
    }
    if(so_error != 0) {
      error = so_error;
      closesocket(sock);
      return INVALID_SOCKET;
    }
  }

  // Back to blocking. A failed restore leaves the socket non-blocking, so a
  // caller expecting a blocking read gets spurious WSAEWOULDBLOCK instead of
  // data -- fail the attempt rather than hand back that socket.
  mode = 0;
  if(ioctlsocket(sock, FIONBIO, &mode) == SOCKET_ERROR) {
    error = WSAGetLastError();
    closesocket(sock);
    return INVALID_SOCKET;
  }

  return sock;
}

// A socket connected to itself: its own address and port are the ones it connected
// to. The system picks a client's port from the same range a server's port of 0
// comes from, so a client sent to a port nobody holds any more can be given that
// very port. A UDP client then reads back the datagrams it sends, and a TCP client
// completes a connection with itself, where both should hear that nobody is there
// (100,000 UDP clients sent to a port just released on Linux: 5 read their own
// datagram).
static bool ConnectedToItself(SOCKET sock) {
  struct sockaddr_storage local, peer;
  socklen_t local_len = sizeof(local);
  socklen_t peer_len = sizeof(peer);
  memset(&local, 0, sizeof(local));
  memset(&peer, 0, sizeof(peer));
  if(getsockname(sock, (struct sockaddr*)&local, &local_len) != 0 || getpeername(sock, (struct sockaddr*)&peer, &peer_len) != 0) {
    return false;
  }
  if(local.ss_family != peer.ss_family) {
    return false;
  }
  if(local.ss_family == AF_INET) {
    const struct sockaddr_in* a = (const struct sockaddr_in*)&local;
    const struct sockaddr_in* b = (const struct sockaddr_in*)&peer;
    return a->sin_port == b->sin_port && memcmp(&a->sin_addr, &b->sin_addr, sizeof(a->sin_addr)) == 0;
  }
  if(local.ss_family == AF_INET6) {
    const struct sockaddr_in6* a = (const struct sockaddr_in6*)&local;
    const struct sockaddr_in6* b = (const struct sockaddr_in6*)&peer;
    return a->sin6_port == b->sin6_port && memcmp(&a->sin6_addr, &b->sin6_addr, sizeof(a->sin6_addr)) == 0;
  }
  return false;
}

// how much a failed attempt's reason says (see IPSocket::Connect in win32.h)
static int ConnectErrorRank(int error) {
  switch(error) {
  case WSAECONNREFUSED:
  case WSAECONNRESET:
  case WSAETIMEDOUT:
    return 2;

  default:
    return 1;
  }
}

// see the declaration in win32.h
SOCKET IPSocket::Connect(const char* address, int port, int type, int timeout_ms, SocketError& error, long long deadline_ms) {
  Startup();
  error = { 0, false };

  struct addrinfo hints;
  ZeroMemory(&hints, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = type;
  hints.ai_protocol = type == SOCK_DGRAM ? IPPROTO_UDP : IPPROTO_TCP;

  struct addrinfo* result = nullptr;
  const std::string port_str = std::to_string(port);
  const int status = getaddrinfo(address, port_str.c_str(), &hints, &result);
  if(status != 0) {
    // the EAI_* codes are WSA error codes on Windows ("No such host is known.").
    // No WSACleanup() here: WSAStartup runs once, in win_main, so a WSACleanup on
    // this path shut Winsock down for the WHOLE PROCESS over one failed lookup.
    error = { status, true };
    return INVALID_SOCKET;
  }

  SOCKET sock = INVALID_SOCKET;
  int kept_rank = 0;
  for(struct addrinfo* ptr = result; ptr != nullptr && sock == INVALID_SOCKET; ptr = ptr->ai_next) {
    int attempt_error = 0;
    int attempt_ms = timeout_ms;
    if(deadline_ms > 0) {
      const long long left = deadline_ms - SocketNowMs();
      if(left <= 0) {
        // out of time: no further address is tried, and this is the reason
        error = { WSAETIMEDOUT, false };
        kept_rank = 2;
        break;
      }
      if(attempt_ms <= 0 || left < attempt_ms) {
        attempt_ms = left > INT_MAX ? INT_MAX : (int)left;
      }
    }
    // (made again when the system would not give the socket a local port: see RetryEphemeralPort)
    for(int tried = 1; ; ++tried) {
      sock = ConnectAttempt(ptr, attempt_ms, attempt_error);
      if(sock != INVALID_SOCKET || !RetryEphemeralPort(attempt_error, tried)) {
        break;
      }
    }
    if(sock != INVALID_SOCKET && ConnectedToItself(sock)) {
      // connect again while the first socket still holds that port, so the system
      // must pick another: this one reaches the peer, or hears that there is none
      const SOCKET itself = sock;
      sock = ConnectAttempt(ptr, attempt_ms, attempt_error);
      closesocket(itself);
    }
    if(sock == INVALID_SOCKET) {
      const int rank = ConnectErrorRank(attempt_error);
      if(rank > kept_rank) {
        kept_rank = rank;
        error = { attempt_error, false };
      }
    }
  }
  freeaddrinfo(result);

  if(sock != INVALID_SOCKET) {
    // an earlier address that failed is not this connect's failure
    error = { 0, false };
  }
  else if(error.code == 0) {
    error = { WSAEADDRNOTAVAIL, false };
  }

  return sock;
}

// see the declaration in win32.h
SOCKET IPSocket::Open(const char* address, int port, int timeout_ms, SocketError& error, long long deadline_ms) {
  const SOCKET sock = Connect(address, port, SOCK_STREAM, timeout_ms, error, deadline_ms);

  // Set the default receive timeout
  if(sock != INVALID_SOCKET) {
    DWORD recv_timeout_ms = TCP_DEFAULT_RECV_TIMEOUT_MS;
    // Best-effort default, left non-fatal: the connection is otherwise usable. The
    // consequence is worth knowing though -- without it a recv here can block
    // indefinitely, so a caller needing the guarantee should use
    // SetReceiveTimeout, which reports failure.
    if(setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&recv_timeout_ms, sizeof(recv_timeout_ms)) == SOCKET_ERROR) {
      WSASetLastError(0);
    }
  }

  return sock;
}

std::vector<std::string> IPSocket::Resolve(const char* address) {
 Startup();
 std::vector<std::string> addresses;

	// getaddrinfo does not assign result unless it succeeds, so freeing it on the
	// failure path handed freeaddrinfo an uninitialised pointer.
	struct addrinfo* result = nullptr;
	if(getaddrinfo(address, nullptr, nullptr, &result)) {
		return std::vector<std::string>();
	}

	struct addrinfo* res;
	for(res = result; res != nullptr; res = res->ai_next) {
		char hostname[NI_MAXHOST] = {0};
		if(getnameinfo(res->ai_addr, (socklen_t)res->ai_addrlen, hostname, NI_MAXHOST, nullptr, 0, 0)) {
			freeaddrinfo(result);
			return std::vector<std::string>();
		}

		if(*hostname != '\0') {
			addresses.push_back(hostname);
		}
	}

	freeaddrinfo(result);
	return addresses;
}

// Closes a socket that failed to bind or listen without losing why: a
// successful closesocket() resets WSAGetLastError() to 0, so the caller read
// "The operation completed successfully." for a port that was already in use.
static void CloseKeepingError(SOCKET sock) {
  const int error = WSAGetLastError();
  closesocket(sock);
  WSASetLastError(error);
}

// see the declarations in win32.h
SOCKET IPSocket::BindAny(int port, int type, bool reuse_address) {
  Startup();
  const int protocol = type == SOCK_DGRAM ? IPPROTO_UDP : IPPROTO_TCP;
  int reuse = 1;

  SOCKET server = socket(AF_INET6, type, protocol);
  if(server != INVALID_SOCKET) {
    int v6_only = 0;
    if(setsockopt(server, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&v6_only, sizeof(v6_only)) == SOCKET_ERROR) {
      closesocket(server);
      server = INVALID_SOCKET;
    }
  }

  if(server != INVALID_SOCKET) {
    // Datagram servers only. On Windows SO_REUSEADDR does not mean what it
    // does on POSIX: it lets a second socket bind, and listen on, a port that
    // a server is already listening on, so a second TCP server "started" on a
    // taken port and the two split its clients. A TCP server needs no option
    // here to restart inside TIME_WAIT -- Windows only refuses a bind while
    // another socket is still bound to the port. A DTLS server does need it:
    // after each accept it binds a fresh listener on its port while the
    // accepted client keeps the old socket (see DtlsAccept).
    if(reuse_address && type == SOCK_DGRAM && setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) == SOCKET_ERROR) {
      WSASetLastError(0);
    }

    struct sockaddr_in6 sin6;
    memset(&sin6, 0, sizeof(sin6));
    sin6.sin6_family = AF_INET6;
    sin6.sin6_addr = in6addr_any;
    sin6.sin6_port = htons(port);
    if(BindEphemeralRetrying(server, (SOCKADDR*)&sin6, sizeof(sin6), port) == SOCKET_ERROR) {
      CloseKeepingError(server);
      return INVALID_SOCKET;
    }

    return server;
  }

  // no IPv6 stack
  server = socket(AF_INET, type, protocol);
  if(server == INVALID_SOCKET) {
    return INVALID_SOCKET;
  }
  // datagram servers only, as above
  if(reuse_address && type == SOCK_DGRAM && setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) == SOCKET_ERROR) {
    WSASetLastError(0);
  }

  struct sockaddr_in sin;
  memset(&sin, 0, sizeof(sin));
  sin.sin_family = AF_INET;
  sin.sin_addr.s_addr = INADDR_ANY;
  sin.sin_port = htons(port);
  if(BindEphemeralRetrying(server, (SOCKADDR*)&sin, sizeof(sin), port) == SOCKET_ERROR) {
    CloseKeepingError(server);
    return INVALID_SOCKET;
  }

  return server;
}

// see the declarations in win32.h
SOCKET IPSocket::BindAddress(const char* host, int port, int type, SocketError& error, bool reuse_address) {
  Startup();
  error = { 0, false };
  if(!host || !*host) {
    // BindAny keeps its failure in WSAGetLastError() across its cleanup
    const SOCKET server = BindAny(port, type, reuse_address);
    if(server == INVALID_SOCKET) {
      error = { WSAGetLastError(), false };
    }
    return server;
  }

  struct addrinfo hints;
  ZeroMemory(&hints, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = type;
  hints.ai_protocol = type == SOCK_DGRAM ? IPPROTO_UDP : IPPROTO_TCP;
  hints.ai_flags = AI_PASSIVE;

  struct addrinfo* result = nullptr;
  const std::string port_str = std::to_string(port);
  const int status = getaddrinfo(host, port_str.c_str(), &hints, &result);
  if(status != 0) {
    // the EAI_* codes are WSA error codes on Windows
    error = { status, true };
    return INVALID_SOCKET;
  }

  SOCKET server = INVALID_SOCKET;
  int last_error = WSAEADDRNOTAVAIL;
  const int families[] = { AF_INET, AF_INET6 };
  for(int i = 0; i < 2 && server == INVALID_SOCKET; ++i) {
    for(struct addrinfo* ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
      if(ptr->ai_family != families[i]) {
        continue;
      }

      SOCKET sock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
      if(sock == INVALID_SOCKET) {
        last_error = WSAGetLastError();
        continue;
      }

      if(ptr->ai_family == AF_INET6) {
        // already the Windows default; set so the meaning does not rest on it
        int v6_only = 1;
        if(setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&v6_only, sizeof(v6_only)) == SOCKET_ERROR) {
          WSASetLastError(0);
        }
      }

      // datagram servers only, as in BindAny: on Windows SO_REUSEADDR would let
      // a second TCP server listen on a port already taken
      int reuse = 1;
      if(reuse_address && type == SOCK_DGRAM && setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) == SOCKET_ERROR) {
        WSASetLastError(0);
      }

      if(BindEphemeralRetrying(sock, ptr->ai_addr, (int)ptr->ai_addrlen, port) == SOCKET_ERROR) {
        last_error = WSAGetLastError();
        closesocket(sock);
        continue;
      }

      server = sock;
      break;
    }
  }
  freeaddrinfo(result);

  if(server == INVALID_SOCKET) {
    error = { last_error, false };
  }

  return server;
}

void IPSocket::FormatAddress(const struct sockaddr* addr, char* buffer, size_t buffer_size, int& port) {
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

SOCKET IPSocket::Accept(SOCKET server, char* client_address, int& client_port) {
	struct sockaddr_storage pin;
	int addrlen = sizeof(pin);

	SOCKET client = accept(server, (struct sockaddr*)&pin, &addrlen);
	if(client == INVALID_SOCKET) {
		client_address[0] = '\0';
		client_port = -1;
		return -1;
	}

	// Same 30s default receive timeout Open() gives a connected socket. An
	// accepted socket had none, so a read on it blocked forever -- harmless
	// while a handler served one request and closed, but a server that holds
	// the connection open for a second request would park its thread on an
	// idle peer permanently. Best-effort: the connection is usable either way.
	DWORD timeout_ms = TCP_DEFAULT_RECV_TIMEOUT_MS;
	if(setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_ms, sizeof(timeout_ms)) == SOCKET_ERROR) {
		WSASetLastError(0);
	}

	// client_address holds INET6_ADDRSTRLEN (see SockTcpAccept)
	FormatAddress((struct sockaddr*)&pin, client_address, INET6_ADDRSTRLEN, client_port);

	return client;
}
