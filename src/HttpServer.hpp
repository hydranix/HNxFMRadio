#pragma once
#include <atomic>
#include <thread>
#include <functional>
#include <string>

class FMTransmitterManager;
class StreamPlayer;
class Config;

class HttpServer {
public:
    HttpServer(Config& config, FMTransmitterManager& fm, StreamPlayer& player)
        : config_(config), fm_(fm), player_(player) {}
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
    std::string handlePlay(const std::string& body);
    std::string handleStop();
    std::string handleStatus();
    std::string handleBrowse(const std::string& path);

    static std::string respond(int status,
                               const std::string& contentType,
                               const std::string& body);
    static std::string urlDecode(const std::string& str);
    static std::string extractQueryParam(const std::string& path,
                                         const std::string& key);

    Config&               config_;
    FMTransmitterManager& fm_;
    StreamPlayer&         player_;
    int                   serverFd_ = -1;
    std::atomic<bool>     running_{false};
    std::thread           acceptThread_;
};
