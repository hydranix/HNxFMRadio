#pragma once
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <string>
#include <alsa/asoundlib.h>

class AudioLoopback {
public:
    AudioLoopback() = default;
    ~AudioLoopback();

    // Initialize loopback module and open ALSA PCM handle
    bool start(const std::string& device, int sampleRate, int channels);
    void stop();

    // Feed raw s16le PCM frames into the loopback (called by AudioInjector).
    // Blocks until all frames are written. Thread-safe.
    void injectAudio(const int16_t* frames, size_t frameCount);

    // Signal that injection is complete so silence resumes
    void endInjection();

    bool isRunning() const { return running_; }

private:
    void silenceThread();
    bool writePCM(const int16_t* frames, size_t frameCount);

    snd_pcm_t*  pcm_          = nullptr;
    int         sampleRate_   = 22050;
    int         channels_     = 1;
    std::atomic<bool> running_{false};
    std::atomic<bool> injecting_{false};

    std::thread           silenceThread_;
    std::mutex            injectMutex_;
    std::condition_variable injectCV_;
};
