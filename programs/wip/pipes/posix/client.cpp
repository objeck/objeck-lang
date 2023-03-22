#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#define FILE_PATH "/tmp/foo"
#define BUFFER_MAX 1024

int main() {
	// --- connect ---
	int client_pipe = socket(AF_UNIX, SOCK_STREAM, 0);
    if(client_pipe < 0) {
      return 1;
    }
    
    struct sockaddr_un client_addr;
    memset(&client_addr, 0, sizeof(struct sockaddr_un));
    client_addr.sun_family = AF_UNIX;   
    strcpy(client_addr.sun_path, FILE_PATH);
    
    const int len = sizeof(client_addr);

    /*
    if(bind(client_sock, (struct sockaddr *) &client_sockaddr, len) < 0) {
      return 1;
    }
    */
    
    if(connect(client_pipe, (struct sockaddr*)&client_addr, len) < 0) {
      return 1;
    }


    // --- I/0 routines ---
    int count;

	// write
    char buffer_out[] = "Hello Unix Socket...";
    count = send(client_pipe, buffer_out, strlen(buffer_out), 0);
    if(count != strlen(buffer_out)) {
    	return 1;
    }

    // read
    char buffer_in[BUFFER_MAX];
    count = recv(client_pipe, buffer_in, sizeof(buffer_in) - 1, 0);
    if(count < 0) {
    	return 1;
    }
    buffer_in[count] = '\0';
    puts(buffer_in);

    // --- close ---
    close(client_pipe);
}