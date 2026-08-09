/***************************************************************************
 * HTTP/3 backend for Windows, built on WinHTTP.
 *
 * The POSIX HTTP/3 implementation is ngtcp2 + nghttp3 + GnuTLS. None of that
 * exists in the Windows toolchain (which uses mbedTLS), so Http3Client shipped
 * documented-and-dead here: the trap handlers compile unconditionally, so the
 * API was present and simply always failed.
 *
 * Windows already has an HTTP/3 stack -- WinHTTP over MsQuic, from Windows 11
 * / Server 2022 on. Using it means no vendored QUIC library, no second TLS
 * backend, no new DLL to deploy, and no risk to the working POSIX path, which
 * this file does not touch.
 *
 * The one thing this must never do is silently succeed over HTTP/2. WinHTTP
 * treats HTTP/3 as a preference and falls back transparently, so every request
 * asserts WINHTTP_OPTION_HTTP_PROTOCOL_USED really was HTTP/3 and fails
 * otherwise -- an Http3Client that quietly spoke HTTP/2 would be the same
 * false promise this is fixing.
 ***************************************************************************/

#ifndef __WIN_HTTP3_H__
#define __WIN_HTTP3_H__

#ifdef OBJECK_HAS_WINHTTP_H3

#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include <map>

// UTF-8 <-> wide conversion: use the shared, locale-independent codecs in
// sys.h rather than hand-rolling MultiByteToWideChar here. common.cpp includes
// this header AFTER common.h so they are in scope.
#include "../shared/sys.h"

// These arrived in newer Windows SDKs. Defining the fallbacks keeps the build
// working against an older SDK -- the values are ABI, so a binary built this
// way still negotiates HTTP/3 at runtime on a Windows that supports it.
#ifndef WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL
#define WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL 133
#endif
#ifndef WINHTTP_OPTION_HTTP_PROTOCOL_USED
#define WINHTTP_OPTION_HTTP_PROTOCOL_USED 134
#endif
#ifndef WINHTTP_PROTOCOL_FLAG_HTTP2
#define WINHTTP_PROTOCOL_FLAG_HTTP2 0x1
#endif
#ifndef WINHTTP_PROTOCOL_FLAG_HTTP3
#define WINHTTP_PROTOCOL_FLAG_HTTP3 0x2
#endif

//
// A WinHTTP-backed HTTP/3 session. Mirrors the parts of Http3SessionCtx the
// traps actually use, so the trap bodies stay the same shape on both paths.
//
struct Http3WinCtx {
  HINTERNET   session;
  HINTERNET   connect;   // host/port live in this handle; no need to keep copies

  int          response_status;
  std::map<std::string, std::string> response_headers;
  std::vector<uint8_t>               response_body;

  // Always empty today, and deliberately kept. Http3Client->AddHeader() stores
  // headers on the Objeck side, but the Http3Request trap signature is
  // (body, content_type, path, method, instance) -- there is no headers
  // argument, so they never reach the VM. That gap is pre-existing and affects
  // the ngtcp2 backend identically (see Http3SessionCtx::request_headers).
  // Kept so that whenever the trap learns to pass headers, both backends send
  // them; dropping it would leave Windows silently worse than POSIX.
  std::map<std::string, std::string> request_headers;

  Http3WinCtx() : session(nullptr), connect(nullptr), response_status(0) {}

  ~Http3WinCtx() {
    if(connect) { WinHttpCloseHandle(connect); }
    if(session) { WinHttpCloseHandle(session); }
  }
};

//
// Open a session pinned to HTTP/3 and connect to the host.
// Returns nullptr on failure; the caller stores the result in instance[0].
//
static Http3WinCtx* h3win_connect(const std::string& host, int port) {
  Http3WinCtx* ctx = new Http3WinCtx();
  ctx->session = WinHttpOpen(L"objeck-http3/1.0",
                             WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                             WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if(!ctx->session) { delete ctx; return nullptr; }

  // Must be set on the SESSION handle, before any request is created --
  // setting it on the request afterwards is accepted and silently ignored.
  DWORD want = WINHTTP_PROTOCOL_FLAG_HTTP3;
  if(!WinHttpSetOption(ctx->session, WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL,
                       &want, sizeof(want))) {
    // This Windows build has no HTTP/3. Fail here rather than connecting and
    // silently delivering HTTP/2 later.
    delete ctx;
    return nullptr;
  }

  ctx->connect = WinHttpConnect(ctx->session, BytesToUnicode(host).c_str(),
                                (INTERNET_PORT)port, 0);
  if(!ctx->connect) { delete ctx; return nullptr; }

  return ctx;
}

//
// Issue one request and collect the response. Returns false on any failure,
// including a successful HTTP exchange that turned out not to be HTTP/3.
// Note TLS verification is left at WinHTTP's default (full verification) --
// nothing here may weaken it.
//
// Closes the per-request handle on every exit path, so the early returns below
// cannot leak it.
struct H3RequestHandle {
  HINTERNET h;
  explicit H3RequestHandle(HINTERNET handle) : h(handle) {}
  ~H3RequestHandle() { if(h) { WinHttpCloseHandle(h); } }
  operator HINTERNET() const { return h; }
};

static bool h3win_request(Http3WinCtx* ctx,
                          const std::string& method,
                          const std::string& path,
                          const std::string& content_type,
                          const uint8_t* body, size_t body_len) {
  if(!ctx || !ctx->connect) { return false; }

  ctx->response_status = 0;
  ctx->response_body.clear();
  ctx->response_headers.clear();

  H3RequestHandle request(WinHttpOpenRequest(ctx->connect,
                                         BytesToUnicode(method).c_str(),
                                         BytesToUnicode(path).c_str(),
                                         nullptr, WINHTTP_NO_REFERER,
                                         WINHTTP_DEFAULT_ACCEPT_TYPES,
                                         WINHTTP_FLAG_SECURE));
  if(!request) { return false; }

  std::wstring headers;
  if(!content_type.empty()) {
    headers += L"Content-Type: " + BytesToUnicode(content_type) + L"\r\n";
  }
  for(std::map<std::string, std::string>::const_iterator it = ctx->request_headers.begin();
      it != ctx->request_headers.end(); ++it) {
    headers += BytesToUnicode(it->first) + L": " + BytesToUnicode(it->second) + L"\r\n";
  }
  if(!headers.empty()) {
    // Fail the request if the block is rejected. Discarding this made a
    // rejected header block indistinguishable from a sent one -- the same
    // silently-does-nothing failure this backend exists to eliminate.
    if(!WinHttpAddRequestHeaders(request, headers.c_str(), (DWORD)-1,
                                 WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) {
      return false;
    }
  }

  const BOOL sent = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                       (LPVOID)(body_len ? (LPVOID)body : WINHTTP_NO_REQUEST_DATA),
                                       (DWORD)body_len, (DWORD)body_len, 0);
  if(!sent || !WinHttpReceiveResponse(request, nullptr)) {
    return false;
  }

  // The decisive check: WinHTTP falls back to HTTP/2 transparently, so a 200
  // proves nothing about the protocol. Refuse a downgrade rather than let
  // Http3Client report success for a connection that was never HTTP/3.
  DWORD used = 0, used_len = sizeof(used);
  if(!WinHttpQueryOption(request, WINHTTP_OPTION_HTTP_PROTOCOL_USED, &used, &used_len) ||
     (used & WINHTTP_PROTOCOL_FLAG_HTTP3) == 0) {
    return false;
  }

  DWORD status = 0, status_len = sizeof(status);
  if(!WinHttpQueryHeaders(request,
                          WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                          WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_len,
                          WINHTTP_NO_HEADER_INDEX)) {
    return false;
  }
  ctx->response_status = (int)status;

  // Content-Type, so the trap can populate instance[6] as the POSIX path does.
  DWORD ct_len = 0;
  WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_TYPE,
                      WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &ct_len,
                      WINHTTP_NO_HEADER_INDEX);
  if(ct_len > 0 && GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
    std::wstring ct((size_t)(ct_len / sizeof(wchar_t)) + 1, L'\0');
    if(WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_TYPE,
                           WINHTTP_HEADER_NAME_BY_INDEX, &ct[0], &ct_len,
                           WINHTTP_NO_HEADER_INDEX)) {
      ct.resize(wcslen(ct.c_str()));
      ctx->response_headers["content-type"] = UnicodeToBytes(ct);
    }
  }

  // Drain the body.
  for(;;) {
    DWORD avail = 0;
    if(!WinHttpQueryDataAvailable(request, &avail)) {
      return false;
    }
    if(avail == 0) { break; }

    const size_t offset = ctx->response_body.size();
    ctx->response_body.resize(offset + avail);
    DWORD read = 0;
    if(!WinHttpReadData(request, &ctx->response_body[offset], avail, &read)) {
      return false;
    }
    // A short read is normal; shrink to what actually arrived.
    ctx->response_body.resize(offset + read);
    if(read == 0) { break; }
  }

  return true;
}

#endif // OBJECK_HAS_WINHTTP_H3
#endif // __WIN_HTTP3_H__
