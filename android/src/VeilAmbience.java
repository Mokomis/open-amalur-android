package org.openamalur.originalengine;

import android.content.Context;
import android.content.res.AssetFileDescriptor;
import android.media.AudioAttributes;
import android.media.SoundPool;
import android.os.SystemClock;
import android.util.Log;

import java.io.IOException;
import java.util.Random;

final class VeilAmbience {
    private static final String TAG = "VeilAmbience";
    private static final String[] CUES = {"night-crickets", "night-owl", "night-treefrog"};
    private static native int volume();

    private final Context context;
    private final Random random = new Random();
    private final int[] samples = new int[CUES.length];
    private final boolean[] loaded = new boolean[CUES.length];
    private SoundPool pool;
    private long nextCheck, nextCue;
    private int stream;
    private int previous = -1;

    VeilAmbience(Context context) {
        this.context = context;
    }

    private void load() {
        AudioAttributes attributes = new AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_GAME)
                .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                .build();
        pool = new SoundPool.Builder().setMaxStreams(1).setAudioAttributes(attributes).build();
        pool.setOnLoadCompleteListener((source, id, status) -> {
            for (int i = 0; i < samples.length; i++) {
                if (samples[i] == id) loaded[i] = status == 0;
            }
            if (status != 0) Log.w(TAG, "Could not decode ambience sample " + id);
        });
        for (int i = 0; i < samples.length; i++) {
            try (AssetFileDescriptor asset = context.getAssets().openFd("ambience/" + CUES[i] + ".m4a")) {
                samples[i] = pool.load(asset, 1);
            } catch (IOException error) {
                Log.w(TAG, "Could not open " + CUES[i], error);
            }
        }
    }

    void update() {
        long now = SystemClock.uptimeMillis();
        if (now < nextCheck) return;
        nextCheck = now + 200;
        int strength = volume();
        if (strength == 0) {
            stop();
            return;
        }
        if (pool == null) load();
        if (nextCue == 0) nextCue = now + 2500 + random.nextInt(5000);
        if (now < nextCue) return;

        int start = random.nextInt(samples.length);
        for (int offset = 0; offset < samples.length; offset++) {
            int index = (start + offset) % samples.length;
            if (!loaded[index] || index == previous) continue;
            float gain = strength * .0025f;
            float pan = random.nextFloat() * .44f - .22f;
            stream = pool.play(samples[index], gain * (1 - Math.max(0f, pan)),
                    gain * (1 + Math.min(0f, pan)), 1, 0, .95f + random.nextFloat() * .10f);
            if (stream != 0) {
                previous = index;
                nextCue = now + 16000 + random.nextInt(28000);
                Log.i(TAG, "Playing " + CUES[index] + " at effects volume " + strength);
                return;
            }
        }
        previous = -1;
        nextCue = now + 1000;
    }

    void stop() {
        if (pool != null && stream != 0) pool.stop(stream);
        stream = 0;
        nextCue = 0;
    }

    void release() {
        stop();
        if (pool != null) {
            pool.release();
            pool = null;
        }
    }
}
