#ifndef NESC_APU_H
#define NESC_APU_H

#include "types.h"

typedef struct {
    uint8_t control;
    uint16_t timer;
    uint8_t lengthCounter;
    bool enabled;
    double phase;
} PulseChannel;

typedef struct {
    PulseChannel pulse1;
    PulseChannel pulse2;
    int frameCounterCycles;
} APU;

void apu_init(APU *apu);
void apu_cpu_write(APU *apu, uint16_t addr, uint8_t data);
uint8_t apu_read_status(APU *apu);
void apu_step(APU *apu, int cycles);
float apu_next_sample(APU *apu, double sample_rate);

#endif
