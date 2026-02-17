cmake_minimum_required(VERSION 3.14)
project(vulkan-objects LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL3 REQUIRED sdl3)
find_package(Vulkan REQUIRED)

# Compiler flags
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CMAKE_CXX_FLAGS_RELEASE "-Os -ffunction-sections -fdata-sections -fno-common")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-s -Wl,--gc-sections")
else()
    set(CMAKE_CXX_FLAGS_DEBUG "-g -D_GLIBCXX_DEBUG -fno-common")
endif()

# Add compiler warnings
add_compile_options(-Wall -Wextra -Wpedantic)

# Source files
set(SOURCES
    src/main.cpp
    src/camera.cpp
    src/tga.cpp
    src/vkobjects.cpp
    src/shaders.cpp
    src/buffer.cpp
    src/image.cpp
    src/barrier.cpp
    src/commands.cpp
    src/frame.cpp
    src/pipeline.cpp
)

set(HEADERS
    src/camera.h
    src/math.h
    src/pch.h
    src/tga.h
    src/vkobjects.h
    src/vkinternal.h
)

# Create executable
add_executable(vulkan ${SOURCES} ${HEADERS})

# Precompiled header
target_precompile_headers(vulkan PRIVATE src/pch.h)

# Include directories
target_include_directories(vulkan PRIVATE
    src
    ${SDL3_INCLUDE_DIRS}
    ${Vulkan_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(vulkan PRIVATE 
    ${SDL3_LIBRARIES}
    Vulkan::Vulkan
)

# Find glslc for shader compilation
find_program(GLSLC glslc REQUIRED)

# Shader compilation — SPV output next to source
set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders)

set(FRAGMENT_SHADERS shadow.frag blit.frag)
set(COMPUTE_SHADERS cubes.comp)
set(MESH_SHADERS cube.mesh shadow.mesh fullscreen.mesh)

# Compile fragment shaders
foreach(SHADER ${FRAGMENT_SHADERS})
    set(SHADER_SOURCE ${SHADER_DIR}/${SHADER})
    set(SHADER_OUTPUT ${SHADER_DIR}/${SHADER}.spv)
    add_custom_command(
        OUTPUT ${SHADER_OUTPUT}
        COMMAND ${GLSLC} --target-env=vulkan1.2 ${SHADER_SOURCE} -o ${SHADER_OUTPUT}
        DEPENDS ${SHADER_SOURCE}
        COMMENT "Compiling fragment shader ${SHADER}"
    )
    list(APPEND SPIRV_BINARIES ${SHADER_OUTPUT})
endforeach()

# Compile compute shaders
foreach(SHADER ${COMPUTE_SHADERS})
    set(SHADER_SOURCE ${SHADER_DIR}/${SHADER})
    set(SHADER_OUTPUT ${SHADER_DIR}/${SHADER}.spv)
    add_custom_command(
        OUTPUT ${SHADER_OUTPUT}
        COMMAND ${GLSLC} --target-env=vulkan1.2 ${SHADER_SOURCE} -o ${SHADER_OUTPUT}
        DEPENDS ${SHADER_SOURCE}
        COMMENT "Compiling compute shader ${SHADER}"
    )
    list(APPEND SPIRV_BINARIES ${SHADER_OUTPUT})
endforeach()

# Compile mesh shaders
foreach(SHADER ${MESH_SHADERS})
    set(SHADER_SOURCE ${SHADER_DIR}/${SHADER})
    set(SHADER_OUTPUT ${SHADER_DIR}/${SHADER}.spv)
    add_custom_command(
        OUTPUT ${SHADER_OUTPUT}
        COMMAND ${GLSLC} --target-env=vulkan1.2 -fshader-stage=mesh ${SHADER_SOURCE} -o ${SHADER_OUTPUT}
        DEPENDS ${SHADER_SOURCE}
        COMMENT "Compiling mesh shader ${SHADER}"
    )
    list(APPEND SPIRV_BINARIES ${SHADER_OUTPUT})
endforeach()

# Add custom target for shaders
add_custom_target(shaders ALL DEPENDS ${SPIRV_BINARIES})

# Make the main target depend on shaders
add_dependencies(vulkan shaders)
