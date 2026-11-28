/***************************************************************************
 * Defines the VM execution model.
 *
 * Copyright (c) 2025, Randy Hollines
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

#pragma once

// Must precede any Windows header (including those pulled in by mbedTLS/nghttp2)
// to prevent min/max macro definitions that break std::min/std::max.
//
// Guarded, because MinGW-w64's libstdc++ already defines it (os_defines.h) and
// an unconditional redefinition warned on every single translation unit of the
// MSYS2 build -- hundreds of lines of noise with a real diagnostic somewhere in
// the middle of it.
#if defined(_WIN32) && !defined(NOMINMAX)
#  define NOMINMAX
#endif

#include <algorithm>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <iterator>
#include <stack>
#include <vector>
#include <list>
#include <set>
#include <string>
#include <ctime>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <atomic>
#include <mutex>
#include <chrono>
#include <climits>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <time.h>
#include "../shared/instrs.h"
#include "../shared/sys.h"
#include "../shared/traps.h"
#include "../shared/int_ops.h"
#include "../shared/num_parse.h"
#include <mbedtls/ssl.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/pk.h>
#include <mbedtls/error.h>
#include <mbedtls/version.h>
#include <mbedtls/timing.h>
#include <mbedtls/ssl_cookie.h>
#include <mbedtls/sha256.h>
#include <mbedtls/md.h>
#ifdef OBJECK_HAS_NGHTTP2
// ssize_t is POSIX; define it from SSIZE_T before nghttp2.h parses it on Windows
#if defined(_WIN32) && !defined(_SSIZE_T_DEFINED)
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#define _SSIZE_T_DEFINED
#endif
#include <nghttp2/nghttp2.h>
#endif
#ifdef OBJECK_HAS_NGTCP2
#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>
#include <openssl/ssl.h>
#include <openssl/rand.h>
#include <ngtcp2/ngtcp2_crypto_boringssl.h>
#include <nghttp3/nghttp3.h>
#ifndef _WIN32
#include <poll.h>
#include <netdb.h>
#include <arpa/inet.h>
#endif
#endif

// Ends the process for a runtime error or Runtime->Exit (#877); defined in
// arch/memory.cpp. Call it instead of exit() anywhere another VM thread may be
// running.
[[noreturn]] void VmExit(int status);

// Native libraries the program loaded and has not unloaded; defined in
// arch/memory.cpp beside the exit path that uses them. A library is recorded once
// its load_lib has run and forgotten when DllProxy->Unload releases it.
// UnloadNativeLibrariesAtExit calls unload_lib of each one still recorded, so a
// library's own threads are stopped BEFORE exit() starts destroying the statics
// they use (theirs, and those of the libraries they link). It runs once, on the
// thread ending the process, and only where exit()'s teardown runs at all.
void NativeLibraryLoaded(void* handle);
void NativeLibraryUnloaded(void* handle);
void UnloadNativeLibrariesAtExit();

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#include <process.h>
#include <userenv.h>
#include <cstring>
#include <bcrypt.h>
#elif _OSX
#include <sys/random.h> 
#include <mach-o/dyld.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <dlfcn.h>
#include <pwd.h>
#include <unistd.h>
#include <sys/types.h>
#else
#include <sys/random.h> 
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <pthread.h>
#include <pwd.h>
#include <stdint.h>
#include <iomanip>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/types.h>
#endif

#define CALC_STACK_SIZE 16384
#define CACERT_PEM_FILE "cacert.pem"

//
// Verifying a server's certificate when the client connects by IP address.
//
// mbedTLS checks the name a client gives mbedtls_ssl_set_hostname against the
// certificate. From 3.5 an IP address there is matched against the
// certificate's iPAddress subjectAltNames; before 3.5 it is matched only
// against dNSName SANs and the CN, as text, so no certificate could ever name
// an address. Windows (vcpkg, 3.6) and macOS (bundled, 3.6) have the newer
// behaviour; Linux links the distribution's mbedTLS -- 2.28 on Ubuntu 24.04
// and Debian 12 -- so there a client of "127.0.0.1" refused a certificate that
// says IP:127.0.0.1, which is exactly the certificate it should accept.
//
// For mbedTLS before 3.5, TlsVerifyIpSan installs a verify callback that,
// for the server's own certificate only (depth 0), clears the name-mismatch
// flag when -- and only when -- the address the client connected to is one of
// the certificate's iPAddress SANs. Every other check (chain, trust anchor,
// expiry, key usage) is untouched, and a certificate that does not carry the
// address is still refused. From 3.5 it does nothing: mbedTLS matches the
// address itself. The TlsIpName must outlive the handshake (it lives in the
// connection's context).
//
struct TlsIpName {
  unsigned char ip[16];
  size_t len;
  TlsIpName() : len(0) { memset(ip, 0, sizeof(ip)); }
};

#if MBEDTLS_VERSION_NUMBER < 0x03050000
#include <mbedtls/asn1.h>
#include <mbedtls/oid.h>
#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

// true if crt carries ip (4 or 16 bytes) as an iPAddress subjectAltName
inline bool TlsCertHasIpSan(const mbedtls_x509_crt* crt, const unsigned char* ip, size_t ip_len)
{
  // the raw extensions; a private member, under its mangled name, in 3.0-3.4
#ifdef MBEDTLS_PRIVATE
  const mbedtls_x509_buf& v3_ext = crt->MBEDTLS_PRIVATE(v3_ext);
#else
  const mbedtls_x509_buf& v3_ext = crt->v3_ext;
#endif
  unsigned char* p = v3_ext.p;
  if(!p || v3_ext.len == 0) {
    return false;
  }
  const unsigned char* end = p + v3_ext.len;

  // Extensions ::= SEQUENCE OF Extension
  size_t len = 0;
  if(mbedtls_asn1_get_tag(&p, end, &len, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE) != 0) {
    return false;
  }
  end = p + len;

  while(p < end) {
    // Extension ::= SEQUENCE { extnID OID, critical BOOLEAN DEFAULT FALSE, extnValue OCTET STRING }
    size_t ext_len = 0;
    if(mbedtls_asn1_get_tag(&p, end, &ext_len, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE) != 0) {
      return false;
    }
    unsigned char* ext_end = p + ext_len;

    mbedtls_x509_buf oid;
    oid.tag = MBEDTLS_ASN1_OID;
    if(mbedtls_asn1_get_tag(&p, ext_end, &oid.len, MBEDTLS_ASN1_OID) != 0) {
      return false;
    }
    oid.p = p;
    p += oid.len;

    int critical = 0;
    const int bool_ret = mbedtls_asn1_get_bool(&p, ext_end, &critical);
    if(bool_ret != 0 && bool_ret != MBEDTLS_ERR_ASN1_UNEXPECTED_TAG) {
      return false;
    }

    size_t value_len = 0;
    if(mbedtls_asn1_get_tag(&p, ext_end, &value_len, MBEDTLS_ASN1_OCTET_STRING) != 0) {
      return false;
    }

    if(MBEDTLS_OID_CMP(MBEDTLS_OID_SUBJECT_ALT_NAME, &oid) == 0) {
      // GeneralNames ::= SEQUENCE OF GeneralName; iPAddress is [7] IMPLICIT OCTET STRING
      unsigned char* q = p;
      const unsigned char* q_end = p + value_len;
      size_t names_len = 0;
      if(mbedtls_asn1_get_tag(&q, q_end, &names_len, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE) != 0) {
        return false;
      }
      q_end = q + names_len;
      while(q < q_end) {
        const unsigned char tag = *q++;
        size_t name_len = 0;
        if(mbedtls_asn1_get_len(&q, q_end, &name_len) != 0) {
          return false;
        }
        if(tag == (MBEDTLS_ASN1_CONTEXT_SPECIFIC | 7) && name_len == ip_len && memcmp(q, ip, ip_len) == 0) {
          return true;
        }
        q += name_len;
      }
      return false;
    }

    p = ext_end;
  }

  return false;
}

inline int TlsIpSanVerifyCallback(void* data, mbedtls_x509_crt* crt, int depth, uint32_t* flags)
{
  const TlsIpName* name = (const TlsIpName*)data;
  if(depth == 0 && name && name->len > 0 && (*flags & MBEDTLS_X509_BADCERT_CN_MISMATCH) &&
     TlsCertHasIpSan(crt, name->ip, name->len)) {
    *flags &= ~(uint32_t)MBEDTLS_X509_BADCERT_CN_MISMATCH;
  }
  return 0;
}

inline void TlsVerifyIpSan(mbedtls_ssl_config* conf, TlsIpName* name, const char* address)
{
  name->len = 0;
  if(inet_pton(AF_INET, address, name->ip) == 1) {
    name->len = 4;
  }
  else if(inet_pton(AF_INET6, address, name->ip) == 1) {
    name->len = 16;
  }

  if(name->len > 0) {
    mbedtls_ssl_conf_verify(conf, TlsIpSanVerifyCallback, name);
  }
}
#else
inline void TlsVerifyIpSan(mbedtls_ssl_config* conf, TlsIpName* name, const char* address)
{
  (void)conf;
  (void)name;
  (void)address;
}
#endif

// The calling thread's last TLS failure and, for a certificate that failed
// verification, mbedTLS's verify flags saying why; for
// TCPSecureSocket->GetLastError() and TCPSecureSocketServer->GetLastError(),
// which are functions: a failed connect frees its context, so the error
// cannot live only there. (defined in common.cpp, as dtls_last_error is)
extern thread_local int tls_last_error;
extern thread_local uint32_t tls_last_verify_flags;
extern thread_local uint32_t dtls_last_verify_flags;

// mbedtls_ssl_get_verify_result's flags when error is a verification failure,
// else 0 (also 0 when there is no session to ask: it answers 0xFFFFFFFF)
inline uint32_t TlsVerifyFlags(int error, const mbedtls_ssl_context* ssl)
{
  if(error != MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) {
    return 0;
  }
  const uint32_t flags = mbedtls_ssl_get_verify_result(ssl);
  return flags == 0xFFFFFFFF ? 0 : flags;
}

//
// Certificate and key files, by a UTF-8 name. mbedtls opens them with fopen,
// which on Windows reads the name in the ANSI code page: the CA bundle in a
// library directory with a non-ASCII name, or a server certificate or key
// named that way, could not be loaded. On Windows the file is read here by its
// UTF-16 name and the bytes parsed, as the *_file calls do: with a NUL after
// them that the length counts, which PEM parsing requires.
//
#ifdef _WIN32
inline bool ReadTlsFile(const std::string& name, std::vector<unsigned char>& data) {
  std::ifstream in(ToFsPath(BytesToUnicode(name)).c_str(), std::ios::binary);
  if(!in.good()) {
    return false;
  }

  data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  data.push_back('\0');
  return true;
}
#endif

inline int TlsParseCertFile(mbedtls_x509_crt* crt, const std::string& name) {
#ifdef _WIN32
  std::vector<unsigned char> data;
  if(!ReadTlsFile(name, data)) {
    return MBEDTLS_ERR_X509_FILE_IO_ERROR;
  }
  return mbedtls_x509_crt_parse(crt, data.data(), data.size());
#else
  return mbedtls_x509_crt_parse_file(crt, name.c_str());
#endif
}

inline int TlsParseKeyFile(mbedtls_pk_context* pk, const std::string& name, const char* passwd,
                           int (*f_rng)(void*, unsigned char*, size_t), void* p_rng) {
#ifdef _WIN32
  std::vector<unsigned char> data;
  if(!ReadTlsFile(name, data)) {
    return MBEDTLS_ERR_PK_FILE_IO_ERROR;
  }
  const unsigned char* pwd = (const unsigned char*)passwd;
  const size_t pwd_len = passwd ? strlen(passwd) : 0;
#if MBEDTLS_VERSION_MAJOR >= 3
  const int ret = mbedtls_pk_parse_key(pk, data.data(), data.size(), pwd, pwd_len, f_rng, p_rng);
#else
  (void)f_rng; (void)p_rng;
  const int ret = mbedtls_pk_parse_key(pk, data.data(), data.size(), pwd, pwd_len);
#endif
  // the key material does not outlive the parse
  volatile unsigned char* wipe = data.data();
  for(size_t i = 0; i < data.size(); ++i) {
    wipe[i] = 0;
  }
  return ret;
#else
#if MBEDTLS_VERSION_MAJOR >= 3
  return mbedtls_pk_parse_keyfile(pk, name.c_str(), passwd, f_rng, p_rng);
#else
  (void)f_rng; (void)p_rng;
  return mbedtls_pk_parse_keyfile(pk, name.c_str(), passwd);
#endif
#endif
}

//
// Why a socket call failed, captured as it failed. code is WSAGetLastError()
// on Windows and errno elsewhere -- or, when resolver is set, the EAI_* code
// getaddrinfo returned: WSA codes on Windows too, but on POSIX not errno
// values, so they are described with gai_strerror. A code of 0 is no failure.
//
struct SocketError {
  int code;
  bool resolver;
};

// The calling thread's last plain socket failure, for the GetLastError() of
// TCPSocket, TCPSocketServer, UDPSocket and UDPSocketServer. Each call that
// reaches the system -- connect, bind, listen, accept, read, write, select, a
// socket option -- replaces it with its own outcome: the failure, or none.
// Read when GetLastError ran, the live value had been reset by the cleanup
// after the failure (a successful closesocket() sets WSAGetLastError() to 0)
// or replaced by whatever ran in between; Close() leaves it alone.
// (defined in common.cpp; see dtls_last_error below for why not inline)
extern thread_local SocketError socket_last_error;

//
// The receive timeouts a client socket starts with, in milliseconds, before a
// program sets one (TCPSocket->SetRecvTimeout, HttpClient->SetReadTimeout). A
// plain TCP client gets 30 seconds. A TLS client gets 300: API.OpenAI and
// API.Google wait on non-streaming HTTPS requests while a model reasons, often
// silently for longer than 30 seconds. OBJECK_TLS_RECV_TIMEOUT overrides the TLS
// default. 0 means no timeout. Web.HTTP.HttpClient->DEFAULT_READ_TIMEOUT_MS and
// Web.HTTP.HttpsClient->DEFAULT_READ_TIMEOUT_MS are these values.
//
const int TCP_DEFAULT_RECV_TIMEOUT_MS = 30000;
const int TLS_DEFAULT_RECV_TIMEOUT_MS = 300000;

//
// A monotonic clock in milliseconds, for socket deadlines: a deadline is an
// instant on this clock, never a wall-clock time, so a clock that steps (NTP,
// a VM resuming) cannot stretch or cut it. 0 is never a deadline, it means none.
//
inline long long SocketNowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count() + 1;
}

//
// How long the next blocking read may wait: the shorter of read_ms (0 is no
// per-read limit) and the time left until deadline_ms (0 is no deadline). -1 is
// no bound at all, 0 is a deadline already past, above 0 the milliseconds to
// wait. Every blocking read checks this first, so a peer that sends a byte at a
// time cannot keep a read going past the deadline: each wait is cut to what is
// left, not restarted.
//
inline int SocketWaitBudget(long long deadline_ms, int read_ms) {
  long long wait = read_ms > 0 ? read_ms : -1;
  if(deadline_ms > 0) {
    const long long left = deadline_ms - SocketNowMs();
    if(left <= 0) {
      return 0;
    }
    if(wait < 0 || left < wait) {
      wait = left;
    }
  }

  return wait > INT_MAX ? INT_MAX : (int)wait;
}

// the OS reason behind an mbedTLS NET_* failure (connect, bind, listen) in
// tls_last_error or dtls_last_error, for the text of GetLastError()
extern thread_local SocketError tls_socket_error;
extern thread_local SocketError dtls_socket_error;

//
// One TLS library, many threads.
//
// mbedTLS keeps state that the connections of a process share: the PSA key store that
// every TLS 1.3 handshake and record uses, and, per server, the private key (an RSA key
// updates its blinding values on every signature) and the random generator that all of
// that server's connections draw from. It guards that state with mutexes of its own only
// when it is built with MBEDTLS_THREADING_C. The Linux distributions and the macOS build
// are; vcpkg's default mbedtls port, which the Windows builds link, is not.
//
// Objeck gives every connection of a server its own thread, and a program may open TLS
// clients from as many threads as it likes. Against a library without threading, eight
// threads making TLS connections to one in-process server ended with every later
// handshake failing ("SSL - The handshake negotiation failed", "Verification of the
// message MAC failed") and a server thread spinning in mbedtls_mpi_div_mpi, called from
// the RSA signature of a handshake, on corrupted blinding values.
//
// So when the library cannot protect itself, the VM does: every call into mbedTLS is made
// holding one process-wide lock (TlsLibraryLock, which a thread may take again while it
// holds it), and the lock is let go for as long as a call waits on the network
// (TlsLibraryUnlock, in the record I/O callbacks and around a connect), so a slow or
// silent peer holds up nobody else. The cryptography of concurrent connections then runs
// one at a time. When the library has threading, both classes are empty and nothing is
// serialized.
//
#if defined(MBEDTLS_THREADING_C)
class TlsLibraryLock {
 public:
  TlsLibraryLock() {}
  ~TlsLibraryLock() {}
};

class TlsLibraryUnlock {
 public:
  TlsLibraryUnlock() {}
  ~TlsLibraryUnlock() {}
};
#else
// (defined in common.cpp) TlsLibraryEnter and TlsLibraryLeave nest; TlsLibraryYield lets
// go of every level this thread holds and answers how many, for TlsLibraryResume
void TlsLibraryEnter();
void TlsLibraryLeave();
int TlsLibraryYield();
void TlsLibraryResume(int depth);

class TlsLibraryLock {
 public:
  TlsLibraryLock() { TlsLibraryEnter(); }
  ~TlsLibraryLock() { TlsLibraryLeave(); }
  TlsLibraryLock(const TlsLibraryLock&) = delete;
  TlsLibraryLock& operator=(const TlsLibraryLock&) = delete;
};

class TlsLibraryUnlock {
  int depth;

 public:
  TlsLibraryUnlock() : depth(TlsLibraryYield()) {}
  ~TlsLibraryUnlock() { TlsLibraryResume(depth); }
  TlsLibraryUnlock(const TlsLibraryUnlock&) = delete;
  TlsLibraryUnlock& operator=(const TlsLibraryUnlock&) = delete;
};
#endif

//
// mbedTLS context wrappers for SSL/TLS connections
//
// A context is C heap that a socket object points to, and more than one thread can be using it when
// the object is closed (see "TLS contexts and the threads that use them" in common.cpp, where the
// counts below are kept, under one lock): it is freed when its last user lets go, not by Close.
//
struct SecureServerCtx;
void TlsServerRelease(SecureServerCtx* srv);
struct DtlsServerCtx;
void DtlsServerRelease(DtlsServerCtx* srv);
#ifdef _WIN32
// Windows: the clients of a DTLS server and their queued datagrams (see "A DTLS server's clients on
// Windows" in common.cpp)
struct DtlsPeer;
struct DtlsPeerTable;
void DtlsPeerForget(DtlsServerCtx* srv, DtlsPeer* peer);
int DtlsPeerReadable(DtlsServerCtx* srv, DtlsPeer* peer);
void DtlsPeerTableFree(DtlsPeerTable* table);
#endif

struct SecureSocketCtx {
  mbedtls_ssl_context ssl;
  mbedtls_ssl_config conf;
  mbedtls_net_context net;
  mbedtls_x509_crt cacert;
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context ctr_drbg;
  // the address connected to, when it is one (see TlsVerifyIpSan)
  TlsIpName ip_name;
  int last_error;
  // reads fail once the monotonic clock (SocketNowMs) passes deadline_ms; 0 is no
  // deadline. read_ms bounds each wait for the peer; 0 is no per-read limit.
  // Both are used by TlsBioRecv.
  long long deadline_ms;
  int read_ms;
  // this side has ended its sending (IPSecureSocket::ShutdownSend): the
  // close_notify has gone out and the socket takes no more writes, so Close
  // does not send a second one into a socket that would refuse it
  bool send_closed;
  // a byte read from the session and given back, or -1: the next byte a read returns (ReadByte and
  // ReadBytes take it first). A Char[] read that completes its last character reads one byte at a time
  // and can find one that belongs to the next character; TLS cannot look at a byte without reading it.
  int pushback;
  // Held by a read of this session from its first byte to its last, the look ahead that completes a
  // character and the push back of a byte that was not part of it included (the TCPSecureSocket read
  // traps in common.cpp), and by Select while it asks whether a read would find anything, if it is
  // free (ssl_ready_for_io). Two threads reading one session otherwise took bytes in between each
  // other's steps, and 'pushback' was written by one while the other read it. A wait for this lock
  // is made parked. Close() never takes it: it ends a read by shutting the connection down.
  std::mutex read_lock;
  // an accepted connection: the server whose configuration (certificate, key, random generator) 'ssl'
  // was set up with and reads for as long as it lives. A reference, given back when this is freed, so
  // closing the listener does not free the configuration under the connections it accepted.
  SecureServerCtx* server;
  // the traps using this context now (TlsSocketUse), and whether its socket object has been closed:
  // freed when it is closed and nobody is using it. Under the lock in common.cpp.
  int users;
  bool closed;

  void SetError(int error) {
    SetError(error, SocketError{ 0, false });
  }

  // a failure with the OS reason behind it (a connect that failed)
  void SetError(int error, const SocketError& reason) {
    last_error = error;
    tls_last_error = error;
    tls_last_verify_flags = TlsVerifyFlags(error, &ssl);
    tls_socket_error = reason;
  }

  SecureSocketCtx() : last_error(0), deadline_ms(0), read_ms(0), send_closed(false), pushback(-1), server(nullptr), users(0), closed(false) {
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    mbedtls_net_init(&net);
    mbedtls_x509_crt_init(&cacert);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
  }

  ~SecureSocketCtx() {
    TlsLibraryLock library_lock;
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_net_free(&net);
    mbedtls_x509_crt_free(&cacert);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    // after mbedtls_ssl_free: the session no longer reads the server's configuration
    if(server) {
      TlsServerRelease(server);
    }
  }
};

// The calling thread's last DTLS failure, for DTLSSocket->GetLastError() and
// DTLSSocketServer->GetLastError(), which are functions: a failed connect or
// accept frees its context, so the error cannot live only there.
// (defined in common.cpp; not an inline variable, which native libraries
// built as C++14 cannot compile)
extern thread_local int dtls_last_error;

//
// TLS record I/O for mbedtls_ssl_set_bio, whose context is the SecureSocketCtx (a
// connecting socket and an accepted one alike, so SetDeadline holds on both):
// TlsBioRecv waits for the peer no longer than the socket's deadline and read
// timeout allow, and reports a read that timed out as MBEDTLS_ERR_SSL_TIMEOUT,
// whether the deadline or SO_RCVTIMEO ended it. (defined in common.cpp)
//
int TlsBioSend(void* ctx, const unsigned char* buf, size_t len);
int TlsBioRecv(void* ctx, unsigned char* buf, size_t len);

//
// mbedtls_net_send and mbedtls_net_recv for mbedtls_ssl_set_bio, whose context is the
// mbedtls_net_context (an HTTP/2 client, which has no deadline): the same calls,
// made without holding the TLS library (see TlsLibraryLock). (defined in common.cpp)
//
int TlsNetSend(void* ctx, const unsigned char* buf, size_t len);
int TlsNetRecv(void* ctx, unsigned char* buf, size_t len);

//
// DTLS record I/O for mbedtls_ssl_set_bio, whose context is the
// DtlsSocketCtx: mbedtls_net_send, _recv and _recv_timeout on its descriptor,
// except that a send first asks whether the network has already refused the
// peer (an ICMP port unreachable for an earlier datagram), and a failure keeps
// its OS reason for GetLastError. Windows never fails a UDP send for that
// refusal -- only a later receive reports it -- so a DTLS client whose server
// had gone wrote into the void, answering true, for ever. (defined in
// common.cpp)
//
int DtlsBioSend(void* ctx, const unsigned char* buf, size_t len);
int DtlsBioRecv(void* ctx, unsigned char* buf, size_t len);
int DtlsBioRecvTimeout(void* ctx, unsigned char* buf, size_t len, uint32_t timeout);

struct DtlsSocketCtx {
  mbedtls_ssl_context ssl;
  mbedtls_ssl_config conf;
  mbedtls_net_context net;
  mbedtls_x509_crt cacert;
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context ctr_drbg;
  // zero-initialized: mbedtls has no init function for this type, and it is handed
  // to mbedtls_ssl_set_timer_cb as opaque data. The normal DTLS flow calls
  // set_delay before get_delay, so this is defensive rather than a live fault.
  mbedtls_timing_delay_context timer{};
  // the address connected to, when it is one (see TlsVerifyIpSan)
  TlsIpName ip_name;
  int last_error;
  // the OS reason behind the last failed record send or receive (DtlsBioSend,
  // DtlsBioRecv), taken by the next SetError
  SocketError net_reason;
  // as SecureSocketCtx's: the server whose configuration an accepted connection reads (a reference),
  // the traps using this context now, and whether its socket object has been closed
  DtlsServerCtx* server;
  int users;
  bool closed;
#ifdef _WIN32
  // a connection a server accepted has no socket of its own on Windows (net.fd is -1): it is this
  // client of the server's one socket
  DtlsPeer* peer;
#endif
  // As SecureSocketCtx's: a byte read from the session and given back, or -1 (a line read that
  // looked at the byte after a CR and found it was not an LF: see SockDtlsInString in common.cpp), and the
  // lock a read holds from its first byte to the byte it gives back, which Select tries.
  int pushback;
  std::mutex read_lock;

  void SetError(int error) {
    const SocketError reason = net_reason;
    net_reason = SocketError{ 0, false };
    SetError(error, reason);
  }

  // a failure with the OS reason behind it (a connect that failed)
  void SetError(int error, const SocketError& reason) {
    last_error = error;
    dtls_last_error = error;
    dtls_last_verify_flags = TlsVerifyFlags(error, &ssl);
    dtls_socket_error = reason;
  }

  DtlsSocketCtx() : last_error(0), net_reason{ 0, false }, server(nullptr), users(0), closed(false),
#ifdef _WIN32
    peer(nullptr),
#endif
    pushback(-1)
  {
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    mbedtls_net_init(&net);
    mbedtls_x509_crt_init(&cacert);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
  }

  ~DtlsSocketCtx() {
    TlsLibraryLock library_lock;
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_net_free(&net);
    mbedtls_x509_crt_free(&cacert);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    // after mbedtls_ssl_free: the session no longer reads the server's configuration
    if(server) {
#ifdef _WIN32
      if(peer) {
        DtlsPeerForget(server, peer);
      }
#endif
      DtlsServerRelease(server);
    }
  }
};

struct SecureServerCtx {
  mbedtls_ssl_config conf;
  mbedtls_x509_crt srvcert;
  mbedtls_pk_context pkey;
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context ctr_drbg;
  mbedtls_net_context listen_fd;
  int last_error;
  // who still needs this: the server object until it is closed (1), each Accept in flight and each
  // accepted connection not yet freed. Under the lock in common.cpp; freed by TlsServerRelease at 0.
  int refs;
  // the Accept calls inside the listening descriptor now, and whether the server has been closed: a
  // closed server's descriptor is closed once those calls have left it (ServerCtxClose in common.cpp)
  int accepting;
  bool closed;

  SecureServerCtx() : last_error(0), refs(1), accepting(0), closed(false) {
    mbedtls_ssl_config_init(&conf);
    mbedtls_x509_crt_init(&srvcert);
    mbedtls_pk_init(&pkey);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_net_init(&listen_fd);
  }

  ~SecureServerCtx() {
    TlsLibraryLock library_lock;
    mbedtls_ssl_config_free(&conf);
    mbedtls_x509_crt_free(&srvcert);
    mbedtls_pk_free(&pkey);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    mbedtls_net_free(&listen_fd);
  }
};

struct DtlsServerCtx {
  mbedtls_ssl_config conf;
  mbedtls_x509_crt srvcert;
  mbedtls_pk_context pkey;
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context ctr_drbg;
  mbedtls_net_context listen_fd;
  mbedtls_ssl_cookie_ctx cookie_ctx;
  int last_error;
  // the listening port, to bind a fresh listener after each accept
  int port;
  // the one address the server was given, if any (bound_len 0: the
  // wildcard), so the fresh listener binds it again rather than the wildcard.
  // Raw bytes the size of a sockaddr_storage: the socket headers are not
  // included here on every platform.
  alignas(8) unsigned char bound_addr[128];
  int bound_len;
  // as SecureServerCtx's: who still needs this, the Accept calls inside the listening descriptor,
  // and whether the server has been closed
  int refs;
  int accepting;
  bool closed;
#ifdef _WIN32
  // the clients sharing listen_fd, made by Listen
  DtlsPeerTable* peers;
#else
  // one Accept at a time waits on the listener and takes it for its client (DtlsAccept): two that
  // waited together both saw the same ClientHello and both took the same socket
  std::mutex accept_turn;
#endif

  DtlsServerCtx() : last_error(0), port(0), bound_len(0), refs(1), accepting(0), closed(false)
#ifdef _WIN32
    , peers(nullptr)
#endif
  {
    memset(&bound_addr, 0, sizeof(bound_addr));
    mbedtls_ssl_config_init(&conf);
    mbedtls_x509_crt_init(&srvcert);
    mbedtls_pk_init(&pkey);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_net_init(&listen_fd);
    mbedtls_ssl_cookie_init(&cookie_ctx);
  }

  ~DtlsServerCtx() {
    TlsLibraryLock library_lock;
    mbedtls_ssl_cookie_free(&cookie_ctx);
    mbedtls_ssl_config_free(&conf);
    mbedtls_x509_crt_free(&srvcert);
    mbedtls_pk_free(&pkey);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
    mbedtls_net_free(&listen_fd);
#ifdef _WIN32
    DtlsPeerTableFree(peers);
#endif
  }
};

//
// HTTP/2 session context (nghttp2-based)
//
struct Http2SessionCtx {
#ifdef OBJECK_HAS_NGHTTP2
  nghttp2_session* session;
#else
  void* session;
#endif
  SecureSocketCtx* tls;
  int              last_stream_id;
  int              response_status;
  bool             response_complete;
  std::map<std::string, std::string> response_headers;
  std::vector<uint8_t> response_body;
  std::map<std::string, std::string> request_headers;

  Http2SessionCtx() : session(nullptr), tls(nullptr),
      last_stream_id(0), response_status(0), response_complete(false) {}

  ~Http2SessionCtx() {
#ifdef OBJECK_HAS_NGHTTP2
    if(session) nghttp2_session_del(session);
#endif
    if(tls) {
      mbedtls_ssl_close_notify(&tls->ssl);
      delete tls;
    }
  }
};

//
// HTTP/3 session context (ngtcp2+nghttp3 based)
//
struct Http3SessionCtx {
#ifdef OBJECK_HAS_NGTCP2
  ngtcp2_conn*                    conn;
  nghttp3_conn*                   h3conn;
  ngtcp2_crypto_conn_ref          conn_ref;
  SSL_CTX*                        ssl_ctx;
  SSL*                            ssl;
  struct sockaddr_storage         local_addr;
  struct sockaddr_storage         remote_addr;
  socklen_t                       local_addrlen;
  socklen_t                       remote_addrlen;
  bool                            handshake_complete;
  int64_t                         last_stream_id;
  std::string                     host;
#else
  void* conn;
  void* h3conn;
#endif
  int    udp_fd;
  int    response_status;
  bool   response_complete;
  std::map<std::string, std::string> response_headers;
  std::vector<uint8_t> response_body;
  std::map<std::string, std::string> request_headers;

  Http3SessionCtx() : conn(nullptr), h3conn(nullptr),
#ifdef OBJECK_HAS_NGTCP2
      // Value-initialized, not left indeterminate: the address pair is handed
      // to ngtcp2 as a path, and sockaddr_storage carries padding (__ss_align,
      // __ss_padding) that a later memcpy of a smaller sockaddr never writes.
      conn_ref(), ssl_ctx(nullptr), ssl(nullptr),
      local_addr(), remote_addr(),
      local_addrlen(0), remote_addrlen(0),
      handshake_complete(false), last_stream_id(-1),
#endif
      udp_fd(-1), response_status(0), response_complete(false) {}

  ~Http3SessionCtx() {
#ifdef OBJECK_HAS_NGTCP2
    if(h3conn) nghttp3_conn_del(h3conn);
    if(conn) ngtcp2_conn_del(conn);
    if(ssl) SSL_free(ssl);
    if(ssl_ctx) SSL_CTX_free(ssl_ctx);
#endif
    if(udp_fd >= 0) {
#ifdef _WIN32
      closesocket(udp_fd);
#else
      close(udp_fd);
#endif
    }
  }
};

#ifdef _WIN32
#define MUTEX_LOCK EnterCriticalSection
#define MUTEX_UNLOCK LeaveCriticalSection
#define WAKE_ALL_CONDITION(cv) WakeAllConditionVariable(cv)
#define SLEEP_CONDITION(cv, cs) SleepConditionVariableCS((cv), (cs), INFINITE)
#else
#define MUTEX_LOCK pthread_mutex_lock
#define MUTEX_UNLOCK pthread_mutex_unlock
#define WAKE_ALL_CONDITION(cv) pthread_cond_broadcast(cv)
#define SLEEP_CONDITION(cv, mtx) pthread_cond_wait((cv), (mtx))
#endif

using namespace instructions;

#define INF_ENDING -2

enum {
  VM_SIGABRT = -40,
  VM_SIGFPE,
  VM_SIGILL,
  VM_SIGINT,
  VM_SIGSEGV,
  VM_SIGTERM
};

class StackClass;

//
// String->ToFloat (S2F, interpreted and JIT): strtod's answer. Text that is
// not a number is 0.0, as before. Out-of-range text no longer aborts: it was
// std::stod, whose std::out_of_range nothing caught, so "1e400", "-1e400" and
// "1e-400" ended the VM with "internal error: stod argument out of range" (and
// so may a valid subnormal such as "5e-324" on a C library that sets ERANGE
// for one; C allows it, the UCRT does not). Now overflow is +/-inf and
// underflow 0 or the nearest subnormal, with the sign kept.
//
inline double ParseFloatText(const wchar_t* str)
{
  wchar_t* end = nullptr;
  const double value = wcstod(str, &end);
  if(end == str) {
    return 0.0;
  }
  return value;
}

//
// String->ToInt(base) (S2I, interpreted and JIT): "0b", "0o" and "0x"
// prefixes, also after a '-', override the base; text that is not a number,
// or does not fit 64 bits, is 0. The JIT kept its own copy, which knew no
// signed prefix, so "-0x10" was -16 interpreted and 0 once ToInt compiled.
//
// The leading number is read as std::stoll read it -- digits after it are
// ignored, so "12abc" is 12 -- but with wcstoll, whose failures are values
// rather than exceptions.
//
inline int64_t ParseIntPrefix(const wchar_t* str, int base)
{
  wchar_t* end = nullptr;
  errno = 0;
  const long long value = wcstoll(str, &end, base);
  if(end == str || errno == ERANGE || errno == EINVAL) {
    return 0;
  }
  return (int64_t)value;
}

inline int64_t ParseIntText(const wchar_t* str, int base)
{
  const size_t size = wcslen(str);
  if(size > 3 && str[0] == L'-') {
    switch(str[2]) {
    case L'b':
      return objeck_int::Sub(0, ParseIntPrefix(str + 3, 2));
    case L'o':
      return objeck_int::Sub(0, ParseIntPrefix(str + 3, 8));
    case L'x':
    case L'X':
      return objeck_int::Sub(0, ParseIntPrefix(str + 3, 16));
    default:
      break;
    }
  }
  else if(size > 2) {
    switch(str[1]) {
    case L'b':
      return ParseIntPrefix(str + 2, 2);
    case L'o':
      return ParseIntPrefix(str + 2, 8);
    case L'x':
    case L'X':
      return ParseIntPrefix(str + 2, 16);
    default:
      break;
    }
  }
  return ParseIntPrefix(str, base);
}

//
// A float-formatting precision -- the "float:string:precision" property that
// Float->ToString reads, and "precision", which the stderr float writer reads
// -- as a whole decimal int. Both used to go through std::stoll, uncaught, so
// Runtime->SetProperty("float:string:precision", "not-a-number") and any later
// Float->ToString ended the program with ">>> virtual machine: internal
// error: invalid stoll argument <<<", and a value past 32 bits was truncated
// by the cast. Runtime->SetProperty now refuses such a value with a
// recoverable error (common.cpp, SetSysProp), so the property never holds
// one; a reader that still meets one formats as if no precision were set.
//
inline bool ParseFloatPrecision(const std::wstring& text, int& precision)
{
  return objeck_parse::ToInt(text, precision);
}

inline const std::wstring IntToString(int v)
{
  return std::to_wstring(v);
}

/********************************
 * StackDclr struct
 ********************************/
struct StackDclr 
{
  std::wstring name;
  ParamType type;
  long id;
};

/********************************
 * StackInstr class
 ********************************/
class StackInstr
{
  // type and operand3 are rewritten at run time while other threads may be
  // executing the same instruction: JitCompiler::PatchCallSites turns a
  // MTHD_CALL into MTHD_CALL_JIT and sets operand3 to 1 once the callee's
  // native code is published, and StackInterpreter::CheckAutoJit sets operand3
  // to -1 when a compile definitively failed. (A DYN_MTHD_CALL is never
  // rewritten: its operands name no method.) They are plain fields on purpose.
  // The protocol that makes that safe in practice, with the ordering argument
  // for ARM64 at PatchCallSites and in docs/design/JIT_CALL_SITE_PATCHING.md:
  //   - each is one naturally aligned word, written whole by a single store;
  //   - every value a writer stores is valid on its own, whichever a reader
  //     sees, and a site never needs the two fields to agree with each other;
  //   - the rewrites only move a site toward "compiled" (or "interpret"), and
  //     the readers that act on "compiled" re-check GetNativeCode(), which is
  //     published with release/acquire, before running native code
  //     (ProcessJitMethodCall), so a stale or early read only costs a trip
  //     through the interpreter.
  // The JIT backends also store into operand3 of the variable-access
  // instructions of a method they compile or inline (frame offsets); the
  // interpreter reads operand3 only for call sites and JMP_TABLE (its default
  // target), never for those. Formally these are still data races. std::atomic with relaxed loads was
  // measured instead (2026-10-31, Linux x64, interleaved medians of 5 with
  // every function 64-byte aligned so placement could not decide it): +1.5%
  // geomean over 17 interpreted benchmarks, up to +10.7% (bench_strength_ext);
  // the compiler schedules the dispatch loop's opcode loads differently once
  // they are atomic. Above the 1% bar, so not adopted; revisit if a reader ever needs both fields to
  // be consistent, or a write stops being a single word.
  InstructionType type;
  long operand;
  union {
    long operand2;
    INT64_VALUE int64_operand;
    FLOAT_VALUE float_operand;
  } alt_operand;
  long operand3;
  long native_offset;
  int line_num;

 public:
  StackInstr() : type(END_STMTS), operand(0), operand3(0), native_offset(0), line_num(-1) {
    alt_operand.operand2 = 0;
  }

  StackInstr(int l, INT64_VALUE v) {
    line_num = l;
    type = LOAD_INT_LIT;
    alt_operand.int64_operand = v;
  }

  StackInstr(int l, InstructionType t) {
    line_num = l;
    type = t;
    operand = operand3 = native_offset = 0;
  }

  StackInstr(int l, InstructionType t, long o) {
    line_num = l;
    type = t;
    operand = o;
    operand3 = native_offset = 0;
  }

  StackInstr(int l, InstructionType t, FLOAT_VALUE f) {
    line_num = l;
    type = t;
    alt_operand.float_operand = f;
    operand = operand3 = native_offset = 0;
  }

  StackInstr(int l, InstructionType t, long o1, long o2) {
    line_num = l;
    type = t;
    operand = o1;
    alt_operand.operand2 = o2;
    operand3 = native_offset = 0;
  }

  StackInstr(int l, InstructionType t, long o1, long o2, long o3) {
    line_num = l;
    type = t;
    operand = o1;
    alt_operand.operand2 = o2;
    operand3 = o3;
    native_offset = 0;
  }

  ~StackInstr() {
  }  

  inline InstructionType GetType() const {
    return type;
  }

  inline long GetOperand() const {
    return operand;
  }

  inline long GetOperand2() const {
    return alt_operand.operand2;
  }

  inline long GetOperand3() const {
    return operand3;
  }

  // Jump operands -- the ONE statement of the convention. A control-flow target
  // (the operand of JMP, TRY_START and JMP_TABLE_SLOT, and the operand3 default
  // of JMP_TABLE) is stored as the index of the instruction that follows the
  // target's LBL: the compiler writes "LBL index + 1" (JumpToLocation) and the
  // loader verifies that the instruction just before it is a LBL. Execution
  // therefore resumes AT that index (the interpreter's `ip = operand`), and the
  // LBL itself, which emits no code, is one index earlier.
  //   JumpTargetIndex()      the first instruction run after the jump; what the
  //                          interpreter and the AMD64 JIT key their loop
  //                          headers, safepoints and patching on
  //   JumpTargetLabelIndex() the LBL that names it; what the ARM64 JIT keys on
  // Both name the same place (a LBL emits no code); a backend picks one and uses
  // it throughout. Never subtract 1 from a raw operand to get there.
  inline long JumpTargetIndex() const {
    return operand;
  }

  inline long JumpTargetLabelIndex() const {
    return operand - 1;
  }

  // JMP_TABLE's out-of-range target, held in operand3 (the slots hold the rest)
  inline long JumpDefaultTargetIndex() const {
    return operand3;
  }

  inline long JumpDefaultTargetLabelIndex() const {
    return operand3 - 1;
  }

  inline INT64_VALUE GetInt64Operand() const {
    return alt_operand.int64_operand;
  }

  inline FLOAT_VALUE GetFloatOperand() const {
    return alt_operand.float_operand;
  }

  inline long GetOffset() const {
    return native_offset;
  }

  int GetLineNumber() const {
    return line_num;
  }

  inline void SetType(InstructionType t) {
    type = t;
  }

  inline void SetOperand3(long o3) {
    operand3 = o3;
  }

  inline void SetOffset(long o) {
    native_offset = o;
  }
};

class StackClass;
class StackMethod;

/********************************
 * A compiled call site's inline cache for a callee that is bound at run
 * time (the calling convention's phase 3): a `virtual` declaration, keyed
 * by the receiver's class, or a func-ref call, keyed by the func-ref's
 * packed word. The site's `current` word -- the first member, which
 * compiled code reads as [site] -- points at the record for the key last
 * seen there, or is null. A record is written once, under the site's lock,
 * and published by that word, so a hit reads one consistent record; the
 * fill resolver reuses a key's record on a repeat and stops filling once
 * the records are used up, after which the site takes the bridge for good.
 * Owned by the caller's NativeCode.
 *
 * Who reads what, across threads:
 *   - compiled code reads `current`, then the record it points at. The
 *     record's three words are stored before `current` is (release), and a
 *     record is never written again.
 *   - a miss (JitResolveVirtualSite) looks through records[0, used) WITHOUT
 *     the site's lock, to skip the resolve for a class it has seen. `used`
 *     is therefore atomic: stored (release) only after the record it admits
 *     is complete, loaded (acquire) before the scan, so a scan reads only
 *     finished records. It was a plain int, read while a fill on another
 *     thread incremented it: a formal data race (ThreadSanitizer, v2026.11.4
 *     soak), harmless only because the fill looks the key up again under
 *     the lock.
 *   - the constructor's last act is a release store of `used`. The site
 *     reaches other threads through the machine code that embeds its
 *     address, published by StackMethod::SetNativeCode, and a compiled
 *     caller can reach that code by loading native_entry itself, with no
 *     acquire the C++ side (or a race detector) can see. The acquire load of
 *     `used` that begins every miss orders the constructor's plain stores
 *     (declaration, decl_cls_id, decl_mthd_id) before the miss reads them.
 ********************************/
struct JitVirtualRecord {
  size_t key;             // the receiver class, or the func-ref word, this record answers for
  void* entry;            // the target's native entry (StackMethod::GetNativeEntry)
  StackMethod* target;    // for the error report
};

struct JitVirtualSite {
  static const int RECORDS = 4;
  std::atomic<JitVirtualRecord*> current;   // read first by compiled code: keep first
  std::atomic<bool> filling;
  std::atomic<int> used;                    // records[0, used) are complete; see above
  JitVirtualRecord records[RECORDS];
  StackMethod* declaration;                 // the `virtual` declaration the site names (null for a func-ref site)
  long decl_cls_id;
  long decl_mthd_id;
  bool funcref;                             // keyed by the func-ref word rather than the receiver's class

  JitVirtualSite(StackMethod* d, long c, long m) : current(nullptr), filling(false), used(0), declaration(d), decl_cls_id(c), decl_mthd_id(m), funcref(d == nullptr) {
    for(int i = 0; i < RECORDS; ++i) {
      records[i] = JitVirtualRecord();
    }
    // publish the fields above to the threads that will miss here (see the comment on the struct)
    used.store(0, std::memory_order_release);
  }
};

/********************************
 * JIT compile code
 ********************************/
class NativeCode {
#if defined(_ARM64) || defined(_M_ARM64)
  uint32_t* code;
  int64_t* ints;
#else
  unsigned char* code;
#endif

  long size;
  FLOAT_VALUE* floats;
  std::vector<JitVirtualSite*> virtual_sites;   // the method's inline caches
  // The register-argument entry a compiled caller calls (see
  // JitAmd64::EmitNativePrologue and JitArm64::EmitNativePrologue); `code`
  // is the bridge entry the interpreter calls through JitRuntime::Execute.
  // Null when the method has the bridge entry only (a func-ref result).
  void* native_entry;
 public:
#if defined(_ARM64) || defined(_M_ARM64)
   NativeCode(uint32_t* c, long s, int64_t* i, FLOAT_VALUE* f, long native_entry_offset = -1) {
    code = c;
    size = s;
    ints = i;
    floats = f;
    // the offset counts 32-bit instructions, as `code` does
    native_entry = (native_entry_offset > 0) ? (void*)(c + native_entry_offset) : nullptr;
  }
#else
  NativeCode(unsigned char* c, long s, FLOAT_VALUE* f, long native_entry_offset = -1) {
    code = c;
    size = s;
    floats = f;
    native_entry = (native_entry_offset > 0) ? (void*)(c + native_entry_offset) : nullptr;
  }
#endif

  ~NativeCode() {
    for(JitVirtualSite* site : virtual_sites) {
      delete site;
    }
    virtual_sites.clear();
#if defined(_ARM64) || defined(_M_ARM64)
    // ARM64 allocates both constant pools with new[] (JitArm64::Compile), so they
    // must be released with delete[]. The old code used free() on ints and, on
    // Windows ARM64 (_ARM64 && _WIN64), VirtualFree() on a new[] buffer -- a hard
    // allocator mismatch (UB / heap corruption) that ran for every method at teardown.
    delete[] ints;
    ints = nullptr;
    delete[] floats;
    floats = nullptr;
#else
  #ifdef _WIN64
    VirtualFree(floats, 0, MEM_RELEASE);
  #else
    free(floats);
  #endif
    floats = nullptr;
#endif
  }

#if defined(_ARM64) || defined(_M_ARM64)
  inline uint32_t* GetCode() const {
    return code;
  }
  
  inline int64_t* GetInts() const {
    return ints;
  }
#else
  inline unsigned char* GetCode() const {
    return code;
  }
#endif
  
  inline long GetSize() {
    return size;
  }

  inline FLOAT_VALUE* GetFloats() const {
    return floats;
  }

  inline void* GetNativeEntry() const {
    return native_entry;
  }

  void SetVirtualSites(std::vector<JitVirtualSite*>& sites) {
    virtual_sites.swap(sites);
  }
};

/********************************
 * StackMethod class
 ********************************/
class StackMethod {
  long id;
  std::wstring name;
  bool is_virtual;
  bool has_and_or;
  bool is_lambda;
  // From the method's record, for 'obr --mcp' (Loader::SelectMcpEntry): whether the
  // method is a function, which can be started without an instance, and whether it is
  // the entry the compiler wrote for a class with tagged functions. Nothing that runs
  // a method reads either.
  bool is_function;
  bool is_mcp_entry;
  StackInstr* instrs;
  int instr_count;
  long param_count;
  long mem_size;
  // Auto-JIT state shared across mutator threads. native_code is the ready signal
  // (release on publish / acquire on read so a non-null pointer implies the fully
  // built NativeCode is visible). jit_call_count is a plain hot counter (relaxed).
  // jit_state elects exactly one compiler thread (0=none,1=compiling,2=done,3=failed)
  // so a method is never compiled twice or patched concurrently by two threads.
  std::atomic<NativeCode*> native_code;
  // The entry address of native_code, published with it. A compiled caller
  // loads this one word to call a compiled callee directly (the calling
  // convention's phase 3); null means "not compiled", and the caller takes
  // the bridge, which counts the call and compiles the callee in time.
  std::atomic<void*> native_entry;
  std::atomic<long> jit_call_count;
  std::atomic<int> jit_state;
  MemoryType rtrn_type;
  std::wstring rtrn_name;
  StackDclr** dclrs;
  long num_dclrs;
  StackClass* cls;

  const std::wstring ParseName(const std::wstring &name) const;

 public:
  StackMethod(long i, const std::wstring &n, bool v, bool h, bool l, StackDclr** d, long nd, long p, long m, MemoryType r, StackClass* k) {
    id = i;
    name = ParseName(n);
    is_virtual = v;
    has_and_or = h;
    is_lambda = l;
    is_function = false;
    is_mcp_entry = false;
    native_code.store(nullptr, std::memory_order_relaxed);
    native_entry.store(nullptr, std::memory_order_relaxed);
    jit_call_count.store(0, std::memory_order_relaxed);
    jit_state.store(JIT_NONE, std::memory_order_relaxed);
    dclrs = d;
    num_dclrs = nd;
    param_count = p;
    mem_size = m;
    rtrn_type = r;
    cls = k;
    instrs = nullptr;
    instr_count = 0;
  }

  ~StackMethod() {
    // clean up
    if(dclrs) {
      for(int i = 0; i < num_dclrs; ++i) {
        StackDclr* tmp = dclrs[i];
        delete tmp;
        tmp = nullptr;
      }
      delete[] dclrs;
      dclrs = nullptr;
    }

    // clean up (single-threaded teardown; relaxed is fine)
    NativeCode* nc = native_code.load(std::memory_order_relaxed);
    if(nc) {
      delete nc;
      native_code.store(nullptr, std::memory_order_relaxed);
    }

    // contiguous array - single deallocation
    delete[] instrs;
    instrs = nullptr;
  }

  inline const std::wstring& GetName() {
    return name;
  }

  inline bool IsVirtual() {
    return is_virtual;
  }

  inline bool HasAndOr() {
    return has_and_or;
  }

  inline bool IsFunction() const {
    return is_function;
  }

  inline bool IsMcpEntry() const {
    return is_mcp_entry;
  }

  // what the method's record says of it (Loader::LoadMethods)
  void SetEntryKind(bool function, bool mcp_entry) {
    is_function = function;
    is_mcp_entry = mcp_entry;
  }

  inline bool IsLambda() {
    return is_lambda;
  }

  inline StackClass* GetClass() const {
    return cls;
  }

#ifdef _DEBUGGER
  // TODO: might have 1 or more variables with the same name
  bool GetDeclaration(const std::wstring& name, StackDclr& found) {
    std::vector<int> results;
    if(name.size() > 0) {
      // search for name
      int index = 0;
      for(int i = 0; i < num_dclrs; i++, index++) {
        StackDclr* dclr = dclrs[i];
        const std::wstring &dclr_name = dclr->name.substr(dclr->name.find_last_of(L':') + 1);       
        if(dclr_name == name) {
          found.name = dclr->name;
          found.type = dclr->type;
          found.id = index;    
          return true;
        }

        // A Float occupies ONE instance/local slot in the 64-bit layout (see the
        // compiler's CalculateEntrySpace); only FUNC_PARM is two slots.
        if(dclr->type == FUNC_PARM) {
          index++;
        }
      }
    }

    return false;
  }
#endif

  inline StackDclr** GetDeclarations() const {
    return dclrs;
  }

  inline long GetNumberDeclarations() {
    return num_dclrs;
  }

  // Auto-JIT compile states (jit_state).
  enum { JIT_NONE = 0, JIT_COMPILING = 1, JIT_DONE = 2, JIT_FAILED = 3 };

  void SetNativeCode(NativeCode* c) {
    // release: publish the fully constructed NativeCode before the pointer is
    // observable, pairing with the acquire load in GetNativeCode. The entry
    // word -- the register-argument entry, null when the method has none --
    // goes out the same way; compiled callers read it with a plain aligned
    // load, which is an acquire on the targets this runs on.
    native_entry.store(c->GetNativeEntry(), std::memory_order_release);
    native_code.store(c, std::memory_order_release);
  }

  // the register-argument entry, or null (see NativeCode::native_entry)
  inline void* GetNativeEntry() const {
    return native_entry.load(std::memory_order_acquire);
  }

  // where a compiled caller reads the entry address from
  inline const void* NativeEntryAddress() const {
    static_assert(sizeof(std::atomic<void*>) == sizeof(void*), "the JIT loads native_entry as one word");
    return &native_entry;
  }

  inline NativeCode* GetNativeCode() const {
    return native_code.load(std::memory_order_acquire);
  }

  inline long GetJitCallCount() const {
    return jit_call_count.load(std::memory_order_relaxed);
  }

  // Returns the PREVIOUS value (fetch_add semantics) so a caller can detect the
  // unique thread that crossed the auto-JIT threshold.
  inline long IncrementJitCallCount() {
    return jit_call_count.fetch_add(1, std::memory_order_relaxed);
  }

  inline int GetJitState() const {
    return jit_state.load(std::memory_order_acquire);
  }

  // Elect a single compiler thread: exactly one CAS from JIT_NONE succeeds.
  inline bool ClaimJitCompile() {
    int expected = JIT_NONE;
    return jit_state.compare_exchange_strong(expected, JIT_COMPILING,
                                             std::memory_order_acq_rel);
  }

  inline void SetJitDone() {
    jit_state.store(JIT_DONE, std::memory_order_release);
  }

  inline void SetJitAttempted() {
    jit_state.store(JIT_FAILED, std::memory_order_release);
  }

  MemoryType GetReturn() const {
    return rtrn_type;
  }

  // the encoded return type ("o.System.String", "i*", "n"), for reflection's
  // Method->GetReturn(); rtrn_type keeps only its memory type (#1175)
  void SetReturnName(const std::wstring& n) {
    rtrn_name = n;
  }

  const std::wstring& GetReturnName() const {
    return rtrn_name;
  }

  void SetInstructions(StackInstr* ii, int ic) {
    instrs = ii;
    instr_count = ic;
  }

  long GetId() const {
    return id;
  }

  long GetParamCount() const {
    return param_count;
  }

  void SetParamCount(long c) {
    param_count = c;
  }

  inline size_t* NewMemory() {
    // +1 is for instance variable
    const long size = mem_size + 2;
    size_t* mem = new size_t[size];
    memset(mem, 0, size * sizeof(size_t));

    return mem;
  }

  inline long GetMemorySize() const {
    return mem_size;
  }

  inline long GetInstructionCount() const {
    return instr_count;
  }

  inline StackInstr* GetInstruction(long i) const {
    return &instrs[i];
  }

  inline StackInstr* GetInstructions() const {
    return instrs;
  }
};

/********************************
 * StackClass class
 ********************************/
class StackClass {
  std::unordered_map<std::wstring, StackMethod*> method_name_map;
  StackMethod** methods;
  int method_num;
  long id;
  std::wstring name;
  std::wstring file_name;
  StackClass* parent;
  long pid;
  bool is_virtual;
  long cls_space;
  long inst_space;
  StackDclr** cls_dclrs;
  long cls_num_dclrs;
  StackDclr** inst_dclrs;
  std::map<int, std::pair<int, StackDclr**> > closure_dclrs;
  long inst_num_dclrs;
  size_t* cls_mem;
  bool is_debug;
  
  // The override each virtual declaration reaches for a receiver of this class, filled
  // in as calls resolve (ResolveVirtualTarget). Every thread making a virtual call reads
  // it and the first call through each declaration adds to it. It was a std::unordered_map
  // read and written with no lock: an insert that rehashed the table while another thread
  // walked a bucket sent that thread through freed nodes, and two inserts at once
  // corrupted the table and the C heap. Eight threads making their first calls together
  // crashed about one run in ten under OBJECK_JIT_THRESHOLD=1 (SIGSEGV in GetVirtualMethod
  // or AddVirtualMethod, or glibc's "malloc(): unaligned tcache chunk detected").
  //
  // Readers take no lock, as a virtual call is too hot for one. The table is an
  // open-addressed array of slots reached through an atomic pointer:
  //   - a slot is empty while its method word is null. A writer fills in the slot's two
  //     ids and then stores the method word (release); a reader loads the method word
  //     (acquire) before it looks at the ids, so it sees a slot empty or complete, never
  //     half written;
  //   - writers, one at a time under the lock, add to the published table in place while
  //     it is at most half full. An entry is never changed or removed, so a probe that
  //     runs beside an insert finds what it would have found before it, or the new entry;
  //   - a table that would pass half full is replaced: a table of twice the size is
  //     filled completely, the new entry included, and only then published. A reader
  //     never sees a table being rehashed;
  //   - a replaced table may still be in a reader's hands, so it is kept until the class
  //     goes. Each is half the size of the next, so all of them together are smaller
  //     than the live one: memory is linear in the entries.
  // A reader that misses an entry added a moment ago resolves the call the slow way and
  // offers the same answer, which AddVirtualMethod finds already there.
  //
  // Until v2026.11.4's second review the published table was an immutable
  // std::unordered_map and every insert copied it, the replaced copies all kept: N
  // declarations resolved on one class held N(N+1)/2 entries (5,000: 12.5 million).
  struct VirtualMethodSlot {
    std::atomic<StackMethod*> method;   // null: an empty slot; stored last
    size_t cls_id;
    size_t mthd_id;
  };

  struct VirtualMethodTable {
    const size_t mask;                  // the slot count, a power of two, less one
    int shift;                          // a word's bits less the slot count's
    size_t used;                        // writers only
    VirtualMethodSlot* const slots;

    explicit VirtualMethodTable(size_t count) : mask(count - 1), shift((int)(sizeof(size_t) * 8)), used(0),
                                                slots(new VirtualMethodSlot[count]()) {
      for(size_t bit = count; bit > 1; bit >>= 1) {
        --shift;
      }
    }

    ~VirtualMethodTable() {
      delete[] slots;
    }

    // Szudzik's pairing of the two ids (as the map this replaces hashed them), spread
    // over the slots by the top bits of a golden-ratio product: the ids of one class's
    // declarations run in sequence, and this gives each of a short run its own slot
    inline size_t First(size_t x, size_t y) const {
      const size_t pair = x >= y ? x * x + x + y : y * y + x;
      const size_t ratio = sizeof(size_t) > 4 ? (size_t)0x9E3779B97F4A7C15ULL : (size_t)0x9E3779B1UL;
      return (pair * ratio) >> shift;
    }

    inline StackMethod* Find(size_t virtual_cls_id, size_t virtual_mthd_id) const {
      // ends at an empty slot: a table is never more than half full
      for(size_t i = First(virtual_cls_id, virtual_mthd_id);; i = (i + 1) & mask) {
        const VirtualMethodSlot& slot = slots[i];
        StackMethod* const found = slot.method.load(std::memory_order_acquire);
        if(!found) {
          return nullptr;
        }
        if(slot.cls_id == virtual_cls_id && slot.mthd_id == virtual_mthd_id) {
          return found;
        }
      }
    }

    // the caller holds the lock, and has checked that the key is absent and that there is room
    inline void Add(size_t virtual_cls_id, size_t virtual_mthd_id, StackMethod* mthd) {
      size_t i = First(virtual_cls_id, virtual_mthd_id);
      while(slots[i].method.load(std::memory_order_relaxed)) {
        i = (i + 1) & mask;
      }
      slots[i].cls_id = virtual_cls_id;
      slots[i].mthd_id = virtual_mthd_id;
      slots[i].method.store(mthd, std::memory_order_release);
      ++used;
    }
  };
  std::atomic<VirtualMethodTable*> virtual_methods{nullptr};
  std::mutex virtual_methods_lock;
  std::vector<VirtualMethodTable*> retired_virtual_methods;
  
  long InitializeClassMemory(long size) {
    if(size > 0) {
      cls_mem = (size_t*)calloc(size, sizeof(char));
    }
    else {
      cls_mem = nullptr;
    }
    return size;
  }

 public:
  StackClass(long i, const std::wstring &cn, const std::wstring &fn, long p, bool v, StackDclr** cdclrs, long ccount,
             StackDclr** idclrs, std::map<int, std::pair<int, StackDclr**> > fdclr, long icount, long cspace, long ispace, bool d) {
    id = i;
    name = cn;
    file_name = fn;
    parent = nullptr;
    // empty until SetMethods, so a class whose methods failed to load can be deleted
    methods = nullptr;
    method_num = 0;
    pid = p;
    is_virtual = v;
    cls_dclrs = cdclrs;
    cls_num_dclrs = ccount;
    inst_dclrs = idclrs;
    closure_dclrs = fdclr;
    inst_num_dclrs = icount;
    cls_space = InitializeClassMemory(cspace);
    inst_space = ispace;
    is_debug = d;
  }

  ~StackClass() {
    // clean up
    delete virtual_methods.load(std::memory_order_acquire);
    virtual_methods.store(nullptr, std::memory_order_release);
    for(size_t i = 0; i < retired_virtual_methods.size(); ++i) {
      delete retired_virtual_methods[i];
    }
    retired_virtual_methods.clear();

    if(cls_dclrs) {
      for(int i = 0; i < cls_num_dclrs; ++i) {
        StackDclr* tmp = cls_dclrs[i];
        delete tmp;
        tmp = nullptr;
      }
      delete[] cls_dclrs;
      cls_dclrs = nullptr;
    }

    if(inst_dclrs) {
      for(int i = 0; i < inst_num_dclrs; ++i) {
        StackDclr* tmp = inst_dclrs[i];
        delete tmp;
        tmp = nullptr;
      }
      delete[] inst_dclrs;
      inst_dclrs = nullptr;
    }

    std::map<int, std::pair<int, StackDclr**> >::iterator iter;
    for(iter = closure_dclrs.begin(); iter != closure_dclrs.end(); ++iter) {
      std::pair<int, StackDclr**> tmp = iter->second;
      const int num_dclrs = tmp.first;
      StackDclr** dclrs = tmp.second;
      for(int i = 0; i < num_dclrs; ++i) {
        StackDclr* tmp2 = dclrs[i];
        delete tmp2;
        tmp2 = nullptr;
      }
      delete[] dclrs;
      dclrs = nullptr;
    }
    closure_dclrs.clear();

    for(int i = 0; i < method_num; ++i) {
      StackMethod* method = methods[i];
      delete method;
      method = nullptr;
    }
    delete[] methods;
    methods = nullptr;

    if(cls_mem) {
      free(cls_mem);
      cls_mem = nullptr;
    }
  }

  inline long GetId() const {
    return id;
  }

  bool IsDebug() {
    return is_debug;
  }

  inline const std::wstring& GetName() {
    return name;
  }

  inline const std::wstring& GetFileName() {
    return file_name;
  }

  inline StackDclr** GetClassDeclarations() const {
    return cls_dclrs;
  }

  inline long GetNumberClassDeclarations() const {
    return cls_num_dclrs;
  }

  inline StackDclr** GetInstanceDeclarations() const {
    return inst_dclrs;
  }

  inline std::pair<int, StackDclr**> GetClosureDeclarations(const int id) const {
    // A LOOKUP, never an insert. The collector calls this from its marking threads
    // (CheckStatic / CheckStack / CheckPdaRoots / CheckJitRoots) for every FUNC_PARM
    // it walks, and an id with no closure declarations is ordinary. operator[] would
    // default-construct an entry for such an id, so two marking threads could insert
    // into this map at the same time and leave its tree inconsistent. Nothing failed
    // at the time: the corruption surfaced much later, when ~StackClass walked the
    // map and dereferenced a null link (#913, macOS arm64, every method compiled).
    // The absent case returns what the inserted default held, so callers are
    // unchanged: CheckMemory and FixupMemory loop over first == 0 declarations.
    std::map<int, std::pair<int, StackDclr**> >::const_iterator found = closure_dclrs.find(id);
    if(found != closure_dclrs.end()) {
      return found->second;
    }

    return std::pair<int, StackDclr**>(0, nullptr);
  }

  inline long GetNumberInstanceDeclarations() const {
    return inst_num_dclrs;
  }

  StackClass* GetParent();

  inline bool IsVirtual() {
    return is_virtual;
  }

  inline size_t* GetClassMemory() const {
    return cls_mem;
  }

  inline long GetInstanceMemorySize() const {
    return inst_space;
  }

  void SetMethods(StackMethod** mthds, const int num) {
    methods = mthds;
    method_num = num;
    // add method signature to map virtual calls
    for(int i = 0; i < num; ++i) {
      method_name_map.insert(make_pair(mthds[i]->GetName(), mthds[i]));
    }
  }

  inline StackMethod* GetMethod(long id) const {
#ifdef _DEBUG
    assert(id > -1 && id < method_num);
#endif
    // Release-mode bounds check: a method id from a crafted call instruction
    // could otherwise read past methods[] and return a wild pointer that the
    // interpreter would then invoke. Matches StackProgram::GetClass.
    if(id < 0 || id >= method_num) {
      return nullptr;
    }
    return methods[id];
  }

#ifdef _DEBUGGER
  std::vector<StackMethod*> GetMethods(const std::wstring &n) {
    std::vector<StackMethod*> found;
    for(int i = 0; i < method_num; ++i) {
      if(methods[i]->GetName().find(n) != std::wstring::npos) {
        found.push_back(methods[i]);
      }
    }

    return found;
  }

  // TODO: might have 1 or more variables with the same name
  bool GetDeclaration(const std::wstring& name, StackDclr& found, MemoryContext &context) {
    std::vector<int> results;
    if(name.size () > 0) {
      // search for instance name
      int index = 0;
      for(int i = 0; i < inst_num_dclrs; i++, index++) {
        StackDclr* dclr = inst_dclrs[i];
        const std::wstring& dclr_name = dclr->name.substr (dclr->name.find_last_of (L':') + 1);
        if(dclr_name == name) {
          found.name = dclr->name;
          found.type = dclr->type;
          found.id = index;
          context = INST;
          return true;
        }

        // A Float occupies ONE instance/local slot in the 64-bit layout (see the
        // compiler's CalculateEntrySpace); only FUNC_PARM is two slots.
        if(dclr->type == FUNC_PARM) {
          index++;
        }
      }

      // search for class name
      index = 0;
      for(int i = 0; i < cls_num_dclrs; i++, index++) {
        StackDclr* dclr = cls_dclrs[i];
        const std::wstring& dclr_name = dclr->name.substr (dclr->name.find_last_of (L':') + 1);
        if(dclr_name == name) {
          found.name = dclr->name;
          found.type = dclr->type;
          found.id = index;
          context = CLS;
          return true;
        }

        // A Float occupies ONE instance/local slot in the 64-bit layout (see the
        // compiler's CalculateEntrySpace); only FUNC_PARM is two slots.
        if(dclr->type == FUNC_PARM) {
          index++;
        }
      }
    }

    return false;
  }

#endif

  inline StackMethod* GetMethod(const std::wstring &n) {
    std::unordered_map<std::wstring, StackMethod*>::iterator result = method_name_map.find(n);
    if(result != method_name_map.end()) {
      return result->second;
    }

    return nullptr;
  }

  inline StackMethod** GetMethods() const {
    return methods;
  }

  inline int GetMethodCount() const {
    return method_num;
  }

#ifdef _DEBUGGER
  bool GetInstanceDeclaration(const std::wstring& name, StackDclr& found) {
    if(name.size() > 0) {
      // search for name
      int index = 0;
      for(int i = 0; i < inst_num_dclrs; i++, index++) {
        StackDclr* dclr = inst_dclrs[i];
        const std::wstring &dclr_name = dclr->name.substr(dclr->name.find_last_of(L':') + 1);       
        if(dclr_name == name) {
          found.name = dclr->name;
          found.type = dclr->type;
          found.id = index;
          return true;
        }
        // update
        // A Float occupies ONE instance/local slot in the 64-bit layout (see the
        // compiler's CalculateEntrySpace); only FUNC_PARM is two slots.
        if(dclr->type == FUNC_PARM) {
          index++;
        }
      }
    }

    return false;
  }

  bool GetClassDeclaration(const std::wstring& name, StackDclr& found) {
    if(name.size() > 0) {
      // search for name
      int index = 0;
      for(int i = 0; i < cls_num_dclrs; i++, index++) {
        StackDclr* dclr = cls_dclrs[i];
        const std::wstring &dclr_name = dclr->name.substr(dclr->name.find_last_of(L':') + 1);       
        if(dclr_name == name) {
          found.name = dclr->name;
          found.type = dclr->type;
          found.id = index;
          return true;
        }
        // update
        // A Float occupies ONE instance/local slot in the 64-bit layout (see the
        // compiler's CalculateEntrySpace); only FUNC_PARM is two slots.
        if(dclr->type == FUNC_PARM) {
          index++;
        }
      }
    }

    return false;
  }
#endif

  // lock-free: see virtual_methods
  StackMethod* GetVirtualMethod(size_t virtual_cls_id, size_t virtual_mthd_id) {
    const VirtualMethodTable* table = virtual_methods.load(std::memory_order_acquire);
    return table ? table->Find(virtual_cls_id, virtual_mthd_id) : nullptr;
  }

  // Safe to call from several threads at once, for the same key or different ones: the
  // first entry for a key stays (two threads that resolved it found the same method).
  void AddVirtualMethod(size_t virtual_cls_id, size_t virtual_mthd_id, StackMethod* mthd) {
    if(!mthd) {
      return;
    }

    std::lock_guard<std::mutex> guard(virtual_methods_lock);
    VirtualMethodTable* current = virtual_methods.load(std::memory_order_acquire);
    if(current && current->Find(virtual_cls_id, virtual_mthd_id)) {
      return;
    }

    // room in the published table: the entry appears to readers when its slot is complete
    if(current && (current->used + 1) * 2 <= current->mask + 1) {
      current->Add(virtual_cls_id, virtual_mthd_id, mthd);
      return;
    }

    // twice the size, built whole before any reader can reach it
    VirtualMethodTable* next = new VirtualMethodTable(current ? (current->mask + 1) * 2 : 8);
    if(current) {
      for(size_t i = 0; i <= current->mask; ++i) {
        const VirtualMethodSlot& slot = current->slots[i];
        StackMethod* const kept = slot.method.load(std::memory_order_relaxed);
        if(kept) {
          next->Add(slot.cls_id, slot.mthd_id, kept);
        }
      }
      // a reader may still hold it: freed with the class
      retired_virtual_methods.push_back(current);
    }
    next->Add(virtual_cls_id, virtual_mthd_id, mthd);
    virtual_methods.store(next, std::memory_order_release);
  }
};

/********************************
 * StackProgram class
 ********************************/
class StackProgram {
  std::map<std::wstring, StackClass*> cls_map;
  StackClass** classes;
  long class_num;
  long* cls_hierarchy;
  long** cls_interfaces;
  long string_cls_id;
  long cls_cls_id;
  long mthd_cls_id;
  long sock_cls_id;
  long secure_sock_cls_id;
  long dtls_sock_cls_id;
  long data_type_cls_id;
  long command_output_cls_id;
  StackMethod* init_method;
  static std::map<std::wstring, std::wstring> properties_map;
	static std::unordered_map<long, StackMethod*> signal_handler_func;
  
  FLOAT_VALUE** float_strings;
  int num_float_strings;

  bool** bool_strings;
  int num_bool_strings;

  char** byte_strings;
  int num_byte_strings;

  INT64_VALUE** int_strings;
  int num_int_strings;

  wchar_t** char_strings;
  int num_char_strings;

#ifdef _WIN32
  static CRITICAL_SECTION program_cs;
  static CRITICAL_SECTION prop_cs;
#else
  static pthread_mutex_t program_mutex;
  static pthread_mutex_t prop_mutex;
#endif

 public:
#ifdef _MODULE
   std::wstringstream output_buffer;
#endif

  StackProgram() {
    // Everything ~StackProgram frees starts empty: a load that stops part way
    // (a refused or unparsable .obe) leaves the rest unset, and the destructor
    // used to free whatever those words held (found by the loader fuzz target).
    cls_hierarchy = nullptr;
    cls_interfaces = nullptr;
    classes = nullptr;
    class_num = 0;
    init_method = nullptr;
    float_strings = nullptr;
    num_float_strings = 0;
    bool_strings = nullptr;
    num_bool_strings = 0;
    byte_strings = nullptr;
    num_byte_strings = 0;
    int_strings = nullptr;
    num_int_strings = 0;
    char_strings = nullptr;
    num_char_strings = 0;
    string_cls_id = cls_cls_id = mthd_cls_id = sock_cls_id = secure_sock_cls_id = dtls_sock_cls_id = data_type_cls_id = command_output_cls_id = -1;
#ifdef _WIN32
    InitializeCriticalSection(&program_cs);
    InitializeCriticalSection(&prop_cs);
#endif
  }

  ~StackProgram() {
    if(classes) {
      for(int i = 0; i < class_num; ++i) {
        StackClass* klass = classes[i];
        delete klass;
        klass = nullptr;
      }
      delete[] classes;
      classes = nullptr;
    }

    if(cls_hierarchy) {
      delete[] cls_hierarchy;
      cls_hierarchy = nullptr;
    }

    if(cls_interfaces) {
      for(int i = 0; i < class_num; ++i) {
        delete[] cls_interfaces[i];
        cls_interfaces[i] = nullptr;
      }
      delete[] cls_interfaces;
      cls_interfaces = nullptr;
    }

    if(float_strings) {
      for(int i = 0; i < num_float_strings; ++i) {
        FLOAT_VALUE* tmp = float_strings[i];
        delete[] tmp;
        tmp = nullptr;
      }
      delete[] float_strings;
      float_strings = nullptr;
    }

    // the Bool and Byte literal tables were never freed
    if(bool_strings) {
      for(int i = 0; i < num_bool_strings; ++i) {
        delete[] bool_strings[i];
      }
      delete[] bool_strings;
      bool_strings = nullptr;
    }

    if(byte_strings) {
      for(int i = 0; i < num_byte_strings; ++i) {
        delete[] byte_strings[i];
      }
      delete[] byte_strings;
      byte_strings = nullptr;
    }

    if(int_strings) {
      for(int i = 0; i < num_int_strings; ++i) {
        INT64_VALUE* tmp = int_strings[i];
        delete[] tmp;
        tmp = nullptr;
      }
      delete[] int_strings;
      int_strings = nullptr;
    }

    if(char_strings) {
      for(int i = 0; i < num_char_strings; ++i) {
        wchar_t* tmp = char_strings[i];
        delete [] tmp;
        tmp = nullptr;
      }
      delete[] char_strings;
      char_strings = nullptr;
    }

    if(init_method) {
      delete init_method;
      init_method = nullptr;
    }

#ifdef _WIN32
    DeleteCriticalSection(&program_cs);
    DeleteCriticalSection(&prop_cs);
#endif
  }

#ifdef _WIN32
  static std::wstring GetProperty(const std::wstring& key) {
    std::wstring value;
    EnterCriticalSection(&prop_cs);

    if(properties_map.size() == 0) {
      InitializeProprieties();
    }
    
    std::map<std::wstring, std::wstring>::iterator find = properties_map.find(key);
    if(find != properties_map.end()) {
      value = find->second;
    }

    LeaveCriticalSection(&prop_cs);
    return value;
  }

  static void SetProperty(const std::wstring& key, const std::wstring& value) {
    EnterCriticalSection(&prop_cs);

    if(properties_map.size() == 0) {
      InitializeProprieties();
    }

    // assigned, not inserted: std::map::insert keeps an existing key's value,
    // which left every property settable only once per process
    properties_map[key] = value;
    LeaveCriticalSection(&prop_cs);
  }

  static BOOL GetUserDirectory(wchar_t* buf, DWORD len) {
    HANDLE handle;

    if(!OpenProcessToken(GetCurrentProcess(), TOKEN_READ, &handle)) {
      return FALSE;
    }

    if(!GetUserProfileDirectoryW(handle, buf, &len)) {
      return FALSE;
    }

    CloseHandle(handle);
    return TRUE;
  }
#else
  static std::wstring GetProperty(const std::wstring& key) {
    std::wstring value;
    
    pthread_mutex_lock(&prop_mutex);
    
    if(properties_map.size() == 0) {
      InitializeProprieties();
    }
    
    std::map<std::wstring, std::wstring>::iterator find = properties_map.find(key);
    if(find != properties_map.end()) {
      value = find->second;
    }
    pthread_mutex_unlock(&prop_mutex);

    return value;
  }

  static void SetProperty(const std::wstring& key, const std::wstring& value) {
    pthread_mutex_lock(&prop_mutex);

    // filled before the first store: once a key is in, the map is never empty
    // again, and the directories and config.prop would never be loaded
    if(properties_map.size() == 0) {
      InitializeProprieties();
    }

    // assigned, not inserted: std::map::insert keeps an existing key's value,
    // which left every property settable only once per process
    properties_map[key] = value;
    pthread_mutex_unlock(&prop_mutex);
  }
#endif

  static bool AddSignalHandler(long key, StackMethod* mthd);
  // the handler registered for a VM signal id, or nullptr; safe beside AddSignalHandler
  static StackMethod* FindSignalHandler(long key);
  // Shutdown: StopSignalDispatch() keeps the dispatcher thread from starting another handler;
  // WaitSignalDispatchIdle() waits (false on timeout) for one already running to return. Called by
  // the VM, in that order around halting the other threads, before the program is freed.
  static void StopSignalDispatch();
  static bool WaitSignalDispatchIdle(int timeout_ms);

  static void InitializeProprieties();
  
  void SetInitializationMethod(StackMethod* i) {
    init_method = i;
  }

  StackMethod* GetInitializationMethod() const {
    return init_method;
  }

  long GetStringObjectId() const {
    return string_cls_id;
  }

  void SetStringObjectId(int id) {
    string_cls_id = id;
  }

   long GetClassObjectId() {
    if(cls_cls_id < 0) {
      StackClass* cls = GetClass(L"System.Introspection.Class");
      if(!cls) {
        std::wcerr << L">>> Internal error: unable to find class: System.Introspection.Class <<<" << std::endl;
        VmExit(1);
      }
      cls_cls_id = cls->GetId();
    }

    return cls_cls_id;
  }

   long GetMethodObjectId() {
    if(mthd_cls_id < 0) {
      StackClass* cls = GetClass(L"System.Introspection.Method");
      if(!cls) {
        std::wcerr << L">>> Internal error: unable to find class: System.Introspection.Method <<<" << std::endl;
        VmExit(1);
      }
      mthd_cls_id = cls->GetId();
    }

    return mthd_cls_id;
  }

   long GetSocketObjectId() {
    if(sock_cls_id < 0) {
      StackClass* cls = GetClass(L"System.IO.Net.TCPSocket");
      if(!cls) {
        std::wcerr << L">>> Internal error: unable to find class: System.IO.Net.TCPSocket <<<" << std::endl;
        VmExit(1);
      }
      sock_cls_id = cls->GetId();
    }

    return sock_cls_id;
  }

  // Its own cache: this shared sock_cls_id with GetSocketObjectId, so whichever
  // kind of server accepted first fixed the class of every socket accepted
  // after it. A plain TCPSocketServer in a process that had already accepted a
  // TLS connection handed back TCPSecureSocket objects holding raw sockets, and
  // the other way round.
  long GetSecureSocketObjectId() {
    if(secure_sock_cls_id < 0) {
      StackClass* cls = GetClass(L"System.IO.Net.TCPSecureSocket");
      if(!cls) {
        std::wcerr << L">>> Internal error: unable to find class: System.IO.Net.TCPSecureSocket <<<" << std::endl;
        VmExit(1);
      }
      secure_sock_cls_id = cls->GetId();
    }

    return secure_sock_cls_id;
  }

  long GetDtlsSocketObjectId() {
    if(dtls_sock_cls_id < 0) {
      StackClass* cls = GetClass(L"System.IO.Net.DTLSSocket");
      if(!cls) {
        std::wcerr << L">>> Internal error: unable to find class: System.IO.Net.DTLSSocket <<<" << std::endl;
        VmExit(1);
      }
      dtls_sock_cls_id = cls->GetId();
    }

    return dtls_sock_cls_id;
  }

   long GetDataTypeObjectId() {
     if(data_type_cls_id < 0) {
       StackClass* cls = GetClass(L"System.Introspection.DataType");
       if(!cls) {
         std::wcerr << L">>> Internal error: unable to find class: System.Introspection.DataType <<<" << std::endl;
         VmExit(1);
       }
       data_type_cls_id = cls->GetId();
     }

     return data_type_cls_id;
   }

   long GetCommandOutputObjectId() {
     if(command_output_cls_id < 0) {
       StackClass* cls = GetClass(L"System.CommandOutput");
       if(!cls) {
         std::wcerr << L">>> Internal error: unable to find class: System.Introspection.DataType <<<" << std::endl;
         VmExit(1);
       }
       command_output_cls_id = cls->GetId();
     }

     return command_output_cls_id;
   }

  void SetFloatStrings(FLOAT_VALUE** s, int n) {
    float_strings = s;
    num_float_strings = n;
  }

  void SetBoolStrings(bool** s, int n) {
    bool_strings = s;
    num_bool_strings = n;
  }

  void SetByteStrings(char** s, int n) {
    byte_strings = s;
    num_byte_strings = n;
  }
  
  void SetIntStrings(INT64_VALUE** s, int n) {
    int_strings = s;
    num_int_strings = n;
  }

  void SetCharStrings(wchar_t** s, int n) {
    char_strings = s;
    num_char_strings = n;
  }  

  FLOAT_VALUE** GetFloatStrings() const {
    return float_strings;
  }

  INT64_VALUE** GetIntStrings() const {
    return int_strings;
  }

  bool** GetBoolStrings() const {
    return bool_strings;
  }

  char** GetByteStrings() const {
    return byte_strings;
  }

  wchar_t** GetCharStrings() const {
    return char_strings;
  }

  void SetClasses(StackClass** clss, const int num) {
    classes = clss;
    class_num = num;

    for(int i = 0; i < num; ++i) {
      const std::wstring &name = clss[i]->GetName();
      if(name.size() > 0) {  
        cls_map.insert(std::pair<std::wstring, StackClass*>(name, clss[i]));
      }
    }
  }

  StackClass* GetClass(const std::wstring &n) {
    if(classes) {
      std::map<std::wstring, StackClass*>::iterator find = cls_map.find(n);
      if(find != cls_map.end()) {
        return find->second;
      }
    }

    return nullptr;
  }

  void SetHierarchy(long* h) {
    cls_hierarchy = h;
  }

  inline long* GetHierarchy() const {
    return cls_hierarchy;
  }

  void SetInterfaces(long** i) {
    cls_interfaces = i;
  }

  inline long** GetInterfaces() const {
    return cls_interfaces;
  }  

  inline StackClass* GetClass(long id) const {
    if(id > -1 && id < class_num) {
      return classes[id];
    }

    return nullptr;
  }

  inline StackClass** GetClasses() const {
    return classes;
  }

  inline long GetClassNumber() const {
    return class_num;
  }


  public:
		//
		// TODO
		//
		static void SignalHandler(int signal);
};

/********************************
 * Call stack frame
 ********************************/
struct StackFrame {
  StackMethod* method;
  size_t* mem;
  long ip;
  bool jit_called;
  size_t* jit_mem;
  union {
    // a compiled frame: its locals' offset from the frame pointer
    long jit_offset;
    // An interpreted frame (jit_mem null): where the collector finds it, for its
    // operand-stack reference map (ref_map.h). The instruction in progress plus
    // one, stored by every instruction that can collect (the dispatch loop's
    // default path); or -(next + 2) while the thread is parked at the loop's
    // safepoint poll, between instructions; 0 before either. ip is the return
    // address only, and error traces read it.
    long cur_ip;
  };
  size_t* jit_inst_mem;  // pointer to JIT frame's INSTANCE_MEM slot for GC fixup
};

/********************************
 * Method signature formatter
 ********************************/
class MethodFormatter {
  static std::wstring FormatParameters(const std::wstring &param_str);
  static std::wstring FormatType(const std::wstring &type_str, size_t &index);
  static std::wstring FormatFunctionalType(const std::wstring &func_str, size_t &index);
  
 public:
  static std::wstring Format(const std::wstring method_sig);
};

/********************************
 * ObjectSerializer class
 ********************************/
class ObjectSerializer 
{
  std::vector<char> values;
  std::map<size_t*, long> serial_ids;
  long next_id;
  long cur_id;
  
  void CheckObject(size_t* mem, bool is_obj, long depth);
  void CheckMemory(size_t* mem, StackDclr** dclrs, const long dcls_size, long depth);
  void Serialize(size_t* inst);

  bool WasSerialized(size_t* mem) {
    std::map<size_t*, long>::iterator find = serial_ids.find(mem);
    if(find != serial_ids.end()) {
      cur_id = find->second;
      SerializeInt((int64_t)cur_id);

      return true;
    }
    next_id++;
    cur_id = next_id * -1;
    serial_ids.insert(std::pair<size_t*, long>(mem, next_id));
    SerializeInt((int64_t)cur_id);

    return false;
  }

  inline void SerializeByte(const char v) {
    char* bp = (char*)&v;
    for(size_t i = 0; i < sizeof(v); ++i) {
      values.push_back(*(bp + i));
    }
  }

  inline void SerializeChar(const wchar_t v) {
    std::string out;
    CharacterToBytes(v, out);
    SerializeInt((INT_VALUE)out.size());
    for(size_t i = 0; i < out.size(); ++i) {
      values.push_back(out[i]); 
    }
  }

  inline void SerializeInt(const INT64_VALUE v) {
    char* bp = (char*)&v;
    for(size_t i = 0; i < sizeof(v); ++i) {
      values.push_back(*(bp + i));
    }
  }

  inline void SerializeFloat(const FLOAT_VALUE v) {
    char* bp = (char*)&v;
    for(size_t i = 0; i < sizeof(v); ++i) {
      values.push_back(*(bp + i));
    }
  }

  inline void SerializeBytes(const void* array, const long len) {
    char* bp = (char*)array;
    for(long i = 0; i < len; ++i) {
      values.push_back(*(bp + i));
    }
  }

  // An array's element count, its rank and every one of its dimensions; the
  // elements, which start after the last dimension, follow. Only the first
  // dimension was written, so a multi-dimensional array lost its tail (#1174).
  // A one-dimensional array's header is unchanged.
  inline void SerializeArrayHeader(const size_t* array) {
    SerializeInt((INT_VALUE)array[0]);
    SerializeInt((INT_VALUE)array[1]);
    for(size_t j = 0; j < array[1]; ++j) {
      SerializeInt((INT_VALUE)array[2 + j]);
    }
  }

 public:
  ObjectSerializer(size_t* i) {
    Serialize(i);
  }

  ~ObjectSerializer() {
  }

  std::vector<char>& GetValues() {
    return values;
  }
};

/********************************
 *  ObjectDeserializer class
 ********************************/
class ObjectDeserializer 
{
  const char* buffer;
  long buffer_offset;
  long buffer_array_size;
  size_t* op_stack;
  size_t* stack_pos;
  StackClass* cls;
  size_t* instance;
  long instance_pos;
  std::map<INT_VALUE, size_t*> mem_cache;
  bool read_error = false;

  // Bounds guard: returns true only if 'n' more bytes can be read at the current
  // offset without running past the input buffer. The buffer comes from an
  // attacker-controllable Byte[] (network/file), so every read must be checked.
  bool CanRead(INT64_VALUE n) {
    // Compare in 64-bit so a large attacker-supplied size cannot truncate or
    // overflow on platforms where 'long' is 32-bit (Windows).
    if(n < 0 || buffer_offset < 0 || (INT64_VALUE)buffer_offset > (INT64_VALUE)buffer_array_size ||
       n > (INT64_VALUE)buffer_array_size - (INT64_VALUE)buffer_offset) {
      read_error = true;
      return false;
    }
    return true;
  }

  char DeserializeByte() {
    if(!CanRead((long)sizeof(char))) { return 0; }
    char value;
    memcpy(&value, buffer + buffer_offset, sizeof(value));
    buffer_offset += sizeof(value);

    return value;
  }

  wchar_t DeserializeChar() {
    // read
    const INT_VALUE num = DeserializeInt32();
    if(num < 0 || !CanRead(num)) { read_error = true; return L'\0'; }
    char* in = new char[num + 1];
    memcpy(in, buffer + buffer_offset, num);
    in[num] = '\0';

    // convert
    wchar_t out = L'\0';
    BytesToCharacter(in, out);
    delete[] in;
    in = nullptr;

    buffer_offset += num;
    return out;
  }

  INT64_VALUE DeserializeInt() {
    if(!CanRead((INT64_VALUE)sizeof(INT64_VALUE))) { return 0; }
    INT64_VALUE value;
    memcpy(&value, buffer + buffer_offset, sizeof(value));
    buffer_offset += sizeof(value);

    return value;
  }

  // Sizes, dimensions and cache ids in well-formed streams always fit in 32
  // bits; anything wider is corrupt/hostile input, so flag a read error
  // instead of silently truncating.
  INT_VALUE DeserializeInt32() {
    const INT64_VALUE value = DeserializeInt();
    if(value < INT32_MIN || value > INT32_MAX) {
      read_error = true;
      return 0;
    }
    return (INT_VALUE)value;
  }

  // Reads the dimensions after an array's first (the caller read 'first')
  // into dims and checks them against its element count (#1174)
  bool DeserializeDimensions(const INT_VALUE size, const INT_VALUE dim, const INT_VALUE first, size_t* dims) {
    if(read_error || size < 0 || dim < 1 || dim > 8 || first < 0) {
      read_error = true;
      return false;
    }
    dims[0] = first;
    INT64_VALUE count = first;
    for(INT_VALUE j = 1; j < dim; ++j) {
      const INT_VALUE value = DeserializeInt32();
      if(read_error || value < 0) {
        read_error = true;
        return false;
      }
      dims[j] = value;
      count *= value;
      if(count > INT32_MAX) {
        read_error = true;
        return false;
      }
    }
    // a one-dimensional header is checked as before: its size stands alone
    if(dim > 1 && count != size) {
      read_error = true;
      return false;
    }
    return true;
  }

  FLOAT_VALUE DeserializeFloat() {
    if(!CanRead((long)sizeof(FLOAT_VALUE))) { return 0; }
    FLOAT_VALUE value;
    memcpy(&value, buffer + buffer_offset, sizeof(value));
    buffer_offset += sizeof(value);

    return value;
  }

 public:
  ObjectDeserializer(const char* b, long s, size_t* stack, size_t* pos) {
    op_stack = stack;
    stack_pos = pos;
    buffer = b;
    buffer_array_size = s;
    buffer_offset = 0;
    cls = nullptr;
    instance = nullptr;
    instance_pos = 0;
  }

  ObjectDeserializer(const char* b, long o, std::map<INT_VALUE, size_t*> &c,
         long s, size_t* stack, size_t* pos) {
    op_stack = stack;
    stack_pos = pos;
    buffer = b;
    buffer_array_size = s;
    buffer_offset = o;
    mem_cache = c;
    cls = nullptr;
    instance = nullptr;
    instance_pos = 0;
  }

  ~ObjectDeserializer() {    
  }

  inline long GetOffset() const {
    return buffer_offset;
  }

  std::map<INT_VALUE, size_t*> GetMemoryCache() {
    return mem_cache;
  }

  size_t* DeserializeObject();
};

/********************************
 * The array terminator invariant
 *
 * A Char[] or Byte[] is [0] element count, [1] dimensions, [2..] one extent per
 * dimension, then the elements: at array + 3 for the one-dimensional arrays the
 * native methods take. Every allocation asks for ONE MORE element than [0]
 * counts (NEW_BYTE_ARY, NEW_CHAR_ARY, the deserializers, CreateStringObject, the
 * APITools_Make*Array calls native libraries use), and MemoryManager hands out
 * zeroed memory. A program cannot index that spare element: [0] bounds every
 * load, store, copy and fill. So the element after the last one is always zero.
 *
 * What rests on it:
 *  - A native that takes TEXT in a Char[] (a String's backing array, a path, a
 *    host name, a command) reads it as a C string: (wchar_t*)(array + 3). The
 *    text ends at its first zero, which for an array filled to its last element
 *    (an "exact-length" array, as String->ToCharArray() returns) is the spare.
 *    System.String keeps a zero after its characters whenever it shrinks.
 *  - A native that stores text into a Char[] may write one zero after the
 *    characters, at most at index [0]: StoreCharArrayText below. One that was
 *    given a slice (offset, num) writes it only inside the slice or at index
 *    [0], never on the caller's element after a full slice: StoreCharArraySlice.
 *
 * What must NOT rest on it: anything given a length. A native that takes
 * (offset, num) or works on a Byte[] uses those numbers and [0], never a search
 * for the zero: a Byte[] holds data, where a zero is a value like any other.
 *
 * Checked, not assumed: with OBJECK_GC_VERIFY set the heap verifier fails a
 * Char[] whose block has no room for the spare element or whose spare element
 * is not zero (invariant B5, arch/memory_verify.cpp). A Byte[] shares its heap
 * type with closure capture blocks, so the verifier cannot tell one from the
 * other; no native reads a Byte[] to its zero any more, which is what the
 * check would have protected.
 ********************************/

/********************************
 * Stores 'count' characters in a Char[] at 'dest' and ends them with a zero. The
 * caller bounds 'count' by the elements left in the array from 'dest'; the zero
 * then lands inside the array or on its spare element (see above).
 *
 * Used where wcsncpy_s / wcsncpy were: the first ends the process (MSVC's
 * invalid-parameter handler) when the text is exactly as long as the size it is
 * given and, in a debug CRT, fills the rest of the destination with 0xFE; the
 * second zero-fills up to its count and writes no terminator at all when the
 * text is that long. Windows and POSIX now store the same elements.
 ********************************/
inline void StoreCharArrayText(wchar_t* dest, const wchar_t* src, size_t count)
{
  if(count > 0) {
    wmemcpy(dest, src, count);
  }
  dest[count] = L'\0';
}

/********************************
 * The same for decoded text going into a one-dimensional Char[] from element
 * 'offset' on (the caller has checked offset <= [0]): what does not fit in the
 * elements that are left is dropped.
 ********************************/
inline void StoreCharArrayText(size_t* array, size_t offset, const std::wstring& text)
{
  const size_t room = array[0] - offset;
  StoreCharArrayText((wchar_t*)(array + 3) + offset, text.c_str(), text.size() < room ? text.size() : room);
}

/********************************
 * The same for a read of (offset, num) into a one-dimensional Char[], where the
 * caller named a slice and the elements after it are the caller's own (the
 * caller has checked offset + num <= [0]). At most 'num' characters are stored.
 * The zero after them is written only where it cannot land on an element the
 * caller did not hand over: inside the slice, when fewer than 'num' characters
 * were stored, or on the array's spare element, when the characters end at the
 * array's last element. A slice filled to its last element that ends before the
 * array does gets no zero: the plain StoreCharArrayText put one on the element
 * just past the slice (a read of 3 at offset 0 of an 8-element array wrote [3]).
 * The same elements on Windows and POSIX. Returns the characters stored.
 ********************************/
inline size_t StoreCharArraySlice(size_t* array, size_t offset, size_t num, const std::wstring& text)
{
  wchar_t* dest = (wchar_t*)(array + 3) + offset;
  const size_t count = text.size() < num ? text.size() : num;
  if(count > 0) {
    wmemcpy(dest, text.c_str(), count);
  }
  if(count < num || offset + count == array[0]) {
    dest[count] = L'\0';
  }
  return count;
}

/********************************
 * The text in elements [offset, offset + num) of a one-dimensional Char[] (the
 * caller has checked the range against [0]): up to the first zero in that range
 * and never past its end.
 ********************************/
inline std::wstring CharArrayRangeText(const size_t* array, size_t offset, size_t num)
{
  const wchar_t* start = (const wchar_t*)(array + 3) + offset;
  const wchar_t* zero = num > 0 ? wmemchr(start, L'\0', num) : nullptr;
  return std::wstring(start, zero ? (size_t)(zero - start) : num);
}

/********************************
 *  TrapManager class
 ********************************/
enum TimeInterval {
  DAY_TIME,
  HOUR_TIME,
  MIN_TIME,
  SEC_TIME
};

class TrapProcessor {
   // Returns: 1 = ready, 0 = not ready, -1 = error
#ifdef _WIN32
   static int socket_ready_for_io(SOCKET sock, bool is_write) {
#else
   static int socket_ready_for_io(int sock, bool is_write) {
#endif
      fd_set fds;
      FD_ZERO(&fds);
      FD_SET(sock, &fds);

      struct timeval tv;
      tv.tv_sec = 0;
      tv.tv_usec = 0;  // zero timeout = poll

      int ret = -1;
#ifdef _WIN32
      // nfds is ignored on Windows, pass 0
      if(is_write) {
         ret = select(0, nullptr, &fds, nullptr, &tv);
      }
      else {
         ret = select(0, &fds, nullptr, nullptr, &tv);
      }

      if(ret == SOCKET_ERROR) {
         return -1;
      }
#else
      // POSIX: nfds = highest fd + 1
      if(is_write) {
         ret = select(sock + 1, nullptr, &fds, nullptr, &tv);
      }
      else {
         ret = select(sock + 1, &fds, nullptr, nullptr, &tv);
      }

      if(ret < 0) {
         return -1;
      }
#endif

      if(ret > 0 && FD_ISSET(sock, &fds)) {
         return 1; // socket ready
      }

      return 0; // not ready
   }


   // Returns: 1 = ready, 0 = not ready, -1 = error
   static int ssl_ready_for_io(SecureSocketCtx* ctx, bool is_write) {
      if(!ctx) {
         return -1;
      }

      // Whether a read would find something is asked while holding the session's read lock, taken
      // only if it is free. The answer looks at what a read changes: the bytes mbedTLS has decrypted
      // and not handed out, and the byte a Char[] read may have given back (pushback). Both were read
      // here with no lock while another thread's read wrote them.
      //
      // If a read is in progress on another thread the answer is 0, "not ready". That read owns
      // whatever has arrived: a byte it has taken while completing a character is in neither the
      // socket nor the session until it is given back, and a caller told "ready" would only queue
      // behind that read for bytes that may all be spoken for. Select is a poll and never waits; the
      // caller asks again, and once the read has returned the answer counts the byte it gave back.
      // (A try-lock may also fail with the lock free, rarely: the same answer, and the same remedy.)
      //
      // The lock is kept to the end of the poll, so the two halves of the answer are of one moment.
      // A check for writing takes no part in any of this.
      std::unique_lock<std::mutex> reading(ctx->read_lock, std::defer_lock);
      if(!is_write) {
         if(!reading.try_lock()) {
            return 0;
         }
         size_t pending = mbedtls_ssl_get_bytes_avail(&ctx->ssl);
         if(pending > 0 || ctx->pushback >= 0) {   // or a read gave a byte back
            return 1;
         }
      }

      // Get the underlying socket file descriptor.
      int fd = ctx->net.fd;
      if(fd < 0) {
         return -1;
      }

      struct timeval tv;
      tv.tv_sec = 0;
      tv.tv_usec = 0;

#ifdef _WIN32
      SOCKET s = (SOCKET)fd;

      fd_set rfds;
      FD_ZERO(&rfds);

      fd_set wfds;
      FD_ZERO(&wfds);

      if(is_write) {
         FD_SET(s, &wfds);
      }
      else {
         FD_SET(s, &rfds);
      }

      const int ret = select(0, is_write ? nullptr : &rfds, is_write ? &wfds : nullptr, nullptr, &tv);
      if(ret == SOCKET_ERROR) {
         return -1;
      }

      if(ret > 0) {
         if(!is_write && FD_ISSET(s, &rfds)) {
            return 1;
         }

         if(is_write && FD_ISSET(s, &wfds)) {
            return 1;
         }
      }

      return 0;

#else
      fd_set fds;
      FD_ZERO(&fds);
      FD_SET(fd, &fds);

      int ret = select(fd + 1, is_write ? nullptr : &fds, is_write ? &fds : nullptr, nullptr, &tv);

      if(ret < 0) {
         return -1;
      }

      if(ret > 0 && FD_ISSET(fd, &fds)) {
         return 1;
      }

      return 0;
#endif
   }

   static int dtls_ready_for_io(DtlsSocketCtx* ctx, bool is_write) {
      if(!ctx) {
         return -1;
      }

      // For reads, check if mbedTLS has buffered decrypted data, or a line read gave a byte back.
      // Under the session's read lock, taken only if it is free, and "not ready" while a read is in
      // progress on another thread, as ssl_ready_for_io: see there.
      std::unique_lock<std::mutex> reading(ctx->read_lock, std::defer_lock);
      if(!is_write) {
         if(!reading.try_lock()) {
            return 0;
         }
         size_t pending = mbedtls_ssl_get_bytes_avail(&ctx->ssl);
         if(pending > 0 || ctx->pushback >= 0) {
            return 1;
         }
      }

#ifdef _WIN32
      // a connection a server accepted: its datagrams are queued for it, and a send never waits
      if(ctx->peer) {
         return is_write ? 1 : DtlsPeerReadable(ctx->server, ctx->peer);
      }
#endif

      // Get the underlying socket file descriptor.
      int fd = ctx->net.fd;
      if(fd < 0) {
         return -1;
      }

      struct timeval tv;
      tv.tv_sec = 0;
      tv.tv_usec = 0;

#ifdef _WIN32
      SOCKET s = (SOCKET)fd;

      fd_set rfds;
      FD_ZERO(&rfds);

      fd_set wfds;
      FD_ZERO(&wfds);

      if(is_write) {
         FD_SET(s, &wfds);
      }
      else {
         FD_SET(s, &rfds);
      }

      const int ret = select(0, is_write ? nullptr : &rfds, is_write ? &wfds : nullptr, nullptr, &tv);
      if(ret == SOCKET_ERROR) {
         return -1;
      }

      if(ret > 0) {
         if(!is_write && FD_ISSET(s, &rfds)) {
            return 1;
         }

         if(is_write && FD_ISSET(s, &wfds)) {
            return 1;
         }
      }

      return 0;

#else
      fd_set fds;
      FD_ZERO(&fds);
      FD_SET(fd, &fds);

      int ret = select(fd + 1, is_write ? nullptr : &fds, is_write ? &fds : nullptr, nullptr, &tv);

      if(ret < 0) {
         return -1;
      }

      if(ret > 0 && FD_ISSET(fd, &fds)) {
         return 1;
      }

      return 0;
#endif
   }

  // Breaks a time down into the caller's own struct tm. The caller owns the
  // buffer: localtime() and gmtime() share one static buffer per process, so
  // two threads breaking down times at once read each other's fields (a file
  // stamp or a date arithmetic result torn between threads), and the Windows
  // form used to hand back a pointer to a local that had gone out of scope.
  static inline bool GetTime(struct tm& curr_time, time_t raw_time, bool is_gmt) {
#ifdef _WIN32
    if(is_gmt) {
      if(gmtime_s(&curr_time, &raw_time)) {
        std::wcerr << L">>> Unable to get GMT time <<<" << std::endl;
        return false;
      }
    }
    else {
      if(localtime_s(&curr_time, &raw_time)) {
        std::wcerr << L">>> Unable to get local time <<<" << std::endl;
        return false;
      }
    }

    return true;
#else
    if(is_gmt) {
      return gmtime_r(&raw_time, &curr_time) != nullptr;
    }

    return localtime_r(&raw_time, &curr_time) != nullptr;
#endif
  }

  //
  // pops an integer from the calculation stack.  this code
  // in normally inlined and there's a macro version available.
  //
  static inline size_t PopInt(size_t* op_stack, size_t* stack_pos) {    
#ifdef _DEBUG
    size_t v = op_stack[--(*stack_pos)];
    std::wcout << L"  [pop_i: stack_pos=" << (*stack_pos) << L"; value=" << v << L"(" << (size_t*)v << L")]" << std::endl;
    return v;
#else
    return op_stack[--(*stack_pos)];
#endif
  }

  //
  // pushes an integer onto the calculation stack. this code
  // in normally inlined and there's a macro version available.
  //
  static inline void PushInt(size_t v, size_t* op_stack, size_t* stack_pos) {
#ifdef _DEBUG
    std::wcout << L"  [push_i: stack_pos=" << (*stack_pos) << L"; value=" << v << L"(" << (size_t*)v << L")]" << std::endl;
#endif
    op_stack[(*stack_pos)++] = v;
  }

  //
  // pushes an double onto the calculation stack.
  //
  static inline void PushFloat(FLOAT_VALUE v, size_t* op_stack, size_t* stack_pos) {
#ifdef _DEBUG
    std::wcout << L"  [push_f: stack_pos=" << (*stack_pos) << L"; value=" << v << L"]" << std::endl;
#endif
    memcpy(&op_stack[(*stack_pos)], &v, sizeof(FLOAT_VALUE));
    (*stack_pos)++;
  }

  //
  // swaps two integers on the calculation stack
  //
  static inline void SwapInt(size_t* op_stack, size_t* stack_pos) {
    size_t v = op_stack[(*stack_pos) - 2];
    op_stack[(*stack_pos) - 2] = op_stack[(*stack_pos) - 1];
    op_stack[(*stack_pos) - 1] = v;
  }

  //
  // pops a double from the calculation stack
  //
  static inline FLOAT_VALUE PopFloat(size_t* op_stack, size_t* stack_pos) {
    FLOAT_VALUE v;
    (*stack_pos)--;

    memcpy(&v, &op_stack[(*stack_pos)], sizeof(FLOAT_VALUE));
#ifdef _DEBUG
    std::wcout << L"  [pop_f: stack_pos=" << (*stack_pos) << L"; value=" << v << L"]" << std::endl;
#endif

    return v;
  }

  //
  // peeks at the integer on the top of the
  // execution stack.
  //
  static inline size_t TopInt(size_t* op_stack, size_t* stack_pos) {
#ifdef _DEBUG
    size_t v = op_stack[(*stack_pos) - 1];
    std::wcout << L"  [top_i: stack_pos=" << (*stack_pos) << L"; value=" << v << L"(" << (void*)v << L")]" << std::endl;
    return v;
#else
    return op_stack[(*stack_pos) - 1];
#endif
  }

  //
  // peeks at the double on the top of the
  // execution stack.
  //
  static inline FLOAT_VALUE TopFloat(size_t* op_stack, size_t* stack_pos) {
    FLOAT_VALUE v;

    size_t index = (*stack_pos) - 1;
    memcpy(&v, &op_stack[index], sizeof(FLOAT_VALUE));
#ifdef _DEBUG
    std::wcout << L"  [top_f: stack_pos=" << (*stack_pos) << L"; value=" << v << L"]" << std::endl;
#endif

    return v;
  }

  // main trap functions
  static bool LoadClsInstId(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool HashStringId(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool LoadNewObjInst(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool LoadClsByInst(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool ConvertBytesToUnicode(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool ConvertUnicodeToBytes(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool LoadMultiArySize(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool CpyCharStrAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool CpyCharStrArys(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool CpyIntStrAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool CpyBoolStrAry(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool CpyByteStrAry(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool CpyFloatStrAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool StdFlush(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool StdOutBool(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool StdOutByte(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool StdOutChar(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool StdOutInt(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool StdOutFloat(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
	static bool StdOutIntFrmt(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
	static bool StdOutFloatFrmt(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
	static bool StdOutFloatPer(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
	static bool StdOutWidth(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
	static bool StdOutFill(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool StdOutString(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool StdOutByteAryLen(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool StdOutCharAryLen(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool StdInByteAryLen(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool StdInCharAryLen(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool StdInString(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool StdErrFlush(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool StdErrBool(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool StdErrByte(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool StdErrChar(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool StdErrInt(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool StdErrFloat(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool StdErrString(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool StdErrCharAry(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool StdErrByteAry(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool AssertTrue(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SysCmd(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
	static bool SetSignal(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
	static bool RaiseSignal(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SysCmdOut(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool Exit(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool GmtTime(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SysTime(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool DateTimeSet1(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool DateTimeSet2(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool DateTimeAddDays(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool DateTimeAddHours(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool DateTimeAddMins(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool DateTimeAddSecs(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool DateToUnixTime(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool DateFromUnixTime(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool DateFromLocalTime(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool TimerStart(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool TimerEnd(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool TimerElapsed(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool GetPltfrm(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool GetUuid(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool GetVersion(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool GetSysProp(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SetSysProp(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool GetSysEnv(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SetSysEnv(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockTcpResolveName(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpHostName(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpConnect(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpBind(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpBindAddress(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockTcpListen(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpAccept(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockTcpSelect(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockTcpClose(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpOutString(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpInString(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpSslConnect(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
	static bool SockTcpSslError(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockTcpError(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockTcpSslIssuer(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockTcpSslSubject(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockTcpSslCertSrv(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockTcpSslClose(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpSslOutString(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpSslInString(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpSslListen(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockTcpSslListenAddress(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static void SecureServerListen(size_t* instance, const std::string& host, size_t*& op_stack, size_t*& stack_pos);
  static bool SockTcpSslAccept(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockTcpSslSelect(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockTcpSslCloseSrv(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  // TCP socket options
  static bool SockTcpSetKeepAlive(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockTcpSetNoDelay(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockTcpSetRecvTimeout(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockTcpSetSendTimeout(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockTcpSetConnTimeout(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockTcpSetDeadline(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockTcpShutdownSend(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockTcpSetRcvBuf(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockTcpSetSndBuf(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  // SSL socket options
  static bool SockTcpSslSetKeepAlive(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockTcpSslSetNoDelay(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockTcpSslSetRecvTimeout(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockTcpSslSetSendTimeout(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockTcpSslSetRcvBuf(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockTcpSslSetSndBuf(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockTcpSslSetMinTLS(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockTcpSslSetVerifyPeer(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockTcpSslGetFingerprint(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  // HTTP/2
  static bool Http2Connect(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool Http2Request(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame,
                           const std::map<std::string, std::string>* extra_headers = nullptr);
  static bool Http2RequestHdrs(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool Http2Close(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  // HTTP/3
  static bool Http3Connect(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool Http3Request(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame,
                           const std::map<std::string, std::string>* extra_headers = nullptr);
  static bool Http3RequestHdrs(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool FloatToStrPrec(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool FileInLine(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool CondNew(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool RuntimeGetError(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool RuntimeGetErrorKind(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool RuntimeClearError(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool RuntimeRaise(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool RuntimeGetLastError(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool AryCopy(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool AryFillInt(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool AryFillFloat(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool FloatToStrShort(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool CompressZlibLevel(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool BytesToAry(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool AryToBytes(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool DeserlBoolAry(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool CondWait(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool CondSignal(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool Http3Close(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockUdpCreate(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockUdpBind(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockUdpClose(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockUdpInByte(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockUdpInByteAry(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockUdpInCharAry(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockUdpOutCharAry(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockUdpOutByte(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockUdpOutByteAry(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockUdpInString(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockUdpOutString(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SerlChar(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SerlInt(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SerlFloat(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SerlObjInst(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SerlByteAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SerlCharAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SerlIntAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SerlObjAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SerlFloatAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool DeserlChar(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool DeserlInt(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool DeserlFloat(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool DeserlObjInst(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool DeserlByteAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool DeserlCharAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool DeserlIntAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool DeserlObjAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool DeserlFloatAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool CompressZlibBytes(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool CompressZlibArray(size_t* array, int level, size_t* &op_stack, size_t* &stack_pos);
  static bool TryUncompressZlibBytes(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool UncompressZlibBytes(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool CompressGzipBytes(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool UncompressGzipBytes(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool CompressDeflateBytes(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool UncompressDeflateBytes(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool CRC32Bytes(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileOpenRead(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileOpenAppend(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileOpenWrite(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileOpenReadWrite(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileClose(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileFlush(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileInString(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileOutString(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileRewind(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  
  static bool PipeCreate(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool PipeOpen(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool PipeInByte(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool PipeOutByte(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool PipeInByteAry(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool PipeInCharAry(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool PipeOutByteAry(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool PipeOutCharAry(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool PipeInString(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool PipeOutString(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool PipeClose(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* fram);
  
  static bool SockTcpIsConnected(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpInByte(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpInByteAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpInCharAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpOutByte(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpOutByteAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpOutCharAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpSslInByte(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpSslInByteAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpSslInCharAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpSslOutByte(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpSslOutByteAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockTcpSslOutCharAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  // DTLS socket operations
  static bool SockDtlsConnect(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockDtlsError(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockDtlsIssuer(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockDtlsSubject(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockDtlsCertSrv(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockDtlsClose(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockDtlsOutString(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockDtlsInString(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockDtlsListen(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockDtlsListenAddress(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static void DtlsServerListen(size_t* instance, const std::string& host, size_t*& op_stack, size_t*& stack_pos);
  static bool SockDtlsAccept(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockDtlsSelect(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockDtlsCloseSrv(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SockDtlsInByte(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockDtlsInByteAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockDtlsInCharAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockDtlsOutByte(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockDtlsOutByteAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool SockDtlsOutCharAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileInByte(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileInCharAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileInByteAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileOutByte(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileOutByteAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileOutCharAry(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileSeek(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileEof(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileIsOpen(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileCanWriteOnly(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileCanReadOnly(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileCanReadWrite(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileExists(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileSize(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileFullPath(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool FileTempName(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool FileAccountOwner(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileGroupOwner(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileDelete(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileRename(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool FileCreateNew(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool ProcessId(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool ProcessAlive(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool ThreadCurrentId(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool ThreadIsAlive(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool UncompressLimit(int window_bits, StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool FileCopy(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool FileCreateTime(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileModifiedTime(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool FileAccessedTime(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool DirCreate(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool DirSlash(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool DirExists(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);
  static bool DirList(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool DirDelete(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool DirCopy(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool DirGetCur(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool DirSetCur(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SymLinkCreate(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SymLinkCopy(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SymLinkLoc(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool SymLinkExists(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  static bool HardLinkCreate(StackProgram* program, size_t* inst, size_t*& op_stack, size_t*& stack_pos, StackFrame* frame);
  
  //
  // writes out serialized objects
  // 
  static void WriteSerializedBytes(const char* array, const long src_buffer_size, size_t* inst,
            size_t* &op_stack, size_t* &stack_pos);
  
  //
  // serializes an array
  // 
  static void SerializeArray(const size_t* array, ParamType type, size_t* inst,
            size_t* &op_stack, size_t* &stack_pos);

  //
  // reads a serialized array
  // 
  static void ReadSerializedBytes(size_t* dest_array, const size_t* src_array,
           ParamType type, size_t* inst);

  //
  // deserializes an array of objects
  // 
  static inline size_t* DeserializeArray(ParamType type, size_t* inst, size_t* &op_stack, size_t* &stack_pos);

  //
  // expand buffer
  //
  static size_t* ExpandSerialBuffer(const long src_buffer_size, size_t* dest_buffer, size_t* inst,
          size_t* &op_stack, size_t* &stack_pos);
  
  // 
  // serializes a byte
  // 
  static void SerializeByte(char value, size_t* inst, size_t* &op_stack, size_t* &stack_pos);

  // 
  // a read that finds the stream short, or an array that declares more than the
  // stream holds, answers its zero value and clears the Deserializer's third field,
  // @is_ok (buffer, position, is_ok), so IsOk() can tell a damaged stream from a real zero
  // 
  static void DeserializeFailed(size_t* inst) {
    inst[2] = 0;
  }

  // 
  // deserializes a byte
  // 
  static char DeserializeByte(size_t* inst);

  // 
  // serializes a char
  // 
  static void SerializeChar(wchar_t value, size_t* inst, size_t* &op_stack, size_t* &stack_pos);

  // 
  // deserializes a char
  // 
  static wchar_t DeserializeChar(size_t* inst);

  // 
  // serializes an int
  // 
  static void SerializeInt(INT64_VALUE value, size_t* inst, size_t* &op_stack, size_t* &stack_pos);

  // 
  // deserializes an int
  // 
  static INT64_VALUE DeserializeInt(size_t* inst);

  // 
  // serializes a float
  // 
  static void SerializeFloat(FLOAT_VALUE value, size_t* inst, size_t* &op_stack, size_t* &stack_pos);

  // 
  // deserializes a float
  // 
  static FLOAT_VALUE DeserializeFloat(size_t* inst);

  //
  // serialize and deserialize object instances
  //
  static void SerializeObject(size_t* inst, StackFrame* frame, size_t* &op_stack, size_t* &stack_pos);
  static void DeserializeObject(size_t* inst, size_t* &op_stack, size_t* &stack_pos);

  //
  // time functions
  //
  static inline void ProcessCurrentTime(StackFrame* frame, bool is_gmt);
  static inline void ProcessSetTime1(size_t* &op_stack, size_t* &stack_pos);
  static inline void ProcessSetTime2(size_t* &op_stack, size_t* &stack_pos);
  static inline void ProcessAddTime(TimeInterval t, size_t* &op_stack, size_t* &stack_pos);
  static inline void ProcessTimerStart(size_t* &op_stack, size_t* &stack_pos);
  static inline void ProcessTimerEnd(size_t* &op_stack, size_t* &stack_pos);
  static inline void ProcessTimerElapsed(size_t* &op_stack, size_t* &stack_pos);
  
  // 
  // platform string
  //
  static inline void ProcessPlatform(StackProgram* program, size_t* &op_stack, size_t* &stack_pos);

  // 
  // platform string
  //
  static inline void ProcessFileOwner(const wchar_t* name, bool is_account,
                                      StackProgram* program, size_t* &op_stack, size_t* &stack_pos);
  
  // 
  // version string
  //
  static inline void ProcessVersion(StackProgram* program, size_t* &op_stack, size_t* &stack_pos);

  //
  // creates new object and call default constructor
  //
  static inline bool CreateNewObject(const std::wstring &cls_id, size_t* &op_stack, size_t* &stack_pos);

  //
  // creates a new class instance
  //
  static inline void CreateClassObject(StackClass* cls, size_t* cls_obj, size_t* &op_stack, 
               size_t* &stack_pos, StackProgram* program);

  //
  // creates an instance of the 'Method' class
  //
  static inline size_t* CreateMethodObject(size_t* cls_obj, StackMethod* mthd, StackProgram* program,
           size_t* &op_stack, size_t* &stack_pos);

  //
  // creates a string object instance
  //
  static inline size_t* CreateStringObject(const std::wstring &value_str, StackProgram* program, size_t* &op_stack, size_t* &stack_pos);

 public:

  static bool ProcessTrap(StackProgram* program, size_t* inst, size_t* &op_stack, size_t* &stack_pos, StackFrame* frame);

#ifdef _MODULE
  static StackProgram* program;

  static void Initialize(StackProgram* p) {
    program = p;
  }
#endif
};

bool EndsWith(std::wstring const& str, std::wstring const& ending);

// System.Concurrency.ThreadMutex. The native lock lives on the C++ heap and
// the object's first word field (@m0) holds its address: a young object is
// moved when it is promoted, and a lock that threads are blocked on cannot
// move. Locking parks the thread while it waits, so a collection started by
// the thread holding the lock (it allocates inside critical) can finish.
void ThreadMutexCreate(size_t* instance);
void ThreadMutexLock(size_t* instance);
void ThreadMutexUnlock(size_t* instance);
// Releases every lock the calling thread still holds (thread-fault isolation:
// a thread that ends in a fault inside a critical section); returns how many
size_t ThreadMutexReleaseAll();

/********************************
 * Call back for DLL method calls
 ********************************/
void APITools_MethodCall(size_t* op_stack, size_t *stack_pos, size_t* instance, 
                         const wchar_t* cls_id, const wchar_t* mthd_id);
void APITools_MethodCallId(size_t* op_stack, size_t *stack_pos, size_t* instance, 
                           const int cls_id, const int mthd_id);

/********************************
 * SSL password callback
 ********************************/
int pem_passwd_cb(char* buffer, int size, int rw_flag, void* passwd);

/********************************
 * GUUID/UUID generation (version 4)
 ********************************/

inline void secure_random_bytes(std::uint8_t* dst, std::size_t len) {
#if defined(_WIN32)
   if(BCryptGenRandom(nullptr, dst, static_cast<ULONG>(len), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
      throw std::runtime_error("BCryptGenRandom failed");
   }
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
   arc4random_buf(dst, len);
#elif defined(__linux__)
   // Try getrandom first (no file descriptor needed)
   std::size_t done = 0;
   while(done < len) {
      ssize_t r = getrandom(dst + done, len - done, 0);
      if(r > 0) { done += static_cast<std::size_t>(r); continue; }
      if(r < 0 && errno == EINTR) continue;

      // Fallback to /dev/urandom if getrandom not available or blocked
      int fd = open("/dev/urandom", O_RDONLY);
      if(fd < 0) throw std::runtime_error("open(/dev/urandom) failed");
      std::size_t got = 0;
      while(got < (len - done)) {
         ssize_t rr = read(fd, dst + done + got, (len - done) - got);
         if(rr > 0) { got += static_cast<std::size_t>(rr); continue; }
         if(rr < 0 && errno == EINTR) continue;
         close(fd);
         throw std::runtime_error("read(/dev/urandom) failed");
      }
      close(fd);

      done += got;
   }
#else
   // Very portable fallback; quality depends on the implementation.
   // Prefer adding a platform path above if you target a specific OS.
   for(std::size_t i = 0; i < len; ++i) {
      dst[i] = static_cast<std::uint8_t>(std::rand() & 0xFF); \
   }
#endif
 }

 inline std::wstring uuidv4() {
   std::array<std::uint8_t, 16> bytes{};
   secure_random_bytes(bytes.data(), bytes.size());

   // Set version (4) and variant (RFC 4122)
   bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0F) | 0x40); // version 4
   bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3F) | 0x80); // variant 10xxxxxx

   // Format 8-4-4-4-12
   std::wostringstream oss;
   oss << std::hex << std::nouppercase << std::setfill(L'0');
   for(int i = 0; i < 16; ++i) {
      oss << std::setw(2) << static_cast<int>(bytes[i]);
      if(i == 3 || i == 5 || i == 7 || i == 9) {
         oss << '-';
      }
   }

   return oss.str();
 }
