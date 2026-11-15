.text
.macro HOOK label, instruction:vararg
.globl veil_stock_\label
.hidden veil_stock_\label
.p2align 2
veil_stock_\label:
    \instruction
    .inst 0x14000000
.endm
HOOK component, stp x29, x30, [sp, #-0x30]!
HOOK portrait_model, stp x29, x30, [sp, #-0x20]!
HOOK portrait_lookup, stp x29, x30, [sp, #-0x30]!
HOOK read_save, stp x29, x30, [sp, #-0x30]!
HOOK write_save, mov w8, #0x4000000
HOOK skin, sub sp, sp, #0xd0
