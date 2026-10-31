# On top of instrument.py: a close also logs how many received bytes were still
# unread (FIONREAD) and the first of them, which is what turns a close into a reset.
import sys

root = sys.argv[1]
p = root + "/core/vm/arch/win32/win32.h"
t = open(p, encoding="utf-8", newline="").read()
old = """    const int shut = shutdown(sock, 1 /* SD_SEND; winsock2.h is not in this header's include set */);
    SockTrace("CLOSE", sock, shut ? WSAGetLastError() : 0);"""
new = """    SockTracePending(sock);
    const int shut = shutdown(sock, 1 /* SD_SEND; winsock2.h is not in this header's include set */);
    SockTrace("CLOSE", sock, shut ? WSAGetLastError() : 0);"""
assert t.count(old) == 1
t = t.replace(old, new)
old = "void SockTrace(const char* what, SOCKET sock, int detail);\n"
assert t.count(old) == 1
t = t.replace(old, old + "void SockTracePending(SOCKET sock);\n")
open(p, "w", encoding="utf-8", newline="").write(t)

p = root + "/core/vm/arch/win32/win32.cpp"
t = open(p, encoding="utf-8", newline="").read()
old = """// see the declaration in win32.h
void IPSocket::Startup() {"""
new = """void SockTracePending(SOCKET sock) {
  u_long pending = 0;
  if(ioctlsocket(sock, FIONREAD, &pending) || pending == 0) {
    return;
  }
  char peek[24];
  const int n = recv(sock, peek, sizeof(peek), MSG_PEEK);
  char text[64] = {0};
  for(int i = 0; i < n && i < (int)sizeof(peek); ++i) {
    const char c = peek[i];
    text[i] = (c >= 33 && c < 127) ? c : (c == '\\r' ? '<' : (c == '\\n' ? '>' : '.'));
  }
  char what[96];
  snprintf(what, sizeof(what), "UNREAD:%s", text);
  SockTrace(what, sock, (int)pending);
}

// see the declaration in win32.h
void IPSocket::Startup() {"""
assert t.count(old) == 1
t = t.replace(old, new)
open(p, "w", encoding="utf-8", newline="").write(t)
print("ok")
