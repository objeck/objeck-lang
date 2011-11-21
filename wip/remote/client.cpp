
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
    if(sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) < 0) {
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

int main() {
  string address = "www.du.edu";
  int port = 80;

#ifdef _WIN32
  WSADATA data;
  int version = MAKEWORD(2, 2);
  WSAStartup(version, &data);
#endif
  
  int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);;
  if(sock > -1) {
    struct hostent* host_info = gethostbyname(address.c_str());	
    long host_addr;
    memcpy(&host_addr, host_info->h_addr, host_info->h_length);
		
    struct sockaddr_in ip_addr;	
    ip_addr.sin_addr.s_addr = host_addr;;
    ip_addr.sin_port=htons(port);
    ip_addr.sin_family = AF_INET;

    if(!connect(sock, (struct sockaddr*)&ip_addr,sizeof(ip_addr))) {
      string get_string = "GET ";
      get_string += "/";
      get_string += " HTTP/1.1";
      get_string += CRLF;
      get_string += "Host: ";
      get_string += "www.du.edu";
      get_string += CRLF;
      get_string += CRLF;
		
#ifdef _WIN32
      int write_num = send(sock, get_string.c_str(), get_string.size(), 0);
#else
      int write_num = write(sock, get_string.c_str(), get_string.size());
#endif
      cout << "write_num: " << write_num << endl;
	
      char buffer[BUFFER_SIZE];
#ifdef _WIN32
      int read_num;
      do {
        read_num = recv(sock, buffer, BUFFER_SIZE, 0);
        cout << "read_num: " << read_num << ": |" << buffer << "|" << endl;
      }
      while(read_num > 0);
#else
      int read_num;
      do {
	read_num = read(sock, buffer, BUFFER_SIZE);
        cout << "read_num: " << read_num << ": |" << buffer << "|" << endl;
      }
      while(read_num > 0);
#endif
			
#ifdef _WIN32
      closesocket(sock);
      WSACleanup();
#else
      close(sock);
#endif
    }
  }
  return 0;	
}
