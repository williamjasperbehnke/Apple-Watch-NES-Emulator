#ifndef NESC_MAPPER_CNROM_H
#define NESC_MAPPER_CNROM_H

#include "types.h"

typedef struct {
    uint8_t chrBank;
} MapperCNROM;

void cnrom_init(MapperCNROM *mapper);

#endif
