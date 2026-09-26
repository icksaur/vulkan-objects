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
    src/accel.cpp
    src/frame.cpp
    src/frame_slots.cpp
    src/pipeline.cpp
    src/pipelinecache.cpp
    src/timestamp.cpp
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
# mesh_demo / vert_demo — same scene, two geometry paths
# ---------------------------------------------------------------------------
set(DEMO_COMMON_SOURCES
    demo/camera.cpp
    demo/tga.cpp
)

add_executable(mesh_demo demo/mesh_main.cpp ${DEMO_COMMON_SOURCES})
add_executable(vert_demo demo/vert_main.cpp ${DEMO_COMMON_SOURCES})

foreach(DEMO_TARGET mesh_demo vert_demo)
    target_include_directories(${DEMO_TARGET} PRIVATE demo)
    target_link_libraries(${DEMO_TARGET} PRIVATE vkobjects)
endforeach()

# ---------------------------------------------------------------------------
# Shader compilation — SPV output next to source
# ---------------------------------------------------------------------------
find_program(GLSLC glslc REQUIRED)

set(SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/demo/shaders)

set(FRAGMENT_SHADERS shadow.frag blit.frag)
set(COMPUTE_SHADERS cubes.comp)
set(MESH_SHADERS cube.mesh shadow.mesh fullscreen.mesh)
set(VERTEX_SHADERS cubes.vert shadow.vert fullscreen.vert)

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

# Compile vertex shaders
foreach(SHADER ${VERTEX_SHADERS})
    set(SHADER_SOURCE ${SHADER_DIR}/${SHADER})
    set(SHADER_OUTPUT ${SHADER_DIR}/${SHADER}.spv)
    add_custom_command(
        OUTPUT ${SHADER_OUTPUT}
        COMMAND ${GLSLC} --target-env=vulkan1.2 -fshader-stage=vertex ${SHADER_SOURCE} -o ${SHADER_OUTPUT}
        DEPENDS ${SHADER_SOURCE}
        COMMENT "Compiling vertex shader ${SHADER}"
    )
    list(APPEND SPIRV_BINARIES ${SHADER_OUTPUT})
endforeach()

# Add custom target for shaders
add_custom_target(shaders ALL DEPENDS ${SPIRV_BINARIES})

# Make the demos depend on shaders
add_dependencies(mesh_demo shaders)
add_dependencies(vert_demo shaders)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()

set(TEST_SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/tests/shaders)
set(TEST_SPIRV ${CMAKE_CURRENT_BINARY_DIR}/tests/shaders/as_oracle.comp.spv)
add_custom_command(
    OUTPUT ${TEST_SPIRV}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/tests/shaders
    COMMAND ${GLSLC} --target-env=vulkan1.2 ${TEST_SHADER_DIR}/as_oracle.comp -o ${TEST_SPIRV}
    DEPENDS ${TEST_SHADER_DIR}/as_oracle.comp
    COMMENT "Compiling test shader as_oracle.comp"
)

# stage name -> glslc -fshader-stage argument, for test shaders whose extension glslc
# cannot infer or whose stage must be forced.
set(TEST_STAGED_SHADERS pull.vert:vert pull.frag:frag attributes.vert:vert)
set(TEST_STAGED_SPIRV)
foreach(ENTRY ${TEST_STAGED_SHADERS})
    string(REPLACE ":" ";" PARTS ${ENTRY})
    list(GET PARTS 0 SHADER)
    list(GET PARTS 1 STAGE)
    set(OUT ${CMAKE_CURRENT_BINARY_DIR}/tests/shaders/${SHADER}.spv)
    add_custom_command(
        OUTPUT ${OUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/tests/shaders
        COMMAND ${GLSLC} --target-env=vulkan1.2 -fshader-stage=${STAGE} ${TEST_SHADER_DIR}/${SHADER} -o ${OUT}
        DEPENDS ${TEST_SHADER_DIR}/${SHADER}
        COMMENT "Compiling test shader ${SHADER}"
    )
    list(APPEND TEST_STAGED_SPIRV ${OUT})
endforeach()

add_custom_target(test-shaders DEPENDS ${TEST_SPIRV} ${TEST_STAGED_SPIRV})

add_executable(vkobjects-accel-structure-tests tests/accel_structure_tests.cpp)
target_include_directories(vkobjects-accel-structure-tests PRIVATE src)
target_link_libraries(vkobjects-accel-structure-tests PRIVATE vkobjects)
add_dependencies(vkobjects-accel-structure-tests test-shaders)
add_test(NAME vkobjects-accel-structure-tests COMMAND vkobjects-accel-structure-tests
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

add_executable(vkobjects-clear-value-tests tests/clear_value_tests.cpp)
target_include_directories(vkobjects-clear-value-tests PRIVATE src)
target_link_libraries(vkobjects-clear-value-tests PRIVATE vkobjects)
add_test(NAME vkobjects-clear-value-tests COMMAND vkobjects-clear-value-tests
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

add_executable(vkobjects-vertex-pipeline-tests tests/vertex_pipeline_tests.cpp)
target_include_directories(vkobjects-vertex-pipeline-tests PRIVATE src)
target_link_libraries(vkobjects-vertex-pipeline-tests PRIVATE vkobjects)
add_dependencies(vkobjects-vertex-pipeline-tests test-shaders)
add_test(NAME vkobjects-vertex-pipeline-tests COMMAND vkobjects-vertex-pipeline-tests
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

add_executable(vkobjects-swapchain-extent-tests tests/swapchain_extent_tests.cpp)
target_include_directories(vkobjects-swapchain-extent-tests PRIVATE src)
target_link_libraries(vkobjects-swapchain-extent-tests PRIVATE vkobjects)
add_test(NAME vkobjects-swapchain-extent-tests COMMAND vkobjects-swapchain-extent-tests
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

add_executable(vkobjects-frame-slot-tests tests/frame_slot_tests.cpp)
target_include_directories(vkobjects-frame-slot-tests PRIVATE src)
target_link_libraries(vkobjects-frame-slot-tests PRIVATE vkobjects)
add_test(NAME vkobjects-frame-slot-tests COMMAND vkobjects-frame-slot-tests
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# ---------------------------------------------------------------------------
# Geometry path benchmark — measurement only, deliberately not an add_test
# ---------------------------------------------------------------------------
# Its shader lands in the source tree next to the demo shaders it is measured against,
# because the benchmark runs from the source directory to reach demo/shaders/*.spv.
set(BENCH_SPIRV ${TEST_SHADER_DIR}/bench.frag.spv)
add_custom_command(
    OUTPUT ${BENCH_SPIRV}
    COMMAND ${GLSLC} --target-env=vulkan1.2 -fshader-stage=frag ${TEST_SHADER_DIR}/bench.frag -o ${BENCH_SPIRV}
    DEPENDS ${TEST_SHADER_DIR}/bench.frag
    COMMENT "Compiling bench shader bench.frag"
)
add_custom_target(bench-shaders DEPENDS ${BENCH_SPIRV})

add_executable(geometry-path-bench tests/geometry_path_bench.cpp demo/camera.cpp)
target_include_directories(geometry-path-bench PRIVATE src demo)
target_link_libraries(geometry-path-bench PRIVATE vkobjects)
add_dependencies(geometry-path-bench bench-shaders shaders)
