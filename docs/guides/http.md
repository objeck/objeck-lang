# HTTP client and web server

Objeck's standard library has an HTTP/1.1 client and server (`Web.HTTP` and
`Web.HTTP.Server`, in the `net` and `net_server` libraries), an HTTP/2 client and an HTTP/3
(QUIC) client. This page covers the HTTP/1.1 pair: how to answer requests, how to make them, and
what v2026.10.3 changed about connections.

Compile with:

```text
obc -src program.obs -lib net,net_server,json,cipher
```

(Leave out `net_server` if you only make requests.)

## The two shipped examples

- [`http_client_22.obs`](../../programs/deploy/http_client_22.obs): an HTTPS `GET`, the status
  code, the response headers, the body parsed as JSON, and every way the request can fail.
  It needs an internet connection.
- [`web_server_23.obs`](../../programs/deploy/web_server_23.obs): a two-route server that calls
  itself and stops.

Read those first. The program below adds a `POST` route and counts connections.

## A server, a client and a connection count

A server is a class that inherits from `HttpRequestHandler` and fills in `ProcessGet` and
`ProcessPost`. Each receives a `Request` (path, query parameters, headers, body) and a
`Response` to fill in; returning `false` says the request is fully answered.
`WebServer->Serve(class, address, port)` runs it and does not return until the server stops, so
the program starts it on a thread, with port `0` meaning "any free port", and asks
`WebServer->GetBoundPort()` which port it got.

The server makes one handler object for each connection it accepts, and that handler serves
every request that arrives on the connection. So a counter incremented in the handler's
constructor counts connections, which is what lets this program show whether the client reuses
them.

<!-- code: programs/examples/guides/http_keepalive.obs -->
```ruby
use System.Concurrency;
use System.IO.Filesystem;
use Web.HTTP.Server;
use Data.JSON;

class CountingHandler from HttpRequestHandler {
  @connections : static : Int;
  @requests : static : Int;
  @lock : static : ThreadMutex;

  New() {
    Parent();
    critical(@lock) {
      @connections += 1;
    };
  }

  method : ProcessGet(request : Request, response : Response) ~ Bool {
    critical(@lock) {
      @requests += 1;
    };

    # /hello?name=Ada answers {"hello":"Ada"}
    if(request->GetPath()->Equals("/hello")) {
      name := request->GetParam("name");
      if(name = Nil) {
        name := "world";
      };
      response->SetCodeTypeContent(200, "application/json", "{\"hello\":\"{$name}\"}");
    }
    else {
      response->SetCodeContent(404, "Not found");
    };

    return false;
  }

  # POST /total with a JSON array of numbers answers their sum
  method : ProcessPost(request : Request, response : Response) ~ Bool {
    critical(@lock) {
      @requests += 1;
    };

    parser := JsonParser->New(request->GetContent());
    if(<>parser->Parse()) {
      response->SetCodeContent(400, "Not JSON");
      return false;
    };

    sum := 0.0;
    numbers := parser->GetRoot();
    each(i : numbers->Size()) {
      sum += numbers->Get(i)->GetFloat();
    };
    response->SetCodeTypeContent(200, "application/json", "{\"total\":{$sum}}");
    return false;
  }

  function : Prepare() ~ Nil {
    @lock := ThreadMutex->New("counting_handler");
  }

  function : GetConnections() ~ Int {
    return @connections;
  }

  function : GetRequests() ~ Int {
    return @requests;
  }
}

class HttpKeepAlive {
  function : Main(args : String[]) ~ Nil {
    CountingHandler->Prepare();

    # Serve() blocks, so it runs on its own thread; port 0 asks for any free port
    server := Thread->New() {
      New() {
        Parent();
      }

      method : public : Run(arg : System.Base) ~ Nil {
        WebServer->Serve(CountingHandler->New()->GetClass(), "127.0.0.1", 0);
      }
    };
    server->Execute(Nil);

    waited := 0;
    while(WebServer->GetBoundPort() = 0 & waited < 10000) {
      Thread->Sleep(25);
      waited += 25;
    };
    port := WebServer->GetBoundPort();
    if(port = 0) {
      "The server did not start."->PrintLine();
      return;
    };
    # the handler made to name the class counts as a connection; start from there
    base := CountingHandler->GetConnections();

    # --- twenty GETs from one client ---
    client := Web.HTTP.HttpClient->New();
    ok := 0;
    each(i : 20) {
      url := Web.HTTP.Url->New("http://127.0.0.1:{$port}/hello?name=n{$i}");
      response := client->Get(url);
      if(response <> Nil & response->GetCode() = 200) {
        ok += 1;
      };
    };
    "GET:  {$ok} of 20 answered, {$(CountingHandler->GetConnections() - base)} connection(s) used"->PrintLine();

    # one body, to see what the server sends
    response := client->Get(Web.HTTP.Url->New("http://127.0.0.1:{$port}/hello?name=Ada"));
    "      {$(response->GetContent()->ToString())}"->PrintLine();

    # --- three POSTs: each one opens its own connection ---
    before := CountingHandler->GetConnections();
    body := "[1.5, 2.5, 3]"->ToByteArray();
    each(i : 3) {
      response := client->Post(Web.HTTP.Url->New("http://127.0.0.1:{$port}/total"), body, "application/json");
      if(i = 0) {
        "POST: {$(response->GetContent()->ToString())}"->PrintLine();
      };
    };
    "POST: 3 sent, {$(CountingHandler->GetConnections() - before)} new connection(s) used"->PrintLine();

    # --- close what is still waiting, then stop the server ---
    Web.HTTP.HttpClient->CloseIdleConnections();
    WebServer->Stop();
    "requests served: {$(CountingHandler->GetRequests())}"->PrintLine();
  }
}
```

```text
obc -src http_keepalive.obs -lib net,net_server,json,cipher
obr http_keepalive.obe
```

```text
GET:  20 of 20 answered, 1 connection(s) used
      {"hello":"Ada"}
POST: {"total":7.000000}
POST: 3 sent, 3 new connection(s) used
requests served: 24
```

Twenty `GET`s from one `HttpClient` used one connection; three `POST`s used three. Run with a
toolchain built before this change, the same program (without the `CloseIdleConnections` line)
printed `20 connection(s) used` for the `GET`s.

## Connection reuse in v2026.10.3

Before 10.3 every request opened a connection and closed it. The side that closes first keeps
the local port in `TIME_WAIT` for about two minutes, and on Windows a process making about 130
requests a second used all 16,384 ports, after which every connect failed at once
(issues [#1443](https://github.com/objeck/objeck-lang/issues/1443),
[#1503](https://github.com/objeck/objeck-lang/issues/1503) and
[#1516](https://github.com/objeck/objeck-lang/issues/1516)).

Now `HttpClient` keeps plain-HTTP connections:

- A connection waits in a pool (up to 16) when the server says `keep-alive` and the response
  ends where its own framing says it does. It is reused for the next request to the same host
  and port, if that comes within 4 seconds.
- `GET` and `DELETE` reuse a waiting connection. If the reused connection answers nothing (the
  server closed it), the request goes once more on a new connection, as RFC 9112 section 9.3.1
  allows.
- `POST`, `PUT` and `PATCH` never reuse a connection.
- `Web.HTTP.HttpClient->CloseIdleConnections()` closes the connections still waiting.
  `WebServer->Stop()` calls it too; the program above calls it first to show the call.
- The pool belongs to `HttpClient` (plain HTTP). `HttpsClient` does not use it.

If your program stops one server and starts another on the same port within four seconds, and
relied on every request getting a fresh connection, wait for the old server to exit first.

## Where next

- The [CHANGELOG](../../CHANGELOG.md) entry for v2026.10.3 has the measurements behind connection
  reuse.
- The HTTP/2 and HTTP/3 clients (`Http2Client`, `Http3Client`) are shown in the
  [README](../../README.md#see-it-in-action).
- [Data and text](data_and_text.md): parsing the JSON a server returns.
