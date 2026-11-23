package org.openamalur.originalengine;

final class TouchHotbar {
    private TouchHotbar() {}

    static int slot(float x, float y, float centerX, float bottom, float height, int hudMode) {
        // The original HUD scales its bottom-centered, 720-high canvas by half in Small.
        if (height <= 0 || hudMode == 2) return 0;
        float unit = height / (hudMode == 1 ? 1440f : 720f);
        float fromFirst = (x - centerX) / unit + 318f;
        int index = Math.round(fromFirst / 75f);
        float fromBottom = (bottom - y) / unit;
        if (index < 0 || index >= 10 || Math.abs(fromFirst - 75f * index) > 34f ||
                fromBottom < 0 || fromBottom > 64f) return 0;
        return index + 1;
    }
}
