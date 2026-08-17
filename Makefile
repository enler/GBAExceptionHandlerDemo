DEVKITPRO ?= /opt/devkitpro
DEVKITARM ?= $(DEVKITPRO)/devkitARM

PREFIX  := $(DEVKITARM)/bin/arm-none-eabi-
CC      := $(PREFIX)gcc
OBJCOPY := $(PREFIX)objcopy
OBJDUMP := $(PREFIX)objdump
NM      := $(PREFIX)nm
SIZE    := $(PREFIX)size
GBAFIX  ?= $(DEVKITPRO)/tools/bin/gbafix
MGBA_HEADLESS ?= mgba-headless

BUILD   := build
TARGET  := $(BUILD)/gba_exception_demo
SMOKE   := $(BUILD)/gba_exception_smoke
CRT0    := $(DEVKITARM)/arm-none-eabi/lib/gba_crt0.o

ARCHFLAGS := -mcpu=arm7tdmi -mthumb-interwork
WARNFLAGS := -Wall -Wextra -Wshadow -Wconversion -Wundef
COMMON_CFLAGS := $(ARCHFLAGS) -std=c11 -O2 -g3 -ffreestanding -fno-builtin \
	-ffunction-sections -fdata-sections $(WARNFLAGS) -Iinclude
THUMB_CFLAGS  := $(COMMON_CFLAGS) -mthumb
ARM_CFLAGS    := $(COMMON_CFLAGS) -marm
ASFLAGS       := $(ARCHFLAGS) -marm -g3 -x assembler-with-cpp -Iinclude
LDFLAGS       := $(ARCHFLAGS) -nostdlib -Wl,-T,linker.ld \
	-Wl,-Map,$(TARGET).map -Wl,--gc-sections -Wl,--no-warn-rwx-segments

COMMON_OBJECTS := \
	$(BUILD)/exception.o \
	$(BUILD)/hook_demo.o \
	$(BUILD)/hook_thumb.o \
	$(BUILD)/hook_arm.o \
	$(BUILD)/handlers.o
OBJECTS := $(BUILD)/main.o $(COMMON_OBJECTS)
SMOKE_OBJECTS := $(BUILD)/main_smoke.o $(COMMON_OBJECTS)

.PHONY: all clean verify disasm smoke-rom runtime-test

all: $(TARGET).gba verify

$(BUILD):
	mkdir -p $@

$(BUILD)/main.o: src/main.c include/demo.h include/gba_hw.h | $(BUILD)
	$(CC) $(THUMB_CFLAGS) -c $< -o $@

$(BUILD)/main_smoke.o: src/main.c include/demo.h include/gba_hw.h | $(BUILD)
	$(CC) $(THUMB_CFLAGS) -DDEMO_RUNTIME_SMOKE -c $< -o $@

$(BUILD)/exception.o: src/exception.c include/demo.h include/gba_hw.h | $(BUILD)
	$(CC) $(ARM_CFLAGS) -c $< -o $@

$(BUILD)/hook_demo.o: src/hook_demo.c include/demo.h | $(BUILD)
	$(CC) $(THUMB_CFLAGS) -c $< -o $@

$(BUILD)/hook_thumb.o: src/hook_thumb.c include/demo.h | $(BUILD)
	$(CC) $(THUMB_CFLAGS) -falign-functions=2 -c $< -o $@

$(BUILD)/hook_arm.o: src/hook_arm.c include/demo.h | $(BUILD)
	$(CC) $(ARM_CFLAGS) -falign-functions=4 -c $< -o $@

$(BUILD)/handlers.o: src/handlers.S include/demo.h | $(BUILD)
	$(CC) $(ASFLAGS) -c $< -o $@

$(TARGET).elf: $(OBJECTS) linker.ld
	$(CC) $(LDFLAGS) $(CRT0) $(OBJECTS) -lgcc -o $@
	$(SIZE) $@

$(TARGET).gba: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@
	$(GBAFIX) $@ -p -tEXCEPTDEMO -cAXDE -m00 -r0 -d0

$(SMOKE).elf: $(SMOKE_OBJECTS) linker.ld
	$(CC) $(ARCHFLAGS) -nostdlib -Wl,-T,linker.ld \
		-Wl,-Map,$(SMOKE).map -Wl,--gc-sections -Wl,--no-warn-rwx-segments \
		$(CRT0) $(SMOKE_OBJECTS) \
		-lgcc -o $@

$(SMOKE).gba: $(SMOKE).elf
	$(OBJCOPY) -O binary $< $@
	$(GBAFIX) $@ -p -tEXCSMOKE -cAXDE -m00 -r0 -d0

smoke-rom: $(SMOKE).gba
	python3 tools/verify_rom.py $(SMOKE).gba $(SMOKE).elf

runtime-test: smoke-rom
	python3 tools/run_smoke.py "$(MGBA_HEADLESS)" $(SMOKE).gba

verify: $(TARGET).gba
	python3 tools/verify_rom.py $(TARGET).gba $(TARGET).elf

disasm: $(TARGET).elf
	$(OBJDUMP) -h -d $< > $(TARGET).disasm.txt
	$(NM) -n $< > $(TARGET).symbols.txt

clean:
	rm -rf $(BUILD)
