#ifndef NESC_H
#define NESC_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NES NES;
typedef NES *NESRef;

NESRef nes_create(void);
void nes_destroy(NESRef nes);

bool nes_load_rom(NESRef nes, const uint8_t *data, size_t size);
void nes_reset(NESRef nes);
void nes_step_frame(NESRef nes);

const uint32_t *nes_framebuffer(NESRef nes);
int nes_framebuffer_width(void);
int nes_framebuffer_height(void);

void nes_set_button(NESRef nes, uint8_t button, bool pressed);

float nes_apu_next_sample(NESRef nes, double sample_rate);
void nes_apu_fill_buffer(NESRef nes, double sample_rate, float *out, int count);

#ifdef __cplusplus
}
#endif

#endif
