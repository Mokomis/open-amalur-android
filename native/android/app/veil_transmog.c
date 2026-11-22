#include "x86rt.h"
#include "guest_layout.h"

#include <android/log.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#define HIDDEN __attribute__((visibility("hidden")))

HIDDEN void f_00DBAC90(X86Ctx *);
enum { RENDERING = 7, CHARACTER_PROFILE = 8, EQUIPMENT = 9, ITEM = 10, WEAPON = 11, ARMOR = 12 };
enum { EQUIPMENT_SLOTS = 64 };
/* Return addresses inside koa.exe that identify which caller reached a hook. */
enum {
    RET_EQUIP_RENDER_LOOKUP = 0x00f7d03b, /* f_00F7CFA0 asking for the target's render component */
    RET_PORTRAIT_OPEN       = 0x01032124, /* portrait window creating its lookup */
    RET_PORTRAIT_MODEL_A    = 0x0102893b, /* portrait refresh paths reading an item's model */
    RET_PORTRAIT_MODEL_B    = 0x010307bc,
    RET_PORTRAIT_MODEL_C    = 0x01029194,
    RET_PORTRAIT_MODEL_D    = 0x01029371,
};
/* Layout of this file's own scratch frame on the guest stack. */
enum {
    SCRATCH_EQUIP_RESULT = 0x50, /* after the RENDER_COMPONENT_SIZE copy: f_00F7CFA0's output */
    FAB_LOOKUP_HANDLE    = 8,    /* fab_pointer's argument slot */
    MOUNT_MODEL          = 12,   /* model passed to the mount creator */
    MOUNT_RESULT         = 16,   /* handle the mount creator writes back */
};
static _Thread_local int result;
static _Thread_local uint32_t player_id;
static _Thread_local uint32_t portrait_window;
static _Thread_local uint32_t cosmetic_target;
static _Thread_local uint32_t cosmetic_render;
static _Thread_local uint32_t render_override_target;


struct Appearance {
    uint32_t equipped_item;
    uint32_t donor;
    uint32_t base_model;
    uint32_t model;
};
static _Thread_local struct Appearance appearances[EQUIPMENT_SLOTS];
static _Atomic uint64_t loaded_save;
static _Thread_local uint64_t active_save;

static bool appearance_file(char *path, size_t size, uint64_t key)
{
    const char *root = getenv("OA_OFFLINE_ROOT");
    if (!root || !key)
        return false;
    return snprintf(path, size, "%s/veil-appearances/%016llx.bin", root,
                    (unsigned long long)key) < (int)size;
}

static void load_appearances(uint32_t actor)
{
    uint64_t save = atomic_load_explicit(&loaded_save, memory_order_acquire);
    if (actor == player_id && save == active_save)
        return;
    player_id = actor;
    active_save = save;
    memset(appearances, 0, sizeof(appearances));
    char path[2304];
    FILE *file = appearance_file(path, sizeof(path), save) ? fopen(path, "rb") : NULL;
    if (file) {
        char magic[8];
        if (fread(magic, sizeof(magic), 1, file) != 1 || memcmp(magic, "VEILARM1", 8) ||
            fread(appearances, sizeof(appearances), 1, file) != 1 || fgetc(file) != EOF)
            memset(appearances, 0, sizeof(appearances));
        fclose(file);
    }
}


static uint32_t component(uint32_t id, unsigned type)
{
    uint32_t manager = ld32(GUEST_ENTITY_MANAGER_PTR);
    if (!manager || !(id & ENTITY_ID_SERIAL_MASK))
        return 0;
    uint32_t pool = manager + ENTITY_MANAGER_POOL;
    uint32_t index = id & ENTITY_ID_INDEX_MASK;
    if (index >= ld32(pool + ENTITY_POOL_COUNT) || ld32(ld32(pool + ENTITY_POOL_IDS) + 4 * index) != id)
        return 0;
    uint32_t entity = ld32(ld32(pool + ENTITY_POOL_ENTITIES) + 4 * index);
    if (!entity || !(ld8(entity + ENTITY_FLAGS) & ENTITY_FLAG_ACTIVE))
        return 0;
    uint32_t value = ld32(entity + ENTITY_COMPONENTS + 4 * type);
    return value && (ld8(value + COMPONENT_FLAGS) & COMPONENT_FLAG_ACTIVE) &&
           ld32(value + COMPONENT_OWNER) == id ? value : 0;
}

static uint32_t player_object(const X86Ctx *ctx)
{
    X86Ctx call = *ctx;
    call.esp -= 12;
    st32(call.esp, 0);
    st32(call.esp + 4, 0);
    f_00DBAC90(&call);
    return call.eax;
}

HIDDEN void f_00F7CFA0(X86Ctx *);
HIDDEN void f_00CB00B0(X86Ctx *);
HIDDEN void f_00CB0110(X86Ctx *);
HIDDEN void f_0102E9F0(X86Ctx *);
HIDDEN void f_01028870(X86Ctx *);
HIDDEN void f_00C8AD60(X86Ctx *);
HIDDEN void f_00D2E110(X86Ctx *);
HIDDEN void f_00D2DBC0(X86Ctx *);
HIDDEN void f_01030710(X86Ctx *);
HIDDEN void f_00F5C2A0(X86Ctx *);
HIDDEN void f_00F74BD0(X86Ctx *);
HIDDEN void f_00DD0650(X86Ctx *);
HIDDEN void f_00DD0670(X86Ctx *);


static uint32_t guest_call(X86Ctx *ctx, uint32_t bottom, X86Fn fn, uint32_t self,
                           unsigned argc, const uint32_t *args)
{
    // Lifted calls use the live dispatcher context, including its private frame chain.
    X86Ctx saved = *ctx;
    ctx->ecx = self;
    ctx->esp = bottom - (argc + 1) * 4;
    st32(ctx->esp, 0);
    for (unsigned i = 0; i < argc; ++i)
        st32(ctx->esp + (i + 1) * 4, args[i]);
    fn(ctx);
    uint32_t output = ctx->eax;
    *ctx = saved;
    return output;
}

enum { SHIELD_EQUIP_TYPE = 16 };

static unsigned fitted_model(uint32_t render, uint32_t profile, bool rigid)
{
    if (rigid) {
        uint32_t primary = ld32(render + RENDER_PRIMARY_MODEL);
        return primary >= 2 ? primary : ld32(render + RENDER_SECONDARY_MODEL);
    }
    return ld32(render + (ld32(profile + PROFILE_BODY_VARIANT) ? RENDER_BODY_MODEL_1 : RENDER_BODY_MODEL_0));
}

static uint32_t weapon_class(uint32_t part)
{
    // ACTOR.is_weapon_class compares the live weapon's class with the original class table.
    return part ? ld32(part + WEAPON_CLASS) : 0;
}

static void refresh_armor_portrait(X86Ctx *ctx, uint32_t window, uint32_t item)
{
    if (!window || ld32(window + PORTRAIT_WINDOW_FAB) < 2)
        return;
    uint32_t bottom = ctx->esp - 0x100;
    uint32_t portrait = window + PORTRAIT_WINDOW_VIEW;
    const uint32_t args[] = {item};
    guest_call(ctx, bottom, f_0102E9F0, portrait, 1, args);
    guest_call(ctx, bottom, f_01028870, portrait, 1, args);
    guest_call(ctx, bottom, f_01030710, portrait, 0, NULL);
}

static uint32_t fab_pointer(X86Ctx *ctx, uint32_t address, uint32_t handle)
{
    st32(address + FAB_LOOKUP_HANDLE, handle);
    return guest_call(ctx, address, f_00C8AD60, ld32(GUEST_RENDER_MANAGER_PTR), 1,
                      (uint32_t[]){address + FAB_LOOKUP_HANDLE});
}

static bool swap_portrait_mounts(X86Ctx *ctx, uint32_t address, uint32_t fab, uint32_t old_model,
                                 uint32_t new_model, uint32_t scene, unsigned depth)
{
    if (!fab || depth > 4)
        return false;
    uint32_t count = ld32(fab + FAB_CHILD_COUNT), children = ld32(fab + FAB_CHILDREN);
    bool changed = false;
    for (unsigned i = 0; children && i < count && i < 128; ++i) {
        uint32_t handle = ld32(children + 4 * i);
        if (handle < 2)
            continue;
        uint32_t child = fab_pointer(ctx, address, handle);
        if (!child)
            continue;
        uint32_t mounted = ld32(child + FAB_MODEL);
        if (mounted == old_model) {
            uint32_t flags = ld32(child + FAB_FLAGS);
            st32(address + MOUNT_MODEL, new_model);
            guest_call(ctx, address, f_00D2E110, ld32(GUEST_RENDER_MANAGER_PTR), 3,
                       (uint32_t[]){address + MOUNT_RESULT, address + MOUNT_MODEL, scene});
            uint32_t replacement = fab_pointer(ctx, address, ld32(address + MOUNT_RESULT));
            if (!replacement)
                continue;
            st32(replacement + FAB_FLAGS, (ld32(replacement + FAB_FLAGS) & ~FAB_FLAG_UNKNOWN_2000) |
                                          (flags & FAB_FLAG_UNKNOWN_2000));
            guest_call(ctx, address, f_00D2DBC0, fab, 3, (uint32_t[]){i, address + MOUNT_RESULT, 1});
            __android_log_print(ANDROID_LOG_INFO, "VeilTransmog", "Portrait mount %u depth %u %x -> %x",
                                i, depth, mounted, new_model);
            changed = true;
        } else if (swap_portrait_mounts(ctx, address, child, old_model, new_model, scene, depth + 1)) {
            changed = true;
        }
    }
    return changed;
}

static int apply_appearance(X86Ctx *ctx, uint32_t donor, unsigned logical_slot, unsigned mode)
{
    uint32_t player = player_object(ctx);
    if (!player)
        return 0;
    uint32_t actor = ld32(player + PLAYER_ACTOR);
    load_appearances(actor);

    uint32_t inventory = component(actor, EQUIPMENT);
    uint32_t donor_item = component(donor, ITEM);
    uint32_t donor_render = component(donor, RENDERING);
    uint32_t profile = component(actor, CHARACTER_PROFILE);
    if (!inventory || !donor_item || !donor_render || !profile ||
        ld32(donor_item + ITEM_OWNER) != actor)
        return 0;

    uint32_t donor_class = weapon_class(component(donor, WEAPON));
    uint32_t kind = ld32(donor_item + ITEM_EQUIP_TYPE);
    uint32_t types = ld32(GUEST_GAME_DATA_PTR) + GAME_DATA_EQUIP_TYPES;
    uint32_t scratch = ctx->esp - 0x100, bottom = scratch - 16;
    // The retail equipment table stores shields in their own slot group.
    bool shield = kind == SHIELD_EQUIP_TYPE;
    bool weapon = donor_class && !shield;
    bool rigid = weapon || shield;
    if ((!rigid && !component(donor, ARMOR)) || fitted_model(donor_render, profile, rigid) < 2)
        return 0;

    uint32_t slots = ld32(inventory + EQUIPMENT_SLOT_TABLE), count = ld32(inventory + EQUIPMENT_SLOT_COUNT);
    uint32_t size = guest_call(ctx, ctx->esp - 0x80, f_00DD0670, types, 1, &kind);
    uint32_t first = guest_call(ctx, ctx->esp - 0x80, f_00DD0650, types, 1, &kind);
    unsigned slot = first + logical_slot;
    uint32_t target = slots && logical_slot < size && slot < count && slot < EQUIPMENT_SLOTS ?
                      ld32(slots + 4 * slot) : 0;
    uint32_t equipped_item = component(target, ITEM);
    uint32_t original = component(target, RENDERING);
    uint32_t target_class = weapon_class(component(target, WEAPON));
    if (mode == 3)
        __android_log_print(ANDROID_LOG_INFO, "VeilTransmog",
                            "Request donor=%x class=%x target=%x class=%x slot=%u",
                            donor, donor_class, target, target_class, slot);
    if (!equipped_item || !original || ld32(equipped_item + ITEM_OWNER) != actor ||
        fitted_model(original, profile, rigid) < 2 ||
        (weapon ? donor_class != target_class : !shield && !component(target, ARMOR)))
        return 0;

    struct Appearance *appearance = &appearances[slot];
    if (target == donor && appearance->equipped_item != target)
        return 0;
    if (mode == 1)
        return target == donor ? 2 : 1;

    // Supply a render-component view on the guest stack. The actual item,
    // equipment slots, durability and modifiers stay under the original engine.
    uint32_t model = fitted_model(donor_render, profile, rigid);
    for (unsigned offset = 0; offset < RENDER_COMPONENT_SIZE; offset += 4)
        st32(scratch + offset, ld32(donor_render + offset));
    if (shield)
        st32(scratch + RENDER_SECONDARY_MODEL, model);
    else
        st32(scratch + RENDER_PRIMARY_MODEL, model);
    st32(scratch + RENDER_UNKNOWN_3C, ld32(original + RENDER_UNKNOWN_3C));
    st32(scratch + RENDER_UNKNOWN_40, ld32(original + RENDER_UNKNOWN_40));
    st32(scratch + SCRATCH_EQUIP_RESULT, 0);
    uint32_t render_manager = ld32(GUEST_RENDER_MANAGER_PTR);
    guest_call(ctx, bottom, f_00CB00B0, render_manager, 1, &model);
    render_override_target = cosmetic_target = target;
    cosmetic_render = scratch;
    guest_call(ctx, bottom, f_00F7CFA0, inventory, 3, (uint32_t[]){scratch + SCRATCH_EQUIP_RESULT, target, shield ? 2 : 1});
    render_override_target = cosmetic_target = cosmetic_render = 0;
    guest_call(ctx, bottom, f_00CB0110, render_manager, 1, &model);
    if (!ld32(scratch + SCRATCH_EQUIP_RESULT))
        return 0;

    uint32_t old_model = appearance->equipped_item == target ? appearance->model :
                         fitted_model(original, profile, rigid);
    *appearance = donor == target ? (struct Appearance){0} :
                  (struct Appearance){target, donor, fitted_model(original, profile, rigid), model};
    if (weapon && portrait_window && old_model != model && ld32(portrait_window + PORTRAIT_WINDOW_FAB) >= 2) {
        uint32_t root = fab_pointer(ctx, bottom, ld32(portrait_window + PORTRAIT_WINDOW_FAB));
        if (!swap_portrait_mounts(ctx, bottom, root, old_model, model,
                                 ld32(portrait_window + PORTRAIT_WINDOW_SCENE), 0)) {
            uint32_t stock = fitted_model(original, profile, rigid);
            if (stock != old_model && stock != model)
                swap_portrait_mounts(ctx, bottom, root, stock, model, ld32(portrait_window + PORTRAIT_WINDOW_SCENE), 0);
        }
    } else if (!rigid)
        refresh_armor_portrait(ctx, portrait_window, target);
    __android_log_print(ANDROID_LOG_INFO, "VeilTransmog",
                        "Item %08x appearance %08x slot %u model %x", target, donor, slot, model);
    return 1;
}

#define HOOK(name) HIDDEN void veil_stock_##name(X86Ctx *); HIDDEN void veil_trace_##name(X86Ctx *ctx)

HOOK(component)
{
    uint32_t out = ctx->ecx;
    bool substitute = render_override_target && ld32(ctx->esp) == RET_EQUIP_RENDER_LOOKUP &&
                      ld32(ctx->esp + 4) == render_override_target && ld32(ctx->esp + 8) == RENDERING;
    veil_stock_component(ctx);
    if (substitute)
        st32(out + 4, cosmetic_render);
}

HOOK(portrait_lookup)
{
    uint32_t caller = ld32(ctx->esp), ref = ld32(ctx->esp + 8);
    veil_stock_portrait_lookup(ctx);
    if (caller == RET_PORTRAIT_OPEN && ctx->eax == 0) {
        uint32_t player = guest_call(ctx, ctx->esp - 0x80, f_00DBAC90, 0, 1, (uint32_t[]){0});
        if (player)
            load_appearances(ld32(player + PLAYER_ACTOR));
        portrait_window = ld32(ref);
    }
}

static const struct Appearance *equipped_appearance(uint32_t actor, uint32_t target, uint32_t render);

HOOK(portrait_model)
{
    uint32_t caller = ld32(ctx->esp), out = ld32(ctx->esp + 4);
    uint32_t render = ctx->ecx;
    uint32_t target = ld32(render + COMPONENT_OWNER);
    veil_stock_portrait_model(ctx);
    if (!player_id || (caller != RET_PORTRAIT_MODEL_A && caller != RET_PORTRAIT_MODEL_B &&
                       caller != RET_PORTRAIT_MODEL_C && caller != RET_PORTRAIT_MODEL_D))
        return;
    const struct Appearance *appearance = equipped_appearance(player_id, target, render);
    if (appearance)
        st32(out, appearance->model);
}

static const struct Appearance *equipped_appearance(uint32_t actor, uint32_t target, uint32_t render)
{
    uint32_t gear = component(actor, EQUIPMENT);
    uint32_t profile = component(actor, CHARACTER_PROFILE);
    if (!gear || !profile || !render)
        return NULL;
    uint32_t slots = ld32(gear + EQUIPMENT_SLOT_TABLE), count = ld32(gear + EQUIPMENT_SLOT_COUNT);
    for (unsigned i = 0; slots && i < count && i < EQUIPMENT_SLOTS; ++i) {
        const struct Appearance *appearance = &appearances[i];
        if (target && ld32(slots + 4 * i) == target && appearance->equipped_item == target &&
            appearance->model >= 2 && (appearance->base_model == ld32(render + RENDER_PRIMARY_MODEL) ||
             appearance->base_model == ld32(render + RENDER_SECONDARY_MODEL) ||
             appearance->base_model == fitted_model(render, profile, false)))
            return appearance;
    }
    return NULL;
}

HOOK(skin)
{
    uint32_t inventory = ctx->ecx;
    uint32_t actor = inventory ? ld32(inventory + COMPONENT_OWNER) : 0;
    uint32_t action = ld32(ctx->esp + 12);
    uint32_t target = ld32(ctx->esp + 8);
    uint32_t render = component(target, RENDERING);
    if (cosmetic_target || (action != 1 && action != 2) || !actor || !render) {
        veil_stock_skin(ctx);
        return;
    }

    uint32_t player = guest_call(ctx, ctx->esp - 0x80, f_00DBAC90, 0, 1, (uint32_t[]){0});
    if (!player || actor != ld32(player + PLAYER_ACTOR)) {
        veil_stock_skin(ctx);
        return;
    }
    load_appearances(actor);
    const struct Appearance *appearance = equipped_appearance(actor, target, render);
    if (!appearance) {
        veil_stock_skin(ctx);
        return;
    }

    uint32_t scratch = ctx->esp - 0x100;
    for (unsigned offset = 0; offset < RENDER_COMPONENT_SIZE; offset += 4)
        st32(scratch + offset, ld32(render + offset));
    uint32_t model = appearance->model;
    uint32_t item = component(target, ITEM);
    if (item && ld32(item + ITEM_EQUIP_TYPE) == SHIELD_EQUIP_TYPE)
        st32(scratch + RENDER_SECONDARY_MODEL, model);
    else
        st32(scratch + RENDER_PRIMARY_MODEL, model);
    uint32_t manager = ld32(GUEST_RENDER_MANAGER_PTR);
    guest_call(ctx, scratch - 16, f_00CB00B0, manager, 1, &model);
    // Skip the nearby state calls only for the manual cosmetic action.
    cosmetic_render = scratch;
    render_override_target = target;
    const uint32_t original_stack = ctx->esp;
    uint32_t args[] = {ld32(original_stack + 4), target, action};
    ctx->eax = guest_call(ctx, scratch - 16, veil_stock_skin, inventory, 3, args);
    ctx->esp = original_stack + 16;
    cosmetic_render = render_override_target = 0;
    guest_call(ctx, scratch - 16, f_00CB0110, manager, 1, &model);
}

HIDDEN void veil_preview_style_effects(X86Ctx *ctx)
{
    if (cosmetic_target)
        ctx->esp += 12;
    else
        f_00F5C2A0(ctx);
}

HIDDEN void veil_preview_held_item(X86Ctx *ctx)
{
    if (cosmetic_target)
        ctx->esp += 12;
    else
        f_00F74BD0(ctx);
}

static int parse_handle(const char **text, uint32_t *id)
{
    const char *value = *text;
    if (value[0] != '0' || value[1] != 'x')
        return 0;
    uint64_t encoded = 0;
    for (unsigned i = 2; i < 18; ++i) {
        unsigned char digit = value[i];
        if (digit >= '0' && digit <= '9')
            digit -= '0';
        else if (digit >= 'a' && digit <= 'f')
            digit -= 'a' - 10;
        else
            return 0;
        encoded = (encoded << 4) | digit;
    }
    if ((uint32_t)encoded != 0xf0)
        return 0;
    *id = (uint32_t)(encoded >> 32) & ENTITY_ID_MASK;
    *text = value + 18;
    return 1;
}

HIDDEN int veil_transmog_command(X86Ctx *ctx, const char *section, const char *key, const char *value)
{
    if (!key || strcasecmp(section, "DEFAULT") || strcasecmp(key, "VeilTransmogAction"))
        return -1;
    result = 0;
    if (!value || (value[0] != '1' && value[0] != '3') || value[1] != '|')
        return 1;
    unsigned mode = value[0] - '0';
    const char *cursor = value + 2;
    uint32_t donor;
    if (parse_handle(&cursor, &donor) && *cursor++ == '|' &&
        cursor[0] >= '0' && cursor[0] <= '9' && cursor[1] == 0)
        result = apply_appearance(ctx, donor, cursor[0] - '0', mode);
    return 1;
}

HIDDEN int veil_transmog_read(const char *section, const char *key, char value[2])
{
    if (strcasecmp(section, "DEFAULT") || strcasecmp(key, "VeilTransmogResult"))
        return 0;
    value[0] = '0' + result;
    value[1] = 0;
    return 1;
}

HIDDEN int veil_stock_read_save(void *, const char *, void *, int);
HIDDEN bool veil_stock_write_save(void *, const char *, const void *, int);

static bool is_game_save(const char *name)
{
    static const char prefix[] = "autocloud/save/svd_fmt_";
    return name && !strncmp(name, prefix, sizeof(prefix) - 1) &&
           strlen(name) >= sizeof(prefix) + 6 && !strcmp(name + strlen(name) - 4, ".sav") &&
           strcmp(name, "autocloud/save/svd_fmt_0_0.sav");
}

static uint64_t save_key(const char *name, const void *data, int size)
{
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (const unsigned char *p = (const unsigned char *)name; *p; ++p)
        hash = (hash ^ *p) * 0x100000001b3ULL;
    const unsigned char *bytes = data;
    for (int i = 0; i < size; ++i)
        hash = (hash ^ bytes[i]) * 0x100000001b3ULL;
    return hash;
}

HIDDEN int veil_trace_read_save(void *storage, const char *name, void *buffer, int size)
{
    int length = veil_stock_read_save(storage, name, buffer, size);
    if (length == size && length > 0 && is_game_save(name)) {
        uint64_t key = save_key(name, buffer, length);
        atomic_store_explicit(&loaded_save, key, memory_order_release);
        __android_log_print(ANDROID_LOG_INFO, "VeilTransmog", "Read appearance save %s key=%llx",
                            name, (unsigned long long)key);
    }
    return length;
}

HIDDEN bool veil_trace_write_save(void *storage, const char *name, const void *buffer, int size)
{
    bool written = veil_stock_write_save(storage, name, buffer, size);
    if (!written || !player_id || !is_game_save(name))
        return written;
    uint64_t key = save_key(name, buffer, size);
    char folder[2304], path[2304], temp[2320];
    const char *root = getenv("OA_OFFLINE_ROOT");
    if (!root || snprintf(folder, sizeof(folder), "%s/veil-appearances", root) >= (int)sizeof(folder) ||
        !appearance_file(path, sizeof(path), key))
        return written;
    mkdir(folder, 0700);
    snprintf(temp, sizeof(temp), "%s.XXXXXX", path);
    int fd = mkstemp(temp);
    if (fd < 0)
        return written;
    FILE *file = fdopen(fd, "wb");
    bool saved = false;
    if (file) {
        saved = fwrite("VEILARM1", 8, 1, file) == 1 &&
                fwrite(appearances, sizeof(appearances), 1, file) == 1 &&
                fflush(file) == 0 && fsync(fd) == 0;
        if (fclose(file))
            saved = false;
    } else {
        close(fd);
    }
    if (!saved || rename(temp, path)) {
        unlink(temp);
        __android_log_print(ANDROID_LOG_WARN, "VeilTransmog", "Failed to save appearances for %s", name);
    } else {
        __android_log_print(ANDROID_LOG_INFO, "VeilTransmog", "Wrote appearance save %s key=%llx",
                            name, (unsigned long long)key);
    }
    return written;
}
