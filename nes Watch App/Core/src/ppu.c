#include "../include/ppu.h"

static const uint32_t nes_palette[64] = {
    0xFF7C7C7C, 0xFF0000FC, 0xFF0000BC, 0xFF4428BC, 0xFF940084, 0xFFA80020, 0xFFA81000, 0xFF881400,
    0xFF503000, 0xFF007800, 0xFF006800, 0xFF005800, 0xFF004058, 0xFF000000, 0xFF000000, 0xFF000000,
    0xFFBCBCBC, 0xFF0078F8, 0xFF0058F8, 0xFF6844FC, 0xFFD800CC, 0xFFE40058, 0xFFF83800, 0xFFE45C10,
    0xFFAC7C00, 0xFF00B800, 0xFF00A800, 0xFF00A844, 0xFF008888, 0xFF000000, 0xFF000000, 0xFF000000,
    0xFFF8F8F8, 0xFF3CBCFC, 0xFF6888FC, 0xFF9878F8, 0xFFF878F8, 0xFFF85898, 0xFFF87858, 0xFFFCA044,
    0xFFF8B800, 0xFFB8F818, 0xFF58D854, 0xFF58F898, 0xFF00E8D8, 0xFF787878, 0xFF000000, 0xFF000000,
    0xFFFCFCFC, 0xFFA4E4FC, 0xFFB8B8F8, 0xFFD8B8F8, 0xFFF8B8F8, 0xFFF8A4C0, 0xFFF0D0B0, 0xFFFCE0A8,
    0xFFF8D878, 0xFFD8F878, 0xFFB8F8B8, 0xFFB8F8D8, 0xFF00FCFC, 0xFFF8D8F8, 0xFF000000, 0xFF000000
};

static uint8_t ppu_read_memory(PPU *ppu, uint16_t addr);
static void ppu_write_memory(PPU *ppu, uint16_t addr, uint8_t data);

static int ppu_mirror_nametable(PPU *ppu, uint16_t addr) {
    int offset = (int)(addr & 0x0FFF);
    Mirroring mirroring = ppu->cartridge ? ppu->cartridge->mirroring : ppu->mirroring;
    if (mirroring == MIRROR_VERTICAL) {
        return offset & 0x07FF;
    }
    int table = (offset / 0x400) & 0x03;
    int index = offset & 0x03FF;
    if (table == 0 || table == 1) {
        return index;
    }
    return 0x400 + index;
}

static int ppu_mirror_palette(uint16_t addr) {
    int index = (int)(addr & 0x001F);
    if (index == 0x10) index = 0x00;
    if (index == 0x14) index = 0x04;
    if (index == 0x18) index = 0x08;
    if (index == 0x1C) index = 0x0C;
    return index;
}

static uint32_t ppu_palette_color(PPU *ppu, int palette, int color) {
    uint16_t indexAddr = (color == 0) ? 0x3F00 : (uint16_t)(0x3F00 + palette * 4 + color);
    uint8_t paletteIndex = (uint8_t)(ppu_read_memory(ppu, indexAddr) & 0x3F);
    return nes_palette[paletteIndex % 64];
}

static uint32_t ppu_sprite_palette_color(PPU *ppu, int palette, int color) {
    uint16_t indexAddr = (uint16_t)(0x3F10 + palette * 4 + color);
    uint8_t paletteIndex = (uint8_t)(ppu_read_memory(ppu, indexAddr) & 0x3F);
    return nes_palette[paletteIndex % 64];
}

static void ppu_render_background_scanline(PPU *ppu, int y) {
    int width = NES_WIDTH;
    bool showBackground = (ppu->mask & 0x08) != 0;
    bool showLeftBackground = (ppu->mask & 0x02) != 0;
    uint16_t patternBase = (ppu->ctrl & 0x10) != 0 ? 0x1000 : 0x0000;
    int baseNTX = (ppu->ctrl & 0x01) != 0 ? 1 : 0;
    int baseNTY = (ppu->ctrl & 0x02) != 0 ? 1 : 0;

    int scrolledY = (y + (int)ppu->scrollY) & 0x1FF;
    int tileY = (scrolledY / 8) % 30;
    int fineY = scrolledY % 8;
    int ntY = ((scrolledY / 240) + baseNTY) & 0x01;

    for (int x = 0; x < width; x++) {
        if (!showBackground || (x < 8 && !showLeftBackground)) {
            uint32_t pixelColor = ppu_palette_color(ppu, 0, 0);
            ppu->frameBuffer.pixels[y * width + x] = pixelColor;
            ppu->bgColorIndex[y * width + x] = 0;
            continue;
        }

        int scrolledX = (x + (int)ppu->scrollX) & 0x1FF;
        int tileX = (scrolledX / 8) % 32;
        int fineX = scrolledX % 8;
        int ntX = ((scrolledX / 256) + baseNTX) & 0x01;
        uint16_t baseNameTable = (uint16_t)(0x2000 + ((ntY << 1) | ntX) * 0x400);
        uint16_t nameAddr = (uint16_t)(baseNameTable + tileY * 32 + tileX);
        uint8_t tileId = ppu_read_memory(ppu, nameAddr);
        uint16_t patternAddr = (uint16_t)(patternBase + (uint16_t)tileId * 16 + (uint16_t)fineY);
        uint8_t plane0 = ppu_read_memory(ppu, patternAddr);
        uint8_t plane1 = ppu_read_memory(ppu, patternAddr + 8);
        int bit = 7 - fineX;
        int color = ((plane1 >> bit) & 0x01) << 1 | ((plane0 >> bit) & 0x01);

        uint16_t attrAddr = (uint16_t)(baseNameTable + 0x03C0 + (tileY / 4) * 8 + (tileX / 4));
        uint8_t attr = ppu_read_memory(ppu, attrAddr);
        int quadrantX = (tileX % 4) / 2;
        int quadrantY = (tileY % 4) / 2;
        int shift = (quadrantY * 2 + quadrantX) * 2;
        int palette = (attr >> shift) & 0x03;

        uint32_t pixelColor = ppu_palette_color(ppu, palette, color);
        ppu->frameBuffer.pixels[y * width + x] = pixelColor;
        ppu->bgColorIndex[y * width + x] = (uint8_t)color;
    }
}

static void ppu_render_sprites_scanline(PPU *ppu, int y) {
    bool showSprites = (ppu->mask & 0x10) != 0;
    bool showLeftSprites = (ppu->mask & 0x04) != 0;
    if (!showSprites) {
        return;
    }

    int width = NES_WIDTH;
    int spriteHeight = (ppu->ctrl & 0x20) != 0 ? 16 : 8;
    uint16_t spriteTable = (ppu->ctrl & 0x08) != 0 ? 0x1000 : 0x0000;

    for (int i = 63; i >= 0; i--) {
        int base = i * 4;
        int spriteY = (int)ppu->oam[base] + 1;
        uint8_t tileId = ppu->oam[base + 1];
        uint8_t attr = ppu->oam[base + 2];
        int spriteX = (int)ppu->oam[base + 3];

        if (y < spriteY || y >= spriteY + spriteHeight) {
            continue;
        }

        int palette = attr & 0x03;
        bool priorityBehind = (attr & 0x20) != 0;
        bool flipH = (attr & 0x40) != 0;
        bool flipV = (attr & 0x80) != 0;

        int row = y - spriteY;
        int spriteRow = flipV ? (spriteHeight - 1 - row) : row;
        uint16_t tileIndex;
        uint16_t patternBase;
        int fineY;

        if (spriteHeight == 16) {
            uint16_t table = (tileId & 0x01) != 0 ? 0x1000 : 0x0000;
            patternBase = table;
            uint16_t baseTile = (uint16_t)(tileId & 0xFE);
            tileIndex = (uint16_t)(baseTile + (spriteRow / 8));
            fineY = spriteRow % 8;
        } else {
            patternBase = spriteTable;
            tileIndex = tileId;
            fineY = spriteRow;
        }

        uint16_t patternAddr = (uint16_t)(patternBase + tileIndex * 16 + fineY);
        uint8_t plane0 = ppu_read_memory(ppu, patternAddr);
        uint8_t plane1 = ppu_read_memory(ppu, patternAddr + 8);

        for (int col = 0; col < 8; col++) {
            int x = spriteX + col;
            if (x < 0 || x >= width) {
                continue;
            }
            if (x < 8 && !showLeftSprites) {
                continue;
            }

            int fineX = flipH ? col : (7 - col);
            int bit = fineX;
            int color = ((plane1 >> bit) & 0x01) << 1 | ((plane0 >> bit) & 0x01);
            if (color == 0) {
                continue;
            }

            uint8_t bgIndex = ppu->bgColorIndex[y * width + x];
            if (priorityBehind && bgIndex != 0) {
                continue;
            }

            if (i == 0 && (ppu->mask & 0x08) != 0) {
                ppu->status |= 0x40;
            }

            uint32_t pixelColor = ppu_sprite_palette_color(ppu, palette, color);
            ppu->frameBuffer.pixels[y * width + x] = pixelColor;
        }
    }
}

void ppu_connect_cartridge(PPU *ppu, Cartridge *cart) {
    ppu->cartridge = cart;
    ppu->mirroring = cart->mirroring;
}

void ppu_reset_frame(PPU *ppu) {
    ppu->frameComplete = false;
}

uint8_t ppu_cpu_read(PPU *ppu, uint16_t addr) {
    switch (addr) {
        case 0x2002: {
            uint8_t value = (uint8_t)((ppu->status & 0xE0) | (ppu->dataBus & 0x1F));
            ppu->status &= 0x1F;
            ppu->addressLatch = false;
            ppu->dataBus = value;
            return value;
        }
        case 0x2004: {
            uint8_t value = ppu->oam[ppu->oamAddr];
            ppu->dataBus = value;
            return value;
        }
        case 0x2007: {
            uint8_t value;
            if (ppu->vramAddr >= 0x3F00) {
                value = ppu_read_memory(ppu, ppu->vramAddr);
                ppu->readBuffer = ppu_read_memory(ppu, (uint16_t)(ppu->vramAddr - 0x1000));
            } else {
                value = ppu->readBuffer;
                ppu->readBuffer = ppu_read_memory(ppu, ppu->vramAddr);
            }
            ppu->vramAddr += (ppu->ctrl & 0x04) != 0 ? 32 : 1;
            ppu->dataBus = value;
            return value;
        }
        default:
            return ppu->dataBus;
    }
}

void ppu_cpu_write(PPU *ppu, uint16_t addr, uint8_t data) {
    ppu->dataBus = data;
    switch (addr) {
        case 0x2000:
            ppu->ctrl = data;
            break;
        case 0x2001:
            ppu->mask = data;
            break;
        case 0x2003:
            ppu->oamAddr = data;
            break;
        case 0x2004:
            ppu->oam[ppu->oamAddr] = data;
            ppu->oamAddr += 1;
            break;
        case 0x2005:
            if (!ppu->addressLatch) {
                ppu->scrollX = data;
                ppu->addressLatch = true;
            } else {
                ppu->scrollY = data;
                ppu->addressLatch = false;
            }
            break;
        case 0x2006:
            if (!ppu->addressLatch) {
                ppu->vramAddr = (uint16_t)(data << 8);
                ppu->addressLatch = true;
            } else {
                ppu->vramAddr = (uint16_t)((ppu->vramAddr & 0xFF00) | data);
                ppu->addressLatch = false;
            }
            break;
        case 0x2007:
            ppu_write_memory(ppu, ppu->vramAddr, data);
            ppu->vramAddr += (ppu->ctrl & 0x04) != 0 ? 32 : 1;
            break;
        default:
            break;
    }
}

void ppu_tick(PPU *ppu) {
    ppu->nmiRequested = false;
    if (ppu->scanline == 241 && ppu->cycle == 1) {
        ppu->status |= 0x80;
        if ((ppu->ctrl & 0x80) != 0) {
            ppu->nmiRequested = true;
        }
    }

    if (ppu->scanline == 261 && ppu->cycle == 1) {
        ppu->status &= 0x1F;
    }

    if (ppu->cycle == 0 && ppu->scanline >= 0 && ppu->scanline < 240) {
        ppu_render_background_scanline(ppu, ppu->scanline);
        ppu_render_sprites_scanline(ppu, ppu->scanline);
    }

    ppu->cycle += 1;
    if (ppu->cycle >= 341) {
        ppu->cycle = 0;
        ppu->scanline += 1;
        if (ppu->scanline >= 262) {
            ppu->scanline = 0;
            ppu->frameComplete = true;
        }
    }
}

static uint8_t ppu_read_memory(PPU *ppu, uint16_t addr) {
    uint16_t address = addr & 0x3FFF;
    if (address < 0x2000) {
        uint8_t value = 0;
        if (ppu->cartridge && cartridge_ppu_read(ppu->cartridge, address, &value)) {
            return value;
        }
        return 0;
    }
    if (address < 0x3F00) {
        int index = ppu_mirror_nametable(ppu, address);
        return ppu->nametableRam[index];
    }
    int paletteIndex = ppu_mirror_palette(address);
    return ppu->paletteRam[paletteIndex];
}

static void ppu_write_memory(PPU *ppu, uint16_t addr, uint8_t data) {
    uint16_t address = addr & 0x3FFF;
    if (address < 0x2000) {
        if (ppu->cartridge) {
            (void)cartridge_ppu_write(ppu->cartridge, address, data);
        }
        return;
    }
    if (address < 0x3F00) {
        int index = ppu_mirror_nametable(ppu, address);
        ppu->nametableRam[index] = data;
        return;
    }
    int paletteIndex = ppu_mirror_palette(address);
    ppu->paletteRam[paletteIndex] = data;
}

void ppu_dma_write_oam(PPU *ppu, uint8_t data) {
    ppu->oam[ppu->oamAddr] = data;
    ppu->oamAddr += 1;
}
