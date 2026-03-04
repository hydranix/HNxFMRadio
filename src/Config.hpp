#pragma once
#include <string>
#include <shared_mutex>

struct RadioConfig {
    double      frequency   = 100.6;
    int         gain        = 0;
    int         sample_rate = 22050;
    int         channels    = 1;
    int         http_port   = 8080;
    int         audio_port  = 8081;
    std::string fm_transmitter_path = "/usr/local/bin/fm_transmitter";
    std::string arecord_path        = "/usr/bin/arecord";
    std::string ffmpeg_path         = "/usr/bin/ffmpeg";
    std::string loopback_device     = "hw:Loopback";
};

class Config {
public:
    static Config& instance();

    // Load from file; creates default file if missing
    bool load(const std::string& path = "/etc/hnxfmradio.conf");

    // Atomically write current values back to the config file
    bool save();

    // Thread-safe getters / setters
    RadioConfig get() const;
    void        set(const RadioConfig& cfg);

    std::string path() const { return path_; }

private:
    Config() = default;

    void        parse(const std::string& content);
    std::string serialize() const;

    mutable std::shared_mutex mutex_;
    RadioConfig               cfg_;
    std::string               path_;
};
