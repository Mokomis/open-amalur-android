package org.openamalur.originalengine;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.IOException;
import java.util.Locale;

public final class SetupActivity extends Activity {
    private static final int GAME_FOLDER = 1, COMPILER_FILE = 2, ENGINE_FILE = 3,
            ALL_FILES_ACCESS = 4;
    private Uri gameFolder;
    private TextView status;
    private Button browse;
    private Button internalChoice;
    private Button sdChoice;
    private boolean importing;
    private File selectedStorage;
    private File internalStorage;
    private File sdStorage;

    @Override protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        // Tapping the launcher icon while the game runs in the background starts a new
        // SetupActivity on top of the existing task, because this root activity already
        // finished. Return to the running game instead of starting a second VeilActivity.
        Intent launch = getIntent();
        if (!isTaskRoot() && launch != null && Intent.ACTION_MAIN.equals(launch.getAction())
                && launch.hasCategory(Intent.CATEGORY_LAUNCHER)) {
            finish();
            return;
        }
        File storage = GameStorage.root(this);
        File[] storageChoices = GameStorage.available(this);
        boolean missingStorage = GameStorage.selectedVolumeMissing(this);
        boolean installed = GameFiles.ready(this, storage);
        boolean repairTitle = !installed && GameFiles.needsTitleRepair(this, storage);
        if (saved != null) gameFolder = saved.getParcelable("gameFolder");
        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setGravity(Gravity.CENTER);
        panel.setBackgroundColor(0xff101516);
        panel.setPadding(48, 32, 48, 32);
        TextView heading = new TextView(this);
        heading.setTextSize(28);
        heading.setTextColor(0xffece5d3);
        heading.setText(installed ? "Open Amalur" : "Set up Open Amalur");
        panel.addView(heading);
        status = new TextView(this);
        status.setGravity(Gravity.CENTER);
        status.setTextSize(16);
        status.setTextColor(Color.LTGRAY);
        status.setPadding(0, 24, 0, 24);
        status.setText(missingStorage ? "The selected SD card is unavailable. Reinsert it and retry."
                : GameStorage.directConfigured(this) && GameStorage.sourceRoot(this) == null
                ? "The game folder or All files access is unavailable. Check the storage volume and grant access to continue."
                : installed ? "Choose Play to start. Crash reports are available from the main menu."
                : repairTitle
                ? "The title animation is missing. Choose your Re-Reckoning folder to restore it."
                : "Where are your Re-Reckoning game files? Choose the storage below, then select "
                        + "the folder containing koa.exe and data. Open Amalur uses those files in place "
                        + "without making another full game copy.");
        panel.addView(status);
        if (!installed) {
            selectedStorage = GameStorage.recommended(this, storageChoices);
            String savedChoice = saved == null ? null : saved.getString("selectedStorage");
            for (File choice : storageChoices) {
                if (choice.getAbsolutePath().equals(savedChoice)) selectedStorage = choice;
            }
            for (File choice : storageChoices) {
                if (GameStorage.removable(choice)) sdStorage = choice;
                else if (internalStorage == null) internalStorage = choice;
            }
            internalChoice = new Button(this);
            internalChoice.setText("Set up and play from internal storage");
            VeilDialogs.styleButton(internalChoice);
            internalChoice.setEnabled(internalStorage != null);
            internalChoice.setOnClickListener(view -> chooseStorage(internalStorage));
            panel.addView(internalChoice);
            TextView internalNote = new TextView(this);
            internalNote.setTextColor(Color.LTGRAY);
            internalNote.setGravity(Gravity.CENTER);
            internalNote.setText("Uses the existing folder on this device. About 1.5 GB free is needed for Veil and saves.");
            panel.addView(internalNote);
            sdChoice = new Button(this);
            sdChoice.setText(sdStorage == null ? "Set up and play from SD card (no card detected)"
                    : "Set up and play from SD card");
            VeilDialogs.styleButton(sdChoice);
            sdChoice.setEnabled(sdStorage != null);
            sdChoice.setOnClickListener(view -> chooseStorage(sdStorage));
            panel.addView(sdChoice);
            TextView sdNote = new TextView(this);
            sdNote.setTextColor(Color.LTGRAY);
            sdNote.setGravity(Gravity.CENTER);
            sdNote.setText(sdStorage == null ? "Insert an SD card with your game folder to use this option."
                    : String.format(Locale.ROOT, "Uses the existing folder on the card. %.1f GB free on card.",
                            sdStorage.getUsableSpace() / 1_000_000_000.0));
            panel.addView(sdNote);
        }
        browse = new Button(this);
        browse.setText(installed ? "Play Open Amalur"
                : missingStorage ? "Retry" : "Choose game folder");
        VeilDialogs.styleButton(browse);
        if (missingStorage) {
            browse.setOnClickListener(view -> recreate());
        } else if (installed) {
            browse.setOnClickListener(view -> launchExistingGame());
            browse.setVisibility(View.VISIBLE);
        } else {
            browse.setOnClickListener(view -> selectFolder());
            browse.setVisibility(View.GONE);
        }
        panel.addView(browse);
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.addView(panel);
        if (installed) {
            // Leave the centered launch artwork visible during preparation. Keep
            // update progress, errors and Retry accessible below it.
            FrameLayout startup = new FrameLayout(this);
            panel.removeView(status);
            panel.removeView(browse);
            LinearLayout actions = new LinearLayout(this);
            actions.setOrientation(LinearLayout.VERTICAL);
            actions.setGravity(Gravity.CENTER);
            actions.addView(status);
            actions.addView(browse);
            startup.addView(actions, new FrameLayout.LayoutParams(-1, -2, Gravity.BOTTOM));
            setContentView(startup);
        } else {
            setContentView(scroll);
        }
        // Explain an unsupported driver or the previous run's fatal exit before
        // starting the engine again. A restored activity skips straight to launch.
        Runnable proceed = installed ? (saved == null ? this::explainLastExit : this::launchExistingGame)
                : () -> {};
        if (saved == null && VulkanSupport.shouldWarn(this)) VulkanSupport.warn(this, proceed);
        else proceed.run();
    }

    private void explainLastExit() {
        int exit = LastExit.check(this);
        if (exit == LastExit.NONE) {
            launchExistingGame();
            return;
        }
        String message = exit == LastExit.GPU_LOST
                ? "Open Amalur closed last time because your device's graphics driver stopped "
                        + "responding (Vulkan device lost). This is usually a driver problem on the device."
                : "Open Amalur closed last time after an internal engine error.";
        status.setText(message);
        VeilDialogs.show(new AlertDialog.Builder(this)
                .setTitle("Open Amalur closed unexpectedly")
                .setMessage(message + "\n\nSending a crash report helps find the cause. It contains "
                        + "recent logs and device details, not your saves or game files.")
                .setCancelable(false)
                .setNeutralButton("Send Crash Report", (dialog, button) ->
                        startActivity(new Intent(this, CrashReportActivity.class)
                                .putExtra(CrashReportActivity.CREATE_ON_OPEN, true)))
                .setPositiveButton("Play", (dialog, button) -> launchExistingGame()));
    }

    private void chooseStorage(File choice) {
        if (choice == null || importing) return;
        selectedStorage = choice;
        selectFolder();
    }

    @Override protected void onSaveInstanceState(Bundle out) {
        super.onSaveInstanceState(out);
        out.putParcelable("gameFolder", gameFolder);
        if (selectedStorage != null) out.putString("selectedStorage", selectedStorage.getAbsolutePath());
    }

    private void selectFolder() {
        try {
            GameStorage.select(this, selectedStorage == null ? GameStorage.requireRoot(this)
                    : selectedStorage);
        } catch (IOException error) {
            status.setText(error.getMessage());
            return;
        }
        Intent picker = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        picker.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        startActivityForResult(picker, GAME_FOLDER);
    }

    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request == ALL_FILES_ACCESS) {
            if (gameFolder != null && android.os.Build.VERSION.SDK_INT >= 30
                    && Environment.isExternalStorageManager())
                importFiles(gameFolder, null);
            else status.setText("Allow All files access to play from the selected game folder, then retry.");
            return;
        }
        if (result != RESULT_OK || data == null || data.getData() == null || importing) return;
        if (request == GAME_FOLDER) {
            gameFolder = data.getData();
            if (android.os.Build.VERSION.SDK_INT < 30) {
                status.setText("Direct play requires Android 11 or newer. Existing imports remain playable on this device.");
                return;
            }
            if (!Environment.isExternalStorageManager()) {
                status.setText("Allow All files access so Open Amalur can read the selected game folder without copying it.");
                Intent access = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                        Uri.parse("package:" + getPackageName()));
                startActivityForResult(access, ALL_FILES_ACCESS);
                return;
            }
            importFiles(gameFolder, null);
        } else if (request == COMPILER_FILE && gameFolder != null) {
            importFiles(gameFolder, data.getData());
        } else if (request == ENGINE_FILE) {
            importEngine(data.getData());
        }
    }

    private void selectCompiler() {
        selectFile(COMPILER_FILE);
    }

    private void selectFile(int request) {
        Intent picker = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        picker.addCategory(Intent.CATEGORY_OPENABLE);
        picker.setType("*/*");
        picker.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        startActivityForResult(picker, request);
    }

    private void importFiles(Uri folder, Uri compiler) {
        importing = true;
        browse.setEnabled(false);
        if (internalChoice != null) internalChoice.setEnabled(false);
        if (sdChoice != null) sdChoice.setEnabled(false);
        status.setText("Checking game files…");
        new Thread(() -> {
            try {
                new GameFiles(getApplicationContext(), folder, (name, copied, total) ->
                        runOnUiThread(() -> status.setText(total > 0
                                ? String.format(Locale.ROOT, "Importing %s (%d%%)", name, copied * 100 / total)
                                : "Importing " + name + "…"))).install(compiler);
                runOnUiThread(this::launchExistingGame);
            } catch (GameFiles.CompilerRequired missing) {
                runOnUiThread(() -> {
                    status.setText(missing.getMessage());
                    browse.setText("Choose DirectX library");
                    browse.setOnClickListener(view -> selectCompiler());
                    browse.setEnabled(true);
                    browse.setVisibility(View.VISIBLE);
                    importing = false;
                    if (internalChoice != null) internalChoice.setEnabled(internalStorage != null);
                    if (sdChoice != null) sdChoice.setEnabled(sdStorage != null);
                });
            } catch (IOException | IllegalArgumentException | SecurityException error) {
                runOnUiThread(() -> {
                    status.setText(error.getMessage());
                    browse.setText("Choose game folder");
                    browse.setOnClickListener(view -> selectFolder());
                    browse.setEnabled(true);
                    importing = false;
                    browse.setVisibility(View.GONE);
                    if (internalChoice != null) internalChoice.setEnabled(internalStorage != null);
                    if (sdChoice != null) sdChoice.setEnabled(sdStorage != null);
                });
            }
        }, "game-file-import").start();
    }

    private void showEngineRequest(String message) {
        importing = false;
        status.setText(message);
        browse.setText("Choose locally built engine");
        browse.setOnClickListener(view -> selectFile(ENGINE_FILE));
        browse.setEnabled(true);
        browse.setVisibility(View.VISIBLE);
    }

    private void importEngine(Uri uri) {
        importing = true;
        browse.setEnabled(false);
        status.setText("Verifying the local engine…");
        new Thread(() -> {
            try {
                LocalEngine.importFrom(getApplicationContext(), uri);
                runOnUiThread(this::launchExistingGame);
            } catch (IOException | SecurityException error) {
                runOnUiThread(() -> showEngineRequest(error.getMessage()));
            }
        }, "engine-import").start();
    }

    private void launchExistingGame() {
        importing = true;
        browse.setVisibility(View.GONE);
        status.setText("");
        new Thread(() -> {
            try {
                prepareGame();
                runOnUiThread(this::startEngine);
            } catch (LocalEngine.EngineRequired missing) {
                runOnUiThread(() -> showEngineRequest(missing.getMessage()));
            } catch (IOException | SecurityException error) {
                android.util.Log.e("VeilSetup", "Could not prepare the installed game", error);
                runOnUiThread(() -> {
                    importing = false;
                    status.setText(error.getMessage());
                    browse.setText("Retry");
                    browse.setOnClickListener(view -> launchExistingGame());
                    browse.setEnabled(true);
                    browse.setVisibility(View.VISIBLE);
                });
            }
        }, "veil-update").start();
    }

    private void prepareGame() throws IOException {
        LocalEngine.prepare(getApplicationContext(), (message, copied, total) ->
                runOnUiThread(() -> status.setText(total > 0
                        ? String.format(Locale.ROOT, "%s (%d%%)", message, copied * 100 / total)
                        : message + "…")));
        File storage = GameStorage.requireRoot(this);
        GameFiles.updateInstalledVeil(getApplicationContext(), storage,
                (message, copied, total) -> runOnUiThread(() -> status.setText(total > 0
                        ? String.format(Locale.ROOT, "%s (%d%%)", message, copied * 100 / total) : message + "…")));
        GameFiles.prepareLocalProfile(storage);
        // Runs before the engine starts its new run folder, so the run that just
        // ended stays available to LastExit and crash reports.
        RunLogs.prune(storage);
    }

    private void startEngine() {
        if (isFinishing() || isDestroyed()) return;
        Intent game = new Intent(this, VeilActivity.class);
        game.putExtra("oa_no_overlays", getIntent().getBooleanExtra("oa_no_overlays", false));
        startActivity(game);
        overridePendingTransition(0, 0);
        finish();
    }
}
