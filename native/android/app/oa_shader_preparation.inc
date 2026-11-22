/* Rebuild locally observed pipeline variants before the first draw that uses them. */
#include <dirent.h>
#include <errno.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

enum { OA_PREPARED_VARIANTS = 128, OA_PREPARED_SHADER_BYTES = 128 * 1024 };
enum { OA_PRESENT_LAYOUT = 1, OA_PRESENT_BLEND = 2, OA_PRESENT_DEPTH = 4, OA_PRESENT_RASTER = 8 };

typedef struct {
    uint64_t vertex_shader, pixel_shader;
    uint32_t states, topology, element_count;
    VkFormat color[8], depth_format;
    oa_elem elements[32];
    uint32_t blend_header[2], blend_targets[64], depth_state[13], raster_state[10];
} OaPipelineRecipe;

static const size_t recipe_size =
    offsetof(OaPipelineRecipe, raster_state) + sizeof(((OaPipelineRecipe *)0)->raster_state);

typedef struct {
    uint8_t magic[8], device_uuid[VK_UUID_SIZE];
    uint32_t vertex_bytes, pixel_bytes;
    uint64_t checksum;
    OaPipelineRecipe recipe;
    oa_shader_info vertex_info, pixel_info;
} OaPreparedHeader;

typedef struct {
    OaPreparedHeader header;
    uint32_t shaders[];
} OaPreparedJob;

_Static_assert(offsetof(OaPreparedJob, shaders) == sizeof(OaPreparedHeader), "recipe file layout");

typedef struct {
    OaPipelineRecipe recipe;
    VkPipeline pipeline;
    uint64_t vertex_interface, pixel_interface;
} OaPreparedPipeline;

static const uint8_t preparation_magic[8] = {'O', 'A', 'V', 'A', 'R', 'I', 'A', 1};
static uint8_t preparation_device_uuid[VK_UUID_SIZE];
static char preparation_dir[4096];
static FILE *pipeline_timing_log;
static unsigned pipeline_timing_rows;
static OaPreparedPipeline prepared_pipelines[OA_PREPARED_VARIANTS];
static unsigned prepared_count, preparation_file_count;
static OaPreparedJob *preparation_jobs[OA_PREPARED_VARIANTS];
static unsigned preparation_read_index, preparation_write_index;
static uint64_t recorded_recipes[OA_PREPARED_VARIANTS];
static unsigned recorded_recipe_count;
static pthread_mutex_t preparation_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t preparation_has_jobs = PTHREAD_COND_INITIALIZER;

static void canonicalize_recipe(OaPipelineRecipe *recipe)
{
    /* D3D11 descriptors have padding and inactive state. Only the fields
       used to create the Vulkan pipeline identify a variant across launches. */
    for (uint32_t i = 0; i < recipe->element_count && i < 32; ++i) {
        oa_elem *element = &recipe->elements[i];
        element->name_hash = 0;
        element->cls = !!element->cls;
        element->step = 0;
    }

    uint32_t attachments = 8;
    while (attachments && !recipe->color[attachments - 1]) --attachments;
    recipe->blend_header[0] = 0;
    recipe->blend_header[1] = !!recipe->blend_header[1];
    if (attachments && !recipe->blend_header[1]) attachments = 1;
    for (unsigned i = 0; i < 8; ++i) {
        uint32_t *target = &recipe->blend_targets[i * 8];
        if (i >= attachments) {
            memset(target, 0, 8 * sizeof(*target));
        } else {
            target[0] = !!target[0];
            if (!target[0]) memset(target + 1, 0, 6 * sizeof(*target));
            target[7] &= 0xf;
        }
    }

    uint32_t *depth = recipe->depth_state;
    depth[0] = !!depth[0];
    depth[1] = depth[0] && depth[1];
    depth[3] = !!depth[3];
    depth[4] &= 0xffff;
    if (!depth[3]) memset(depth + 4, 0, 9 * sizeof(*depth));

    uint32_t *raster = recipe->raster_state;
    raster[2] = !!raster[2];
    raster[6] = !!raster[6];
    memset(raster + 7, 0, 3 * sizeof(*raster));
}

static OaPipelineRecipe describe_pipeline(const pipe_key *key)
{
    OaPipelineRecipe recipe;
    memset(&recipe, 0, sizeof(recipe));
    recipe.vertex_shader = key->vs->recipe_id;
    recipe.pixel_shader = key->ps ? key->ps->recipe_id : 0;
    recipe.topology = key->topology;
    memcpy(recipe.color, key->rt, sizeof(recipe.color));
    recipe.depth_format = key->ds;
    if (key->layout) {
        recipe.states |= OA_PRESENT_LAYOUT;
        recipe.element_count = (uint32_t)key->layout->n;
        if (recipe.element_count <= 32)
            memcpy(recipe.elements, key->layout->e, sizeof(oa_elem) * recipe.element_count);
    }
    if (key->blend) {
        const oa_blend *blend = (const oa_blend *)key->blend;
        recipe.states |= OA_PRESENT_BLEND;
        memcpy(recipe.blend_header, blend->s.desc, sizeof(recipe.blend_header));
        memcpy(recipe.blend_targets, blend->rt, sizeof(recipe.blend_targets));
    }
    if (key->depth) {
        recipe.states |= OA_PRESENT_DEPTH;
        memcpy(recipe.depth_state, key->depth->desc, sizeof(recipe.depth_state));
    }
    if (key->raster) {
        recipe.states |= OA_PRESENT_RASTER;
        memcpy(recipe.raster_state, key->raster->desc, sizeof(recipe.raster_state));
    }
    canonicalize_recipe(&recipe);
    return recipe;
}

static int shader_bytes_valid(uint32_t bytes)
{
    return bytes >= 5 * sizeof(uint32_t) && bytes <= OA_PREPARED_SHADER_BYTES && !(bytes % 4);
}

static size_t prepared_shader_bytes(const OaPreparedHeader *header)
{
    return (size_t)header->vertex_bytes + header->pixel_bytes;
}

static uint64_t preparation_checksum(const OaPreparedJob *job)
{
    const size_t offset = offsetof(OaPreparedJob, header.recipe);
    return shader_recipe_id((const uint8_t *)job + offset,
                            sizeof(job->header) - offset + prepared_shader_bytes(&job->header));
}

static int prepared_header_valid(const OaPreparedHeader *h)
{
    if (memcmp(h->magic, preparation_magic, sizeof(h->magic)) ||
        memcmp(h->device_uuid, preparation_device_uuid, sizeof(h->device_uuid)) ||
        !shader_bytes_valid(h->vertex_bytes) ||
        (h->pixel_bytes ? !shader_bytes_valid(h->pixel_bytes) || !h->recipe.pixel_shader ||
                          !h->pixel_info.is_pixel : h->recipe.pixel_shader != 0) ||
        h->vertex_info.is_pixel || h->vertex_info.input_count < 0 ||
        h->vertex_info.input_count > OA_MAX_SIG ||
        h->recipe.states & ~(OA_PRESENT_LAYOUT | OA_PRESENT_BLEND | OA_PRESENT_DEPTH | OA_PRESENT_RASTER) ||
        h->recipe.element_count > 32)
        return 0;

    for (uint32_t i = 0; i < h->recipe.element_count; ++i) {
        const oa_elem *element = &h->recipe.elements[i];
        if (element->slot >= 16 || !memchr(element->name, 0, sizeof(element->name))) return 0;
    }
    for (int i = 0; i < h->vertex_info.input_count; ++i)
        if (!memchr(h->vertex_info.inputs[i].name, 0, sizeof(h->vertex_info.inputs[i].name))) return 0;
    return 1;
}

static VkShaderModule prepared_shader_module(const uint32_t *code, uint32_t bytes)
{
    VkShaderModule module = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo info = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = bytes;
    info.pCode = code;
    if (code[0] != 0x07230203 ||
        pvkCreateShaderModule(g_dev, &info, NULL, &module) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return module;
}

static VkPipeline compile_prepared_pipeline(const OaPreparedJob *job)
{
    const OaPreparedHeader *header = &job->header;
    const OaPipelineRecipe *recipe = &header->recipe;
    oa_shader vs = {.info = header->vertex_info};
    oa_shader ps = {.info = header->pixel_info};
    oa_layout layout = {.n = (int)recipe->element_count};
    oa_blend blend = {0};
    oa_state depth = {0}, raster = {0};
    memcpy(layout.e, recipe->elements, sizeof(layout.e));
    memcpy(blend.s.desc, recipe->blend_header, sizeof(recipe->blend_header));
    memcpy(blend.rt, recipe->blend_targets, sizeof(blend.rt));
    memcpy(depth.desc, recipe->depth_state, sizeof(recipe->depth_state));
    memcpy(raster.desc, recipe->raster_state, sizeof(recipe->raster_state));
    pipe_key key = {
        .vs = &vs,
        .ps = header->pixel_bytes ? &ps : NULL,
        .layout = (recipe->states & OA_PRESENT_LAYOUT) ? &layout : NULL,
        .blend = (recipe->states & OA_PRESENT_BLEND) ? &blend.s : NULL,
        .depth = (recipe->states & OA_PRESENT_DEPTH) ? &depth : NULL,
        .raster = (recipe->states & OA_PRESENT_RASTER) ? &raster : NULL,
        .topology = recipe->topology,
        .ds = recipe->depth_format,
    };
    memcpy(key.rt, recipe->color, sizeof(key.rt));

    vs.module = prepared_shader_module(job->shaders, header->vertex_bytes);
    if (key.ps)
        ps.module = prepared_shader_module(job->shaders + header->vertex_bytes / 4, header->pixel_bytes);
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptors;
    VkPipelineLayout program;
    if (vs.module && (!key.ps || ps.module) && create_program_layout(&vs, key.ps, &descriptors, &program)) {
        pipeline = build_pipeline(&key, program, VK_NULL_HANDLE);
        pvkDestroyPipelineLayout(g_dev, program, NULL);
        pvkDestroyDescriptorSetLayout(g_dev, descriptors, NULL);
    }
    if (ps.module) pvkDestroyShaderModule(g_dev, ps.module, NULL);
    if (vs.module) pvkDestroyShaderModule(g_dev, vs.module, NULL);
    return pipeline;
}

static OaPreparedJob *read_prepared_job(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    OaPreparedHeader header;
    OaPreparedJob *job = NULL;
    if (fread(&header, sizeof(header), 1, file) == 1 && prepared_header_valid(&header)) {
        size_t bytes = prepared_shader_bytes(&header);
        job = malloc(sizeof(*job) + bytes);
        if (job) {
            job->header = header;
            if (fread(job->shaders, bytes, 1, file) != 1 || fgetc(file) != EOF ||
                ferror(file) || preparation_checksum(job) != header.checksum ||
                shader_recipe_id(job->shaders, header.vertex_bytes) != header.recipe.vertex_shader ||
                (header.pixel_bytes &&
                 shader_recipe_id(job->shaders + header.vertex_bytes / 4, header.pixel_bytes) !=
                     header.recipe.pixel_shader)) {
                free(job);
                job = NULL;
            }
            if (job) canonicalize_recipe(&job->header.recipe);
        }
    }
    fclose(file);
    return job;
}

static void prepare_recorded_pipelines(void)
{
    DIR *directory = opendir(preparation_dir);
    if (!directory) return;
    struct dirent *entry;
    unsigned files = 0, count = 0, examined = 0;
    LARGE_INTEGER begin, finished;
    QueryPerformanceCounter(&begin);
    while (files < OA_PREPARED_VARIANTS && examined++ < OA_PREPARED_VARIANTS * 4 &&
           (entry = readdir(directory))) {
        if (strspn(entry->d_name, "0123456789abcdef") != 16 ||
            strcmp(entry->d_name + 16, ".variant"))
            continue;
        ++files;
        if (!prepare_shader_variants) continue;
        char path[4160];
        snprintf(path, sizeof(path), "%s/%s", preparation_dir, entry->d_name);
        OaPreparedJob *job = read_prepared_job(path);
        if (!job) continue;
        const uint64_t vertex_interface = shader_recipe_id(&job->header.vertex_info,
                                                           sizeof(job->header.vertex_info));
        const uint64_t pixel_interface = job->header.pixel_bytes ?
            shader_recipe_id(&job->header.pixel_info, sizeof(job->header.pixel_info)) : 0;
        unsigned duplicate = 0;
        while (duplicate < count &&
               (prepared_pipelines[duplicate].vertex_interface != vertex_interface ||
                prepared_pipelines[duplicate].pixel_interface != pixel_interface ||
                memcmp(&prepared_pipelines[duplicate].recipe, &job->header.recipe, recipe_size)))
            ++duplicate;
        VkPipeline pipeline = duplicate == count ? compile_prepared_pipeline(job) : VK_NULL_HANDLE;
        if (pipeline) {
            prepared_pipelines[count++] = (OaPreparedPipeline){
                .recipe = job->header.recipe, .pipeline = pipeline,
                .vertex_interface = vertex_interface, .pixel_interface = pixel_interface,
            };
            __atomic_store_n(&prepared_count, count, __ATOMIC_RELEASE);
        }
        free(job);
    }
    closedir(directory);
    preparation_file_count = files;
    if (files >= OA_PREPARED_VARIANTS)
        __atomic_store_n(&prepared_recipe_store_full, 1, __ATOMIC_RELAXED);
    QueryPerformanceCounter(&finished);
    logf_("SHADER_PREPARATION ready=%u recipes=%u elapsed_ms=%.1f", count, files,
          (double)(finished.QuadPart - begin.QuadPart) / 1000000.0);
}

static void save_prepared_job(OaPreparedJob *job)
{
    if (preparation_file_count >= OA_PREPARED_VARIANTS) return;
    char path[4160], temporary[4176];
    uint64_t name = shader_recipe_id(&job->header.recipe, recipe_size);
    snprintf(path, sizeof(path), "%s/%016llx.variant", preparation_dir, (unsigned long long)name);
    if (access(path, F_OK) == 0) return;
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    FILE *file = fopen(temporary, "wb");
    if (!file) return;
    job->header.checksum = preparation_checksum(job);
    int written = fwrite(job, sizeof(*job) + prepared_shader_bytes(&job->header), 1, file) == 1;
    if (fclose(file) || !written || rename(temporary, path)) {
        unlink(temporary);
        return;
    }
    if (++preparation_file_count >= OA_PREPARED_VARIANTS)
        __atomic_store_n(&prepared_recipe_store_full, 1, __ATOMIC_RELAXED);
}

static void *shader_preparation_worker(void *context)
{
    (void)context;
    setpriority(PRIO_PROCESS, 0, 10);
    prepare_recorded_pipelines();
    if (!record_shader_variants) return NULL;
    for (;;) {
        pthread_mutex_lock(&preparation_lock);
        while (preparation_read_index == preparation_write_index)
            pthread_cond_wait(&preparation_has_jobs, &preparation_lock);
        OaPreparedJob *job = preparation_jobs[preparation_read_index++];
        pthread_mutex_unlock(&preparation_lock);
        save_prepared_job(job);
        free(job);
    }
}

static void start_shader_preparation(void)
{
    if (gpu_timing_log) {
        pipeline_timing_log = open_renderer_file(".oa-pacing-benchmark/pipeline-timing.csv", "w");
        if (pipeline_timing_log) fputs("frame,start_ns,build_us,prepared\n", pipeline_timing_log);
    }
    if (!prepare_shader_variants && !record_shader_variants) return;
    const char *root = getenv("OA_OFFLINE_ROOT");
    if (!root || !*root) goto unavailable;
    int length = snprintf(preparation_dir, sizeof(preparation_dir), "%s/vulkan-variants-v1", root);
    if (length <= 0 || length >= (int)sizeof(preparation_dir)) goto unavailable;
    if (mkdir(preparation_dir, 0700) && errno != EEXIST) goto unavailable;
    VkPhysicalDeviceProperties properties;
    pvkGetPhysicalDeviceProperties(g_phys, &properties);
    memcpy(preparation_device_uuid, properties.pipelineCacheUUID, sizeof(preparation_device_uuid));
    pthread_t thread;
    if (pthread_create(&thread, NULL, shader_preparation_worker, NULL) == 0) {
        pthread_detach(thread);
        return;
    }

unavailable:
    prepare_shader_variants = record_shader_variants = 0;
    logf_("SHADER_PREPARATION unavailable");
}

static void record_pipeline_recipe(const pipe_key *key, const OaPipelineRecipe *recipe)
{
    if (__atomic_load_n(&prepared_recipe_store_full, __ATOMIC_RELAXED) ||
        recorded_recipe_count >= OA_PREPARED_VARIANTS || recipe->element_count > 32 ||
        !key->vs->recipe_code || (key->ps && !key->ps->recipe_code))
        return;
    uint64_t id = shader_recipe_id(recipe, recipe_size);
    for (unsigned i = 0; i < recorded_recipe_count; ++i)
        if (recorded_recipes[i] == id) return;
    size_t vertex_bytes = key->vs->recipe_bytes;
    size_t pixel_bytes = key->ps ? key->ps->recipe_bytes : 0;
    OaPreparedJob *job = calloc(1, sizeof(*job) + vertex_bytes + pixel_bytes);
    if (!job) return;
    memcpy(job->header.magic, preparation_magic, sizeof(job->header.magic));
    memcpy(job->header.device_uuid, preparation_device_uuid, sizeof(job->header.device_uuid));
    job->header.vertex_bytes = (uint32_t)vertex_bytes;
    job->header.pixel_bytes = (uint32_t)pixel_bytes;
    job->header.recipe = *recipe;
    job->header.vertex_info = key->vs->info;
    memcpy(job->shaders, key->vs->recipe_code, vertex_bytes);
    if (key->ps) {
        job->header.pixel_info = key->ps->info;
        memcpy(job->shaders + vertex_bytes / 4, key->ps->recipe_code, pixel_bytes);
    }
    recorded_recipes[recorded_recipe_count++] = id;
    pthread_mutex_lock(&preparation_lock);
    preparation_jobs[preparation_write_index++] = job;
    pthread_cond_signal(&preparation_has_jobs);
    pthread_mutex_unlock(&preparation_lock);
}

static VkPipeline get_prepared_pipeline(const pipe_key *key, VkPipelineLayout layout)
{
    if (!prepare_shader_variants && !record_shader_variants && !pipeline_timing_log) {
        VkPipeline pipeline = build_pipeline(key, layout, g_pcache);
        if (pipeline) pipeline_cache_note_creation();
        return pipeline;
    }

    VkPipeline pipeline = VK_NULL_HANDLE;
    LARGE_INTEGER start, finished;
    QueryPerformanceCounter(&start);
    OaPipelineRecipe recipe = {0};
    unsigned count = __atomic_load_n(&prepared_count, __ATOMIC_ACQUIRE);
    if (count || record_shader_variants)
        recipe = describe_pipeline(key);
    const uint64_t pixel_interface = key->ps ? key->ps->recipe_interface_id : 0;
    for (unsigned i = 0; i < count; ++i) {
        if (prepared_pipelines[i].vertex_interface == key->vs->recipe_interface_id &&
            prepared_pipelines[i].pixel_interface == pixel_interface &&
            !memcmp(&prepared_pipelines[i].recipe, &recipe, recipe_size)) {
            pipeline = prepared_pipelines[i].pipeline;
            break;
        }
    }
    int prepared = !!pipeline;
    if (!pipeline) {
        pipeline = build_pipeline(key, layout, g_pcache);
        if (pipeline) pipeline_cache_note_creation();
    }
    QueryPerformanceCounter(&finished);
    double build_us = (double)(finished.QuadPart - start.QuadPart) / 1000.0;
    if (pipeline && !prepared && record_shader_variants && build_us >= 750.0)
        record_pipeline_recipe(key, &recipe);
    if (pipeline_timing_log) {
        fprintf(pipeline_timing_log, "%llu,%llu,%.3f,%d\n", (unsigned long long)g_frame,
                (unsigned long long)start.QuadPart, build_us, prepared);
        if (!(++pipeline_timing_rows % 16)) fflush(pipeline_timing_log);
    }
    return pipeline;
}
