/* Part of oa_vk_fullscreen.c: DXGI to Vulkan format mapping. Included in place; not compiled on its own. */
/* ---- formats ---- */
typedef struct { VkFormat vk; int block, bytes; } fmt_info;

static fmt_info dx_format(uint32_t f) {
    fmt_info r = {VK_FORMAT_UNDEFINED, 1, 4};
#define F(D, V, B, Y) case D: r.vk = V; r.block = B; r.bytes = Y; break
    switch (f) {
    F(2, VK_FORMAT_R32G32B32A32_SFLOAT, 1, 16);
    F(6, VK_FORMAT_R32G32B32_SFLOAT, 1, 12);
    F(9, VK_FORMAT_R16G16B16A16_SFLOAT, 1, 8);
    F(10, VK_FORMAT_R16G16B16A16_SFLOAT, 1, 8);
    F(11, VK_FORMAT_R16G16B16A16_UNORM, 1, 8);
    F(12, VK_FORMAT_R16G16B16A16_UINT, 1, 8);
    F(13, VK_FORMAT_R16G16B16A16_SNORM, 1, 8);
    F(14, VK_FORMAT_R16G16B16A16_SINT, 1, 8);
    F(16, VK_FORMAT_R32G32_SFLOAT, 1, 8);
    F(24, VK_FORMAT_A2B10G10R10_UNORM_PACK32, 1, 4);
    F(26, VK_FORMAT_B10G11R11_UFLOAT_PACK32, 1, 4);
    F(27, VK_FORMAT_R8G8B8A8_UNORM, 1, 4);
    F(28, VK_FORMAT_R8G8B8A8_UNORM, 1, 4);
    F(29, VK_FORMAT_R8G8B8A8_SRGB, 1, 4);
    F(30, VK_FORMAT_R8G8B8A8_UINT, 1, 4);
    F(31, VK_FORMAT_R8G8B8A8_SNORM, 1, 4);
    F(32, VK_FORMAT_R8G8B8A8_SINT, 1, 4);
    F(34, VK_FORMAT_R16G16_SFLOAT, 1, 4);
    F(35, VK_FORMAT_R16G16_UNORM, 1, 4);
    F(36, VK_FORMAT_R16G16_UINT, 1, 4);
    F(37, VK_FORMAT_R16G16_SNORM, 1, 4);
    F(38, VK_FORMAT_R16G16_SINT, 1, 4);
    F(39, VK_FORMAT_R32_SFLOAT, 1, 4);
    F(40, VK_FORMAT_D32_SFLOAT, 1, 4);
    F(41, VK_FORMAT_R32_SFLOAT, 1, 4);
    F(44, g_d24s8, 1, 4);
    F(45, g_d24s8, 1, 4);
    F(46, g_d24s8, 1, 4);
    F(49, VK_FORMAT_R8G8_UNORM, 1, 2);
    F(53, VK_FORMAT_R16_UNORM, 1, 2);
    F(54, VK_FORMAT_R16_SFLOAT, 1, 2);
    F(55, VK_FORMAT_D16_UNORM, 1, 2);
    F(56, VK_FORMAT_R16_UNORM, 1, 2);
    F(57, VK_FORMAT_R16_UINT, 1, 2);
    F(61, VK_FORMAT_R8_UNORM, 1, 1);
    F(65, VK_FORMAT_R8_UNORM, 1, 1);
    F(71, VK_FORMAT_BC1_RGBA_UNORM_BLOCK, 4, 8);
    F(72, VK_FORMAT_BC1_RGBA_SRGB_BLOCK, 4, 8);
    F(74, VK_FORMAT_BC2_UNORM_BLOCK, 4, 16);
    F(75, VK_FORMAT_BC2_SRGB_BLOCK, 4, 16);
    F(77, VK_FORMAT_BC3_UNORM_BLOCK, 4, 16);
    F(78, VK_FORMAT_BC3_SRGB_BLOCK, 4, 16);
    F(80, VK_FORMAT_BC4_UNORM_BLOCK, 4, 8);
    F(83, VK_FORMAT_BC5_UNORM_BLOCK, 4, 16);
    F(87, VK_FORMAT_B8G8R8A8_UNORM, 1, 4);
    F(88, VK_FORMAT_B8G8R8A8_UNORM, 1, 4);
    F(91, VK_FORMAT_B8G8R8A8_SRGB, 1, 4);
    default: logf_("unknown DXGI format %u", f); break;
    }
#undef F
    return r;
}

/* Report once per format anything this driver cannot do with it. Using an unsupported format is undefined, and can differ by GPU. */
static uint8_t g_fmt_seen[256];
static void check_format(uint32_t dx, int use_vertex, int use_rt, uint32_t w, uint32_t h, uint32_t mips) {
    VkFormatProperties p;
    fmt_info fi;
    uint8_t bit = use_vertex ? 1 : use_rt ? 2 : 4;
    if (dx >= 256 || (g_fmt_seen[dx] & bit)) return;
    g_fmt_seen[dx] |= bit;
    fi = dx_format(dx);
    pvkGetPhysicalDeviceFormatProperties(g_phys, fi.vk, &p);
    logf_("format dxgi %u vk %d use %s first %ux%u mips %u: buffer %x optimal %x%s%s%s", dx, fi.vk, use_vertex ? "vertex" : use_rt ? "target" : "texture", w, h, mips, p.bufferFeatures, p.optimalTilingFeatures,
          use_vertex && !(p.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT) ? " NO-VERTEX" : "",
          !use_vertex && !(p.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) ? " NO-SAMPLE" : !use_vertex && !(p.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) ? " NO-LINEAR" : "",
          use_rt && !(p.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT) ? " NO-BLEND" : "");
}

static uint32_t mip_dim(uint32_t d, uint32_t m) { d >>= m; return d ? d : 1; }
