#include <assert.h>
#include <stdlib.h>
#include "touch_spell_actions.c"

static uint8_t guest[GUEST_GAME_DATA_PTR + 4];
uintptr_t x86_mem_base = (uintptr_t)guest;
atomic_bool veil_run_filter_active;
enum { GAME = 0x1000, ROWS = 0x6000, MODE = 0x6200, MATCH = 0x6300, SEQS = 0x6400,
       INDICES = 0x6480, HANDLES = 0x6500, MANAGER = 0x6600, FLAGS = 0x6800,
       PLAYER = 0x6900, ACTIONS = 0x7000, ACTOR_ACTIONS = 0x7900, OUT = 0x7910, TOP = 0xf000,
       INTERACT = 23, SPRINT_HOLD = 24, SPRINT_OFF = 25, SPRINT_TOGGLE = 26, SPELL = 27 };
static unsigned script[32], actor[32], name_resolves, extra;
static bool playable = true;
static uint32_t asset(uint32_t handle) { return ACTIONS + (handle - INTERACT) * 128; }

unsigned veil_take_touch_context(void) { unsigned intent = extra; extra = 0; return intent; }
int veil_touch_spell_playable(void) { return playable; }
void f_00BC35E0(X86Ctx *ctx) {
    assert(ld32(ld32(ctx->esp + 4)) == INPUT_INTERACT_ACTION_HASH);
    ++name_resolves;
    ctx->eax = INTERACT;
    ctx->esp += 8;
}
void f_00DBAC90(X86Ctx *ctx) { ctx->eax = PLAYER; ctx->esp += 8; }
void f_00A71E80(X86Ctx *ctx) {
    ctx->eax = asset(ld32(ctx->esp + 4));
    ctx->esp += 12;
}
void f_00B97900(X86Ctx *ctx) {
    unsigned handle = INTERACT + (ctx->ecx - ACTIONS) / 128;
    script[handle]++;
    ctx->eax = ld32(ctx->esp + 4);
    st8(ctx->eax, ld8(ctx->ecx + SPELL_ACTION_FLAGS));
    ctx->esp += 20;
}
void f_00B8A350(X86Ctx *ctx) {
    uint32_t event = ld32(ctx->esp + 4);
    assert(ld32(event + SPELL_MODE_EVENT_COUNT) == 1);
    actor[ld32(ld32(event + SPELL_MODE_EVENT_DATA))]++;
    ctx->esp += 8;
}
void f_00BAB1E0(X86Ctx *ctx) {
    actor[ld32(ld32(ctx->esp + 4))]++;
    ctx->esp += 8;
}

#ifdef RETAIL_MATCH
#include "oa_probe_fast.h"
int oa_diagnostics_enabled;
void oa_probe_slow_enter(X86Ctx *ctx, uint32_t pc) { (void)ctx; (void)pc; abort(); }
#include "retail_match.inc"
#endif

static void match_action_frame(X86Ctx *ctx, unsigned handle, unsigned binding)
{
    st32(HANDLES, handle);
    st32(SEQS + SPELL_SEQUENCE_MATCHED_BINDING, binding);
    st32(MATCH + 0x14, 1);
    ctx->esp = TOP;
#ifdef RETAIL_MATCH
    st32(TOP + 4, OUT);
    st32(TOP + 8, MATCH);
    st32(TOP + 12, 0);
    st32(TOP + 16, 0);
    st32(TOP + 20, ACTOR_ACTIONS);
    ctx->ecx = MODE;
    f_00BB39A0(ctx);
    assert(ctx->esp == TOP + 24 && ctx->eax == OUT);
#else
    if (!veil_run_filter_active || !veil_drop_run_interaction(ctx, TOP, MODE, MATCH, 0)) {
        ++actor[handle];
        ++script[handle];
    }
#endif
}

int main(void)
{
    X86Ctx ctx = {0};
    VeilRunSample sample = {0};
    st32(GUEST_GAME_DATA_PTR, GAME);
    st32(GAME + INTERACTION_QUEUE_ROWS, ROWS);
    st32(GAME + INTERACTION_QUEUE_COUNT, 2);
    st32(GAME + INTERACTION_QUEUE_SELECTED, 0);
    st32(ROWS + INTERACTION_ROW_SIZE + INTERACTION_ROW_ACTION, INTERACTION_JUMP_DOWN_ACTION);
    st32(ROWS + INTERACTION_ROW_SIZE + INTERACTION_ROW_LABEL, INTERACTION_JUMP_DOWN_LABEL);
    st32(MODE + INPUT_MODE_ACTION_HANDLES, HANDLES);
    st32(MATCH + INPUT_MATCH_SEQUENCES, SEQS);
    st32(MATCH + INPUT_MATCH_INDICES, INDICES);
    st32(SPELL_ACTION_MANAGER, MANAGER);
    st32(SPELL_SYSTEM_MANAGER, MANAGER);
    st32(MANAGER, SPELL_ACTION_MANAGER_VTABLE);
    st32(MANAGER + SPELL_ASSET_COUNT, 64);
    st32(MANAGER + SPELL_ASSET_FLAGS, FLAGS);
    st8(FLAGS + INTERACT, SPELL_ASSET_LOADED);
    for (unsigned h = INTERACT; h <= SPELL; ++h) st8(asset(h) + SPELL_ACTION_FLAGS, 4);
    st32(asset(INTERACT), SPELL_ACTION_VTABLE);
    st32(asset(INTERACT) + SPELL_ACTION_BINDINGS, HANDLES);
    st32(asset(INTERACT) + SPELL_ACTION_BINDING_COUNT, 3);
    st32(PLAYER + PLAYER_ACTOR, 0x2000);
    uint32_t stack = PLAYER + PLAYER_INPUT_STACK;
    st32(stack, SPELL_INPUT_STACK_VTABLE);
    st32(stack + SPELL_BROADCASTER, ACTOR_ACTIONS + 32);
    st32(ACTOR_ACTIONS + 32 + SPELL_BROADCAST_COUNT, 1);
    st32(ACTOR_ACTIONS + 32 + SPELL_BROADCAST_LISTENERS, ACTOR_ACTIONS);

    assert(veil_sample_run(&sample, true, false, false, false) == VEIL_NO_CONTEXT);
    veil_run_filter_active = sample.run_owns_face;
    assert(veil_run_filter_active);
    veil_context_frame_begin();
    match_action_frame(&ctx, INTERACT, 0);  /* Shared A, NPC currently selected. */
    match_action_frame(&ctx, SPRINT_HOLD, 0);
    match_action_frame(&ctx, SPRINT_TOGGLE, 0);
    match_action_frame(&ctx, SPELL, 0);
    assert(!script[INTERACT] && !actor[INTERACT] && name_resolves == 1);
    assert(script[SPRINT_HOLD] && actor[SPRINT_TOGGLE] && script[SPELL]);
    match_action_frame(&ctx, INTERACT, 2);  /* Genuine keyboard F. */
    assert(script[INTERACT] == 1);
    st32(GAME + INTERACTION_QUEUE_SELECTED, 1);
    match_action_frame(&ctx, INTERACT, 0);  /* Same A at authored Jump Down. */
    assert(script[INTERACT] == 2 && actor[INTERACT] == 2);
    st32(GAME + INTERACTION_QUEUE_SELECTED, 0);
    extra = veil_sample_run(&sample, true, true, false, false);
    assert(extra == VEIL_INTERACT);
    veil_context_after_input(&ctx);
    veil_context_after_input(&ctx);
    assert(script[INTERACT] == 3 && actor[INTERACT] == 3);
    assert(!veil_sample_run(&sample, true, true, false, false));
    assert(!veil_sample_run(&sample, false, false, false, false));
    veil_run_filter_active = sample.run_owns_face;
    match_action_frame(&ctx, SPRINT_OFF, 0);
    assert(!veil_run_filter_active && script[SPRINT_OFF] == 1);

    extra = veil_sample_run(&sample, true, false, true, false);
    assert(extra == VEIL_LEAP_ONLY && !sample.run_owns_face);
    veil_context_after_input(&ctx); /* LS-remapped sprint alongside NPC. */
    assert(script[INTERACT] == 3);
    assert(!veil_sample_run(&sample, false, false, true, false));
    st32(GAME + INTERACTION_QUEUE_SELECTED, 1);
    extra = veil_sample_run(&sample, true, false, true, false);
    veil_context_after_input(&ctx);
    assert(script[INTERACT] == 4 && actor[INTERACT] == 4);
    st32(GAME + INTERACTION_QUEUE_SELECTED, UINT32_MAX); /* No active row. */
    extra = VEIL_LEAP_ONLY;
    veil_context_after_input(&ctx);
    playable = false;
    extra = VEIL_INTERACT;
    veil_context_after_input(&ctx);
    assert(script[INTERACT] == 4);

    assert(!veil_sample_run(&sample, false, false, false, false));
    assert(!veil_sample_run(&sample, true, true, false, false));
    assert(!sample.run_owns_face); /* Simultaneous first press: explicit Interact wins. */
    return 0;
}
