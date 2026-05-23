# CMake Wrapper Makefile

BUILD_DIR = build
TARGET    = fit

.PHONY: all init build clean fmt run test install uninstall

all: build

init:
	@if [ -z "$$VCPKG_ROOT" ]; then \
		echo "Error: VCPKG_ROOT environment variable is not defined."; \
		exit 1; \
	fi
	cmake -B $(BUILD_DIR) -S . -DCMAKE_TOOLCHAIN_FILE=$$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake

build:
	@if [ ! -d "$(BUILD_DIR)" ]; then \
		echo "Error: '$(BUILD_DIR)' folder missing. Run 'make init' first."; \
		exit 1; \
	fi
	cmake --build $(BUILD_DIR)

clean:
	cmake --build $(BUILD_DIR) --target clean

fmt:
	cmake --build $(BUILD_DIR) --target fmt

run: build
	./$(BUILD_DIR)/$(TARGET) status

test: build
	cd $(BUILD_DIR) && ctest --output-on-failure

install: build
	sudo cmake --install $(BUILD_DIR)

uninstall:
	@if [ -f "$(BUILD_DIR)/install_manifest.txt" ]; then \
		echo "Uninstalling binaries tracked in install_manifest.txt..."; \
		sudo xargs rm -f < $(BUILD_DIR)/install_manifest.txt; \
		echo "Clean uninstallation complete."; \
	else \
		echo "Error: install_manifest.txt not found inside '$(BUILD_DIR)'. Was the project installed?"; \
		exit 1; \
	fi
