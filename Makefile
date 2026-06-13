# ---------------------------------------------------------------------------
# Space Harrier-style Mega Drive prototype - build wrapper around SGDK
#
# Targets:
#   make / make build   build out/rom.bin
#   make clean          remove build output
#   make run            build then launch in emulator (blastem if found, else ares)
#   make run-ares       build then launch in ares explicitly
#   make rebuild-run    clean + build + run
#   make sgdk-lib       (re)build the SGDK library itself (one-time setup)
# ---------------------------------------------------------------------------

GDK ?= $(CURDIR)/tools/sgdk
TOOLS_BIN := $(CURDIR)/tools/bin

# Homebrew OpenJDK location on macOS (keg-only, not on PATH by default).
# Harmless on other platforms where this directory does not exist.
JAVA_BIN := /opt/homebrew/opt/openjdk/bin

export GDK
export PATH := $(TOOLS_BIN):$(JAVA_BIN):$(PATH)

# SGDK was written for older GCC; gnu11 keeps its 'bool' typedef working on GCC >= 15.
SGDK_EXTRA_FLAGS := -std=gnu11

ROM := out/rom.bin

# --- Emulator detection -----------------------------------------------------
UNAME := $(shell uname -s)
BLASTEM := $(shell command -v blastem 2>/dev/null)
ifeq ($(UNAME),Darwin)
ARES := /Applications/ares.app/Contents/MacOS/ares
else
ARES := $(shell command -v ares 2>/dev/null)
endif

ifneq ($(BLASTEM),)
# Force PAL: the ROM targets 320x240 (V30), which needs a 50 Hz machine
EMU := $(BLASTEM)
EMUFLAGS := -r E
else
# ares picks PAL automatically from the ROM header region field (E)
EMU := $(ARES)
EMUFLAGS :=
endif

.PHONY: build clean run run-ares rebuild-run sgdk-lib

build:
	$(MAKE) -f $(GDK)/makefile.gen EXTRA_FLAGS="$(SGDK_EXTRA_FLAGS)" release

clean:
	$(MAKE) -f $(GDK)/makefile.gen clean

run: build
	$(EMU) $(EMUFLAGS) $(ROM)

run-ares: build
	$(ARES) $(ROM)

rebuild-run: clean run

# One-time: build SGDK's own library (after cloning SGDK into tools/sgdk and
# building tools/bin/sjasm + tools/bin/bintos, see README).
sgdk-lib:
	$(MAKE) -C $(GDK) -f makelib.gen EXTRA_FLAGS="$(SGDK_EXTRA_FLAGS)"
