#pragma once
#include <string>
#include <mutex>
#include <atomic>

class StreamPlayer {
public:
    StreamPlayer() = default;
    ~StreamPlayer();

    // Play a URL or file path through the ALSA loopback device
    bool play(const std::string& source,
              const std::string& ffmpegPath,
              const std::string& loopbackDevice,
              int sampleRate,
              int channels);

    void stop();

    bool        isPlaying()     const { return playing_; }
    std::string currentSource() const;

private:
    void killProcess();

    mutable std::mutex mutex_;
    std::atomic<bool>  playing_{false};
    std::string        source_;
    pid_t              ffmpegPid_{-1};
    pid_t              aplayPid_{-1};
    int                pipefd_[2]{-1, -1};
};
