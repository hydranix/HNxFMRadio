#include "FMTransmitterManager.hpp"
#include "Logger.hpp"

#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <cstring>
#include <sstream>
#include <chrono>

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

FMTransmitterManager::~FMTransmitterManager()
{
    stop();
}

bool FMTransmitterManager::start(const std::string& fmPath,
                                  const std::string& arecordPath,
                                  const std::string& loopbackDevice,
                                  double frequency,
                                  int    sampleRate,
                                  int    channels)
{
    fmPath_ = fmPath;
    arecordPath_ = arecordPath;
    loopbackDevice_ = loopbackDevice;
    frequency_ = frequency;
    sampleRate_ = sampleRate;
    channels_ = channels;

    running_ = true;
    launchPipeline();

    timerThread_ = std::thread(&FMTransmitterManager::restartTimerThread, this);
    watchThread_ = std::thread(&FMTransmitterManager::watcherThread, this);
    return true;
}

void FMTransmitterManager::stop()
{
    running_ = false;
    cv_.notify_all();

    if (timerThread_.joinable()) timerThread_.join();
    if (watchThread_.joinable()) watchThread_.join();

    killPipeline();
}

void FMTransmitterManager::restart(double frequency)
{
    std::lock_guard<std::mutex> lk(mutex_);
    frequency_ = frequency;
    killPipeline();
    launchPipeline();
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void FMTransmitterManager::launchPipeline()
{
// Create a pipe: arecord writes to pipefd_[1], fm_transmitter reads from pipefd_[0]
    if (pipe(pipefd_) < 0)
    {
        Logger::error("pipe() failed: " + std::string(strerror(errno)));
        return;
    }

    // --- Fork arecord ---
    arecordPid_ = fork();
    if (arecordPid_ < 0)
    {
        Logger::error("fork(arecord) failed: " + std::string(strerror(errno)));
        return;
    }
    if (arecordPid_ == 0)
    {
// Child: arecord
// stdout → pipe write end
        dup2(pipefd_[1], STDOUT_FILENO);
        close(pipefd_[0]);
        close(pipefd_[1]);

        std::string captureDevice = loopbackDevice_ + ",0"; // capture side
        std::string srStr = std::to_string(sampleRate_);
        std::string chStr = std::to_string(channels_);

        const char* argv[] = {
            arecordPath_.c_str(),
            "-D", captureDevice.c_str(),
            "-f", "S16_LE",
            "-r", srStr.c_str(),
            "-c", chStr.c_str(),
            nullptr
        };
        execvp(arecordPath_.c_str(), const_cast<char* const*>(argv));
        _exit(127);
    }

    // --- Fork fm_transmitter ---
    fmPid_ = fork();
    if (fmPid_ < 0)
    {
        Logger::error("fork(fm_transmitter) failed: " + std::string(strerror(errno)));
        return;
    }
    if (fmPid_ == 0)
    {
// Child: fm_transmitter reads from pipe read end
        dup2(pipefd_[0], STDIN_FILENO);
        close(pipefd_[0]);
        close(pipefd_[1]);

        std::string freqStr = std::to_string(frequency_);
        // Trim trailing zeros from frequency string for cleanliness
        auto dot = freqStr.find('.');
        if (dot != std::string::npos)
        {
            freqStr.erase(freqStr.find_last_not_of('0') + 1);
            if (freqStr.back() == '.') freqStr.pop_back();
        }

        const char* argv[] = {
            fmPath_.c_str(),
            "-f", freqStr.c_str(),
            "-",         // read from stdin
            nullptr
        };
        execvp(fmPath_.c_str(), const_cast<char* const*>(argv));
        _exit(127);
    }

    // Parent: close both pipe ends (children hold their copies)
    close(pipefd_[0]); pipefd_[0] = -1;
    close(pipefd_[1]); pipefd_[1] = -1;

    Logger::info("FM pipeline started (arecord PID=" + std::to_string(arecordPid_) +
                 ", fm_transmitter PID=" + std::to_string(fmPid_) +
                 ", freq=" + std::to_string(frequency_) + " MHz)");
}

void FMTransmitterManager::killPipeline()
{
    auto killAndWait = [](pid_t& pid)
        {
            if (pid > 0)
            {
                kill(pid, SIGTERM);
                // Give it 2 seconds then SIGKILL
                std::this_thread::sleep_for(std::chrono::seconds(2));
                if (waitpid(pid, nullptr, WNOHANG) == 0)
                    kill(pid, SIGKILL);
                waitpid(pid, nullptr, 0);
                pid = -1;
            }
        };
    killAndWait(fmPid_);
    killAndWait(arecordPid_);

    for (int& fd : pipefd_)
    {
        if (fd >= 0) { close(fd); fd = -1; }
    }
    Logger::info("FM pipeline stopped");
}

void FMTransmitterManager::restartTimerThread()
{
    while (running_)
    {
// Wait 30 minutes (or until stopped)
        std::unique_lock<std::mutex> lk(mutex_);
        cv_.wait_for(lk, std::chrono::minutes(30), [this] { return !running_; });
        if (!running_) break;

        Logger::info("30-minute restart: cycling fm_transmitter pipeline");
        killPipeline();
        launchPipeline();
    }
}

void FMTransmitterManager::watcherThread()
{
    while (running_)
    {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        if (!running_) break;

        bool needRestart = false;
        if (fmPid_ > 0)
        {
            int status = 0;
            pid_t ret = waitpid(fmPid_, &status, WNOHANG);
            if (ret == fmPid_)
            {
                Logger::warn("fm_transmitter exited unexpectedly, restarting pipeline");
                fmPid_ = -1;
                needRestart = true;
            }
        }
        if (arecordPid_ > 0)
        {
            int status = 0;
            pid_t ret = waitpid(arecordPid_, &status, WNOHANG);
            if (ret == arecordPid_)
            {
                Logger::warn("arecord exited unexpectedly, restarting pipeline");
                arecordPid_ = -1;
                needRestart = true;
            }
        }
        if (needRestart && running_)
        {
            std::lock_guard<std::mutex> lk(mutex_);
            killPipeline();
            launchPipeline();
        }
    }
}
