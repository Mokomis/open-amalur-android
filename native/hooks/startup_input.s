.text
.global startup_input
startup_input:
    // x20 points at the lifted guest memory base. Preserve x0 (X86Ctx).
    ldr x9, [x20]
    movz w10, #0x3ae0
    movk w10, #0x1a0, lsl #16
    add x10, x9, w10, uxtw
    ldar w10, [x10]
    cbz w10, done
    add x10, x9, w10, uxtw
    add x10, x10, #1, lsl #12
    add x10, x10, #0x274
    // These are the engine's permanent keyboard and mouse devices.
    // Claim only unassigned devices for the local startup profile.
    ldar w11, [x10]
    cmp w11, #1
    b.ne mouse
    add x11, x10, #0x14
    ldarb w12, [x11]
    cmp w12, #255
    b.ne mouse
    stlrb wzr, [x11]
mouse:
    add x11, x10, #0x18
    ldar w12, [x11]
    cmp w12, #2
    b.ne controller
    add x11, x11, #0x14
    ldarb w12, [x11]
    cmp w12, #255
    b.ne controller
    stlrb wzr, [x11]
controller:
    // The original profile checks the first XInput device first when present.
    // Assign an enumerated controller in the same way as the startup input.
    add x11, x10, #0x30
    ldar w12, [x11]
    cmp w12, #3
    b.ne done
    add x11, x11, #0x14
    ldarb w12, [x11]
    cmp w12, #255
    b.ne done
    stlrb wzr, [x11]
done:
    // Tail-call the original profile acquisition routine. The caller retains LR.
    b acquisition
