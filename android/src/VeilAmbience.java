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
    private static final int CUES_PER_PERIOD = 3;
    private static final String[] CUES = {
        "day-thrush", "day-flicker", "day-grasshopper",
        "night-crickets", "night-owl", "night-treefrog",
    };
    private static native int current();

    private final Context context;
    private final Random random = new Random();
    private final int[] samples = new int[CUES.length];
    private final boolean[] loaded = new boolean[CUES.length];
    private SoundPool pool;
    private long nextCheck, nextCue;
    private int stream;
    private int activePeriod;
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
        int sample = current();
        int period = sample >> 7;
        int strength = sample & 127;
        if (activePeriod != period) {
            stop();
            activePeriod = period;
        }
        if (period == 0) return;
        if (pool == null) load();
        if (nextCue == 0) nextCue = now + 2500 + random.nextInt(5000);
        if (now < nextCue) return;

        int first = (period - 1) * CUES_PER_PERIOD;
        int start = random.nextInt(CUES_PER_PERIOD);
        for (int offset = 0; offset < CUES_PER_PERIOD; offset++) {
            int index = first + (start + offset) % CUES_PER_PERIOD;
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
        activePeriod = 0;
    }

    void release() {
        stop();
        if (pool != null) {
            pool.release();
            pool = null;
        }
    }
}
