#include "../include/apu.h"

#include <math.h>
#include <string.h>

static const uint8_t apu_length_table[32] = {
    10, 254, 20, 2, 40, 4, 80, 6,
    160, 8, 60, 10, 14, 12, 26, 14,
    12, 16, 24, 18, 48, 20, 96, 22,
    192, 24, 72, 26, 16, 28, 32, 30
};

static void pulse_write_control(PulseChannel *ch, uint8_t data) {
    ch->control = data;
    ch->envStart = true;
}

static void pulse_write_sweep(PulseChannel *ch, uint8_t data) {
    ch->sweepEnabled = (data & 0x80) != 0;
    ch->sweepPeriod = (data >> 4) & 0x07;
    ch->sweepNegate = (data & 0x08) != 0;
    ch->sweepShift = data & 0x07;
    ch->sweepReload = true;
}

static void pulse_write_timer_low(PulseChannel *ch, uint8_t data) {
    ch->timer = (uint16_t)((ch->timer & 0xFF00) | data);
}

static void pulse_write_timer_high(PulseChannel *ch, uint8_t data) {
    ch->timer = (uint16_t)((ch->timer & 0x00FF) | ((data & 0x07) << 8));
    uint8_t lengthIndex = (data >> 3) & 0x1F;
    ch->lengthCounter = apu_length_table[lengthIndex];
    ch->envStart = true;
}

static void pulse_set_enabled(PulseChannel *ch, bool enabled) {
    ch->enabled = enabled;
    if (!enabled) {
        ch->lengthCounter = 0;
    }
}

static void pulse_tick_length(PulseChannel *ch) {
    bool lengthHalt = (ch->control & 0x20) != 0;
    if (!lengthHalt && ch->lengthCounter > 0) {
        ch->lengthCounter -= 1;
    }
}

static void pulse_tick_envelope(PulseChannel *ch) {
    uint8_t volume = ch->control & 0x0F;
    bool loop = (ch->control & 0x20) != 0;
    if (ch->envStart) {
        ch->envStart = false;
        ch->envDecay = 15;
        ch->envDivider = volume;
        return;
    }
    if (ch->envDivider == 0) {
        ch->envDivider = volume;
        if (ch->envDecay > 0) {
            ch->envDecay -= 1;
        } else if (loop) {
            ch->envDecay = 15;
        }
    } else {
        ch->envDivider -= 1;
    }
}

static void pulse_apply_sweep(PulseChannel *ch) {
    if (!ch->sweepEnabled || ch->sweepShift == 0) {
        ch->sweepMute = false;
        return;
    }
    uint16_t change = (uint16_t)(ch->timer >> ch->sweepShift);
    uint16_t target;
    if (ch->sweepNegate) {
        target = (uint16_t)(ch->timer - change - (ch->sweepOnesComplement ? 1 : 0));
    } else {
        target = (uint16_t)(ch->timer + change);
    }
    ch->sweepMute = (target > 0x7FF) || (ch->timer < 8);
    if (!ch->sweepMute) {
        ch->timer = target;
    }
}

static void pulse_tick_sweep(PulseChannel *ch) {
    if (ch->sweepReload) {
        ch->sweepReload = false;
        ch->sweepDivider = ch->sweepPeriod;
        if (ch->sweepEnabled) {
            pulse_apply_sweep(ch);
        }
        return;
    }
    if (ch->sweepDivider == 0) {
        ch->sweepDivider = ch->sweepPeriod;
        if (ch->sweepEnabled) {
            pulse_apply_sweep(ch);
        }
    } else {
        ch->sweepDivider -= 1;
    }
}

static double pulse_sample(PulseChannel *ch, double sample_rate, double cpu_clock) {
    if (!ch->enabled || ch->lengthCounter == 0) {
        return 0.0;
    }
    if (ch->timer < 8 || ch->sweepMute) {
        return 0.0;
    }

    uint8_t duty = (uint8_t)((ch->control >> 6) & 0x03);
    double dutyCycle = 0.75;
    switch (duty) {
        case 0: dutyCycle = 0.125; break;
        case 1: dutyCycle = 0.25; break;
        case 2: dutyCycle = 0.5; break;
        default: dutyCycle = 0.75; break;
    }

    double frequency = cpu_clock / (16.0 * ((double)ch->timer + 1.0));
    if (!isfinite(frequency) || frequency <= 0.0) {
        return 0.0;
    }

    ch->phase += frequency / sample_rate;
    if (ch->phase >= 1.0) {
        ch->phase -= floor(ch->phase);
    }

    double raw = (ch->phase < dutyCycle) ? 1.0 : -1.0;
    bool constantVolume = (ch->control & 0x10) != 0;
    uint8_t env = constantVolume ? (ch->control & 0x0F) : ch->envDecay;
    double volume = (double)env / 15.0;
    return raw * volume * 0.3;
}

void apu_init(APU *apu) {
    memset(apu, 0, sizeof(*apu));
    apu->pulse1.sweepOnesComplement = true;
}

void apu_cpu_write(APU *apu, uint16_t addr, uint8_t data) {
    switch (addr) {
        case 0x4000: pulse_write_control(&apu->pulse1, data); break;
        case 0x4001: pulse_write_sweep(&apu->pulse1, data); break;
        case 0x4002: pulse_write_timer_low(&apu->pulse1, data); break;
        case 0x4003: pulse_write_timer_high(&apu->pulse1, data); break;
        case 0x4004: pulse_write_control(&apu->pulse2, data); break;
        case 0x4005: pulse_write_sweep(&apu->pulse2, data); break;
        case 0x4006: pulse_write_timer_low(&apu->pulse2, data); break;
        case 0x4007: pulse_write_timer_high(&apu->pulse2, data); break;
        case 0x4015:
            pulse_set_enabled(&apu->pulse1, (data & 0x01) != 0);
            pulse_set_enabled(&apu->pulse2, (data & 0x02) != 0);
            break;
        case 0x4017:
            break;
        default:
            break;
    }
}

uint8_t apu_read_status(APU *apu) {
    uint8_t value = 0;
    if (apu->pulse1.enabled && apu->pulse1.lengthCounter > 0) {
        value |= 0x01;
    }
    if (apu->pulse2.enabled && apu->pulse2.lengthCounter > 0) {
        value |= 0x02;
    }
    return value;
}

void apu_step(APU *apu, int cycles) {
    apu->frameCounterCycles += cycles;
    while (apu->frameCounterCycles >= 7457) {
        apu->frameCounterCycles -= 7457;
        apu->frameCounterStep = !apu->frameCounterStep;
        pulse_tick_length(&apu->pulse1);
        pulse_tick_length(&apu->pulse2);
        pulse_tick_envelope(&apu->pulse1);
        pulse_tick_envelope(&apu->pulse2);
        if (apu->frameCounterStep) {
            pulse_tick_sweep(&apu->pulse1);
            pulse_tick_sweep(&apu->pulse2);
        }
    }
}

float apu_next_sample(APU *apu, double sample_rate) {
    const double cpu_clock = 1789773.0;
    double s1 = pulse_sample(&apu->pulse1, sample_rate, cpu_clock);
    double s2 = pulse_sample(&apu->pulse2, sample_rate, cpu_clock);
    double mixed = (s1 + s2) * 0.5;
    return (float)mixed;
}
