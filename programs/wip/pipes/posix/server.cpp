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
	// --- create server ---
	int server_pipe = socket(AF_UNIX, SOCK_STREAM, 0);
    if(server_pipe < 0) {
      return 1;
    }

    unlink(FILE_PATH);
    
    struct sockaddr_un server_addr;
    memset(&server_addr, 0, sizeof(struct sockaddr_un));
    server_addr.sun_family = AF_UNIX;   
    strcpy(server_addr.sun_path, FILE_PATH);
    
    const int server_len = sizeof(server_addr);
    if(bind(server_pipe, (struct sockaddr*) &server_addr, server_len) < 0) {
      return 1;
    }

    // --- listen ---
    if(listen(server_pipe, 4) < 0){ 
      return 1;
    }
    
    struct sockaddr_un client_addr;
    memset(&client_addr, 0, sizeof(struct sockaddr_un));
    
    socklen_t client_len;
    int client_pipe = accept(server_pipe, (struct sockaddr*)&client_addr, &client_len);
    close(server_pipe);

    // --- I/0 routines ---
    int count;

    // read
    char buffer_in[BUFFER_MAX];
    count = recv(client_pipe, buffer_in, sizeof(buffer_in) - 1, 0);
    if(count < 0) {
    	return 1;
    }
    buffer_in[count] = '\0';
    puts(buffer_in);

    // write
    char buffer_out[] = "Thank you!";
    count = send(client_pipe, buffer_out, strlen(buffer_out), 0);
    if(count != strlen(buffer_out)) {
    	return 1;
    }

    // --- close ---
    close(client_pipe);
}