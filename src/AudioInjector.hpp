#pragma once
#include <atomic>
#include <thread>
#include <string>

class AudioLoopback;

class AudioInjector {
public:
    explicit AudioInjector(AudioLoopback& loopback) : loopback_(loopback) {}
    ~AudioInjector();

    // Start listening for incoming audio connections
    bool start(const std::string& ffmpegPath, int port, int sampleRate, int channels);
    void stop();

    bool isRunning() const { return running_; }

private:
    void acceptLoop();
    void handleClient(int clientFd);
    void runFfmpegPipeline(int clientFd, const std::string& ffmpegPath,
                           int sampleRate, int channels);

    AudioLoopback& loopback_;
    std::string    ffmpegPath_;
    int            sampleRate_ = 22050;
    int            channels_   = 1;
    int            serverFd_   = -1;
    std::atomic<bool> running_{false};
    std::thread       acceptThread_;
};
