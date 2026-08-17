#include "demo.h"

    .syntax unified
    .cpu arm7tdmi

/*
 * The retail BIOS jumps here for Undefined/FIQ exceptions when header byte
 * 0x9C is A5 and byte 0xB4 selects the 8-Mbit DACS layout. The actual handler
 * remains in ordinary cartridge ROM; no writable memory is needed here.
 */
    .section .debug_vector, "ax", %progbits
    .arm
    .align 2
    .global cartridge_debug_vector
    .type cartridge_debug_vector, %function
cartridge_debug_vector:
    b       rom_exception_entry
    .size cartridge_debug_vector, . - cartridge_debug_vector

/*
 * BIOS entry state:
 *   mode = UND (for this demo), ARM state, IRQ disabled
 *   sp   = 0x03007FE0
 *   lr   = BIOS continuation
 *   [03007FE0] SPSR, [7FE4] CPSR, [7FE8] caller r12, [7FEC] exception LR
 */
    .section .text.exception_entry, "ax", %progbits
    .arm
    .align 2
    .global rom_exception_entry
    .type rom_exception_entry, %function
rom_exception_entry:
    /* 14 registers keep the public C call stack 8-byte aligned. */
    stmdb   sp!, {r0-r12, lr}

    /*
     * UND has banked SP/LR. Capture the interrupted code's unbanked SP/LR
     * before entering C; the hook path needs caller LR to replace the whole
     * target function and return exactly as its normal BX LR would.
     */
    sub     sp, sp, #8
    stmia   sp, {r13, r14}^
    mov     r0, sp
    bl      exception_dispatch
    add     sp, sp, #8
    ldmia   sp!, {r0-r12, lr}
    bx      lr
    .size rom_exception_entry, . - rom_exception_entry

/* Standard BIOS IRQ handler installed through 0x03007FFC. Must be ARM code. */
    .section .text.irq, "ax", %progbits
    .arm
    .align 2
    .global rom_irq_handler
    .type rom_irq_handler, %function
rom_irq_handler:
    ldr     r0, =0x04000202
    ldrh    r1, [r0]
    and     r1, r1, #1
    cmp     r1, #0
    bxeq    lr

    /* IF is write-one-to-clear. Mirror the bit into the BIOS IntrWait flags. */
    strh    r1, [r0]
    ldr     r0, =0x03007FF8
    ldrh    r2, [r0]
    orr     r2, r2, r1
    strh    r2, [r0]

    ldr     r0, =g_vblank_count
    ldr     r2, [r0]
    add     r2, r2, #1
    str     r2, [r0]
    bx      lr
    .ltorg
    .size rom_irq_handler, . - rom_irq_handler

/* ARM-state call gates used by the Thumb UI. */
    .section .text.arm_traps, "ax", %progbits
    .arm
    .align 2

    .global demo_debug_break
    .type demo_debug_break, %function
demo_debug_break:
    .word   UDF_DEBUG_BREAK
    bx      lr
    .size demo_debug_break, . - demo_debug_break

    .global demo_syscall_add
    .type demo_syscall_add, %function
demo_syscall_add:
    .word   UDF_SYSCALL_ADD
    bx      lr
    .size demo_syscall_add, . - demo_syscall_add

    .global demo_armv5_clz
    .type demo_armv5_clz, %function
demo_armv5_clz:
    .word   ARMV5_CLZ_R0_R0
    bx      lr
    .size demo_armv5_clz, . - demo_armv5_clz
