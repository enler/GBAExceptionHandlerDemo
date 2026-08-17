#ifndef GBA_EXCEPTION_DEMO_H
#define GBA_EXCEPTION_DEMO_H

/*
 * GBATEK marks cond01111111xxxxxxxxxxxx1111xxxx as free for user code.
 * Pack a 16-bit operation number into bits 19..8 and 3..0.
 */
#define UDF_USER_BASE          0xE7F000F0
#define UDF_USER_MASK          0xFFF000F0
#define UDF_ENCODE(imm16) \
    (UDF_USER_BASE | (((imm16) & 0xFFF0) << 4) | ((imm16) & 0x000F))

#define UDF_DEBUG_BREAK        UDF_ENCODE(0x0001)
#define UDF_SYSCALL_ADD        UDF_ENCODE(0x0100)
#define UDF_HOOK_ARM           UDF_ENCODE(0x0200)

/* GBATEK lists Thumb 1101 1110 xxxxxxxx as free user/Undefined encodings. */
#define UDF_HOOK_THUMB         0xDE42

/* ARMv5 CLZ r0,r0. ARM7TDMI is ARMv4T, so this traps as undefined. */
#define ARMV5_CLZ_R0_R0        0xE16F0F10

#define TRAP_KIND_NONE         0
#define TRAP_KIND_DEBUG        1
#define TRAP_KIND_SYSCALL      2
#define TRAP_KIND_ARMV5_CLZ    3
#define TRAP_KIND_HOOK_THUMB   4
#define TRAP_KIND_HOOK_ARM     5
#define TRAP_KIND_UNHANDLED    0xFFFFFFFF

#define WRAM_HOOK_THUMB_OK     0x01
#define WRAM_HOOK_ARM_OK       0x02

#ifndef __ASSEMBLER__

#include <stdint.h>

/* Stack image made by rom_exception_entry before calling C. */
typedef struct TrapFrame {
    uint32_t user_sp;     /* Unbanked r13 captured with an ARM STM^ transfer. */
    uint32_t user_lr;     /* Unbanked r14: return address of a hooked call. */
    uint32_t r[13];       /* r0-r12; entry r12 is BIOS scratch, not caller r12. */
    uint32_t bios_lr;     /* return address inside the BIOS debug trampoline. */
} TrapFrame;

typedef struct TrapReport {
    uint32_t total_count;
    uint32_t debug_count;
    uint32_t syscall_count;
    uint32_t emulated_count;
    uint32_t unhandled_count;
    uint32_t last_kind;
    uint32_t last_opcode;
    uint32_t last_pc;
    uint32_t last_spsr;
    uint32_t last_cpsr;
    uint32_t last_r0;
    uint32_t last_r1;
    uint32_t last_r2;
    uint32_t last_r3;
    uint32_t last_r4;
    uint32_t last_r5;
    uint32_t last_r6;
    uint32_t last_r7;
    uint32_t last_r8;
    uint32_t last_r9;
    uint32_t last_r10;
    uint32_t last_r11;
    uint32_t last_r12;
    uint32_t last_user_sp;
    uint32_t last_user_lr;
    uint32_t last_exception_lr;
} TrapReport;

typedef uint32_t (*ExceptionHookFunction)(uint32_t value);

typedef struct ExceptionHookDescriptor {
    ExceptionHookFunction target;
    ExceptionHookFunction replacement;
    uint32_t opcode;
    uint32_t instruction_width;
    uint32_t trap_kind;
} ExceptionHookDescriptor;

typedef struct WramHookReport {
    uint32_t flags;
    uint32_t thumb_target;
    uint32_t thumb_patch;
    uint32_t thumb_before;
    uint32_t thumb_after;
    uint32_t thumb_hits;
    uint32_t thumb_trap_pc;
    uint32_t thumb_caller_lr;
    uint32_t arm_target;
    uint32_t arm_patch;
    uint32_t arm_before;
    uint32_t arm_after;
    uint32_t arm_hits;
    uint32_t arm_trap_pc;
    uint32_t arm_caller_lr;
} WramHookReport;

extern volatile TrapReport g_trap_report;
extern volatile WramHookReport g_wram_hook_report;
extern volatile uint32_t g_vblank_count;
extern const ExceptionHookDescriptor g_exception_hook_table[];
extern const uint32_t g_exception_hook_count;

void rom_exception_entry(void);
void rom_irq_handler(void);
void exception_dispatch(TrapFrame *frame);

uint32_t demo_debug_break(uint32_t marker);
uint32_t demo_syscall_add(uint32_t left, uint32_t right);
uint32_t demo_armv5_clz(uint32_t value);

uint32_t wram_thumb_target(uint32_t value);
uint32_t wram_arm_target(uint32_t value);
uint32_t hook_thumb_reimplementation(uint32_t value);
uint32_t hook_arm_reimplementation(uint32_t value);
uint32_t run_wram_hook_demo(void);

#endif /* !__ASSEMBLER__ */

#endif /* GBA_EXCEPTION_DEMO_H */
