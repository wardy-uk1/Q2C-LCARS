// SPDX-License-Identifier: GPL-3.0-or-later

#include "serial_tone_sound_backend.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>

#if defined(__linux__)
#include <dirent.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

SerialToneSoundBackend::~SerialToneSoundBackend() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (fd_ >= 0 && sounding_) {
        static constexpr char kSilence[] = "S\n";
        write_line(kSilence, sizeof(kSilence) - 1);
    }
    close_port();
}

std::string SerialToneSoundBackend::find_device(const char* env_override,
                                                const std::string& by_id_dir) {
    if (env_override && *env_override) {
        return env_override;
    }
#if defined(__linux__)
    DIR* dir = opendir(by_id_dir.c_str());
    if (!dir) {
        return {};
    }
    std::string found;
    while (dirent* entry = readdir(dir)) {
        const std::string name = entry->d_name;
        if (name.find(kDeviceMatch) != std::string::npos) {
            found = by_id_dir + "/" + name;
            break;
        }
    }
    closedir(dir);
    return found;
#else
    (void)by_id_dir;
    return {};
#endif
}

bool SerialToneSoundBackend::initialize(const std::string& path) {
#if defined(__linux__)
    std::lock_guard<std::mutex> lock(mutex_);
    close_port();
    const int fd = ::open(path.c_str(), O_WRONLY | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        spdlog::debug("[SerialTone] Cannot open '{}': {}", path, std::strerror(errno));
        return false;
    }
    termios tio{};
    if (tcgetattr(fd, &tio) == 0) {
        cfmakeraw(&tio);
        cfsetospeed(&tio, B115200); // ignored by USB CDC, set for real UARTs
        tcsetattr(fd, TCSANOW, &tio);
    }
    fd_ = fd;
    path_ = path;
    last_freq_ = last_amp_ = last_wave_ = -1;
    sounding_ = false;
    static constexpr char kSilence[] = "S\n";
    write_line(kSilence, sizeof(kSilence) - 1);
    return fd_ >= 0;
#else
    (void)path;
    return false;
#endif
}

void SerialToneSoundBackend::set_tone(float freq_hz, float amplitude, float duty_cycle) {
    (void)duty_cycle;
    std::lock_guard<std::mutex> lock(mutex_);
    if (fd_ < 0) {
        return;
    }
    if (amplitude <= 0.001f || freq_hz <= 0.0f) {
        if (sounding_) {
            static constexpr char kSilence[] = "S\n";
            write_line(kSilence, sizeof(kSilence) - 1);
            sounding_ = false;
            last_freq_ = last_amp_ = -1;
        }
        return;
    }
    const int freq = std::clamp(static_cast<int>(freq_hz + 0.5f), 20, 12000);
    const int amp = std::clamp(static_cast<int>(amplitude * 1000.0f + 0.5f), 0, 1000);
    const int wave = static_cast<int>(waveform_);
    if (sounding_ && freq == last_freq_ && amp == last_amp_ && wave == last_wave_) {
        return;
    }
    char line[48];
    const int n = std::snprintf(line, sizeof(line), "T %d %d.%03d %d\n", freq, amp / 1000,
                                amp % 1000, wave);
    if (n > 0) {
        write_line(line, static_cast<size_t>(n));
    }
    last_freq_ = freq;
    last_amp_ = amp;
    last_wave_ = wave;
    sounding_ = true;
}

void SerialToneSoundBackend::silence() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (fd_ < 0 || !sounding_) {
        return;
    }
    static constexpr char kSilence[] = "S\n";
    write_line(kSilence, sizeof(kSilence) - 1);
    sounding_ = false;
    last_freq_ = last_amp_ = -1;
}

void SerialToneSoundBackend::set_waveform(Waveform w) {
    std::lock_guard<std::mutex> lock(mutex_);
    waveform_ = w;
}

void SerialToneSoundBackend::write_line(const char* line, size_t len) {
#if defined(__linux__)
    // Caller holds mutex_. A full buffer drops the update: the sequencer sends
    // the next one within a tick, and audio must never block it.
    const ssize_t written = ::write(fd_, line, len);
    if (written < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        spdlog::warn("[SerialTone] Write to '{}' failed ({}); sound output stopped", path_,
                     std::strerror(errno));
        close_port();
    }
#else
    (void)line;
    (void)len;
#endif
}

void SerialToneSoundBackend::close_port() {
#if defined(__linux__)
    if (fd_ >= 0) {
        ::close(fd_);
    }
#endif
    fd_ = -1;
    sounding_ = false;
}
