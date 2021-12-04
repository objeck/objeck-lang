/*
  Randy Hollines
  openssl req -x509 -newkey rsa:4096 -keyout key.pem -out cert.pem -sha256 -days 365 -subj '/CN=localhost'
*/

#include <stdio.h>
#include <string.h>
#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#ifdef _WIN32
#pragma comment(lib, "crypt32")
#endif

int pem_passwd_cb(char* buffer, int size, int rw_flag, void* passwd) {
  strncpy(buffer, (char*)passwd, size);
  buffer[size - 1] = '\0';
  return(strlen(buffer));
}

int main(int argc, char* argv[]) {
  if(argc != 4) {
    return 1;
  }
  else {
    char* cert_path = argv[1];
    char* key_path = argv[2];
    char* key_passwd = argv[3];
    char buffer[1024];

    //
    // start: SOCK_TCP_SSL_LISTEN
    //

    SSL_CTX *ctx = SSL_CTX_new(SSLv23_server_method());
    if(!ctx) {
      printf("ERROR for context: %s\n", ERR_reason_error_string(ERR_get_error()));
      exit(1);
    }

    // get password for private key
    if(key_passwd) {
      SSL_CTX_set_default_passwd_cb_userdata(ctx, key_passwd);
      SSL_CTX_set_default_passwd_cb(ctx, pem_passwd_cb);
    }

    // load certificates
    if(!SSL_CTX_use_certificate_file(ctx, cert_path, SSL_FILETYPE_PEM) || !SSL_CTX_use_PrivateKey_file(ctx, key_path, SSL_FILETYPE_PEM)) {
      printf("ERROR for certificate: %s\n", ERR_reason_error_string(ERR_get_error()));
      exit(1);
    }

    BIO* bio = BIO_new_ssl(ctx, 0);
    if(!bio) {
      printf("ERROR for new ssl: %s\n", ERR_reason_error_string(ERR_get_error()));
      exit(1);
    }

    // register and accept collections
    SSL* ssl = nullptr;
    BIO_get_ssl(bio, &ssl);
    SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);

    BIO* server_bio = BIO_new_accept("localhost:15001");
    BIO_set_accept_bios(server_bio, bio);
    BIO_do_accept(server_bio);

    //
    // end: SOCK_TCP_SSL_LISTEN
    //

    // wait for clients
    for(;;) {
      printf("DEBUG: waiting for connection\n");
      

      //
      // start: SOCK_TCP_SSL_ACCEPT
      //

      BIO_do_accept(server_bio);
      BIO* client_bio = BIO_pop(server_bio);

      if(BIO_do_handshake(client_bio) <= 0) {
        printf("ERROR handshake ssl: %s\n", ERR_reason_error_string(ERR_get_error()));
        exit(1);
      }


      //
      // end: SOCK_TCP_SSL_ACCEPT
      //

      char read_buffer[1024];
      int read = BIO_read(client_bio, read_buffer, 1024);
      if(read > 0) {
        puts(read_buffer);

        const char write_buffer[] = "HTTP/1.0 200 OK\r\nContent-type: text/plain\r\n\r\n";
        BIO_write(client_bio, write_buffer, strlen(write_buffer));
      }
    
      // close client connection  
      BIO_free_all(client_bio);
    }


    //
    // start: SOCK_TCP_SSL_SRV_CLOSE
    //

    // free server resources
    BIO_free_all(server_bio);
    BIO_free_all(bio);
    SSL_CTX_free(ctx);

    //
    // end: SOCK_TCP_SSL_SRV_CLOSE
    //

    return 0;
  }
}