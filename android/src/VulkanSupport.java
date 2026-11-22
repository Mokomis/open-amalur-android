package org.openamalur.originalengine;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.content.pm.FeatureInfo;
import android.content.pm.PackageManager;
import android.net.Uri;

import java.io.IOException;
import java.util.Locale;

/** Warns before setup when the system Vulkan driver is older than the renderer needs. */
final class VulkanSupport {
    private static final int REQUIRED = (1 << 22) | (3 << 12); // Vulkan 1.3
    private static final String BUNDLED_DRIVER = "local-build/bundled-vulkan";
    private static final String PREFS = "vulkan-support";
    private static final String ACCEPTED = "accepted-below-1.3";
    private static final String REQUIREMENTS =
            "https://github.com/Mokomis/open-amalur-android#minimum-android-requirements";

    private VulkanSupport() {}

    /** Returns the system Vulkan version, or -1 when the device declares none. */
    private static int systemVersion(Context context) {
        FeatureInfo[] features = context.getPackageManager().getSystemAvailableFeatures();
        if (features == null) return -1;
        for (FeatureInfo feature : features) {
            if (PackageManager.FEATURE_VULKAN_HARDWARE_VERSION.equals(feature.name)) return feature.version;
        }
        return -1;
    }

    // Test builds that ship their own Vulkan driver add this asset to skip the check.
    private static boolean bundledDriver(Context context) {
        try {
            context.getAssets().open(BUNDLED_DRIVER).close();
            return true;
        } catch (IOException absent) {
            return false;
        }
    }

    /** True when the player should be warned before continuing. */
    static boolean shouldWarn(Context context) {
        if (bundledDriver(context)) return false;
        if (context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getBoolean(ACCEPTED, false))
            return false;
        return systemVersion(context) < REQUIRED;
    }

    static void warn(Activity activity, Runnable proceed) {
        int version = systemVersion(activity);
        String reported = version < 0 ? "does not report Vulkan support"
                : String.format(Locale.ROOT, "reports Vulkan %d.%d", version >>> 22, (version >>> 12) & 0x3ff);
        VeilDialogs.show(new AlertDialog.Builder(activity)
                .setTitle("Graphics driver not supported")
                .setMessage("This device's graphics driver " + reported + ". Open Amalur needs "
                        + "Vulkan 1.3, so the game will most likely close before the main menu.\n\n"
                        + "Some devices receive newer drivers than they report. Choose Try anyway "
                        + "to continue at your own risk.")
                .setCancelable(false)
                .setNegativeButton("Close", (dialog, button) -> activity.finish())
                .setNeutralButton("Requirements", (dialog, button) -> {
                    try {
                        activity.startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(REQUIREMENTS)));
                    } catch (ActivityNotFoundException ignored) {
                        // No browser; the dialog text already explains the requirement.
                    }
                    activity.finish();
                })
                .setPositiveButton("Try anyway", (dialog, button) -> {
                    activity.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
                            .putBoolean(ACCEPTED, true).apply();
                    proceed.run();
                }));
    }
}
