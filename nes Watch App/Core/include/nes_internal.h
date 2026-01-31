#ifndef NESC_INTERNAL_H
#define NESC_INTERNAL_H

#include "apu.h"
#include "bus.h"
#include "cartridge.h"
#include "cpu.h"
#include "ppu.h"

typedef struct NES {
    Bus bus;
    CPU cpu;
    PPU ppu;
    APU apu;
    Cartridge cart;
    bool hasCart;
} NES;

#endif
