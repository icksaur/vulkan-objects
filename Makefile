# Simple Makefile wrapper for CMake
.PHONY: all clean release run help

all:
	@mkdir -p build
	@cd build && cmake -DCMAKE_BUILD_TYPE=Debug .. && cmake --build .
	@echo "✓ Build complete: build/vulkan"

release:
	@mkdir -p build
	@cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && cmake --build .
	@echo "✓ Release build complete: build/vulkan"

clean:
	@rm -rf build
	@echo "✓ Cleaned build directory"

run: all
	@build/vulkan

help:
	@echo "Available targets:"
	@echo "  all (default) - Build debug version"
	@echo "  release       - Build optimized version"
	@echo "  clean         - Remove build directory"
	@echo "  run           - Build and run"
	@echo "  help          - Show this help"