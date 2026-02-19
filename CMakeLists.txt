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

# ---------------------------------------------------------------------------
# vkobjects — static library
# ---------------------------------------------------------------------------
set(LIB_SOURCES
    src/vkobjects.cpp
    src/shaders.cpp
    src/buffer.cpp
    src/image.cpp
    src/barrier.cpp
    src/commands.cpp
    src/frame.cpp
    src/pipeline.cpp
)

add_library(vkobjects STATIC ${LIB_SOURCES})

target_precompile_headers(vkobjects PRIVATE src/pch.h)

# include/  — public (consumers see vkobjects.h)
# src/      — private (internal headers: vkinternal.h, vk_mem_alloc.h, pch.h)
target_include_directories(vkobjects
    PUBLIC  include
    PRIVATE src
)

# SDL3 and Vulkan are PUBLIC because vkobjects.h includes their headers
target_include_directories(vkobjects PUBLIC
    ${SDL3_INCLUDE_DIRS}
    ${Vulkan_INCLUDE_DIRS}
)

target_link_libraries(vkobjects PUBLIC
    ${SDL3_LIBRARIES}
    Vulkan::Vulkan
)

# ---------------------------------------------------------------------------
# vulkan-demo — demo executable
# ---------------------------------------------------------------------------
set(DEMO_SOURCES
    demo/main.cpp
    demo/camera.cpp
    demo/tga.cpp
)

add_executable(vulkan-demo ${DEMO_SOURCES})

target_include_directories(vulkan-demo PRIVATE demo)

target_link_libraries(vulkan-demo PRIVATE vkobjects)

# ---------------------------------------------------------------------------
# Shader compilation — SPV output next to source
# ---------------------------------------------------------------------------
find_program(GLSLC glslc REQUIRED)

set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/demo/shaders)

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

# Make the demo depend on shaders
add_dependencies(vulkan-demo shaders)
