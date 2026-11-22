/* Recover texture/sampler pairs from the translator's direct loads. This
   keeps overrides away from the bloom, LUT, shadow and screen samplers. */
static void af_get_texture_pairs(oa_shader *shader, const uint32_t *spv, size_t words)
{
    if (words < 5 || !spv[3] || spv[3] > words) return;
    uint32_t *binding = calloc(spv[3], sizeof(*binding));
    if (!binding) return;
    for (size_t at = 5; at < words;) {
        const uint32_t *op = spv + at;
        uint32_t size = op[0] >> 16;
        if (!size || size > words - at) break;
        switch (op[0] & 0xffffu) {
        case 71: /* OpDecorate, Binding */
            if (size == 4 && op[2] == 33 && op[1] < spv[3] && op[3] < 64)
                binding[op[1]] = op[3] + 1;
            break;
        case 61: /* OpLoad */
            if (size >= 4 && op[2] < spv[3] && op[3] < spv[3])
                binding[op[2]] = binding[op[3]];
            break;
        case 86: /* OpSampledImage */
            if (size == 5 && op[3] < spv[3] && op[4] < spv[3]) {
                uint32_t t = binding[op[3]] - (OA_BIND_TEXTURE + 1);
                uint32_t s = binding[op[4]] - (OA_BIND_SAMPLER + 1);
                if (s < 16 && t < 32) shader->af_textures[s] |= 1u << t;
            }
            break;
        }
        at += size;
    }
    free(binding);
}

static int af_is_material(const oa_shader *shader, unsigned slot, oa_view **views)
{
    uint32_t mask = shader->af_textures[slot];
    if (!mask) return 0;
    for (; mask; mask &= mask - 1) {
        unsigned t = __builtin_ctz(mask);
        const oa_view *view = views[t];
        if (!view || !view->view || shader->info.texture_dim[t] != 3 ||
            view->tex->is_depth || view->tex->layers != 1 || view->tex->block != 4 ||
            view->tex->mips < 2 || (view->tex->desc[8] & 0xa0u))
            return 0;
    }
    return 1;
}

static void af_poll(void)
{
    static uint64_t next_poll;
    if (g_frame < next_poll) return;
    next_poll = g_frame + 30;
    const char *root = getenv("OA_OFFLINE_ROOT");
    if (!root) return;
    char path[4096];
    if (snprintf(path, sizeof(path), "%s/texture-anisotropy", root) >= (int)sizeof(path)) return;
    FILE *file = fopen(path, "r");
    unsigned requested = 0;
    if (file) {
        if (fscanf(file, "%u", &requested) != 1 || (requested != 8 && requested != 16))
            requested = 0;
        fclose(file);
    }
    if (requested > af_device_limit) requested = 0;
    if (requested != af_requested) {
        af_requested = requested;
        ++af_generation;
    }
    logf_("AF active=%u frame=%llu eligible_bindings=%u native_levels=%#x",
          af_requested, (unsigned long long)g_frame, af_active_draws, af_stock_mask);
    af_active_draws = 0;
    af_stock_mask = 0;
}
