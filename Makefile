# ---------------------------------------------------------------------------
# Space Harrier-style Mega Drive prototype - build wrapper around SGDK
#
# Targets:
#   make / make build   build out/rom.bin
#   make clean          remove build output
#   make run            build then launch in emulator (blastem if found, else ares)
#   make run-ares       build then launch in ares explicitly
#   make rebuild-run    clean + build + run
#   make sgdk-lib       BLOCKED — see README (Homebrew GCC breaks XGM2)
#   make restore-sgdk-lib  copy libmd.a from ghcr.io/stephane-d/sgdk:latest
# ---------------------------------------------------------------------------

GDK ?= $(CURDIR)/tools/sgdk
TOOLS_BIN := $(CURDIR)/tools/bin

# Homebrew OpenJDK location on macOS (keg-only, not on PATH by default).
JAVA_BIN := /opt/homebrew/opt/openjdk/bin

export GDK
export PATH := $(TOOLS_BIN):$(JAVA_BIN):$(PATH)

# SGDK was written for older GCC; gnu11 keeps its 'bool' typedef working on GCC >= 15.
SGDK_EXTRA_FLAGS := -std=gnu11

ROM := out/rom.bin
SGDK_IMAGE := ghcr.io/stephane-d/sgdk:latest
DOCKER := $(shell command -v docker 2>/dev/null)
DOCKER_HOST_SOCK := $(HOME)/.colima/default/docker.sock
UIDGID := $(shell id -u):$(shell id -g)

# --- Emulator detection -----------------------------------------------------
UNAME := $(shell uname -s)
BLASTEM := $(shell command -v blastem 2>/dev/null)
ifeq ($(UNAME),Darwin)
ARES := /Applications/ares.app/Contents/MacOS/ares
else
ARES := $(shell command -v ares 2>/dev/null)
endif

ifneq ($(BLASTEM),)
EMU := $(BLASTEM)
EMUFLAGS := -r U
else
EMU := $(ARES)
EMUFLAGS :=
endif

define DOCKER_BUILD
$(if $(DOCKER),\
  DOCKER_HOST=unix://$(DOCKER_HOST_SOCK) docker run --rm --platform linux/amd64 \
    -v "$(CURDIR)":/src -w /src -u $(UIDGID) --entrypoint sh $(SGDK_IMAGE) \
    -c 'make -f /src/tools/sgdk/makefile.gen EXTRA_FLAGS="$(SGDK_EXTRA_FLAGS)" $(1)',\
  $(error docker not found — install colima + docker, or use CI ROM builds))
endef

.PHONY: build clean run run-ares rebuild-run sgdk-lib restore-sgdk-lib

build:
	$(call DOCKER_BUILD,release)

clean:
	$(MAKE) -f $(GDK)/makefile.gen clean

run: build
	$(EMU) $(EMUFLAGS) $(ROM)

run-ares: build
	$(ARES) $(ROM)

rebuild-run: clean run

restore-sgdk-lib:
	@test -n "$(DOCKER)" || (echo "docker required"; exit 1)
	cp -av $(GDK)/lib/libmd.a $(GDK)/lib/libmd.a.broken-local
	cid=$$(DOCKER_HOST=unix://$(DOCKER_HOST_SOCK) docker create $(SGDK_IMAGE)); \
	DOCKER_HOST=unix://$(DOCKER_HOST_SOCK) docker cp $$cid:/sgdk/lib/libmd.a $(GDK)/lib/libmd.a; \
	DOCKER_HOST=unix://$(DOCKER_HOST_SOCK) docker cp $$cid:/sgdk/lib/libmd_debug.a $(GDK)/lib/libmd_debug.a; \
	DOCKER_HOST=unix://$(DOCKER_HOST_SOCK) docker rm $$cid
	@strings $(GDK)/lib/libmd.a | grep -m1 "GCC: "

sgdk-lib:
	@echo "ERROR: do not run make sgdk-lib with Homebrew m68k-elf-gcc (16.x)."
	@echo "It miscompiles SGDK's XGM2 driver — PCM crashes on playback."
	@echo "Restore the Docker-built library instead: make restore-sgdk-lib"
	@echo "Or rebuild lib inside Docker: docker run ... make -f makelib.gen release"
	@exit 1
