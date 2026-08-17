#include "demo.h"

#include <stdint.h>

#define WRAM_THUMB_CODE \
    __attribute__((section(".iwram.hook.thumb"), noinline, used))

/* Ordinary C function copied to IWRAM so its first halfword is writable. */
uint32_t WRAM_THUMB_CODE wram_thumb_target(uint32_t value)
{
    return value + 1u;
}
