#ifndef NESC_MAPPER_NROM_H
#define NESC_MAPPER_NROM_H

#include "types.h"

typedef struct {
    int prgBanks;
    int chrBanks;
} MapperNROM;

int mapper_nrom_cpu_map(const MapperNROM *mapper, uint16_t addr);
int mapper_nrom_ppu_map(const MapperNROM *mapper, uint16_t addr, bool write);

#endif
