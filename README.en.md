# GBA Exception Handler Demo

[中文（主文档）](README.md) | **English (translation)**

This ROM demonstrates exception paths on real GBA/ARM7TDMI hardware. At startup it automatically runs four tests and displays `PASS/FAIL` on a Mode 3 screen; press `A` to run them again.

It includes:

1. **Normal debug snapshot**: a user-defined Undefined instruction acts as a software breakpoint and records the exception PC, opcode, SPSR/CPSR, R0-R12, user SP/LR, and exception LR.
2. **Emulated syscall**: the 16-bit immediate of an Undefined instruction serves as a syscall number. The example service returns `r0 + r1` in `r0`.
3. **Observe and emulate an instruction unsupported by the GBA**: the ROM executes the actual ARMv5 `CLZ r0,r0` bit pattern. ARM7TDMI does not implement `CLZ`, but that does not guarantee that this bit pattern raises Undefined; a real GBA can continue without an exception and leave the input unchanged. Only an environment that classifies the pattern as Undefined enters the ROM handler, which emulates the instruction and writes back its result.
4. **Undefined-exception function hooks**: startup code copies two ordinary C target functions to IWRAM. At runtime, their entries are replaced with a 2-byte Thumb Undefined instruction and a 4-byte ARM Undefined instruction respectively. The ROM handler takes over the call, invokes a C replacement, and returns directly to the original caller.

## Building

Requires devkitARM and gba-tools (`gbafix`):

```sh
make
```

Outputs:

- `build/gba_exception_demo.gba`: runnable ROM, fixed at 32 MiB.
- `build/gba_exception_demo.elf`: retains symbols for mGBA/GDB debugging.
- `build/gba_exception_demo.map`: linker map.

Additional commands:

```sh
make verify       # Verify the ROM header, ROM handler, WRAM hook addresses, and trap opcodes
make disasm       # Generate a disassembly and symbol table
make smoke-rom    # Generate a ROM for automated mgba-headless testing
make runtime-test # Run the smoke ROM; MGBA_HEADLESS=/path/to/mgba-headless is supported
make clean
```

Recent mGBA versions can open `build/gba_exception_demo.gba` directly. The CI smoke ROM executes `SWI 0x7F` after testing and sets `r0=0x0F` when all four checks pass:

```sh
mgba-headless -S 127 -R r0 build/gba_exception_smoke.gba
echo $?
# Expected: 15
```

The project can also handle this special exit code:

```sh
make runtime-test MGBA_HEADLESS=/path/to/mgba-headless
```

This `0x0F` result is only the expected result under mGBA's current exception model; it does not certify real-hardware behavior. The current CLZ check defines “the original ARMv5 bit pattern enters UND and the handler produces 8” as PASS. A real GBA, or an emulator that closely reproduces its CPU behavior, may not raise UND for that pattern. Consequently, all four checks passing actually means that the environment did not reproduce this particular native behavior. If the other three checks pass while CLZ returns its original input, `0x00F00000`, the result mask should be `0x0B`.

## Why the ROM Is 32 MiB

The GBA's physical exception vectors reside in its read-only BIOS, so a cartridge program cannot replace `0x00000004` as an ordinary bare-metal program would. GBATEK documents a debug forwarding path retained by the retail BIOS:

- Setting ROM header byte `0x0800009C` to `0xA5` enables FIQ/Undefined forwarding.
- When bit 7 of ROM header byte `0x080000B4` is 0, the BIOS jumps to `0x09FFC000`.
- When bit 7 is 1, it jumps to `0x09FE2000` instead.

This project uses `0x09FFC000`. That address lies near the end of the 32 MiB Game Pak window, where the linker places a fixed entry that transfers control to `rom_exception_entry`. `rom_exception_entry`, `exception_dispatch()`, the hook descriptor table, both C replacement implementations, and the IRQ handler all reside in ROM; only the two modifiable C target functions reside in IWRAM. `gbafix -d0` sets the header debug field and fixes the header checksum, while `-p` pads the image to 32 MiB.

On real hardware, the flash cartridge must therefore map the complete 32 MiB ROM. It must not trim the trailing `0xFF` bytes or the fixed entry near `0x09FFC000`.

## Exception Flow

```text
ARM/Thumb undefined opcode
    -> CPU Undefined vector 0x00000004
    -> GBA BIOS debug forwarding entry
       saves SPSR/CPSR/r12/LR at 0x03007FE0..0x03007FEC
    -> cartridge address 0x09FFC000
    -> fixed cartridge entry in ROM
    -> rom_exception_entry
    -> exception_dispatch in ROM
       normal trap: emulate or dispatch, then resume at the next instruction
       hook trap:   replace r0 and set the exception return target to the caller LR
       unknown trap: retain the snapshot and stop in a ROM loop
    -> return to the BIOS
    -> BIOS restores CPSR and registers
```

IRQ follows a separate standard path: a VBlank IRQ is forwarded by the BIOS to the ROM function `rom_irq_handler` registered at `0x03007FFC`. The handler acknowledges `IF` and also updates the BIOS IRQ flags at `0x03007FF8`, preserving the `IntrWait/VBlankIntrWait` convention.

## Opcode Convention

GBATEK marks the following ARM encoding space as available for user-defined instructions:

```text
cond 01111111 xxxx xxxx xxxx 1111 xxxx
```

This demo uses:

| Purpose | ARM opcode | Immediate |
|---|---:|---:|
| Debug breakpoint | `E7F000F1` | `0001` |
| Pseudo syscall ADD | `E7F010F0` | `0100` |
| ARM 4-byte hook trap | `E7F020F0` | `0200` |
| ARMv5 `CLZ r0,r0` | `E16F0F10` | — |

The pseudo syscall is **not** a real `SWI`. The GBA's SWI vector also resides in the BIOS and is dispatched by the BIOS itself, so an ordinary cartridge cannot replace it. This demo instead follows GBATEK's suggested Undefined-extension mechanism to obtain a syscall-like ABI. It demonstrates a calling convention and service dispatch, not memory or privilege isolation.

To add a syscall, allocate an immediate in `include/demo.h`, add an ARM call gate in `src/handlers.S`, and add a branch to `exception_dispatch()`. To add an emulated instruction, decode it with a strict mask and explicitly define the supported source and destination registers.

## The CLZ Check Is a Platform-Difference Probe

Starting with ARMv5T, `E16F0F10` means `CLZ r0,r0`, but “ARMv4T does not support this instruction” does not imply “ARM7TDMI must enter UND.” The ARM7TDMI manual explicitly warns that some undefined encodings do not generate an Undefined Instruction trap and instead have unpredictable effects. On a real GBA this bit pattern can behave like a no-op: `r0` remains at the input value `0x00F00000`, the following `bx lr` returns normally, and the ROM handler never observes the execution.

The current self-check intentionally requires both the computed result and an exception snapshot, distinguishing three kinds of environment:

| Execution behavior | Return value | CLZ check |
|---|---:|---:|
| Real GBA/native-like behavior: no UND | `00F00000` | FAIL |
| Current mGBA: enters UND and is emulated by the ROM handler | `00000008` | PASS |
| ARMv5 or newer processor executes `CLZ` natively | `00000008` | FAIL (no handler snapshot) |

Therefore, all four checks passing only proves that the four predefined exception-test paths work in that environment; it does not mean that the environment is closer to a real GBA. A deterministic, cross-implementation demonstration of “extending the instruction set through exceptions” should execute an explicit user-defined Undefined encoding and place a virtual CLZ operation number in its immediate, instead of assuming that a later-architecture instruction must trap on an older processor.

## 2-byte and 4-byte Function Hooks

This **exception-assisted hook** occupies a hook point with an Undefined instruction of the same width as the current instruction set. The Undefined Instruction handler supplies the replacement semantics.

| Target state | Bytes replaced | Value written by this demo | Meaning |
|---|---:|---:|---|
| Thumb | 2 bytes | `DE42` | `1101 1110 imm8`; the reserved/undefined cond=`1110` encoding in ARMv4T |
| ARM | 4 bytes | `E7F020F0` | GBATEK user-defined Undefined encoding with immediate `0200` |

For every hook, the ROM-resident `g_exception_hook_table` stores the target function, replacement implementation, opcode, instruction width, and trap kind. The current ROM registers two fixed C function entries. Supporting another writable entry requires only a matching descriptor and patch installation. A mid-function hook additionally needs an explicit continuation or a strategy for emulating the overwritten instruction.

Real Game Pak ROM is read-only, so a running program still cannot modify function entries in ROM. The handler can reside in ROM, but this demonstration places the two C hook targets in IWRAM:

```text
03000000 wram_thumb_target:  adds r0,#1       ; Original Thumb C function
                             bx   lr
03000004 wram_arm_target:    add  r0,r0,#2    ; Original ARM C function
                             bx   lr
```

`run_wram_hook_demo()` first calls the original functions and records their results, then performs the actual entry writes:

```c
*thumb_entry = 0xDE42;       /* Write a 2-byte Thumb Undefined instruction */
*arm_entry   = 0xE7F020F0;   /* Write a 4-byte ARM Undefined instruction   */
```

Calling the same target addresses again makes the CPU enter UND mode. The ROM handler proceeds as follows:

1. It reads the saved `SPSR.T`; the fault PC is `LR_und-2` for Thumb and `LR_und-4` for ARM.
2. It matches the fault PC, opcode, and instruction width against the ROM hook descriptor table, avoiding false identification of other Undefined instructions as hooks.
3. UND mode has its own banked LR, so the assembly entry additionally uses `STM^` to obtain the interrupted code's user/system LR: the original caller's return address.
4. `exception_dispatch()` calls the ROM C replacement selected by the descriptor. The examples compute `value+0x40` and `value+0x80`, then write the result to the saved `r0`.
5. The handler restores ARM/Thumb state from bit 0 of the caller LR and changes the BIOS exception return address to that caller LR. The rest of the original function does not execute; the handler has completely reimplemented the C function.

For an input of `0x11`, the unhooked functions return `0x12` and `0x13`; after hook installation they return `0x51` and `0x91`. Pressing `A` first restores the original entries and then reinstalls both exception hooks.

## Self-check Boundaries

The test result uses four bits for debug, syscall, CLZ, and hook respectively. Each bit depends only on the observations for that feature. All four checks passing under current mGBA produces `0x0F`; on a real GBA, if only the CLZ check fails with the native behavior described above, the result is `0x0B`. VBlank IRQ is only an aid for the display loop and does not contribute to any PASS result.

In addition to the return value and opcode, the debug self-check verifies the exact fault PC, the range of the user SP, and the user LR. The syscall and CLZ checks also verify the trap PC and input registers. The hook check independently verifies the original results, replacement results, hit counts, and actual fault PCs. An unrecognized exception does not resume execution: the handler preserves `g_trap_report` and stops in a ROM loop so that an emulator or debugger can inspect the snapshot.

## Files

- `src/handlers.S`: DACS fixed entry, user-LR capture, ROM exception entry, ROM IRQ handler, two ARM trap call gates, and one CLZ probe gate.
- `src/exception.c`: ROM-resident register snapshot, opcode decoding, pseudo syscall, CLZ emulation, hook-table dispatch, and unknown-exception stop.
- `src/hook_demo.c`: ROM hook descriptor table, entry save/restore, 2/4-byte Undefined patch writes, C replacements, and result checks.
- `src/hook_thumb.c`, `src/hook_arm.c`: the two C targets, compiled for Thumb and ARM respectively and copied to IWRAM.
- `src/main.c`: Mode 3 demonstration UI and on-device self-checks.
- `linker.ld`: ROM/IWRAM/EWRAM layout and the fixed section at `0x09FFC000`.
- `tools/verify_rom.py`: verifies the ROM header, fixed entry, code locations, and both hook descriptors without running an emulator.
- `tools/run_smoke.py`: runs the mGBA smoke image and requires the four-test result mask to be `0x0F`.

## Limitations and Compatibility

- The current example completely replaces function entries. A mid-function hook must emulate the overwritten instruction or choose an explicitly safe continuation PC; it cannot unconditionally use “return to caller LR.”
- The example provides no trampoline for calling the original function after hooking it. Preserving original behavior requires saving and executing or emulating the overwritten instruction, including relocation of PC-relative instructions.
- Every hook hit pays for full exception entry, BIOS forwarding, the C dispatcher, and exception return, so this technique is unsuitable for frequently executed hot paths.
- The fault PC is part of a hook's identity. Every additional hook point must have a descriptor that strictly matches its opcode, address, and instruction width.
- ARM7TDMI has no instruction cache. Porting the technique to a processor with an I-cache also requires the appropriate cache maintenance after writing code.
- The GBA has no MMU/MPU, and ordinary retail hardware cannot reliably generate Data or Prefetch Abort through invalid memory access. This demo focuses on practically triggerable Undefined and IRQ exceptions.
- The ARMv5 `CLZ` bit pattern is not a reliable Undefined trigger. It remains in the demo to expose CPU/emulator differences. Use the project's explicit Undefined encodings when a deterministic exception is required.
- The FIQ pin is normally unavailable on retail hardware, so this project does not use FIQ as a repeatable software test source.
- Some older emulators' built-in replacement BIOSes do not implement debug-header forwarding. Use a recent mGBA version or another environment that correctly implements this BIOS behavior. Do not download a copyrighted GBA BIOS from an untrusted source.

## References

- [GBATEK Markdown Fork](https://mgba-emu.github.io/gbatek/): GBA cartridge header, BIOS interrupt handling, IWRAM system area, and ARM exception/undefined encodings.
- [mGBA HLE BIOS implementation](https://github.com/mgba-emu/mgba/blob/master/src/gba/hle-bios.s): useful for cross-checking the BIOS save area, `A5` test, and the two DACS jump addresses.
- [Arm ARM7TDMI Technical Reference Manual](https://documentation-service.arm.com/static/5e8e1323fd977155116a3129): ARMv4T exception behavior and the hardware limitation that some undefined encodings are not guaranteed to raise Undefined.
- [Arm's overview of architecture versions and CLZ](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/arm-fundamentals-introduction-to-understanding-arm-processors): ARM7TDMI implements ARMv4T, while `CLZ` was introduced with ARMv5T.
