#include <assert.h>
#include <time.h>

#include "touch_spell_actions.h"
#include "guest_layout.h"

static uint64_t now;
static int test_clock(clockid_t clock, struct timespec *out)
{
    (void)clock;
    out->tv_sec = (time_t)(now / 1000);
    out->tv_nsec = (long)(now % 1000) * 1000000;
    return 0;
}

#define clock_gettime test_clock
#include "touch_spell_input.c"
#undef clock_gettime

static unsigned char guest[4096];
uintptr_t x86_mem_base = (uintptr_t)guest;
enum { TEST_PLAYER = 32 };
static bool playable = true;
static unsigned calls, original_updates, last_original_update;
static unsigned slots[16];
static VeilSpellPhase phases[16];

int veil_touch_spell_playable(void) { return playable; }
uint32_t veil_spell_player(X86Ctx *ctx) { (void)ctx; return TEST_PLAYER; }
uint32_t veil_spell_hold_delay(X86Ctx *ctx, unsigned slot) { (void)ctx; (void)slot; return 66; }
void f_00C15370(X86Ctx *ctx) { (void)ctx; ++original_updates; }

bool veil_spell_emit(X86Ctx *ctx, uint32_t player, unsigned slot, VeilSpellPhase phase)
{
    (void)ctx;
    assert(player == TEST_PLAYER && calls < sizeof(slots) / sizeof(slots[0]));
    /* The original update must finish first; press and release get distinct frames. */
    assert(original_updates > last_original_update);
    last_original_update = original_updates;
    slots[calls] = slot;
    phases[calls++] = phase;
    return true;
}

static void frame(uint64_t milliseconds)
{
    X86Ctx ctx = {0};
    now = milliseconds;
    unsigned before = original_updates;
    veil_spell_after_input(&ctx);
    assert(original_updates == before + 1);
}

int main(void)
{
    st32(TEST_PLAYER + PLAYER_ACTOR, 77);
    /* A short Cast before the HUD poll resolves this character's selected slot. */
    veil_spell_set_held(true);
    veil_spell_set_held(false);
    assert(veil_spell_take_request() == 11);
    veil_spell_accept_slot(3);
    frame(100);
    frame(171);
    assert(calls == 1 && slots[0] == 3 && phases[0] == VEIL_SPELL_PRESS);
    frame(178);
    assert(calls == 2 && phases[1] == VEIL_SPELL_RELEASE);

    veil_spell_tap_slot(8);
    assert(veil_spell_take_request() == 8);
    assert(veil_spell_take_request() == 0);  /* empty slot was never acknowledged */
    veil_spell_tap_slot(4);
    assert(veil_spell_take_request() == 4);
    veil_spell_accept_slot(2);  /* an unrelated acknowledgement is ignored */
    frame(280);
    assert(calls == 2);

    veil_spell_set_held(true);
    assert(veil_spell_take_request() == 11);
    veil_spell_accept_slot(4);
    frame(350);
    frame(415);
    assert(calls == 3 && slots[2] == 4);
    frame(416);
    frame(470);
    frame(482);
    assert(calls == 5 && phases[3] == VEIL_SPELL_HOLD && phases[4] == VEIL_SPELL_HOLD);
    veil_spell_set_held(false);
    frame(483);
    assert(calls == 6 && phases[5] == VEIL_SPELL_RELEASE);

    veil_spell_tap_slot(2);
    assert(veil_spell_take_request() == 2);
    veil_spell_accept_slot(2);
    frame(730);
    st32(TEST_PLAYER + PLAYER_ACTOR, 88);
    frame(815);
    assert(calls == 7); /* the new actor never receives the old actor's release */
    veil_spell_tap_slot(1);
    assert(veil_spell_take_request() == 1);
    veil_spell_accept_slot(1);
    playable = false;
    frame(840);
    assert(calls == 7);
    veil_spell_set_held(true);
    veil_spell_cancel();
    assert(veil_spell_take_request() == 0);
    return 0;
}
