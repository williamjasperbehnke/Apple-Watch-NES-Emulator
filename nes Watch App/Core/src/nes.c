#include "../include/nesc.h"

#include <stdlib.h>
#include <string.h>

#include "../include/nes_internal.h"

static uint8_t nes_bus_read(void *context, uint16_t addr) {
    return bus_cpu_read((Bus *)context, addr);
}

NESRef nes_create(void) {
    NES *nes = (NES *)calloc(1, sizeof(NES));
    if (!nes) {
        return NULL;
    }
    apu_init(&nes->apu);
    nes->bus.cpu = &nes->cpu;
    nes->bus.ppu = &nes->ppu;
    nes->bus.apu = &nes->apu;
    cpu_init(&nes->cpu);
    nes->cpu.bus = &nes->bus;
    apu_set_read_callback(&nes->apu, nes_bus_read, &nes->bus);
    return nes;
}

void nes_destroy(NESRef nes) {
    if (!nes) {
        return;
    }
    cartridge_free(&nes->cart);
    free(nes);
}

bool nes_load_rom(NESRef nes, const uint8_t *data, size_t size) {
    if (!nes) {
        return false;
    }
    cartridge_free(&nes->cart);
    memset(&nes->cart, 0, sizeof(nes->cart));
    if (!cartridge_load(&nes->cart, data, size)) {
        cartridge_free(&nes->cart);
        return false;
    }
    nes->bus.cartridge = &nes->cart;
    ppu_connect_cartridge(&nes->ppu, &nes->cart);
    nes->hasCart = true;
    nes_reset(nes);
    return true;
}

void nes_reset(NESRef nes) {
    if (!nes) {
        return;
    }
    apu_reset(&nes->apu);
    cpu_reset(&nes->cpu);
}

void nes_step_frame(NESRef nes) {
    if (!nes || !nes->hasCart) {
        return;
    }
    ppu_reset_frame(&nes->ppu);
    while (!nes->ppu.frameComplete) {
        int cycles = cpu_step(&nes->cpu);
        if (cycles > 0) {
            for (int i = 0; i < cycles * 3; i++) {
                ppu_tick(&nes->ppu);
                if (nes->ppu.nmiRequested) {
                    cpu_nmi(&nes->cpu);
                }
            }
        }
    }
}

const uint32_t *nes_framebuffer(NESRef nes) {
    if (!nes) {
        return NULL;
    }
    return nes->ppu.frameBuffer.pixels;
}

int nes_framebuffer_width(void) { return NES_WIDTH; }
int nes_framebuffer_height(void) { return NES_HEIGHT; }

void nes_set_button(NESRef nes, uint8_t button, bool pressed) {
    if (!nes) {
        return;
    }
    controller_set_button(&nes->bus.controller, button, pressed);
}

float nes_apu_next_sample(NESRef nes, double sample_rate) {
    if (!nes) {
        return 0.0f;
    }
    return apu_next_sample(&nes->apu, sample_rate);
}

void nes_apu_fill_buffer(NESRef nes, double sample_rate, float *out, int count) {
    if (!nes) {
        return;
    }
    apu_fill_buffer(&nes->apu, sample_rate, out, count);
}
