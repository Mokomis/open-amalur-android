#ifndef VEIL_TOUCH_RUN_PAD_H
#define VEIL_TOUCH_RUN_PAD_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

extern atomic_bool veil_run_filter_active;

enum { VEIL_TOUCH_RUN = 0x10000, VEIL_PAD_A = 0x1000, VEIL_PAD_LS = 0x40 };
typedef enum { VEIL_NO_CONTEXT, VEIL_LEAP_ONLY, VEIL_INTERACT } VeilContextRequest;

typedef struct {
    bool run, interact, face, run_owns_face;
} VeilRunSample;

/* Called once per original XInput sample, under the virtual pad lock. */
static inline VeilContextRequest veil_sample_run(VeilRunSample *sample, bool run, bool interact,
                                                  bool stick, bool finisher) {
    bool face = interact || finisher || (run && !stick);
    VeilContextRequest request = VEIL_NO_CONTEXT;
    if (interact && !sample->interact && sample->face && sample->run_owns_face)
        request = VEIL_INTERACT;  /* A is already held by the other finger. */
    else if (stick && run && !sample->run && !interact && !finisher)
        request = VEIL_LEAP_ONLY;  /* LS has no ordinary A-press interaction. */
    if (!face || !sample->face)
        sample->run_owns_face = face && run && !stick && !interact && !finisher;
    sample->face = face;
    sample->run = run;
    sample->interact = interact;
    return request;
}
#endif
