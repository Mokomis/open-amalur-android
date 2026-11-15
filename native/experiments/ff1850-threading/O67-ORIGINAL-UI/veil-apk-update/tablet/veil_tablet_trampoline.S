.text
.globl veil_stock_pad_get
.hidden veil_stock_pad_get
.p2align 2
veil_stock_pad_get:
    stp x29, x30, [sp, #-0x30]!
    .inst 0x14000000
