#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using sock_t = SOCKET;
static void closeSock(sock_t s) { closesocket(s); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using sock_t = int;
constexpr sock_t INVALID_SOCKET = -1;
static void closeSock(sock_t s) { close(s); }
#endif

#include "HttpDownload.hpp"
#include "emu/ResumeState.hpp"

namespace fs = std::filesystem;

namespace {

// Serves one canned response per connection on 127.0.0.1, then closes.
class LoopbackServer {
public:
    using Handler = std::function<std::string(const std::string& request)>;

    explicit LoopbackServer(Handler handler) : handler_(std::move(handler)) {
#ifdef _WIN32
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
#endif
        listener_ = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        REQUIRE(bind(listener_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) == 0);
        REQUIRE(listen(listener_, 8) == 0);
        socklen_t len = sizeof addr;
        getsockname(listener_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);
        thread_ = std::thread([this] { run(); });
    }

    ~LoopbackServer() {
        stop_ = true;
        thread_.join();
        closeSock(listener_);
    }

    std::string url(const std::string& path = "/file") const {
        return "http://127.0.0.1:" + std::to_string(port_) + path;
    }

    int requestCount() {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<int>(requests_.size());
    }

    std::string lastRequest() {
        std::lock_guard<std::mutex> lock(mutex_);
        return requests_.empty() ? std::string() : requests_.back();
    }

private:
    void run() {
        while (!stop_) {
            fd_set set;
            FD_ZERO(&set);
            FD_SET(listener_, &set);
            timeval tv{0, 50000};
            if (select(static_cast<int>(listener_) + 1, &set, nullptr, nullptr, &tv) <= 0)
                continue;
            sock_t client = accept(listener_, nullptr, nullptr);
            if (client == INVALID_SOCKET) continue;
            std::string request;
            char buf[1024];
            while (request.find("\r\n\r\n") == std::string::npos) {
                int n = recv(client, buf, sizeof buf, 0);
                if (n <= 0) break;
                request.append(buf, static_cast<size_t>(n));
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                requests_.push_back(request);
            }
            std::string response = handler_(request);
            size_t sent = 0;
            while (sent < response.size()) {
                int n = send(client, response.data() + sent,
                             static_cast<int>(response.size() - sent), 0);
                if (n <= 0) break;
                sent += static_cast<size_t>(n);
            }
            closeSock(client);
        }
    }

    Handler handler_;
    sock_t listener_ = INVALID_SOCKET;
    unsigned short port_ = 0;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::mutex mutex_;
    std::vector<std::string> requests_;
};

std::string response(const std::string& status, const std::string& headers,
                     const std::string& body, size_t declaredLength = std::string::npos) {
    size_t length = declaredLength == std::string::npos ? body.size() : declaredLength;
    return "HTTP/1.1 " + status + "\r\n" + headers + "Content-Length: " + std::to_string(length) +
           "\r\nConnection: close\r\n\r\n" + body;
}

std::string partIn(const char* name) {
    fs::path dir = fs::temp_directory_path() / "eqx_http_resumable_test";
    fs::create_directories(dir);
    std::string part = (dir / name).string();
    fs::remove(part);
    fs::remove(part + ".resume");
    return part;
}

void writeFile(const std::string& path, const std::string& content) {
    std::ofstream(path, std::ios::binary) << content;
}

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool exists(const std::string& path) { return fs::exists(path); }

const Progress kNoProgress;

}  // namespace

TEST_CASE("fresh download writes the part and clears the control file") {
    LoopbackServer server([](const std::string&) {
        return response("200 OK", "ETag: \"v1\"\r\n", std::string(100, 'a'));
    });
    std::string part = partIn("fresh.part");
    ResumableResult r = httpGetResumable(server.url(), part, kNoProgress);
    CHECK(r.ok);
    CHECK_FALSE(r.restarted);
    CHECK_FALSE(r.writeFailed);
    CHECK(r.status == 200);
    CHECK(readFile(part) == std::string(100, 'a'));
    CHECK_FALSE(exists(part + ".resume"));
    CHECK(server.lastRequest().find("Range:") == std::string::npos);
}

TEST_CASE("206 appends to the existing part and sends Range and If-Range") {
    LoopbackServer server([](const std::string&) {
        return response("206 Partial Content",
                        "ETag: \"v1\"\r\nContent-Range: bytes 40-99/100\r\n",
                        std::string(60, 'b'));
    });
    std::string part = partIn("append.part");
    writeFile(part, std::string(40, 'a'));
    REQUIRE(emu::saveResume(part, {server.url(), "\"v1\"", 100}));

    uint64_t lastDone = 0;
    Progress progress = [&](uint64_t done, uint64_t) {
        lastDone = done;
        return true;
    };
    ResumableResult r = httpGetResumable(server.url(), part, progress);
    CHECK(r.ok);
    CHECK_FALSE(r.restarted);
    CHECK(readFile(part) == std::string(40, 'a') + std::string(60, 'b'));
    CHECK_FALSE(exists(part + ".resume"));
    CHECK(lastDone == 100);
    std::string request = server.lastRequest();
    CHECK(request.find("Range: bytes=40-") != std::string::npos);
    CHECK(request.find("If-Range: \"v1\"") != std::string::npos);
}

TEST_CASE("200 on an If-Range mismatch truncates the part and sets restarted") {
    LoopbackServer server([](const std::string&) {
        return response("200 OK", "ETag: \"v2\"\r\n", std::string(100, 'c'));
    });
    std::string part = partIn("restart.part");
    writeFile(part, std::string(40, 'a'));
    REQUIRE(emu::saveResume(part, {server.url(), "\"v1\"", 100}));

    ResumableResult r = httpGetResumable(server.url(), part, kNoProgress);
    CHECK(r.ok);
    CHECK(r.restarted);
    CHECK(readFile(part) == std::string(100, 'c'));
    CHECK_FALSE(exists(part + ".resume"));
}

TEST_CASE("200 whose body is as long as the offset is still a restart") {
    LoopbackServer server([](const std::string&) {
        return response("200 OK", "ETag: \"v2\"\r\n", std::string(40, 'd'));
    });
    std::string part = partIn("samelen.part");
    writeFile(part, std::string(40, 'a'));
    REQUIRE(emu::saveResume(part, {server.url(), "\"v1\"", 100}));

    ResumableResult r = httpGetResumable(server.url(), part, kNoProgress);
    CHECK(r.ok);
    CHECK(r.restarted);
    CHECK(readFile(part) == std::string(40, 'd'));
}

TEST_CASE("416 without a body fails and removes the part and control file") {
    LoopbackServer server(
        [](const std::string&) { return response("416 Range Not Satisfiable", "", ""); });
    std::string part = partIn("r416a.part");
    writeFile(part, std::string(40, 'a'));
    REQUIRE(emu::saveResume(part, {server.url(), "\"v1\"", 100}));

    ResumableResult r = httpGetResumable(server.url(), part, kNoProgress);
    CHECK_FALSE(r.ok);
    CHECK(r.status == 416);
    CHECK_FALSE(exists(part));
    CHECK_FALSE(exists(part + ".resume"));
}

TEST_CASE("416 with a body fails and removes the part and control file") {
    LoopbackServer server([](const std::string&) {
        return response("416 Range Not Satisfiable", "", "range not satisfiable");
    });
    std::string part = partIn("r416b.part");
    writeFile(part, std::string(40, 'a'));
    REQUIRE(emu::saveResume(part, {server.url(), "\"v1\"", 100}));

    ResumableResult r = httpGetResumable(server.url(), part, kNoProgress);
    CHECK_FALSE(r.ok);
    CHECK(r.status == 416);
    CHECK_FALSE(exists(part));
    CHECK_FALSE(exists(part + ".resume"));
}

TEST_CASE("a connection cut mid-body keeps the part and control file") {
    LoopbackServer server([](const std::string&) {
        return response("200 OK", "ETag: \"v1\"\r\n", std::string(200, 'a'), 1000);
    });
    std::string part = partIn("cut.part");
    ResumableResult r = httpGetResumable(server.url(), part, kNoProgress);
    CHECK_FALSE(r.ok);
    CHECK_FALSE(r.writeFailed);
    CHECK(fs::file_size(part) == 200);
    auto saved = emu::loadResume(part);
    REQUIRE(saved.has_value());
    CHECK(saved->url == server.url());
    CHECK(saved->validator == "\"v1\"");
    CHECK(saved->total == 1000);
}

TEST_CASE("connection refused keeps an existing part and control file") {
    std::string url;
    {
        LoopbackServer gone([](const std::string&) { return std::string(); });
        url = gone.url();
    }
    std::string part = partIn("refused.part");
    writeFile(part, std::string(40, 'a'));
    REQUIRE(emu::saveResume(part, {url, "\"v1\"", 100}));

    ResumableResult r = httpGetResumable(url, part, kNoProgress);
    CHECK_FALSE(r.ok);
    CHECK(r.status == 0);
    CHECK(fs::file_size(part) == 40);
    CHECK(emu::loadResume(part).has_value());
}

TEST_CASE("a cut 206 without validators keeps the control state") {
    LoopbackServer server([](const std::string&) {
        return response("206 Partial Content", "Content-Range: bytes 40-99/100\r\n",
                        std::string(20, 'b'), 60);
    });
    std::string part = partIn("novalidator.part");
    writeFile(part, std::string(40, 'a'));
    REQUIRE(emu::saveResume(part, {server.url(), "\"v1\"", 100}));

    ResumableResult r = httpGetResumable(server.url(), part, kNoProgress);
    CHECK_FALSE(r.ok);
    CHECK(fs::file_size(part) == 60);
    auto saved = emu::loadResume(part);
    REQUIRE(saved.has_value());
    CHECK(saved->validator == "\"v1\"");
    CHECK(saved->total == 100);
}

TEST_CASE("a part that already has total bytes makes no request") {
    LoopbackServer server([](const std::string&) {
        return response("416 Range Not Satisfiable", "", "");
    });
    std::string part = partIn("complete.part");
    writeFile(part, std::string(100, 'a'));
    REQUIRE(emu::saveResume(part, {server.url(), "\"v1\"", 100}));

    ResumableResult r = httpGetResumable(server.url(), part, kNoProgress);
    CHECK(r.ok);
    CHECK(server.requestCount() == 0);
    CHECK(readFile(part) == std::string(100, 'a'));
}

TEST_CASE("a 3xx without Location is not a success") {
    LoopbackServer server(
        [](const std::string&) { return response("300 Multiple Choices", "", "choose"); });
    std::string part = partIn("r300.part");
    ResumableResult r = httpGetResumable(server.url(), part, kNoProgress);
    CHECK_FALSE(r.ok);
    CHECK(r.status == 300);
}
