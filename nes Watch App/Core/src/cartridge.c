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

    if (cart->mapperID == 0) {
        cart->mapperType = MAPPER_NROM;
        cart->mapperNrom.prgBanks = prgBanks;
        cart->mapperNrom.chrBanks = chrBanks;
    } else if (cart->mapperID == 1) {
        cart->mapperType = MAPPER_MMC1;
        mmc1_init(&cart->mapperMmc1);
    } else if (cart->mapperID == 3) {
        cart->mapperType = MAPPER_CNROM;
        cnrom_init(&cart->mapperCnrom);
    } else {
        return false;
    }
    return true;
}

bool cartridge_cpu_read(const Cartridge *cart, uint16_t addr, uint8_t *out) {
    if (cart->mapperType == MAPPER_NROM) {
        int mapped = nrom_cpu_map(&cart->mapperNrom, addr);
        if (mapped < 0) {
            return false;
        }
        *out = cart->prgROM[mapped];
        return true;
    }
    if (cart->mapperType == MAPPER_MMC1) {
        if (addr < 0x8000) {
            return false;
        }
        uint8_t prgMode = (cart->mapperMmc1.control >> 2) & 0x03;
        int prgBankCount = (int)(cart->prgSize / (16 * 1024));
        int bank = cart->mapperMmc1.prgBank & 0x0F;
        uint32_t mapped = 0;

        switch (prgMode) {
            case 0:
            case 1: {
                int bank32 = (bank & 0x0E);
                if (addr < 0xC000) {
                    mapped = (uint32_t)bank32 * 16 * 1024 + (addr - 0x8000);
                } else {
                    mapped = (uint32_t)(bank32 + 1) * 16 * 1024 + (addr - 0xC000);
                }
                break;
            }
            case 2: {
                if (addr < 0xC000) {
                    mapped = (uint32_t)(0 * 16 * 1024 + (addr - 0x8000));
                } else {
                    int b = bank % prgBankCount;
                    mapped = (uint32_t)b * 16 * 1024 + (addr - 0xC000);
                }
                break;
            }
            case 3:
            default: {
                if (addr < 0xC000) {
                    int b = bank % prgBankCount;
                    mapped = (uint32_t)b * 16 * 1024 + (addr - 0x8000);
                } else {
                    mapped = (uint32_t)(prgBankCount - 1) * 16 * 1024 + (addr - 0xC000);
                }
                break;
            }
        }

        if (mapped >= cart->prgSize) {
            return false;
        }
        *out = cart->prgROM[mapped];
        return true;
    }
    if (cart->mapperType == MAPPER_CNROM) {
        if (addr < 0x8000) {
            return false;
        }
        uint32_t mapped = 0;
        if (cart->prgSize == 16 * 1024) {
            mapped = (uint32_t)(addr & 0x3FFF);
        } else {
            mapped = (uint32_t)(addr - 0x8000);
        }
        if (mapped >= cart->prgSize) {
            return false;
        }
        *out = cart->prgROM[mapped];
        return true;
    }
    return false;
}

bool cartridge_cpu_write(const Cartridge *cart, uint16_t addr) {
    if (cart->mapperType == MAPPER_NROM) {
        int mapped = nrom_cpu_map(&cart->mapperNrom, addr);
        return mapped >= 0;
    }
    if (cart->mapperType == MAPPER_MMC1) {
        return addr >= 0x8000;
    }
    if (cart->mapperType == MAPPER_CNROM) {
        return addr >= 0x8000;
    }
    return false;
}

bool cartridge_ppu_read(const Cartridge *cart, uint16_t addr, uint8_t *out) {
    if (cart->mapperType == MAPPER_NROM) {
        int mapped = nrom_ppu_map(&cart->mapperNrom, addr, false);
        if (mapped < 0) {
            return false;
        }
        *out = cart->chrROM[mapped];
        return true;
    }
    if (cart->mapperType == MAPPER_MMC1) {
        if (addr >= 0x2000) {
            return false;
        }
        uint8_t chrMode = (cart->mapperMmc1.control >> 4) & 0x01;
        uint32_t mapped = 0;
        if (chrMode == 0) {
            uint32_t bank = (uint32_t)(cart->mapperMmc1.chrBank0 & 0x1E);
            mapped = bank * 4 * 1024 + addr;
        } else {
            if (addr < 0x1000) {
                mapped = (uint32_t)cart->mapperMmc1.chrBank0 * 4 * 1024 + addr;
            } else {
                mapped = (uint32_t)cart->mapperMmc1.chrBank1 * 4 * 1024 + (addr - 0x1000);
            }
        }
        if (mapped >= cart->chrSize) {
            return false;
        }
        *out = cart->chrROM[mapped];
        return true;
    }
    if (cart->mapperType == MAPPER_CNROM) {
        if (addr >= 0x2000) {
            return false;
        }
        int chrBankCount = (int)(cart->chrSize / (8 * 1024));
        if (chrBankCount <= 0) {
            return false;
        }
        uint8_t bank = cart->mapperCnrom.chrBank;
        if ((chrBankCount & (chrBankCount - 1)) == 0) {
            bank = (uint8_t)(bank & (uint8_t)(chrBankCount - 1));
        } else {
            bank = (uint8_t)(bank % chrBankCount);
        }
        uint32_t mapped = (uint32_t)bank * 8 * 1024 + addr;
        if (mapped >= cart->chrSize) {
            return false;
        }
        *out = cart->chrROM[mapped];
        return true;
    }
    return false;
}

bool cartridge_ppu_write(Cartridge *cart, uint16_t addr, uint8_t data) {
    if (cart->mapperType == MAPPER_NROM) {
        int mapped = nrom_ppu_map(&cart->mapperNrom, addr, true);
        if (mapped < 0) {
            return false;
        }
        if (cart->hasChrRam) {
            cart->chrROM[mapped] = data;
            return true;
        }
        return false;
    }
    if (cart->mapperType == MAPPER_MMC1) {
        if (!cart->hasChrRam) {
            return false;
        }
        if (addr >= 0x2000) {
            return false;
        }
        uint8_t chrMode = (cart->mapperMmc1.control >> 4) & 0x01;
        uint32_t mapped = 0;
        if (chrMode == 0) {
            uint32_t bank = (uint32_t)(cart->mapperMmc1.chrBank0 & 0x1E);
            mapped = bank * 4 * 1024 + addr;
        } else {
            if (addr < 0x1000) {
                mapped = (uint32_t)cart->mapperMmc1.chrBank0 * 4 * 1024 + addr;
            } else {
                mapped = (uint32_t)cart->mapperMmc1.chrBank1 * 4 * 1024 + (addr - 0x1000);
            }
        }
        if (mapped >= cart->chrSize) {
            return false;
        }
        cart->chrROM[mapped] = data;
        return true;
    }
    if (cart->mapperType == MAPPER_CNROM) {
        if (!cart->hasChrRam) {
            return false;
        }
        if (addr >= 0x2000) {
            return false;
        }
        int chrBankCount = (int)(cart->chrSize / (8 * 1024));
        if (chrBankCount <= 0) {
            return false;
        }
        uint8_t bank = cart->mapperCnrom.chrBank;
        if ((chrBankCount & (chrBankCount - 1)) == 0) {
            bank = (uint8_t)(bank & (uint8_t)(chrBankCount - 1));
        } else {
            bank = (uint8_t)(bank % chrBankCount);
        }
        uint32_t mapped = (uint32_t)bank * 8 * 1024 + addr;
        if (mapped >= cart->chrSize) {
            return false;
        }
        cart->chrROM[mapped] = data;
        return true;
    }
    return false;
}
