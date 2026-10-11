ARM := $(DEVKITARM)/bin/arm-none-eabi-
OUT ?= ../../build/ring-poc
ROOT := ../..
.PHONY: all
all: $(OUT)/ring-poc.gba
$(OUT):
	mkdir -p $@
$(OUT)/start.o: start.s | $(OUT)
	$(ARM)as -mcpu=arm7tdmi -o $@ $<
$(OUT)/main.o: main.c | $(OUT)
	$(ARM)gcc -mcpu=arm7tdmi -mthumb -mthumb-interwork -Os -ffreestanding -fno-builtin -Wall -Wextra -c -o $@ $<
$(OUT)/ring-poc.elf: $(OUT)/start.o $(OUT)/main.o link.ld
	$(ARM)ld -T link.ld -o $@ $(OUT)/start.o $(OUT)/main.o
$(OUT)/ring-poc.gba: $(OUT)/ring-poc.elf
	$(ARM)objcopy -O binary $< $@
	$(ROOT)/tools/gbafix/gbafix $@ -p -tRINGPOC -cRNG8 --silent
