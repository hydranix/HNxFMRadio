#include "AudioInjector.hpp"
#include "AudioLoopback.hpp"
#include "Logger.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <cstring>
#include <vector>
#include <thread>

static constexpr size_t kReadBufSize   = 65536;     // bytes read from TCP per chunk
static constexpr size_t kPCMFrameBuf   = 4096;      // PCM frames per loopback write

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

AudioInjector::~AudioInjector() {
    stop();
}

bool AudioInjector::start(const std::string& ffmpegPath, int port, int sampleRate, int channels) {
    ffmpegPath_  = ffmpegPath;
    sampleRate_  = sampleRate;
    channels_    = channels;

    serverFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd_ < 0) {
        Logger::error("AudioInjector: socket(): " + std::string(strerror(errno)));
        return false;
    }

    int opt = 1;
    setsockopt(serverFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((uint16_t)port);

    if (bind(serverFd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
        Logger::error("AudioInjector: bind(" + std::to_string(port) + "): " +
                      std::string(strerror(errno)));
        close(serverFd_); serverFd_ = -1;
        return false;
    }

    listen(serverFd_, 1); // accept one pending client
    running_     = true;
    acceptThread_ = std::thread(&AudioInjector::acceptLoop, this);
    Logger::info("AudioInjector listening on port " + std::to_string(port));
    return true;
}

void AudioInjector::stop() {
    running_ = false;
    if (serverFd_ >= 0) {
        shutdown(serverFd_, SHUT_RDWR);
        close(serverFd_);
        serverFd_ = -1;
    }
    if (acceptThread_.joinable()) acceptThread_.join();
}

// ---------------------------------------------------------------------------
// Accept loop — runs one client at a time
// ---------------------------------------------------------------------------

void AudioInjector::acceptLoop() {
    while (running_) {
        int clientFd = accept(serverFd_, nullptr, nullptr);
        if (clientFd < 0) {
            if (running_) Logger::warn("AudioInjector: accept(): " + std::string(strerror(errno)));
            break;
        }
        Logger::info("AudioInjector: client connected");
        handleClient(clientFd);
        close(clientFd);
        Logger::info("AudioInjector: client disconnected, resuming silence");
        loopback_.endInjection();
    }
}

// ---------------------------------------------------------------------------
// Handle one client: wire TCP → ffmpeg → AudioLoopback
// ---------------------------------------------------------------------------

void AudioInjector::handleClient(int clientFd) {
    // Pipes: tcpToFfmpeg[1] ← TCP data; tcpToFfmpeg[0] → ffmpeg stdin
    //        ffmpegToPcm[0]  ← ffmpeg stdout → injectAudio
    int tcpToFfmpeg[2], ffmpegToPcm[2];
    if (pipe(tcpToFfmpeg) < 0 || pipe(ffmpegToPcm) < 0) {
        Logger::error("AudioInjector: pipe(): " + std::string(strerror(errno)));
        return;
    }

    // Build ffmpeg argv
    std::string srStr = std::to_string(sampleRate_);
    std::string chStr = std::to_string(channels_);
    const char* argv[] = {
        ffmpegPath_.c_str(),
        "-hide_banner", "-loglevel", "error",
        "-i", "pipe:0",
        "-ar", srStr.c_str(),
        "-ac", chStr.c_str(),
        "-f", "s16le",
        "pipe:1",
        nullptr
    };

    pid_t ffmpegPid = fork();
    if (ffmpegPid < 0) {
        Logger::error("AudioInjector: fork(ffmpeg): " + std::string(strerror(errno)));
        close(tcpToFfmpeg[0]); close(tcpToFfmpeg[1]);
        close(ffmpegToPcm[0]); close(ffmpegToPcm[1]);
        return;
    }
    if (ffmpegPid == 0) {
        // ffmpeg child
        dup2(tcpToFfmpeg[0], STDIN_FILENO);
        dup2(ffmpegToPcm[1], STDOUT_FILENO);
        close(tcpToFfmpeg[0]); close(tcpToFfmpeg[1]);
        close(ffmpegToPcm[0]); close(ffmpegToPcm[1]);
        execvp(ffmpegPath_.c_str(), const_cast<char* const*>(argv));
        _exit(127);
    }

    // Parent: close child-side fds
    close(tcpToFfmpeg[0]);
    close(ffmpegToPcm[1]);

    // Thread 1: forward TCP → ffmpeg stdin
    std::thread sender([&]() {
        std::vector<char> buf(kReadBufSize);
        while (true) {
            ssize_t n = recv(clientFd, buf.data(), buf.size(), 0);
            if (n <= 0) break;
            ssize_t written = 0;
            while (written < n) {
                ssize_t w = write(tcpToFfmpeg[1], buf.data() + written, n - written);
                if (w <= 0) goto done;
                written += w;
            }
        }
        done:
        close(tcpToFfmpeg[1]);
    });

    // Thread 2 (this thread): read ffmpeg stdout → AudioLoopback
    {
        const size_t frameSamples = kPCMFrameBuf * (size_t)channels_;
        std::vector<int16_t> pcmBuf(frameSamples);
        const size_t bytesPerBuf = frameSamples * sizeof(int16_t);

        while (running_) {
            size_t total = 0;
            while (total < bytesPerBuf) {
                ssize_t n = read(ffmpegToPcm[0],
                                 reinterpret_cast<char*>(pcmBuf.data()) + total,
                                 bytesPerBuf - total);
                if (n <= 0) goto pcmDone;
                total += (size_t)n;
            }
            loopback_.injectAudio(pcmBuf.data(), kPCMFrameBuf);
        }
        pcmDone:;
    }
    close(ffmpegToPcm[0]);

    sender.join();
    waitpid(ffmpegPid, nullptr, 0);
}
