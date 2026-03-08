#include "AudioLoopback.hpp"
#include "Logger.hpp"

#include <cstring>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <filesystem>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool modprobeLoopback()
{
	int ret = std::system("modprobe snd-aloop 2>/dev/null");
	return ret == 0;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

AudioLoopback::~AudioLoopback()
{
	stop();
}

bool AudioLoopback::start(const std::string& device, int sampleRate, int channels)
{
	sampleRate_ = sampleRate;
	channels_ = channels;

	if (fs::exists("/sys/module/snd_aloop"))
		if (!modprobeLoopback())
		{
			Logger::error("modprobe snd-aloop failed — module may already be loaded or unavailable");
			return false;
		}

	// Build aplay device string: e.g. "hw:Loopback,0"
	std::string playback = device + ",0";

	// Create a pipe for aplay's stdin
	int pipefd[2];
	if (pipe(pipefd) < 0)
	{
		Logger::error("AudioLoopback: pipe(): " + std::string(strerror(errno)));
		return false;
	}

	pid_t pid = fork();
	if (pid < 0)
	{
		Logger::error("AudioLoopback: fork(aplay): " + std::string(strerror(errno)));
		close(pipefd[0]);
		close(pipefd[1]);
		return false;
	}

	if (pid == 0)
	{
		// Child: aplay reads from stdin
		dup2(pipefd[0], STDIN_FILENO);
		close(pipefd[0]);
		close(pipefd[1]);

		std::string srStr = std::to_string(sampleRate);
		std::string chStr = std::to_string(channels);

		execlp("aplay", "aplay",
				 "-D", playback.c_str(),
				 "-f", "S16_LE",
				 "-r", srStr.c_str(),
				 "-c", chStr.c_str(),
				 "-t", "raw",
				 "--buffer-size", "8192",
				 "-", (char*)nullptr);
		_exit(127);
	}

	// Parent: keep write end, close read end
	close(pipefd[0]);
	aplayFd_ = pipefd[1];
	aplayPid_ = pid;

	running_ = true;
	silenceThread_ = std::thread(&AudioLoopback::silenceThread, this);
	Logger::info("AudioLoopback started (aplay on " + playback + ")");
	return true;
}

void AudioLoopback::stop()
{
	running_ = false;
	injecting_ = false;
	injectCV_.notify_all();

	if (silenceThread_.joinable())
		silenceThread_.join();

	if (aplayFd_ >= 0)
	{
		close(aplayFd_);
		aplayFd_ = -1;
	}

	if (aplayPid_ > 0)
	{
		kill(aplayPid_, SIGTERM);
		waitpid(aplayPid_, nullptr, 0);
		aplayPid_ = -1;
	}
}

void AudioLoopback::injectAudio(const int16_t* frames, size_t frameCount)
{
	std::unique_lock<std::mutex> lk(injectMutex_);
	injecting_ = true;
	lk.unlock();

	writePipe(frames, frameCount * (size_t)channels_ * sizeof(int16_t));
}

void AudioLoopback::endInjection()
{
	std::unique_lock<std::mutex> lk(injectMutex_);
	injecting_ = false;
	lk.unlock();
	injectCV_.notify_all();
}

// ---------------------------------------------------------------------------
// Private
// ---------------------------------------------------------------------------

void AudioLoopback::silenceThread()
{
	// One period worth of silence
	const size_t periodFrames = 512;
	std::vector<int16_t> silence(periodFrames * (size_t)channels_, 0);
	const size_t silenceBytes = silence.size() * sizeof(int16_t);

	while (running_)
	{
		{
			std::unique_lock<std::mutex> lk(injectMutex_);
			injectCV_.wait(lk, [this] { return !injecting_ || !running_; });
		}
		if (!running_) break;
		writePipe(silence.data(), silenceBytes);
	}
}

bool AudioLoopback::writePipe(const void* data, size_t bytes)
{
	if (aplayFd_ < 0) return false;

	const char* ptr = static_cast<const char*>(data);
	size_t written = 0;
	while (written < bytes)
	{
		ssize_t w = write(aplayFd_, ptr + written, bytes - written);
		if (w <= 0)
		{
			if (w < 0 && errno == EINTR) continue;
			Logger::error("AudioLoopback: write to aplay pipe failed: " +
							std::string(strerror(errno)));
			return false;
		}
		written += (size_t)w;
	}
	return true;
}
