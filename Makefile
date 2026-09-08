# Simple Makefile wrapper for CMake
.PHONY: all clean release run run-vert help

all:
	@mkdir -p build
	@cd build && cmake -DCMAKE_BUILD_TYPE=Debug .. && cmake --build .
	@echo "✓ Build complete: build/mesh_demo build/vert_demo"

release:
	@mkdir -p build
	@cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && cmake --build .
	@echo "✓ Release build complete: build/mesh_demo build/vert_demo"

clean:
	@rm -rf build
	@echo "✓ Cleaned build directory"

run: all
	@build/mesh_demo

run-vert: all
	@build/vert_demo

help:
	@echo "Available targets:"
	@echo "  all (default) - Build debug version"
	@echo "  release       - Build optimized version"
	@echo "  clean         - Remove build directory"
	@echo "  run           - Build and run mesh_demo"
	@echo "  run-vert      - Build and run vert_demo"
	@echo "  help          - Show this help"