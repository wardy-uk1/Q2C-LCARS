// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "sound_backend.h"
#include "sound_theme.h"

#include <mutex>
#include <string>

/// Serial tone backend: drives a USB "sound card" built from a Raspberry Pi Pico
/// running the Q2C-LCARS sound firmware (pico/q2c_lcars_sound), which synthesises
/// the tone itself and plays it through an I2S amplifier (MAX98357A).
///
/// The Pico appears as a USB CDC serial port. It is found by its USB product
/// name in /dev/serial/by-id, so a Klipper MCU on another ttyACM is never
/// opened. HELIX_SERIAL_SOUND=<path> names the port directly (tests, pty).
///
/// Wire protocol, one ASCII line per change, newline-terminated:
///   T <freq_hz> <amplitude 0..1> <waveform 0..3>   play / retune
///   S                                               silence
class SerialToneSoundBackend : public SoundBackend {
  public:
    /// Substring of the /dev/serial/by-id name the firmware's USB descriptors produce.
    static constexpr const char* kDeviceMatch = "Q2C-LCARS_Sound";
    static constexpr const char* kPathEnv = "HELIX_SERIAL_SOUND";

    SerialToneSoundBackend() = default;
    ~SerialToneSoundBackend() override;

    /// The port to use: the env override, else the first by-id entry naming the
    /// firmware. Empty when no Pico is connected.
    static std::string find_device(const char* env_override,
                                   const std::string& by_id_dir = "/dev/serial/by-id");

    /// Open the port raw and non-blocking. False when it cannot be opened.
    bool initialize(const std::string& path);

    void set_tone(float freq_hz, float amplitude, float duty_cycle) override;
    void silence() override;
    void set_waveform(Waveform w) override;

    bool supports_waveforms() const override {
        return true;
    }
    bool supports_amplitude() const override {
        return true;
    }
    float min_tick_ms() const override {
        return 2.0f;
    }

    const std::string& path() const {
        return path_;
    }

  private:
    void write_line(const char* line, size_t len);
    void close_port();

    std::mutex mutex_;
    int fd_ = -1;
    std::string path_;
    Waveform waveform_ = Waveform::SQUARE;
    int last_freq_ = -1;
    int last_amp_ = -1;
    int last_wave_ = -1;
    bool sounding_ = false;
};
