#ifndef GBA_EXCEPTION_DEMO_HW_H
#define GBA_EXCEPTION_DEMO_HW_H

#include <stdint.h>

#define REG16(address) (*(volatile uint16_t *)(uintptr_t)(address))
#define REG32(address) (*(volatile uint32_t *)(uintptr_t)(address))

#define REG_DISPCNT     REG16(0x04000000)
#define REG_DISPSTAT    REG16(0x04000004)
#define REG_VCOUNT      REG16(0x04000006)
#define REG_KEYINPUT    REG16(0x04000130)
#define REG_IE          REG16(0x04000200)
#define REG_IF          REG16(0x04000202)
#define REG_IME         REG16(0x04000208)

#define BIOS_IRQ_FLAGS  REG16(0x03007FF8)
#define BIOS_IRQ_VECTOR REG32(0x03007FFC)

#define MODE3_VRAM      ((volatile uint16_t *)(uintptr_t)0x06000000)

#define DCNT_MODE3      0x0003
#define DCNT_BG2        0x0400
#define DSTAT_VBL_IRQ   0x0008
#define IRQ_VBLANK      0x0001

#define KEY_A           0x0001
#define KEY_START       0x0008

#define RGB5(red, green, blue) \
    ((uint16_t)(((red) & 31) | (((green) & 31) << 5) | (((blue) & 31) << 10)))

#endif /* GBA_EXCEPTION_DEMO_HW_H */
