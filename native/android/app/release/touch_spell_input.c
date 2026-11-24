#include "touch_spell_input.h"
#include "touch_spell_actions.h"
#include "guest_layout.h"
#include <pthread.h>
#include <stdint.h>
#include <stdatomic.h>
#include <time.h>

extern int veil_touch_spell_playable(void);
extern void f_00C15370(X86Ctx *);

typedef struct {
    unsigned slot;
    unsigned finger_press;
} SpellRequest;

/* Numbered requests originate in the UI. The original HUD accepts them or
 * rejects them for this character. Only verified requests reach the game
 * update thread. A finger serial pairs a held Cast with its own release. */
static pthread_mutex_t request_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned queued_slot, finger_serial, queued_finger;
static bool finger_held;
static SpellRequest awaiting_hud, armed, active;
static uint32_t active_owner;
static uint64_t active_since;
static uint64_t next_hold_at;
static atomic_bool needs_input_tick;
static uint32_t active_hold_ms;
static bool emitting;

static uint64_t clock_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

void veil_spell_tap_slot(unsigned slot)
{
    if (slot < 1 || slot > 10) return;
    pthread_mutex_lock(&request_lock);
    queued_slot = slot;
    pthread_mutex_unlock(&request_lock);
}

void veil_spell_set_held(bool held)
{
    pthread_mutex_lock(&request_lock);
    if (held && !finger_held) {
        if (!++finger_serial) ++finger_serial;
        queued_finger = finger_serial;
    }
    finger_held = held;
    pthread_mutex_unlock(&request_lock);
}

void veil_spell_cancel(void)
{
    pthread_mutex_lock(&request_lock);
    queued_slot = queued_finger = 0;
    awaiting_hud = armed = (SpellRequest){0};
    finger_held = false;
    atomic_store(&needs_input_tick, active.slot || emitting);
    pthread_mutex_unlock(&request_lock);
}

unsigned veil_spell_take_request(void)
{
    pthread_mutex_lock(&request_lock);
    /* A missing synchronous acknowledgement means the last slot was empty
     * or unavailable. Never re-use an acknowledged spell from an older save. */
    awaiting_hud = (SpellRequest){0};
    if (!armed.slot && !active.slot && !emitting) {
        if (queued_slot) {
            awaiting_hud.slot = queued_slot;
            queued_slot = 0;
        } else if (queued_finger) {
            awaiting_hud = (SpellRequest){11, queued_finger};
            queued_finger = 0;
        }
    }
    unsigned number = awaiting_hud.slot;
    pthread_mutex_unlock(&request_lock);
    return number;
}

void veil_spell_accept_slot(unsigned slot)
{
    if (slot < 1 || slot > 10) return;
    pthread_mutex_lock(&request_lock);
    if (awaiting_hud.slot == slot || awaiting_hud.slot == 11) {
        armed = (SpellRequest){slot, awaiting_hud.finger_press};
        atomic_store(&needs_input_tick, true);
        awaiting_hud = (SpellRequest){0};
    }
    pthread_mutex_unlock(&request_lock);
}

static void dispatch_touch_spell(X86Ctx *ctx)
{
    pthread_mutex_lock(&request_lock);
    if (emitting || (!armed.slot && !active.slot)) {
        pthread_mutex_unlock(&request_lock);
        return;
    }
    bool playable = veil_touch_spell_playable();
    bool began = !active.slot;
    if (began && !playable) {
        armed = (SpellRequest){0};
        atomic_store(&needs_input_tick, false);
        pthread_mutex_unlock(&request_lock);
        return;
    }
    uint64_t now = clock_ms();
    SpellRequest request = began ? armed : active;
    VeilSpellPhase phase = VEIL_SPELL_PRESS;
    uint32_t owner = active_owner;
    if (began) {
        armed = (SpellRequest){0};
    } else {
        bool manual_held = request.finger_press && request.finger_press == finger_serial && finger_held;
        uint64_t elapsed = now - active_since;
        if (!playable || (!manual_held && elapsed >= 72)) {
            phase = VEIL_SPELL_RELEASE;
        } else if (manual_held && active_hold_ms && now >= next_hold_at) {
            phase = VEIL_SPELL_HOLD;
        } else {
            pthread_mutex_unlock(&request_lock);
            return;
        }
    }
    emitting = true;
    pthread_mutex_unlock(&request_lock);

    /* Guest event dispatch can call back through the HUD; release the host lock
     * around both original functions. Never send a release to a new actor. */
    uint32_t player = veil_spell_player(ctx);
    uint32_t actor = player ? ld32(player + PLAYER_ACTOR) : 0;
    bool sent = actor && (began || owner == actor) && veil_spell_emit(ctx, player, request.slot, phase);
    uint32_t hold_ms = sent && began && request.finger_press ? veil_spell_hold_delay(ctx, request.slot) : 0;
    pthread_mutex_lock(&request_lock);
    if (sent && began) {
        active_hold_ms = hold_ms;
        active = request;
        active_owner = actor;
        active_since = now;
        next_hold_at = now + hold_ms;
    } else if (!began && (phase == VEIL_SPELL_RELEASE || !actor || actor != owner)) {
        active = (SpellRequest){0};
    } else if (phase == VEIL_SPELL_HOLD) {
        next_hold_at = now + active_hold_ms;
        if (!sent) active_hold_ms = 0;
    }
    emitting = false;
    atomic_store(&needs_input_tick, active.slot || armed.slot);
    pthread_mutex_unlock(&request_lock);
}

void veil_spell_after_input(X86Ctx *ctx)
{
    veil_context_frame_begin();
    f_00C15370(ctx);
    veil_context_after_input(ctx);
    if (atomic_load(&needs_input_tick)) dispatch_touch_spell(ctx);
}
