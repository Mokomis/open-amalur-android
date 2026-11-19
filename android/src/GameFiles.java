package org.openamalur.originalengine;

import android.content.ContentResolver;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.provider.DocumentsContract.Document;
import android.system.ErrnoException;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.security.SecureRandom;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.Locale;
import java.util.Map;

final class GameFiles {
    static final String IMPORTED = "imported-install";
    static final String DIRECT_PATCH = "veil-patch_0.pak";
    private static final String DIRECT_READY = "sd-direct.ready";
    private static final String BINK_HASH = "36810b3318acd07a5b4e78fe72db163ed63880ab2eb2d9b5bda0331ded32a9c3";
    private static final String EXE_HASH = "16a400f6e8fc10dbe446a9e57717de9fbe975ef17c004f4ca405e2e4e09cb314";
    private static final long PATCH_DIRECTORY = 0x42b93653L;
    private static final long PATCH_BYTES = 1119582387L;
    private static final String PATCH_HASH = "ba34a31a0acb861d7fd1fba118d094e00e5d572e0d3f10df142815750bf44e4c";
    private static final String COMPILER_HASH = "2f23182ec6f4889397ac4bf03d62536136c5bdba825c7d2c4ef08c827f3a8a1c";
    private static final String REBASED_HASH = "e46ac5ccb6ab66198eb5b175ee334b928cccb6bd56c9394c59d3ef34bc15ca46";
    private static final String FATESWORN_DIR = "data_1278260";
    private static final String FATESWORN_ARCHIVE = "dlc_mit_0.pak";
    private static final long FATESWORN_BYTES = 5098328372L;
    private static final String FATESWORN_HASH = "78ff3faaf962448340433bdefb600369d068536183c7dfbad07b58de05a1ecee";
    private static final String TITLE_VIDEO = "well.bik";
    private static final long TITLE_VIDEO_BYTES = 2884472L;
    private static final String TITLE_VIDEO_HASH = "b3fe11aa874d966ebb54584694a27aca3f9074d14cf41a696355a6e70c2197f1";
    private static final String[] BASE_ARCHIVES = {
            "DATA_0.pak", "initial_0.pak", "patch_0.pak", "en_us_0.pak", "de_de_0.pak", "fr_fr_0.pak"
    };
    // ColorOS can retain a stale size for a file added to a folder while it is open.
    private static final long[] BASE_ARCHIVE_BYTES = {
            17976535039L, 5230887138L, PATCH_BYTES, 4384534917L, 5434418079L, 4402112832L
    };

    static final class CompilerRequired extends IOException {
        CompilerRequired() {
            super("Select the matching 32-bit Microsoft d3dcompiler_43.dll from Windows SysWOW64.");
        }
    }

    interface Progress {
        void report(String name, long copied, long total);
    }

    private static final class Entry {
        final Uri uri;
        final String id;
        final String name;
        final boolean directory;
        final long size;

        Entry(Uri uri, String id, String name, boolean directory, long size) {
            this.uri = uri;
            this.id = id;
            this.name = name;
            this.directory = directory;
            this.size = size;
        }
    }

    static boolean ready(Context context, File directory) {
        if (GameStorage.directConfigured(context) && !directReady(context, directory)) return false;
        if (directReady(context, directory)) return hasDirectTitleVideo(GameStorage.sourceRoot(context));
        File installed = installedGame(context, directory);
        return installed != null && hasTitleVideo(installed);
    }

    static boolean needsTitleRepair(Context context, File directory) {
        if (GameStorage.directConfigured(context)) return false;
        File installed = installedGame(context, directory);
        return installed != null && !hasTitleVideo(installed);
    }

    private static boolean hasTitleVideo(File installed) {
        File video = new File(installed, "game/data/videos/" + TITLE_VIDEO);
        return video.isFile() && video.length() == TITLE_VIDEO_BYTES;
    }

    private static boolean hasDirectTitleVideo(File source) {
        File video = new File(source, "data/videos/" + TITLE_VIDEO);
        return video.isFile() && video.length() == TITLE_VIDEO_BYTES;
    }

    static boolean hasFatesworn(Context context, File storage) {
        if (directReady(context, storage)) {
            File archive = fateswornArchive(GameStorage.sourceRoot(context));
            return archive.isFile() && archive.length() == FATESWORN_BYTES;
        }
        File installed = installedGame(context, storage);
        if (installed == null) return false;
        File archive = fateswornArchive(new File(installed, "game"));
        return archive.isFile() && archive.length() == FATESWORN_BYTES;
    }

    private static File fateswornArchive(File game) {
        return new File(new File(game, FATESWORN_DIR), FATESWORN_ARCHIVE);
    }

    private static File installedGame(Context context, File directory) {
        if (directory == null) return null;
        File imported = new File(directory, IMPORTED);
        File installed = new File(imported, ".ready").isFile() ? imported : directory;
        return valid(installed, !hasNativeReflection(context)) ? installed : null;
    }

    private static boolean directReady(Context context, File storage) {
        File source = GameStorage.sourceRoot(context);
        return storage != null && source != null
                && new File(storage, DIRECT_READY).isFile()
                && new File(storage, DIRECT_PATCH).isFile()
                && validDirect(context, source);
    }

    private static boolean validDirect(Context context, File source) {
        if (!new File(source, "koa.exe").isFile()
                || !new File(source, "binkw32.dll").isFile()) return false;
        for (int i = 0; i < BASE_ARCHIVES.length; ++i) {
            File archive = new File(source, "data/" + BASE_ARCHIVES[i]);
            if (!archive.isFile() || archive.length() != BASE_ARCHIVE_BYTES[i]) return false;
        }
        return hasNativeReflection(context) || new File(source, "d3dcompiler_43.dll").isFile();
    }

    private static boolean hasNativeReflection(Context context) {
        try (InputStream marker = context.getAssets().open("local-build/native-reflection")) {
            return marker.read() == '1';
        } catch (IOException absent) {
            return false;
        }
    }

    private static boolean valid(File directory, boolean requiresCompiler) {
        if (requiresCompiler && !new File(directory, "d3dcompiler_43.dll").isFile()) return false;
        for (String name : new String[]{"koa.exe", "binkw32.dll"}) {
            if (!new File(directory, name).isFile()) return false;
        }
        for (String name : BASE_ARCHIVES) {
            if (!new File(directory, "game/data/" + name).isFile()) return false;
        }
        return true;
    }

    static void prepareLocalProfile(File storage) throws IOException {
        if (storage == null) throw new IOException("Android game storage is unavailable.");
        makeDirectory(new File(storage, "offline-saves/working"));
        File profile = new File(storage, "offline-profile.bin");
        if (profile.exists()) return;
        // The offline engine expects a nonzero local account in this field.
        // This generated identity stays inside the app and makes no Steam request.
        long localAccount = 0x0110000100000000L | (1L + new SecureRandom().nextInt(Integer.MAX_VALUE));
        ByteBuffer fields = ByteBuffer.allocate(31).order(ByteOrder.LITTLE_ENDIAN);
        fields.put(new byte[]{'O', 'A', 'O', 'F', 'F', '0', '1', 0});
        fields.putLong(localAccount).putInt(0).putInt(7);
        fields.put(new byte[]{'e', 'n', 'g', 'l', 'i', 's', 'h'});
        File temporary = new File(storage, "offline-profile.bin.setup");
        Files.write(temporary.toPath(), fields.array());
        Files.move(temporary.toPath(), profile.toPath());
        File resolution = new File(storage, "render-scale");
        if (!resolution.exists()) Files.write(resolution.toPath(), new byte[]{'1', '0', '0', '\n'});
    }

    private final Context context;
    private final ContentResolver resolver;
    private final Uri tree;
    private final Progress progress;
    private final byte[] buffer = new byte[1024 * 1024];

    GameFiles(Context context, Uri tree, Progress progress) {
        this.context = context;
        resolver = context.getContentResolver();
        this.tree = tree;
        this.progress = progress;
    }

    private Map<String, Entry> list(String parent) throws IOException {
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, parent);
        String[] fields = {Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME,
                Document.COLUMN_MIME_TYPE, Document.COLUMN_SIZE};
        Map<String, Entry> entries = new LinkedHashMap<>();
        try (Cursor rows = resolver.query(children, fields, null, null, null)) {
            if (rows == null) throw new IOException("Android could not read the selected folder.");
            while (rows.moveToNext()) {
                String name = rows.getString(1);
                if (name == null || name.isEmpty() || name.startsWith(".") ||
                        name.contains("/") || name.contains("\\")) continue;
                String id = rows.getString(0);
                Entry entry = new Entry(DocumentsContract.buildDocumentUriUsingTree(tree, id),
                        id, name, Document.MIME_TYPE_DIR.equals(rows.getString(2)),
                        rows.isNull(3) ? -1 : rows.getLong(3));
                entries.put(name.toLowerCase(Locale.ROOT), entry);
            }
        }
        return entries;
    }

    private static Entry require(Map<String, Entry> entries, String name, boolean directory) throws IOException {
        Entry entry = entries.get(name.toLowerCase(Locale.ROOT));
        if (entry == null || entry.directory != directory) {
            throw new IOException("Missing " + name + ". Select the Re-Reckoning folder that contains koa.exe and data.");
        }
        return entry;
    }

    void install(Uri shaderCompiler) throws IOException {
        synchronized (GameFiles.class) {
            importDirect();
        }
    }

    private void importDirect() throws IOException {
        File storage = GameStorage.requireRoot(context);
        File source = GameStorage.resolveSource(context, tree);
        if (android.os.Build.VERSION.SDK_INT < 30
                || !android.os.Environment.isExternalStorageManager(source))
            throw new IOException("Direct play requires Android 11 or newer and All files access.");
        if (!validDirect(context, source) || !hasDirectTitleVideo(source))
            throw new IOException("The selected folder needs the supported koa.exe, binkw32.dll, data archives, and title video.");
        if (storage.getUsableSpace() < PATCH_BYTES + 400_000_000L)
            throw new IOException("The selected volume needs about 1.5 GB free for Open Amalur's patched archive.");
        File stage = new File(storage, DIRECT_PATCH + ".partial");
        File patch = new File(storage, DIRECT_PATCH);
        try {
            Files.deleteIfExists(stage.toPath());
            progress.report("Verifying SD game archive", 0, PATCH_BYTES);
            File retail = new File(source, "data/patch_0.pak");
            try (InputStream input = Files.newInputStream(retail.toPath())) {
                copyStream(input, stage, "patch_0.pak", PATCH_HASH, PATCH_BYTES);
            }
            if (hasVeilRecipe(context)) new VeilPatch(context.getAssets(), PATCH_BYTES,
                    PATCH_DIRECTORY, PATCH_HASH).applyToRetail(stage,
                    new File(source, "data/initial_0.pak"), progress);
            try {
                android.system.Os.rename(stage.getPath(), patch.getPath());
            } catch (ErrnoException error) {
                throw new IOException("Android could not finish the patched archive on this volume.", error);
            }
            GameStorage.saveSource(context, source);
            Files.write(new File(storage, DIRECT_READY).toPath(), new byte[]{'1'});
        } finally {
            Files.deleteIfExists(stage.toPath());
        }
    }

    void addFatesworn() throws IOException {
        synchronized (GameFiles.class) {
            if (GameStorage.directConfigured(context))
                throw new IOException("Add Fatesworn to the original game folder, then restart Open Amalur.");
            File installed = installedGame(context, GameStorage.requireRoot(context));
            if (installed == null) throw new IOException("Open Amalur could not find the installed game.");
            Map<String, Entry> files = list(DocumentsContract.getTreeDocumentId(tree));
            Entry depot = files.get(FATESWORN_DIR);
            if (depot != null && depot.directory) files = list(depot.id);
            Entry source = files.get(FATESWORN_ARCHIVE);
            if (source == null || source.directory) {
                throw new IOException("Fatesworn was not found. Choose the Re-Reckoning folder containing "
                        + FATESWORN_DIR + ", or select that folder directly.");
            }
            File target = fateswornArchive(new File(installed, "game"));
            makeDirectory(target.getParentFile());
            File temp = new File(target.getParentFile(), "." + FATESWORN_ARCHIVE + ".partial");
            try {
                Files.deleteIfExists(temp.toPath());
                copy(source, temp, FATESWORN_HASH, FATESWORN_BYTES);
                Files.move(temp.toPath(), target.toPath(), StandardCopyOption.ATOMIC_MOVE,
                        StandardCopyOption.REPLACE_EXISTING);
            } finally {
                Files.deleteIfExists(temp.toPath());
            }
        }
    }

    private void importGame(Uri shaderCompiler) throws IOException {
        File storage = GameStorage.requireRoot(context);
        Map<String, Entry> root = list(DocumentsContract.getTreeDocumentId(tree));
        Map<String, Entry> data = list(require(root, "data", true).id);
        Map<String, Entry> videos = list(require(data, "videos", true).id);
        Entry title = require(videos, TITLE_VIDEO, false);
        File installed = installedGame(context, storage);
        if (installed != null && !hasTitleVideo(installed)) {
            File videoDirectory = new File(installed, "game/data/videos");
            makeDirectory(videoDirectory);
            File stagedTitle = new File(videoDirectory, "." + TITLE_VIDEO + ".partial");
            try {
                copy(title, stagedTitle, TITLE_VIDEO_HASH, TITLE_VIDEO_BYTES);
                Files.move(stagedTitle.toPath(), new File(videoDirectory, TITLE_VIDEO).toPath(),
                        java.nio.file.StandardCopyOption.ATOMIC_MOVE,
                        java.nio.file.StandardCopyOption.REPLACE_EXISTING);
            } finally {
                Files.deleteIfExists(stagedTitle.toPath());
            }
            return;
        }
        Entry executable = require(root, "koa.exe", false);
        Entry bink = require(root, "binkw32.dll", false);
        for (String name : BASE_ARCHIVES) require(data, name, false);
        boolean requiresCompiler = !hasNativeReflection(context);
        if (requiresCompiler && shaderCompiler == null) {
            Entry compilerInFolder = root.get("d3dcompiler_43.dll");
            if (compilerInFolder != null && !compilerInFolder.directory) shaderCompiler = compilerInFolder.uri;
            if (shaderCompiler == null && !LocalEngine.downloadEnabled(context)) throw new CompilerRequired();
        }
        Entry depot = root.get(FATESWORN_DIR);
        Entry fatesworn = depot == null ? null
                : require(list(require(root, FATESWORN_DIR, true).id), FATESWORN_ARCHIVE, false);
        long required = 23741952L + 198144L + TITLE_VIDEO_BYTES + 1_000_000_000L;
        for (long size : BASE_ARCHIVE_BYTES) required += size;
        for (Entry video : videos.values()) {
            String name = video.name.toLowerCase(Locale.ROOT);
            if (!video.directory && name.endsWith(".bik") && !name.equals(TITLE_VIDEO)
                    && video.size > 0) required += video.size;
        }
        if (fatesworn != null) required += FATESWORN_BYTES;
        File destination = new File(storage, IMPORTED);
        if (destination.exists()) throw new IOException("An imported game already exists.");
        File stage = new File(storage, IMPORTED + "-partial");
        removePartial(stage);
        if (storage.getUsableSpace() < required) {
            throw new IOException(String.format(Locale.ROOT,
                    "Not enough free space at the selected install location. "
                            + "About %.1f GB is needed. Choose an SD card with enough room or free space and retry.",
                    required / 1_000_000_000.0));
        }
        makeDirectory(stage);
        try {
            copy(executable, new File(stage, "koa.exe"), EXE_HASH, 23741952L);
            copy(bink, new File(stage, "binkw32.dll"), BINK_HASH, 198144L);
            if (requiresCompiler) {
                File compiler = new File(stage, "d3dcompiler_43.dll");
                if (shaderCompiler == null) {
                    progress.report("Microsoft’s shader compiler", 0, 0);
                    prepareCompiler(CompilerDownload.fetch(), compiler);
                } else {
                    prepareCompiler(shaderCompiler, compiler);
                }
            }
            File game = new File(stage, "game");
            File gameData = new File(game, "data");
            makeDirectory(gameData);
            File patch = new File(gameData, "patch_0.pak");
            copy(require(data, "patch_0.pak", false), patch, PATCH_HASH, BASE_ARCHIVE_BYTES[2]);
            for (int i = 0; i < BASE_ARCHIVES.length; i++) {
                String name = BASE_ARCHIVES[i];
                if (!name.equals("patch_0.pak")) {
                    copy(require(data, name, false), new File(gameData, name), null, BASE_ARCHIVE_BYTES[i]);
                }
            }
            if (hasVeilRecipe(context)) {
                new VeilPatch(context.getAssets(), PATCH_BYTES, PATCH_DIRECTORY, PATCH_HASH)
                        .applyToRetail(patch, new File(gameData, "initial_0.pak"), progress);
            }
            File target = new File(gameData, "videos");
            makeDirectory(target);
            copy(title, new File(target, TITLE_VIDEO), TITLE_VIDEO_HASH, TITLE_VIDEO_BYTES);
            for (Entry video : videos.values()) {
                String name = video.name.toLowerCase(Locale.ROOT);
                if (!video.directory && name.endsWith(".bik") && !name.equals(TITLE_VIDEO)) {
                    copy(video, new File(target, video.name), null, video.size);
                }
            }
            if (fatesworn != null) {
                File archive = fateswornArchive(game);
                makeDirectory(archive.getParentFile());
                copy(fatesworn, archive, FATESWORN_HASH, FATESWORN_BYTES);
            }
            Files.createFile(new File(stage, ".ready").toPath());
            android.system.Os.rename(stage.getPath(), destination.getPath());
            GameStorage.clearSource(context);
        } catch (ErrnoException error) {
            throw new IOException("Android could not finish the import.", error);
        } finally {
            removePartial(stage);
        }
    }

    static synchronized void updateInstalledVeil(Context context, File storage, Progress progress)
            throws IOException {
        if (GameStorage.directConfigured(context)) {
            if (!directReady(context, storage))
                throw new IOException("The game folder is unavailable. Check the storage volume and All files access.");
            if (hasVeilRecipe(context)) new VeilPatch(context.getAssets(), PATCH_BYTES,
                    PATCH_DIRECTORY, PATCH_HASH).update(new File(storage, DIRECT_PATCH),
                    new File(GameStorage.sourceRoot(context), "data/initial_0.pak"), progress);
            return;
        }
        File installed = installedGame(context, storage);
        if (installed == null) throw new IOException("Open Amalur could not find the installed game.");
        if (!hasVeilRecipe(context)) return;
        File data = new File(installed, "game/data");
        new VeilPatch(context.getAssets(), PATCH_BYTES, PATCH_DIRECTORY, PATCH_HASH)
                .update(new File(data, "patch_0.pak"), new File(data, "initial_0.pak"), progress);
    }

    private static boolean hasVeilRecipe(Context context) throws IOException {
        String[] entries = context.getAssets().list("local-build");
        if (entries == null) return false;
        boolean edits = Arrays.asList(entries).contains("veil-edits.bin");
        boolean metadata = Arrays.asList(entries).contains("veil-edits.properties");
        if (edits != metadata) throw new IOException("This APK contains an incomplete Veil recipe.");
        return edits;
    }

    private InputStream open(Uri uri) throws IOException {
        InputStream input = resolver.openInputStream(uri);
        if (input == null) throw new IOException("Android could not open the selected file.");
        return input;
    }

    private void copy(Entry entry, File target, String expectedHash, long expectedSize) throws IOException {
        if (expectedSize > target.getParentFile().getUsableSpace()) {
            throw new IOException("There is not enough free space to copy " + entry.name + ".");
        }
        long copied = 0;
        long nextProgress = 0;
        MessageDigest hash = expectedHash == null ? null : sha256();
        try (InputStream input = open(entry.uri); FileOutputStream output = new FileOutputStream(target)) {
            int count;
            while ((count = input.read(buffer)) != -1) {
                output.write(buffer, 0, count);
                if (hash != null) hash.update(buffer, 0, count);
                copied += count;
                if (expectedSize >= 0 && copied > expectedSize) {
                    throw new IOException(entry.name + " exceeds the supported file size.");
                }
                if (copied >= nextProgress) {
                    progress.report(entry.name, copied, expectedSize);
                    nextProgress = copied + 16L * buffer.length;
                }
            }
        }
        if (copied == 0 || (expectedSize >= 0 && copied != expectedSize)) {
            throw new IOException(entry.name + " is incomplete (" + copied + " bytes read).");
        }
        if (hash != null && !expectedHash.equals(hex(hash.digest()))) {
            throw new IOException(entry.name + " does not match the supported Re-Reckoning version.");
        }
    }

    private void copyStream(InputStream input, File target, String name,
                            String expectedHash, long expectedSize) throws IOException {
        long copied = 0;
        MessageDigest hash = sha256();
        try (FileOutputStream output = new FileOutputStream(target)) {
            int count;
            while ((count = input.read(buffer)) != -1) {
                output.write(buffer, 0, count);
                hash.update(buffer, 0, count);
                copied += count;
                if (copied > expectedSize) throw new IOException(name + " is too large.");
                if (copied % (16L * buffer.length) < count)
                    progress.report(name, copied, expectedSize);
            }
        }
        if (copied != expectedSize || !expectedHash.equals(hex(hash.digest())))
            throw new IOException(name + " does not match the supported Re-Reckoning version.");
    }

    private void prepareCompiler(Uri uri, File target) throws IOException {
        byte[] bytes = new byte[CompilerDownload.DLL_BYTES];
        try (InputStream input = new BufferedInputStream(open(uri))) {
            int copied = 0;
            while (copied < bytes.length) {
                int count = input.read(bytes, copied, bytes.length - copied);
                if (count < 0) throw new IOException("Select the supported 32-bit Microsoft d3dcompiler_43.dll.");
                copied += count;
            }
            if (input.read() != -1) throw new IOException("Select the supported 32-bit Microsoft d3dcompiler_43.dll.");
        }
        prepareCompiler(bytes, target);
    }

    private static void prepareCompiler(byte[] bytes, File target) throws IOException {
        String hash = hex(sha256().digest(bytes));
        if (COMPILER_HASH.equals(hash)) {
            rebaseCompiler(bytes);
            hash = hex(sha256().digest(bytes));
        }
        if (!REBASED_HASH.equals(hash)) {
            throw new IOException("Use the 32-bit Microsoft d3dcompiler_43.dll from Windows SysWOW64.");
        }
        Files.write(target.toPath(), bytes);
    }

    private static void rebaseCompiler(byte[] bytes) {
        // The caller checks the entire original file before reading its PE32 relocation table.
        ByteBuffer pe = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN);
        int header = pe.getInt(0x3c);
        int optional = header + 24;
        int sections = optional + (pe.getShort(header + 20) & 0xffff);
        int sectionCount = pe.getShort(header + 6) & 0xffff;
        int delta = 0x20000000 - pe.getInt(optional + 28);
        int position = fileOffset(pe, sections, sectionCount, pe.getInt(optional + 136));
        int end = position + pe.getInt(optional + 140);
        while (position < end) {
            int page = pe.getInt(position);
            int size = pe.getInt(position + 4);
            if (size < 8) break;
            for (int entry = position + 8; entry < position + size; entry += 2) {
                int relocation = pe.getShort(entry) & 0xffff;
                if ((relocation >>> 12) != 3) continue;
                int offset = fileOffset(pe, sections, sectionCount, page + (relocation & 0xfff));
                if (offset >= 0) pe.putInt(offset, pe.getInt(offset) + delta);
            }
            position += size;
        }
        pe.putInt(optional + 28, 0x20000000);
    }

    private static int fileOffset(ByteBuffer pe, int section, int count, int rva) {
        for (int i = 0; i < count; i++, section += 40) {
            int relative = rva - pe.getInt(section + 12);
            if (relative >= 0 && relative <= pe.getInt(section + 16) - 4) {
                return pe.getInt(section + 20) + relative;
            }
        }
        return -1;
    }

    static MessageDigest sha256() {
        try {
            return MessageDigest.getInstance("SHA-256");
        } catch (NoSuchAlgorithmException error) {
            throw new AssertionError(error);
        }
    }

    static String hex(byte[] bytes) {
        StringBuilder result = new StringBuilder(bytes.length * 2);
        for (byte b : bytes) result.append(Character.forDigit((b & 0xff) >>> 4, 16))
                .append(Character.forDigit(b & 0xf, 16));
        return result.toString();
    }

    private static void makeDirectory(File path) throws IOException {
        if (!path.mkdirs() && !path.isDirectory()) throw new IOException("Could not create " + path.getName());
    }

    private static void removePartial(File path) {
        File[] children = path.listFiles();
        if (children != null) for (File child : children) removePartial(child);
        if (path.exists()) path.delete();
    }
}
