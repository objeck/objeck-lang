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
#include <stdlib.h>
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
#include <sys/un.h>
#include <pwd.h>
#include <grp.h>

#define SOCKET int

/****************************
 * File support class
 ****************************/
class File {
 public:
  static std::string FullPathName(const std::string name) {
    char buffer[PATH_MAX] = "";
    if(!realpath(name.c_str(), buffer)) {
      return "";
    }
    
    return buffer;
  }
  
  static long FileSize(const char* name) {
    struct stat buf;
    if(stat(name, &buf)) {
      return -1;
    }
    
    return buf.st_size;
  }

  static std::string TempName() {
    char buffer[] = "/tmp/objeck-XXXXXX";
    if(mkstemp(buffer) < 0) {
      return "";
    }
    
    return buffer;
  }
  
  static bool FileExists(const char* name) {
    struct stat buf;
    if(stat(name, &buf)) {
      return false;
    }

    return S_IFREG & buf.st_mode;
  }
  
  static bool FileReadOnly(const char* name) {
    if(!access(name, R_OK) && access(name, W_OK) == EACCES) {
      return true;
    }
    
    return false;
  }
  
  static bool FileWriteOnly(const char* name) {
    if(!access(name, W_OK) && access(name, R_OK) == EACCES) {
      return true;
    }
    
    return false;
  }

  static bool FileReadWrite(const char* name) {
    if (!access(name, W_OK) && !access(name, R_OK)) {
      return true;
    }

    return false;
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
    return fopen(name, mode);
  }

  static std::wstring FileOwner(const char* name, bool is_account) {
    struct stat info;
    if(stat(name, &info)) {
      return L"";
    }

    if(is_account) {
      struct passwd* account = getpwuid(info.st_uid);
      return BytesToUnicode(account->pw_name);
    }
        
    struct group * group = getgrgid(info.st_gid);
    return BytesToUnicode(group->gr_name);
  }
  
  static bool MakeDir(const char* name) {
    if(mkdir(name, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) < 0) {
      return false;
    }
    
    return true;
  }

  static bool DirExists(const char* name) {
    struct stat buf;
    if(stat(name, &buf)) {
      return false;
    }
    
    return S_IFDIR & buf.st_mode;
  }

  static  std::vector<std::string> ListDir(const char* path) {
     std::vector<std::string> files;

    struct dirent** names;
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
 * Pipe support class
 ****************************/
class Pipe {
public:
  static bool Create(const char* name, int& client_pipe) {
    client_pipe = 0;

    int server_pipe = socket(AF_UNIX, SOCK_STREAM, 0);
    if(server_pipe < 0) {
      return false;
    }

    unlink(name);
    
    struct sockaddr_un server_addr;
    memset(&server_addr, 0, sizeof(struct sockaddr_un));
    server_addr.sun_family = AF_UNIX;   
    strncpy(server_addr.sun_path, name, sizeof(server_addr.sun_path) - 1);
    
    const int len = sizeof(server_addr);
    if(bind(server_pipe, (struct sockaddr*) &server_addr, len) < 0) {
      close(server_pipe);
      return false;
    }

    if(listen(server_pipe, 4) < 0){ 
      close(server_pipe);
      return false;
    }
    
    struct sockaddr_un client_addr;
    memset(&client_addr, 0, sizeof(struct sockaddr_un));
    
    socklen_t client_len = 0;
    client_pipe = accept(server_pipe, (struct sockaddr*)&client_addr, &client_len);
    if(client_pipe < 0) {
      client_pipe = 0;
      close(server_pipe);
      return false;
    }
    close(server_pipe);
    
    return true;
  }
  
  static bool Open(const char* name, int& client_pipe) {
    client_pipe = socket(AF_UNIX, SOCK_STREAM, 0);
    if(client_pipe < 0) {
      return false;
    }
    
    struct sockaddr_un client_addr;
    memset(&client_addr, 0, sizeof(struct sockaddr_un));
    client_addr.sun_family = AF_UNIX;   
    strncpy(client_addr.sun_path, name, sizeof(client_addr.sun_path) - 1);
    
    const int len = sizeof(client_addr);
    if(connect(client_pipe, (struct sockaddr*)&client_addr, len) < 0) {
		close(client_pipe);
      return false;
    }
    
    return true;
  }
  
  static void Close(int pipe) {
    close(pipe);
  }
  
  static bool ReadByte(char &value, int pipe) {
    if(recv(pipe, &value, 1, 0) != 1) {
        return false;
    }
    
    return true;
  }

  static size_t ReadByteArray(char* buffer, size_t offset, size_t num, int pipe) {
    return recv(pipe, buffer + offset, num, 0);
  }

  static size_t WriteByteArray(const char* buffer, size_t offset, size_t num, int pipe) {
    return send(pipe, buffer + offset, num, 0);
  }
  
  static bool WriteByte(char value, int pipe) {
    return send(pipe, &value, 1, 0);
  }
  
  static std::string ReadString(int pipe) {
    char buffer[MID_BUFFER_MAX];
    size_t buffer_index = 0;

    char value;
    bool done = false;
    do {
      if(recv(pipe, &value, 1, 0) != 1) {
        done = true;
      }
      else if(value != '\0' && value != '\r' && value != '\n') {
        buffer[buffer_index++] = value;
      }
      else {
        done = true;
      }
    } 
    while(!done && buffer_index < MID_BUFFER_MAX - 1);
    buffer[buffer_index] = '\0';

    if(value == '\r') {
      recv(pipe, &value, 1, 0);
    }

    return buffer;
  }

  static bool WriteString(const std::string& line, int pipe) {
    const size_t num = line.size();
    return send(pipe, line.c_str(), num, 0) == (int)num;
  }
};

/****************************
 * IP socket support class
 ****************************/
class IPSocket {
public:
  static  std::vector<std::string> Resolve(const char* address) {
    std::vector<std::string> addresses;

    struct addrinfo* result;
    if(getaddrinfo(address, nullptr, nullptr, &result)) {
      freeaddrinfo(result);
      return  std::vector<std::string>();
    }

    struct addrinfo* res;
    for(res = result; res != nullptr; res = res->ai_next) {
      char hostname[NI_MAXHOST] = { 0 };
      if(getnameinfo(res->ai_addr, (socklen_t)res->ai_addrlen, hostname, NI_MAXHOST, nullptr, 0, 0)) {
	freeaddrinfo(result);
	return  std::vector<std::string>();
      }

      if(*hostname != '\0') {
	addresses.push_back(hostname);
      }
    }

    freeaddrinfo(result);
    return addresses;
  }
  
  static SOCKET Open(const char* address, int port) {
    SOCKET sock = -1;
    struct addrinfo* result = nullptr, *ptr = nullptr, hints;
    
    bzero(&hints, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    std::string port_str = std::to_string(port);
    if(getaddrinfo(address, port_str.c_str(), &hints, &result) != 0) {
      return -1;
    }

    for(ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
      sock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
      if(sock < 0) {
        freeaddrinfo(result);
        return -1;
      }
      
      if(connect(sock, ptr->ai_addr, (int)ptr->ai_addrlen) < 0) {
        close(sock);
        sock = -1;
        continue;
      }
      break;
    }
    freeaddrinfo(result);
    
    return sock;
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
    
    if(::bind(server, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
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

    SOCKET client = accept(server, (struct sockaddr*)&pin, &addrlen);
    if(client < 0) {
      client_address[0] = '\0';
      client_port = -1;
      return -1;
    }
    
    char buffer[INET_ADDRSTRLEN] = { 0 };
    inet_ntop(AF_INET, &(pin.sin_addr), buffer, INET_ADDRSTRLEN);
    strncpy(client_address, buffer, INET_ADDRSTRLEN);
    client_port = ntohs(pin.sin_port);

    return client;
  }
  
  static int WriteByte(const char value, SOCKET sock) {
    return send(sock, &value, 1, 0);
  }
  
  static int WriteBytes(const char* values, int len, SOCKET sock) {
    return send(sock, values, len, 0);
  }
  
  static char ReadByte(SOCKET sock, int &status) {
    char value;
    status = recv(sock, &value, 1, 0);
    if(status < 0) {
      return '\0';
    }

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
 * UDP socket support class
 ****************************/
class UDPSocket {
public:
  static bool Bind(int port, SOCKET& sock, struct sockaddr_in*& addr_in) {
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(sock > -1) {
      addr_in = new struct sockaddr_in;
      addr_in->sin_family = AF_INET;
      addr_in->sin_port = htons(port);
      addr_in->sin_addr.s_addr = htonl(INADDR_ANY);

      if(bind(sock, (struct sockaddr*)addr_in, sizeof(struct sockaddr_in)) < 0) {
        close(sock);
        sock = -1;

        delete addr_in;
        addr_in = nullptr;

        return false;
      }

      return true;
    }

    return false;
  }

  static void Close(SOCKET sock, struct sockaddr_in* addr_in) {
    close(sock);

    delete addr_in;
    addr_in = nullptr;
  }
};

/****************************
 * IP socket support class
 ****************************/
class IPSecureSocket {
 public:
  static bool Open(const char* address, int port, const char* pem_file, SSL_CTX* &ctx, BIO* &bio, X509* &cert) {
    ctx = SSL_CTX_new(SSLv23_client_method());
    bio = BIO_new_ssl_connect(ctx);
    if(!bio) {
      SSL_CTX_free(ctx);
      return false;
    }

    std::string cert_path; 
    if(pem_file) {
      cert_path = pem_file;
    }
    else {
      cert_path = UnicodeToBytes(GetLibraryPath()) + CACERT_PEM_FILE;
    }
    
    if(!SSL_CTX_load_verify_locations(ctx, cert_path.c_str(), nullptr)) {
      BIO_free_all(bio);
      SSL_CTX_free(ctx);
      return false;
    }
    
    SSL* ssl;
    BIO_get_ssl(bio, &ssl);
    SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);
    std::string ssl_address = address;
    if(ssl_address.size() < 1 || port < 0) {
      BIO_free_all(bio);
      SSL_CTX_free(ctx);
      return false;
    }
    ssl_address += ":";
    ssl_address += UnicodeToBytes(IntToString(port));
    BIO_set_conn_hostname(bio, ssl_address.c_str());
    
    if(!SSL_set_tlsext_host_name(ssl, address)) {
      BIO_free_all(bio);
      SSL_CTX_free(ctx);
      return false;
    }
    
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
    
    cert = SSL_get_peer_certificate(ssl);
    if(!cert) {
      BIO_free_all(bio);
      SSL_CTX_free(ctx);
      return false;
    }
    
    const int status = SSL_get_verify_result(ssl);
    if(status != X509_V_OK && status != X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT) {
      BIO_free_all(bio);
      SSL_CTX_free(ctx);
      X509_free(cert);
      return false;
    }
    
    return true;
  }
  
  static void WriteByte(char value, SSL_CTX* ctx, BIO* bio) {
    int status = BIO_write(bio, &value, 1);
    if(status < 0) {
      value = '\0';
    }
  }
  
  static int WriteBytes(const char* values, int len, SSL_CTX* ctx, BIO* bio) {
    int status = BIO_write(bio, values, len);
    if(status < 0) {
      return -1;
    } 
    
		return BIO_flush(bio);
  }

  static char ReadByte(SSL_CTX* ctx, BIO* bio, int &status) {
    char value;
    status = BIO_read(bio, &value, 1);
    if(status < 0) {
      return '\0';
    } 
    
    return value;
  }
  
  static int ReadBytes(char* values, int len, SSL_CTX* ctx, BIO* bio) {
    int status = BIO_read(bio, values, len);
    if(status < 0) {
      return -1;
    } 
    
    return status;
  }
  
  static void Close(SSL_CTX* ctx, BIO* bio, X509* cert) {
    if(bio) {
      BIO_free_all(bio);
    }

    if(ctx) {
      SSL_CTX_free(ctx);
    }

    if(cert) {
      X509_free(cert);
    }
  }
};

/****************************
 * System operations
 ****************************/
class System {
 public:
   static  std::vector<std::string> CommandOutput(const char* c, int& status) {
      std::vector<std::string> output;

     // create temporary file
     const std::string tmp_file_name = File::TempName();
     FILE* file = File::FileOpen(tmp_file_name.c_str(), "wb");
     if(file) {
       fclose(file);

       std::string str_cmd(c);
       str_cmd += " > ";
       str_cmd += tmp_file_name;

       // ignoring return value
       status = std::system(str_cmd.c_str());
       
       // read file output
       std::ifstream file_out(tmp_file_name.c_str());
       if(file_out.is_open()) {
         std::string line_out;
         while(getline(file_out, line_out)) {
           output.push_back(line_out);
         }
         file_out.close();

         // delete file
         remove(tmp_file_name.c_str());
       }
     }

     return output;
   }

  static std::string GetPlatform() {
    std::string platform;
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

  static size_t GetTotalSystemMemory()
  {
    const size_t pages = sysconf(_SC_PHYS_PAGES);
    const size_t page_size = sysconf(_SC_PAGE_SIZE);
    return pages * page_size;
  }
};

#endif
