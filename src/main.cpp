#include "Config.hpp"
#include "Logger.hpp"
#include "FMTransmitterManager.hpp"
#include "AudioInjector.hpp"
#include "HttpServer.hpp"

#include <csignal>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unistd.h>

static std::atomic<bool> g_shutdown{ false };

static void signalHandler(int)
{
    g_shutdown = true;
}

static void printUsage(const char* prog)
{
    std::cout << "Usage: " << prog << " [-c /path/to/config.conf]\n"
        << "  -c  Config file path (default: /etc/hnxfmradio.conf)\n"
        << "  -h  Show this help\n";
}

int main(int argc, char* argv[])
{
    std::string configPath = "/etc/hnxfmradio.conf";

    int opt;
    while ((opt = getopt(argc, argv, "c:h")) != -1)
    {
        switch (opt)
        {
        case 'c': configPath = optarg; break;
        case 'h': printUsage(argv[0]); return 0;
        default:  printUsage(argv[0]); return 1;
        }
    }

    // Signal handling
    signal(SIGTERM, signalHandler);
    signal(SIGINT, signalHandler);
    signal(SIGPIPE, SIG_IGN); // Ignore broken pipe from child processes

    // ---------- Config ----------
    auto& cfg = Config::instance();
    if (!cfg.load(configPath))
    {
        std::cerr << "Failed to load/create config at " << configPath << "\n";
        return 1;
    }

    // ---------- Logger ----------
    Logger::enableSyslog(true);
    Logger::info("hnxfmradiod starting (config: " + configPath + ")");

    auto rc = cfg.get();

    // ---------- FM Transmitter ----------
    FMTransmitterManager fm;
    if (!fm.start(rc.fm_transmitter_path,
                  rc.arecord_path,
                  rc.loopback_device,
                  rc.frequency,
                  rc.sample_rate,
                  rc.channels))
    {
        Logger::error("Failed to start FMTransmitterManager — aborting");
        return 1;
    }

    // ---------- Audio Injector ----------
    AudioInjector injector;
    if (!injector.start(rc.ffmpeg_path, rc.audio_port,
                        rc.loopback_device, rc.sample_rate, rc.channels))
    {
        Logger::error("Failed to start AudioInjector — aborting");
        fm.stop();
        return 1;
    }

    // ---------- HTTP Server ----------
    HttpServer http(cfg, fm);
    if (!http.start(rc.http_port))
    {
        Logger::error("Failed to start HttpServer — aborting");
        injector.stop();
        fm.stop();
        return 1;
    }

    Logger::info("hnxfmradiod running — FM " + std::to_string(rc.frequency) +
                 " MHz | HTTP :" + std::to_string(rc.http_port) +
                 " | Audio :" + std::to_string(rc.audio_port));

    // ---------- Main loop: wait for shutdown signal ----------
    while (!g_shutdown)
    {
        pause();
    }

    // ---------- Graceful shutdown (reverse order) ----------
    Logger::info("Shutting down…");
    http.stop();
    injector.stop();
    fm.stop();
    Logger::info("hnxfmradiod stopped");
    return 0;
}
