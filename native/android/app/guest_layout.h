#ifndef GUEST_LAYOUT_H
#define GUEST_LAYOUT_H
/* Layouts of the retail game's (x86, koa.exe cs:15032) data structures that
 * hand-written hooks read through ld8/ld32/st32 on guest addresses.
 *
 * Rules (see CLAUDE.md):
 *   - Hand-written code never uses a raw offset into a game structure; add the
 *     field here and use the name.
 *   - Each structure and field is defined once, in this file.
 *   - Name a field from what the engine provably does with it. If its meaning
 *     is not established, call it UNKNOWN_<offset> and say what is known.
 *
 * Offsets are relative to the start of the structure. Guest pointers are
 * 32-bit addresses in the emulated address space, so these are offsets, not
 * C structs. */

/* Global pointers in koa.exe's data section. */
enum {
    GUEST_ENTITY_MANAGER_PTR = 0x19fec38, /* entity manager: owns the entity pool */
    GUEST_RENDER_MANAGER_PTR = 0x19fdf54, /* render/fab manager: model references, fab lookup */
    GUEST_GAME_DATA_PTR      = 0x19fe9c4, /* game data: owns the equipment type table */
};

/* Entity manager (*GUEST_ENTITY_MANAGER_PTR). */
enum {
    ENTITY_MANAGER_POOL = 0x2238, /* embedded entity pool */
};

/* Entity ids: the low 16 bits index the entity pool; ids with no bits set in
 * ENTITY_ID_SERIAL_MASK are not valid entities. */
enum {
    ENTITY_ID_INDEX_MASK  = 0x0000ffff,
    ENTITY_ID_SERIAL_MASK = 0x0fff0000,
    ENTITY_ID_MASK        = 0x0fffffff,
};

/* Entity pool, embedded in the entity manager. An entity id's low 16 bits
 * index the tables; the slot is live only if the stored id matches. */
enum {
    ENTITY_POOL_ENTITIES = 0x0c, /* pointer to an array of entity pointers */
    ENTITY_POOL_IDS      = 0x1c, /* pointer to an array of entity ids */
    ENTITY_POOL_COUNT    = 0x20, /* number of slots */
};

/* Entity. */
enum {
    ENTITY_COMPONENTS = 0x3c,  /* array of component pointers, indexed by component type */
    ENTITY_FLAGS      = 0x10c, /* byte; ENTITY_FLAG_ACTIVE */
};
enum { ENTITY_FLAG_ACTIVE = 0x01 };

/* Header shared by every component. */
enum {
    COMPONENT_OWNER = 0x18, /* id of the entity that owns this component */
    COMPONENT_FLAGS = 0x20, /* byte; COMPONENT_FLAG_ACTIVE */
};
enum { COMPONENT_FLAG_ACTIVE = 0x01 };

/* Player object (returned by f_00DBAC90). */
enum {
    PLAYER_INPUT_STACK = 0x18,
    PLAYER_LOCAL_SLOT = 0x100,
    PLAYER_ACTOR = 0x1ec, /* entity id of the player's actor */
};

/* Equipment component (component type 9). */
enum {
    EQUIPMENT_SLOT_TABLE = 0x34, /* pointer to an array of equipped item ids, one per slot */
    EQUIPMENT_SLOT_COUNT = 0x38,
};

/* Item component (component type 10). */
enum {
    ITEM_EQUIP_TYPE = 0x74, /* equipment type; selects the slot group in the type table */
    ITEM_OWNER      = 0x8c, /* entity id of the actor holding the item */
};

/* Weapon component (component type 11). */
enum {
    WEAPON_CLASS = 0x1f0, /* compared against the original class table by ACTOR.is_weapon_class */
};

/* Character profile component (component type 8). */
enum {
    PROFILE_BODY_VARIANT = 0x28, /* nonzero selects RENDER_BODY_MODEL_1 for fitted armor */
};

/* Rendering component (component type 7). Hooks copy RENDER_COMPONENT_SIZE
 * bytes to build a substitute view on the guest stack. */
enum {
    RENDER_PRIMARY_MODEL   = 0x28, /* model for weapons; used for rigid items when >= 2 */
    RENDER_BODY_MODEL_0    = 0x2c, /* fitted armor model when PROFILE_BODY_VARIANT is 0 */
    RENDER_BODY_MODEL_1    = 0x30, /* fitted armor model when PROFILE_BODY_VARIANT is nonzero */
    RENDER_SECONDARY_MODEL = 0x34, /* shield model; rigid fallback when the primary is < 2 */
    RENDER_UNKNOWN_3C      = 0x3c, /* kept from the equipped item when swapping appearance */
    RENDER_UNKNOWN_40      = 0x40, /* kept from the equipped item when swapping appearance */
    RENDER_COMPONENT_SIZE  = 0x44,
};

/* Game data (*GUEST_GAME_DATA_PTR). */
enum {
    GAME_DATA_EQUIP_TYPES = 0x2ee4, /* equipment type table (f_00DD0650 first slot, f_00DD0670 size) */
};

/* Fab (scene node) returned by the render manager's fab lookup (f_00C8AD60). */
enum {
    FAB_CHILDREN    = 0x24,  /* pointer to an array of child fab handles */
    FAB_CHILD_COUNT = 0x28,
    FAB_MODEL       = 0x198, /* model mounted on this node */
    FAB_FLAGS       = 0x1d0,
};
enum { FAB_FLAG_UNKNOWN_2000 = 0x2000 /* preserved when a mount is replaced */ };

/* Character portrait window (the inventory/equipment paper doll). */
enum {
    PORTRAIT_WINDOW_VIEW  = 0x410, /* portrait object passed to the refresh calls */
    PORTRAIT_WINDOW_FAB   = 0x470, /* handle of the portrait's root fab; >= 2 when valid */
    PORTRAIT_WINDOW_SCENE = 0x928, /* scene used when creating a replacement mount */
};


/* cs:15032 input binding resources. The original input matcher f_00B97900
 * publishes UI events. Flag 4 also publishes the matched action handles to
 * the player animation graph through f_00B8A350. */
enum {
    SPELL_SYSTEM_MANAGER = 0x19fd5e4,
    SPELL_ACTION_MANAGER = 0x19f4d94,
    SPELL_SEQUENCE_MANAGER = 0x19f4d90,
    SPELL_ACTION_MANAGER_VTABLE = 0x173607c,
    SPELL_SEQUENCE_MANAGER_VTABLE = 0x17342ac,
    SPELL_ACTION_VTABLE = 0x1736458,
    SPELL_ASSET_COUNT = 0x5c,
    SPELL_ASSET_FLAGS = 0x28,
    SPELL_ASSET_LOADED = 4,
    SPELL_ASSET_INVALID = 16,
    SPELL_ACTION_EVENTS = 0x0c,
    SPELL_ACTION_EVENT_COUNT = 0x10,
    SPELL_ACTION_BINDINGS = 0x1c,
    SPELL_ACTION_BINDING_COUNT = 0x20,
    SPELL_ACTION_FLAGS = 0x2c,
    SPELL_PUBLISH_TO_ACTOR = 4,
    SPELL_EVENT_RECORD_SIZE = 0x18,
    SPELL_EVENT_PRIMARY_STRINGS = 0,
    SPELL_EVENT_STRING_COUNT = 4,
    SPELL_EVENT_INSTANCE = 0x14,
    SPELL_INPUT_STACK_VTABLE = 0x17369a4,
    SPELL_BROADCASTER = 0x44,
    SPELL_BROADCAST_LISTENERS = 4,
    SPELL_BROADCAST_COUNT = 8,
    SPELL_SEQUENCE_MATCHED_BINDING = 0x10,
    SPELL_MODE_EVENT_VTABLE = 0x1735a84,
    SPELL_MODE_EVENT_CONSUMED = 4,
    SPELL_MODE_EVENT_DATA = 8,
    SPELL_MODE_EVENT_COUNT = 12,
    SPELL_MODE_EVENT_CAPACITY = 16,
    SPELL_MODE_EVENT_ALLOCATOR = 20,
    SPELL_MODE_EVENT_GROWTH = 22,
    SPELL_SEQUENCE_VTABLE = 0x1736440,
    SPELL_SEQUENCE_CHORD_COUNT = 4,
    SPELL_SEQUENCE_CHORDS = 8,
    SPELL_SEQUENCE_HOLD_MILLISECONDS = 0x14,
    SPELL_CHORD_CONDITION_COUNT = 0,
    SPELL_CHORD_CONDITIONS = 4,
    SPELL_CONDITION_TIMED_HOLD = 4
};

#endif
