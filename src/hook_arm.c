#include "demo.h"

#include <stdint.h>

#define WRAM_ARM_CODE \
    __attribute__((section(".iwram.hook.arm"), noinline, used))

/* ARM-state C function copied to IWRAM so its first word is writable. */
uint32_t WRAM_ARM_CODE wram_arm_target(uint32_t value)
{
    return value + 2u;
}
