#ifndef VEIL_TOUCH_SPELL_ACTIONS_H
#define VEIL_TOUCH_SPELL_ACTIONS_H
#include "x86rt.h"
#include <stdbool.h>

typedef enum {
    VEIL_SPELL_PRESS,
    VEIL_SPELL_HOLD,
    VEIL_SPELL_RELEASE,
    VEIL_SPELL_CHAIN
} VeilSpellPhase;

uint32_t veil_spell_player(X86Ctx *ctx);
uint32_t veil_spell_hold_delay(X86Ctx *ctx, unsigned slot);
bool veil_spell_emit(X86Ctx *ctx, uint32_t player, unsigned slot, VeilSpellPhase phase);
#endif
