#ifndef NESC_PPU_H
#define NESC_PPU_H

#include "cartridge.h"

typedef struct {
    FrameBuffer frameBuffer;
    Cartridge *cartridge;
    Mirroring mirroring;
    uint8_t dataBus;
    uint8_t ctrl;
    uint8_t mask;
    uint8_t status;
    uint8_t oamAddr;
    uint8_t oam[256];
    uint8_t bgColorIndex[NES_WIDTH * NES_HEIGHT];
    uint8_t scrollX;
    uint8_t scrollY;
    bool addressLatch;
    uint16_t vramAddr;
    uint8_t readBuffer;
    int cycle;
    int scanline;
    bool frameComplete;
    bool nmiRequested;
    uint8_t nametableRam[2048];
    uint8_t paletteRam[32];
} PPU;

void ppu_connect_cartridge(PPU *ppu, Cartridge *cart);
void ppu_reset_frame(PPU *ppu);
uint8_t ppu_cpu_read(PPU *ppu, uint16_t addr);
void ppu_cpu_write(PPU *ppu, uint16_t addr, uint8_t data);
void ppu_tick(PPU *ppu);
void ppu_dma_write_oam(PPU *ppu, uint8_t data);

#endif
