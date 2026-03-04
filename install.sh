#!/usr/bin/env bash
set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$REPO_DIR/build"
CONFIG_SRC="$REPO_DIR/config/hnxfmradio.conf.example"
CONFIG_DST="/etc/hnxfmradio.conf"
SERVICE_NAME="hnxfmradiod"

info()  { printf '\033[1;34m[INFO]\033[0m  %s\n' "$*"; }
warn()  { printf '\033[1;33m[WARN]\033[0m  %s\n' "$*"; }
error() { printf '\033[1;31m[ERROR]\033[0m %s\n' "$*"; exit 1; }

if [[ $EUID -ne 0 ]]; then
    error "This script must be run as root (use sudo)."
fi

# ── Install build dependencies ──────────────────────────────────────────────

install_deps() {
    if command -v apt-get &>/dev/null; then
        info "Installing dependencies via apt..."
        apt-get update -qq
        apt-get install -y -qq cmake g++ libasound2-dev alsa-utils ffmpeg
    elif command -v pacman &>/dev/null; then
        info "Installing dependencies via pacman..."
        pacman -Sy --needed --noconfirm cmake gcc alsa-lib alsa-utils ffmpeg
    elif command -v dnf &>/dev/null; then
        info "Installing dependencies via dnf..."
        dnf install -y cmake gcc-c++ alsa-lib-devel alsa-utils ffmpeg
    else
        warn "Unrecognized package manager. Please install manually:"
        warn "  cmake, g++ (C++17), libasound2-dev/alsa-lib, alsa-utils, ffmpeg"
    fi
}

# ── Build ────────────────────────────────────────────────────────────────────

build() {
    info "Building in $BUILD_DIR..."
    mkdir -p "$BUILD_DIR"
    cmake -S "$REPO_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$BUILD_DIR" -j"$(nproc)"
}

# ── Install ──────────────────────────────────────────────────────────────────

install_files() {
    info "Installing via cmake --install..."
    cmake --install "$BUILD_DIR"

    if [[ ! -f "$CONFIG_DST" ]]; then
        info "Creating config at $CONFIG_DST from example..."
        cp "$CONFIG_SRC" "$CONFIG_DST"
    else
        warn "$CONFIG_DST already exists — not overwriting."
    fi
}

# ── Enable service ───────────────────────────────────────────────────────────

enable_service() {
    if command -v systemctl &>/dev/null; then
        info "Reloading systemd and enabling $SERVICE_NAME..."
        systemctl daemon-reload
        systemctl enable --now "$SERVICE_NAME"
        info "Service status:"
        systemctl --no-pager status "$SERVICE_NAME" || true
    else
        warn "systemd not found — skipping service setup."
        warn "Start the daemon manually: /usr/local/bin/hnxfmradiod -c /etc/hnxfmradio.conf"
    fi
}

# ── Main ─────────────────────────────────────────────────────────────────────

info "HNxFMRadio installer"
install_deps
build
install_files
enable_service
info "Done. Web UI available at http://$(hostname -I | awk '{print $1}'):8080"
