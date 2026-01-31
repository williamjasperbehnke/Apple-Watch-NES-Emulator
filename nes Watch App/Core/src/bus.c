#include "../include/bus.h"

static uint8_t bus_cpu_read_internal(Bus *bus, uint16_t addr) {
    if (bus->cartridge) {
        uint8_t cartData = 0;
        if (cartridge_cpu_read(bus->cartridge, addr, &cartData)) {
            bus->dataBus = cartData;
            return cartData;
        }
    }

    if (addr <= 0x1FFF) {
        uint8_t value = bus->cpuRam[addr & 0x07FF];
        bus->dataBus = value;
        return value;
    }
    if (addr >= 0x6000 && addr <= 0x7FFF) {
        uint8_t value = bus->prgRam[addr & 0x1FFF];
        bus->dataBus = value;
        return value;
    }
    if (addr <= 0x3FFF) {
        uint16_t reg = (uint16_t)(0x2000 + (addr & 0x0007));
        uint8_t value = bus->ppu ? ppu_cpu_read(bus->ppu, reg) : bus->dataBus;
        bus->dataBus = value;
        return value;
    }
    if (addr == 0x4016) {
        uint8_t value = controller_read(&bus->controller);
        uint8_t result = (uint8_t)((bus->dataBus & 0xE0) | (value & 0x01));
        bus->dataBus = result;
        return result;
    }
    if (addr == 0x4017) {
        uint8_t value = bus->dataBus;
        bus->dataBus = value;
        return value;
    }
    if (addr == 0x4015) {
        uint8_t value = bus->apu ? apu_read_status(bus->apu) : bus->dataBus;
        bus->dataBus = value;
        return value;
    }
    return bus->dataBus;
}

uint8_t bus_cpu_read(Bus *bus, uint16_t addr) {
    return bus_cpu_read_internal(bus, addr);
}

uint8_t bus_cpu_read_opcode(Bus *bus, uint16_t addr) {
    return bus_cpu_read_internal(bus, addr);
}

static void bus_start_dma(Bus *bus, uint8_t page) {
    bus->dmaActive = true;
    bus->dmaPage = page;
    bus->dmaIndex = 0;
    bus->dmaCycle = 0;
}

static void bus_step_dma(Bus *bus) {
    if (!bus->dmaActive) {
        return;
    }
    if (bus->dmaCycle % 2 == 0) {
        uint16_t addr = (uint16_t)(bus->dmaPage << 8) | bus->dmaIndex;
        bus->dmaData = bus_cpu_read(bus, addr);
    } else {
        if (bus->ppu) {
            ppu_dma_write_oam(bus->ppu, bus->dmaData);
        }
        bus->dmaIndex += 1;
        if (bus->dmaIndex == 256) {
            bus->dmaActive = false;
        }
    }
    bus->dmaCycle += 1;
}

void bus_request_stall(Bus *bus, int cycles) {
    if (cycles > bus->stallCycles) {
        bus->stallCycles = cycles;
    }
}

bool bus_consume_stall(Bus *bus) {
    if (bus->stallCycles > 0) {
        bus->stallCycles -= 1;
        bus_step_dma(bus);
        return true;
    }
    return false;
}

void bus_cpu_write(Bus *bus, uint16_t addr, uint8_t data) {
    bus->dataBus = data;

    if (bus->cartridge && cartridge_cpu_write(bus->cartridge, addr)) {
        if (bus->cartridge->mapperType == MAPPER_MMC1 && addr >= 0x8000) {
            if (data & 0x80) {
                mmc1_reset(&bus->cartridge->mapperMmc1);
                bus->cartridge->mapperMmc1.control |= 0x0C;
            } else {
                MapperMMC1 *m = &bus->cartridge->mapperMmc1;
                m->shiftReg = (uint8_t)((m->shiftReg >> 1) | ((data & 0x01) << 4));
                m->shiftCount += 1;
                if (m->shiftCount == 5) {
                    uint8_t value = m->shiftReg;
                    uint16_t region = (addr >> 13) & 0x03;
                    if (region == 0) {
                        m->control = value;
                        uint8_t mirror = value & 0x03;
                        if (mirror == 3) {
                            bus->cartridge->mirroring = MIRROR_HORIZONTAL;
                        } else {
                            bus->cartridge->mirroring = MIRROR_VERTICAL;
                        }
                    } else if (region == 1) {
                        m->chrBank0 = value;
                    } else if (region == 2) {
                        m->chrBank1 = value;
                    } else {
                        m->prgBank = value;
                    }
                    m->shiftReg = 0x10;
                    m->shiftCount = 0;
                }
            }
        } else if (bus->cartridge->mapperType == MAPPER_CNROM && addr >= 0x8000) {
            int chrBankCount = (int)(bus->cartridge->chrSize / (8 * 1024));
            uint8_t bank = data;
            if (chrBankCount > 0) {
                if ((chrBankCount & (chrBankCount - 1)) == 0) {
                    bank = (uint8_t)(bank & (uint8_t)(chrBankCount - 1));
                } else {
                    bank = (uint8_t)(bank % chrBankCount);
                }
            } else {
                bank = 0;
            }
            bus->cartridge->mapperCnrom.chrBank = bank;
        }
        return;
    }

    if (addr <= 0x1FFF) {
        bus->cpuRam[addr & 0x07FF] = data;
        return;
    }
    if (addr >= 0x6000 && addr <= 0x7FFF) {
        bus->prgRam[addr & 0x1FFF] = data;
        return;
    }
    if (addr <= 0x3FFF) {
        if (bus->ppu) {
            ppu_cpu_write(bus->ppu, (uint16_t)(0x2000 + (addr & 0x0007)), data);
        }
        return;
    }
    if (addr == 0x4014) {
        bus_start_dma(bus, data);
        return;
    }
    if (addr == 0x4016) {
        controller_write(&bus->controller, data);
        return;
    }
    if ((addr >= 0x4000 && addr <= 0x4013) || addr == 0x4015 || addr == 0x4017) {
        if (bus->apu) {
            apu_cpu_write(bus->apu, addr, data);
        }
        if (addr == 0x4017) {
            bool irqEnabled = (data & 0x40) == 0;
            bus->irqPending = irqEnabled;
        }
        return;
    }
}

bool bus_is_irq_pending(Bus *bus) {
    return bus->irqPending;
}

void bus_ack_irq(Bus *bus) {
    bus->irqPending = false;
}

void bus_tick(Bus *bus, int cycles) {
    if (bus->apu) {
        apu_step(bus->apu, cycles);
    }
}

void bus_set_cpu_bus(Bus *bus, uint8_t value) {
    bus->dataBus = value;
}
