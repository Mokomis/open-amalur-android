package org.openamalur.originalengine;

import android.app.NativeActivity;
import android.app.AlertDialog;
import android.content.SharedPreferences;
import android.content.Intent;
import android.graphics.Canvas;
import android.graphics.PixelFormat;
import android.graphics.Paint;
import android.graphics.Point;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.os.Build;
import android.os.Bundle;
import android.text.InputFilter;
import android.text.InputType;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.util.SparseArray;
import android.util.Log;
import android.view.Display;
import android.view.InputDevice;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.view.inputmethod.EditorInfo;
import android.widget.EditText;

import java.io.IOException;
import java.io.File;

public final class VeilActivity extends NativeActivity {
    private static final Typeface CONTROL_LABEL_FONT = Typeface.create("sans-serif-medium", Typeface.NORMAL);
    private static final String[] FINISHER_LABEL = {"Finish", "A"};

    private static native int mode();
    private static native int aspect();
    private static native int input(int enabled, int buttons, int lt, int rt, int cast,
                                    int lx, int ly, int rx, int ry);
    private static native int menu(int destination);
    private static native int mash();
    private static native int finisherPosition();
    private static native int finisher();
    private static native int titleVisible();
    private static native int nameDialogVisible();
    private static native int submitName(String name);
    private static native int confirmNativeName();
    private static native int takeSaveImportRequest();

    private TouchControls overlay;
    private final SparseArray<Vibrator> activeRumble = new SparseArray<>();
    private UpdateBadge updateBadge;
    private GameFileActions gameFileActions;
    private SaveImportActions saveImportActions;
    private boolean noOverlays;
    private View launchCover;
    private android.graphics.Bitmap launchSample;
    private boolean samplingLaunch;
    private long titleReadyAt;

    private void addLaunchCover() {
        if (isFinishing() || isDestroyed() || titleVisible() != 0) return;
        WindowManager.LayoutParams params = new WindowManager.LayoutParams(
                -1, -1, WindowManager.LayoutParams.TYPE_APPLICATION_PANEL,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE |
                WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE |
                WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN, PixelFormat.OPAQUE);
        params.token = getWindow().getDecorView().getWindowToken();
        if (Build.VERSION.SDK_INT >= 30) params.setFitInsetsTypes(0);
        params.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        params.setTitle("Open Amalur startup");
        launchCover = new View(this);
        // Both activities use the same themed window background.
        android.util.TypedValue background = new android.util.TypedValue();
        getTheme().resolveAttribute(android.R.attr.windowBackground, background, true);
        launchCover.setBackgroundResource(background.resourceId);
        launchCover.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
        getWindowManager().addView(launchCover, params);
    }

    private void checkLaunchCover() {
        if (launchCover == null || samplingLaunch) return;
        if (titleReadyAt == 0) {
            if (titleVisible() == 0) return;
            titleReadyAt = android.os.SystemClock.uptimeMillis();
        }
        // The title script runs before Vulkan displays the menu. Sample the
        // underlying game window, which excludes our separate startup panel.
        // If the compositor cannot copy the surface, still hand control back.
        if (android.os.SystemClock.uptimeMillis() - titleReadyAt >= 2000) {
            removeLaunchCover();
            return;
        }
        if (launchSample == null) launchSample = android.graphics.Bitmap.createBitmap(
                32, 20, android.graphics.Bitmap.Config.ARGB_8888);
        samplingLaunch = true;
        try {
            android.view.PixelCopy.request(getWindow(), launchSample, result -> {
                samplingLaunch = false;
                if (launchCover == null || result != android.view.PixelCopy.SUCCESS) return;
                int lit = 0;
                for (int y = 0; y < launchSample.getHeight(); y++) {
                    for (int x = 0; x < launchSample.getWidth(); x++) {
                        int color = launchSample.getPixel(x, y);
                        if (android.graphics.Color.red(color) > 32 ||
                                android.graphics.Color.green(color) > 32) lit++;
                    }
                }
                if (lit >= 50) removeLaunchCover();
            }, new android.os.Handler(getMainLooper()));
        } catch (IllegalArgumentException surfaceNotReady) {
            samplingLaunch = false;
        }
    }

    private void removeLaunchCover() {
        if (launchCover == null) return;
        getWindowManager().removeViewImmediate(launchCover);
        launchCover = null;
        launchSample = null;
    }
    private AlertDialog namePrompt;
    private View nameCover;
    private boolean promptedForName;
    private boolean lastNameDialogState;
    private final Runnable watchMenuRequests = new Runnable() {
        @Override public void run() {
            if (isFinishing() || isDestroyed()) return;
            checkLaunchCover();
            if (hasWindowFocus() && takeSaveImportRequest() != 0) saveImportActions.show();
            boolean open = nameDialogVisible() != 0;
            if (open != lastNameDialogState) {
                Log.i("OANameInput", "name dialog=" + open + " focus=" + hasWindowFocus());
                lastNameDialogState = open;
            }
            if (!open) {
                promptedForName = false;
                if (namePrompt != null) namePrompt.dismiss();
                removeNameCover();
            } else if (!promptedForName && hasWindowFocus()) {
                promptedForName = true;
                Log.i("OANameInput", "showing keyboard prompt");
                showNamePrompt();
            }
            getWindow().getDecorView().postDelayed(this, 100);
        }
    };

    private void showNamePrompt() {
        showNamePrompt("", false);
    }

    private void showNamePrompt(String initialName, boolean retry) {
        EditText field = new EditText(this);
        field.setSingleLine(true);
        field.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_CAP_SENTENCES);
        field.setFilters(new InputFilter[]{new InputFilter.LengthFilter(24)});
        field.setHint("Character name");
        field.setText(initialName);
        field.setSelection(field.length());
        field.setTextColor(0xffece5d3);
        field.setHintTextColor(0xff908a7b);
        field.setBackgroundTintList(android.content.res.ColorStateList.valueOf(0xffaa9365));
        field.setImeOptions(EditorInfo.IME_ACTION_DONE);
        int padding = (int)(24 * getResources().getDisplayMetrics().density);
        field.setPadding(padding, 0, padding, 0);
        namePrompt = VeilDialogs.show(new AlertDialog.Builder(this)
                .setTitle("Name your character")
                .setMessage(retry ? "Could not confirm the name. Try again." : "Enter a name to use in the game.")
                .setView(field)
                .setCancelable(false)
                .setNegativeButton("Keep current", null)
                .setPositiveButton("Use name", null), field);
        AlertDialog prompt = namePrompt;
        prompt.getButton(AlertDialog.BUTTON_NEGATIVE).setOnClickListener(view -> confirmName(prompt, ""));
        prompt.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(view -> {
            String name = field.getText().toString().trim();
            if (name.isEmpty()) {
                prompt.setMessage("Enter a name, or choose Keep current.");
            } else {
                confirmName(prompt, name);
            }
        });
        field.setOnEditorActionListener((view, action, event) -> {
            if (action != EditorInfo.IME_ACTION_DONE) return false;
            namePrompt.getButton(AlertDialog.BUTTON_POSITIVE).performClick();
            return true;
        });
    }

    private void confirmName(AlertDialog prompt, String name) {
        // Dismiss the keyboard window so the engine can receive input. Cover
        // the native prompt until the game finishes confirming the name.
        WindowManager.LayoutParams coverParams = new WindowManager.LayoutParams(
                WindowManager.LayoutParams.MATCH_PARENT, WindowManager.LayoutParams.MATCH_PARENT,
                WindowManager.LayoutParams.TYPE_APPLICATION_PANEL,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE |
                        WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE |
                        WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN, PixelFormat.OPAQUE);
        coverParams.token = getWindow().getDecorView().getWindowToken();
        nameCover = new View(this);
        nameCover.setBackgroundColor(0xff000000);
        getWindowManager().addView(nameCover, coverParams);
        prompt.dismiss();
        getWindow().getDecorView().postDelayed(() -> {
            if (isFinishing() || isDestroyed() || nameDialogVisible() == 0) return;
            if (submitName(name) == 0) {
                retryName(name);
                return;
            }
            getWindow().getDecorView().postDelayed(() -> {
                if (nameDialogVisible() != 0) confirmNativeName();
                getWindow().getDecorView().postDelayed(() -> {
                    if (nameDialogVisible() != 0) retryName(name);
                }, 1500);
            }, 150);
        }, 200);
    }

    private void retryName(String name) {
        if (isFinishing() || isDestroyed()) return;
        showNamePrompt(name, true);
        removeNameCover();
    }

    private void removeNameCover() {
        if (nameCover == null) return;
        getWindowManager().removeViewImmediate(nameCover);
        nameCover = null;
    }

    @Override public void onCreate(Bundle state) {
        try {
            LocalEngine.load(this);
        } catch (IOException error) {
            throw new IllegalStateException(error);
        }
        super.onCreate(state);
        noOverlays = getIntent().getBooleanExtra("oa_no_overlays", false);
        if (GameStorage.root(this) == null || !GameFiles.ready(this, GameStorage.root(this))) {
            startActivity(new Intent(this, SetupActivity.class));
            finish();
            return;
        }
        getWindow().getDecorView().post(this::addLaunchCover);
        requestDisplayMode();
        overlay = new TouchControls();
        gameFileActions = new GameFileActions(this);
        saveImportActions = new SaveImportActions(this);
        updateBadge = new UpdateBadge(this, gameFileActions::show, this::openCrashReport);
        getWindow().getDecorView().post(() -> {
            if (isFinishing() || isDestroyed() || noOverlays) return;
            WindowManager.LayoutParams controls = new WindowManager.LayoutParams(
                    WindowManager.LayoutParams.MATCH_PARENT,
                    WindowManager.LayoutParams.MATCH_PARENT,
                    WindowManager.LayoutParams.TYPE_APPLICATION_PANEL,
                    WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE |
                    WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE |
                    WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN |
                    WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL,
                    PixelFormat.TRANSLUCENT);
            controls.token = getWindow().getDecorView().getWindowToken();
            if (Build.VERSION.SDK_INT >= 30) {
                controls.setFitInsetsTypes(0);
                controls.layoutInDisplayCutoutMode =
                        WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
            } else {
                controls.layoutInDisplayCutoutMode =
                        WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            }
            controls.setTitle("Veil touch controls");
            getWindowManager().addView(overlay, controls);
            overlay.resume();
        });
    }

    /** NativeActivity's built-in externalDataPath always names the primary volume. */
    public String gameStoragePath() {
        File root = GameStorage.root(this);
        return root == null ? null : root.getAbsolutePath();
    }

    public String gameSourcePath() {
        File source = GameStorage.sourceRoot(this);
        return source == null ? null : source.getAbsolutePath();
    }

    private void openCrashReport() {
        startActivity(new Intent(this, CrashReportActivity.class)
                .putExtra(CrashReportActivity.CREATE_ON_OPEN, true));
    }


    private void requestDisplayMode() {
        Display display = getWindowManager().getDefaultDisplay();
        Display.Mode current = display.getMode();
        for (Display.Mode mode : display.getSupportedModes()) {
            if (mode.getPhysicalWidth() == current.getPhysicalWidth() &&
                    mode.getPhysicalHeight() == current.getPhysicalHeight() &&
                    Math.abs(mode.getRefreshRate() - 120f) < 0.5f) {
                WindowManager.LayoutParams attributes = getWindow().getAttributes();
                attributes.preferredDisplayModeId = mode.getModeId();
                getWindow().setAttributes(attributes);
                return;
            }
        }
    }

    // Query the display hosting this activity; native code removes repeated
    // sizes (the OS can report one mode per refresh rate).
    @SuppressWarnings("deprecation")
    private int[] gameDisplaySizes() {
        Display display = getWindowManager().getDefaultDisplay();
        Display.Mode[] modes = display.getSupportedModes();
        int[] sizes = new int[modes.length * 2];
        for (int i = 0; i < modes.length; ++i) {
            sizes[i * 2] = modes[i].getPhysicalWidth();
            sizes[i * 2 + 1] = modes[i].getPhysicalHeight();
        }
        return sizes;
    }

    @SuppressWarnings("deprecation")
    private long gameWindowSize() {
        int width, height;
        if (Build.VERSION.SDK_INT >= 30) {
            Rect bounds = getWindowManager().getCurrentWindowMetrics().getBounds();
            width = bounds.width();
            height = bounds.height();
        } else {
            Point size = new Point();
            getWindowManager().getDefaultDisplay().getRealSize(size);
            width = size.x;
            height = size.y;
        }
        return ((long)width << 32) | (height & 0xffffffffL);
    }

    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request == GameFileActions.SELECT_FATESWORN && result == RESULT_OK && data != null) {
            gameFileActions.importFrom(data.getData());
        } else if (request == SaveImportActions.SELECT_SAVES && result == RESULT_OK && data != null) {
            saveImportActions.importFrom(data);
        }
    }

    @Override protected void onResume() {
        super.onResume();
        Log.i("OANameInput", "onResume");
        overlay.resume();
        getWindow().getDecorView().post(watchMenuRequests);
    }

    @Override public void onBackPressed() {
        if (overlay != null && overlay.editing) {
            overlay.closeEditor();
        } else {
            super.onBackPressed();
        }
    }

    @Override protected void onDestroy() {
        getWindow().getDecorView().removeCallbacks(watchMenuRequests);
        removeLaunchCover();
        if (namePrompt != null) namePrompt.dismiss();
        removeNameCover();
        updateBadge.show(false);
        if (overlay.isAttachedToWindow()) getWindowManager().removeViewImmediate(overlay);
        super.onDestroy();
    }

    @Override protected void onPause() {
        getWindow().getDecorView().removeCallbacks(watchMenuRequests);
        updateBadge.show(false);
        overlay.suspend();
        super.onPause();
    }

    private synchronized void rumbleController(int deviceId, int left, int right) {
        int strength = Math.max(left, right);
        Vibrator vibrator = activeRumble.get(deviceId);
        if (strength == 0) {
            if (vibrator != null) vibrator.cancel();
            activeRumble.remove(deviceId);
            return;
        }
        if (vibrator == null) {
            InputDevice device = InputDevice.getDevice(deviceId);
            if (device == null) return;
            vibrator = controllerVibrator(device);
            if (vibrator == null) return;
            activeRumble.put(deviceId, vibrator);
            android.util.Log.i("VeilRumble", "Vibration enabled for " + device.getName());
        }
        int amplitude = 1 + (strength * 254 + 32767) / 65535;
        vibrator.vibrate(VibrationEffect.createWaveform(new long[]{100}, new int[]{amplitude}, 0));
    }

    private static Vibrator controllerVibrator(InputDevice device) {
        Vibrator direct = inputVibrator(device);
        if (direct.hasVibrator()) return direct;

        // Ember can expose the controller's motors on a separate input device.
        Vibrator companion = null;
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice candidate = InputDevice.getDevice(id);
            if (candidate == null || !candidate.getName().endsWith(" (rumble)") ||
                candidate.getVendorId() != device.getVendorId() ||
                candidate.getProductId() != device.getProductId()) continue;
            Vibrator vibration = inputVibrator(candidate);
            if (!vibration.hasVibrator()) continue;
            if (companion != null) return null;
            companion = vibration;
        }
        return companion;
    }

    @SuppressWarnings("deprecation")
    private static Vibrator inputVibrator(InputDevice device) {
        return Build.VERSION.SDK_INT >= 31 ? device.getVibratorManager().getDefaultVibrator()
                : device.getVibrator();
    }

    private void enterImmersiveMode() {
        if (Build.VERSION.SDK_INT >= 30) {
            WindowInsetsController controller = getWindow().getInsetsController();
            if (controller != null) {
                controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                controller.hide(WindowInsets.Type.systemBars());
            }
        } else {
            getWindow().getDecorView().setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_LAYOUT_STABLE |
                    View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        }
    }

    @Override public void onWindowFocusChanged(boolean focused) {
        super.onWindowFocusChanged(focused);
        if (focused) enterImmersiveMode();
        if (overlay != null) {
            if (focused) overlay.resume();
            else overlay.releaseAll();
        }
    }

    private static final class Control {
        final String name, settingsKey;
        final String[] labelLines;
        final float xDefault, yDefault, sizeDefault;
        final int button, shortcut;
        float x, y, size;
        int pointer = -1;
        Control(String label, String settingsKey, float x, float y, float size, int button, int shortcut) {
            name = label.replace('\n', ' ');
            labelLines = label.split("\n");
            this.settingsKey = settingsKey;
            xDefault = this.x = x;
            yDefault = this.y = y;
            sizeDefault = this.size = size;
            this.button = button;
            this.shortcut = shortcut;
        }

        void reset() {
            x = xDefault;
            y = yDefault;
            size = sizeDefault;
        }
    }

    private final class TouchControls extends View {
        private static final int GOLD = 0xffab9364, IVORY = 0xffece5d3;
        private static final int PANEL = 0xef111819, FILL = 0xa4141a1b;
        private static final float EDITOR_FOOTER_HEIGHT = 68f / 720f;
        private static final float CONTROL_EDGE_MARGIN = 6f / 720f;
        private static final float LEGACY_ASPECT = 17778f / 10000f;
        private static final String FULL_DISPLAY_LAYOUT = "full-display-layout";
        private static final int MOVE = 0, LOOK = 1, FATE = 12, MENU = 9;
        private final Control[] controls = {
            new Control("Move", "move", .145f, .78f, .195f, 0, 0),
            new Control("Camera", "look", .68f, .63f, .152f, 0, 0),
            new Control("Primary\nAttack", "sword", .921f, .65f, .123f, 0x4000, 0),
            new Control("Secondary\nAttack", "axes", .838f, .78f, .105f, 0x8000, 0),
            new Control("Dodge", "dodge", .932f, .84f, .091f, 0x2000, 0),
            new Control("Block\nLT", "shield", .824f, .61f, .095f, 0, 0),
            new Control("Interact", "hand", .922f, .49f, .098f, 0x1000, 0),
            new Control("Abilities\nRT", "magic", .735f, .83f, .085f, 0, 0),
            new Control("Run/Leap", "sprint", .242f, .85f, .100f, 0, 0),
            new Control("Game\nMenu", "person", .418f, .150f, .078f, 0, 1),
            new Control("Inventory", "bag", .475f, .150f, .078f, 0, 2),
            new Control("World\nMap", "map", .532f, .150f, .078f, 0, 3),
            new Control("Reckoning\nLT + RT", "fate", .832f, .49f, .084f, 0, 0),
            new Control("Quests", "book", .589f, .150f, .078f, 0, 4),
            new Control("Edit\nLayout", "edit", .646f, .150f, .078f, 0, 5),
            new Control("Item\nRadial", "items", .703f, .150f, .078f, 0, 6),
            new Control("Cast", "cast", .762f, .723f, .081f, 0, 0),
        };
        private static final int BLOCK = 5, INTERACT = 6, MAGIC = 7, SPRINT = 8, CAST = 16;
        private final SharedPreferences preferences = getSharedPreferences("veil-touch", MODE_PRIVATE);
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final RectF screen = new RectF();
        private final RectF gameViewport = new RectF();
        private final RectF done = new RectF(), defaults = new RectF();
        private final RectF toggle = new RectF(), minus = new RectF(), plus = new RectF();
        private boolean enabled = preferences.getBoolean("enabled", true);
        private boolean restoreLegacyPositions =
                preferences.contains("move.x") && !preferences.getBoolean(FULL_DISPLAY_LAYOUT, false);
        private float controlReferenceHeight;
        private boolean visible, editing, running, mashVisible, tutorialVisible, triggerLesson;
        private int slotPointer = -1, pressedSlot = 0;
        private int finisherPointer = -1;
        private int finisherPosition;
        private final RectF mashButton = new RectF();
        private int selected, editorPointer = -1;
        private int disabledMenuPointer = -1;
        private float dragOffsetX, dragOffsetY;
        private float moveX, moveY, lookX, lookY, lookStartX, lookStartY;
        private final long[] startedAt = new long[controls.length];
        private final long[] releasedUntil = new long[controls.length];
        private boolean hasGesture;
        private final Runnable pulse = new Runnable() {
            @Override public void run() {
                updateBadge.show(!noOverlays && VeilActivity.this.hasWindowFocus() && titleVisible() != 0);
                int current = mode();
                if (current == 2) openEditor();
                boolean next = current == 1 || current == 3;
                boolean nextMash = current == 4;
                boolean nextTutorial = current == 5;
                boolean nextTriggers = current == 6;
                updateWindowInput(next || editing || nextMash || nextTutorial || nextTriggers);
                if (mashVisible != nextMash || tutorialVisible != nextTutorial) {
                    mashVisible = nextMash;
                    tutorialVisible = nextTutorial;
                    invalidate();
                }
                if (triggerLesson != nextTriggers) {
                    releaseAll();
                    triggerLesson = nextTriggers;
                    invalidate();
                }
                if (visible && !next) releaseAll();
                if (visible != next) {
                    visible = next;
                    invalidate();
                }
                int nextFinisher = next ? finisherPosition() : 0;
                if (finisherPosition != nextFinisher) {
                    finisherPosition = nextFinisher;
                    invalidate();
                }
                if (visible || triggerLesson) send();
                if (running) postDelayed(this, visible || editing || mashVisible || tutorialVisible || triggerLesson ? 25 : 90);
            }
        };

        TouchControls() {
            super(VeilActivity.this);
            setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
            for (Control c : controls) {
                c.x = preferences.getFloat(c.settingsKey + ".x", c.xDefault);
                c.y = preferences.getFloat(c.settingsKey + ".y", c.yDefault);
                c.size = preferences.getFloat(c.settingsKey + ".size", c.sizeDefault);
            }
        }

        void resume() {
            if (running) return;
            running = true;
            send();
            post(pulse);
        }

        void suspend() {
            running = false;
            removeCallbacks(pulse);
            releaseAll();
            updateWindowInput(false);
        }

        private void persist() {
            SharedPreferences.Editor settings = preferences.edit()
                    .putBoolean("enabled", enabled)
                    .putBoolean(FULL_DISPLAY_LAYOUT, true);
            for (Control c : controls) {
                settings.putFloat(c.settingsKey + ".x", c.x).putFloat(c.settingsKey + ".y", c.y);
                settings.putFloat(c.settingsKey + ".size", c.size);
            }
            settings.apply();
        }

        private void updateWindowInput(boolean active) {
            if (!isAttachedToWindow()) return;
            WindowManager.LayoutParams window = (WindowManager.LayoutParams)getLayoutParams();
            int flags = window.flags;
            if (active) window.flags &= ~WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE;
            else window.flags |= WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE;
            if (flags != window.flags) getWindowManager().updateViewLayout(this, window);
        }

        private void closeEditor() {
            layoutControls();
            if (!restoreLegacyPositions) persist();
            editing = false;
            send();
            invalidate();
        }

        private void openEditor() {
            releaseAll();
            editing = true;
            updateWindowInput(true);
            invalidate();
        }

        private void layoutControls() {
            float width = getWidth(), height = getHeight();
            if (width <= 0 || height <= 0) return;

            screen.set(0, 0, width, height);
            float ratio = aspect() / 10000f;
            float gameWidth = Math.min(width, height * ratio);
            float gameHeight = Math.min(height, width / ratio);
            gameViewport.set((width - gameWidth) / 2f, (height - gameHeight) / 2f,
                             (width + gameWidth) / 2f, (height + gameHeight) / 2f);

            // Keep existing button sizes and convert saved positions from the
            // old 16:9 area to the full overlay without moving them on screen.
            controlReferenceHeight = Math.min(height, width / LEGACY_ASPECT);
            if (restoreLegacyPositions) {
                float oldWidth = Math.min(width, height * LEGACY_ASPECT);
                float oldLeft = (width - oldWidth) / 2f;
                float oldTop = (height - controlReferenceHeight) / 2f;
                for (Control c : controls) {
                    c.x = (oldLeft + c.x * oldWidth) / width;
                    c.y = (oldTop + c.y * controlReferenceHeight) / height;
                }
                restoreLegacyPositions = false;
            }

            if (!editing) return;
            float u = height / 720f;
            done.set(screen.right - 114*u, screen.bottom - 55*u, screen.right - 20*u, screen.bottom - 13*u);
            defaults.set(done.left - 90*u, done.top, done.left - 10*u, done.bottom);
            toggle.set(defaults.left - 145*u, done.top, defaults.left - 10*u, done.bottom);
            plus.set(toggle.left - 63*u, done.top, toggle.left - 20*u, done.bottom);
            minus.set(plus.left - 103*u, done.top, plus.left - 60*u, done.bottom);
        }

        private float cx(Control c) { return c.x * screen.width(); }
        private float cy(Control c) { return c.y * screen.height(); }
        private float radius(Control c) { return c.size * controlReferenceHeight * .5f; }

        @Override protected void onDraw(Canvas canvas) {
            super.onDraw(canvas);
            if (!visible && !editing && !mashVisible && !tutorialVisible && !triggerLesson) return;
            layoutControls();
            if (triggerLesson && !editing) {
                drawControl(canvas, BLOCK);
                drawControl(canvas, MAGIC);
                drawControl(canvas, FATE);
                return;
            }
            if ((mashVisible || tutorialVisible) && !editing) {
                float u = gameViewport.height() / 720f;
                mashButton.set(gameViewport.right - 207*u, gameViewport.bottom - 136*u,
                               gameViewport.right - 43*u, gameViewport.bottom - 65*u);
                if (tutorialVisible) {
                    mashButton.set(gameViewport.centerX() - 88*u, gameViewport.centerY() + 21*u,
                                   gameViewport.centerX() + 88*u, gameViewport.centerY() + 78*u);
                }
                editorButton(canvas, mashButton, tutorialVisible ? "A   Continue" : "Tap rapidly", u);
                return;
            }
            if (editing) {
                paint.setColor(0xd4101516);
                canvas.drawRect(screen, paint);
            }
            if (visible && !enabled && !editing) {
                drawControl(canvas, MENU);
                return;
            }
            for (int i = 0; i < controls.length; i++) drawControl(canvas, i);
            if (editing) drawEditor(canvas);
        }

        private void drawControl(Canvas canvas, int index) {
            Control c = controls[index];
            float x = cx(c), y = cy(c), r = radius(c);
            boolean active = c.pointer >= 0 || (editing && selected == index);
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(active ? 0xd34e4432 : FILL);
            canvas.drawCircle(x, y, r, paint);
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(Math.max(1.3f, controlReferenceHeight / 480));
            paint.setColor(active ? IVORY : 0xbda99164);
            canvas.drawCircle(x, y, r, paint);
            paint.setStyle(Paint.Style.FILL);
            if (index == MOVE || index == LOOK) {
                float dx = (index == MOVE ? moveX : lookX) * r * .54f;
                float dy = -(index == MOVE ? moveY : lookY) * r * .54f;
                paint.setColor(active ? 0xc5b39f72 : 0x6eb39f72);
                canvas.drawCircle(x + dx, y + dy, r * .31f, paint);
                return;
            }
            String[] label = index == INTERACT && finisherPosition != 0 ? FINISHER_LABEL : c.labelLines;
            drawControlLabel(canvas, label, x, y, r);
        }

        private void drawControlLabel(Canvas canvas, String[] lines, float x, float y, float radius) {
            paint.setColor(IVORY);
            paint.setTypeface(CONTROL_LABEL_FONT);
            float size = radius * (lines.length == 1 ? .49f : .46f);
            paint.setTextSize(size);
            float widest = 0;
            for (String line : lines) widest = Math.max(widest, paint.measureText(line));
            float fittedSize = size * Math.min(1, radius * 1.76f / widest);
            paint.setTextSize(fittedSize);
            float lineHeight = fittedSize * 1.12f;
            float baseline = y - (paint.ascent() + paint.descent() + (lines.length - 1) * lineHeight) / 2;
            paint.setTextAlign(Paint.Align.CENTER);
            for (String line : lines) {
                canvas.drawText(line, x, baseline, paint);
                baseline += lineHeight;
            }
            paint.setTextAlign(Paint.Align.LEFT);
        }

        private int abilitySlot(float x, float y) {
            float unit = gameViewport.width() / 1280f;
            float railCenter = gameViewport.centerX() + 20f * unit;
            float relativeX = (x - railCenter) / unit;
            int slot = Math.round((relativeX + 338f) / 75f) + 1;
            if (slot < 1 || slot > 10 || Math.abs(relativeX - ((slot - 1) * 75f - 338f)) > 34)
                return 0;
            float bottomOffset = (gameViewport.bottom - y) / (gameViewport.height() / 720f);
            return bottomOffset >= 0 && bottomOffset <= 64 ? slot : 0;
        }

        private boolean floatingFinisherHit(float x, float y) {
            if (finisherPosition == 0) return false;
            int position = finisherPosition - 1;
            float promptX = gameViewport.left + (position % 1281) * gameViewport.width() / 1280f;
            float promptY = gameViewport.top + (position / 1281) * gameViewport.height() / 720f;
            return Math.hypot(x - promptX, y - promptY) <= gameViewport.height() * .065f;
        }

        private boolean mashTouch(MotionEvent event) {
            int action = event.getActionMasked(), index = event.getActionIndex();
            if ((action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) &&
                mashButton.contains(event.getX(index), event.getY(index))) {
                mash();
            }
            return true;
        }

        private void text(Canvas canvas, String label, float x, float y, float size, int color) {
            paint.setTypeface(Typeface.create("sans-serif", Typeface.NORMAL));
            paint.setTextSize(size);
            paint.setColor(color);
            canvas.drawText(label, x, y, paint);
        }

        private void editorButton(Canvas canvas, RectF bounds, String label, float u) {
            paint.setColor(0xff222a2a);
            canvas.drawRoundRect(bounds, 2*u, 2*u, paint);
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(u);
            paint.setColor(GOLD);
            canvas.drawRoundRect(bounds, 2*u, 2*u, paint);
            paint.setStyle(Paint.Style.FILL);
            paint.setTextSize(17*u);
            text(canvas, label, bounds.centerX() - paint.measureText(label) / 2,
                 bounds.centerY() + 6*u, 17*u, IVORY);
        }

        private void drawEditor(Canvas canvas) {
            float u = screen.height() / 720;
            float footerTop = screen.bottom - screen.height() * EDITOR_FOOTER_HEIGHT;
            paint.setColor(PANEL);
            canvas.drawRect(screen.left, footerTop, screen.right, screen.bottom, paint);
            paint.setColor(GOLD);
            paint.setStrokeWidth(u);
            canvas.drawLine(screen.left, footerTop, screen.right, footerTop, paint);
            text(canvas, controls[selected].name, screen.left+22*u, screen.bottom-35*u, 22*u, IVORY);
            text(canvas, "Drag a control. Select one to resize it.", screen.left+22*u,
                 screen.bottom-15*u, 15*u, GOLD);
            editorButton(canvas, toggle, enabled ? "Controls On" : "Gameplay Off", u);
            editorButton(canvas, defaults, "Reset", u);
            editorButton(canvas, done, "Done", u);
            editorButton(canvas, minus, "−", u);
            editorButton(canvas, plus, "+", u);
            paint.setTextAlign(Paint.Align.CENTER);
            text(canvas, Math.round(100*controls[selected].size/controls[selected].sizeDefault)+"%",
                 (minus.right + plus.left) / 2, screen.bottom-28*u, 15*u, IVORY);
            paint.setTextAlign(Paint.Align.LEFT);
        }

        private int hit(float x, float y) {
            for (int i = controls.length - 1; i >= 0; i--) {
                Control c = controls[i];
                if (triggerLesson && i != BLOCK && i != MAGIC && i != FATE) continue;
                if (Math.hypot(x - cx(c), y - cy(c)) <= radius(c)) return i;
            }
            return -1;
        }

        private float clamp(float x, float minimum, float maximum) {
            return Math.max(minimum, Math.min(maximum, x));
        }

        private void position(Control c, float x, float y) {
            float radius = radius(c);
            float sideMargin = radius / screen.width();
            c.x = clamp(x / screen.width(), sideMargin, 1f - sideMargin);
            float verticalMargin = (radius + CONTROL_EDGE_MARGIN * controlReferenceHeight) / screen.height();
            c.y = clamp(y / screen.height(), verticalMargin, 1f - verticalMargin - EDITOR_FOOTER_HEIGHT);
        }

        private boolean editorTouch(MotionEvent event) {
            int action = event.getActionMasked(), index = event.getActionIndex();
            if (action == MotionEvent.ACTION_DOWN) {
                float x = event.getX(index), y = event.getY(index);
                if (done.contains(x,y)) {
                    closeEditor();
                } else if (defaults.contains(x,y)) {
                    for (Control control : controls) control.reset();
                } else if (toggle.contains(x,y)) {
                    enabled = !enabled;
                    send();
                } else if (minus.contains(x,y) || plus.contains(x,y)) {
                    Control control = controls[selected];
                    control.size = clamp(control.size + (plus.contains(x,y) ? .009f : -.009f), .055f, .31f);
                    position(control, cx(control), cy(control));
                } else {
                    int control = hit(x,y);
                    if (control >= 0) {
                        selected = control;
                        editorPointer = event.getPointerId(index);
                        dragOffsetX = cx(controls[control]) - x;
                        dragOffsetY = cy(controls[control]) - y;
                    }
                }
            } else if (action == MotionEvent.ACTION_MOVE && editorPointer >= 0) {
                int i = event.findPointerIndex(editorPointer);
                if (i >= 0) position(controls[selected], event.getX(i) + dragOffsetX, event.getY(i) + dragOffsetY);
            } else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
                editorPointer = -1;
            }
            invalidate();
            return true;
        }

        void releaseAll() {
            for (int i = 0; i < controls.length; i++) {
                controls[i].pointer = -1;
                releasedUntil[i] = 0;
            }
            moveX = moveY = lookX = lookY = 0;
            disabledMenuPointer = -1;
            slotPointer = -1;
            pressedSlot = 0;
            finisherPointer = -1;
            hasGesture = false;
            send();
        }

        private void send() {
            int buttons = 0, lt = 0, rt = 0, cast = 0;
            long now = android.os.SystemClock.uptimeMillis();
            for (int i = 0; i < controls.length; i++) {
                if (controls[i].pointer < 0 && now >= releasedUntil[i]) continue;
                buttons |= controls[i].button;
                if (i == BLOCK || i == FATE) lt = 255;
                if (i == MAGIC || i == FATE) rt = 255;
                if (i == SPRINT) buttons |= 0x40;
                if (i == CAST) cast = 1;
            }
            input(enabled ? 1 : 0, buttons, lt, rt, cast, (int)(32767*moveX), (int)(32767*moveY),
                  (int)(32767*lookX), (int)(32767*lookY));
        }

        private void axis(int which, float x, float y) {
            Control c = controls[which];
            float dx = (x - (which == MOVE ? cx(c) : lookStartX)) / radius(c);
            float dy = -(y - (which == MOVE ? cy(c) : lookStartY)) / radius(c);
            float length = (float)Math.hypot(dx, dy);
            float magnitude = clamp((length - .12f) / .88f, 0, 1);
            float scale = length <= .12f ? 0 : magnitude / length;
            if (which == MOVE) {
                moveX = dx * scale;
                moveY = dy * scale;
            } else {
                lookX = dx * scale;
                lookY = dy * scale;
            }
        }

        private boolean disabledMenuTouch(MotionEvent event) {
            int action = event.getActionMasked(), index = event.getActionIndex();
            int pointer = event.getPointerId(index);
            boolean onMenu = hit(event.getX(index), event.getY(index)) == MENU;
            if (action == MotionEvent.ACTION_DOWN && onMenu) disabledMenuPointer = pointer;
            if (disabledMenuPointer < 0) return false;
            if (action == MotionEvent.ACTION_UP && pointer == disabledMenuPointer) {
                if (onMenu) menu(1);
                disabledMenuPointer = -1;
            } else if (action == MotionEvent.ACTION_CANCEL) {
                disabledMenuPointer = -1;
            }
            return true;
        }

        @Override public boolean onTouchEvent(MotionEvent event) {
            if (!visible && !editing && !hasGesture && !mashVisible && !tutorialVisible && !triggerLesson) return false;
            layoutControls();
            if (editing) return editorTouch(event);
            if (mashVisible || tutorialVisible) return mashTouch(event);
            if (!enabled) return disabledMenuTouch(event);
            int action = event.getActionMasked(), index = event.getActionIndex();
            if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
                float x = event.getX(index), y = event.getY(index);
                int which = hit(x,y);
                if (finisherPointer < 0 && !triggerLesson && finisherPosition != 0 &&
                    (which == INTERACT || floatingFinisherHit(x, y))) {
                    finisherPointer = event.getPointerId(index);
                    hasGesture = true;
                    return true;
                }
                if (which < 0 && !triggerLesson && slotPointer < 0) {
                    int slot = abilitySlot(x, y);
                    if (slot != 0) {
                        slotPointer = event.getPointerId(index);
                        pressedSlot = slot;
                        hasGesture = true;
                        return true;
                    }
                }
                if (which < 0 && !triggerLesson && x > gameViewport.centerX() &&
                    y > gameViewport.top + .26f * gameViewport.height() &&
                    y < gameViewport.bottom && controls[LOOK].pointer < 0) which = LOOK;
                hasGesture = true;
                if (which < 0) return true;
                Control control = controls[which];
                if (control.pointer >= 0) return true;
                control.pointer = event.getPointerId(index);
                startedAt[which] = android.os.SystemClock.uptimeMillis();
                releasedUntil[which] = 0;
                if (which == LOOK) {
                    lookStartX = x;
                    lookStartY = y;
                }
                if (which <= LOOK) axis(which,x,y);
            } else if (action == MotionEvent.ACTION_MOVE) {
                for (int i = MOVE; i <= LOOK; i++) {
                    int pointer = event.findPointerIndex(controls[i].pointer);
                    if (pointer >= 0) axis(i, event.getX(pointer), event.getY(pointer));
                }
            } else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) {
                int pointer = event.getPointerId(index);
                if (pointer == finisherPointer) {
                    float x = event.getX(index), y = event.getY(index);
                    if (hit(x, y) == INTERACT || floatingFinisherHit(x, y)) finisher();
                    finisherPointer = -1;
                }
                if (pointer == slotPointer) {
                    if (abilitySlot(event.getX(index), event.getY(index)) == pressedSlot)
                        menu(10 + pressedSlot);
                    slotPointer = -1;
                    pressedSlot = 0;
                }
                for (int i = 0; i < controls.length; i++) {
                    Control c = controls[i];
                    if (c.pointer != pointer) continue;
                    c.pointer = -1;
                    if (i > LOOK && c.shortcut == 0)
                        releasedUntil[i] = startedAt[i] + 72;
                    if (i == MOVE) moveX = moveY = 0;
                    if (i == LOOK) lookX = lookY = 0;
                    if (c.shortcut > 0 && hit(event.getX(index),event.getY(index)) == i) {
                        releaseAll();
                        if (c.shortcut == 5) openEditor();
                        else menu(c.shortcut);
                    }
                    break;
                }
                if (action == MotionEvent.ACTION_UP) hasGesture = false;
            } else if (action == MotionEvent.ACTION_CANCEL) {
                releaseAll();
            }
            send();
            invalidate();
            return true;
        }
    }
}
