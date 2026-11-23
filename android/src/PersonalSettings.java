package org.openamalur.originalengine;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.Arrays;
import java.util.Locale;

final class PersonalSettings {
    private static final String DEFAULT = "\ufeff[DEFAULT]\nDisplayFullscreen=0\n";

    private static byte[] cameraSettings(Path file) throws IOException {
        byte[] bytes = Files.readAllBytes(file);
        boolean wide = bytes.length >= 2 && bytes[0] == (byte)0xff && bytes[1] == (byte)0xfe;
        String text = new String(bytes, wide ? StandardCharsets.UTF_16LE : StandardCharsets.UTF_8);
        StringBuilder result = new StringBuilder(DEFAULT);
        boolean defaults = false;
        for (String line : text.replace("\ufeff", "").split("\n")) {
            String value = line.trim();
            if (value.startsWith("[")) {
                defaults = value.equalsIgnoreCase("[DEFAULT]");
                continue;
            }
            int equals = value.indexOf('=');
            if (!defaults || equals < 1) continue;
            String key = value.substring(0, equals).trim().toLowerCase(Locale.ROOT);
            if (key.startsWith("camera_") || key.startsWith("autofollow_"))
                result.append(value).append('\n');
        }
        return result.toString().getBytes(StandardCharsets.UTF_16LE);
    }

    static synchronized void prepare(File storage) throws IOException {
        Path target = new File(storage, "veil-personal.ini").toPath();
        byte[] seed = DEFAULT.getBytes(StandardCharsets.UTF_16LE);
        byte[] settings = seed;
        if (Files.isRegularFile(target)) {
            // Display Width/Height are runtime values and can change under dynamic resolution.
            // Let the engine choose them afresh, as on a clean launch.
            settings = cameraSettings(target);
            if (Arrays.equals(settings, Files.readAllBytes(target))) return;
        } else {
            for (File run : RunLogs.newestFirst(storage)) {
                File old = new File(run, "overlay/overlay/kaiko/koa/personal.ini");
                if (!old.isFile() || old.length() < 2 || old.length() > 2097152) continue;
                settings = cameraSettings(old.toPath());
                if (!Arrays.equals(settings, seed)) break;
            }
        }
        Path temp = Files.createTempFile(storage.toPath(), "personal-", ".tmp");
        try {
            Files.write(temp, settings);
            Files.move(temp, target, StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
        } finally {
            Files.deleteIfExists(temp);
        }
    }

    private PersonalSettings() {}
}
