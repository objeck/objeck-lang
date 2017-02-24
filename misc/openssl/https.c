/* Standard headers */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
 
/* OpenSSL headers */
#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

void openssltest() {
  // Set up a SSL_CTX object, which will tell our BIO object how to do its work
  SSL_CTX* ctx = SSL_CTX_new(SSLv23_client_method());
  // Create a SSL object pointer, which our BIO object will provide.
  SSL* ssl;
  // Create our BIO object for SSL connections.
  BIO* bio = BIO_new_ssl_connect(ctx);
  // Failure?
  if (bio == NULL) {
    printf("Error creating BIO!\n");
    ERR_print_errors_fp(stderr);
    // We need to free up the SSL_CTX before we leave.
    SSL_CTX_free(ctx);
    return;
  }
  // Makes ssl point to bio's SSL object.
  BIO_get_ssl(bio, &ssl);
  // Set the SSL to automatically retry on failure.
  SSL_set_mode(ssl, SSL_MODE_AUTO_RETRY);
  // We're connection to google.com on port 443.
  BIO_set_conn_hostname(bio, "www.sourceforge.net:https");

  // Same as before, try to connect.
  if (BIO_do_connect(bio) <= 0) {
    printf("Failed to connect!");
    BIO_free_all(bio);
    SSL_CTX_free(ctx);
    return;
  }

  // Now we need to do the SSL handshake, so we can communicate.
  if (BIO_do_handshake(bio) <= 0) {
    printf("Failed to do SSL handshake!");
    BIO_free_all(bio);
    SSL_CTX_free(ctx);
    return;
  }
  // Create a buffer for grabbing information from the page.
  char buf[1024];
  memset(buf, 0, sizeof(buf));
  // Create a buffer for the reqest we'll send to the server
  char send[1024];
  memset(send, 0, sizeof(send));
  // Create our GET request.
  strcat(send, "GET / HTTP/1.1\nHost:google.com\nUser Agent: Mozilla/5.0 (compatible; MSIE 9.0; Windows NT 6.1; Trident/5.0)\nConnection: Close\n\n");
  // BIO_puts sends a null-terminated string to the server. In this case it's our GET request.
  BIO_puts(bio, send);
  // Loop while there's information to be read.
  while (1) {
    // BIO_read() reads data from the server into a buffer. It returns the number of characters read in.
    int x = BIO_read(bio, buf, sizeof(buf) - 1);
    // If we haven't read in anything, assume there's nothing more to be sent since we used Connection: Close.
    if (x == 0) {
      break;
    }
    // If BIO_read() returns a negative number, there was an error
    else if (x < 0) {
      // BIO_should_retry lets us know if we should keep trying to read data or not.
      if (!BIO_should_retry(bio)) {
	printf("\nRead Failed!\n");
	BIO_free_all(bio);
	SSL_CTX_free(ctx);         
	return;
      }
    }
    // We actually got some data, without errors!
    else {
      // Null-terminate our buffer, just in case
      buf[x] = 0;
      // Echo what the server sent to the screen
      printf("%s", buf);
    }
  }
  // Free up that BIO object we created.
  BIO_free_all(bio);
  // Remember, we also need to free up that SSL_CTX object!
  SSL_CTX_free(ctx);
}


/**
 * Main SSL demonstration code entry point
 */
int main(int argc, char** argv) {
  CRYPTO_malloc_init(); // Initialize malloc, free, etc for OpenSSL's use
  SSL_library_init(); // Initialize OpenSSL's SSL libraries
  SSL_load_error_strings(); // Load SSL error strings
  ERR_load_BIO_strings(); // Load BIO error strings
  OpenSSL_add_all_algorithms(); // Load all available encryption algorithms
  
  openssltest(); 
}
