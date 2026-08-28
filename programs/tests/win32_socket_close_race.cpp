/***************************************************************************
 * Standalone reproduction for objeck-lang issue #669.
 *
 * On Windows, a peer that calls send() and then immediately tears the
 * connection down can have the just-sent data DISCARDED. The reader's recv()
 * returns WSAECONNRESET (10054) with ZERO bytes -- even though send() accepted
 * every byte and the data had physically arrived. Linux does not do this.
 *
 * There is no Objeck here on purpose. The point of this file is that the
 * behaviour belongs to the platform, not to the VM: the same client/server
 * shape that makes core_http_server and core_net_buffer fail intermittently on
 * Windows fails here too, in a hundred lines of plain sockets.
 *
 * Build:
 *   Windows   cl /nologo /EHsc /O2 /std:c++17 win32_socket_close_race.cpp
 *   Linux     g++ -O2 -std=c++17 -pthread win32_socket_close_race.cpp -o race
 *
 * Run:
 *   race        reproduce: the sequence the VM uses today
 *   race 1      bind a unique local port per connection
 *   race 2      pause briefly before tearing down
 *
 * Expected, Windows x64 loopback:
 *   mode 0 -> ~40% of transfers fail       (18/50, 41/100, 46/100 observed)
 *   mode 1 -> still fails, and every local port is distinct, which is what
 *             rules out ephemeral-port/TIME_WAIT reuse as the cause
 *   mode 2 -> 0 failures, every run
 *
 * Expected, Linux: 0 failures in every mode.
 *
 * Mode 2 is a diagnosis, NOT a proposed fix. It works only because the pause
 * lets the READING side reach its recv(), which acknowledges the data; the
 * sender then tears down a connection with nothing outstanding. Putting that
 * pause in the VM's socket close would cost latency on every close and would
 * paper over the race rather than close it. See #669 for the full table of
 * close strategies that were measured and rejected (SO_LINGER, drain-to-EOF at
 * two timeouts, asymmetric drain, close-without-shutdown).
 ***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <set>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
typedef int socklen_t;
#define LAST_ERROR       WSAGetLastError()
#define CLOSE_SOCKET(s)  closesocket(s)
#define SHUT_WRITE       SD_SEND
#define SLEEP_MS(ms)     Sleep(ms)
#define BAD_SOCKET       INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
typedef int SOCKET;
#define LAST_ERROR       errno
#define CLOSE_SOCKET(s)  close(s)
#define SHUT_WRITE       SHUT_WR
#define SLEEP_MS(ms)     do { struct timespec t{0, (ms) * 1000000L}; nanosleep(&t, nullptr); } while(0)
#define BAD_SOCKET       (-1)
#define SOCKET_ERROR     (-1)
#endif

static int MODE = 0;
static const int PORT = 19921;
static const int BASE_LOCAL_PORT = 41000;
static const int ROUNDS = 10;
static const int SIZES[] = {1024, 4096, 8192, 16384, 32768};

// Both of these mirror core/vm/arch/*/IPSocket exactly.
static int ReadBytes(char* values, int len, SOCKET sock) {
  int total = 0;
  while(total < len) {
    const int status = (int)recv(sock, values + total, len - total, 0);
    if(status == SOCKET_ERROR) {
      fprintf(stderr, "  recv err=%d want=%d got=%d\n", LAST_ERROR, len, total);
      return total > 0 ? total : -1;
    }
    if(status == 0) {
      break;
    }
    total += status;
  }
  return total;
}

static int WriteBytes(const char* values, int len, SOCKET sock) {
  int total = 0;
  while(total < len) {
    const int status = (int)send(sock, values + total, len - total, 0);
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

static void CloseSocket(SOCKET sock, bool is_sender) {
  if(MODE == 2 && is_sender) {
    SLEEP_MS(5);
  }
  shutdown(sock, SHUT_WRITE);
  CLOSE_SOCKET(sock);
}

// Answers each connection with the number of bytes the client asked for, then
// closes -- the "one request, one response, close" shape the Objeck HTTP server
// and core_net_buffer both use.
static void ServerThread() {
  SOCKET listener = socket(AF_INET, SOCK_STREAM, 0);
  int yes = 1;
  setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));

  sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(PORT);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if(bind(listener, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
    fprintf(stderr, "bind failed: %d\n", LAST_ERROR);
    return;
  }
  listen(listener, 16);

  for(;;) {
    const SOCKET client = accept(listener, nullptr, nullptr);
    if(client == BAD_SOCKET) {
      break;
    }

    char header[4];
    if(ReadBytes(header, 4, client) != 4) {
      CloseSocket(client, true);
      continue;
    }
    const int size = ((unsigned char)header[0] << 24) | ((unsigned char)header[1] << 16) |
                     ((unsigned char)header[2] << 8) | (unsigned char)header[3];
    if(size == 0) {
      CloseSocket(client, true);
      break;
    }

    std::vector<char> payload(size);
    for(int i = 0; i < size; i++) {
      payload[i] = (char)(i % 251);
    }
    WriteBytes(payload.data(), size, client);
    CloseSocket(client, true);
  }
  CLOSE_SOCKET(listener);
}

static SOCKET Connect(int explicit_local_port) {
  const SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
  if(sock == BAD_SOCKET) {
    return BAD_SOCKET;
  }

  if(explicit_local_port > 0) {
    sockaddr_in local;
    memset(&local, 0, sizeof(local));
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    local.sin_port = htons((unsigned short)explicit_local_port);
    if(bind(sock, (sockaddr*)&local, sizeof(local)) == SOCKET_ERROR) {
      CLOSE_SOCKET(sock);
      return BAD_SOCKET;
    }
  }

  sockaddr_in remote;
  memset(&remote, 0, sizeof(remote));
  remote.sin_family = AF_INET;
  remote.sin_port = htons(PORT);
  remote.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if(connect(sock, (sockaddr*)&remote, sizeof(remote)) == SOCKET_ERROR) {
    CLOSE_SOCKET(sock);
    return BAD_SOCKET;
  }
  return sock;
}

int main(int argc, char** argv) {
  if(argc > 1) {
    MODE = atoi(argv[1]);
  }

#ifdef _WIN32
  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

  std::thread server(ServerThread);
  SLEEP_MS(400);

  int failures = 0, attempts = 0, failures_on_a_reused_port = 0;
  std::set<int> local_ports;
  int next_local_port = BASE_LOCAL_PORT;

  for(int round = 0; round < ROUNDS; round++) {
    for(int s = 0; s < 5; s++) {
      const int size = SIZES[s];
      const SOCKET sock = Connect(MODE == 1 ? next_local_port++ : 0);
      if(sock == BAD_SOCKET) {
        continue;
      }

      sockaddr_in me;
      socklen_t me_len = sizeof(me);
      memset(&me, 0, sizeof(me));
      getsockname(sock, (sockaddr*)&me, &me_len);
      const bool reused = !local_ports.insert(ntohs(me.sin_port)).second;

      char header[4] = {(char)(size >> 24), (char)(size >> 16), (char)(size >> 8), (char)size};
      WriteBytes(header, 4, sock);

      std::vector<char> buffer(size);
      const int got = ReadBytes(buffer.data(), size, sock);
      attempts++;
      if(got != size) {
        failures++;
        if(reused) {
          failures_on_a_reused_port++;
        }
        printf("FAIL size=%d got=%d\n", size, got);
      }
      CloseSocket(sock, false);
    }
  }

  const SOCKET stop = Connect(0);
  if(stop != BAD_SOCKET) {
    char zero[4] = {0, 0, 0, 0};
    WriteBytes(zero, 4, stop);
    CloseSocket(stop, false);
  }
  server.join();

  printf("\nmode=%d  failed=%d/%d  distinct_local_ports=%d  failures_on_a_reused_port=%d\n",
         MODE, failures, attempts, (int)local_ports.size(), failures_on_a_reused_port);

#ifdef _WIN32
  WSACleanup();
#endif
  return failures ? 1 : 0;
}
