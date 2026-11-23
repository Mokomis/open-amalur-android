package org.openamalur.originalengine;

/** Touches held on the two lock screens, in the original game's controller vocabulary. */
final class LockTouch {
    enum Action {
        PICK_LEFT("◀  Pick", 0, -1, 0), PICK_RIGHT("Pick  ▶", 0, 1, 0),
        BOLT_LEFT("◀  Bolt", 0, 0, -1), BOLT_RIGHT("Bolt  ▶", 0, 0, 1),
        PICK("Pick lock", 0x1000, 0, 0), AUTO("Auto-attempt", 0x8000, 0, 0),
        BACK("Back", 0x2000, 0, 0), MELTER("Lockmelter", 0x4000, 0, 0);

        final String label;
        final int button, pick, bolt;

        Action(String label, int button, int pick, int bolt) {
            this.label = label;
            this.button = button;
            this.pick = pick;
            this.bolt = bolt;
        }
    }

    static final Action[] SPLASH = {Action.PICK, Action.AUTO, Action.BACK, Action.MELTER};
    static final Action[] GAME = {Action.PICK_LEFT, Action.PICK_RIGHT, Action.BOLT_LEFT,
                                 Action.BOLT_RIGHT, Action.BACK, Action.MELTER};
    private final int[] pointers = new int[Action.values().length];
    private final long[] minimumHold = new long[pointers.length];
    int buttons, pickAxis, boltAxis;

    LockTouch() { clear(); }

    void clear() {
        java.util.Arrays.fill(pointers, -1);
        java.util.Arrays.fill(minimumHold, 0);
        buttons = pickAxis = boltAxis = 0;
    }

    void press(Action action, int pointer, long now) {
        int i = action.ordinal();
        if (pointers[i] >= 0) return;
        pointers[i] = pointer;
        // Let game polling see quick taps. Releasing either stick immediately
        // matters for fine adjustment and avoiding broken picks.
        minimumHold[i] = action.button == 0 ? 0 : now + 72;
    }

    void release(int pointer) {
        for (int i = 0; i < pointers.length; i++)
            if (pointers[i] == pointer) pointers[i] = -1;
    }

    int pointer(Action action) { return pointers[action.ordinal()]; }

    boolean held(Action action, long now) {
        int i = action.ordinal();
        return pointers[i] >= 0 || now < minimumHold[i];
    }

    void sample(Action[] actions, long now) {
        buttons = pickAxis = boltAxis = 0;
        for (Action a : actions) {
            if (!held(a, now)) continue;
            buttons |= a.button;
            pickAxis += a.pick * 32767;
            boltAxis += a.bolt * 32767;
        }
    }
}
