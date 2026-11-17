package org.openamalur.originalengine;

import android.app.Activity;
import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.ClipData;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/** Launcher entry that works even if the game crashes before its title screen. */
public final class CrashReportActivity extends Activity {
    private static final long LOG_LIMIT = 4L * 1024 * 1024;
    private static final long TRACE_LIMIT = 2L * 1024 * 1024;
    private TextView status;
    private Button share;

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout panel = new LinearLayout(this);
        panel.setOrientation(LinearLayout.VERTICAL);
        panel.setGravity(Gravity.CENTER);
        panel.setPadding(48, 40, 48, 40);
        panel.setBackgroundColor(0xff101516);
        TextView title = new TextView(this);
        title.setText("Send Open Amalur crash logs");
        title.setTextColor(0xffece5d3);
        title.setTextSize(26);
        panel.addView(title);
        status = new TextView(this);
        status.setText("After a crash, tap below to share the latest run logs and Android's "
                + "recorded exit details. The report does not contain game archives or saves. "
                + "Logs can include local file paths; review who you send them to.");
        status.setTextColor(Color.LTGRAY);
        status.setTextSize(16);
        status.setPadding(0, 24, 0, 24);
        panel.addView(status);
        share = new Button(this);
        share.setText("Create and share crash report");
        share.setOnClickListener(view -> createReport());
        panel.addView(share);
        setContentView(panel);
    }

    private void createReport() {
        share.setEnabled(false);
        status.setText("Collecting the latest logs…");
        new Thread(() -> {
            try {
                File report = CrashReportProvider.reportFile(this);
                File parent = report.getParentFile();
                if (!parent.isDirectory() && !parent.mkdirs())
                    throw new IOException("Cannot create the report folder on this storage volume.");
                File temporary = new File(parent, CrashReportProvider.NAME + ".partial");
                try {
                    writeReport(temporary);
                    android.system.Os.rename(temporary.getPath(), report.getPath());
                } finally {
                    Files.deleteIfExists(temporary.toPath());
                }
                runOnUiThread(this::shareReport);
            } catch (Exception error) {
                runOnUiThread(() -> {
                    status.setText("Could not create a report: " + error.getMessage());
                    share.setEnabled(true);
                });
            }
        }, "amalur-crash-report").start();
    }

    private void writeReport(File output) throws IOException {
        StringBuilder summary = new StringBuilder();
        summary.append("Open Amalur diagnostic report\n");
        summary.append("Device: ").append(Build.MANUFACTURER).append(' ')
                .append(Build.MODEL).append("\nAndroid: ").append(Build.VERSION.RELEASE)
                .append(" (API ").append(Build.VERSION.SDK_INT).append(")\n");
        try {
            PackageInfo info = getPackageManager().getPackageInfo(getPackageName(), 0);
            summary.append("App version: ").append(info.versionName).append("\n");
        } catch (Exception unavailable) {
            summary.append("App version: unavailable\n");
        }
        File root = GameStorage.root(this);
        summary.append("Selected storage: ").append(root == null ? "unavailable"
                : GameStorage.removable(root) ? "removable" : "internal").append('\n');
        summary.append("Direct game folder configured: ").append(GameStorage.directConfigured(this))
                .append("\nDirect game folder accessible: ")
                .append(GameStorage.sourceRoot(this) != null).append('\n');

        try (ZipOutputStream zip = new ZipOutputStream(Files.newOutputStream(output.toPath()))) {
            if (root != null) {
                File marker = new File(root, "latest-run.txt");
                if (marker.isFile()) {
                    File run = new File(new String(Files.readAllBytes(marker.toPath()),
                            StandardCharsets.UTF_8).trim()).getCanonicalFile();
                    File canonicalRoot = root.getCanonicalFile();
                    if (run.toPath().startsWith(canonicalRoot.toPath())
                            && run.getName().startsWith("run-") && run.isDirectory()) {
                        summary.append("Latest run: ").append(run.getName()).append('\n');
                        addLog(zip, new File(run, "pc-runtime.log"));
                        addLog(zip, new File(run, "oa-vulkan.log"));
                    } else summary.append("Latest run marker is unavailable or invalid.\n");
                } else summary.append("No previous run marker found.\n");
            }
            addExitInfo(zip, summary);
            zip.putNextEntry(new ZipEntry("report.txt"));
            zip.write(summary.toString().getBytes(StandardCharsets.UTF_8));
            zip.closeEntry();
        }
    }

    private static void addLog(ZipOutputStream zip, File file) throws IOException {
        if (!file.isFile()) return;
        zip.putNextEntry(new ZipEntry(file.getName()));
        try (RandomAccessFile input = new RandomAccessFile(file, "r")) {
            long skip = Math.max(0, input.length() - LOG_LIMIT);
            input.seek(skip);
            byte[] bytes = new byte[64 * 1024];
            int count;
            while ((count = input.read(bytes)) != -1) zip.write(bytes, 0, count);
        }
        zip.closeEntry();
    }

    private void addExitInfo(ZipOutputStream zip, StringBuilder summary) throws IOException {
        if (Build.VERSION.SDK_INT < 30) {
            summary.append("Android exit history requires Android 11 or newer.\n");
            return;
        }
        ActivityManager manager = (ActivityManager) getSystemService(Context.ACTIVITY_SERVICE);
        List<ApplicationExitInfo> exits = manager.getHistoricalProcessExitReasons(
                getPackageName(), 0, 8);
        summary.append("Recorded process exits: ").append(exits.size()).append('\n');
        int traces = 0;
        for (ApplicationExitInfo exit : exits) {
            summary.append("Exit: time=").append(exit.getTimestamp())
                    .append(" reason=").append(exit.getReason())
                    .append(" status=").append(exit.getStatus())
                    .append(" process=").append(exit.getProcessName())
                    .append(" detail=").append(exit.getDescription()).append('\n');
            if (traces >= 2) continue;
            try (InputStream trace = exit.getTraceInputStream()) {
                if (trace == null) continue;
                String extension = exit.getReason() == ApplicationExitInfo.REASON_CRASH_NATIVE
                        ? ".pb" : ".txt";
                zip.putNextEntry(new ZipEntry("android-exit-trace-" + (++traces) + extension));
                byte[] bytes = new byte[64 * 1024];
                long remaining = TRACE_LIMIT;
                int count;
                while (remaining > 0 && (count = trace.read(bytes, 0,
                        (int)Math.min(bytes.length, remaining))) != -1) {
                    zip.write(bytes, 0, count);
                    remaining -= count;
                }
                zip.closeEntry();
            } catch (IOException unavailable) {
                summary.append("Android trace unavailable: ").append(unavailable.getMessage()).append('\n');
            }
        }
    }

    private void shareReport() {
        if (isFinishing() || isDestroyed()) return;
        Uri uri = new Uri.Builder().scheme("content")
                .authority(getPackageName() + ".crashreports").path("latest").build();
        try (InputStream check = getContentResolver().openInputStream(uri)) {
            if (check == null || check.read() != 'P' || check.read() != 'K')
                throw new IOException("The report could not be opened for sharing.");
        } catch (IOException error) {
            status.setText(error.getMessage());
            share.setEnabled(true);
            return;
        }
        Intent send = new Intent(Intent.ACTION_SEND);
        send.setType("application/zip");
        send.putExtra(Intent.EXTRA_STREAM, uri);
        send.setClipData(ClipData.newRawUri(CrashReportProvider.NAME, uri));
        send.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        startActivity(Intent.createChooser(send, "Send Open Amalur crash report"));
        status.setText("Report ready. Choose how to send it, or tap again to refresh it.");
        share.setEnabled(true);
    }
}
