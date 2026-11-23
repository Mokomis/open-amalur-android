package org.openamalur.originalengine;

import java.io.File;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import static org.openamalur.originalengine.LockTouch.Action.*;

public final class TouchIssue17Test {
    private static void check(boolean value, String message) {
        if (!value) throw new AssertionError(message);
    }

    private static byte[] personal(String extra) {
        return ("\ufeff[DEFAULT]\nDisplayFullscreen=0\n" + extra).getBytes(StandardCharsets.UTF_16LE);
    }

    private static void legacy(Path storage, int run, byte[] data) throws Exception {
        Path file = storage.resolve("run-" + run + "/overlay/overlay/kaiko/koa/personal.ini");
        Files.createDirectories(file.getParent());
        Files.write(file, data);
    }

    public static void main(String[] args) throws Exception {
        File storage = new File(args[0]);
        Path target = storage.toPath().resolve("veil-personal.ini");
        byte[] custom = personal("camera_fov_vertical(Min:10,Max:80,Default:42)=59\n");
        if (args[1].equals("upgrade")) {
            legacy(storage.toPath(), 17, personal("Display Width=1260\nDisplay Height=840\n"
                    + "camera_fov_vertical(Min:10,Max:80,Default:42)=59\n"));
            legacy(storage.toPath(), 18, personal(""));
            PersonalSettings.prepare(storage);
            check(Arrays.equals(Files.readAllBytes(target), custom), "migrate camera setting");
        } else if (args[1].equals("relaunch")) {
            String height = "camera_height_offset_cm(Min:0,Max:150,Default:0)=75\n";
            Files.write(target, personal(height + "Display Height=840\n"));
            byte[] edited = personal(height);
            legacy(storage.toPath(), 19, personal(""));
            PersonalSettings.prepare(storage);
            check(Arrays.equals(Files.readAllBytes(target), edited), "preserve later edit");
        } else {
            PersonalSettings.prepare(storage);
            check(Arrays.equals(Files.readAllBytes(target), personal("")), "new profile");
        }

        LockTouch lock = new LockTouch();
        lock.press(PICK_LEFT, 8, 100);
        lock.press(BOLT_RIGHT, 5, 103);
        lock.sample(LockTouch.GAME, 111);
        check(lock.pickAxis == -32767 && lock.boltAxis == 32767 && lock.buttons == 0, "both thumbs");
        lock.release(5);
        lock.sample(LockTouch.GAME, 112);
        check(lock.pickAxis == -32767 && lock.boltAxis == 0, "release pressure independently");
        lock.release(8);
        lock.press(PICK_RIGHT, 8, 130);
        lock.press(BOLT_LEFT, 12, 130);
        lock.sample(LockTouch.GAME, 131);
        check(lock.pickAxis == 32767 && lock.boltAxis == -32767, "opposite directions");
        lock.clear();
        lock.sample(LockTouch.GAME, 132);
        check((lock.boltAxis | lock.pickAxis | lock.buttons) == 0, "cancel");
        for (LockTouch.Action action : LockTouch.SPLASH) {
            lock.clear();
            lock.press(action, 9, 200);
            lock.release(9);
            lock.sample(LockTouch.SPLASH, 205);
            check(lock.buttons == action.button && lock.buttons != 0, "quick tap " + action);
            lock.sample(LockTouch.SPLASH, 272);
            check(lock.buttons == 0, "button released");
        }
    }
}
