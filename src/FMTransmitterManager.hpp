#pragma once
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

class FMTransmitterManager {
public:
    FMTransmitterManager() = default;
    ~FMTransmitterManager();

    // Start the arecord|fm_transmitter pipeline
    bool start(const std::string& fmPath,
               const std::string& arecordPath,
               const std::string& loopbackDevice,
               double frequency,
               int    gain,
               int    sampleRate,
               int    channels);
    void stop();

    // Restart the pipeline with new frequency/gain (e.g. after config change)
    void restart(double frequency, int gain);

    bool isRunning() const { return running_; }

private:
    void launchPipeline();
    void killPipeline();
    void restartTimerThread();
    void watcherThread();

    std::string fmPath_;
    std::string arecordPath_;
    std::string loopbackDevice_;
    double      frequency_  = 100.6;
    int         gain_       = 0;
    int         sampleRate_ = 22050;
    int         channels_   = 1;

    // PIDs of spawned processes
    pid_t arecordPid_ = -1;
    pid_t fmPid_      = -1;
    int   pipefd_[2]  = {-1, -1}; // arecord→fm_transmitter pipe

    std::atomic<bool> running_{false};
    std::mutex        mutex_;
    std::condition_variable cv_;

    std::thread timerThread_;
    std::thread watchThread_;
};
