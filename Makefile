# ==============================================================================
# lzmalloc v0.2.0 - Makefile Orchestrator
# ==============================================================================

# Directorios de compilación aislados
CC := clang
BUILD_DIR_RELEASE = build/release
BUILD_DIR_DEBUG   = build/debug

# Reglas falsas (no corresponden a archivos)
.PHONY: all release debug clean install

# Objetivo por defecto
all: release

release:
	@echo "=== [lzmalloc] Ensamblando arquitectura de silicio (Release) ==="
	@mkdir -p $(BUILD_DIR_RELEASE)
	@cd $(BUILD_DIR_RELEASE) && cmake -DCMAKE_BUILD_TYPE=Release ../../
	@$(MAKE) -C $(BUILD_DIR_RELEASE) -j$(shell nproc 2>/dev/null || sysctl -n hw.ncpu)
	@echo "=== Compilación exitosa. Artefacto: $(BUILD_DIR_RELEASE)/liblzmalloc.so ==="

debug:
	@echo "=== [lzmalloc] Ensamblando arquitectura de silicio (Debug) ==="
	@mkdir -p $(BUILD_DIR_DEBUG)
	@cd $(BUILD_DIR_DEBUG) && cmake -DCMAKE_BUILD_TYPE=Debug ../../
	@$(MAKE) -C $(BUILD_DIR_DEBUG) -j$(shell nproc 2>/dev/null || sysctl -n hw.ncpu)

clean:
	@echo "=== Limpiando artefactos generados ==="
	@rm -rf build

install: release
	@echo "=== Instalando lzmalloc a nivel global del SO ==="
	@cd $(BUILD_DIR_RELEASE) && sudo cmake --install .
	@echo "Actualizando caché del enlazador dinámico (ldconfig)..."
	@sudo ldconfig 2>/dev/null || true
	@echo "=== Instalación completada ==="