#ifndef NESC_TYPES_H
#define NESC_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NES_WIDTH 256
#define NES_HEIGHT 240

typedef enum {
    MIRROR_HORIZONTAL = 0,
    MIRROR_VERTICAL = 1
} Mirroring;

typedef struct {
    uint32_t pixels[NES_WIDTH * NES_HEIGHT];
} FrameBuffer;

#endif
