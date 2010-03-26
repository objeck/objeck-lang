#include <string>
#include <iostream>
#include <string>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

using namespace std;

#define BUFFER_SIZE 256

int main() {
	string address = "www.du.edu";
	int port = 80;

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
			get_string += "\r\n";
			get_string += "Host: ";
			get_string += "www.du.edu";
			get_string += "\r\n";
			get_string += "\r\n";
			
			int write_num = write(sock, get_string.c_str(), get_string.size());
			cout << "write_num: " << write_num << endl;
	
			char buffer[BUFFER_SIZE];
			int read_num = read(sock, buffer, BUFFER_SIZE);
			
			cout << "read_num: " << read_num << ": |" << buffer << "|" << endl;
			
			close(sock);
		}
	}

	return 0;	
}
