#ifndef NESC_CONTROLLER_H
#define NESC_CONTROLLER_H

#include "types.h"

typedef struct {
    uint8_t state;
    uint8_t shift;
    bool strobe;
} Controller;

void controller_set_button(Controller *ctl, uint8_t button, bool pressed);
void controller_write(Controller *ctl, uint8_t data);
uint8_t controller_read(Controller *ctl);

#endif
