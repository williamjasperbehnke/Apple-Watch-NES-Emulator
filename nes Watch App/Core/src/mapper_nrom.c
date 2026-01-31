#include "../include/mapper_nrom.h"

int mapper_nrom_cpu_map(const MapperNROM *mapper, uint16_t addr) {
    if (addr < 0x8000) {
        return -1;
    }
    if (mapper->prgBanks > 1) {
        return (int)(addr & 0x7FFF);
    }
    return (int)(addr & 0x3FFF);
}

int mapper_nrom_ppu_map(const MapperNROM *mapper, uint16_t addr, bool write) {
    if (addr >= 0x2000) {
        return -1;
    }
    if (write && mapper->chrBanks != 0) {
        return -1;
    }
    return (int)addr;
}
