#include "http_server.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <sstream>
#include <thread>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include "json_writer.h"

namespace wb {

namespace {

const char* reason(int status) {
    switch (status) {
        case 200: return "OK";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 413: return "Payload Too Large";
        case 422: return "Unprocessable Entity";
        case 500: return "Internal Server Error";
        case 503: return "Service Unavailable";
        default: return "Status";
    }
}

bool sendAll(SOCKET s, const char* data, std::size_t len) {
    while (len > 0) {
        const int n = ::send(s, data, static_cast<int>(std::min<std::size_t>(len, 1 << 20)), 0);
        if (n <= 0) return false;
        data += n;
        len -= static_cast<std::size_t>(n);
    }
    return true;
}

void writeResponse(SOCKET s, const HttpResponse& r) {
    std::ostringstream h;
    h << "HTTP/1.1 " << r.status << ' ' << reason(r.status) << "\r\n"
      << "Content-Type: " << r.contentType << "\r\n"
      << "Content-Length: " << r.body.size() << "\r\n"
      << "Cache-Control: no-store\r\n"
      << "X-Content-Type-Options: nosniff\r\n"
      << "Connection: close\r\n";
    for (const auto& kv : r.headers) h << kv.first << ": " << kv.second << "\r\n";
    h << "\r\n";
    const std::string head = h.str();
    if (sendAll(s, head.data(), head.size())) sendAll(s, r.body.data(), r.body.size());
}

std::map<std::string, std::string> parseQuery(const std::string& q) {
    std::map<std::string, std::string> out;
    std::istringstream ss(q);
    std::string pair;
    while (std::getline(ss, pair, '&')) {
        if (pair.empty()) continue;
        const auto eq = pair.find('=');
        if (eq == std::string::npos) out[urlDecode(pair)] = "";
        else out[urlDecode(pair.substr(0, eq))] = urlDecode(pair.substr(eq + 1));
    }
    return out;
}

}  // namespace

std::string urlDecode(const std::string& s) {
    std::string r;
    r.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+') r += ' ';
        else if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
                 std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            r += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else
            r += s[i];
    }
    return r;
}

HttpResponse HttpResponse::json(std::string body, int status) {
    HttpResponse r;
    r.status = status;
    r.body = std::move(body);
    return r;
}

HttpResponse HttpResponse::error(int status, const std::string& message) {
    JsonWriter w;
    w.beginObject().field("ok", false).field("error", message).endObject();
    return json(w.str(), status);
}

HttpServer::HttpServer() : listenSock_(INVALID_SOCKET) {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
}

HttpServer::~HttpServer() {
    stop();
    // Give in-flight connection threads a moment to finish before tearing down Winsock.
    for (int i = 0; i < 50 && active_.load() > 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    WSACleanup();
}

int HttpServer::listen(int firstPort, int lastPort) {
    for (int port = firstPort; port <= lastPort; ++port) {
        SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) return 0;
        BOOL excl = TRUE;  // never share the port with another process
        setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&excl), sizeof(excl));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<u_short>(port));
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // loopback only: not reachable from the network
        if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0 && ::listen(s, SOMAXCONN) == 0) {
            listenSock_ = static_cast<std::uintptr_t>(s);
            running_ = true;
            return port;
        }
        closesocket(s);
    }
    return 0;
}

void HttpServer::stop() {
    if (running_.exchange(false) && listenSock_ != INVALID_SOCKET) {
        closesocket(static_cast<SOCKET>(listenSock_));
        listenSock_ = INVALID_SOCKET;
    }
}

void HttpServer::serve(const HttpHandler& handler) {
    while (running_) {
        const SOCKET c = ::accept(static_cast<SOCKET>(listenSock_), nullptr, nullptr);
        if (c == INVALID_SOCKET) continue;  // stop() closes the socket and ends the loop
        if (active_.load() >= kMaxConnections) {
            writeResponse(c, HttpResponse::error(503, "Server busy; retry in a moment."));
            closesocket(c);
            continue;
        }
        ++active_;
        std::thread([this, c, &handler] {
            handleConnection(static_cast<std::uintptr_t>(c), handler);
            --active_;
        }).detach();
    }
}

void HttpServer::handleConnection(std::uintptr_t sockValue, const HttpHandler& handler) {
    const SOCKET s = static_cast<SOCKET>(sockValue);
    DWORD timeoutMs = 30000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));

    std::string buf;
    char chunk[65536];
    std::size_t headerEnd = std::string::npos;
    while (headerEnd == std::string::npos) {
        const int n = ::recv(s, chunk, sizeof(chunk), 0);
        if (n <= 0 || buf.size() > 64 * 1024) {
            closesocket(s);
            return;
        }
        buf.append(chunk, static_cast<std::size_t>(n));
        headerEnd = buf.find("\r\n\r\n");
    }

    HttpRequest req;
    std::istringstream hs(buf.substr(0, headerEnd));
    std::string line, target, version;
    std::getline(hs, line);
    std::istringstream rl(line);
    rl >> req.method >> target >> version;
    while (std::getline(hs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = line.substr(0, colon);
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        std::string value = line.substr(colon + 1);
        value.erase(0, value.find_first_not_of(' '));
        req.headers[name] = value;
    }
    const auto qpos = target.find('?');
    req.path = urlDecode(target.substr(0, qpos));
    if (qpos != std::string::npos) req.query = parseQuery(target.substr(qpos + 1));

    std::size_t contentLength = 0;
    if (req.headers.count("content-length")) {
        try {
            contentLength = static_cast<std::size_t>(std::stoull(req.headers["content-length"]));
        } catch (...) {
            writeResponse(s, HttpResponse::error(400, "Invalid Content-Length header."));
            closesocket(s);
            return;
        }
    }
    if (contentLength > kMaxBodyBytes) {
        writeResponse(s, HttpResponse::error(413, "Upload is larger than the 70 MB transport limit."));
        closesocket(s);
        return;
    }
    req.body = buf.substr(headerEnd + 4);
    while (req.body.size() < contentLength) {
        const int n = ::recv(s, chunk, sizeof(chunk), 0);
        if (n <= 0) {
            closesocket(s);
            return;
        }
        req.body.append(chunk, static_cast<std::size_t>(n));
    }
    req.body.resize(contentLength);

    HttpResponse resp;
    try {
        resp = handler(req);
    } catch (const std::exception& e) {
        resp = HttpResponse::error(500, std::string("Internal error: ") + e.what());
    }
    writeResponse(s, resp);
    shutdown(s, SD_SEND);
    closesocket(s);
}

}  // namespace wb
