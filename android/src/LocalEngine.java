package org.openamalur.originalengine;

import android.content.Context;
import android.net.Uri;
import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;

final class LocalEngine {
    private static final String PIN = "local-build/engine.sha256";
    private static final String DOWNLOAD = "local-build/engine.url";
    private static final long MAX_ENGINE_BYTES = 256L * 1024 * 1024;
    private static boolean loaded;

    static final class EngineRequired extends IOException {
        EngineRequired(String message) { super(message); }
    }

    private static String readAsset(Context context, String name, int limit) throws IOException {
        try (InputStream input = context.getAssets().open(name)) {
            byte[] bytes = new byte[limit];
            int offset = 0;
            while (offset < bytes.length) {
                int count = input.read(bytes, offset, bytes.length - offset);
                if (count < 0) break;
                offset += count;
            }
            if (input.read() != -1) throw new IOException("Invalid Android engine metadata.");
            return new String(bytes, 0, offset, StandardCharsets.US_ASCII).trim();
        } catch (FileNotFoundException absent) {
            return null;
        }
    }

    private static String expectedHash(Context context) throws IOException {
        String hash = readAsset(context, PIN, 65);
        if (hash != null && !hash.matches("[0-9a-f]{64}")) {
            throw new IOException("This release contains an invalid engine fingerprint.");
        }
        return hash;
    }

    static boolean downloadEnabled(Context context) throws IOException {
        return readAsset(context, DOWNLOAD, 256) != null;
    }

    private static File localFile(Context context) {
        return new File(context.getFilesDir(), "local-engine/liboaoriginal.so");
    }

    private static String transfer(InputStream source, FileOutputStream destination,
                                   long size, GameFiles.Progress progress) throws IOException {
        MessageDigest digest = GameFiles.sha256();
        byte[] buffer = new byte[1024 * 1024];
        long total = 0;
        long nextProgress = 0;
        int count;
        while ((count = source.read(buffer)) != -1) {
            total += count;
            if (total > MAX_ENGINE_BYTES) throw new EngineRequired("The Android engine file is too large.");
            digest.update(buffer, 0, count);
            if (destination != null) destination.write(buffer, 0, count);
            if (progress != null && total >= nextProgress) {
                progress.report("Downloading the Android engine", total, size);
                nextProgress = total + 4L * buffer.length;
            }
        }
        return GameFiles.hex(digest.digest());
    }

    static synchronized void importFrom(Context context, Uri uri) throws IOException {
        String expected = expectedHash(context);
        if (expected == null) throw new IOException("This version already has an engine.");
        try (InputStream input = context.getContentResolver().openInputStream(uri)) {
            if (input == null) throw new IOException("Android could not open the selected engine file.");
            install(context, expected, input, -1, null);
        }
    }

    private static void install(Context context, String expected, InputStream input, long size,
                                GameFiles.Progress progress) throws IOException {
        File engine = localFile(context);
        Files.createDirectories(engine.getParentFile().toPath());
        File staging = File.createTempFile("engine-", ".partial", engine.getParentFile());
        try {
            try (FileOutputStream output = new FileOutputStream(staging)) {
                if (!staging.setReadOnly()) throw new IOException("Could not protect the local engine file.");
                if (!expected.equals(transfer(input, output, size, progress))) {
                    throw new EngineRequired("This engine does not match the installed Open Amalur release.");
                }
                output.getFD().sync();
            }
            Files.move(staging.toPath(), engine.toPath(), StandardCopyOption.ATOMIC_MOVE,
                    StandardCopyOption.REPLACE_EXISTING);
        } finally {
            Files.deleteIfExists(staging.toPath());
        }
    }

    static synchronized void prepare(Context context, GameFiles.Progress progress) throws IOException {
        try {
            load(context);
            return;
        } catch (EngineRequired missing) {
            String url = readAsset(context, DOWNLOAD, 256);
            if (url == null) throw missing;
            progress.report("Downloading the Android engine", 0, 0);
            try {
                HttpURLConnection connection = SetupDownload.open(url, "github.com",
                        "release-assets.githubusercontent.com", "objects.githubusercontent.com");
                try (InputStream input = connection.getInputStream()) {
                    long size = connection.getContentLengthLong();
                    if (size > MAX_ENGINE_BYTES) throw new IOException("The Android engine file is too large.");
                    install(context, expectedHash(context), input, size, progress);
                } finally {
                    connection.disconnect();
                }
            } catch (IOException error) {
                throw new IOException("Could not download the Android engine. " + error.getMessage(), error);
            }
        }
        load(context);
    }

    static synchronized void load(Context context) throws IOException {
        if (loaded) return;
        String expected = expectedHash(context);
        try {
            if (expected == null) {
                System.loadLibrary("oaoriginal");
            } else {
                File engine = localFile(context);
                if (!engine.isFile()) {
                    throw new EngineRequired("Choose the liboaoriginal.so you built from your game files on Windows.");
                }
                try (InputStream input = new FileInputStream(engine)) {
                    if (!expected.equals(transfer(input, null, -1, null))) {
                        throw new EngineRequired("Choose the liboaoriginal.so built for this Open Amalur update.");
                    }
                }
                System.load(engine.getAbsolutePath());
                Log.i("VeilSetup", "Loaded verified local engine " + expected);
            }
            loaded = true;
        } catch (UnsatisfiedLinkError failure) {
            throw new IOException("Android could not load the Open Amalur engine.", failure);
        }
    }
}
