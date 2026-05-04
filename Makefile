# =============================================================================
# Pilocows — top-level Makefile
# =============================================================================
# Targets:
#
#   Build:
#     make build-handheld          compile firmware → handheld/.pio/build/sc01plus/
#     make build-backend           cargo build --release
#     make build-frontend          tauri build (produces .dmg / .msi)
#     make build-all               all three
#     make flash                   build + flash handheld over USB
#
#   Release  (requires: gh CLI authenticated):
#     make release-handheld        tag handheld-vX.Y.Z + upload firmware binaries
#     make release-backend         tag backend-vX.Y.Z  + upload binary
#     make release-frontend        tag frontend-vX.Y.Z + upload installer
#
#   Bump version (edit canonical source, then commit):
#     make bump-handheld  V=1.1.0  update handheld/VERSION
#     make bump-backend   V=1.1.0  update backend/Cargo.toml
#     make bump-frontend  V=1.1.0  update frontend/package.json + tauri.conf.json
#
#   Info:
#     make versions                print current versions
# =============================================================================

# --- Read versions from each sub-project's canonical source ----------------

HANDHELD_VERSION := $(shell cat handheld/VERSION 2>/dev/null | tr -d '[:space:]')
BACKEND_VERSION  := $(shell grep '^version' backend/Cargo.toml | head -1 | sed 's/version = "\(.*\)"/\1/')
FRONTEND_VERSION := $(shell node -p "require('./frontend/package.json').version" 2>/dev/null)

# --- Artifact paths --------------------------------------------------------

HANDHELD_DIR        := handheld/.pio/build/sc01plus
HANDHELD_BOOTLOADER := $(HANDHELD_DIR)/bootloader.bin
HANDHELD_PARTITIONS := $(HANDHELD_DIR)/partitions.bin
HANDHELD_OTA_DATA   := $(HANDHELD_DIR)/ota_data_initial.bin
HANDHELD_FIRMWARE   := $(HANDHELD_DIR)/firmware.bin

BACKEND_BIN         := backend/target/release/pilocows-backend

# Tauri bundle directory varies by platform
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  FRONTEND_BUNDLE_EXT := dmg
else
  FRONTEND_BUNDLE_EXT := msi
endif
FRONTEND_BUNDLE_DIR := frontend/src-tauri/target/release/bundle/$(FRONTEND_BUNDLE_EXT)

# ---------------------------------------------------------------------------

.PHONY: build-handheld build-backend build-frontend build-all flash
.PHONY: release-handheld release-backend release-frontend
.PHONY: bump-handheld bump-backend bump-frontend
.PHONY: versions help

# =============================================================================
# Build
# =============================================================================

build-handheld:
	cd handheld && pio run -e sc01plus

build-backend:
	cd backend && cargo build --release

build-frontend:
	cd frontend && npm run tauri build

build-all: build-handheld build-backend build-frontend

## Build + flash handheld over USB
flash:
	cd handheld && pio run -e sc01plus -t upload

# =============================================================================
# Release
# =============================================================================

release-handheld: build-handheld
	@echo "→ Releasing handheld-v$(HANDHELD_VERSION) ..."
	@for f in "$(HANDHELD_BOOTLOADER)" "$(HANDHELD_PARTITIONS)" "$(HANDHELD_OTA_DATA)" "$(HANDHELD_FIRMWARE)"; do \
		if [ ! -f "$$f" ]; then echo "Missing artifact: $$f"; exit 1; fi; \
	done
	gh release create handheld-v$(HANDHELD_VERSION) \
		--title "Handheld v$(HANDHELD_VERSION)" \
		--notes "## Handheld Firmware v$(HANDHELD_VERSION)\n\nFlash all four binaries via USB:\n\`\`\`\nmake flash\n\`\`\`\nOr manually with esptool — see README." \
		"$(HANDHELD_BOOTLOADER)#bootloader.bin" \
		"$(HANDHELD_PARTITIONS)#partitions.bin" \
		"$(HANDHELD_OTA_DATA)#ota_data_initial.bin" \
		"$(HANDHELD_FIRMWARE)#firmware.bin"

release-backend: build-backend
	@echo "→ Releasing backend-v$(BACKEND_VERSION) ..."
	@if [ ! -f "$(BACKEND_BIN)" ]; then echo "Missing artifact: $(BACKEND_BIN)"; exit 1; fi
	gh release create backend-v$(BACKEND_VERSION) \
		--title "Backend v$(BACKEND_VERSION)" \
		--notes "## Backend v$(BACKEND_VERSION)\n\nStandalone Rust/Axum REST API. Requires no setup — database is created automatically on first run.\n\`\`\`\n./pilocows-backend\n\`\`\`" \
		"$(BACKEND_BIN)#pilocows-backend"

release-frontend: build-frontend
	@echo "→ Releasing frontend-v$(FRONTEND_VERSION) ..."
	@INSTALLER=$$(find "$(FRONTEND_BUNDLE_DIR)" \
		-name "Pilocows_$(FRONTEND_VERSION)_*.$(FRONTEND_BUNDLE_EXT)" 2>/dev/null | head -1); \
	if [ -z "$$INSTALLER" ]; then \
		echo "ERROR: no installer found in $(FRONTEND_BUNDLE_DIR)"; exit 1; \
	fi; \
	gh release create frontend-v$(FRONTEND_VERSION) \
		--title "Frontend v$(FRONTEND_VERSION)" \
		--notes "## Desktop App v$(FRONTEND_VERSION)\n\nTauri + React desktop application (macOS .dmg / Windows .msi)." \
		"$$INSTALLER"

# =============================================================================
# Bump versions
# =============================================================================

bump-handheld:
	@if [ -z "$(V)" ]; then echo "Usage: make bump-handheld V=x.y.z"; exit 1; fi
	@printf '%s\n' "$(V)" > handheld/VERSION
	@echo "handheld → $(V)"

bump-backend:
	@if [ -z "$(V)" ]; then echo "Usage: make bump-backend V=x.y.z"; exit 1; fi
	@python3 -c "\
import re, sys; \
f = 'backend/Cargo.toml'; \
t = open(f).read(); \
t = re.sub(r'^version = \"[^\"]+\"', 'version = \"$(V)\"', t, count=1, flags=re.M); \
open(f, 'w').write(t)"
	@echo "backend  → $(V)"

bump-frontend:
	@if [ -z "$(V)" ]; then echo "Usage: make bump-frontend V=x.y.z"; exit 1; fi
	@python3 -c "\
import json; \
f = 'frontend/package.json'; \
p = json.load(open(f)); \
p['version'] = '$(V)'; \
open(f, 'w').write(json.dumps(p, indent=2) + '\n')"
	@python3 -c "\
import json; \
f = 'frontend/src-tauri/tauri.conf.json'; \
p = json.load(open(f)); \
p['version'] = '$(V)'; \
open(f, 'w').write(json.dumps(p, indent=2) + '\n')"
	@echo "frontend → $(V)"

# =============================================================================
# Info
# =============================================================================

versions:
	@echo "handheld : $(HANDHELD_VERSION)"
	@echo "backend  : $(BACKEND_VERSION)"
	@echo "frontend : $(FRONTEND_VERSION)"

help:
	@echo ""
	@echo "Build:"
	@echo "  make build-handheld          compile firmware"
	@echo "  make build-backend           cargo build --release"
	@echo "  make build-frontend          tauri build"
	@echo "  make build-all               all three"
	@echo "  make flash                   build + flash handheld over USB"
	@echo ""
	@echo "Release  (requires gh CLI):"
	@echo "  make release-handheld        tag handheld-vX.Y.Z + upload firmware"
	@echo "  make release-backend         tag backend-vX.Y.Z  + upload binary"
	@echo "  make release-frontend        tag frontend-vX.Y.Z + upload installer"
	@echo ""
	@echo "Bump version:"
	@echo "  make bump-handheld  V=1.1.0"
	@echo "  make bump-backend   V=1.1.0"
	@echo "  make bump-frontend  V=1.1.0"
	@echo ""
	@$(MAKE) -s versions
	@echo ""
