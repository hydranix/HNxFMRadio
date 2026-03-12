#include "AudioInjector.hpp"
#include "Logger.hpp"

// ---------------------------------------------------------------------------
// AudioInjector is now handled by a separate systemd service (hnxfmradio-transcode.service)
// which runs: socat TCP-LISTEN:21100,reuseaddr,fork EXEC:"/usr/local/bin/TranscodeToLoopback"
//
// This class is kept as a stub for backwards compatibility.
// ---------------------------------------------------------------------------

AudioInjector::~AudioInjector()
{
    stop();
}

bool AudioInjector::start(const std::string& ffmpegPath, int port,
                          const std::string& loopbackDevice, int sampleRate, int channels)
{
    (void)ffmpegPath;
    (void)port;
    (void)loopbackDevice;
    (void)sampleRate;
    (void)channels;

    running_ = true;
    Logger::info("AudioInjector: transcoding is now handled by hnxfmradio-transcode.service");
    return true;
}

void AudioInjector::stop()
{
    running_ = false;
}

