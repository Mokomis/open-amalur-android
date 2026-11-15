package org.openamalur.originalengine;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.IOException;
import java.util.Locale;

public final class SetupActivity extends Activity {
    private static final int GAME_FOLDER = 1, COMPILER_FILE = 2, ENGINE_FILE = 3;
    private Uri gameFolder;
    private TextView status;
    private Button browse;
    private boolean importing;

    @Override protected void onCreate(Bundle saved) {
        super.onCreate(saved);
        boolean installed = GameFiles.ready(this, getExternalFilesDir(null));
        boolean repairTitle = !installed && GameFiles.needsTitleRepair(this, getExternalFilesDir(null));
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
        status.setText(installed ? "Opening Open Amalur…" : repairTitle
                ? "The title animation is missing. Choose your Re-Reckoning folder to restore it."
                : "Choose the Kingdoms of Amalur: Re-Reckoning folder containing koa.exe and data.\n"
                        + "For a private DroidDeck library, copy the game folder to shared storage first.");
        panel.addView(status);
        browse = new Button(this);
        browse.setText(installed ? "Retry" : "Choose game folder");
        if (installed) {
            browse.setOnClickListener(view -> launchExistingGame());
            browse.setVisibility(View.GONE);
        } else {
            browse.setOnClickListener(view -> selectFolder());
        }
        panel.addView(browse);
        setContentView(panel);
        if (installed) launchExistingGame();
    }

    @Override protected void onSaveInstanceState(Bundle out) {
        super.onSaveInstanceState(out);
        out.putParcelable("gameFolder", gameFolder);
    }

    private void selectFolder() {
        Intent picker = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        picker.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        startActivityForResult(picker, GAME_FOLDER);
    }

    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (result != RESULT_OK || data == null || data.getData() == null || importing) return;
        if (request == GAME_FOLDER) {
            gameFolder = data.getData();
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
                    importing = false;
                });
            } catch (IOException | IllegalArgumentException | SecurityException error) {
                runOnUiThread(() -> {
                    status.setText(error.getMessage());
                    browse.setText("Choose game folder");
                    browse.setOnClickListener(view -> selectFolder());
                    browse.setEnabled(true);
                    importing = false;
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
        status.setText("Opening Open Amalur…");
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
        GameFiles.updateInstalledVeil(getApplicationContext(), getExternalFilesDir(null),
                (message, copied, total) -> runOnUiThread(() -> status.setText(total > 0
                        ? String.format(Locale.ROOT, "%s (%d%%)", message, copied * 100 / total) : message + "…")));
        GameFiles.prepareLocalProfile(getExternalFilesDir(null));
    }

    private void startEngine() {
        if (isFinishing() || isDestroyed()) return;
        startActivity(new Intent(this, VeilActivity.class));
        finish();
    }
}
