/* Process-local Steamworks stat cache. StoreStats commits to app storage only. */
enum { STAT_INT32 = 1, STAT_FLOAT = 2, MAX_STATS = 1024 };
typedef struct {
    char name[128];
    uint32_t type;
    uint32_t bits;
} OfflineStat;

static const char stats_magic[8] = "OASTAT1";
static OfflineStat local_stats[MAX_STATS];
static uint32_t local_stat_count;
static bool local_stats_dirty;
static unsigned stored_events_pending;
static pthread_mutex_t stat_lock = PTHREAD_MUTEX_INITIALIZER;

static void stat_path(char *path, size_t size)
{
    snprintf(path, size, "%s/offline-user-stats.bin", offline_root);
}

static void load_local_stats(void)
{
    char path[2304], magic[sizeof stats_magic];
    stat_path(path, sizeof path);
    FILE *file = fopen(path, "rb");
    if (!file) return;
    uint32_t count = 0;
    bool valid = fread(magic, 1, sizeof magic, file) == sizeof magic &&
        memcmp(magic, stats_magic, sizeof magic) == 0 &&
        fread(&count, sizeof count, 1, file) == 1 && count <= MAX_STATS &&
        fread(local_stats, sizeof local_stats[0], count, file) == count &&
        fgetc(file) == EOF && !ferror(file);
    fclose(file);
    for (uint32_t i = 0; valid && i < count; ++i) {
        const OfflineStat *entry = &local_stats[i];
        valid = entry->name[0] && memchr(entry->name, 0, sizeof entry->name) &&
            (entry->type == STAT_INT32 || entry->type == STAT_FLOAT);
    }
    if (valid) local_stat_count = count;
    else rt_log("OFFLINE: could not read local stats file");
}

static OfflineStat *find_local_stat(const char *name)
{
    for (uint32_t i = 0; i < local_stat_count; ++i)
        if (!strcmp(name, local_stats[i].name)) return &local_stats[i];
    return NULL;
}

static bool read_local_stat(const char *name, uint32_t type, uint32_t *bits)
{
    if (!offline_ready || !name || !bits) return false;
    pthread_mutex_lock(&stat_lock);
    OfflineStat *entry = find_local_stat(name);
    bool found = entry && entry->type == type;
    if (found) *bits = entry->bits;
    pthread_mutex_unlock(&stat_lock);
    return found;
}

static bool write_local_stat(const char *name, uint32_t type, uint32_t bits)
{
    if (!offline_ready || !name || !*name || strlen(name) >= sizeof local_stats[0].name)
        return false;
    if (type == STAT_FLOAT && (bits & 0x7f800000u) == 0x7f800000u) return false;
    pthread_mutex_lock(&stat_lock);
    OfflineStat *entry = find_local_stat(name);
    if (!entry && local_stat_count < MAX_STATS) {
        entry = &local_stats[local_stat_count++];
        *entry = (OfflineStat){ .type = type, .bits = bits };
        strcpy(entry->name, name);
        local_stats_dirty = true;
    }
    bool ok = entry && entry->type == type;
    if (ok && entry->bits != bits) {
        entry->bits = bits;
        local_stats_dirty = true;
    }
    pthread_mutex_unlock(&stat_lock);
    return ok;
}

static bool commit_local_stats(void)
{
    if (!offline_ready) return false;
    pthread_mutex_lock(&stat_lock);
    bool ok = true;
    if (local_stats_dirty) {
        char path[2304], tmp[2320];
        stat_path(path, sizeof path);
        snprintf(tmp, sizeof tmp, "%s.XXXXXX", path);
        int fd = mkstemp(tmp);
        FILE *file = fd < 0 ? NULL : fdopen(fd, "wb");
        ok = file && fwrite(stats_magic, 1, sizeof stats_magic, file) == sizeof stats_magic &&
            fwrite(&local_stat_count, sizeof local_stat_count, 1, file) == 1 &&
            fwrite(local_stats, sizeof local_stats[0], local_stat_count, file) == local_stat_count &&
            fflush(file) == 0 &&
#ifdef _WIN32
            _commit(fd) == 0;
#else
            fsync(fd) == 0;
#endif
        if (file) {
            if (fclose(file)) ok = false;
        } else if (fd >= 0) close(fd);
        if (ok) ok = rename(tmp, path) == 0;
        if (fd >= 0 && !ok) unlink(tmp);
        if (ok) local_stats_dirty = false;
    }
    if (ok) ++stored_events_pending;
    pthread_mutex_unlock(&stat_lock);
    return ok;
}
