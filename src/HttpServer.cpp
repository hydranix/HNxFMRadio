#include "HttpServer.hpp"
#include "Config.hpp"
#include "FMTransmitterManager.hpp"
#include "Logger.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <sstream>
#include <thread>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Embedded HTML config page
// ---------------------------------------------------------------------------

static const char* kIndexHtml = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>HNx FM Radio</title>
  <style>
    body { font-family: sans-serif; max-width: 480px; margin: 2em auto; background: #1a1a2e; color: #eee; }
    h1   { color: #e94560; text-align: center; }
    label { display: block; margin-top: 1.2em; font-size: .9em; color: #aaa; }
    input[type=range], input[type=number] {
      width: 100%; margin-top: .3em; background: #16213e; color: #eee;
      border: 1px solid #0f3460; border-radius: 4px; padding: .4em;
    }
    #freqDisplay { font-size: 1.6em; text-align: center; color: #e94560; margin: .4em 0; }
    button { margin-top: 1.6em; width: 100%; padding: .8em; background: #e94560;
             color: #fff; border: none; border-radius: 4px; font-size: 1em; cursor: pointer; }
    button:hover { background: #c73652; }
    #status { margin-top: .8em; text-align: center; font-size: .85em; color: #0f3; }
  </style>
</head>
<body>
  <h1>&#128247; HNx FM Radio</h1>

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

  <button onclick="applyConfig()">Apply</button>
  <div id="status"></div>

  <script>
    async function loadConfig() {
      const r = await fetch('/api/config');
      const c = await r.json();
      document.getElementById('freqSlider').value = c.frequency;
      document.getElementById('freqDisplay').textContent = c.frequency + ' MHz';
      document.getElementById('sampleRate').value = c.sample_rate;
      document.getElementById('channels').value   = c.channels;
      document.getElementById('httpPort').value   = c.http_port;
      document.getElementById('audioPort').value  = c.audio_port;
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
      el.textContent = r.ok ? '✓ ' + msg.status : '✗ ' + msg.error;
      el.style.color = r.ok ? '#0f3' : '#f33';
    }
    loadConfig();
  </script>
</body>
</html>
)HTML";

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
        // Detach a short-lived thread per connection
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
// Read the entire request (up to 16 KB)
    std::string request;
    char buf[4096];
    while (request.size() < 16384)
    {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) break;
        request.append(buf, (size_t)n);
        // Stop when we have the full headers + body
        auto headerEnd = request.find("\r\n\r\n");
        if (headerEnd != std::string::npos)
        {
// Check Content-Length to know if we have the full body
            std::string cl = "Content-Length: ";
            auto pos = request.find(cl);
            if (pos == std::string::npos) break; // no body
            size_t clStart = pos + cl.size();
            size_t clEnd = request.find("\r\n", clStart);
            int contentLen = std::stoi(request.substr(clStart, clEnd - clStart));
            size_t bodyStart = headerEnd + 4;
            if ((int)(request.size() - bodyStart) >= contentLen) break;
        }
    }
    if (request.empty()) return;

    // Parse first line: METHOD /path HTTP/1.x
    auto lineEnd = request.find("\r\n");
    std::string firstLine = request.substr(0, lineEnd);
    std::istringstream ls(firstLine);
    std::string method, path;
    ls >> method >> path;

    // Extract body
    std::string body;
    auto headerEnd = request.find("\r\n\r\n");
    if (headerEnd != std::string::npos)
        body = request.substr(headerEnd + 4);

    std::string response;
    if (method == "GET" && path == "/")
    {
        response = respond(200, "text/html; charset=utf-8", kIndexHtml);
    }
    else if (method == "GET" && path == "/api/config")
    {
        response = respond(200, "application/json", handleGetConfig());
    }
    else if (method == "POST" && path == "/api/config")
    {
        response = respond(200, "application/json", handlePostConfig(body));
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

// ---------------------------------------------------------------------------
// Helper
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
