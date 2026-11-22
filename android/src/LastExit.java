package org.openamalur.originalengine;

import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.Context;
import android.content.SharedPreferences;
import android.os.Build;

import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Recognises when the previous run ended through the engine's fatal-error path
 * (rt_fatal exits with status 3) so the next launch can explain what happened.
 */
final class LastExit {
    static final int NONE = 0, GPU_LOST = 1, ENGINE_ERROR = 2;
    private static final int FATAL_STATUS = 3;
    private static final int VK_ERROR_DEVICE_LOST = -4;
    private static final long LOG_TAIL = 256 * 1024;
    private static final String PREFS = "last-exit";
    private static final String SEEN = "seen-timestamp";
    private static final Pattern FATAL = Pattern.compile("FATAL: (.*)");
    private static final Pattern VULKAN_RESULT = Pattern.compile("Vulkan .*: (-?[0-9]{1,9})\\s*$");

    private LastExit() {}

    /** Classifies the newest unseen process exit, then marks it as seen. */
    static int check(Context context) {
        if (Build.VERSION.SDK_INT < 30) return NONE;
        ApplicationExitInfo exit;
        try {
            ActivityManager manager = (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
            List<ApplicationExitInfo> exits = manager.getHistoricalProcessExitReasons(
                    context.getPackageName(), 0, 1);
            if (exits.isEmpty()) return NONE;
            exit = exits.get(0);
        } catch (RuntimeException unavailable) {
            return NONE;
        }
        SharedPreferences prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        long seen = prefs.getLong(SEEN, 0);
        if (seen == 0) {
            // First run with this check: ignore exits from before the update was installed.
            try {
                seen = context.getPackageManager().getPackageInfo(context.getPackageName(), 0).lastUpdateTime;
            } catch (Exception unavailable) {
                seen = System.currentTimeMillis();
            }
        }
        if (exit.getTimestamp() <= seen) return NONE;
        prefs.edit().putLong(SEEN, exit.getTimestamp()).apply();
        if (exit.getReason() != ApplicationExitInfo.REASON_EXIT_SELF
                || exit.getStatus() != FATAL_STATUS) return NONE;
        return lastFatalWasDeviceLoss(GameStorage.root(context)) ? GPU_LOST : ENGINE_ERROR;
    }

    private static long runNumber(File file) {
        String name = file.getName();
        if (!name.matches("run-[0-9]{1,19}")) return -1;
        try { return Long.parseLong(name.substring(4)); }
        catch (NumberFormatException invalid) { return -1; }
    }

    private static boolean lastFatalWasDeviceLoss(File root) {
        if (root == null) return false;
        File[] runs = root.listFiles(file -> file.isDirectory() && runNumber(file) >= 0);
        if (runs == null || runs.length == 0) return false;
        File latest = runs[0];
        for (File run : runs) if (runNumber(run) > runNumber(latest)) latest = run;
        String fatal = null;
        try (RandomAccessFile log = new RandomAccessFile(new File(latest, "pc-runtime.log"), "r")) {
            long length = log.length();
            long start = Math.max(0, length - LOG_TAIL);
            byte[] tail = new byte[(int) (length - start)];
            log.seek(start);
            log.readFully(tail);
            Matcher line = FATAL.matcher(new String(tail, StandardCharsets.UTF_8));
            while (line.find()) fatal = line.group(1);
        } catch (IOException unavailable) {
            return false;
        }
        if (fatal == null) return false;
        Matcher result = VULKAN_RESULT.matcher(fatal);
        return result.find() && Integer.parseInt(result.group(1)) == VK_ERROR_DEVICE_LOST;
    }
}
