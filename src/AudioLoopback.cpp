#include "AudioLoopback.hpp"
#include "Logger.hpp"

#include <cstring>
#include <cstdlib>
#include <chrono>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool modprobeLoopback() {
    int ret = std::system("modprobe snd-aloop 2>/dev/null");
    return ret == 0;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

AudioLoopback::~AudioLoopback() {
    stop();
}

bool AudioLoopback::start(const std::string& device, int sampleRate, int channels) {
    sampleRate_ = sampleRate;
    channels_   = channels;

    if (!modprobeLoopback())
        Logger::warn("modprobe snd-aloop failed — module may already be loaded");

    // Open PCM for playback on the loopback device
    std::string playback = device + ",0";  // e.g. hw:Loopback,0
    int err = snd_pcm_open(&pcm_, playback.c_str(), SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        Logger::error("snd_pcm_open(" + playback + "): " + snd_strerror(err));
        return false;
    }

    snd_pcm_hw_params_t* params = nullptr;
    snd_pcm_hw_params_alloca(&params);
    snd_pcm_hw_params_any(pcm_, params);
    snd_pcm_hw_params_set_access(pcm_, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(pcm_, params, SND_PCM_FORMAT_S16_LE);
    snd_pcm_hw_params_set_channels(pcm_, params, (unsigned)channels_);

    unsigned int rate = (unsigned)sampleRate_;
    snd_pcm_hw_params_set_rate_near(pcm_, params, &rate, nullptr);

    snd_pcm_uframes_t frames = 512;
    snd_pcm_hw_params_set_period_size_near(pcm_, params, &frames, nullptr);

    err = snd_pcm_hw_params(pcm_, params);
    if (err < 0) {
        Logger::error("snd_pcm_hw_params: " + std::string(snd_strerror(err)));
        snd_pcm_close(pcm_);
        pcm_ = nullptr;
        return false;
    }

    running_ = true;
    silenceThread_ = std::thread(&AudioLoopback::silenceThread, this);
    Logger::info("AudioLoopback started on " + playback);
    return true;
}

void AudioLoopback::stop() {
    running_ = false;
    injecting_ = false;
    injectCV_.notify_all();

    if (silenceThread_.joinable())
        silenceThread_.join();

    if (pcm_) {
        snd_pcm_drain(pcm_);
        snd_pcm_close(pcm_);
        pcm_ = nullptr;
    }
}

void AudioLoopback::injectAudio(const int16_t* frames, size_t frameCount) {
    std::unique_lock<std::mutex> lk(injectMutex_);
    injecting_ = true;
    lk.unlock();

    writePCM(frames, frameCount);
}

void AudioLoopback::endInjection() {
    std::unique_lock<std::mutex> lk(injectMutex_);
    injecting_ = false;
    lk.unlock();
    injectCV_.notify_all();
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void AudioLoopback::silenceThread() {
    // One period worth of silence
    const snd_pcm_uframes_t periodFrames = 512;
    std::vector<int16_t> silence(periodFrames * (size_t)channels_, 0);

    while (running_) {
        {
            std::unique_lock<std::mutex> lk(injectMutex_);
            injectCV_.wait(lk, [this]{ return !injecting_ || !running_; });
        }
        if (!running_) break;
        writePCM(silence.data(), periodFrames);
    }
}

bool AudioLoopback::writePCM(const int16_t* frames, size_t frameCount) {
    if (!pcm_) return false;

    size_t written = 0;
    while (written < frameCount) {
        snd_pcm_sframes_t ret = snd_pcm_writei(pcm_, frames + written * channels_,
                                                frameCount - written);
        if (ret == -EPIPE) {
            snd_pcm_prepare(pcm_);
        } else if (ret < 0) {
            ret = snd_pcm_recover(pcm_, (int)ret, 0);
            if (ret < 0) {
                Logger::error("snd_pcm_writei recover: " + std::string(snd_strerror((int)ret)));
                return false;
            }
        } else {
            written += (size_t)ret;
        }
    }
    return true;
}
