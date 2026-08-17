#!/usr/bin/env python3
"""Static invariants for the exception-demo ROM and its ELF symbols."""

from __future__ import annotations

import os
import struct
import subprocess
import sys
from pathlib import Path

ROM_BASE = 0x08000000
ROM_SIZE = 32 * 1024 * 1024
DEBUG_VECTOR = 0x09FFC000

UDF_DEBUG = 0xE7F000F1
UDF_SYSCALL = 0xE7F010F0
ARMV5_CLZ = 0xE16F0F10
THUMB_HOOK_UDF = 0xDE42
ARM_HOOK_UDF = 0xE7F020F0


def fail(message: str) -> None:
    raise SystemExit(f"verify: FAIL: {message}")


def load_symbols(elf_path: Path) -> dict[str, int]:
    devkitarm = os.environ.get("DEVKITARM", "/opt/devkitpro/devkitARM")
    nm = Path(devkitarm) / "bin" / "arm-none-eabi-nm"
    output = subprocess.check_output([str(nm), "-n", str(elf_path)], text=True)
    symbols: dict[str, int] = {}
    for line in output.splitlines():
        fields = line.split()
        if len(fields) >= 3:
            try:
                symbols[fields[2]] = int(fields[0], 16)
            except ValueError:
                continue
    return symbols


def rom_word(data: bytes, address: int) -> int:
    offset = address - ROM_BASE
    if offset < 0 or offset + 4 > len(data):
        fail(f"address 0x{address:08X} is outside the ROM")
    return struct.unpack_from("<I", data, offset)[0]


def require_symbol(symbols: dict[str, int], name: str) -> int:
    if name not in symbols:
        fail(f"missing ELF symbol {name}")
    return symbols[name]


def main() -> None:
    if len(sys.argv) != 3:
        fail("usage: verify_rom.py ROM ELF")

    rom_path = Path(sys.argv[1])
    elf_path = Path(sys.argv[2])
    data = rom_path.read_bytes()
    symbols = load_symbols(elf_path)

    if len(data) != ROM_SIZE:
        fail(f"ROM is {len(data)} bytes, expected exactly {ROM_SIZE}")
    if data[0x9C] != 0xA5:
        fail(f"header debug byte is 0x{data[0x9C]:02X}, expected 0xA5")
    if data[0xB4] != 0x00:
        fail(f"DACS selector is 0x{data[0xB4]:02X}, expected 0x00")
    if data[0xB2] != 0x96:
        fail("header fixed byte is not 0x96")

    expected_checksum = (-0x19 - sum(data[0xA0:0xBD])) & 0xFF
    if data[0xBD] != expected_checksum:
        fail(
            f"header checksum is 0x{data[0xBD]:02X}, "
            f"expected 0x{expected_checksum:02X}"
        )

    vector_symbol = require_symbol(symbols, "cartridge_debug_vector")
    handler_symbol = require_symbol(symbols, "rom_exception_entry")
    dispatch_symbol = require_symbol(symbols, "exception_dispatch")
    irq_symbol = require_symbol(symbols, "rom_irq_handler")
    stop_symbol = require_symbol(symbols, "stop_unhandled_exception")

    if vector_symbol != DEBUG_VECTOR:
        fail(f"debug vector linked at 0x{vector_symbol:08X}")
    for name, address in (
        ("rom_exception_entry", handler_symbol),
        ("exception_dispatch", dispatch_symbol),
        ("rom_irq_handler", irq_symbol),
        ("stop_unhandled_exception", stop_symbol),
    ):
        if not ROM_BASE <= address < DEBUG_VECTOR:
            fail(f"{name} is not in ordinary ROM: 0x{address:08X}")

    vector_opcode = rom_word(data, DEBUG_VECTOR)
    if (vector_opcode & 0xFF000000) != 0xEA000000:
        fail("fixed debug vector is not a four-byte ARM B instruction")
    branch_words = vector_opcode & 0x00FFFFFF
    if branch_words & 0x00800000:
        branch_words -= 0x01000000
    vector_target = (DEBUG_VECTOR + 8 + branch_words * 4) & 0xFFFFFFFF
    if vector_target != handler_symbol:
        fail(
            f"fixed debug vector targets 0x{vector_target:08X}, "
            f"expected ROM handler 0x{handler_symbol:08X}"
        )

    expected_traps = {
        "demo_debug_break": UDF_DEBUG,
        "demo_syscall_add": UDF_SYSCALL,
        "demo_armv5_clz": ARMV5_CLZ,
    }
    for name, opcode in expected_traps.items():
        address = require_symbol(symbols, name)
        actual = rom_word(data, address)
        if actual != opcode:
            fail(f"{name} has opcode 0x{actual:08X}, expected 0x{opcode:08X}")

    hook_symbols = {
        name: require_symbol(symbols, name)
        for name in (
            "wram_thumb_target",
            "wram_arm_target",
        )
    }
    for name, address in hook_symbols.items():
        if not 0x03000000 <= address < 0x03007F00:
            fail(f"{name} is not writable IWRAM code: 0x{address:08X}")
    for name in ("hook_thumb_reimplementation", "hook_arm_reimplementation"):
        address = require_symbol(symbols, name)
        if not ROM_BASE <= address < DEBUG_VECTOR:
            fail(f"{name} is not in ROM: 0x{address:08X}")

    hook_count_address = require_symbol(symbols, "g_exception_hook_count")
    hook_table_address = require_symbol(symbols, "g_exception_hook_table")
    if not ROM_BASE <= hook_count_address < DEBUG_VECTOR:
        fail("hook descriptor count is not in ROM")
    if not ROM_BASE <= hook_table_address < DEBUG_VECTOR:
        fail("hook descriptor table is not in ROM")
    if rom_word(data, hook_count_address) != 2:
        fail("hook descriptor table does not contain exactly two demo entries")

    descriptor_words = 5
    expected_hooks = (
        (
            "wram_thumb_target",
            "hook_thumb_reimplementation",
            THUMB_HOOK_UDF,
            2,
            4,
        ),
        (
            "wram_arm_target",
            "hook_arm_reimplementation",
            ARM_HOOK_UDF,
            4,
            5,
        ),
    )
    for index, (target, replacement, opcode, width, kind) in enumerate(
        expected_hooks
    ):
        descriptor = hook_table_address + index * descriptor_words * 4
        stored_target = rom_word(data, descriptor)
        stored_replacement = rom_word(data, descriptor + 4)
        actual = tuple(
            rom_word(data, descriptor + offset) for offset in (8, 12, 16)
        )
        target_mask = ~1 if width == 2 else ~3
        if stored_target & target_mask != hook_symbols[target]:
            fail(f"hook descriptor {index} has the wrong target")
        if stored_replacement & ~1 != require_symbol(symbols, replacement):
            fail(f"hook descriptor {index} has the wrong replacement")
        if actual != (opcode, width, kind):
            fail(f"hook descriptor {index} has invalid opcode/width/kind")

    title = data[0xA0:0xAC].rstrip(b"\0").decode("ascii", errors="replace")
    print(
        "verify: OK\n"
        f"  title          {title}\n"
        f"  size           {len(data)} bytes (32 MiB)\n"
        f"  BIOS hand-off  0x{DEBUG_VECTOR:08X}\n"
        f"  ROM handler    0x{handler_symbol:08X}\n"
        f"  ROM IRQ        0x{irq_symbol:08X}\n"
        f"  WRAM Thumb     0x{hook_symbols['wram_thumb_target']:08X} "
        f"(2-byte UDF 0x{THUMB_HOOK_UDF:04X})\n"
        f"  WRAM ARM       0x{hook_symbols['wram_arm_target']:08X} "
        f"(4-byte UDF 0x{ARM_HOOK_UDF:08X})\n"
        "  ROM hook table 2 descriptors verified\n"
        "  ROM dispatch   debug/syscall/CLZ and hook handlers verified"
    )


if __name__ == "__main__":
    main()
