#include "Config.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <filesystem>

Config& Config::instance()
{
  static Config inst;
  return inst;
}

bool Config::load(const std::string& path)
{
  path_ = path;
  std::ifstream f(path);
  if (!f.is_open())
  {
// Missing config is fine — use defaults and write them out
    return save();
  }
  std::ostringstream ss;
  ss << f.rdbuf();
  std::unique_lock lock(mutex_);
  parse(ss.str());
  return true;
}

bool Config::save()
{
  std::string tmp = path_ + ".tmp";
  {
    std::shared_lock lock(mutex_);
    std::ofstream f(tmp);
    if (!f.is_open()) return false;
    f << serialize();
  }
  // Atomic replace
  return std::rename(tmp.c_str(), path_.c_str()) == 0;
}

RadioConfig Config::get() const
{
  std::shared_lock lock(mutex_);
  return cfg_;
}

void Config::set(const RadioConfig& cfg)
{
  std::unique_lock lock(mutex_);
  cfg_ = cfg;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void Config::parse(const std::string& content)
{
  std::istringstream ss(content);
  std::string line, section;
  while (std::getline(ss, line))
  {
// Strip comments and trailing whitespace
    auto pos = line.find(';');
    if (pos != std::string::npos) line.erase(pos);
    while (!line.empty() && std::isspace((unsigned char)line.back()))
      line.pop_back();
    if (line.empty()) continue;

    if (line.size() >= 2 && line.front() == '[' && line.back() == ']')
    {
      section = line.substr(1, line.size() - 2);
      continue;
    }

    auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    std::string key = line.substr(0, eq);
    std::string value = line.substr(eq + 1);

    // Trim key/value (both leading and trailing whitespace)
    while (!key.empty() && std::isspace((unsigned char)key.front()))   key.erase(key.begin());
    while (!key.empty() && std::isspace((unsigned char)key.back()))    key.pop_back();
    while (!value.empty() && std::isspace((unsigned char)value.front())) value.erase(value.begin());
    while (!value.empty() && std::isspace((unsigned char)value.back()))  value.pop_back();

    if (section == "radio")
    {
      if (key == "frequency")
      {
        try
        {
          cfg_.frequency = std::stod(value);
        }
        catch (const std::invalid_argument&)
        {
          // Ignore malformed frequency value and keep existing/default
        }
        catch (const std::out_of_range&)
        {
          // Ignore out-of-range frequency value and keep existing/default
        }
      }
    }
    else if (section == "audio")
    {
      try
      {
        if (key == "sample_rate") cfg_.sample_rate = std::stoi(value);
        else if (key == "channels")    cfg_.channels = std::stoi(value);
        else if (key == "http_port")   cfg_.http_port = std::stoi(value);
        else if (key == "audio_port")  cfg_.audio_port = std::stoi(value);
      }
      catch (const std::invalid_argument&)
      {
        // Ignore malformed integer value and keep existing/default
      }
      catch (const std::out_of_range&)
      {
        // Ignore out-of-range integer value and keep existing/default
      }
    }
    else if (section == "paths")
    {
      if (key == "fm_transmitter")  cfg_.fm_transmitter_path = value;
      else if (key == "arecord")         cfg_.arecord_path = value;
      else if (key == "ffmpeg")          cfg_.ffmpeg_path = value;
      else if (key == "loopback_device") cfg_.loopback_device = value;
    }
  }
}

std::string Config::serialize() const
{
  std::ostringstream ss;
  ss << "[radio]\n"
    << "frequency=" << cfg_.frequency << "\n"
    << "\n"
    << "[audio]\n"
    << "sample_rate=" << cfg_.sample_rate << "\n"
    << "channels=" << cfg_.channels << "\n"
    << "http_port=" << cfg_.http_port << "\n"
    << "audio_port=" << cfg_.audio_port << "\n"
    << "\n"
    << "[paths]\n"
    << "fm_transmitter=" << cfg_.fm_transmitter_path << "\n"
    << "arecord=" << cfg_.arecord_path << "\n"
    << "ffmpeg=" << cfg_.ffmpeg_path << "\n"
    << "loopback_device=" << cfg_.loopback_device << "\n";
  return ss.str();
}
