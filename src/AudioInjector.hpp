#pragma once
#include <atomic>
#include <thread>
#include <string>

class AudioInjector {
public:
    AudioInjector() = default;
    ~AudioInjector();

    // Start listening for incoming audio connections
    bool start(const std::string& ffmpegPath, int port,
               const std::string& loopbackDevice, int sampleRate, int channels);
    void stop();

    bool isRunning() const { return running_; }

private:
    void acceptLoop();
    void handleClient(int clientFd);

    std::string    ffmpegPath_;
    std::string    loopbackDevice_;
    int            sampleRate_ = 22050;
    int            channels_ = 1;
    int            serverFd_ = -1;
    std::atomic<bool> running_{ false };
    std::thread       acceptThread_;
};
