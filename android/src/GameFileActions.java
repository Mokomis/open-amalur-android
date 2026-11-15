package org.openamalur.originalengine;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.net.Uri;
import android.os.Process;

import java.io.IOException;
import java.util.Locale;

final class GameFileActions {
    static final int SELECT_FATESWORN = 14;
    static final int SELECT_WATERFALL = 15;
    private final Activity activity;
    private boolean importing;

    GameFileActions(Activity activity) {
        this.activity = activity;
    }

    void show() {
        if (importing) return;
        boolean installed = GameFiles.hasFatesworn(activity.getExternalFilesDir(null));
        new AlertDialog.Builder(activity).setTitle("Game Files")
                .setMessage(installed ? "Fatesworn is installed."
                        : "Choose your updated Re-Reckoning folder to add Fatesworn.")
                .setNeutralButton("Import Waterfall", (dialog, button) -> {
                    Intent picker = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                    picker.addCategory(Intent.CATEGORY_OPENABLE);
                    picker.setType("*/*");
                    picker.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
                    activity.startActivityForResult(picker, SELECT_WATERFALL);
                })
                .setNegativeButton("Back", null)
                .setPositiveButton(installed ? "Replace Fatesworn" : "Add Fatesworn", (dialog, button) -> {
                    Intent picker = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
                    picker.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
                    activity.startActivityForResult(picker, SELECT_FATESWORN);
                }).show();
    }

    void importWaterfall(Uri file) {
        if (importing || file == null) return;
        importing = true;
        AlertDialog progress = new AlertDialog.Builder(activity).setTitle("Importing Waterfall")
                .setMessage("Copying the portrait background…").setCancelable(false).show();
        new Thread(() -> {
            String error = null;
            try {
                GameFiles.importWaterfall(activity.getApplicationContext(), file);
            } catch (IOException | SecurityException failure) {
                error = failure.getMessage();
                if (error == null) error = "Android could not read the selected waterfall file.";
            }
            final String message = error;
            activity.runOnUiThread(() -> {
                importing = false;
                if (activity.isFinishing() || activity.isDestroyed()) return;
                progress.dismiss();
                if (message != null) {
                    new AlertDialog.Builder(activity).setTitle("Could not import waterfall").setMessage(message)
                            .setPositiveButton("OK", null).show();
                }
            });
        }, "waterfall-import").start();
    }

    void importFrom(Uri folder) {
        if (importing || folder == null) return;
        importing = true;
        AlertDialog progress = new AlertDialog.Builder(activity).setTitle("Adding Fatesworn")
                .setMessage("Checking game files…").setCancelable(false).show();
        new Thread(() -> {
            String error = null;
            try {
                new GameFiles(activity.getApplicationContext(), folder, (name, copied, total) ->
                        activity.runOnUiThread(() -> progress.setMessage(String.format(Locale.ROOT,
                                "Copying and verifying Fatesworn (%d%%)", copied * 100 / total))))
                        .addFatesworn();
            } catch (IOException | IllegalArgumentException | SecurityException failure) {
                error = failure.getMessage();
                if (error == null) error = "Android could not read the selected game files.";
            }
            final String message = error;
            activity.runOnUiThread(() -> {
                importing = false;
                if (activity.isFinishing() || activity.isDestroyed()) return;
                progress.dismiss();
                if (message != null) {
                    new AlertDialog.Builder(activity).setTitle("Could not add Fatesworn").setMessage(message)
                            .setPositiveButton("OK", null).show();
                    return;
                }
                restartDialog("Fatesworn added", "Close and reopen Open Amalur to load the expansion.");
            });
        }, "fatesworn-import").start();
    }

    private void restartDialog(String title, String message) {
        new AlertDialog.Builder(activity).setTitle(title).setMessage(message)
                .setNegativeButton("Later", null)
                .setPositiveButton("Close game", (dialog, button) -> {
                    activity.finishAndRemoveTask();
                    Process.killProcess(Process.myPid());
                }).show();
    }
}
