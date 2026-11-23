package org.openamalur.originalengine;

final class TouchHotbarTest {
    static void expect(int expected, int actual) {
        if (actual != expected) throw new AssertionError("Expected slot " + expected + ", got " + actual);
    }

    public static void main(String[] args) {
        // Slot centers measured on the OPPO at 3:2, including the two outside
        // the former width-based hit area and the rightmost slot labelled 0.
        int[] centers = {164, 219, 275, 331, 386, 442, 497, 552, 608, 664};
        for (int i = 0; i < centers.length; i++)
            for (int delta : new int[] {-12, 0, 12})
                expect(i + 1, TouchHotbar.slot(centers[i] + delta, 508, 400, 533, 533, 0));
        expect(0, TouchHotbar.slot(192, 508, 400, 533, 533, 0)); // between slots
        expect(0, TouchHotbar.slot(164, 465, 400, 533, 533, 0)); // above the rail
        expect(0, TouchHotbar.slot(318, 0, 0, 0, 0, 0));
        int[] smallCenters = {282, 310, 338, 366, 393, 421, 449, 477, 504, 532};
        for (int i = 0; i < smallCenters.length; i++)
            expect(i + 1, TouchHotbar.slot(smallCenters[i], 520, 400, 533, 533, 1));
        expect(0, TouchHotbar.slot(164, 508, 400, 533, 533, 1)); // outside the smaller bar
        expect(0, TouchHotbar.slot(282, 520, 400, 533, 533, 2)); // hidden
        // A 16:9 canvas with space above and below the game viewport.
        expect(1, TouchHotbar.slot(322, 800, 640, 820, 720, 0));
        expect(10, TouchHotbar.slot(997, 800, 640, 820, 720, 0));
        expect(0, TouchHotbar.slot(997, 841, 640, 820, 720, 0));
    }
}
