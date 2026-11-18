package org.openamalur.originalengine;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.graphics.PixelFormat;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;

import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

final class UpdateBadge {
    private static final String UP_TO_DATE = "Up To Date";
    private static final String REPO = "Mokomis/open-amalur-android";
    private static final String RELEASES = "https://github.com/" + REPO + "/releases";
    private static final Pattern STABLE_TAG = Pattern.compile("v?([0-9]{1,8})\\.([0-9]{1,8})\\.([0-9]{1,8})");
    private final Activity activity;
    private final Button button;
    private final LinearLayout actions;
    private boolean started;
    private boolean visible;
    private Uri download;

    UpdateBadge(Activity activity, Runnable openGameFiles, Runnable openCrashReport) {
        this.activity = activity;
        actions = new LinearLayout(activity);
        actions.setOrientation(LinearLayout.VERTICAL);
        actions.setGravity(Gravity.RIGHT);
        button = addButton("Checking for updates…", 0);
        button.setOnClickListener(v -> open());
        button.setClickable(false);
        addButton(GameFileActions.TITLE, 6).setOnClickListener(v -> openGameFiles.run());
        addButton("Send Crash Report", 6).setOnClickListener(v -> openCrashReport.run());
    }

    private Button addButton(String text, int gap) {
        Button action = new Button(activity);
        action.setTextSize(13);
        action.setAllCaps(false);
        action.setTextColor(0xffece5d3);
        action.setText(text);
        GradientDrawable border = new GradientDrawable();
        border.setColor(0xe5121919);
        border.setStroke(dp(1), 0xffab9364);
        border.setCornerRadius(dp(3));
        action.setBackground(border);
        action.setPadding(dp(13), 0, dp(13), 0);
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, dp(40));
        params.topMargin = dp(gap);
        actions.addView(action, params);
        return action;
    }

    private int dp(int value) {
        return Math.round(value * activity.getResources().getDisplayMetrics().density);
    }

    void show(boolean show) {
        if (show == visible) return;
        if (!show) {
            activity.getWindowManager().removeViewImmediate(actions);
            visible = false;
            return;
        }
        WindowManager.LayoutParams params = new WindowManager.LayoutParams(
                WindowManager.LayoutParams.WRAP_CONTENT, WindowManager.LayoutParams.WRAP_CONTENT,
                WindowManager.LayoutParams.TYPE_APPLICATION_PANEL,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE |
                        WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL, PixelFormat.TRANSLUCENT);
        params.token = activity.getWindow().getDecorView().getWindowToken();
        if (params.token == null) return;
        params.gravity = Gravity.TOP | Gravity.RIGHT;
        params.x = dp(16);
        params.y = dp(12);
        activity.getWindowManager().addView(actions, params);
        visible = true;
        if (!started) {
            started = true;
            new Thread(this::check, "github-release-check").start();
        }
    }

    private void check() {
        HttpURLConnection connection = null;
        String message = "No Updates";
        Uri release = null;
        try {
            String version = activity.getPackageManager().getPackageInfo(activity.getPackageName(), 0).versionName;
            connection = (HttpURLConnection) new URL("https://api.github.com/repos/" + REPO + "/releases/latest").openConnection();
            connection.setConnectTimeout(5000);
            connection.setReadTimeout(5000);
            connection.setInstanceFollowRedirects(false);
            connection.setRequestProperty("Accept", "application/vnd.github+json");
            connection.setRequestProperty("User-Agent", "OpenAmalur-Android/" + version);
            int status = connection.getResponseCode();
            if (status == 200) {
                JSONObject latest = new JSONObject(readResponse(connection));
                String tag = latest.getString("tag_name");
                Uri page = Uri.parse(latest.getString("html_url"));
                if (!latest.optBoolean("draft") && !latest.optBoolean("prerelease")
                        && STABLE_TAG.matcher(tag).matches() && STABLE_TAG.matcher(version).matches()
                        && allowed(page)) {
                    if (newer(tag, version)) {
                        message = "Update Available!";
                        release = page;
                    } else {
                        message = UP_TO_DATE;
                    }
                }
            }
        } catch (Exception error) {
            android.util.Log.i("VeilUpdates", "GitHub release check unavailable", error);
        } finally {
            if (connection != null) connection.disconnect();
        }
        final String text = message;
        final Uri target = release;
        activity.runOnUiThread(() -> {
            if (activity.isDestroyed()) return;
            download = target;
            button.setText(text);
            button.setClickable(target != null);
        });
    }

    private static String readResponse(HttpURLConnection connection) throws IOException {
        try (InputStream input = connection.getInputStream(); ByteArrayOutputStream response = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[8192];
            int count;
            while ((count = input.read(buffer)) != -1) {
                if (response.size() + count > 262144) throw new IOException("GitHub release response exceeded 256 KiB");
                response.write(buffer, 0, count);
            }
            return new String(response.toByteArray(), StandardCharsets.UTF_8);
        }
    }

    private static boolean newer(String release, String installed) {
        Matcher latest = STABLE_TAG.matcher(release);
        Matcher current = STABLE_TAG.matcher(installed);
        if (!latest.matches() || !current.matches()) return false;
        for (int i = 1; i <= 3; i++) {
            int comparison = Integer.compare(Integer.parseInt(latest.group(i)), Integer.parseInt(current.group(i)));
            if (comparison != 0) return comparison > 0;
        }
        return false;
    }

    private static boolean allowed(Uri page) {
        return "https".equals(page.getScheme()) && "github.com".equals(page.getHost())
                && page.toString().startsWith(RELEASES + "/tag/");
    }

    private void open() {
        if (download == null) return;
        try {
            activity.startActivity(new Intent(Intent.ACTION_VIEW, download));
        } catch (ActivityNotFoundException error) {
            VeilDialogs.show(new AlertDialog.Builder(activity)
                    .setTitle("Open Amalur updates")
                    .setMessage("Open " + RELEASES + " in a browser.")
                    .setPositiveButton("OK", null));
        }
    }
}
