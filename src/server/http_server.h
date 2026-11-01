// Minimal HTTP/1.1 server bound to the loopback interface (transport layer only:
// no engineering logic lives here). One short-lived thread per connection,
// capped by kMaxConnections; every response closes the connection.
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <string>

namespace wb {

struct HttpRequest {
    std::string method;
    std::string path;                          // decoded, without query string
    std::map<std::string, std::string> query;  // decoded key/value pairs
    std::map<std::string, std::string> headers;  // lower-case names
    std::string body;
};

struct HttpResponse {
    int status = 200;
    std::string contentType = "application/json; charset=utf-8";
    std::string body;
    std::map<std::string, std::string> headers;

    static HttpResponse json(std::string body, int status = 200);
    static HttpResponse error(int status, const std::string& message);
};

using HttpHandler = std::function<HttpResponse(const HttpRequest&)>;

class HttpServer {
public:
    static constexpr std::size_t kMaxBodyBytes = 70u * 1024u * 1024u;
    static constexpr int kMaxConnections = 32;

    HttpServer();
    ~HttpServer();
    // Tries ports [firstPort, lastPort] on 127.0.0.1; returns the bound port or 0.
    int listen(int firstPort, int lastPort);
    // Blocks until stop() is called.
    void serve(const HttpHandler& handler);
    void stop();

private:
    void handleConnection(std::uintptr_t sock, const HttpHandler& handler);

    std::uintptr_t listenSock_;
    std::atomic<bool> running_{false};
    std::atomic<int> active_{0};
};

std::string urlDecode(const std::string& s);

}  // namespace wb
