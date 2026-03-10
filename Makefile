SHELL := /bin/bash

ROOT_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
NUTTX_DIR := $(ROOT_DIR)/nuttx
NUTTX_APPS_DIR := $(ROOT_DIR)/nuttx-apps
BOARD_SRC_DIR := $(ROOT_DIR)/boards/arm/rp23xx/picocalc-rp2350b
BOARD_DST_DIR := $(NUTTX_DIR)/boards/arm/rp23xx/picocalc-rp2350b

NUTTX_REMOTE ?= https://github.com/apache/nuttx.git
NUTTX_APPS_REMOTE ?= https://github.com/apache/nuttx-apps.git
NUTTX_BRANCH ?= master
NUTTX_APPS_BRANCH ?= master

BOARD_CONFIG ?= picocalc-rp2350b:full
JOBS ?= $(shell nproc)

.PHONY: help setup fetch-nuttx fetch-nuttx-apps sync-board configure build rebuild \
	clean distclean menuconfig uf2 clean-deps fix-eol tips

help:
	@echo "PicoCalc-Term build targets"
	@echo "  make setup                  Install host dependencies and fetch NuttX trees"
	@echo "  make fetch-nuttx            Clone apache/nuttx into ./nuttx if missing"
	@echo "  make fetch-nuttx-apps       Clone apache/nuttx-apps into ./nuttx-apps if missing"
	@echo "  make sync-board             Copy custom board files into nuttx tree"
	@echo "  make configure              Configure NuttX (BOARD_CONFIG=picocalc-rp2350b:full)"
	@echo "  make build                  Build configured firmware"
	@echo "  make rebuild                Clean and rebuild"
	@echo "  make clean                  Clean build artifacts"
	@echo "  make distclean              Remove build artifacts and .config"
	@echo "  make menuconfig             Open Kconfig menu"
	@echo "  make uf2                    Show resulting UF2 file"
	@echo "  make clean-deps             Remove stale Make.dep/.depend files"
	@echo "  make fix-eol                Convert CRLF to LF for build files"

fetch-nuttx:
	@if [ -d "$(NUTTX_DIR)/.git" ]; then \
		echo "[OK] Existing NuttX tree: $(NUTTX_DIR)"; \
	elif [ -d "$(NUTTX_DIR)" ] && [ -n "$$({ ls -A "$(NUTTX_DIR)" 2>/dev/null || true; })" ]; then \
		echo "ERROR: $(NUTTX_DIR) exists and is not empty, refusing to overwrite."; \
		exit 1; \
	else \
		rm -rf "$(NUTTX_DIR)"; \
		echo "==> Cloning NuttX ($(NUTTX_BRANCH))"; \
		git clone --depth 1 --branch "$(NUTTX_BRANCH)" "$(NUTTX_REMOTE)" "$(NUTTX_DIR)"; \
	fi

fetch-nuttx-apps:
	@if [ -d "$(NUTTX_APPS_DIR)/.git" ]; then \
		echo "[OK] Existing NuttX apps tree: $(NUTTX_APPS_DIR)"; \
	elif [ -d "$(NUTTX_APPS_DIR)" ] && [ -n "$$({ ls -A "$(NUTTX_APPS_DIR)" 2>/dev/null || true; })" ]; then \
		echo "ERROR: $(NUTTX_APPS_DIR) exists and is not empty, refusing to overwrite."; \
		exit 1; \
	else \
		rm -rf "$(NUTTX_APPS_DIR)"; \
		echo "==> Cloning NuttX apps ($(NUTTX_APPS_BRANCH))"; \
		git clone --depth 1 --branch "$(NUTTX_APPS_BRANCH)" "$(NUTTX_APPS_REMOTE)" "$(NUTTX_APPS_DIR)"; \
	fi

sync-board: fetch-nuttx
	@echo "==> Syncing board files into NuttX tree"
	@mkdir -p "$(BOARD_DST_DIR)"
	@rsync -a --delete "$(BOARD_SRC_DIR)/" "$(BOARD_DST_DIR)/"

setup:
	@./tools/wsl/setup_nuttx_env.sh
	@$(MAKE) fetch-nuttx fetch-nuttx-apps sync-board

configure: fetch-nuttx fetch-nuttx-apps sync-board
	@if [ -f "$(NUTTX_DIR)/.config" ]; then \
		echo "==> Cleaning previous configuration"; \
		$(MAKE) -C "$(NUTTX_DIR)" distclean; \
	fi
	@echo "==> Configuring $(BOARD_CONFIG)"
	@cd "$(NUTTX_DIR)" && ./tools/configure.sh -l "$(BOARD_CONFIG)"
	@$(MAKE) -C "$(NUTTX_DIR)" olddefconfig

build: fetch-nuttx fetch-nuttx-apps sync-board
	@if [ ! -f "$(NUTTX_DIR)/.config" ]; then \
		echo "ERROR: .config missing. Run 'make configure BOARD_CONFIG=$(BOARD_CONFIG)' first."; \
		exit 1; \
	fi
	@$(MAKE) -C "$(NUTTX_DIR)" -j"$(JOBS)"

rebuild: clean build

clean:
	@if [ -f "$(NUTTX_DIR)/Makefile" ]; then \
		$(MAKE) -C "$(NUTTX_DIR)" clean; \
	fi

distclean:
	@if [ -f "$(NUTTX_DIR)/Makefile" ]; then \
		$(MAKE) -C "$(NUTTX_DIR)" distclean; \
	fi

menuconfig:
	@if [ ! -f "$(NUTTX_DIR)/.config" ]; then \
		echo "ERROR: .config missing. Run 'make configure' first."; \
		exit 1; \
	fi
	@$(MAKE) -C "$(NUTTX_DIR)" menuconfig

uf2:
	@if [ -f "$(NUTTX_DIR)/nuttx.uf2" ]; then \
		ls -lh "$(NUTTX_DIR)/nuttx.uf2"; \
	else \
		echo "UF2 not found at $(NUTTX_DIR)/nuttx.uf2"; \
		exit 1; \
	fi

clean-deps:
	@find "$(NUTTX_DIR)" "$(NUTTX_APPS_DIR)" -type f \( -name 'Make.dep' -o -name '.depend' \) -delete 2>/dev/null || true
	@echo "[OK] Removed stale dependency files"

fix-eol:
	@./tools/wsl/fix_line_endings.sh

tips:
	@echo "Build tips"
	@echo "  1) Run 'make setup' once per environment"
	@echo "  2) Run 'make configure BOARD_CONFIG=picocalc-rp2350b:full' once"
	@echo "  3) Use 'make build JOBS=<n>' for incremental builds"
	@echo "  4) If dependency errors appear, run 'make clean-deps'"