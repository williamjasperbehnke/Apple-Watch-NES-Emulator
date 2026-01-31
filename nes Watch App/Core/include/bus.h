#ifndef NESC_BUS_H
#define NESC_BUS_H

#include "apu.h"
#include "controller.h"
#include "ppu.h"

struct CPU;

typedef struct Bus {
    struct CPU *cpu;
    PPU *ppu;
    APU *apu;
    Cartridge *cartridge;
    Controller controller;

    uint8_t cpuRam[2048];
    uint8_t prgRam[8192];
    uint8_t dataBus;

    bool irqPending;
    int stallCycles;

    bool dmaActive;
    uint8_t dmaPage;
    uint16_t dmaIndex;
    int dmaCycle;
    uint8_t dmaData;
} Bus;

uint8_t bus_cpu_read(Bus *bus, uint16_t addr);
uint8_t bus_cpu_read_opcode(Bus *bus, uint16_t addr);
void bus_cpu_write(Bus *bus, uint16_t addr, uint8_t data);

bool bus_is_irq_pending(Bus *bus);
void bus_ack_irq(Bus *bus);
void bus_tick(Bus *bus, int cycles);

void bus_request_stall(Bus *bus, int cycles);
bool bus_consume_stall(Bus *bus);

void bus_set_cpu_bus(Bus *bus, uint8_t value);

#endif
