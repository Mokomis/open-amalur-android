package org.openamalur.originalengine;

import android.content.Context;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Environment;
import android.provider.DocumentsContract;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/** One persistent root for imported archives, saves, and native game state. */
final class GameStorage {
    private static final String PREFS = "game-storage";
    private static final String ROOT = "root";
    private static final String SOURCE = "source";
    // Existing imported installs can still occupy a complete copy.
    private static final long RECOMMENDED_FREE = 1_500_000_000L;

    private GameStorage() {}

    static File[] available(Context context) {
        List<File> writable = new ArrayList<>();
        File[] directories = context.getExternalFilesDirs(null);
        if (directories == null) return new File[0];
        for (File directory : directories) {
            if (directory != null && Environment.MEDIA_MOUNTED.equals(
                    Environment.getExternalStorageState(directory))
                    && (directory.isDirectory() || directory.mkdirs())
                    && directory.canWrite()) {
                writable.add(directory);
            }
        }
        return writable.toArray(new File[0]);
    }

    static File root(Context context) {
        String saved = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(ROOT, null);
        File[] choices = available(context);
        if (saved == null) return choices.length == 0 ? null : choices[0];
        for (File choice : choices) {
            if (saved.equals(choice.getAbsolutePath())) return choice;
        }
        return null; // Never silently move an installed game when its card is removed.
    }

    static boolean selectedVolumeMissing(Context context) {
        SharedPreferences prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        return prefs.contains(ROOT) && root(context) == null;
    }

    static File requireRoot(Context context) throws IOException {
        File root = root(context);
        if (root == null) throw new IOException(selectedVolumeMissing(context)
                ? "The selected SD card is unavailable. Reinsert it and retry."
                : "Android game storage is unavailable.");
        return root;
    }

    static File recommended(Context context, File[] choices) {
        if (choices.length == 0) return null;
        File selected = root(context);
        if (context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).contains(ROOT)) return selected;
        if (choices[0].getUsableSpace() < RECOMMENDED_FREE) {
            for (int i = 1; i < choices.length; ++i) {
                if (choices[i].getUsableSpace() >= RECOMMENDED_FREE) return choices[i];
            }
        }
        return choices[0];
    }

    static void select(Context context, File directory) throws IOException {
        for (File choice : available(context)) {
            if (choice.getAbsolutePath().equals(directory.getAbsolutePath())) {
                if (!context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
                        .putString(ROOT, choice.getAbsolutePath()).commit()) {
                    throw new IOException("Could not save the install location. Retry setup.");
                }
                return;
            }
        }
        throw new IOException("The selected storage location is unavailable or read-only.");
    }

    static String label(File directory) {
        String name = Environment.isExternalStorageRemovable(directory)
                ? "SD card / removable storage" : "Device storage";
        return String.format(Locale.ROOT, "%s (%.1f GB free)", name,
                directory.getUsableSpace() / 1_000_000_000.0);
    }

    static boolean removable(File directory) {
        return directory != null && Environment.isExternalStorageRemovable(directory);
    }

    private static File volumeRoot(File appDirectory) throws IOException {
        String path = appDirectory.getCanonicalPath();
        int suffix = path.indexOf("/Android/data/");
        if (suffix < 1) throw new IOException("Android did not provide an SD card app folder.");
        return new File(path.substring(0, suffix));
    }

    static File resolveSource(Context context, Uri tree) throws IOException {
        File storage = requireRoot(context);
        if (!"com.android.externalstorage.documents".equals(tree.getAuthority())) {
            throw new IOException("Choose a local folder on the same SD card, not a cloud folder.");
        }
        String id = DocumentsContract.getTreeDocumentId(tree);
        int colon = id.indexOf(':');
        if (colon <= 0 || colon == id.length() - 1)
            throw new IOException("Choose the game folder, not the storage root.");
        File volume = volumeRoot(storage).getCanonicalFile();
        String expectedId = removable(storage) ? volume.getName() : "primary";
        if (!id.substring(0, colon).equalsIgnoreCase(expectedId)) {
            throw new IOException("Choose the game folder on the selected storage volume.");
        }
        File source = new File(volume, id.substring(colon + 1)).getCanonicalFile();
        if (!source.toPath().startsWith(volume.toPath()) || !source.isDirectory()) {
            throw new IOException("The selected game folder is unavailable.");
        }
        return source;
    }

    static File sourceRoot(Context context) {
        File storage = root(context);
        if (storage == null) return null;
        String saved = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
                .getString(SOURCE, null);
        if (saved == null) return null;
        try {
            File volume = volumeRoot(storage).getCanonicalFile();
            File source = new File(saved).getCanonicalFile();
            return source.toPath().startsWith(volume.toPath()) && source.isDirectory()
                    && android.os.Build.VERSION.SDK_INT >= 30
                    && Environment.isExternalStorageManager(source) ? source : null;
        } catch (IOException | IllegalArgumentException unavailable) {
            return null;
        }
    }

    static boolean directConfigured(Context context) {
        return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).contains(SOURCE);
    }

    static void clearSource(Context context) {
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().remove(SOURCE).commit();
    }

    static void saveSource(Context context, File source) throws IOException {
        File storage = requireRoot(context);
        File volume = volumeRoot(storage).getCanonicalFile();
        source = source.getCanonicalFile();
        if (!source.toPath().startsWith(volume.toPath()) || !source.isDirectory()
                || android.os.Build.VERSION.SDK_INT < 30
                || !Environment.isExternalStorageManager(source)) {
            throw new IOException("Grant All files access, then select the game folder on the chosen storage volume.");
        }
        if (!context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
                .putString(SOURCE, source.getAbsolutePath()).commit()) {
            throw new IOException("Could not save the SD game folder. Retry setup.");
        }
    }
}
