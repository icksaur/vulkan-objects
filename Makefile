# Name of the output binary
TARGET := vulkan

# Variables
CXX := g++
CC := gcc
CXXFLAGS := -Wextra -Wpedantic -flto -std=c++20 -g -I./inc -I./inc/GL -D_GLIBCXX_DEBUG -fno-common
CFLAGS := -Wall -I./inc -g -I./inc/GL -D_GLIBCXX_DEBUG -fno-common
LDFLAGS := -flto -lvulkan -lSDL3
GLSLC := glslc

# Detect source and header files
CPP_SOURCES := $(wildcard *.cpp)
C_SOURCES := $(wildcard *.c)
HEADERS := $(wildcard *.h)
OBJ_DIR := obj
OBJECTS := $(addprefix $(OBJ_DIR)/,$(CPP_SOURCES:.cpp=.o)) $(addprefix $(OBJ_DIR)/,$(C_SOURCES:.c=.o))
VERTEX_SHADERS := $(wildcard *.vert)
FRAGMENT_SHADERS := $(wildcard *.frag)
COMPUTE_SHADERS := $(wildcard *.comp)
MESH_SHADERS := $(wildcard *.mesh)
GLSL_INCLUDES := $(wildcard *.glsl)
SPIRV := $(VERTEX_SHADERS:.vert=.vert.spv) $(FRAGMENT_SHADERS:.frag=.frag.spv) $(COMPUTE_SHADERS:.comp=.comp.spv) $(MESH_SHADERS:.mesh=.mesh.spv)

# Rules
.PHONY: all clean

all: $(OBJ_DIR) $(TARGET) $(SPIRV)

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

clean:
	rm -rf $(TARGET) $(OBJ_DIR) *.spv