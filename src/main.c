#include "demo.h"
#include "gba_hw.h"

#include <stdint.h>

enum {
    SCREEN_WIDTH = 240,
    SCREEN_HEIGHT = 160,
};

static const uint16_t COLOR_BG = RGB5(2, 3, 7);
static const uint16_t COLOR_PANEL = RGB5(4, 7, 13);
static const uint16_t COLOR_PANEL_ALT = RGB5(5, 9, 16);
static const uint16_t COLOR_TITLE = RGB5(31, 25, 7);
static const uint16_t COLOR_TEXT = RGB5(25, 27, 31);
static const uint16_t COLOR_DIM = RGB5(14, 18, 23);
static const uint16_t COLOR_PASS = RGB5(8, 31, 14);
static const uint16_t COLOR_FAIL = RGB5(31, 8, 8);
static const uint16_t COLOR_ACCENT = RGB5(7, 22, 31);

typedef struct DemoResults {
    uint32_t debug_pc;
    uint32_t debug_lr;
    uint32_t syscall_value;
    uint32_t clz_value;
    uint32_t thumb_patch;
    uint32_t arm_patch;
    uint8_t debug_pass;
    uint8_t syscall_pass;
    uint8_t clz_pass;
    uint8_t thumb_hook_pass;
    uint8_t arm_hook_pass;
    uint8_t hook_pass;
} DemoResults;

static DemoResults g_results;

/* 5x7 rows for 0-9 followed by A-Z. */
static const uint8_t font36[36][7] = {
    {14, 17, 19, 21, 25, 17, 14}, /* 0 */
    {4, 12, 4, 4, 4, 4, 14},      /* 1 */
    {14, 17, 1, 2, 4, 8, 31},     /* 2 */
    {30, 1, 1, 14, 1, 1, 30},     /* 3 */
    {2, 6, 10, 18, 31, 2, 2},     /* 4 */
    {31, 16, 16, 30, 1, 1, 30},   /* 5 */
    {6, 8, 16, 30, 17, 17, 14},   /* 6 */
    {31, 1, 2, 4, 8, 8, 8},       /* 7 */
    {14, 17, 17, 14, 17, 17, 14}, /* 8 */
    {14, 17, 17, 15, 1, 2, 12},   /* 9 */
    {14, 17, 17, 31, 17, 17, 17}, /* A */
    {30, 17, 17, 30, 17, 17, 30}, /* B */
    {14, 17, 16, 16, 16, 17, 14}, /* C */
    {28, 18, 17, 17, 17, 18, 28}, /* D */
    {31, 16, 16, 30, 16, 16, 31}, /* E */
    {31, 16, 16, 30, 16, 16, 16}, /* F */
    {14, 17, 16, 23, 17, 17, 15}, /* G */
    {17, 17, 17, 31, 17, 17, 17}, /* H */
    {14, 4, 4, 4, 4, 4, 14},      /* I */
    {7, 2, 2, 2, 18, 18, 12},     /* J */
    {17, 18, 20, 24, 20, 18, 17}, /* K */
    {16, 16, 16, 16, 16, 16, 31}, /* L */
    {17, 27, 21, 21, 17, 17, 17}, /* M */
    {17, 25, 21, 19, 17, 17, 17}, /* N */
    {14, 17, 17, 17, 17, 17, 14}, /* O */
    {30, 17, 17, 30, 16, 16, 16}, /* P */
    {14, 17, 17, 17, 21, 18, 13}, /* Q */
    {30, 17, 17, 30, 20, 18, 17}, /* R */
    {15, 16, 16, 14, 1, 1, 30},   /* S */
    {31, 4, 4, 4, 4, 4, 4},       /* T */
    {17, 17, 17, 17, 17, 17, 14}, /* U */
    {17, 17, 17, 17, 17, 10, 4},  /* V */
    {17, 17, 17, 21, 21, 21, 10}, /* W */
    {17, 17, 10, 4, 10, 17, 17},  /* X */
    {17, 17, 10, 4, 4, 4, 4},     /* Y */
    {31, 1, 2, 4, 8, 16, 31},     /* Z */
};

static const uint8_t glyph_question[7] = {14, 17, 1, 2, 4, 0, 4};
static const uint8_t glyph_colon[7] = {0, 4, 4, 0, 4, 4, 0};
static const uint8_t glyph_plus[7] = {0, 4, 4, 31, 4, 4, 0};
static const uint8_t glyph_equal[7] = {0, 0, 31, 0, 31, 0, 0};
static const uint8_t glyph_dash[7] = {0, 0, 0, 31, 0, 0, 0};
static const uint8_t glyph_lbracket[7] = {14, 8, 8, 8, 8, 8, 14};
static const uint8_t glyph_rbracket[7] = {14, 2, 2, 2, 2, 2, 14};
static const uint8_t glyph_lparen[7] = {2, 4, 8, 8, 8, 4, 2};
static const uint8_t glyph_rparen[7] = {8, 4, 2, 2, 2, 4, 8};
static const uint8_t glyph_slash[7] = {1, 1, 2, 4, 8, 16, 16};
static const uint8_t glyph_dot[7] = {0, 0, 0, 0, 0, 12, 12};
static const uint8_t glyph_at[7] = {14, 17, 23, 21, 23, 16, 14};
static const uint8_t glyph_blank[7] = {0, 0, 0, 0, 0, 0, 0};

static const uint8_t *glyph_for(char character)
{
    if (character >= '0' && character <= '9') {
        return font36[(uint32_t)(character - '0')];
    }
    if (character >= 'A' && character <= 'Z') {
        return font36[10u + (uint32_t)(character - 'A')];
    }
    switch (character) {
    case ' ': return glyph_blank;
    case ':': return glyph_colon;
    case '+': return glyph_plus;
    case '=': return glyph_equal;
    case '-': return glyph_dash;
    case '[': return glyph_lbracket;
    case ']': return glyph_rbracket;
    case '(': return glyph_lparen;
    case ')': return glyph_rparen;
    case '/': return glyph_slash;
    case '.': return glyph_dot;
    case '@': return glyph_at;
    default: return glyph_question;
    }
}

static void put_pixel(uint32_t x, uint32_t y, uint16_t color)
{
    if (x < SCREEN_WIDTH && y < SCREEN_HEIGHT) {
        MODE3_VRAM[y * SCREEN_WIDTH + x] = color;
    }
}

static void fill_rect(uint32_t x, uint32_t y, uint32_t width,
                      uint32_t height, uint16_t color)
{
    for (uint32_t row = 0; row < height && y + row < SCREEN_HEIGHT; ++row) {
        for (uint32_t column = 0;
             column < width && x + column < SCREEN_WIDTH; ++column) {
            MODE3_VRAM[(y + row) * SCREEN_WIDTH + x + column] = color;
        }
    }
}

static void draw_char(uint32_t x, uint32_t y, char character,
                      uint16_t color, uint32_t scale)
{
    const uint8_t *rows = glyph_for(character);
    for (uint32_t row = 0; row < 7u; ++row) {
        for (uint32_t column = 0; column < 5u; ++column) {
            if ((rows[row] & (uint8_t)(1u << (4u - column))) != 0u) {
                for (uint32_t dy = 0; dy < scale; ++dy) {
                    for (uint32_t dx = 0; dx < scale; ++dx) {
                        put_pixel(x + column * scale + dx,
                                  y + row * scale + dy, color);
                    }
                }
            }
        }
    }
}

static uint32_t draw_text(uint32_t x, uint32_t y, const char *text,
                          uint16_t color, uint32_t scale)
{
    while (*text != '\0') {
        draw_char(x, y, *text, color, scale);
        x += 6u * scale;
        ++text;
    }
    return x;
}

static uint32_t draw_hex32(uint32_t x, uint32_t y, uint32_t value,
                           uint16_t color)
{
    static const char hex[] = "0123456789ABCDEF";
    for (uint32_t shift = 28u;; shift -= 4u) {
        draw_char(x, y, hex[(value >> shift) & 0xFu], color, 1u);
        x += 6u;
        if (shift == 0u) {
            break;
        }
    }
    return x;
}

static void draw_pass_fail(uint32_t y, uint8_t passed)
{
    const uint16_t color = passed != 0u ? COLOR_PASS : COLOR_FAIL;
    draw_text(198u, y, passed != 0u ? "PASS" : "FAIL", color, 1u);
}

static void wait_for_vblank_edge(void)
{
    while (REG_VCOUNT >= 160u) {
        /* Wait until visible scanlines resume. */
    }
    while (REG_VCOUNT < 160u) {
        /* Wait for the next VBlank IRQ edge. */
    }
}

static void configure_vblank_irq(void)
{
    REG_IME = 0u;
    REG_IE = 0u;
    REG_IF = 0x3FFFu;
    BIOS_IRQ_FLAGS = 0u;
    g_vblank_count = 0u;
    BIOS_IRQ_VECTOR = (uint32_t)(uintptr_t)&rom_irq_handler;
    REG_DISPSTAT = (uint16_t)(REG_DISPSTAT | DSTAT_VBL_IRQ);
    REG_IE = IRQ_VBLANK;
    REG_IME = 1u;
}

static void run_exception_tests(void)
{
    const uint32_t debug_marker = 0xC0DEF00Du;
    const uint32_t debug_gate =
        (uint32_t)(uintptr_t)&demo_debug_break & ~3u;
    const uint32_t syscall_gate =
        (uint32_t)(uintptr_t)&demo_syscall_add & ~3u;
    const uint32_t clz_gate =
        (uint32_t)(uintptr_t)&demo_armv5_clz & ~3u;

    const uint32_t debug_output = demo_debug_break(debug_marker);
    g_results.debug_pc = g_trap_report.last_pc;
    g_results.debug_lr = g_trap_report.last_user_lr;
    g_results.debug_pass =
        (uint8_t)(debug_output == debug_marker &&
                  g_trap_report.last_kind == TRAP_KIND_DEBUG &&
                  g_trap_report.last_opcode == UDF_DEBUG_BREAK &&
                  g_trap_report.last_pc == debug_gate &&
                  g_trap_report.last_r0 == debug_marker &&
                  g_trap_report.last_user_sp >= 0x03000000u &&
                  g_trap_report.last_user_sp < 0x03007F00u &&
                  (g_trap_report.last_user_lr & ~1u) >= 0x08000000u &&
                  (g_trap_report.last_user_lr & ~1u) < 0x0A000000u);

    g_results.syscall_value = demo_syscall_add(0x13u, 0x17u);
    g_results.syscall_pass =
        (uint8_t)(g_results.syscall_value == 0x2Au &&
                  g_trap_report.last_kind == TRAP_KIND_SYSCALL &&
                  g_trap_report.last_opcode == UDF_SYSCALL_ADD &&
                  g_trap_report.last_pc == syscall_gate &&
                  g_trap_report.last_r0 == 0x13u &&
                  g_trap_report.last_r1 == 0x17u);

    g_results.clz_value = demo_armv5_clz(0x00F00000u);
    g_results.clz_pass =
        (uint8_t)(g_results.clz_value == 8u &&
                  g_trap_report.last_kind == TRAP_KIND_ARMV5_CLZ &&
                  g_trap_report.last_opcode == ARMV5_CLZ_R0_R0 &&
                  g_trap_report.last_pc == clz_gate &&
                  g_trap_report.last_r0 == 0x00F00000u);

    const uint32_t hook_flags = run_wram_hook_demo();
    g_results.thumb_patch = g_wram_hook_report.thumb_patch;
    g_results.arm_patch = g_wram_hook_report.arm_patch;
    g_results.thumb_hook_pass =
        (uint8_t)((hook_flags & WRAM_HOOK_THUMB_OK) != 0u);
    g_results.arm_hook_pass =
        (uint8_t)((hook_flags & WRAM_HOOK_ARM_OK) != 0u);
    g_results.hook_pass =
        (uint8_t)(g_results.thumb_hook_pass != 0u &&
                  g_results.arm_hook_pass != 0u &&
                  g_results.thumb_patch == UDF_HOOK_THUMB &&
                  g_results.arm_patch == UDF_HOOK_ARM &&
                  g_wram_hook_report.thumb_caller_lr != 0u &&
                  g_wram_hook_report.arm_caller_lr != 0u &&
                  g_wram_hook_report.thumb_target >= 0x03000000u &&
                  g_wram_hook_report.thumb_target < 0x03007F00u &&
                  g_wram_hook_report.arm_target >= 0x03000000u &&
                  g_wram_hook_report.arm_target < 0x03007F00u);
}

static void draw_dashboard(void)
{
    fill_rect(0u, 0u, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BG);
    fill_rect(0u, 0u, SCREEN_WIDTH, 23u, COLOR_PANEL_ALT);
    fill_rect(0u, 22u, SCREEN_WIDTH, 1u, COLOR_ACCENT);

    draw_text(18u, 4u, "GBA EXCEPTION LAB", COLOR_TITLE, 2u);
    draw_text(52u, 25u, "ROM HANDLER + WRAM UDF", COLOR_DIM, 1u);

    fill_rect(5u, 36u, 230u, 25u, COLOR_PANEL);
    draw_text(10u, 39u, "1 DEBUG SNAPSHOT", COLOR_TEXT, 1u);
    draw_pass_fail(39u, g_results.debug_pass);
    uint32_t x = draw_text(10u, 50u, "PC ", COLOR_DIM, 1u);
    x = draw_hex32(x, 50u, g_results.debug_pc, COLOR_ACCENT);
    x = draw_text(x, 50u, " LR ", COLOR_DIM, 1u);
    (void)draw_hex32(x, 50u, g_results.debug_lr, COLOR_ACCENT);

    fill_rect(5u, 63u, 230u, 25u, COLOR_PANEL_ALT);
    draw_text(10u, 66u, "2 PSEUDO SYSCALL", COLOR_TEXT, 1u);
    draw_pass_fail(66u, g_results.syscall_pass);
    x = draw_text(10u, 77u, "00000013 + 00000017 = ", COLOR_DIM, 1u);
    (void)draw_hex32(x, 77u, g_results.syscall_value, COLOR_ACCENT);

    fill_rect(5u, 90u, 230u, 25u, COLOR_PANEL);
    draw_text(10u, 93u, "3 ARMV5 CLZ EMULATION", COLOR_TEXT, 1u);
    draw_pass_fail(93u, g_results.clz_pass);
    x = draw_text(10u, 104u, "CLZ(00F00000) = ", COLOR_DIM, 1u);
    (void)draw_hex32(x, 104u, g_results.clz_value, COLOR_ACCENT);

    fill_rect(5u, 117u, 230u, 25u, COLOR_PANEL_ALT);
    draw_text(10u, 120u, "4 EXCEPTION C HOOK", COLOR_TEXT, 1u);
    draw_pass_fail(120u, g_results.hook_pass);
    x = draw_text(10u, 131u, "THUMB 2B ", COLOR_DIM, 1u);
    x = draw_text(x, 131u,
                  g_results.thumb_hook_pass != 0u ? "PASS" : "FAIL",
                  g_results.thumb_hook_pass != 0u ? COLOR_PASS : COLOR_FAIL,
                  1u);
    x = draw_text(x, 131u, "  ARM 4B ", COLOR_DIM, 1u);
    (void)draw_text(x, 131u,
                    g_results.arm_hook_pass != 0u ? "PASS" : "FAIL",
                    g_results.arm_hook_pass != 0u ? COLOR_PASS : COLOR_FAIL,
                    1u);

    draw_text(66u, 150u, "A: RUN AGAIN", COLOR_TITLE, 1u);
}

#ifdef DEMO_RUNTIME_SMOKE
/*
 * Test-only image: mGBA-headless can stop on SWI 0x7F and use r0 as its
 * process exit status. The production ROM never compiles this path.
 */
static void runtime_smoke_exit(void)
{
    uint32_t result = 0u;
    result |= g_results.debug_pass != 0u ? 1u : 0u;
    result |= g_results.syscall_pass != 0u ? 2u : 0u;
    result |= g_results.clz_pass != 0u ? 4u : 0u;
    result |= g_results.hook_pass != 0u ? 8u : 0u;

    register uint32_t r0 __asm__("r0") = result;
    __asm__ volatile("swi 0x7f" : "+r"(r0) : : "memory");
    for (;;) {
    }
}
#endif

/* The project is freestanding; the stock devkitARM crt0 still calls this. */
void __libc_init_array(void)
{
}

int main(void)
{
    REG_DISPCNT = (uint16_t)(DCNT_MODE3 | DCNT_BG2);
    configure_vblank_irq();
    run_exception_tests();
#ifdef DEMO_RUNTIME_SMOKE
    /* A second pass also verifies that hook entries restore and repatch. */
    run_exception_tests();
    runtime_smoke_exit();
#endif
    draw_dashboard();

    uint16_t previous_keys = 0u;
    for (;;) {
        wait_for_vblank_edge();
        const uint16_t keys = (uint16_t)((~REG_KEYINPUT) & 0x03FFu);
        const uint16_t pressed = (uint16_t)(keys & (uint16_t)~previous_keys);
        previous_keys = keys;
        if ((pressed & KEY_A) != 0u) {
            run_exception_tests();
            draw_dashboard();
        }
    }
}
