#include "../include/cartridge.h"

#include <stdlib.h>
#include <string.h>

void cartridge_free(Cartridge *cart) {
    if (!cart) {
        return;
    }
    free(cart->prgROM);
    free(cart->chrROM);
    memset(cart, 0, sizeof(*cart));
}

bool cartridge_load(Cartridge *cart, const uint8_t *data, size_t size) {
    if (!data || size < 16) {
        return false;
    }
    if (data[0] != 0x4E || data[1] != 0x45 || data[2] != 0x53 || data[3] != 0x1A) {
        return false;
    }

    uint8_t prgBanks = data[4];
    uint8_t chrBanks = data[5];
    uint8_t flags6 = data[6];
    uint8_t flags7 = data[7];

    cart->mapperID = (uint8_t)((flags7 & 0xF0) | (flags6 >> 4));
    cart->mirroring = (flags6 & 0x01) == 0 ? MIRROR_HORIZONTAL : MIRROR_VERTICAL;

    size_t offset = 16;
    if (flags6 & 0x04) {
        offset += 512;
    }

    size_t prgSize = (size_t)prgBanks * 16 * 1024;
    size_t chrSize = (size_t)chrBanks * 8 * 1024;
    size_t prgStart = offset;
    size_t chrStart = prgStart + prgSize;

    if (size < chrStart + chrSize) {
        return false;
    }

    cart->prgROM = (uint8_t *)malloc(prgSize);
    if (!cart->prgROM) {
        return false;
    }
    memcpy(cart->prgROM, data + prgStart, prgSize);
    cart->prgSize = prgSize;

    if (chrSize > 0) {
        cart->chrROM = (uint8_t *)malloc(chrSize);
        if (!cart->chrROM) {
            return false;
        }
        memcpy(cart->chrROM, data + chrStart, chrSize);
        cart->chrSize = chrSize;
        cart->hasChrRam = false;
    } else {
        cart->chrROM = (uint8_t *)calloc(8 * 1024, 1);
        if (!cart->chrROM) {
            return false;
        }
        cart->chrSize = 8 * 1024;
        cart->hasChrRam = true;
    }

    if (cart->mapperID != 0) {
        return false;
    }

    cart->mapper.prgBanks = prgBanks;
    cart->mapper.chrBanks = chrBanks;
    return true;
}

bool cartridge_cpu_read(const Cartridge *cart, uint16_t addr, uint8_t *out) {
    int mapped = mapper_nrom_cpu_map(&cart->mapper, addr);
    if (mapped < 0) {
        return false;
    }
    *out = cart->prgROM[mapped];
    return true;
}

bool cartridge_cpu_write(const Cartridge *cart, uint16_t addr) {
    int mapped = mapper_nrom_cpu_map(&cart->mapper, addr);
    return mapped >= 0;
}

bool cartridge_ppu_read(const Cartridge *cart, uint16_t addr, uint8_t *out) {
    int mapped = mapper_nrom_ppu_map(&cart->mapper, addr, false);
    if (mapped < 0) {
        return false;
    }
    *out = cart->chrROM[mapped];
    return true;
}

bool cartridge_ppu_write(Cartridge *cart, uint16_t addr, uint8_t data) {
    int mapped = mapper_nrom_ppu_map(&cart->mapper, addr, true);
    if (mapped < 0) {
        return false;
    }
    if (cart->hasChrRam) {
        cart->chrROM[mapped] = data;
        return true;
    }
    return false;
}
