# GBA Exception Handler Demo

**中文（主文档）** | [English](README.en.md)

这是一个面向真实 GBA/ARM7TDMI 异常路径的演示 ROM。启动后会自动执行四项测试，并在 Mode 3 画面中显示 `PASS/FAIL`；按 `A` 可重新执行。

它包含：

1. **正常除错快照**：用用户自定义 Undefined 指令充当软件断点，记录异常 PC、opcode、SPSR/CPSR、R0-R12、user SP/LR 和异常 LR。
2. **模拟 syscall**：用 Undefined 指令的 16 位立即数作为 syscall 编号，示例服务把 `r0 + r1` 返回到 `r0`。
3. **观察并模拟 GBA 不支持的指令**：实际执行 ARMv5 `CLZ r0,r0` 位流。ARM7TDMI 不实现 `CLZ`，但这不代表该位流必然触发 Undefined；真实 GBA 可以无异常地继续执行并保留原值。只有把该位流判为 Undefined 的运行环境才会进入 ROM handler，由软件模拟语义并写回目的寄存器。
4. **Undefined exception 函数 hook**：两个普通 C 目标函数由启动代码复制到 IWRAM。运行时分别把 Thumb 函数入口的 2 字节和 ARM 函数入口的 4 字节改成未定义指令。ROM handler 根据异常 PC 接管调用、执行 C 语言替代实现，再直接返回原调用者。

## 构建

需要 devkitARM 和 gba-tools（`gbafix`）：

```sh
make
```

输出：

- `build/gba_exception_demo.gba`：可运行 ROM，固定为 32 MiB。
- `build/gba_exception_demo.elf`：保留符号，适合 mGBA/GDB 调试。
- `build/gba_exception_demo.map`：链接布局。

额外命令：

```sh
make verify       # 校验 ROM 头、ROM handler、WRAM hook 地址和 trap opcode
make disasm       # 生成反汇编和符号表
make smoke-rom    # 生成供 mgba-headless 自动测试的 ROM
make runtime-test # 运行 smoke ROM；可用 MGBA_HEADLESS=/path/to/mgba-headless
make clean
```

在较新的 mGBA 中直接打开 `build/gba_exception_demo.gba` 即可。用于 CI 的 smoke ROM 会在测试后执行 `SWI 0x7F`，四项全部通过时让 `r0=0x0F`：

```sh
mgba-headless -S 127 -R r0 build/gba_exception_smoke.gba
echo $?
# 预期：15
```

也可让工程包装这个特殊退出码：

```sh
make runtime-test MGBA_HEADLESS=/path/to/mgba-headless
```

这里的 `0x0F` 只是当前 mGBA 异常模型的预期结果，不是真机认证。当前 CLZ 项把“原始 ARMv5 位流进入 UND，并由 handler 得到 8”定义为 PASS；真实 GBA 或贴近真机 CPU 行为的模拟器可以不为该位流触发 UND。因此四项全部 PASS 反而说明该环境没有复现这项原生行为。若其余三项通过，而 CLZ 返回原输入 `0x00F00000`，结果掩码应为 `0x0B`。

## 为什么 ROM 是 32 MiB

GBA 的物理异常向量位于只读 BIOS，卡带程序不能像普通裸机工程那样覆盖 `0x00000004`。GBATEK 记录了零售 BIOS 保留的调试转发路径：

- ROM 头 `0x0800009C` 设为 `0xA5`，解锁 FIQ/Undefined 转发；
- ROM 头 `0x080000B4` 的 bit 7 为 0 时，BIOS 跳到 `0x09FFC000`；
- bit 7 为 1 时则跳到 `0x09FE2000`。

本工程选择 `0x09FFC000`。该地址位于 32 MiB Game Pak 窗口末尾，链接器在那里放置转入 `rom_exception_entry` 的固定入口。`rom_exception_entry`、`exception_dispatch()`、hook 描述表、两个 C 替代实现和 IRQ handler 都在 ROM；只有两个待修改的 C 目标函数位于 IWRAM。`gbafix -d0` 设置头部调试位并修正校验和，`-p` 将镜像补齐到 32 MiB。

这也意味着真机烧录介质必须完整映射 32 MiB ROM，不能把尾部 `0xFF` 和 `0x09FFC000` 附近的固定入口裁掉。

## 异常链路

```text
ARM/Thumb 未定义指令
    -> CPU Undefined 异常向量 0x00000004
    -> GBA BIOS 调试转发入口
       在 0x03007FE0..0x03007FEC 保存 SPSR/CPSR/r12/LR
    -> 卡带地址 0x09FFC000
    -> ROM 中的卡带固定入口
    -> rom_exception_entry
    -> ROM 中的 exception_dispatch
       普通 trap：模拟或分派后，从下一条指令继续
       hook trap：替换 r0，并把异常返回目标改为调用者 LR
       未识别 trap：保留现场并停在 ROM 循环
    -> 返回 BIOS
    -> BIOS 恢复 CPSR 和寄存器
```

IRQ 走另一条标准路径：VBlank IRQ 经 BIOS 转发到 `0x03007FFC` 中登记的 ROM 函数 `rom_irq_handler`。handler 清除 `IF`，并同步更新 `0x03007FF8` 的 BIOS IRQ flags，兼容 `IntrWait/VBlankIntrWait` 约定。

## Opcode 约定

GBATEK 将下面的 ARM 编码空间标为用户可用：

```text
cond 01111111 xxxx xxxx xxxx 1111 xxxx
```

本演示使用：

| 用途 | ARM opcode | 立即数 |
|---|---:|---:|
| Debug breakpoint | `E7F000F1` | `0001` |
| Pseudo syscall ADD | `E7F010F0` | `0100` |
| ARM 4-byte hook trap | `E7F020F0` | `0200` |
| ARMv5 `CLZ r0,r0` | `E16F0F10` | — |

伪 syscall **不是**真正的 `SWI`。GBA 的 SWI vector 同样在 BIOS 中，由 BIOS 自己分发，普通卡带无法替换；因此这里采用 GBATEK 建议的 Undefined 扩展方式，获得类似系统调用的 ABI。它演示的是调用约定和服务分派，不提供内存或权限隔离。

新增 syscall 时，可在 `include/demo.h` 分配立即数，在 `src/handlers.S` 添加 ARM call gate，并在 `exception_dispatch()` 中增加分支。新增可模拟指令时，应使用严格 mask 解码，同时明确支持哪些源/目的寄存器。

## CLZ 项是平台差异探针

`E16F0F10` 从 ARMv5T 起表示 `CLZ r0,r0`，但“ARMv4T 不支持该指令”不能直接推导成“ARM7TDMI 必然进入 UND”。ARM7TDMI 手册明确警告，部分没有定义的编码不会产生 Undefined Instruction trap，其效果属于不可预测行为。真实 GBA 上该位流可以表现为无效果操作：`r0` 仍为输入的 `0x00F00000`，随后 `bx lr` 正常返回，ROM handler 根本看不到这次执行。

当前自检有意同时要求结果和异常现场，所以三种环境会被区分开：

| 执行行为 | 返回值 | CLZ 项 |
|---|---:|---:|
| 原生 GBA／贴近原生行为：不进入 UND | `00F00000` | FAIL |
| 当前 mGBA：进入 UND，由 ROM handler 模拟 | `00000008` | PASS |
| ARMv5 以上处理器原生执行 `CLZ` | `00000008` | FAIL（没有 handler 现场） |

因此，本 ROM 的四项全 PASS 只证明四条预设的异常测试路径在该运行环境中成立；它不表示该环境更接近真实 GBA。若需要跨实现、确定性地演示“用异常扩展指令集”，应执行明确的用户 Undefined 编码并把虚拟 CLZ 编号放在立即数中，而不能依赖后续架构指令在旧处理器上一定 trap。

## 2 字节与 4 字节函数 hook

这个 **exception-assisted hook** 用一条与当前指令集等宽的未定义指令占住 hook 点，由 Undefined Instruction handler 决定新的语义。

| 目标状态 | 覆盖大小 | 本演示写入 | 含义 |
|---|---:|---:|---|
| Thumb | 2 字节 | `DE42` | `1101 1110 imm8`；ARMv4T 中 cond=`1110` 的保留/未定义编码 |
| ARM | 4 字节 | `E7F020F0` | GBATEK 用户自定义 Undefined 编码，立即数 `0200` |

ROM 中的 `g_exception_hook_table` 为每个 hook 保存目标函数、替代实现、opcode、指令宽度和 trap kind。当前 ROM 登记两个固定 C 函数入口；增加其他可写执行入口时，只需增加相应描述项和补丁安装逻辑。函数中间 hook 还需要单独定义 continuation 或被覆盖指令的模拟策略。

真机 Game Pak ROM 是只读的，所以运行时仍不能修改 ROM 函数入口。handler 可以放在 ROM，但演示用的两个待 hook C 函数必须放在 IWRAM：

```text
03000000 wram_thumb_target:  adds r0,#1       ; Thumb C 原函数
                             bx   lr
03000004 wram_arm_target:    add  r0,r0,#2    ; ARM C 原函数
                             bx   lr
```

`run_wram_hook_demo()` 先调用原函数并记录结果，然后执行真正的入口写入：

```c
*thumb_entry = 0xDE42;       /* 写 2 字节 Thumb Undefined */
*arm_entry   = 0xE7F020F0;   /* 写 4 字节 ARM Undefined   */
```

再次调用同一目标地址时，CPU 进入 UND 模式。ROM handler 的处理过程是：

1. 读取 BIOS 保存的 `SPSR.T`；Thumb 用 `LR_und-2`、ARM 用 `LR_und-4` 得到 fault PC。
2. 用 fault PC、opcode 和指令宽度匹配 ROM hook 描述表，避免把其他 Undefined 指令误认成 hook。
3. UND 模式有自己的 banked LR，因此汇编入口用 `STM^` 额外取得被中断代码的 user/system LR，也就是原函数调用者的返回地址。
4. `exception_dispatch()` 调用描述项指定的 ROM C 替代实现。示例分别计算 `value+0x40` 和 `value+0x80`，再把结果写回保存的 `r0`。
5. handler 根据 caller LR 的 bit 0 恢复 ARM/Thumb 状态，并把 BIOS 异常返回地址改成 caller LR。原函数剩余指令不再执行，效果相当于 handler 完整重实现了这个 C 函数。

因此输入 `0x11` 时，未安装 hook 的两个返回值为 `0x12`、`0x13`；安装后变为 `0x51`、`0x91`。按 `A` 重跑会先恢复原入口，再重新安装两个异常 hook。

## 自检边界

运行测试以四个位分别表示 debug、syscall、CLZ 和 hook。每一位只由对应功能的观测结果决定；当前 mGBA 四项通过时为 `0x0F`，真实 GBA 仅 CLZ 项按上述原生行为失败时为 `0x0B`。VBlank IRQ 是界面循环使用的辅助设施，不参与任何一项 PASS 判定。

debug 自检除返回值和 opcode 外，还验证精确 fault PC、user SP 所在范围及 user LR；syscall 和 CLZ 也校验 trap PC 与输入寄存器。hook 自检分别验证原始结果、替代结果、命中次数和实际 fault PC。未识别的异常不会继续执行：handler 保留 `g_trap_report` 后停在 ROM 循环，便于模拟器或调试器检查现场。

## 文件说明

- `src/handlers.S`：DACS 固定入口、user LR 捕获、ROM 异常入口、ROM IRQ handler、两个 ARM trap call gate 和一个 CLZ 探针 gate。
- `src/exception.c`：ROM 中的寄存器快照、opcode 解码、伪 syscall、CLZ 模拟、hook 表分派和未知异常停机。
- `src/hook_demo.c`：ROM hook 描述表、保存/恢复入口、写入 2/4 字节 Undefined 补丁、C 替代实现和结果检查。
- `src/hook_thumb.c`、`src/hook_arm.c`：分别以 Thumb/ARM 编译并复制到 IWRAM 的两个目标 C 函数。
- `src/main.c`：Mode 3 演示界面与板上自检。
- `linker.ld`：ROM/IWRAM/EWRAM 布局以及 `0x09FFC000` 固定段。
- `tools/verify_rom.py`：不运行模拟器也能校验 ROM 头、固定入口、代码位置以及两个 hook 描述项。
- `tools/run_smoke.py`：运行 mGBA smoke image，并要求四项测试位掩码为 `0x0F`。

## 限制与兼容性

- 当前示例是函数入口的完全替代。若在函数中间下 hook，handler 必须模拟被覆盖指令，或明确选择一个安全 continuation PC；不能无条件套用“返回 caller LR”。
- 示例不提供“先 hook、再调用原函数”的 trampoline。要保留原函数行为，仍需保存并执行/模拟被覆盖指令，同时处理 PC-relative 指令的重定位。
- 每次 hook 命中都有完整异常进入、BIOS 转发、C dispatcher 和异常返回开销，不适合高频热路径。
- fault PC 是 hook 身份的一部分；扩展时必须为每个 hook 点增加严格匹配 opcode、地址和指令宽度的描述项。
- ARM7TDMI 没有指令缓存；若将同一技术移植到带 I-cache 的处理器，写代码后还必须执行相应 cache maintenance。
- GBA 没有 MMU/MPU，正常零售机上无法用非法内存访问可靠地产生 Data/Prefetch Abort；本演示聚焦可实际触发的 Undefined 和 IRQ。
- ARMv5 `CLZ` 位流不是可靠的 Undefined 触发器；它保留在演示中用于暴露 CPU／模拟器差异。需要确定性异常时，应使用本工程已有的明确 Undefined 编码。
- 零售机的 FIQ 引脚通常不可用，本工程不把 FIQ 当作可重复的软件测试源。
- 某些旧模拟器的内置替代 BIOS 没有实现调试头转发。请使用较新的 mGBA，或使用能正确实现该 BIOS 行为的环境。不要从不明来源下载受版权保护的 GBA BIOS。

## 参考资料

- [GBATEK Markdown Fork](https://mgba-emu.github.io/gbatek/)：GBA cartridge header、BIOS interrupt handling、IWRAM 系统区和 ARM exception/undefined encoding。
- [mGBA HLE BIOS 实现](https://github.com/mgba-emu/mgba/blob/master/src/gba/hle-bios.s)：可交叉核对 BIOS 保存区、`A5` 检查和两个 DACS 跳转地址。
- [Arm ARM7TDMI Technical Reference Manual](https://documentation-service.arm.com/static/5e8e1323fd977155116a3129)：ARMv4T 异常模型，以及部分未定义编码不保证触发 Undefined 的硬件限制。
- [Arm 对架构版本与 CLZ 的说明](https://developer.arm.com/community/arm-community-blogs/b/architectures-and-processors-blog/posts/arm-fundamentals-introduction-to-understanding-arm-processors)：ARM7TDMI 属于 ARMv4T，`CLZ` 从 ARMv5T 起提供。
