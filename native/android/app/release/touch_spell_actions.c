#include "touch_spell_actions.h"
#include "guest_layout.h"
#include "touch_run_pad.h"

extern void f_00A71E80(X86Ctx *);  /* look up a loaded asset */
extern void f_00B8A350(X86Ctx *);  /* notify mode-stack listeners */
extern void f_00B97900(X86Ctx *);  /* emit the original action's UI events */
extern void f_00BC35E0(X86Ctx *);  /* resolve an input action name hash */
extern void f_00DBAC90(X86Ctx *);  /* local player by slot */

uint32_t veil_spell_player(X86Ctx *ctx)
{
    if (!ld32(SPELL_SYSTEM_MANAGER)) return 0;
    X86Ctx saved = *ctx;
    ctx->esp = (ctx->esp - 96) & ~15u;
    st32(ctx->esp, 0);
    st32(ctx->esp + 4, 0);
    f_00DBAC90(ctx);
    uint32_t player = ctx->eax;
    *ctx = saved;
    return player && !ld32(player + PLAYER_LOCAL_SLOT) ? player : 0;
}

static uint32_t action_handle(X86Ctx *ctx, uint32_t hash)
{
    uint32_t manager = ld32(SPELL_ACTION_MANAGER);
    if (!manager || ld32(manager) != SPELL_ACTION_MANAGER_VTABLE) return 0;
    X86Ctx saved = *ctx;
    ctx->esp = (ctx->esp - 96) & ~15u;
    ctx->ecx = manager;
    st32(ctx->esp, 0);
    st32(ctx->esp + 4, ctx->esp + 16);
    st32(ctx->esp + 16, hash);
    f_00BC35E0(ctx);
    uint32_t handle = ctx->eax;
    *ctx = saved;
    return handle;
}

/* PC_Magic_A through PC_Magic_J are the ten actual PC spell bindings. */
static uint32_t spell_handle(X86Ctx *ctx, unsigned slot, VeilSpellPhase phase)
{
    static const uint32_t names[] = {
        0x006b5a74, 0x006c78b3, 0x006d58ea, 0x005fee39, 0x00602e34,
        0x0060722b, 0x0060ac4a, 0x0060e9b1, 0x00615d78, 0x0061a0bb
    };
    static const uint32_t suffix[] = {
        [VEIL_SPELL_PRESS] = 0, [VEIL_SPELL_HOLD] = 0x006a9dc5,
        [VEIL_SPELL_RELEASE] = 0x00cd1818, [VEIL_SPELL_CHAIN] = 0x00a010b3
    };
    if (slot < 1 || slot > 10 || (unsigned)phase >= sizeof(suffix)/sizeof(suffix[0])) return 0;
    return action_handle(ctx, names[slot - 1] + suffix[phase]);
}

static uint32_t loaded_asset(X86Ctx *ctx, uint32_t manager, uint32_t handle)
{
    if (!manager || handle < 2 || handle >= ld32(manager + SPELL_ASSET_COUNT)) return 0;
    uint32_t flags = ld32(manager + SPELL_ASSET_FLAGS);
    if (!flags || (ld8(flags + handle) & (SPELL_ASSET_LOADED | SPELL_ASSET_INVALID))
            != SPELL_ASSET_LOADED) return 0;
    X86Ctx saved = *ctx;
    ctx->esp = (ctx->esp - 96) & ~15u;
    ctx->ecx = manager;
    st32(ctx->esp, 0);
    st32(ctx->esp + 4, handle);
    st32(ctx->esp + 8, 0);  /* leave unavailable assets unloaded */
    f_00A71E80(ctx);
    uint32_t asset = ctx->eax;
    *ctx = saved;
    return asset;
}

uint32_t veil_spell_hold_delay(X86Ctx *ctx, unsigned slot)
{
    uint32_t action = loaded_asset(ctx, ld32(SPELL_ACTION_MANAGER),
                                   spell_handle(ctx, slot, VEIL_SPELL_HOLD));
    uint32_t manager = ld32(SPELL_SEQUENCE_MANAGER);
    if (!action || ld32(action) != SPELL_ACTION_VTABLE || !ld32(action + SPELL_ACTION_BINDING_COUNT) ||
        !manager || ld32(manager) != SPELL_SEQUENCE_MANAGER_VTABLE) return 0;
    uint32_t bindings = ld32(action + SPELL_ACTION_BINDINGS);
    uint32_t sequence = bindings ? loaded_asset(ctx, manager, ld32(bindings)) : 0;
    if (!sequence || ld32(sequence) != SPELL_SEQUENCE_VTABLE || !ld32(sequence + SPELL_SEQUENCE_CHORD_COUNT)) return 0;
    uint32_t chords = ld32(sequence + SPELL_SEQUENCE_CHORDS);
    if (!chords || !ld32(chords + SPELL_CHORD_CONDITION_COUNT)) return 0;
    uint32_t conditions = ld32(chords + SPELL_CHORD_CONDITIONS);
    return conditions && ld32(conditions) == SPELL_CONDITION_TIMED_HOLD
        ? ld32(sequence + SPELL_SEQUENCE_HOLD_MILLISECONDS) : 0;
}

static bool match_action(X86Ctx *ctx, uint32_t player, uint32_t handle, bool *for_actor)
{
    uint32_t action = loaded_asset(ctx, ld32(SPELL_ACTION_MANAGER), handle);
    if (!action || ld32(action) != SPELL_ACTION_VTABLE || !ld32(action + SPELL_ACTION_BINDINGS) ||
        !ld32(action + SPELL_ACTION_BINDING_COUNT)) return false;
    uint32_t count = ld32(action + SPELL_ACTION_EVENT_COUNT);
    uint32_t records = ld32(action + SPELL_ACTION_EVENTS);
    if (count > 256 || (count && !records) ||
        (!count && !(ld8(action + SPELL_ACTION_FLAGS) & SPELL_PUBLISH_TO_ACTOR))) return false;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t row = records + i * SPELL_EVENT_RECORD_SIZE;
        if (!ld32(row + SPELL_EVENT_INSTANCE) || !ld32(row + SPELL_EVENT_PRIMARY_STRINGS) ||
            !ld32(row + SPELL_EVENT_STRING_COUNT)) return false;
    }
    uint32_t stack = player + PLAYER_INPUT_STACK;
    uint32_t broadcaster = ld32(stack + SPELL_BROADCASTER);
    if (!broadcaster || !ld32(broadcaster + SPELL_BROADCAST_COUNT) ||
        !ld32(broadcaster + SPELL_BROADCAST_LISTENERS)) return false;
    X86Ctx saved = *ctx;
    uint32_t scratch = (ctx->esp - 192) & ~15u;
    uint32_t flags = scratch + 32, sequence = scratch + 48;
    for (unsigned i = 0; i <= SPELL_SEQUENCE_MATCHED_BINDING; i += 4) st32(sequence + i, 0);
    st32(flags, 0);
    ctx->esp = scratch;
    ctx->ecx = action;
    st32(scratch, 0);
    st32(scratch + 4, flags);
    st32(scratch + 8, sequence);  /* primary binding at index zero */
    st32(scratch + 12, broadcaster);
    st32(scratch + 16, ld32(player + PLAYER_LOCAL_SLOT));
    f_00B97900(ctx);
    *for_actor = (ld8(flags) & SPELL_PUBLISH_TO_ACTOR) != 0;
    *ctx = saved;
    return true;
}

static void publish_actor_actions(X86Ctx *ctx, uint32_t stack, const uint32_t *handles, unsigned count)
{
    X86Ctx saved = *ctx;
    uint32_t scratch = (ctx->esp - 160) & ~15u, event = scratch + 32, data = scratch + 64;
    st32(event, SPELL_MODE_EVENT_VTABLE);
    st32(event + SPELL_MODE_EVENT_CONSUMED, 0);
    st32(event + SPELL_MODE_EVENT_DATA, data);
    st32(event + SPELL_MODE_EVENT_COUNT, count);
    st32(event + SPELL_MODE_EVENT_CAPACITY, count);
    st16(event + SPELL_MODE_EVENT_ALLOCATOR, 0x21);
    st16(event + SPELL_MODE_EVENT_GROWTH, 0xffff);
    for (unsigned i = 0; i < count; i++) st32(data + 4 * i, handles[i]);
    ctx->esp = scratch;
    ctx->ecx = stack;
    st32(scratch, 0);
    st32(scratch + 4, event);
    f_00B8A350(ctx);
    *ctx = saved;
}

bool veil_spell_emit(X86Ctx *ctx, uint32_t player, unsigned slot, VeilSpellPhase phase)
{
    if (!player || !ld32(player + PLAYER_ACTOR)) return false;
    uint32_t stack = player + PLAYER_INPUT_STACK;
    if (ld32(stack) != SPELL_INPUT_STACK_VTABLE || !ld32(stack + SPELL_BROADCAST_LISTENERS) ||
        !ld32(stack + SPELL_BROADCAST_COUNT)) return false;
    uint32_t handles[2];
    unsigned count = 0, phases = phase == VEIL_SPELL_PRESS ? 2 : 1;
    for (unsigned i = 0; i < phases; ++i) {
        uint32_t handle = spell_handle(ctx, slot, i ? VEIL_SPELL_CHAIN : phase);
        bool for_actor = false;
        if (!match_action(ctx, player, handle, &for_actor)) {
            if (!i) return false;
            continue;
        }
        if (for_actor) handles[count++] = handle;
    }
    /* The original fires Press and Chain from the same button-down match, then
     * sends one mode-stack event to the player's animation/equipment listeners. */
    if (count) publish_actor_actions(ctx, stack, handles, count);
    return true;
}

/* Original game input actions reached from the Android Run/Leap control. */
static _Thread_local uint32_t frame_interact_handle;
extern unsigned veil_take_touch_context(void);
extern int veil_touch_spell_playable(void);

static bool jump_down_offered(void)
{
    uint32_t game = ld32(GUEST_GAME_DATA_PTR);
    if (!game) return false;
    uint32_t rows = ld32(game + INTERACTION_QUEUE_ROWS);
    uint32_t count = ld32(game + INTERACTION_QUEUE_COUNT);
    uint32_t selected = ld32(game + INTERACTION_QUEUE_SELECTED);
    if (!rows || selected >= count) return false;
    uint32_t row = rows + selected * INTERACTION_ROW_SIZE;
    return ld32(row + INTERACTION_ROW_LABEL) == INTERACTION_JUMP_DOWN_LABEL &&
           ld32(row + INTERACTION_ROW_ACTION) == INTERACTION_JUMP_DOWN_ACTION;
}

void veil_context_frame_begin(void)
{
    frame_interact_handle = 0;
}

/* Before the original match publishes either its script or actor event.
 * Ignore only the virtual A match. Keyboard F, real controllers, spells and
 * the original SprintOn/Off/Toggle actions still follow their own bindings. */
bool veil_drop_run_interaction(X86Ctx *ctx, uint32_t stack, uint32_t mode,
                               uint32_t matches, uint32_t item)
{
    uint32_t index = ld32(ld32(matches + INPUT_MATCH_INDICES) + item * sizeof(uint32_t));
    uint32_t sequences = ld32(matches + INPUT_MATCH_SEQUENCES);
    if (ld32(sequences + index * INPUT_MATCH_STATE_SIZE + SPELL_SEQUENCE_MATCHED_BINDING) != 0)
        return false;
    uint32_t handle = ld32(ld32(mode + INPUT_MODE_ACTION_HANDLES) + index * sizeof(uint32_t));
    if (!frame_interact_handle) {
        X86Ctx query = *ctx;
        query.esp = stack;
        frame_interact_handle = action_handle(&query, INPUT_INTERACT_ACTION_HASH);
    }
    return handle && handle == frame_interact_handle && !jump_down_offered();
}

void veil_context_after_input(X86Ctx *ctx)
{
    unsigned intent = veil_take_touch_context();
    if (intent == VEIL_NO_CONTEXT || !veil_touch_spell_playable()) return;
    if (intent == VEIL_LEAP_ONLY && !jump_down_offered()) return;
    uint32_t player = veil_spell_player(ctx);
    if (!player || !ld32(player + PLAYER_ACTOR) ||
        ld32(player + PLAYER_INPUT_STACK) != SPELL_INPUT_STACK_VTABLE) return;
    uint32_t handle = action_handle(ctx, INPUT_INTERACT_ACTION_HASH);
    bool for_actor = false;
    if (match_action(ctx, player, handle, &for_actor) && for_actor)
        publish_actor_actions(ctx, player + PLAYER_INPUT_STACK, &handle, 1);
}
