// SPDX-License-Identifier: GPL-3.0-or-later
//
// Q2C-LCARS sound firmware for Raspberry Pi Pico / Pico 2.
//
// The Pico enumerates as a USB serial port named "Q2C-LCARS Sound". HelixScreen
// (SerialToneSoundBackend) sends one line per change:
//
//   T <freq_hz> <amplitude 0..1> <waveform 0..3>   play / retune (square, saw, triangle, sine)
//   S                                               silence
//   V <0..100>                                      master volume (default 60)
//   ?                                               reply "Q2C-LCARS-SOUND 1"
//
// The tone is synthesised here and sent over I2S to a MAX98357A amplifier.
// Wiring (Pico pin numbers in brackets):
//   GP18 [24] -> BCLK    GP19 [25] -> LRC    GP20 [26] -> DIN
//   VBUS [40] -> Vin     GND  [23] -> GND    speaker -> amp + / -

#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "i2s.pio.h"
#include "pico/stdlib.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PIN_BCLK 18 // LRC is PIN_BCLK + 1
#define PIN_DIN 20
#define SAMPLE_RATE 22050u
#define BLOCK 32                 // samples between USB polls (~1.5 ms)
#define SILENCE_TIMEOUT_US 3000000 // a note left on with no host traffic stops

enum { WAVE_SQUARE = 0, WAVE_SAW = 1, WAVE_TRIANGLE = 2, WAVE_SINE = 3 };

static PIO g_pio = pio0;
static uint g_sm;

static int16_t g_sine[256];
static uint32_t g_phase;
static uint32_t g_phase_inc;
static int g_wave = WAVE_SQUARE;
static float g_amp = 0.0f;        // current, slewed toward g_amp_target
static float g_amp_target = 0.0f; // from the host
static float g_volume = 0.6f;
static uint64_t g_last_msg_us;

static void set_frequency(float hz) {
    if (hz < 20.0f)
        hz = 20.0f;
    if (hz > 12000.0f)
        hz = 12000.0f;
    g_phase_inc = (uint32_t)((double)hz * 4294967296.0 / (double)SAMPLE_RATE);
}

static int16_t next_sample(void) {
    // ~2 ms ramp on every amplitude change: no clicks between notes.
    static const float kStep = 1.0f / (SAMPLE_RATE * 0.002f);
    if (g_amp < g_amp_target) {
        g_amp += kStep;
        if (g_amp > g_amp_target)
            g_amp = g_amp_target;
    } else if (g_amp > g_amp_target) {
        g_amp -= kStep;
        if (g_amp < g_amp_target)
            g_amp = g_amp_target;
    }
    if (g_amp <= 0.0f)
        return 0;

    g_phase += g_phase_inc;
    float s;
    switch (g_wave) {
    case WAVE_SQUARE:
        s = (g_phase & 0x80000000u) ? 0.55f : -0.55f; // squares are loud: tamed
        break;
    case WAVE_SAW:
        s = (float)(int32_t)g_phase * (0.7f / 2147483648.0f);
        break;
    case WAVE_TRIANGLE: {
        const uint32_t t = g_phase >> 16; // 0..65535
        const int32_t tri = t < 32768u ? (int32_t)t * 2 - 32768 : 98303 - (int32_t)t * 2;
        s = (float)tri / 32768.0f;
        break;
    }
    default:
        s = (float)g_sine[g_phase >> 24] / 32767.0f;
        break;
    }
    return (int16_t)(s * g_amp * g_volume * 32767.0f);
}

static inline void push_sample(int16_t v) {
    const uint32_t u = (uint16_t)v;
    pio_sm_put_blocking(g_pio, g_sm, (u << 16) | u); // same sample on both channels
}

static void play_blocking(float hz, float amp, int wave, uint32_t ms) {
    set_frequency(hz);
    g_wave = wave;
    g_amp_target = amp;
    const uint32_t n = SAMPLE_RATE * ms / 1000u;
    for (uint32_t i = 0; i < n; ++i)
        push_sample(next_sample());
}

static void handle_line(char* line) {
    g_last_msg_us = time_us_64();
    switch (line[0]) {
    case 'T': {
        char* p = line + 1;
        const float hz = strtof(p, &p);
        const float amp = strtof(p, &p);
        const long wave = strtol(p, &p, 10);
        set_frequency(hz);
        g_wave = (wave >= 0 && wave <= 3) ? (int)wave : WAVE_SQUARE;
        g_amp_target = amp < 0.0f ? 0.0f : (amp > 1.0f ? 1.0f : amp);
        break;
    }
    case 'S':
        g_amp_target = 0.0f;
        break;
    case 'V': {
        long v = strtol(line + 1, NULL, 10);
        if (v < 0)
            v = 0;
        if (v > 100)
            v = 100;
        g_volume = (float)v / 100.0f;
        break;
    }
    case '?':
        printf("Q2C-LCARS-SOUND 1\n");
        break;
    default:
        break;
    }
}

int main(void) {
    stdio_init_all();

    for (int i = 0; i < 256; ++i)
        g_sine[i] = (int16_t)(sinf((float)i * 6.28318531f / 256.0f) * 32767.0f);

    g_sm = pio_claim_unused_sm(g_pio, true);
    const uint offset = pio_add_program(g_pio, &i2s_out_program);
    i2s_out_program_init(g_pio, g_sm, offset, PIN_DIN, PIN_BCLK, SAMPLE_RATE);

    // Power-on chirp: proves the amp and speaker wiring with no printer attached.
    play_blocking(1480.0f, 0.5f, WAVE_TRIANGLE, 50);
    play_blocking(1976.0f, 0.5f, WAVE_TRIANGLE, 60);
    play_blocking(1976.0f, 0.0f, WAVE_TRIANGLE, 10);

    char line[64];
    size_t len = 0;
    g_last_msg_us = time_us_64();

    for (;;) {
        for (int i = 0; i < BLOCK; ++i)
            push_sample(next_sample());

        int c;
        while ((c = getchar_timeout_us(0)) >= 0) {
            if (c == '\n' || c == '\r') {
                if (len > 0) {
                    line[len] = '\0';
                    handle_line(line);
                    len = 0;
                }
            } else if (len < sizeof(line) - 1) {
                line[len++] = (char)c;
            } else {
                len = 0; // overlong line: drop it
            }
        }

        if (g_amp_target > 0.0f && time_us_64() - g_last_msg_us > SILENCE_TIMEOUT_US)
            g_amp_target = 0.0f;
    }
}
