# Adds temporary socket tracing to the Windows VM (never committed): with
# OBJECK_SOCK_TRACE set to a folder, every connect, accept, end of read and close
# is logged per process with both ports and a wall-clock time.
import sys

root = sys.argv[1]


def patch(rel, pairs):
    p = root + "/" + rel
    t = open(p, encoding="utf-8", newline="").read()
    for old, new in pairs:
        assert t.count(old) == 1, (rel, old[:60], t.count(old))
        t = t.replace(old, new)
    open(p, "w", encoding="utf-8", newline="").write(t)


patch("core/vm/arch/win32/win32.h", [
    ("class IPSocket {", "void SockTrace(const char* what, SOCKET sock, int detail);\n\nclass IPSocket {"),
    ("""    status = recv(sock, &value, 1, 0);
    if(status == SOCKET_ERROR) {
      error = WSAGetLastError();
      return '\\0';
    }
""", """    status = recv(sock, &value, 1, 0);
    if(status == SOCKET_ERROR) {
      error = WSAGetLastError();
      SockTrace("READ_ERR", sock, error);
      return '\\0';
    }
    if(status == 0) {
      SockTrace("READ_EOF", sock, 0);
    }
"""),
    ("""      int status = recv(sock, values + total, len - total, 0);
      if(status == SOCKET_ERROR) {
        error = WSAGetLastError();
        return total > 0 ? total : -1;
      }
      if(status == 0) {
        break; // connection closed""", """      int status = recv(sock, values + total, len - total, 0);
      if(status == SOCKET_ERROR) {
        error = WSAGetLastError();
        SockTrace("READ_ERR", sock, error);
        return total > 0 ? total : -1;
      }
      if(status == 0) {
        SockTrace("READ_EOF", sock, 0);
        break; // connection closed"""),
    ("""    shutdown(sock, 1 /* SD_SEND; winsock2.h is not in this header's include set */);
    closesocket(sock);""", """    const int shut = shutdown(sock, 1 /* SD_SEND; winsock2.h is not in this header's include set */);
    SockTrace("CLOSE", sock, shut ? WSAGetLastError() : 0);
    if(closesocket(sock)) {
      SockTrace("CLOSE_ERR", INVALID_SOCKET, WSAGetLastError());
    }"""),
])

patch("core/vm/arch/win32/win32.cpp", [
    ("""// see the declaration in win32.h
void IPSocket::Startup() {""", """#include <mutex>

void SockTrace(const char* what, SOCKET sock, int detail) {
  static std::mutex lock;
  static FILE* out = [] {
    char dir[512];
    if(!GetEnvironmentVariableA("OBJECK_SOCK_TRACE", dir, sizeof(dir))) {
      return (FILE*)nullptr;
    }
    char path[600];
    snprintf(path, sizeof(path), "%s\\\\sock_%lu.log", dir, GetCurrentProcessId());
    return _fsopen(path, "a", _SH_DENYNO);
  }();
  if(!out) {
    return;
  }
  const int saved = WSAGetLastError();
  int lport = -1, pport = -1;
  struct sockaddr_storage a;
  int len = sizeof(a);
  if(sock != INVALID_SOCKET && !getsockname(sock, (struct sockaddr*)&a, &len)) {
    lport = ntohs(((struct sockaddr_in*)&a)->sin_port);
  }
  len = sizeof(a);
  int peer_err = 0;
  if(sock != INVALID_SOCKET && !getpeername(sock, (struct sockaddr*)&a, &len)) {
    pport = ntohs(((struct sockaddr_in*)&a)->sin_port);
  }
  else {
    peer_err = WSAGetLastError();
  }
  FILETIME ft;
  GetSystemTimePreciseAsFileTime(&ft);
  const unsigned long long t = ((unsigned long long)ft.dwHighDateTime << 32 | ft.dwLowDateTime) / 10; // us
  std::lock_guard<std::mutex> guard(lock);
  fprintf(out, "%llu %lu %lu %s h=%llu l=%d p=%d d=%d pe=%d\\n", t, GetCurrentProcessId(), GetCurrentThreadId(),
          what, (unsigned long long)sock, lport, pport, detail, peer_err);
  fflush(out);
  WSASetLastError(saved);
}

// see the declaration in win32.h
void IPSocket::Startup() {"""),
    ("""  const SOCKET sock = Connect(address, port, SOCK_STREAM, timeout_ms, error);
""", """  const SOCKET sock = Connect(address, port, SOCK_STREAM, timeout_ms, error);
  SockTrace(sock != INVALID_SOCKET ? "CONNECT" : "CONNECT_FAIL", sock, sock != INVALID_SOCKET ? port : error.code);
"""),
    ("""	// client_address holds INET6_ADDRSTRLEN (see SockTcpAccept)""", """	SockTrace("ACCEPT", client, 0);

	// client_address holds INET6_ADDRSTRLEN (see SockTcpAccept)"""),
])
print("ok")
