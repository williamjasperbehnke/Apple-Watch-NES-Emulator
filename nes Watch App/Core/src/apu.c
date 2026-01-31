#include "../include/apu.h"

#include <math.h>
#include <string.h>

static const uint8_t apu_length_table[32] = {
    10, 254, 20, 2, 40, 4, 80, 6,
    160, 8, 60, 10, 14, 12, 26, 14,
    12, 16, 24, 18, 48, 20, 96, 22,
    192, 24, 72, 26, 16, 28, 32, 30
};

static const uint16_t noise_period_table[16] = {
    4, 8, 16, 32, 64, 96, 128, 160,
    202, 254, 380, 508, 762, 1016, 2034, 4068
};

static const uint16_t dmc_rate_table[16] = {
    428, 380, 340, 320, 286, 254, 226, 214,
    190, 160, 142, 128, 106, 85, 72, 54
};

static const uint8_t triangle_sequence[32] = {
    15, 14, 13, 12, 11, 10, 9, 8,
    7, 6, 5, 4, 3, 2, 1, 0,
    0, 1, 2, 3, 4, 5, 6, 7,
    8, 9, 10, 11, 12, 13, 14, 15
};

static const double apu_cpu_clock = 1789773.0;

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

static double pulse_sample(PulseChannel *ch, double sample_rate) {
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

    double frequency = apu_cpu_clock / (16.0 * ((double)ch->timer + 1.0));
    if (!isfinite(frequency) || frequency <= 0.0) {
        return 0.0;
    }

    ch->phase += frequency / sample_rate;
    if (ch->phase >= 1.0) {
        ch->phase -= floor(ch->phase);
    }

    bool constantVolume = (ch->control & 0x10) != 0;
    uint8_t env = constantVolume ? (ch->control & 0x0F) : ch->envDecay;
    double volume = (double)env;
    return (ch->phase < dutyCycle) ? volume : 0.0;
}

static void triangle_write_control(TriangleChannel *ch, uint8_t data) {
    ch->linearControl = (data & 0x80) != 0;
    ch->linearReload = data & 0x7F;
}

static void triangle_write_timer_low(TriangleChannel *ch, uint8_t data) {
    ch->timer = (uint16_t)((ch->timer & 0xFF00) | data);
}

static void triangle_write_timer_high(TriangleChannel *ch, uint8_t data) {
    ch->timer = (uint16_t)((ch->timer & 0x00FF) | ((data & 0x07) << 8));
    uint8_t lengthIndex = (data >> 3) & 0x1F;
    ch->lengthCounter = apu_length_table[lengthIndex];
    ch->linearReloadFlag = true;
}

static void triangle_set_enabled(TriangleChannel *ch, bool enabled) {
    ch->enabled = enabled;
    if (!enabled) {
        ch->lengthCounter = 0;
    }
}

static void triangle_tick_length(TriangleChannel *ch) {
    if (!ch->linearControl && ch->lengthCounter > 0) {
        ch->lengthCounter -= 1;
    }
}

static void triangle_tick_linear(TriangleChannel *ch) {
    if (ch->linearReloadFlag) {
        ch->linearCounter = ch->linearReload;
    } else if (ch->linearCounter > 0) {
        ch->linearCounter -= 1;
    }
    if (!ch->linearControl) {
        ch->linearReloadFlag = false;
    }
}

static void triangle_tick_timer(TriangleChannel *ch) {
    if (ch->timerCounter == 0) {
        ch->timerCounter = ch->timer;
        if (ch->lengthCounter > 0 && ch->linearCounter > 0) {
            ch->sequencePos = (uint8_t)((ch->sequencePos + 1) & 0x1F);
        }
    } else {
        ch->timerCounter -= 1;
    }
}

static double triangle_sample(TriangleChannel *ch) {
    if (!ch->enabled || ch->lengthCounter == 0 || ch->linearCounter == 0) {
        return 0.0;
    }
    return (double)triangle_sequence[ch->sequencePos];
}

static void noise_write_control(NoiseChannel *ch, uint8_t data) {
    ch->control = data;
    ch->envStart = true;
}

static void noise_write_period(NoiseChannel *ch, uint8_t data) {
    uint8_t idx = data & 0x0F;
    ch->timer = noise_period_table[idx];
}

static void noise_write_length(NoiseChannel *ch, uint8_t data) {
    uint8_t lengthIndex = (data >> 3) & 0x1F;
    ch->lengthCounter = apu_length_table[lengthIndex];
    ch->envStart = true;
}

static void noise_set_enabled(NoiseChannel *ch, bool enabled) {
    ch->enabled = enabled;
    if (!enabled) {
        ch->lengthCounter = 0;
    }
}

static void noise_tick_length(NoiseChannel *ch) {
    bool lengthHalt = (ch->control & 0x20) != 0;
    if (!lengthHalt && ch->lengthCounter > 0) {
        ch->lengthCounter -= 1;
    }
}

static void noise_tick_envelope(NoiseChannel *ch) {
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

static void noise_tick_timer(NoiseChannel *ch) {
    if (ch->timerCounter == 0) {
        ch->timerCounter = ch->timer;
        uint16_t feedback;
        bool mode = (ch->control & 0x80) != 0;
        if (mode) {
            feedback = (uint16_t)(((ch->lfsr & 0x0001) ^ ((ch->lfsr >> 6) & 0x0001)) & 0x0001);
        } else {
            feedback = (uint16_t)(((ch->lfsr & 0x0001) ^ ((ch->lfsr >> 1) & 0x0001)) & 0x0001);
        }
        ch->lfsr = (uint16_t)((ch->lfsr >> 1) | (feedback << 14));
    } else {
        ch->timerCounter -= 1;
    }
}

static double noise_sample(NoiseChannel *ch) {
    if (!ch->enabled || ch->lengthCounter == 0) {
        return 0.0;
    }
    if (ch->lfsr & 0x0001) {
        return 0.0;
    }
    bool constantVolume = (ch->control & 0x10) != 0;
    uint8_t env = constantVolume ? (ch->control & 0x0F) : ch->envDecay;
    return (double)env;
}

static void dmc_restart(DMCChannel *ch) {
    ch->currentAddress = ch->sampleAddress;
    ch->bytesRemaining = ch->sampleLength;
}

static void dmc_set_enabled(DMCChannel *ch, bool enabled) {
    ch->enabled = enabled;
    if (!enabled) {
        ch->bytesRemaining = 0;
    } else if (ch->bytesRemaining == 0) {
        dmc_restart(ch);
    }
}

static void dmc_fetch_sample(APU *apu) {
    DMCChannel *ch = &apu->dmc;
    if (!ch->sampleBufferEmpty || ch->bytesRemaining == 0) {
        return;
    }
    if (apu->read) {
        ch->sampleBuffer = apu->read(apu->readContext, ch->currentAddress);
    } else {
        ch->sampleBuffer = 0;
    }
    ch->sampleBufferEmpty = false;
    ch->currentAddress = (uint16_t)(ch->currentAddress + 1);
    if (ch->currentAddress == 0) {
        ch->currentAddress = 0x8000;
    }
    if (ch->bytesRemaining > 0) {
        ch->bytesRemaining -= 1;
    }
    if (ch->bytesRemaining == 0) {
        if (ch->loop) {
            dmc_restart(ch);
        }
    }
}

static void dmc_tick_timer(APU *apu) {
    DMCChannel *ch = &apu->dmc;
    if (ch->timerCounter == 0) {
        ch->timerCounter = ch->timer;
        if (ch->bitCount == 0) {
            if (!ch->sampleBufferEmpty) {
                ch->shiftRegister = ch->sampleBuffer;
                ch->sampleBufferEmpty = true;
                ch->bitCount = 8;
            } else {
                return;
            }
        }
        if (ch->bitCount > 0) {
            if (ch->shiftRegister & 0x01) {
                if (ch->outputLevel <= 125) {
                    ch->outputLevel = (uint8_t)(ch->outputLevel + 2);
                }
            } else {
                if (ch->outputLevel >= 2) {
                    ch->outputLevel = (uint8_t)(ch->outputLevel - 2);
                }
            }
            ch->shiftRegister >>= 1;
            ch->bitCount -= 1;
        }
    } else {
        ch->timerCounter -= 1;
    }
}

static double dmc_sample(DMCChannel *ch) {
    return (double)ch->outputLevel;
}

void apu_init(APU *apu) {
    memset(apu, 0, sizeof(*apu));
    pthread_mutex_init(&apu->mutex, NULL);
    apu->pulse1.sweepOnesComplement = true;
    apu->noise.lfsr = 1;
    apu->dmc.sampleBufferEmpty = true;
    apu->outputFilter = 0.0;
    apu->sampleCycleRemainder = 0.0;
}

void apu_reset(APU *apu) {
    ApuReadFunc read = apu->read;
    void *context = apu->readContext;
    pthread_mutex_destroy(&apu->mutex);
    memset(apu, 0, sizeof(*apu));
    pthread_mutex_init(&apu->mutex, NULL);
    apu->read = read;
    apu->readContext = context;
    apu->pulse1.sweepOnesComplement = true;
    apu->noise.lfsr = 1;
    apu->dmc.sampleBufferEmpty = true;
    apu->outputFilter = 0.0;
    apu->sampleCycleRemainder = 0.0;
}

void apu_set_read_callback(APU *apu, ApuReadFunc read, void *context) {
    apu->read = read;
    apu->readContext = context;
}

void apu_cpu_write(APU *apu, uint16_t addr, uint8_t data) {
    pthread_mutex_lock(&apu->mutex);
    switch (addr) {
        case 0x4000: pulse_write_control(&apu->pulse1, data); break;
        case 0x4001: pulse_write_sweep(&apu->pulse1, data); break;
        case 0x4002: pulse_write_timer_low(&apu->pulse1, data); break;
        case 0x4003: pulse_write_timer_high(&apu->pulse1, data); break;
        case 0x4004: pulse_write_control(&apu->pulse2, data); break;
        case 0x4005: pulse_write_sweep(&apu->pulse2, data); break;
        case 0x4006: pulse_write_timer_low(&apu->pulse2, data); break;
        case 0x4007: pulse_write_timer_high(&apu->pulse2, data); break;
        case 0x4008: triangle_write_control(&apu->triangle, data); break;
        case 0x400A: triangle_write_timer_low(&apu->triangle, data); break;
        case 0x400B: triangle_write_timer_high(&apu->triangle, data); break;
        case 0x400C: noise_write_control(&apu->noise, data); break;
        case 0x400E: noise_write_period(&apu->noise, data); apu->noise.control = (uint8_t)((apu->noise.control & 0x7F) | (data & 0x80)); break;
        case 0x400F: noise_write_length(&apu->noise, data); break;
        case 0x4010:
            apu->dmc.control = data;
            apu->dmc.irqEnabled = (data & 0x80) != 0;
            apu->dmc.loop = (data & 0x40) != 0;
            apu->dmc.timer = dmc_rate_table[data & 0x0F];
            break;
        case 0x4011:
            apu->dmc.directLoad = data & 0x7F;
            apu->dmc.outputLevel = apu->dmc.directLoad;
            break;
        case 0x4012:
            apu->dmc.sampleAddress = (uint16_t)(0xC000 | ((uint16_t)data << 6));
            break;
        case 0x4013:
            apu->dmc.sampleLength = (uint16_t)((uint16_t)data * 16 + 1);
            break;
        case 0x4015:
            pulse_set_enabled(&apu->pulse1, (data & 0x01) != 0);
            pulse_set_enabled(&apu->pulse2, (data & 0x02) != 0);
            triangle_set_enabled(&apu->triangle, (data & 0x04) != 0);
            noise_set_enabled(&apu->noise, (data & 0x08) != 0);
            dmc_set_enabled(&apu->dmc, (data & 0x10) != 0);
            break;
        case 0x4017:
            apu->frameCounterMode = (data & 0x80) != 0;
            apu->frameIrqInhibit = (data & 0x40) != 0;
            apu->frameCounterCycle = 0;
            if (apu->frameCounterMode) {
                pulse_tick_length(&apu->pulse1);
                pulse_tick_length(&apu->pulse2);
                triangle_tick_length(&apu->triangle);
                noise_tick_length(&apu->noise);
                pulse_tick_sweep(&apu->pulse1);
                pulse_tick_sweep(&apu->pulse2);
                pulse_tick_envelope(&apu->pulse1);
                pulse_tick_envelope(&apu->pulse2);
                noise_tick_envelope(&apu->noise);
                triangle_tick_linear(&apu->triangle);
            }
            break;
        default:
            break;
    }
    pthread_mutex_unlock(&apu->mutex);
}

uint8_t apu_read_status(APU *apu) {
    pthread_mutex_lock(&apu->mutex);
    uint8_t value = 0;
    if (apu->pulse1.enabled && apu->pulse1.lengthCounter > 0) {
        value |= 0x01;
    }
    if (apu->pulse2.enabled && apu->pulse2.lengthCounter > 0) {
        value |= 0x02;
    }
    if (apu->triangle.enabled && apu->triangle.lengthCounter > 0) {
        value |= 0x04;
    }
    if (apu->noise.enabled && apu->noise.lengthCounter > 0) {
        value |= 0x08;
    }
    if (apu->dmc.bytesRemaining > 0) {
        value |= 0x10;
    }
    pthread_mutex_unlock(&apu->mutex);
    return value;
}

static void apu_quarter_frame(APU *apu) {
    pulse_tick_envelope(&apu->pulse1);
    pulse_tick_envelope(&apu->pulse2);
    noise_tick_envelope(&apu->noise);
    triangle_tick_linear(&apu->triangle);
}

static void apu_half_frame(APU *apu) {
    pulse_tick_length(&apu->pulse1);
    pulse_tick_length(&apu->pulse2);
    triangle_tick_length(&apu->triangle);
    noise_tick_length(&apu->noise);
    pulse_tick_sweep(&apu->pulse1);
    pulse_tick_sweep(&apu->pulse2);
}

static void apu_step_cycles_unlocked(APU *apu, int cycles) {
    for (int i = 0; i < cycles; i++) {
        apu->frameCounterCycle += 1;
        if (!apu->frameCounterMode) {
            if (apu->frameCounterCycle == 3729) {
                apu_quarter_frame(apu);
            } else if (apu->frameCounterCycle == 7457) {
                apu_quarter_frame(apu);
                apu_half_frame(apu);
            } else if (apu->frameCounterCycle == 11186) {
                apu_quarter_frame(apu);
            } else if (apu->frameCounterCycle == 14915) {
                apu_quarter_frame(apu);
                apu_half_frame(apu);
                apu->frameCounterCycle = 0;
            }
        } else {
            if (apu->frameCounterCycle == 3729) {
                apu_quarter_frame(apu);
            } else if (apu->frameCounterCycle == 7457) {
                apu_quarter_frame(apu);
                apu_half_frame(apu);
            } else if (apu->frameCounterCycle == 11186) {
                apu_quarter_frame(apu);
            } else if (apu->frameCounterCycle == 14915) {
                apu_quarter_frame(apu);
                apu_half_frame(apu);
            } else if (apu->frameCounterCycle == 18641) {
                apu->frameCounterCycle = 0;
            }
        }

        triangle_tick_timer(&apu->triangle);
        noise_tick_timer(&apu->noise);
        dmc_tick_timer(apu);
        dmc_fetch_sample(apu);
    }
}

void apu_step(APU *apu, int cycles) {
    pthread_mutex_lock(&apu->mutex);
    apu_step_cycles_unlocked(apu, cycles);
    pthread_mutex_unlock(&apu->mutex);
}

static float apu_next_sample_unlocked(APU *apu, double sample_rate) {
    double p1 = pulse_sample(&apu->pulse1, sample_rate);
    double p2 = pulse_sample(&apu->pulse2, sample_rate);
    double t = triangle_sample(&apu->triangle);
    double n = noise_sample(&apu->noise);
    double d = dmc_sample(&apu->dmc);

    double pulseOut = 0.0;
    if (p1 + p2 > 0.0) {
        pulseOut = 95.88 / ((8128.0 / (p1 + p2)) + 100.0);
    }
    double tndOut = 0.0;
    double tnd = (t / 8227.0) + (n / 12241.0) + (d / 22638.0);
    if (tnd > 0.0) {
        tndOut = 159.79 / ((1.0 / tnd) + 100.0);
    }
    double mixed = pulseOut + tndOut;
    double cutoff = 12000.0;
    double rc = 1.0 / (2.0 * 3.141592653589793 * cutoff);
    double dt = 1.0 / sample_rate;
    double alpha = dt / (rc + dt);
    apu->outputFilter += alpha * (mixed - apu->outputFilter);
    return (float)apu->outputFilter;
}

float apu_next_sample(APU *apu, double sample_rate) {
    float sample = 0.0f;
    pthread_mutex_lock(&apu->mutex);
    sample = apu_next_sample_unlocked(apu, sample_rate);
    pthread_mutex_unlock(&apu->mutex);
    return sample;
}

void apu_fill_buffer(APU *apu, double sample_rate, float *out, int count) {
    if (!out || count <= 0) {
        return;
    }
    double cyclesPerSample = apu_cpu_clock / sample_rate;
    pthread_mutex_lock(&apu->mutex);
    for (int i = 0; i < count; i++) {
        apu->sampleCycleRemainder += cyclesPerSample;
        int cycles = (int)apu->sampleCycleRemainder;
        if (cycles > 0) {
            apu_step_cycles_unlocked(apu, cycles);
            apu->sampleCycleRemainder -= (double)cycles;
        }
        out[i] = apu_next_sample_unlocked(apu, sample_rate);
    }
    pthread_mutex_unlock(&apu->mutex);
}
