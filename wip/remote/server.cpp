// 

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <iostream>
#include <string>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <pthread.h>

using namespace std;

long server_sock = 0;

void error(const char* msg)
{
  perror(msg);
  exit(1);
}

void* request_handler(void* data) 
{
  long client_sock = (long)data;
  
  int value = -42;
  #ifdef _WIN32
  unsigned long send_num = send(client_sock, &value, sizeof(value), 0);
#else
  unsigned long send_num = write(client_sock, &value, sizeof(value));
#endif
  

  close(client_sock);  
  return NULL;
}

void terminate(int value)
{
  cout << "shutting down server..." << endl;
  close(server_sock);
  exit(0);
}

// TODO...
// 1) new object instance
// 2) function/method call
// 3) free object instance
// 4) class level memory???

int main(int argc, char **argv)
{
  if(argc < 2) {
    fprintf(stderr,"ERROR, no port provided\n");
    exit(1);
  }
  
  server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (server_sock < 0) {
    error("ERROR opening socket");
  }
  
  int port = atoi(argv[1]);
  struct sockaddr_in serv_addr;
  bzero((char*)&serv_addr, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = INADDR_ANY;
  serv_addr.sin_port = htons(port);
  
  if(bind(server_sock, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
    error("ERROR on binding");
  }
  
  listen(server_sock, SOMAXCONN);
  signal(SIGINT, terminate);
  
  struct sockaddr_in cli_addr;
  socklen_t cli_len = sizeof(cli_addr);  
  while(true) {
    long client_sock = accept(server_sock, (struct sockaddr*) &cli_addr, &cli_len);
    if(client_sock < 0) {
      error("ERROR on accept");
    }
    
    pthread_t handle;
    pthread_create(&handle, NULL, request_handler, (void*)client_sock);
  }
  
  return 0;
}
