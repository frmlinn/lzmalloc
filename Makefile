# =========================================================================
# lzmalloc - Wrapper Makefile for CMake automation
# =========================================================================

# Force Clang by default, but allow overrides
CC ?= clang
BUILD_DIR_RELEASE = build/release
BUILD_DIR_DEBUG = build/debug

.PHONY: all release debug clean

# By default, compile the hyper-optimized release version
all: release

release:
	@echo "=== Configuring lzmalloc (RELEASE Mode) ==="
	@mkdir -p $(BUILD_DIR_RELEASE)
	@cd $(BUILD_DIR_RELEASE) && cmake -DCMAKE_C_COMPILER=$(CC) -DCMAKE_BUILD_TYPE=Release ../..
	@echo "=== Compiling lzmalloc ==="
	@$(MAKE) --no-print-directory -C $(BUILD_DIR_RELEASE)
	@echo "Done. Library generated at: $(BUILD_DIR_RELEASE)/liblzmalloc.so"

debug:
	@echo "=== Configuring lzmalloc (DEBUG Mode) ==="
	@mkdir -p $(BUILD_DIR_DEBUG)
	@cd $(BUILD_DIR_DEBUG) && cmake -DCMAKE_C_COMPILER=$(CC) -DCMAKE_BUILD_TYPE=Debug ../..
	@echo "=== Compiling lzmalloc ==="
	@$(MAKE) --no-print-directory -C $(BUILD_DIR_DEBUG)
	@echo "Done. Library generated at: $(BUILD_DIR_DEBUG)/liblzmalloc.so"

clean:
	@echo "=== Cleaning build environment ==="
	@rm -rf build
	@echo "Directory 'build/' successfully removed."