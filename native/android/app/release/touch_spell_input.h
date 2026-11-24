#ifndef VEIL_TOUCH_SPELL_INPUT_H
#define VEIL_TOUCH_SPELL_INPUT_H
#include <stdbool.h>
void veil_spell_set_held(bool held);
void veil_spell_tap_slot(unsigned slot);
unsigned veil_spell_take_request(void);
void veil_spell_accept_slot(unsigned slot);
void veil_spell_cancel(void);
#endif
