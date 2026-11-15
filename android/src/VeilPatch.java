package org.openamalur.originalengine;

import android.content.res.AssetManager;
import android.system.ErrnoException;
import android.system.Os;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.RandomAccessFile;
import java.nio.file.Files;
import java.nio.file.StandardOpenOption;
import java.security.MessageDigest;
import java.util.HashMap;
import java.util.Map;
import java.util.Properties;

final class VeilPatch {
    private static final String ASSET_ROOT = "local-build/";
    private static final long PROGRESS_INTERVAL = 32L * 1024 * 1024;

    private final AssetManager assets;
    private final long retailBytes;
    private final long archiveBytes;
    private final long directory;
    private final long retailDirectory;
    private final String tailHash;
    private final String archiveHash;
    private final Map<String, Long> predecessors = new HashMap<>();
    private final byte[] buffer = new byte[1024 * 1024];

    VeilPatch(AssetManager assets, long retailBytes, long retailDirectory, String retailHash) throws IOException {
        this.assets = assets;
        this.retailBytes = retailBytes;
        this.retailDirectory = retailDirectory;
        Properties manifest = new Properties();
        try (InputStream file = assets.open(ASSET_ROOT + "veil-edits.properties")) {
            manifest.load(file);
        } catch (IllegalArgumentException invalid) {
            throw new IOException("This APK has invalid Veil update information.", invalid);
        }
        archiveBytes = getPositiveSize(manifest.getProperty("archive.bytes"));
        directory = getPositiveSize(manifest.getProperty("archive.directory"));
        if (archiveBytes <= retailBytes || directory < retailBytes || directory > archiveBytes - 8) {
            throw new IOException("This APK has invalid Veil update information.");
        }
        tailHash = requireHash(manifest.getProperty("tail.sha256"));
        archiveHash = requireHash(manifest.getProperty("archive.sha256"));
        predecessors.put(retailHash, retailBytes);
        for (String name : manifest.stringPropertyNames()) {
            if (name.startsWith("previous.")) {
                predecessors.put(requireHash(name.substring("previous.".length())),
                        getPositiveSize(manifest.getProperty(name)));
            }
        }
    }

    // The importer has verified the retail archive and keeps it in staging
    // until all game files have been copied and the Veil edits are verified.
    void applyToRetail(File archive, File initial, GameFiles.Progress progress) throws IOException {
        if (archive.length() != retailBytes) throw unsupportedArchive();
        progress.report("Building Veil menus", 0, -1);
        try (RandomAccessFile file = new RandomAccessFile(archive, "rw");
             InputStream edits = assets.open(ASSET_ROOT + "veil-edits.bin")) {
            VeilPak.apply(file, initial, retailBytes, retailDirectory, edits, GameFiles.sha256());
            progress.report("Verifying Veil", 0, -1);
            if (file.length() != archiveBytes || !archiveHash.equals(hashFrom(file, 0))) {
                throw invalidRecipe();
            }
        }
    }

    void update(File archive, File initial, GameFiles.Progress progress) throws IOException {
        // A process killed during an earlier copy may have left this private
        // temporary file. The game's active archive is replaced only at rename.
        File staged = new File(archive.getParentFile(), ".patch_0.pak.veil-update");
        Files.deleteIfExists(staged.toPath());
        if (isCurrent(archive)) return;

        long previousSize = archive.length();
        long previousModified = archive.lastModified();
        if (!predecessors.containsValue(previousSize)) throw unsupportedArchive();
        if (archive.getParentFile().getUsableSpace() < archiveBytes) {
            throw new IOException("Free about 1.2 GB on this device, then retry the Veil update.");
        }

        try {
            copyVerifiedPrefix(archive, staged, previousSize, progress);
            applyToRetail(staged, initial, progress);
            if (archive.length() != previousSize || archive.lastModified() != previousModified) {
                throw new IOException("The installed game archive changed during the update. Retry Veil.");
            }
            Os.rename(staged.getPath(), archive.getPath());
            android.util.Log.i("VeilSetup", "Veil update applied: " + archiveHash);
        } catch (ErrnoException error) {
            throw new IOException("Android could not replace the game archive. Retry the Veil update.", error);
        } finally {
            Files.deleteIfExists(staged.toPath());
        }
    }

    private boolean isCurrent(File archive) throws IOException {
        if (archive.length() != archiveBytes) return false;
        try (RandomAccessFile file = new RandomAccessFile(archive, "r")) {
            return VeilPak.retailDirectory(file) == directory &&
                    tailHash.equals(hashFrom(file, retailBytes));
        }
    }

    // Hash the complete installed archive while copying only the retail
    // prefix. Existing Veil payloads never accumulate across updates.
    private void copyVerifiedPrefix(File archive, File staged, long previousSize,
                                    GameFiles.Progress progress) throws IOException {
        MessageDigest digest = GameFiles.sha256();
        long read = 0;
        long nextReport = PROGRESS_INTERVAL;
        progress.report("Updating Veil", 0, retailBytes);
        try (InputStream source = Files.newInputStream(archive.toPath());
             OutputStream destination = Files.newOutputStream(staged.toPath(),
                     StandardOpenOption.CREATE_NEW, StandardOpenOption.WRITE)) {
            int count;
            while ((count = source.read(buffer)) != -1) {
                if (count > previousSize - read) throw unsupportedArchive();
                digest.update(buffer, 0, count);
                if (read < retailBytes) {
                    destination.write(buffer, 0, (int) Math.min(count, retailBytes - read));
                }
                read += count;
                if (read >= nextReport) {
                    progress.report("Updating Veil", Math.min(retailBytes, read), retailBytes);
                    nextReport = read + PROGRESS_INTERVAL;
                }
            }
        }
        Long knownSize = predecessors.get(GameFiles.hex(digest.digest()));
        if (read != previousSize || knownSize == null || knownSize != previousSize) throw unsupportedArchive();
    }

    private String hashFrom(RandomAccessFile file, long offset) throws IOException {
        MessageDigest digest = GameFiles.sha256();
        file.seek(offset);
        int count;
        while ((count = file.read(buffer)) != -1) digest.update(buffer, 0, count);
        return GameFiles.hex(digest.digest());
    }

    private static long getPositiveSize(String value) throws IOException {
        try {
            long size = Long.parseLong(value);
            if (size > 0) return size;
        } catch (NumberFormatException invalid) {
            throw new IOException("This APK has invalid Veil update information.", invalid);
        }
        throw new IOException("This APK has invalid Veil update information.");
    }

    private static String requireHash(String value) throws IOException {
        if (value != null && value.matches("[0-9a-f]{64}")) return value;
        throw new IOException("This APK has invalid Veil update information.");
    }

    private static IOException invalidRecipe() {
        return new IOException("The Veil update did not pass verification. Try reinstalling this APK.");
    }

    private static IOException unsupportedArchive() {
        return new IOException("This game archive is from an unsupported version or has been modified. "
                + "Restore a supported game archive and retry.");
    }
}
