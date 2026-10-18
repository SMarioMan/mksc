# Configuration
TITLE			:= MARIO KART
GAME_CODE		:= AMKE
MAKER_CODE		:= 01
REVISION		:= 0
ROM				:= mksc.gba

# Tools
BIN_DIR			:= $(DEVKITARM)/bin
# If DEVKITARM is not set will default to installed /bin/./arm-none-eabi-__;
PREFIX			:= arm-none-eabi-
CPP				:= $(BIN_DIR)/./$(PREFIX)cpp
OBJCOPY 		:= $(BIN_DIR)/./$(PREFIX)objcopy
LD 				:= $(BIN_DIR)/./$(PREFIX)ld
AS 			    := $(BIN_DIR)/./$(PREFIX)as
NM				:= $(BIN_DIR)/./$(PREFIX)nm
SHA1			:= $(shell { command -v sha1sum || command -v shasum; } 2>/dev/null) -c
FIX				:= tools/gbafix/./gbafix
SHELL			:= /bin/bash -o pipefail
OLD_AGBCC		:= tools/agbcc/bin/./old_agbcc
AGBCC			:= tools/agbcc/bin/./agbcc -fprologue-bugfix
AIF2PCM   		:= tools/aif2pcm/./aif2pcm
BIN2S			:= tools/bin2s/./bin2s

# Flags
ASFLAGS			:= -mcpu=arm7tdmi -I include -I.
CFLAGS			:= -mthumb-interwork -Wimplicit -Wparentheses -O2
CPPFLAGS		:= -I tools/agbcc -I tools/agbcc/include -I lib -iquote include -nostdinc
LDFLAGS			= -L../tools/agbcc/lib -L../lib/libunk -lgcc -lc -lunk --just-symbols=../symbols.txt

# Files
ELF = $(ROM:.gba=.elf)
MAP = $(ROM:.gba=.map)
SYMS= $(ROM:.gba=.sym)
OBJ_DIR := build
LD_SCRIPT := ld_script.ld
SYMBOL_FILE := symbols.txt
COMPARE_TARGET := compare

ifeq ($(RING),1)
ifneq ($(filter-out normal shared,$(RING_MUSIC)),)
$(error RING_MUSIC must be normal or shared)
endif
ifneq ($(filter-out original japanese,$(RING_SUNSET_ART)),)
$(error RING_SUNSET_ART must be original or japanese)
endif
ifeq ($(RING_SUNSET_ART),japanese)
ASFLAGS += --defsym MKSC_RING_JAPANESE_SUNSET=1
endif
ifeq ($(RING_MUSIC_RESTORE),1)
CPPFLAGS += -DMKSC_RING_MUSIC_RESTORE=1
ASFLAGS += --defsym MKSC_RING_MUSIC_RESTORE=1
endif
ROM := mksc-ring.gba
COMPARE_TARGET :=
OBJ_DIR := build-ring
CPPFLAGS += -DMKSC_RING=1 -DMKSC_RING_BOOT=$(or $(RING_BOOT),0) -DMKSC_RING_MAP_DISPLAY=$(or $(RING_MAP),0)
ifeq ($(RING_MUSIC),shared)
CPPFLAGS += -DMKSC_RING_MUSIC_SHARED=1
ASFLAGS += --defsym MKSC_RING_AUDIO_AHEAD=2
else
ASFLAGS += --defsym MKSC_RING_AUDIO_AHEAD=3
endif
ASFLAGS += --defsym MKSC_RING=1
LD_SCRIPT := build-ring/ld_script.ld
SYMBOL_FILE := build-ring/symbols.txt
endif

C_SUBDIR = src
ASM_SUBDIR = asm
SOUND_SUBDIR = sound
DATA_SRC_SUBDIR = src/data
# DATA_ASM_SUBDIR = data
DATA_SUBDIR = data

C_BUILDDIR = $(OBJ_DIR)/$(C_SUBDIR)
ASM_BUILDDIR = $(OBJ_DIR)/$(ASM_SUBDIR)
SOUND_BUILDDIR = $(OBJ_DIR)/$(SOUND_SUBDIR)
# DATA_ASM_BUILDDIR = $(OBJ_DIR)/$(DATA_ASM_SUBDIR)
DATA_BUILDDIR = $(OBJ_DIR)/$(DATA_SUBDIR)

C_SRCS := $(wildcard $(C_SUBDIR)/*.c $(C_SUBDIR)/*/*.c $(C_SUBDIR)/*/*/*.c)
C_OBJS := $(patsubst $(C_SUBDIR)/%.c,$(C_BUILDDIR)/%.o,$(C_SRCS))
C_DEPS := $(patsubst $(C_SUBDIR)/%.c,$(C_BUILDDIR)/%.d,$(C_SRCS))

ASM_SRCS := $(wildcard $(ASM_SUBDIR)/*.s)
ASM_OBJS := $(patsubst $(ASM_SUBDIR)/%.s,$(ASM_BUILDDIR)/%.o,$(ASM_SRCS))

SOUND_SRCS := $(wildcard $(SOUND_SUBDIR)/*.s $(SOUND_SUBDIR)/*/*.s $(SOUND_SUBDIR)/*/*/*.s)
SOUND_OBJS := $(patsubst $(SOUND_SUBDIR)/%.s,$(SOUND_BUILDDIR)/%.o,$(SOUND_SRCS))

SOUND_AIFS := $(wildcard $(SOUND_SUBDIR)/*.aif $(SOUND_SUBDIR)/*/*.aif $(SOUND_SUBDIR)/*/*/*.aif)
SOUND_AIF_OBJS := $(patsubst $(SOUND_SUBDIR)/%.aif,$(SOUND_BUILDDIR)/%.o,$(SOUND_AIFS))

DATA_BINS := $(wildcard $(DATA_SUBDIR)/*.bin)
DATA_OBJS := $(patsubst $(DATA_SUBDIR)/%.bin,$(DATA_BUILDDIR)/%.o,$(DATA_BINS))

OBJS     := $(C_OBJS) $(ASM_OBJS) $(DATA_OBJS) $(SOUND_OBJS) $(SOUND_AIF_OBJS)
OBJS_REL := $(patsubst $(OBJ_DIR)/%,%,$(OBJS))

SUBDIRS  := $(sort $(dir $(OBJS)))

$(shell mkdir -p $(SUBDIRS))

define bin2o
	$(BIN2S) -H $(@).h $< | $(AS) -o $(@)
endef

# Rules
.PHONY: tools libraries rom clean progress

rom: tools libraries $(ROM) $(COMPARE_TARGET) $(SYMS)
 
compare: $(ROM)
	@$(SHA1) rom.sha1

progress: $(MAP)
	@perl tools/calcrom.pl $(MAP)

libraries:
	@$(MAKE) -C lib/libunk

clean:
	$(MAKE) clean -C tools/aif2pcm
	$(MAKE) clean -C tools/bin2s
	$(MAKE) clean -C tools/gbafix
	rm -f $(ROM) $(ELF) $(MAP) $(SYMS)
	rm -r build/*
	$(MAKE) -C lib/libunk clean

tools:
	@$(MAKE) -C tools/aif2pcm
	@$(MAKE) -C tools/bin2s
	@$(MAKE) -C tools/gbafix

$(OBJ_DIR)/src/agbbackup/%.o : src/agbbackup/%.c
	@$(CPP) -MMD -MT $@ $(CPPFLAGS) $< -o $(OBJ_DIR)/src/agbbackup/$*.i
	@$(OLD_AGBCC) $(OBJ_DIR)/src/agbbackup/$*.i $(CFLAGS) -O1 -o $(OBJ_DIR)/src/agbbackup/$*.s
	@echo -e ".text\n\t.align\t2, 0\n" >> $(OBJ_DIR)/src/agbbackup/$*.s
	$(AS) $(ASFLAGS) -o $@ $(OBJ_DIR)/src/agbbackup/$*.s

$(C_BUILDDIR)/mp2000/%.o : $(C_SUBDIR)/mp2000/%.c
	@$(CPP) -MMD -MT $@ $(CPPFLAGS) $< -o $(OBJ_DIR)/src/mp2000/$*.i
	@$(OLD_AGBCC) $(OBJ_DIR)/src/mp2000/$*.i $(CFLAGS) -o $(OBJ_DIR)/src/mp2000/$*.s
	@echo -e ".text\n\t.align\t2, 0\n" >> $(OBJ_DIR)/src/mp2000/$*.s
	$(AS) $(ASFLAGS) -o $@ $(OBJ_DIR)/src/mp2000/$*.s

ifeq ($(RING),1)
RING_FAST_MODE := -mthumb
RING_FAST_OPT := -O3
$(C_BUILDDIR)/ringLink.o: RING_FAST_OPT := -O2
$(C_BUILDDIR)/ringTarget.O3.o: RING_FAST_MODE := -marm
# Ring renderer/target hot paths use the optimizing toolchain.
# The clean matching build continues to use agbcc and its native byte layout.
$(C_BUILDDIR)/ringLink.o $(C_BUILDDIR)/ringTarget.O3.o $(C_BUILDDIR)/ringProjection.O3.o $(C_BUILDDIR)/ringSoundControl.O3.o $(C_BUILDDIR)/ringIdentity.O3.o $(C_BUILDDIR)/ringMap.O3.o $(C_BUILDDIR)/soundQueries.o $(C_BUILDDIR)/collision.o $(C_BUILDDIR)/oam.O3.o : $(C_BUILDDIR)/%.o : $(C_SUBDIR)/%.c Makefile
	@$(CPP) -MMD -MT $@ $(CPPFLAGS) $< -o $(C_BUILDDIR)/$*.i
	$(BIN_DIR)/./$(PREFIX)gcc -mcpu=arm7tdmi $(RING_FAST_MODE) -mthumb-interwork $(RING_FAST_OPT) -fwrapv -fno-toplevel-reorder -fno-strict-aliasing -ffreestanding -std=gnu89 -Wa,--defsym,MKSC_RING=1 -x cpp-output -c $(C_BUILDDIR)/$*.i -o $@
endif

$(C_BUILDDIR)/%.O3.o : $(C_SUBDIR)/%.O3.c
	@$(CPP) -MMD -MT $@ $(CPPFLAGS) $< -o $(C_BUILDDIR)/$*.i
	@$(AGBCC) $(C_BUILDDIR)/$*.i $(CFLAGS) -O3 -o $(C_BUILDDIR)/$*.s
	@echo -e ".text\n\t.align\t2, 0\n" >> $(C_BUILDDIR)/$*.s
	@sed -i -e 's/\.align\t2/\.align\t2, 0/' $(C_BUILDDIR)/$*.s
	$(AS) $(ASFLAGS) -o $@ $(C_BUILDDIR)/$*.s

$(C_BUILDDIR)/%.o : $(C_SUBDIR)/%.c
	@$(CPP) -MMD -MT $@ $(CPPFLAGS) $< -o $(C_BUILDDIR)/$*.i
	@$(AGBCC) $(C_BUILDDIR)/$*.i $(CFLAGS) -o $(C_BUILDDIR)/$*.s
	@echo -e ".text\n\t.align\t2, 0\n" >> $(C_BUILDDIR)/$*.s
	@sed -i -e 's/\.align\t2/\.align\t2, 0/' $(C_BUILDDIR)/$*.s
	$(AS) $(ASFLAGS) -o $@ $(C_BUILDDIR)/$*.s

$(SOUND_BUILDDIR)/%.o: $(SOUND_SUBDIR)/%.aif
	$(AIF2PCM) $< $(patsubst %.o,%,$@)
	$(BIN2S) -H $(@).h $(patsubst %.o,%,$@) | $(AS) -o $(@)

$(SOUND_BUILDDIR)/%.o: $(SOUND_SUBDIR)/%.s
	$(AS) $(ASFLAGS) -o $@ $<

$(ASM_BUILDDIR)/%.o: $(ASM_SUBDIR)/%.s
	$(AS) $(ASFLAGS) -o $@ $<

ifeq ($(RING),1)
$(OBJ_DIR)/retro-battle.stamp: baserom.gba tools/ring/restore_battle_assets.py tools/ring/fixtures/snes-battle-restoration.bps tools/ring/restore_sunset_assets.py tools/ring/fixtures/japanese-sunset-art.bps
	python3 tools/ring/restore_battle_assets.py --rom baserom.gba --output $(OBJ_DIR) $(if $(filter japanese,$(RING_SUNSET_ART)),--japanese-sunset)
	@touch $@

$(OBJ_DIR)/data/courseData.o: $(OBJ_DIR)/retro-battle.stamp
	$(BIN2S) -H $(@).h $(OBJ_DIR)/courseData.bin | $(AS) -o $(@)

$(OBJ_DIR)/asm/ringBattleData.o: $(OBJ_DIR)/retro-battle.stamp
endif

$(DATA_BUILDDIR)/%.o : $(DATA_SUBDIR)/%.bin
	@echo $(notdir $<)
	@$(bin2o)

$(ELF): $(OBJS) $(LD_SCRIPT) $(SYMBOL_FILE)
	cd $(OBJ_DIR) && $(LD) -Map ../$(MAP) -T ../$(LD_SCRIPT) -o ../$@ $(filter-out --just-symbols=../symbols.txt,$(LDFLAGS)) --just-symbols=../$(SYMBOL_FILE) $(OBJS_REL)

$(MAP): rom

$(SYMS): $(ELF)
	$(NM) -Ung $< > $@

$(ROM): $(ELF)
	$(OBJCOPY) -O binary $< $@
	$(FIX) $@ -p -t"$(TITLE)" -c$(GAME_CODE) -m$(MAKER_CODE) -r$(REVISION) --silent

ifeq ($(RING_MUSIC_RESTORE),1)
ifeq ($(RING),1)
include build-ring/restored-audio/objects.mk
$(OBJ_DIR)/asm/ringRestoredAudio.o: build-ring/restored-audio/samples.bin
endif
endif

-include $(C_DEPS)

# The OAM renderer embeds this assembly through asm_unified.
$(C_BUILDDIR)/oam.O3.o: nonmatching/text0802F6BC.s

$(C_BUILDDIR)/camera.o: nonmatching/ringCamera.s nonmatching/camProjectActor.s

$(ASM_BUILDDIR)/m4a_1.o: Makefile

$(C_BUILDDIR)/ringIdentity.O3.o: src/ringIdentityArt.h
