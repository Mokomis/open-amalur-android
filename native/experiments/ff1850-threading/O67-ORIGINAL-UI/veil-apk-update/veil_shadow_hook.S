.text
.globl veil_shadow_map_dimension
.hidden veil_shadow_map_dimension
.type veil_shadow_map_dimension, %function
veil_shadow_map_dimension:
    adrp x0, veil_shadow_map_size
    ldr w0, [x0, :lo12:veil_shadow_map_size]
    ret
.size veil_shadow_map_dimension, .-veil_shadow_map_dimension

.globl veil_vegetation_radius
.hidden veil_vegetation_radius
.type veil_vegetation_radius, %function
veil_vegetation_radius:
    // The lifted query uses the radius both directly and through w11's
    // XMM scratch spill. Keep the two reads consistent.
    str x0, [sp, #-32]!
    str q31, [sp, #16]
    adrp x0, veil_vegetation_scale
    ldr s31, [x0, :lo12:veil_vegetation_scale]
    fmov s2, w11
    fmul s2, s2, s31
    fmov w11, s2
    ldr q31, [sp, #16]
    ldr x0, [sp], #32
    ret
.size veil_vegetation_radius, .-veil_vegetation_radius

.globl veil_occlusion_gate
.hidden veil_occlusion_gate
.type veil_occlusion_gate, %function
veil_occlusion_gate:
    str x0, [sp, #-16]!
    adrp x0, veil_occlusion_enabled
    ldr w0, [x0, :lo12:veil_occlusion_enabled]
    ldarb w16, [x17]
    cbnz w0, 1f
    mov w16, wzr
1:
    ldr x0, [sp], #16
    ret
.size veil_occlusion_gate, .-veil_occlusion_gate

.globl veil_depth_of_field_radius
.hidden veil_depth_of_field_radius
.type veil_depth_of_field_radius, %function
veil_depth_of_field_radius:
    // Replace the loaded double before it splits into the x/y texel offsets.
    str x0, [sp, #-32]!
    str q31, [sp, #16]
    adrp x0, veil_depth_of_field_scale
    ldr d31, [x0, :lo12:veil_depth_of_field_scale]
    fmov d2, x15
    fmul d2, d2, d31
    fmov x15, d2
    ldr q31, [sp, #16]
    ldr x0, [sp], #32
    ret
.size veil_depth_of_field_radius, .-veil_depth_of_field_radius
