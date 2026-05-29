#include "StreamPlayer.hpp"
#include "Logger.hpp"

#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <cstring>
#include <chrono>
#include <thread>

StreamPlayer::~StreamPlayer() { stop(); }

bool StreamPlayer::play(const std::string& source,
                        const std::string& ffmpegPath,
                        const std::string& loopbackDevice,
                        int sampleRate,
                        int channels)
{
    std::lock_guard<std::mutex> lk(mutex_);

    // Stop any existing playback
    killProcess();

    // Create pipe: ffmpeg stdout → aplay stdin
    if (pipe(pipefd_) < 0)
    {
        Logger::error("StreamPlayer: pipe() failed: " + std::string(strerror(errno)));
        return false;
    }

    // Playback side of loopback (paired with capture side ,0)
    std::string playbackDevice = loopbackDevice + ",1";
    std::string srStr = std::to_string(sampleRate);
    std::string chStr = std::to_string(channels);

    // Fork ffmpeg
    ffmpegPid_ = fork();
    if (ffmpegPid_ < 0)
    {
        Logger::error("StreamPlayer: fork(ffmpeg) failed: " + std::string(strerror(errno)));
        close(pipefd_[0]); close(pipefd_[1]);
        pipefd_[0] = pipefd_[1] = -1;
        return false;
    }
    if (ffmpegPid_ == 0)
    {
        // Child: ffmpeg → pipe write end
        dup2(pipefd_[1], STDOUT_FILENO);
        close(pipefd_[0]);
        close(pipefd_[1]);

        // Redirect stderr to /dev/null to avoid noisy ffmpeg output
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) { dup2(devnull, STDERR_FILENO); close(devnull); }

        const char* argv[] = {
            ffmpegPath.c_str(),
            "-reconnect", "1",
            "-reconnect_streamed", "1",
            "-reconnect_delay_max", "5",
            "-i", source.c_str(),
            "-f", "s16le",
            "-ar", srStr.c_str(),
            "-ac", chStr.c_str(),
            "pipe:1",
            nullptr
        };
        execvp(ffmpegPath.c_str(), const_cast<char* const*>(argv));
        _exit(127);
    }

    // Fork aplay
    aplayPid_ = fork();
    if (aplayPid_ < 0)
    {
        Logger::error("StreamPlayer: fork(aplay) failed: " + std::string(strerror(errno)));
        kill(ffmpegPid_, SIGKILL);
        waitpid(ffmpegPid_, nullptr, 0);
        ffmpegPid_ = -1;
        close(pipefd_[0]); close(pipefd_[1]);
        pipefd_[0] = pipefd_[1] = -1;
        return false;
    }
    if (aplayPid_ == 0)
    {
        // Child: aplay reads from pipe read end
        dup2(pipefd_[0], STDIN_FILENO);
        close(pipefd_[0]);
        close(pipefd_[1]);

        const char* argv[] = {
            "aplay",
            "-D", playbackDevice.c_str(),
            "-f", "S16_LE",
            "-r", srStr.c_str(),
            "-c", chStr.c_str(),
            "-t", "raw",
            nullptr
        };
        execvp("aplay", const_cast<char* const*>(argv));
        _exit(127);
    }

    // Parent: close both pipe ends
    close(pipefd_[0]); pipefd_[0] = -1;
    close(pipefd_[1]); pipefd_[1] = -1;

    source_ = source;
    playing_ = true;

    Logger::info("StreamPlayer: playing " + source);
    return true;
}

void StreamPlayer::stop()
{
    std::lock_guard<std::mutex> lk(mutex_);
    killProcess();
}

std::string StreamPlayer::currentSource() const
{
    std::lock_guard<std::mutex> lk(mutex_);
    return playing_ ? source_ : "";
}

void StreamPlayer::killProcess()
{
    auto killAndWait = [](pid_t& pid)
    {
        if (pid > 0)
        {
            kill(pid, SIGTERM);
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (waitpid(pid, nullptr, WNOHANG) == 0)
                kill(pid, SIGKILL);
            waitpid(pid, nullptr, 0);
            pid = -1;
        }
    };

    killAndWait(ffmpegPid_);
    killAndWait(aplayPid_);

    for (int& fd : pipefd_)
    {
        if (fd >= 0) { close(fd); fd = -1; }
    }

    if (playing_)
    {
        Logger::info("StreamPlayer: stopped");
        playing_ = false;
        source_.clear();
    }
}
