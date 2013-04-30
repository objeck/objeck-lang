/***************************************************************************
 * Provides runtime support for POSIX compliant systems i.e 
 * Linux, OS X, etc.
 *
 * Copyright (c) 2008-2013, Randy Hollines
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in
 * the documentation and/or other materials provided with the distribution.
 * - Neither the name of the Objeck Team nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 *  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ***************************************************************************/

#ifndef __POSIX_H__
#define __POSIX_H__

#include "../../common.h"
#include <sys/utsname.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

#define SOCKET int

/****************************
 * File support class
 ****************************/
class File {
 public:
  static long FileSize(const char* name) {
    struct stat buf;
    if(stat(name, &buf)) {
      return -1;
    }
    
    return buf.st_size;
  }
  
  static bool FileExists(const char* name) {
    struct stat buf;
    if(stat(name, &buf)) {
      return false;
    }
    
    return true;
  }

  static time_t FileCreatedTime(const char* name) {
    struct stat buf;
    if(stat(name, &buf)) {
      return -1;
    }

    return buf.st_ctime;
  }

  static time_t FileModifiedTime(const char* name) {
    struct stat buf;
    if(stat(name, &buf)) {
      return -1;
    }

    return buf.st_mtime;
  }

  static time_t FileAccessedTime(const char* name) {
    struct stat buf;
    if(stat(name, &buf)) {
      return -1;
    }

    return buf.st_atime;
  }

  static FILE* FileOpen(const char* name, const char* mode) {
    FILE* file = fopen(name, mode);
    if(file < 0) {
      return NULL;
    }
    
    return file;
  }

  static bool MakeDir(const char* name) {
    if(mkdir(name, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) < 0) {
      return false;
    }

    return true;
  }

  static bool IsDir(const char* name) {
    DIR* dir = opendir(name);
    if(!dir) {
      return false;
    }
    closedir(dir);

    return true;
  }

  static vector<string> ListDir(const char* path) {
    vector<string> files;
    
    struct dirent **names;
    int n = scandir(path, &names, 0, alphasort);
    if(n > 0) {
      while(n--) {
	if((strcmp(names[n]->d_name, "..") != 0) && (strcmp(names[n]->d_name, ".") != 0)) {
	  files.push_back(names[n]->d_name);
	}
	free(names[n]);
      }
      free(names);
    }

    return files;
  }
};

/****************************
 * IP socket support class
 ****************************/
class IPSocket {
 public:
  static SOCKET Open(const char* address, int port) {
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(socket < 0) {
      return -1;
    }

    struct hostent* host_info = gethostbyname(address);
    if(!host_info) {
      close(sock);
      return -1;
    }
    
    long host_addr;
    memcpy(&host_addr, host_info->h_addr, host_info->h_length);
    
    struct sockaddr_in ip_addr;
    ip_addr.sin_addr.s_addr = host_addr;;
    ip_addr.sin_port=htons(port);
    ip_addr.sin_family = AF_INET;
    
    if(!connect(sock, (struct sockaddr*)&ip_addr,sizeof(ip_addr))) {
      return sock;
    }
     
    close(sock);
    return -1;
  }
  
  static SOCKET Bind(int port) {
    SOCKET server = socket(AF_INET, SOCK_STREAM, 0);
    if(server < 0) {
      return -1;
    }
    
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(port);
    
    if(bind(server, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
      close(server);
      return -1;
    }
    
    return server;
  }
  
  static bool Listen(SOCKET server, int backlog) {
    if(listen(server, backlog) < 0) {
      return false;
    }
    
    return true;
  }
  
  static SOCKET Accept(SOCKET server, char* client_address, int &client_port) {
    struct sockaddr_in pin;
    socklen_t addrlen = sizeof(pin); 
    SOCKET client = accept(server, (struct sockaddr *)&pin, &addrlen);
    if(client < 0) {
      client_address[0] = '\0';
      client_port = -1;
      return -1;
    }
    
    strncpy(client_address, inet_ntoa(pin.sin_addr), 255);
    client_port = ntohs(pin.sin_port);
    
    return client;
  }

  static void WriteByte(const char value, SOCKET sock) {
    send(sock, &value, 1, 0);
  }

  static int WriteBytes(const char* values, int len, SOCKET sock) {
    return send(sock, values, len, 0);
  }

  static char ReadByte(SOCKET sock, int &status) {
    char value;
    status = recv(sock, &value, 1, 0);
    return value;
  }

  static int ReadBytes(char* values, int len, SOCKET sock) {
    return recv(sock, values, len, 0);
  }
  
  static void Close(SOCKET sock) {
    close(sock);
  }
};

/****************************
 * IP socket support class
 ****************************/
class IPSecureSocket {
 public:
  static bool Open(const char* address, int port, SSL_CTX* &ctx, BIO* &bio) {
    ctx = SSL_CTX_new(SSLv23_client_method());
    bio = BIO_new_ssl_connect(ctx);
    if(!bio) {
      SSL_CTX_free(ctx);
      return false;
    }
    
    SSL* ssl;
    BIO_get_ssl(bio, &ssl);
    SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);
    string ssl_address = address;
    if(ssl_address.size() < 1 || port < 0) {
      BIO_free_all(bio);
      SSL_CTX_free(ctx);
      return false;
    }    
    ssl_address += ":";
    ssl_address += UnicodeToBytes(IntToString(port));
    BIO_set_conn_hostname(bio, ssl_address.c_str());
    
    if(BIO_do_connect(bio) <= 0) {
      BIO_free_all(bio);
      SSL_CTX_free(ctx);
      return false;
    }

    if(BIO_do_handshake(bio) <= 0) {
      BIO_free_all(bio);
      SSL_CTX_free(ctx);
      return false;
    }
    
    return true;
  }
  
  static void WriteByte(char value, SSL_CTX* ctx, BIO* bio) {
    BIO_write(bio, &value, 1);
  }

  static int WriteBytes(const char* values, int len, SSL_CTX* ctx, BIO* bio) {
    return BIO_write(bio, values, len);
  }

  static char ReadByte(SSL_CTX* ctx, BIO* bio, int &status) {
    char value;
    status = BIO_read(bio, &value, 1);
    return value;
  }
  
  static int ReadBytes(char* values, int len, SSL_CTX* ctx, BIO* bio) {
    return BIO_read(bio, values, len);
  }
  
  static void Close(SSL_CTX* ctx, BIO* bio) {
    BIO_free_all(bio);
    SSL_CTX_free(ctx);
  }
};

/****************************
 * System operations
 ****************************/
class System {
 public:
  static string GetPlatform() {
    string platform;
    struct utsname uts;
    
    if(uname(&uts) < 0) {
      platform = "Unknown";
    }
    else {
      platform += uts.sysname;
      platform += " ";
      platform += uts.release;
      platform += ", ";
      platform += uts.machine;
    }
    
    return platform;
  }
};

#endif
