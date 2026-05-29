#include "HttpServer.hpp"
#include "Config.hpp"
#include "FMTransmitterManager.hpp"
#include "StreamPlayer.hpp"
#include "Logger.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <sstream>
#include <thread>
#include <filesystem>
#include <algorithm>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Embedded HTML page
// ---------------------------------------------------------------------------

static const char* kIndexHtml = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>HNx FM Radio</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body { font-family: sans-serif; max-width: 600px; margin: 0 auto; background: #1a1a2e; color: #eee; padding: 1em; }
    h1 { color: #e94560; text-align: center; margin-bottom: .6em; font-size: 1.4em; }

    /* Now Playing bar */
    #nowPlaying { background: #16213e; border: 1px solid #0f3460; border-radius: 6px;
                  padding: .8em; margin-bottom: 1em; display: none; }
    #nowPlaying.active { display: flex; align-items: center; gap: .8em; }
    #npLabel { flex: 1; font-size: .9em; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
    #npLabel span { color: #e94560; font-weight: bold; }
    .btn-stop { background: #c73652; color: #fff; border: none; border-radius: 4px;
                padding: .5em 1em; cursor: pointer; font-size: .85em; flex-shrink: 0; }
    .btn-stop:hover { background: #a02040; }

    /* Tabs */
    .tabs { display: flex; gap: 2px; margin-bottom: 1em; }
    .tab { flex: 1; padding: .6em; text-align: center; background: #16213e; color: #aaa;
           border: 1px solid #0f3460; cursor: pointer; font-size: .85em; border-radius: 4px 4px 0 0; }
    .tab.active { background: #0f3460; color: #fff; border-bottom-color: #0f3460; }
    .panel { display: none; }
    .panel.active { display: block; }

    /* Stations */
    .station { display: flex; align-items: center; padding: .6em .8em; margin-bottom: 4px;
               background: #16213e; border: 1px solid #0f3460; border-radius: 4px; cursor: pointer;
               transition: background .15s; }
    .station:hover { background: #0f3460; }
    .station.playing { border-color: #e94560; background: #1f1040; }
    .st-info { flex: 1; overflow: hidden; }
    .st-name { font-size: .9em; font-weight: bold; }
    .st-genre { font-size: .75em; color: #888; }
    .st-play { color: #e94560; font-size: 1.2em; flex-shrink: 0; margin-left: .5em; }

    /* Custom URL */
    .url-row { display: flex; gap: .5em; margin-bottom: 1em; }
    .url-row input { flex: 1; background: #16213e; color: #eee; border: 1px solid #0f3460;
                     border-radius: 4px; padding: .5em; font-size: .85em; }
    .url-row button { background: #e94560; color: #fff; border: none; border-radius: 4px;
                      padding: .5em 1em; cursor: pointer; font-size: .85em; }
    .url-row button:hover { background: #c73652; }

    /* File browser */
    #browserPath { display: flex; gap: .5em; margin-bottom: .6em; align-items: center; }
    #browserPath input { flex: 1; background: #16213e; color: #eee; border: 1px solid #0f3460;
                         border-radius: 4px; padding: .5em; font-size: .85em; }
    #browserPath button { background: #0f3460; color: #eee; border: none; border-radius: 4px;
                          padding: .5em .8em; cursor: pointer; font-size: .85em; }
    .file-list { max-height: 50vh; overflow-y: auto; }
    .file-entry { display: flex; align-items: center; padding: .5em .8em; margin-bottom: 2px;
                  background: #16213e; border: 1px solid #0f3460; border-radius: 4px; cursor: pointer;
                  transition: background .15s; font-size: .85em; }
    .file-entry:hover { background: #0f3460; }
    .fe-icon { margin-right: .6em; font-size: 1.1em; flex-shrink: 0; }
    .fe-name { flex: 1; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }

    /* Settings (existing config) */
    label { display: block; margin-top: 1em; font-size: .85em; color: #aaa; }
    input[type=range], input[type=number] {
      width: 100%; margin-top: .3em; background: #16213e; color: #eee;
      border: 1px solid #0f3460; border-radius: 4px; padding: .4em;
    }
    #freqDisplay { font-size: 1.4em; text-align: center; color: #e94560; margin: .4em 0; }
    button.primary { margin-top: 1.2em; width: 100%; padding: .7em; background: #e94560;
             color: #fff; border: none; border-radius: 4px; font-size: .95em; cursor: pointer; }
    button.primary:hover { background: #c73652; }
    #status { margin-top: .6em; text-align: center; font-size: .8em; color: #0f3; }

    .section-label { font-size: .8em; color: #888; margin-bottom: .5em; text-transform: uppercase;
                     letter-spacing: .05em; }
  </style>
</head>
<body>
  <h1>&#128247; HNx FM Radio</h1>

  <!-- Now Playing -->
  <div id="nowPlaying">
    <div id="npLabel">Now Playing: <span id="npSource"></span></div>
    <button class="btn-stop" onclick="stopPlayback()">Stop</button>
  </div>

  <!-- Tabs -->
  <div class="tabs">
    <div class="tab active" onclick="switchTab('stations')">Stations</div>
    <div class="tab" onclick="switchTab('browser')">Files</div>
    <div class="tab" onclick="switchTab('settings')">Settings</div>
  </div>

  <!-- Stations Panel -->
  <div id="stations" class="panel active">
    <div class="section-label">Custom URL</div>
    <div class="url-row">
      <input type="text" id="customUrl" placeholder="http://stream-url.com/stream.mp3">
      <button onclick="playUrl(document.getElementById('customUrl').value)">Play</button>
    </div>

    <div class="section-label">SomaFM Stations</div>
    <div id="stationList"></div>
  </div>

  <!-- File Browser Panel -->
  <div id="browser" class="panel">
    <div class="section-label">Local Filesystem</div>
    <div id="browserPath">
      <button onclick="browseUp()">&#8593;</button>
      <input type="text" id="pathInput" value="/" onkeydown="if(event.key==='Enter')browseTo(this.value)">
      <button onclick="browseTo(document.getElementById('pathInput').value)">Go</button>
    </div>
    <div id="fileList" class="file-list"></div>

    <div class="section-label" style="margin-top:1em">Remote Filesystem</div>
    <div id="remoteBrowserPath">
      <div style="display:flex;gap:.5em;margin-bottom:.6em;align-items:center">
        <button onclick="remoteBrowseUp()">&#8593;</button>
        <input type="text" id="remotePathInput" value="/mnt" onkeydown="if(event.key==='Enter')remoteBrowseTo(this.value)">
        <button onclick="remoteBrowseTo(document.getElementById('remotePathInput').value)">Go</button>
      </div>
    </div>
    <div id="remoteFileList" class="file-list"></div>
  </div>

  <!-- Settings Panel -->
  <div id="settings" class="panel">
    <label>Frequency (MHz)</label>
    <input type="range" id="freqSlider" min="87.5" max="108.0" step="0.1" value="100.6"
           oninput="document.getElementById('freqDisplay').textContent=this.value+' MHz'">
    <div id="freqDisplay">100.6 MHz</div>

    <label>Sample Rate (Hz)</label>
    <input type="number" id="sampleRate" min="8000" max="48000" step="100" value="22050">

    <label>Channels</label>
    <input type="number" id="channels" min="1" max="2" value="1">

    <label>HTTP Port</label>
    <input type="number" id="httpPort" min="1024" max="65535" value="8080">

    <label>Audio Injection Port</label>
    <input type="number" id="audioPort" min="1024" max="65535" value="8081">

    <button class="primary" onclick="applyConfig()">Apply</button>
    <div id="status"></div>
  </div>

  <script>
    // --- SomaFM Stations ---
    const stations = [
      { name: 'Groove Salad',      genre: 'Ambient / Downtempo',    url: 'http://ice1.somafm.com/groovesalad-128-mp3' },
      { name: 'Drone Zone',        genre: 'Ambient',                url: 'http://ice1.somafm.com/dronezone-128-mp3' },
      { name: 'Space Station Soma',genre: 'Space Music',            url: 'http://ice1.somafm.com/spacestation-128-mp3' },
      { name: 'DEF CON Radio',     genre: 'Hacker Culture',         url: 'http://ice1.somafm.com/defcon-128-mp3' },
      { name: 'Underground 80s',   genre: '80s Alternative',        url: 'http://ice1.somafm.com/u80s-128-mp3' },
      { name: 'Secret Agent',      genre: 'Lounge / Spy',           url: 'http://ice1.somafm.com/secretagent-128-mp3' },
      { name: 'Lush',              genre: 'Electronic Vocals',      url: 'http://ice1.somafm.com/lush-128-mp3' },
      { name: 'Beat Blender',      genre: 'DJ Mixes',               url: 'http://ice1.somafm.com/beatblender-128-mp3' },
      { name: 'Fluid',             genre: 'Instrumental Hip-Hop',   url: 'http://ice1.somafm.com/fluid-128-mp3' },
      { name: 'The Trip',          genre: 'Progressive Electronic', url: 'http://ice1.somafm.com/thetrip-128-mp3' },
      { name: 'cliqhop idm',       genre: 'Electronica / IDM',      url: 'http://ice1.somafm.com/cliqhop-128-mp3' },
      { name: 'Sonic Universe',    genre: 'Jazz',                   url: 'http://ice1.somafm.com/sonicuniverse-128-mp3' },
      { name: 'Indie Pop Rocks',   genre: 'Indie Pop',              url: 'http://ice1.somafm.com/indiepop-128-mp3' },
      { name: 'Left Coast 70s',    genre: '70s Hits',               url: 'http://ice1.somafm.com/seventies-128-mp3' },
      { name: 'Boot Liquor',       genre: 'Americana / Country',    url: 'http://ice1.somafm.com/bootliquor-128-mp3' },
      { name: 'Metal Detector',    genre: 'Heavy Metal',            url: 'http://ice1.somafm.com/metal-128-mp3' },
      { name: 'Seven Inch Soul',   genre: '60s/70s Soul',           url: 'http://ice1.somafm.com/7soul-128-mp3' },
      { name: 'Suburbs of Goa',    genre: 'World / Dub',            url: 'http://ice1.somafm.com/suburbsofgoa-128-mp3' },
      { name: 'Illinois Street Lounge', genre: 'Exotica / Lounge',  url: 'http://ice1.somafm.com/illstreet-128-mp3' },
      { name: 'BAGeL Radio',       genre: 'Beatles-Influenced',     url: 'http://ice1.somafm.com/bagel-128-mp3' },
    ];

    let currentSource = '';
    let currentLocalPath = '/';
    let currentRemotePath = '/mnt';

    // --- Tab switching ---
    function switchTab(name) {
      document.querySelectorAll('.tab').forEach((t, i) => {
        const panels = ['stations', 'browser', 'settings'];
        t.classList.toggle('active', panels[i] === name);
      });
      document.querySelectorAll('.panel').forEach(p => p.classList.toggle('active', p.id === name));
    }

    // --- Station rendering ---
    function renderStations() {
      const el = document.getElementById('stationList');
      el.innerHTML = stations.map(s =>
        `<div class="station${currentSource === s.url ? ' playing' : ''}" onclick="playUrl('${s.url}')">
          <div class="st-info"><div class="st-name">${s.name}</div><div class="st-genre">${s.genre}</div></div>
          <div class="st-play">${currentSource === s.url ? '&#9632;' : '&#9654;'}</div>
        </div>`
      ).join('');
    }

    // --- Playback ---
    async function playUrl(url) {
      if (!url) return;
      try {
        const r = await fetch('/api/play', {
          method: 'POST',
          headers: {'Content-Type': 'application/json'},
          body: JSON.stringify({ source: url })
        });
        if (r.ok) { await pollStatus(); }
      } catch(e) { console.error(e); }
    }

    async function stopPlayback() {
      try {
        await fetch('/api/stop', { method: 'POST' });
        currentSource = '';
        updateNowPlaying();
        renderStations();
      } catch(e) { console.error(e); }
    }

    async function pollStatus() {
      try {
        const r = await fetch('/api/status');
        const s = await r.json();
        currentSource = s.playing ? s.source : '';
        updateNowPlaying();
        renderStations();
      } catch(e) { console.error(e); }
    }

    function updateNowPlaying() {
      const np = document.getElementById('nowPlaying');
      const src = document.getElementById('npSource');
      if (currentSource) {
        // Show friendly name if it's a known station
        const st = stations.find(s => s.url === currentSource);
        src.textContent = st ? st.name : currentSource;
        np.classList.add('active');
      } else {
        np.classList.remove('active');
      }
    }

    // --- File browser ---
    async function browseTo(path) {
      try {
        const r = await fetch('/api/browse?path=' + encodeURIComponent(path));
        const data = await r.json();
        if (data.error) { alert(data.error); return; }
        currentLocalPath = data.path;
        document.getElementById('pathInput').value = data.path;
        renderFileList('fileList', data.entries, false);
      } catch(e) { console.error(e); }
    }

    function browseUp() {
      const parts = currentLocalPath.replace(/\/+$/, '').split('/');
      parts.pop();
      browseTo(parts.join('/') || '/');
    }

    async function remoteBrowseTo(path) {
      try {
        const r = await fetch('/api/browse?path=' + encodeURIComponent(path));
        const data = await r.json();
        if (data.error) { alert(data.error); return; }
        currentRemotePath = data.path;
        document.getElementById('remotePathInput').value = data.path;
        renderFileList('remoteFileList', data.entries, true);
      } catch(e) { console.error(e); }
    }

    function remoteBrowseUp() {
      const parts = currentRemotePath.replace(/\/+$/, '').split('/');
      parts.pop();
      remoteBrowseTo(parts.join('/') || '/');
    }

    function renderFileList(elId, entries, isRemote) {
      const el = document.getElementById(elId);
      if (!entries || entries.length === 0) {
        el.innerHTML = '<div style="color:#666;padding:.8em;text-align:center">Empty directory</div>';
        return;
      }
      el.innerHTML = entries.map(e => {
        const icon = e.is_dir ? '&#128193;' : '&#127925;';
        const onclick = e.is_dir
          ? (isRemote ? `remoteBrowseTo('${e.path.replace(/'/g,"\\'")}')` : `browseTo('${e.path.replace(/'/g,"\\'")}')`)
          : `playUrl('${e.path.replace(/'/g,"\\'")}')`;
        return `<div class="file-entry" onclick="${onclick}">
          <div class="fe-icon">${icon}</div>
          <div class="fe-name">${e.name}</div>
        </div>`;
      }).join('');
    }

    // --- Settings ---
    async function loadConfig() {
      try {
        const r = await fetch('/api/config');
        const c = await r.json();
        document.getElementById('freqSlider').value = c.frequency;
        document.getElementById('freqDisplay').textContent = c.frequency + ' MHz';
        document.getElementById('sampleRate').value = c.sample_rate;
        document.getElementById('channels').value   = c.channels;
        document.getElementById('httpPort').value   = c.http_port;
        document.getElementById('audioPort').value  = c.audio_port;
      } catch(e) { console.error(e); }
    }

    async function applyConfig() {
      const body = {
        frequency:   parseFloat(document.getElementById('freqSlider').value),
        sample_rate: parseInt(document.getElementById('sampleRate').value),
        channels:    parseInt(document.getElementById('channels').value),
        http_port:   parseInt(document.getElementById('httpPort').value),
        audio_port:  parseInt(document.getElementById('audioPort').value),
      };
      const r = await fetch('/api/config', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(body)
      });
      const msg = await r.json();
      const el  = document.getElementById('status');
      el.textContent = r.ok ? '\u2713 ' + msg.status : '\u2717 ' + msg.error;
      el.style.color = r.ok ? '#0f3' : '#f33';
    }

    // --- Init ---
    loadConfig();
    renderStations();
    pollStatus();
    setInterval(pollStatus, 5000);
    browseTo('/');
    remoteBrowseTo('/mnt');
  </script>
</body>
</html>
)HTML";

// Audio file extensions for filtering in the file browser
static const std::vector<std::string> kAudioExtensions = {
    ".mp3", ".flac", ".wav", ".ogg", ".opus", ".aac", ".m4a",
    ".wma", ".aiff", ".ape", ".mka", ".webm", ".mp4", ".pls",
    ".m3u", ".m3u8"
};

static bool isAudioFile(const std::string& name)
{
    auto dot = name.rfind('.');
    if (dot == std::string::npos) return false;
    std::string ext = name.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return std::find(kAudioExtensions.begin(), kAudioExtensions.end(), ext)
           != kAudioExtensions.end();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

HttpServer::~HttpServer() { stop(); }

bool HttpServer::start(int port)
{
    serverFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd_ < 0)
    {
        Logger::error("HttpServer: socket(): " + std::string(strerror(errno)));
        return false;
    }
    int opt = 1;
    setsockopt(serverFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);

    if (bind(serverFd_, (sockaddr*)&addr, sizeof(addr)) < 0)
    {
        Logger::error("HttpServer: bind(" + std::to_string(port) + "): " +
                      std::string(strerror(errno)));
        close(serverFd_); serverFd_ = -1;
        return false;
    }
    listen(serverFd_, 8);
    running_ = true;
    acceptThread_ = std::thread(&HttpServer::acceptLoop, this);
    Logger::info("HttpServer listening on port " + std::to_string(port));
    return true;
}

void HttpServer::stop()
{
    running_ = false;
    if (serverFd_ >= 0)
    {
        shutdown(serverFd_, SHUT_RDWR);
        close(serverFd_);
        serverFd_ = -1;
    }
    if (acceptThread_.joinable()) acceptThread_.join();
}

// ---------------------------------------------------------------------------
// Accept loop
// ---------------------------------------------------------------------------

void HttpServer::acceptLoop()
{
    while (running_)
    {
        int fd = accept(serverFd_, nullptr, nullptr);
        if (fd < 0) break;
        std::thread([this, fd]()
        {
            handleClient(fd);
            close(fd);
        }).detach();
    }
}

// ---------------------------------------------------------------------------
// HTTP request handling (minimal HTTP/1.1 parser)
// ---------------------------------------------------------------------------

void HttpServer::handleClient(int fd)
{
    std::string request;
    char buf[4096];
    while (request.size() < 16384)
    {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        request.append(buf, (size_t)n);
        auto headerEnd = request.find("\r\n\r\n");
        if (headerEnd != std::string::npos)
        {
            std::string cl = "Content-Length: ";
            auto pos = request.find(cl);
            if (pos == std::string::npos) break;
            size_t clStart = pos + cl.size();
            size_t clEnd = request.find("\r\n", clStart);
            int contentLen = std::stoi(request.substr(clStart, clEnd - clStart));
            size_t bodyStart = headerEnd + 4;
            if ((int)(request.size() - bodyStart) >= contentLen) break;
        }
    }
    if (request.empty()) return;

    auto lineEnd = request.find("\r\n");
    std::string firstLine = request.substr(0, lineEnd);
    std::istringstream ls(firstLine);
    std::string method, path;
    ls >> method >> path;

    std::string body;
    auto headerEnd = request.find("\r\n\r\n");
    if (headerEnd != std::string::npos)
        body = request.substr(headerEnd + 4);

    // Route the request
    std::string response;
    std::string basePath = path;
    auto qpos = basePath.find('?');
    if (qpos != std::string::npos)
        basePath = basePath.substr(0, qpos);

    if (method == "GET" && basePath == "/")
    {
        response = respond(200, "text/html; charset=utf-8", kIndexHtml);
    }
    else if (method == "GET" && basePath == "/api/config")
    {
        response = respond(200, "application/json", handleGetConfig());
    }
    else if (method == "POST" && basePath == "/api/config")
    {
        response = respond(200, "application/json", handlePostConfig(body));
    }
    else if (method == "POST" && basePath == "/api/play")
    {
        response = respond(200, "application/json", handlePlay(body));
    }
    else if (method == "POST" && basePath == "/api/stop")
    {
        response = respond(200, "application/json", handleStop());
    }
    else if (method == "GET" && basePath == "/api/status")
    {
        response = respond(200, "application/json", handleStatus());
    }
    else if (method == "GET" && basePath == "/api/browse")
    {
        std::string browsePath = extractQueryParam(path, "path");
        if (browsePath.empty()) browsePath = "/";
        response = respond(200, "application/json", handleBrowse(browsePath));
    }
    else
    {
        response = respond(404, "application/json", R"({"error":"not found"})");
    }

    send(fd, response.c_str(), response.size(), 0);
}

// ---------------------------------------------------------------------------
// API handlers
// ---------------------------------------------------------------------------

std::string HttpServer::handleGetConfig()
{
    auto cfg = config_.get();
    json j;
    j["frequency"] = cfg.frequency;
    j["sample_rate"] = cfg.sample_rate;
    j["channels"] = cfg.channels;
    j["http_port"] = cfg.http_port;
    j["audio_port"] = cfg.audio_port;
    return j.dump();
}

std::string HttpServer::handlePostConfig(const std::string& body)
{
    try
    {
        auto j = json::parse(body);
        auto cfg = config_.get();

        bool freqChanged = false;
        if (j.contains("frequency"))
        {
            double f = j["frequency"].get<double>();
            if (f < 87.5 || f > 108.0) throw std::runtime_error("frequency out of range");
            freqChanged |= (cfg.frequency != f);
            cfg.frequency = f;
        }
        if (j.contains("sample_rate")) cfg.sample_rate = j["sample_rate"].get<int>();
        if (j.contains("channels"))    cfg.channels = j["channels"].get<int>();
        if (j.contains("http_port"))   cfg.http_port = j["http_port"].get<int>();
        if (j.contains("audio_port"))  cfg.audio_port = j["audio_port"].get<int>();

        config_.set(cfg);
        config_.save();

        if (freqChanged)
            fm_.restart(cfg.frequency);

        Logger::info("Config updated via HTTP: freq=" + std::to_string(cfg.frequency));
        return R"({"status":"applied"})";
    }
    catch (const std::exception& e)
    {
        Logger::warn("HttpServer: bad config POST: " + std::string(e.what()));
        return json{ {"error", e.what()} }.dump();
    }
}

std::string HttpServer::handlePlay(const std::string& body)
{
    try
    {
        auto j = json::parse(body);
        std::string source = j.at("source").get<std::string>();
        if (source.empty())
            return json{ {"error", "empty source"} }.dump();

        auto cfg = config_.get();
        bool ok = player_.play(source, cfg.ffmpeg_path, cfg.loopback_device,
                               cfg.sample_rate, cfg.channels);
        if (ok)
            return json{ {"status", "playing"}, {"source", source} }.dump();
        else
            return json{ {"error", "failed to start playback"} }.dump();
    }
    catch (const std::exception& e)
    {
        return json{ {"error", e.what()} }.dump();
    }
}

std::string HttpServer::handleStop()
{
    player_.stop();
    return R"({"status":"stopped"})";
}

std::string HttpServer::handleStatus()
{
    json j;
    j["playing"] = player_.isPlaying();
    j["source"] = player_.currentSource();
    return j.dump();
}

std::string HttpServer::handleBrowse(const std::string& rawPath)
{
    try
    {
        fs::path p = fs::canonical(fs::path(rawPath));
        if (!fs::is_directory(p))
            return json{ {"error", "not a directory"} }.dump();

        json entries = json::array();
        std::vector<std::pair<std::string, fs::path>> dirs, files;

        for (auto& entry : fs::directory_iterator(p, fs::directory_options::skip_permission_denied))
        {
            std::string name = entry.path().filename().string();
            if (name.empty() || name[0] == '.') continue; // skip hidden

            if (entry.is_directory())
                dirs.emplace_back(name, entry.path());
            else if (entry.is_regular_file() && isAudioFile(name))
                files.emplace_back(name, entry.path());
        }

        // Sort alphabetically
        auto cmp = [](const auto& a, const auto& b) { return a.first < b.first; };
        std::sort(dirs.begin(), dirs.end(), cmp);
        std::sort(files.begin(), files.end(), cmp);

        for (auto& [name, path] : dirs)
            entries.push_back({ {"name", name}, {"path", path.string()}, {"is_dir", true} });
        for (auto& [name, path] : files)
            entries.push_back({ {"name", name}, {"path", path.string()}, {"is_dir", false} });

        return json{ {"path", p.string()}, {"entries", entries} }.dump();
    }
    catch (const std::exception& e)
    {
        return json{ {"error", e.what()} }.dump();
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::string HttpServer::respond(int status,
                                const std::string& contentType,
                                const std::string& body)
{
    std::ostringstream ss;
    std::string statusText = (status == 200) ? "OK" :
        (status == 404) ? "Not Found" : "Error";
    ss << "HTTP/1.1 " << status << " " << statusText << "\r\n"
        << "Content-Type: " << contentType << "\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n"
        << "Access-Control-Allow-Origin: *\r\n"
        << "\r\n"
        << body;
    return ss.str();
}

std::string HttpServer::urlDecode(const std::string& str)
{
    std::string result;
    result.reserve(str.size());
    for (size_t i = 0; i < str.size(); ++i)
    {
        if (str[i] == '%' && i + 2 < str.size())
        {
            int val = 0;
            std::istringstream iss(str.substr(i + 1, 2));
            if (iss >> std::hex >> val)
            {
                result += static_cast<char>(val);
                i += 2;
                continue;
            }
        }
        else if (str[i] == '+')
        {
            result += ' ';
            continue;
        }
        result += str[i];
    }
    return result;
}

std::string HttpServer::extractQueryParam(const std::string& fullPath,
                                          const std::string& key)
{
    auto qpos = fullPath.find('?');
    if (qpos == std::string::npos) return "";
    std::string query = fullPath.substr(qpos + 1);

    std::string search = key + "=";
    size_t pos = 0;
    while (pos < query.size())
    {
        if (query.compare(pos, search.size(), search) == 0)
        {
            size_t valStart = pos + search.size();
            size_t valEnd = query.find('&', valStart);
            if (valEnd == std::string::npos) valEnd = query.size();
            return urlDecode(query.substr(valStart, valEnd - valStart));
        }
        auto amp = query.find('&', pos);
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return "";
}
