#include "demo.h"

#include <stdint.h>

volatile WramHookReport g_wram_hook_report;

static uint16_t saved_thumb_entry;
static uint32_t saved_arm_entry;
static uint8_t entries_saved;

static void code_write_barrier(void)
{
    /* ARM7TDMI has no instruction cache; a compiler barrier is sufficient. */
    __asm__ volatile("" : : : "memory");
}

/* These replacement implementations stay in ROM and run from the handler. */
uint32_t __attribute__((noinline))
hook_thumb_reimplementation(uint32_t value)
{
    return value + 0x40u;
}

uint32_t __attribute__((noinline))
hook_arm_reimplementation(uint32_t value)
{
    return value + 0x80u;
}

/* ROM-resident dispatch metadata. Add another descriptor for another entry. */
const ExceptionHookDescriptor g_exception_hook_table[] = {
    {
        &wram_thumb_target,
        &hook_thumb_reimplementation,
        UDF_HOOK_THUMB,
        2u,
        TRAP_KIND_HOOK_THUMB,
    },
    {
        &wram_arm_target,
        &hook_arm_reimplementation,
        UDF_HOOK_ARM,
        4u,
        TRAP_KIND_HOOK_ARM,
    },
};

const uint32_t g_exception_hook_count =
    (uint32_t)(sizeof(g_exception_hook_table) /
               sizeof(g_exception_hook_table[0]));

/*
 * Demonstrate exception-driven entry hooks on writable C code:
 *   Thumb: overwrite one 16-bit instruction with user UDF 0xDE42.
 *   ARM:   overwrite one 32-bit instruction with a user UDF opcode.
 * Undefined Instruction transfers control to the ROM handler, which recognizes
 * the address and replaces the whole call.
 */
uint32_t run_wram_hook_demo(void)
{
    const uint32_t input = 0x11u;
    const uint32_t thumb_target =
        (uint32_t)(uintptr_t)&wram_thumb_target & ~1u;
    const uint32_t arm_target =
        (uint32_t)(uintptr_t)&wram_arm_target & ~3u;
    volatile uint16_t *const thumb_entry =
        (volatile uint16_t *)(uintptr_t)thumb_target;
    volatile uint32_t *const arm_entry =
        (volatile uint32_t *)(uintptr_t)arm_target;
    uint32_t flags = 0u;

    if (entries_saved == 0u) {
        saved_thumb_entry = *thumb_entry;
        saved_arm_entry = *arm_entry;
        entries_saved = 1u;
    } else {
        *thumb_entry = saved_thumb_entry;
        *arm_entry = saved_arm_entry;
        code_write_barrier();
    }

    g_wram_hook_report.thumb_target = thumb_target;
    g_wram_hook_report.arm_target = arm_target;
    g_wram_hook_report.thumb_patch = UDF_HOOK_THUMB;
    g_wram_hook_report.arm_patch = UDF_HOOK_ARM;
    g_wram_hook_report.thumb_hits = 0u;
    g_wram_hook_report.thumb_trap_pc = 0u;
    g_wram_hook_report.thumb_caller_lr = 0u;
    g_wram_hook_report.arm_hits = 0u;
    g_wram_hook_report.arm_trap_pc = 0u;
    g_wram_hook_report.arm_caller_lr = 0u;
    g_wram_hook_report.thumb_before = wram_thumb_target(input);
    g_wram_hook_report.arm_before = wram_arm_target(input);

    *thumb_entry = UDF_HOOK_THUMB; /* Actual two-byte exception hook. */
    code_write_barrier();
    g_wram_hook_report.thumb_after = wram_thumb_target(input);
    if (g_wram_hook_report.thumb_before == 0x12u &&
        g_wram_hook_report.thumb_after == 0x51u &&
        g_wram_hook_report.thumb_hits == 1u &&
        g_wram_hook_report.thumb_trap_pc == thumb_target) {
        flags |= WRAM_HOOK_THUMB_OK;
    }

    *arm_entry = UDF_HOOK_ARM; /* Actual four-byte exception hook. */
    code_write_barrier();
    g_wram_hook_report.arm_after = wram_arm_target(input);
    if (g_wram_hook_report.arm_before == 0x13u &&
        g_wram_hook_report.arm_after == 0x91u &&
        g_wram_hook_report.arm_hits == 1u &&
        g_wram_hook_report.arm_trap_pc == arm_target) {
        flags |= WRAM_HOOK_ARM_OK;
    }

    g_wram_hook_report.flags = flags;
    return flags;
}
