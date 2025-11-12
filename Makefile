# Name of the output binary
TARGET := vulkan

# Variables
CXX := g++
CC := gcc

# Build configuration (debug by default, use 'make release' for optimized build)
BUILD_TYPE ?= debug

ifeq ($(BUILD_TYPE),release)
    CXXFLAGS := -Wextra -Wpedantic -flto -std=c++20 -Os -I./inc -I./inc/GL -ffunction-sections -fdata-sections -fno-common
    CFLAGS := -Wall -Os -I./inc -I./inc/GL -ffunction-sections -fdata-sections -fno-common
    LDFLAGS := -flto -lvulkan -lSDL3 -s -Wl,--gc-sections
else
    CXXFLAGS := -Wextra -Wpedantic -flto -std=c++20 -g -I./inc -I./inc/GL -D_GLIBCXX_DEBUG -fno-common
    CFLAGS := -Wall -I./inc -g -I./inc/GL -D_GLIBCXX_DEBUG -fno-common
    LDFLAGS := -flto -lvulkan -lSDL3
endif

GLSLC := glslc

# Detect source and header files
CPP_SOURCES := $(wildcard *.cpp)
C_SOURCES := $(wildcard *.c)
HEADERS := $(wildcard *.h)
OBJ_DIR := obj
OBJECTS := $(addprefix $(OBJ_DIR)/,$(CPP_SOURCES:.cpp=.o)) $(addprefix $(OBJ_DIR)/,$(C_SOURCES:.c=.o))

# Shader files
VERTEX_SHADERS := $(wildcard *.vert)
FRAGMENT_SHADERS := $(wildcard *.frag)
COMPUTE_SHADERS := $(wildcard *.comp)
MESH_SHADERS := $(wildcard *.mesh)
GLSL_INCLUDES := $(wildcard *.glsl)
SPIRV := $(VERTEX_SHADERS:.vert=.vert.spv) $(FRAGMENT_SHADERS:.frag=.frag.spv) $(COMPUTE_SHADERS:.comp=.comp.spv) $(MESH_SHADERS:.mesh=.mesh.spv)

# Windows cross-compilation variables
MINGW_CXX := x86_64-w64-mingw32-g++
MINGW_CC := x86_64-w64-mingw32-gcc
WIN_TARGET := $(TARGET).exe
WIN_OBJ_DIR := obj-win
WIN_OBJECTS := $(addprefix $(WIN_OBJ_DIR)/,$(CPP_SOURCES:.cpp=.o)) $(addprefix $(WIN_OBJ_DIR)/,$(C_SOURCES:.c=.o))

ifeq ($(BUILD_TYPE),release)
    WIN_LDFLAGS := -static-libgcc -static-libstdc++ -flto -Wl,-Bstatic -lSDL3 /usr/x86_64-w64-mingw32/lib/libwinpthread.a -Wl,-Bdynamic -lvulkan-1 -lwinmm -limm32 -lversion -lsetupapi -lole32 -loleaut32 -luuid -lgdi32 -lcfgmgr32 -s -Wl,--gc-sections
else
    WIN_LDFLAGS := -static-libgcc -static-libstdc++ -flto -Wl,-Bstatic -lSDL3 /usr/x86_64-w64-mingw32/lib/libwinpthread.a -Wl,-Bdynamic -lvulkan-1 -lwinmm -limm32 -lversion -lsetupapi -lole32 -loleaut32 -luuid -lgdi32 -lcfgmgr32
endif

# Rules
.PHONY: all clean windows clean-windows release windows-release windows-distrib

all: $(OBJ_DIR) $(TARGET) $(SPIRV)

windows: $(WIN_OBJ_DIR) $(WIN_TARGET) $(SPIRV)

release:
	$(MAKE) BUILD_TYPE=release all

windows-release:
	$(MAKE) BUILD_TYPE=release windows

windows-distrib: windows
	cp /usr/x86_64-w64-mingw32/bin/libwinpthread-1.dll .
	cp /usr/x86_64-w64-mingw32/bin/vulkan-1.dll .
	cp /usr/x86_64-w64-mingw32/bin/libssp-0.dll .
	zip -9 vulkan.zip $(WIN_TARGET) vulkan.tga *.spv libwinpthread-1.dll vulkan-1.dll libssp-0.dll

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(TARGET): $(OBJECTS)
	$(CXX) $^ $(LDFLAGS) -o $@

$(OBJ_DIR)/%.o: %.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

%.vert.spv: %.vert $(GLSL_INCLUDES)
	$(GLSLC) $(GLSLCFLAGS) $< -o $@

%.frag.spv: %.frag $(GLSL_INCLUDES)
	$(GLSLC) $(GLSLCFLAGS) $< -o $@

%.comp.spv: %.comp $(GLSL_INCLUDES)
	$(GLSLC) $(GLSLCFLAGS) $< -o $@

%.mesh.spv: %.mesh $(GLSL_INCLUDES)
	$(GLSLC) $(GLSLCFLAGS) --target-env=vulkan1.2 -fshader-stage=mesh $< -o $@

# Windows build rules
$(WIN_OBJ_DIR):
	mkdir -p $(WIN_OBJ_DIR)

$(WIN_TARGET): $(WIN_OBJECTS)
	$(MINGW_CXX) $^ $(WIN_LDFLAGS) -o $@

$(WIN_OBJ_DIR)/%.o: %.cpp $(HEADERS)
	$(MINGW_CXX) $(CXXFLAGS) -c $< -o $@

$(WIN_OBJ_DIR)/%.o: %.c $(HEADERS)
	$(MINGW_CC) $(CFLAGS) -c $< -o $@

clean-windows:
	rm -rf $(WIN_TARGET) $(WIN_OBJ_DIR) libwinpthread-1.dll vulkan-1.dll libssp-0.dll vulkan.zip

clean:
	rm -rf $(TARGET) $(OBJ_DIR) *.spv
	$(MAKE) clean-windows