#pragma once
#include <atomic>
#include <thread>
#include <functional>
#include <string>

class FMTransmitterManager;
class Config;

class HttpServer {
public:
    HttpServer(Config& config, FMTransmitterManager& fm)
        : config_(config), fm_(fm) {}
    ~HttpServer();

    bool start(int port);
    void stop();

    bool isRunning() const { return running_; }

private:
    void acceptLoop();
    void handleClient(int fd);

    // Request handlers
    std::string handleGetConfig();
    std::string handlePostConfig(const std::string& body);

    static std::string respond(int status,
                               const std::string& contentType,
                               const std::string& body);

    Config&               config_;
    FMTransmitterManager& fm_;
    int                   serverFd_ = -1;
    std::atomic<bool>     running_{false};
    std::thread           acceptThread_;
};
