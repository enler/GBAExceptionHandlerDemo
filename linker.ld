/*
 * Minimal GBA cartridge linker script.
 *
 * The exception handler remains in normal ROM. The retail BIOS debug hand-off
 * is fixed at 0x09FFC000, so a small hand-off stub is deliberately placed at
 * the end of a 32 MiB ROM. IWRAM is used only by the writable C
 * targets patched with Undefined opcodes and by normal mutable state.
 */
OUTPUT_FORMAT("elf32-littlearm", "elf32-bigarm", "elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_start)

MEMORY
{
    rom   (rx)  : ORIGIN = 0x08000000, LENGTH = 32M
    /* Keep the BIOS-owned 0x03007F00..0x03007FFF area out of allocations. */
    iwram (rwx) : ORIGIN = 0x03000000, LENGTH = 0x7F00
    ewram (rwx) : ORIGIN = 0x02000000, LENGTH = 256K
}

__text_start = ORIGIN(rom);
__iwram_start = ORIGIN(iwram);
__iwram_top = 0x03008000;
__sp_irq = 0x03007FA0;
__sp_usr = 0x03007F00;
__irq_flags = 0x03007FF8;
__eheap_end = ORIGIN(ewram) + LENGTH(ewram);
fake_heap_end = __eheap_end;

SECTIONS
{
    .crt0 ORIGIN(rom) :
    {
        KEEP(*(.crt0))
        . = ALIGN(4);
    } > rom = 0xff

    .text :
    {
        KEEP(*(SORT_NONE(.init)))
        *(.text .text.*)
        *(.glue_7t) *(.glue_7) *(.vfp11_veneer)
        . = ALIGN(4);
    } > rom = 0xff

    .rodata :
    {
        *(.rodata .rodata.*)
        *(.gnu.linkonce.r*)
        . = ALIGN(4);
    } > rom = 0xff

    .ARM.extab :
    {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > rom

    __exidx_start = .;
    .ARM.exidx :
    {
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    } > rom
    __exidx_end = .;

    .iwram ORIGIN(iwram) :
    {
        __iwram_start__ = ABSOLUTE(.);
        KEEP(*(.iwram.hook.thumb))
        KEEP(*(.iwram.hook.arm))
        *(.iwram.text .iwram.text.*)
        *(.iwram.data .iwram.data.*)
        *(.iwram .iwram.*)
        . = ALIGN(4);
        __iwram_end__ = ABSOLUTE(.);
    } > iwram AT> rom = 0xff
    __iwram_lma = LOADADDR(.iwram);

    .data ALIGN(4) :
    {
        __data_start__ = ABSOLUTE(.);
        *(.data .data.*)
        *(.gnu.linkonce.d*)
        . = ALIGN(4);
        __data_end__ = ABSOLUTE(.);
    } > iwram AT> rom = 0xff
    __data_lma = LOADADDR(.data);

    .bss ALIGN(4) (NOLOAD) :
    {
        __bss_start = ABSOLUTE(.);
        __bss_start__ = ABSOLUTE(.);
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end__ = ABSOLUTE(.);
    } > iwram

    .sbss ALIGN(4) (NOLOAD) :
    {
        __sbss_start__ = ABSOLUTE(.);
        *(.sbss .sbss.*)
        . = ALIGN(4);
        __sbss_end__ = ABSOLUTE(.);
        __end__ = ABSOLUTE(.);
    } > iwram

    /* The stock crt0 understands optional IWRAM overlays and EWRAM payloads. */
    __iwram_overlay_start = .;
    __iwram_overlay_end = .;
    __load_start_iwram0 = .;
    __load_stop_iwram0 = .;

    .ewram ORIGIN(ewram) :
    {
        __ewram_start = ABSOLUTE(.);
        *(.ewram .ewram.*)
        . = ALIGN(4);
        __ewram_end = ABSOLUTE(.);
    } > ewram AT> rom = 0xff
    __ewram_lma = LOADADDR(.ewram);

    /* Header byte 0x9C=A5 and byte 0xB4=00 select this BIOS target. */
    .debug_vector 0x09FFC000 :
    {
        __debug_vector_start = ABSOLUTE(.);
        KEEP(*(.debug_vector))
        . = ALIGN(4);
        __debug_vector_end = ABSOLUTE(.);
    } > rom = 0xff

    ASSERT(__iwram_end__ <= 0x03007F00,
           "IWRAM payload overlaps BIOS stacks")
    ASSERT(SIZEOF(.debug_vector) <= 0x4000,
           "Debug vector exceeds the final 16 KiB of ROM")
    ASSERT(__debug_vector_end <= 0x0A000000,
           "Debug vector exceeds the 32 MiB cartridge window")

    /DISCARD/ :
    {
        *(.comment)
        *(.note.GNU-stack)
        *(.eh_frame*)
    }

    .debug_info      0 : { *(.debug_info .gnu.linkonce.wi.*) }
    .debug_abbrev    0 : { *(.debug_abbrev) }
    .debug_aranges   0 : { *(.debug_aranges) }
    .debug_line      0 : { *(.debug_line .debug_line.*) }
    .debug_str       0 : { *(.debug_str) }
    .debug_loclists  0 : { *(.debug_loclists) }
    .debug_rnglists  0 : { *(.debug_rnglists) }
    .debug_frame     0 : { *(.debug_frame) }
    .ARM.attributes  0 : { KEEP(*(.ARM.attributes)) }
}
