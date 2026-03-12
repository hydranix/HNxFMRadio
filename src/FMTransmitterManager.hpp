#pragma once
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <random>

class FMTransmitterManager {
public:
    FMTransmitterManager() = default;
    ~FMTransmitterManager();

    // Start the arecord|fm_transmitter pipeline
    bool start(const std::string& fmPath,
               const std::string& arecordPath,
               const std::string& loopbackDevice,
               double frequency,
               int    sampleRate,
               int    channels,
               int    restartBaseMs = 1800000,
               int    restartRandomnessMs = 0);
    void stop();

    // Restart the pipeline with new frequency (e.g. after config change)
    void restart(double frequency);

    bool isRunning() const { return running_; }

private:
    void launchPipeline();
    void killPipeline();
    void restartTimerThread();
    void watcherThread();

    std::string fmPath_;
    std::string arecordPath_;
    std::string loopbackDevice_;
    double      frequency_ = 100.6;
    int         sampleRate_ = 22050;
    int         channels_ = 1;
    int         restartBaseMs_ = 1800000;        // 30 minutes in milliseconds
    int         restartRandomnessMs_ = 0;         // additional random milliseconds

    // PIDs of spawned processes
    pid_t arecordPid_ = -1;
    pid_t fmPid_ = -1;
    int   pipefd_[2] = { -1, -1 }; // arecord→fm_transmitter pipe

    std::atomic<bool> running_{ false };
    std::mutex        mutex_;
    std::condition_variable cv_;

    std::thread timerThread_;
    std::thread watchThread_;
};
