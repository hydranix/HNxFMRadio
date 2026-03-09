#include "AudioInjector.hpp"
#include "Logger.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <cstring>
#include <vector>
#include <thread>

static constexpr size_t kReadBufSize = 65536;     // bytes read from TCP per chunk

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

AudioInjector::~AudioInjector()
{
    stop();
}

bool AudioInjector::start(const std::string& ffmpegPath, int port,
                          const std::string& loopbackDevice, int sampleRate, int channels)
{
    ffmpegPath_ = ffmpegPath;
    loopbackDevice_ = loopbackDevice + ",0";
    sampleRate_ = sampleRate;
    channels_ = channels;

    serverFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd_ < 0)
    {
        Logger::error("AudioInjector: socket(): " + std::string(strerror(errno)));
        return false;
    }

    int opt = 1;
    setsockopt(serverFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);

    if (bind(serverFd_, (sockaddr*)&addr, sizeof(addr)) < 0)
    {
        Logger::error("AudioInjector: bind(" + std::to_string(port) + "): " +
                      std::string(strerror(errno)));
        close(serverFd_); serverFd_ = -1;
        return false;
    }

    listen(serverFd_, 1);
    running_ = true;
    acceptThread_ = std::thread(&AudioInjector::acceptLoop, this);
    Logger::info("AudioInjector listening on port " + std::to_string(port));
    return true;
}

void AudioInjector::stop()
{
    running_ = false;
    if (serverFd_ >= 0)
    {
        shutdown(serverFd_, SHUT_RDWR);
        close(serverFd_);
        serverFd_ = -1;
    }
    if (acceptThread_.joinable()) acceptThread_.join();
}

// ---------------------------------------------------------------------------
// Accept loop — runs one client at a time
// ---------------------------------------------------------------------------

void AudioInjector::acceptLoop()
{
    while (running_)
    {
        int clientFd = accept(serverFd_, nullptr, nullptr);
        if (clientFd < 0)
        {
            if (running_) Logger::warn("AudioInjector: accept(): " + std::string(strerror(errno)));
            break;
        }
        Logger::info("AudioInjector: client connected");
        handleClient(clientFd);
        close(clientFd);
        Logger::info("AudioInjector: client disconnected");
    }
}

// ---------------------------------------------------------------------------
// Handle one client: wire TCP -> ffmpeg -> aplay
// ---------------------------------------------------------------------------

void AudioInjector::handleClient(int clientFd)
{
    // Pipe: TCP data -> ffmpeg stdin
    int tcpToFfmpeg[2];
    // Pipe: ffmpeg stdout (s16le PCM) -> aplay stdin
    int ffmpegToAplay[2];

    if (pipe(tcpToFfmpeg) < 0 || pipe(ffmpegToAplay) < 0)
    {
        Logger::error("AudioInjector: pipe(): " + std::string(strerror(errno)));
        return;
    }

    // Build ffmpeg argv
    std::string srStr = std::to_string(sampleRate_);
    std::string chStr = std::to_string(channels_);
    const char* ffmpegArgv[] = {
        ffmpegPath_.c_str(),
        "-hide_banner", "-loglevel", "error",
        "-i", "pipe:0",
        "-ar", srStr.c_str(),
        "-ac", chStr.c_str(),
        "-f", "s16le",
        "pipe:1",
        nullptr
    };

    // --- Fork ffmpeg ---
    pid_t ffmpegPid = fork();
    if (ffmpegPid < 0)
    {
        Logger::error("AudioInjector: fork(ffmpeg): " + std::string(strerror(errno)));
        close(tcpToFfmpeg[0]); close(tcpToFfmpeg[1]);
        close(ffmpegToAplay[0]); close(ffmpegToAplay[1]);
        return;
    }
    if (ffmpegPid == 0)
    {
        dup2(tcpToFfmpeg[0], STDIN_FILENO);
        dup2(ffmpegToAplay[1], STDOUT_FILENO);
        close(tcpToFfmpeg[0]); close(tcpToFfmpeg[1]);
        close(ffmpegToAplay[0]); close(ffmpegToAplay[1]);
        execvp(ffmpegPath_.c_str(), const_cast<char* const*>(ffmpegArgv));
        _exit(127);
    }

    // --- Fork aplay ---
    pid_t aplayPid = fork();
    if (aplayPid < 0)
    {
        Logger::error("AudioInjector: fork(aplay): " + std::string(strerror(errno)));
        close(tcpToFfmpeg[0]); close(tcpToFfmpeg[1]);
        close(ffmpegToAplay[0]); close(ffmpegToAplay[1]);
        kill(ffmpegPid, SIGTERM);
        waitpid(ffmpegPid, nullptr, 0);
        return;
    }
    if (aplayPid == 0)
    {
        dup2(ffmpegToAplay[0], STDIN_FILENO);
        close(tcpToFfmpeg[0]); close(tcpToFfmpeg[1]);
        close(ffmpegToAplay[0]); close(ffmpegToAplay[1]);
        execlp("aplay", "aplay",
               "-D", loopbackDevice_.c_str(),
               "-f", "S16_LE",
               "-r", srStr.c_str(),
               "-c", chStr.c_str(),
               "-t", "raw",
               "-", (char*)nullptr);
        _exit(127);
    }

    // Parent: close child-side pipe ends
    close(tcpToFfmpeg[0]);
    close(ffmpegToAplay[0]);
    close(ffmpegToAplay[1]);

    // Forward TCP -> ffmpeg stdin
    {
        std::vector<char> buf(kReadBufSize);
        while (running_)
        {
            ssize_t n = recv(clientFd, buf.data(), buf.size(), 0);
            if (n <= 0) break;
            ssize_t written = 0;
            while (written < n)
            {
                ssize_t w = write(tcpToFfmpeg[1], buf.data() + written, n - written);
                if (w <= 0) goto done;
                written += w;
            }
        }
    done:;
    }
    close(tcpToFfmpeg[1]);

    // Wait for children to finish
    waitpid(ffmpegPid, nullptr, 0);
    waitpid(aplayPid, nullptr, 0);
}
