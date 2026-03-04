# HNxFMRadio

A lightweight FM radio transmitter daemon for the **Raspberry Pi Zero W 2**. It turns your Pi into an FM broadcast station by chaining an ALSA loopback device, `arecord`, and the [`fm_transmitter`](https://github.com/markondej/fm_transmitter) tool, all managed by a single C++17 daemon with a built-in web configuration UI.

## How It Works

```
Audio source --> ALSA loopback (snd-aloop) --> arecord --> pipe --> fm_transmitter --> FM broadcast
```

External audio can also be injected over TCP (port 8081 by default). The daemon accepts any format that ffmpeg can decode, transcodes it to raw PCM, and feeds it into the loopback device.

A built-in HTTP server (port 8080 by default) serves a single-page config UI where you can change the broadcast frequency, sample rate, channels, and port numbers on the fly.

## Features

- FM broadcast on any frequency between 87.5 and 108.0 MHz
- Built-in dark-themed web UI for live configuration
- TCP audio injection (send any audio format ffmpeg understands)
- Automatic pipeline restart every 30 minutes for stability
- Watchdog that restarts crashed child processes
- JSON REST API (`GET/POST /api/config`)
- systemd service for auto-start on boot
- INI-based config file with sane defaults

## Requirements

### Hardware

- Raspberry Pi Zero W 2 (or any Pi with GPIO/PWM access)
- A short wire (~20 cm) connected to **GPIO 4** as an antenna

### Software

- Linux with the `snd-aloop` kernel module available
- [fm_transmitter](https://github.com/markondej/fm_transmitter) built and installed
- ALSA development libraries (`libasound2-dev` / `alsa-lib`)
- `arecord` (from `alsa-utils`)
- `ffmpeg`
- CMake 3.20+
- A C++17 compiler (GCC 8+ or Clang 7+)

## Building

```bash
git clone https://github.com/hydranix/HNxFMRadio.git
cd HNxFMRadio
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

The binary is built at `build/hnxfmradiod`.

## Installation

### Quick Install (script)

An install script is provided that builds from source and installs everything:

```bash
sudo ./install.sh
```

This will:

1. Install build dependencies (supports apt and pacman)
2. Build the project in Release mode
3. Install the binary to `/usr/local/bin/hnxfmradiod`
4. Install the systemd service to `/etc/systemd/system/`
5. Copy the example config to `/etc/hnxfmradio.conf` (only if one doesn't already exist)
6. Enable and start the service

### Manual Install

```bash
cd build
sudo make install
```

Then copy the example config:

```bash
sudo cp /etc/hnxfmradio.conf.example /etc/hnxfmradio.conf
```

Enable and start the service:

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now hnxfmradiod
```

## Configuration

Edit `/etc/hnxfmradio.conf`:

```ini
[radio]
frequency=100.6

[audio]
sample_rate=22050
channels=1
http_port=8080
audio_port=8081

[paths]
fm_transmitter=/usr/local/bin/fm_transmitter
arecord=/usr/bin/arecord
ffmpeg=/usr/bin/ffmpeg
loopback_device=hw:Loopback
```

Or use the web UI at `http://<pi-ip>:8080`.

## Usage

### Injecting Audio

Send any audio file over TCP to the audio injection port:

```bash
# Stream an MP3 file
cat song.mp3 | nc <pi-ip> 8081

# Stream from a URL via ffmpeg
ffmpeg -i http://example.com/stream.mp3 -f mp3 - | nc <pi-ip> 8081
```

### REST API

```bash
# Get current config
curl http://<pi-ip>:8080/api/config

# Change frequency
curl -X POST http://<pi-ip>:8080/api/config \
  -H 'Content-Type: application/json' \
  -d '{"frequency": 99.5}'
```

### Service Management

```bash
sudo systemctl status hnxfmradiod    # Check status
sudo systemctl restart hnxfmradiod   # Restart
journalctl -u hnxfmradiod -f         # Follow logs
```

## Notes

- `fm_transmitter` requires access to `/dev/mem` for GPIO/PWM control. The service runs as root by default. Alternatively, grant `CAP_SYS_RAWIO` to the binary.
- Broadcasting on FM frequencies may be subject to local regulations. Check your local laws before transmitting.
- The default frequency in the config file is 100.6 MHz. The daemon defaults to 106.1 MHz if no config file is found.

## License

MIT
