# ==============================================================================
# lzmalloc v0.2.0 - Build System Orchestrator
# @details Manages multi-profile compilation (Release/Debug) using CMake 
# out-of-source builds and native parallelism detection.
# ==============================================================================

# Compiler definition
CC := clang
# Target build directories
BUILD_DIR_RELEASE = build/release
BUILD_DIR_DEBUG   = build/debug

# Standard phony targets
.PHONY: all release debug clean install

# @target all: Default build profile
all: release

# @target release: Compiles with high optimization (-O3, LTO)
release:
	@echo "=== [lzmalloc] Orchestrating Silicon Architecture (Release) ==="
	@mkdir -p $(BUILD_DIR_RELEASE)
	@cd $(BUILD_DIR_RELEASE) && cmake -DCMAKE_BUILD_TYPE=Release ../../
	@$(MAKE) -C $(BUILD_DIR_RELEASE) -j$(shell nproc 2>/dev/null || sysctl -n hw.ncpu)
	@echo "=== Build Successful. Artifact: $(BUILD_DIR_RELEASE)/liblzmalloc.so ==="

# @target debug: Compiles with full symbols and zero optimization
debug:
	@echo "=== [lzmalloc] Orchestrating Silicon Architecture (Debug) ==="
	@mkdir -p $(BUILD_DIR_DEBUG)
	@cd $(BUILD_DIR_DEBUG) && cmake -DCMAKE_BUILD_TYPE=Debug ../../
	@$(MAKE) -C $(BUILD_DIR_DEBUG) -j$(shell nproc 2>/dev/null || sysctl -n hw.ncpu)

# @target clean: Wipes all build artifacts
clean:
	@echo "=== Purging build artifacts ==="
	@rm -rf build

# @target test: Executes CTest suite under the Debug profile
test: debug
	@echo "=== Running Tests Suite via CTest ==="
	@cd $(BUILD_DIR_DEBUG) && ctest --output-on-failure -V

# @target install: Deploys library to system directories and updates ldcache
install: release
	@echo "=== Deploying lzmalloc to System Library Path ==="
	@cd $(BUILD_DIR_RELEASE) && sudo cmake --install .
	@echo "Updating dynamic linker cache (ldconfig)..."
	@sudo ldconfig 2>/dev/null || true
	@echo "=== Installation Completed ==="