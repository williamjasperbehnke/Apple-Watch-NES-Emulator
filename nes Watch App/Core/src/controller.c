#include "../include/controller.h"

void controller_set_button(Controller *ctl, uint8_t button, bool pressed) {
    if (pressed) {
        ctl->state |= button;
    } else {
        ctl->state &= (uint8_t)~button;
    }
}

void controller_write(Controller *ctl, uint8_t data) {
    ctl->strobe = (data & 0x01) != 0;
    if (ctl->strobe) {
        ctl->shift = ctl->state;
    }
}

uint8_t controller_read(Controller *ctl) {
    if (ctl->strobe) {
        return (uint8_t)(ctl->state & 0x01);
    }
    uint8_t value = (uint8_t)(ctl->shift & 0x01);
    ctl->shift >>= 1;
    return value;
}
