
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
