
#include <stdlib.h>
#include <string>
#include <iostream>
#include <string>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#endif

using namespace std;

#define BUFFER_SIZE 256
#define CRLF "\r\n"

class RemoteClient {
  long sock;
  string address;
  int port;
  
public:
  RemoteClient(const string &a, int p) {
    address = a;
    port = p;
    sock = 0;
  }

  ~RemoteClient() {
    Disconnect();
  }
  
  bool Connect() {
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(sock < 0) {
      sock = 0;
      return false;
    }
    
    struct hostent* host_info = gethostbyname(address.c_str()); 
    long host_addr;
    memcpy(&host_addr, host_info->h_addr, host_info->h_length);
    
    struct sockaddr_in ip_addr; 
    ip_addr.sin_addr.s_addr = host_addr;;
    ip_addr.sin_port=htons(port);
    ip_addr.sin_family = AF_INET;
    
    if(connect(sock, (struct sockaddr*)&ip_addr,sizeof(ip_addr))) {

      cout << "...." << endl;

      sock = 0;
      return false;
    }
    
    return true;
  }

  bool IsConnected() {
    return sock > 0;
  }
  
  bool Send(int value) {
    if(!IsConnected()) {
      return false;
    }

#ifdef _WIN32
    int send_num = send(sock, &value, sizeof(value), 0);
#else
    int send_num = write(sock, &value, sizeof(value));
#endif
    
    if(send_num < sizeof(value)) {
      Disconnect();
      return false;
    }

    return true;
  }
  
  bool Send(double value) {
    if(!IsConnected()) {
      return false;
    }
    
#ifdef _WIN32
    int send_num = send(sock, &value, sizeof(value), 0);
#else
    int send_num = write(sock, &value, sizeof(value));
#endif
    
    if(send_num < sizeof(value)) {
      Disconnect();
      return false;
    }
    
    return true;
  }

  int ReadInt() {
    int value;
    
    if(!IsConnected()) {
      return 0;
    }
    
#ifdef _WIN32
    int read_num = recv(sock, &value, sizeof(value), 0);
#else
    int read_num = read(sock, &value, sizeof(value));
#endif
    
    if(read_num < sizeof(value)) {
      Disconnect();
    }
  }
  
  double ReadDouble() {
    double value;
    
    if(!IsConnected()) {
      return 0.0;
    }
    
#ifdef _WIN32
    int read_num = recv(sock, &value, sizeof(value), 0);
#else
    int read_num = read(sock, &value, sizeof(value));
#endif
    
    if(read_num < sizeof(value)) {
      Disconnect();
    }
  }

  void Disconnect() {
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
    sock = 0;
  }
};

int main(int argc, char **argv) {
#ifdef _WIN32
  WSADATA data;
  int version = MAKEWORD(2, 2);
  WSAStartup(version, &data);
#endif
  
  if(argc == 3) {
    cout << "trying: " << argv[1] << " (" << argv[2] << ")" << endl;
    RemoteClient client(argv[1], atoi(argv[2]));
    if(client.Connect()) {
      client.Send(42);
    }
  }
  
#ifdef _WIN32
  WSACleanup();
#endif
  
  return 0;	
}
