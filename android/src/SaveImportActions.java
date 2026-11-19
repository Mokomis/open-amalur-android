package org.openamalur.originalengine;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ClipData;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Set;

final class SaveImportActions {
    static final int SELECT_SAVES = 15;
    private static final int MAX_FILES = 256;
    private static final int MAX_FILE_BYTES = 64 * 1024 * 1024;
    private static final String DROIDDECK_AUTHORITY = "com.droiddeck.launcher.documents";
    private final Activity activity;
    private boolean importing;
    private boolean droidDeckSource;

    SaveImportActions(Activity activity) {
        this.activity = activity;
    }

    void show() {
        if (importing) return;
        VeilDialogs.show(new AlertDialog.Builder(activity).setTitle("Import Saves")
                .setItems(new String[]{"GameNative", "DroidDeck"}, (dialog, source) ->
                        showSource(source == 1))
                .setNegativeButton("Cancel", null));
    }

    private void showSource(boolean fromDroidDeck) {
        String details = fromDroidDeck
                ? "Choose DroidDeck in Android's file picker, then open files → linuxfs → root → "
                    + "enable Show hidden files, then .local → share → Steam → userdata → your account → "
                    + "1041720 → remote → autocloud → save. "
                    + "Select the svd_fmt_*.sav files. If DroidDeck is missing from the picker, "
                    + "open DroidDeck once and return here."
                : "Choose the svd_fmt_*.sav files from your GameNative export in Download/GameNative.";
        VeilDialogs.show(new AlertDialog.Builder(activity).setTitle(fromDroidDeck ? "Import from DroidDeck" : "Import from GameNative")
                .setMessage(details + "\n\nMatching Open Amalur saves are backed up before replacement. "
                        + "Restart the game after importing.")
                .setNegativeButton("Back", null)
                .setPositiveButton("Choose save files", (dialog, button) -> openPicker(fromDroidDeck))
                );
    }

    private void openPicker(boolean fromDroidDeck) {
        Intent picker = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        picker.addCategory(Intent.CATEGORY_OPENABLE);
        picker.setType("*/*");
        picker.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
        picker.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        if (fromDroidDeck) {
            String path = "/data/user/0/com.droiddeck.launcher/files/linuxfs/root/.local/share/Steam/userdata";
            picker.putExtra(DocumentsContract.EXTRA_INITIAL_URI,
                    DocumentsContract.buildDocumentUri(DROIDDECK_AUTHORITY, path));
            // ColorOS's own picker does not show third-party document providers.
            for (String packageName : new String[]{"com.android.documentsui", "com.google.android.documentsui"}) {
                Intent systemPicker = new Intent(picker).setPackage(packageName);
                if (systemPicker.resolveActivity(activity.getPackageManager()) != null) {
                    picker = systemPicker;
                    break;
                }
            }
        }
        droidDeckSource = fromDroidDeck;
        activity.startActivityForResult(picker, SELECT_SAVES);
    }

    void importFrom(Intent result) {
        if (importing || result == null) return;
        List<Uri> selected = new ArrayList<>();
        ClipData clip = result.getClipData();
        if (clip != null) {
            for (int i = 0; i < clip.getItemCount(); i++) selected.add(clip.getItemAt(i).getUri());
        } else if (result.getData() != null) {
            selected.add(result.getData());
        }
        if (selected.isEmpty() || selected.size() > MAX_FILES) {
            message("Could not import saves", "Choose between 1 and 256 save files.");
            return;
        }
        if (droidDeckSource) {
            for (Uri uri : selected) {
                if (!DROIDDECK_AUTHORITY.equals(uri.getAuthority())) {
                    message("Choose DroidDeck saves", "Select the Amalur save files from DroidDeck in the file picker.");
                    return;
                }
            }
        }
        importing = true;
        AlertDialog progress = VeilDialogs.show(new AlertDialog.Builder(activity).setTitle("Importing Saves")
                .setMessage("Checking selected files…").setCancelable(false));
        new Thread(() -> {
            int imported = 0, backedUp = 0;
            String error = null;
            try {
                File root = GameStorage.requireRoot(activity.getApplicationContext());
                File destination = new File(root, "offline-saves/working/autocloud/save");
                File backup = new File(root, "offline-saves/import-backups/" + System.currentTimeMillis());
                if (!destination.isDirectory() && !destination.mkdirs())
                    throw new IOException("Could not create the save folder.");
                Set<String> names = new HashSet<>();
                for (int i = 0; i < selected.size(); i++) {
                    Uri uri = selected.get(i);
                    String name = displayName(uri);
                    if (!validName(name) || !names.add(name.toLowerCase(Locale.ROOT)))
                        throw new IOException("Select distinct Amalur svd_fmt_*.sav files only.");
                    int current = i + 1;
                    activity.runOnUiThread(() -> progress.setMessage(
                            "Importing save " + current + " of " + selected.size()));
                    File target = new File(destination, name);
                    File staged = File.createTempFile(".save-import-", ".tmp", destination);
                    try {
                        copy(uri, staged);
                        File previous = null;
                        if (target.exists()) {
                            if (!target.isFile()) throw new IOException("Save target is not a file: " + name);
                            if (!backup.isDirectory() && !backup.mkdirs())
                                throw new IOException("Could not create the save backup folder.");
                            previous = new File(backup, name);
                            if (previous.exists() || !target.renameTo(previous))
                                throw new IOException("Could not back up existing save: " + name);
                        }
                        if (!staged.renameTo(target)) {
                            if (previous != null && !previous.renameTo(target))
                                throw new IOException("Import failed and the original save could not be restored: " + name);
                            throw new IOException("Could not install save: " + name);
                        }
                        imported++;
                        if (previous != null) backedUp++;
                    } finally {
                        if (staged.exists()) staged.delete();
                    }
                }
            } catch (IOException | SecurityException failure) {
                error = failure.getMessage();
                if (error == null) error = "Android could not read the selected save files.";
            }
            final int count = imported, backups = backedUp;
            final String failure = error;
            activity.runOnUiThread(() -> {
                importing = false;
                if (activity.isFinishing() || activity.isDestroyed()) return;
                progress.dismiss();
                if (failure != null) {
                    message("Save import stopped", failure + "\n\n" + count
                            + (count == 1 ? " save" : " saves") + " imported before the error.");
                } else {
                    message("Saves imported", count + (count == 1 ? " save" : " saves")
                            + " imported. " + backups + " matching "
                            + (backups == 1 ? "save" : "saves")
                            + " backed up. Close and reopen Open Amalur before loading them.");
                }
            });
        }, "save-import").start();
    }

    private String displayName(Uri uri) throws IOException {
        try (Cursor cursor = activity.getContentResolver().query(uri,
                new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor == null || !cursor.moveToFirst()) throw new IOException("Could not read a file name.");
            return cursor.getString(0);
        }
    }

    private static boolean validName(String name) {
        return name != null && name.matches("(?i)svd_fmt_[0-9]{1,4}_[0-9]{1,4}\\.sav");
    }

    private void copy(Uri uri, File staged) throws IOException {
        try (InputStream input = activity.getContentResolver().openInputStream(uri);
             FileOutputStream output = new FileOutputStream(staged)) {
            if (input == null) throw new IOException("Could not open a selected save.");
            byte[] buffer = new byte[64 * 1024];
            int total = 0, read;
            while ((read = input.read(buffer)) != -1) {
                total += read;
                if (total > MAX_FILE_BYTES) throw new IOException("A save exceeds the 64 MB limit.");
                output.write(buffer, 0, read);
            }
            if (total == 0) throw new IOException("A selected save is empty.");
            output.getFD().sync();
        }
    }

    private void message(String title, String body) {
        VeilDialogs.show(new AlertDialog.Builder(activity).setTitle(title).setMessage(body)
                .setPositiveButton("OK", null));
    }
}
