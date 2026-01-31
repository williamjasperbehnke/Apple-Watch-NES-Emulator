#ifndef NESC_APU_H
#define NESC_APU_H

#include "types.h"
#include <pthread.h>

typedef uint8_t (*ApuReadFunc)(void *context, uint16_t addr);

typedef struct {
    uint8_t control;
    uint16_t timer;
    uint8_t lengthCounter;
    bool enabled;
    double phase;
    uint8_t envDivider;
    uint8_t envDecay;
    bool envStart;
    uint8_t sweepDivider;
    uint8_t sweepPeriod;
    uint8_t sweepShift;
    bool sweepEnabled;
    bool sweepNegate;
    bool sweepReload;
    bool sweepMute;
    bool sweepOnesComplement;
} PulseChannel;

typedef struct {
    uint8_t control;
    uint16_t timer;
    uint16_t timerCounter;
    uint8_t lengthCounter;
    bool enabled;
    uint8_t linearCounter;
    uint8_t linearReload;
    bool linearControl;
    bool linearReloadFlag;
    uint8_t sequencePos;
} TriangleChannel;

typedef struct {
    uint8_t control;
    uint16_t timer;
    uint16_t timerCounter;
    uint8_t lengthCounter;
    bool enabled;
    uint16_t lfsr;
    uint8_t envDivider;
    uint8_t envDecay;
    bool envStart;
} NoiseChannel;

typedef struct {
    uint8_t control;
    uint8_t directLoad;
    uint16_t sampleAddress;
    uint16_t sampleLength;
    uint16_t currentAddress;
    uint16_t bytesRemaining;
    uint8_t shiftRegister;
    uint8_t bitCount;
    uint8_t outputLevel;
    bool enabled;
    bool irqEnabled;
    bool loop;
    bool sampleBufferEmpty;
    uint8_t sampleBuffer;
    uint16_t timer;
    uint16_t timerCounter;
} DMCChannel;

typedef struct {
    PulseChannel pulse1;
    PulseChannel pulse2;
    TriangleChannel triangle;
    NoiseChannel noise;
    DMCChannel dmc;
    int frameCounterCycle;
    bool frameCounterMode;
    bool frameIrqInhibit;
    double outputFilter;
    double sampleCycleRemainder;
    pthread_mutex_t mutex;
    ApuReadFunc read;
    void *readContext;
} APU;

void apu_init(APU *apu);
void apu_reset(APU *apu);
void apu_set_read_callback(APU *apu, ApuReadFunc read, void *context);
void apu_cpu_write(APU *apu, uint16_t addr, uint8_t data);
uint8_t apu_read_status(APU *apu);
void apu_step(APU *apu, int cycles);
float apu_next_sample(APU *apu, double sample_rate);
void apu_fill_buffer(APU *apu, double sample_rate, float *out, int count);

#endif
