# Simple Makefile wrapper for CMake
.PHONY: all clean release run help

all:
	@mkdir -p build
	@cd build && cmake -DCMAKE_BUILD_TYPE=Debug .. && cmake --build .
	@echo "✓ Build complete: build/vulkan-demo"

release:
	@mkdir -p build
	@cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && cmake --build .
	@echo "✓ Release build complete: build/vulkan-demo"

clean:
	@rm -rf build
	@echo "✓ Cleaned build directory"

run: all
	@build/vulkan-demo

help:
	@echo "Available targets:"
	@echo "  all (default) - Build debug version"
	@echo "  release       - Build optimized version"
	@echo "  clean         - Remove build directory"
	@echo "  run           - Build and run"
	@echo "  help          - Show this help"