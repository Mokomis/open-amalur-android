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
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Locale;

final class LocalEngine {
    private static final String PIN = "local-build/engine.sha256";
    private static final long MAX_ENGINE_BYTES = 256L * 1024 * 1024;
    private static boolean loaded;

    static final class EngineRequired extends IOException {
        EngineRequired(String message) { super(message); }
    }

    private static String expectedHash(Context context) throws IOException {
        try (InputStream input = context.getAssets().open(PIN)) {
            byte[] bytes = new byte[65];
            int offset = 0;
            while (offset < bytes.length) {
                int count = input.read(bytes, offset, bytes.length - offset);
                if (count < 0) break;
                offset += count;
            }
            String hash = new String(bytes, 0, offset, StandardCharsets.US_ASCII).trim();
            if (!hash.matches("[0-9a-f]{64}") || input.read() != -1) {
                throw new IOException("This release contains an invalid engine fingerprint.");
            }
            return hash;
        } catch (FileNotFoundException embeddedEngine) {
            return null;
        }
    }

    private static File localFile(Context context) {
        return new File(context.getFilesDir(), "local-engine/liboaoriginal.so");
    }

    private static String transfer(InputStream source, FileOutputStream destination) throws IOException {
        MessageDigest digest;
        try {
            digest = MessageDigest.getInstance("SHA-256");
        } catch (NoSuchAlgorithmException unavailable) {
            throw new AssertionError(unavailable);
        }
        byte[] buffer = new byte[1024 * 1024];
        long total = 0;
        int count;
        while ((count = source.read(buffer)) != -1) {
            total += count;
            if (total > MAX_ENGINE_BYTES) throw new EngineRequired("Choose the liboaoriginal.so built for this release.");
            digest.update(buffer, 0, count);
            if (destination != null) destination.write(buffer, 0, count);
        }
        StringBuilder hexadecimal = new StringBuilder(64);
        for (byte value : digest.digest()) hexadecimal.append(String.format(Locale.ROOT, "%02x", value & 255));
        return hexadecimal.toString();
    }

    static synchronized void importFrom(Context context, Uri uri) throws IOException {
        String expected = expectedHash(context);
        if (expected == null) throw new IOException("This version already has an engine.");
        File engine = localFile(context);
        Files.createDirectories(engine.getParentFile().toPath());
        File staging = File.createTempFile("engine-", ".partial", engine.getParentFile());
        try {
            try (InputStream input = context.getContentResolver().openInputStream(uri);
                 FileOutputStream output = new FileOutputStream(staging)) {
                if (input == null) throw new IOException("Android could not open the selected engine file.");
                if (!staging.setReadOnly()) throw new IOException("Could not protect the local engine file.");
                if (!expected.equals(transfer(input, output))) {
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
                    if (!expected.equals(transfer(input, null))) {
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
