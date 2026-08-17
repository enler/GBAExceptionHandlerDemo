#include "demo.h"

#include <stddef.h>
#include <stdint.h>

#define FORCE_INLINE __attribute__((always_inline)) inline

/* Layout written by the retail BIOS debug trampoline. */
#define BIOS_SAVED_SPSR (*(volatile uint32_t *)(uintptr_t)0x03007FE0)
#define BIOS_SAVED_CPSR (*(volatile uint32_t *)(uintptr_t)0x03007FE4)
#define BIOS_SAVED_R12  (*(volatile uint32_t *)(uintptr_t)0x03007FE8)
#define BIOS_SAVED_LR   (*(volatile uint32_t *)(uintptr_t)0x03007FEC)
#define PSR_THUMB_STATE  (1u << 5)

volatile TrapReport g_trap_report;
volatile uint32_t g_vblank_count;

_Static_assert(offsetof(TrapFrame, user_sp) == 0u, "TrapFrame user_sp offset");
_Static_assert(offsetof(TrapFrame, user_lr) == 4u, "TrapFrame user_lr offset");
_Static_assert(offsetof(TrapFrame, r) == 8u, "TrapFrame register offset");
_Static_assert(sizeof(TrapFrame) == 64u, "TrapFrame assembly layout");

static FORCE_INLINE uint32_t decode_udf_immediate(uint32_t opcode)
{
    return ((opcode >> 4) & 0xFFF0u) | (opcode & 0xFu);
}

static FORCE_INLINE int is_user_udf(uint32_t opcode)
{
    return (opcode & UDF_USER_MASK) == UDF_USER_BASE;
}

static FORCE_INLINE uint32_t frame_read_reg(const TrapFrame *frame,
                                             uint32_t reg)
{
    if (reg <= 11u) {
        return frame->r[reg];
    }
    if (reg == 12u) {
        return BIOS_SAVED_R12;
    }
    return 0u;
}

static FORCE_INLINE void frame_write_reg(TrapFrame *frame, uint32_t reg,
                                          uint32_t value)
{
    if (reg <= 11u) {
        frame->r[reg] = value;
    } else if (reg == 12u) {
        BIOS_SAVED_R12 = value;
    }
}

static FORCE_INLINE uint32_t software_clz(uint32_t value)
{
    uint32_t result = 0u;
    if (value == 0u) {
        return 32u;
    }
    while ((value & 0x80000000u) == 0u) {
        value <<= 1;
        ++result;
    }
    return result;
}

static const ExceptionHookDescriptor *find_exception_hook(uint32_t fault_pc,
                                                           uint32_t width,
                                                           uint32_t opcode)
{
    for (uint32_t index = 0u; index < g_exception_hook_count; ++index) {
        const ExceptionHookDescriptor *const descriptor =
            &g_exception_hook_table[index];
        const uint32_t alignment_mask =
            descriptor->instruction_width == 2u ? ~1u : ~3u;
        const uint32_t target_pc =
            (uint32_t)(uintptr_t)descriptor->target & alignment_mask;

        if (descriptor->instruction_width == width &&
            descriptor->opcode == opcode && target_pc == fault_pc) {
            return descriptor;
        }
    }
    return NULL;
}

static __attribute__((noreturn, noinline)) void stop_unhandled_exception(void)
{
    /* Leave g_trap_report intact for an emulator/debugger or memory viewer. */
    for (;;) {
        __asm__ volatile("nop" : : : "memory");
    }
}

/*
 * Replace the hooked function's BX LR. The BIOS later executes
 * SUBS pc,lr,#4, so store caller_pc+4 and make SPSR.T match caller LR bit 0.
 */
static FORCE_INLINE void complete_hook_call(TrapFrame *frame,
                                             uint32_t result)
{
    const uint32_t caller_lr = frame->user_lr;
    uint32_t return_spsr = BIOS_SAVED_SPSR & ~PSR_THUMB_STATE;

    return_spsr |= (caller_lr & 1u) << 5;
    frame->r[0] = result;
    BIOS_SAVED_SPSR = return_spsr;
    BIOS_SAVED_LR = (caller_lr & ~1u) + 4u;
}

/*
 * This is called in UND mode from rom_exception_entry. The BIOS itself will
 * eventually execute SUBS pc,lr,#4. For ordinary traps, adding four to the
 * saved exception LR makes that BIOS return resume at the next instruction in
 * either state. Function hooks instead replace the return target with user LR.
 */
void exception_dispatch(TrapFrame *frame)
{
    const uint32_t saved_spsr = BIOS_SAVED_SPSR;
    const uint32_t exception_lr = BIOS_SAVED_LR;
    const uint32_t instruction_width =
        (saved_spsr & PSR_THUMB_STATE) != 0u ? 2u : 4u;
    const uint32_t fault_pc = exception_lr - instruction_width;
    const uint32_t opcode = (instruction_width == 4u)
                                ? *(const volatile uint32_t *)(uintptr_t)fault_pc
                                : (uint32_t)*(const volatile uint16_t *)(uintptr_t)fault_pc;
    const uint32_t immediate = is_user_udf(opcode)
                                   ? decode_udf_immediate(opcode)
                                   : 0xFFFFFFFFu;
    const ExceptionHookDescriptor *const hook =
        find_exception_hook(fault_pc, instruction_width, opcode);
    uint32_t kind = TRAP_KIND_UNHANDLED;
    uint32_t hook_completed = 0u;

    ++g_trap_report.total_count;
    g_trap_report.last_opcode = opcode;
    g_trap_report.last_pc = fault_pc;
    g_trap_report.last_spsr = saved_spsr;
    g_trap_report.last_cpsr = BIOS_SAVED_CPSR;
    g_trap_report.last_r0 = frame->r[0];
    g_trap_report.last_r1 = frame->r[1];
    g_trap_report.last_r2 = frame->r[2];
    g_trap_report.last_r3 = frame->r[3];
    g_trap_report.last_r4 = frame->r[4];
    g_trap_report.last_r5 = frame->r[5];
    g_trap_report.last_r6 = frame->r[6];
    g_trap_report.last_r7 = frame->r[7];
    g_trap_report.last_r8 = frame->r[8];
    g_trap_report.last_r9 = frame->r[9];
    g_trap_report.last_r10 = frame->r[10];
    g_trap_report.last_r11 = frame->r[11];
    g_trap_report.last_r12 = BIOS_SAVED_R12;
    g_trap_report.last_user_sp = frame->user_sp;
    g_trap_report.last_user_lr = frame->user_lr;
    g_trap_report.last_exception_lr = exception_lr;

    if (is_user_udf(opcode) && immediate == 0x0001u) {
        kind = TRAP_KIND_DEBUG;
        ++g_trap_report.debug_count;
    } else if (is_user_udf(opcode) && immediate == 0x0100u) {
        frame->r[0] = frame->r[0] + frame->r[1];
        kind = TRAP_KIND_SYSCALL;
        ++g_trap_report.syscall_count;
    } else if (hook != NULL) {
        const uint32_t result = hook->replacement(frame->r[0]);

        if (hook->trap_kind == TRAP_KIND_HOOK_THUMB) {
            ++g_wram_hook_report.thumb_hits;
            g_wram_hook_report.thumb_trap_pc = fault_pc;
            g_wram_hook_report.thumb_caller_lr = frame->user_lr;
        } else if (hook->trap_kind == TRAP_KIND_HOOK_ARM) {
            ++g_wram_hook_report.arm_hits;
            g_wram_hook_report.arm_trap_pc = fault_pc;
            g_wram_hook_report.arm_caller_lr = frame->user_lr;
        }

        complete_hook_call(frame, result);
        hook_completed = 1u;
        kind = hook->trap_kind;
    } else if ((opcode & 0x0FFF0FF0u) == 0x016F0F10u) {
        const uint32_t destination = (opcode >> 12) & 0xFu;
        const uint32_t source = opcode & 0xFu;
        if (destination <= 12u && source <= 12u) {
            frame_write_reg(frame, destination,
                            software_clz(frame_read_reg(frame, source)));
            kind = TRAP_KIND_ARMV5_CLZ;
            ++g_trap_report.emulated_count;
        }
    }

    if (kind == TRAP_KIND_UNHANDLED) {
        ++g_trap_report.unhandled_count;
        g_trap_report.last_kind = kind;
        stop_unhandled_exception();
    }
    g_trap_report.last_kind = kind;

    if (hook_completed == 0u) {
        /* Skip one trapped opcode when BIOS resumes the interrupted program. */
        BIOS_SAVED_LR = exception_lr + 4u;
    }
}
