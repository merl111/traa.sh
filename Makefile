# traa.sh — simple build wrappers around CMake
BUILD_DIR ?= build
GENERATOR ?= Ninja
CMAKE ?= cmake
CTEST ?= ctest
PREFIX ?= $(HOME)/.local
PYTHON ?= python3
DOCS_PORT ?= 8080

.PHONY: all configure build run test demo clean install install-user embed-icon docs-check docs-preview help

all: build

configure:
	$(CMAKE) -B $(BUILD_DIR) -G $(GENERATOR) -DTRAASH_BUILD_TESTS=ON

build: configure
	$(CMAKE) --build $(BUILD_DIR)

run: build
	./$(BUILD_DIR)/traash

demo: build
	./$(BUILD_DIR)/traash --demo

test: build
	$(CTEST) --test-dir $(BUILD_DIR) --output-on-failure

# Rebuild src/assets/icon_embedded.c from assets/icons/traash.png (needs Pillow)
embed-icon:
	$(PYTHON) tools/embed_icon.py

# Install binary + lua + icons + .desktop (default: ~/.local)
install: build
	$(CMAKE) --install $(BUILD_DIR) --prefix "$(PREFIX)"
	-@update-desktop-database "$(PREFIX)/share/applications" 2>/dev/null || true
	-@gtk-update-icon-cache -f -t "$(PREFIX)/share/icons/hicolor" 2>/dev/null || true
	@echo "Installed to $(PREFIX) — relaunch from the app menu or PATH"

# Alias for clarity
install-user: install

docs-check:
	$(PYTHON) tools/check_docs.py

docs-preview: docs-check
	@echo "Serving docs at http://localhost:$(DOCS_PORT)/"
	$(PYTHON) -m http.server -d docs $(DOCS_PORT)

clean:
	rm -rf $(BUILD_DIR)

help:
	@echo "Targets: all configure build run demo test install install-user embed-icon docs-check docs-preview clean"
	@echo "Vars:    BUILD_DIR=$(BUILD_DIR) GENERATOR=$(GENERATOR) PREFIX=$(PREFIX) DOCS_PORT=$(DOCS_PORT)"
	@echo ""
	@echo "  embed-icon    Regenerate baked window-icon pixels from traash.png"
	@echo "  install       Install app + desktop entry + hicolor icons to PREFIX"
	@echo "  docs-check    Validate internal HTML, asset, and anchor links in docs/"
	@echo "  docs-preview  Serve docs/ at http://localhost:$(DOCS_PORT)/"
