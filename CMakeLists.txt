cmake_minimum_required(VERSION 3.20)
project(Engine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# FetchContent for Dependencies
include(FetchContent)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(USE_SSE4_2 ON CACHE BOOL "" FORCE)
set(WERROR OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.3.8
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glfw)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glm)

FetchContent_Declare(
    jolt
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG v5.0.0
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR Build
)
FetchContent_MakeAvailable(jolt)

# GCC 15 has stricter warnings that Jolt v5.0.0 doesn't handle
if(TARGET Jolt)
    target_compile_options(Jolt PRIVATE -Wno-error -Wno-maybe-uninitialized)
endif()

# Dear ImGui
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.8
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)

# Create ImGui library target
add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui_lib PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_compile_definitions(imgui_lib PUBLIC IMGUI_IMPL_VULKAN_NO_PROTOTYPES VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)

# Find Vulkan
find_package(Vulkan REQUIRED)

target_link_libraries(imgui_lib PUBLIC Vulkan::Vulkan glfw)

# Shader Compilation
set(SHADERS
    shaders/SDFCompute.glsl
    shaders/TerrainBrush.glsl
)

foreach(SHADER ${SHADERS})
    get_filename_component(SHADER_NAME ${SHADER} NAME_WE)
    set(SPIRV_FILE "${CMAKE_BINARY_DIR}/shaders/${SHADER_NAME}.spv")
    add_custom_command(
        OUTPUT ${SPIRV_FILE}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/shaders"
        COMMAND glslc -fshader-stage=compute ${CMAKE_CURRENT_SOURCE_DIR}/${SHADER} -o ${SPIRV_FILE}
        DEPENDS ${SHADER}
        COMMENT "Compiling ${SHADER} to ${SPIRV_FILE}"
    )
    list(APPEND SPIRV_SHADERS ${SPIRV_FILE})
endforeach()

add_executable(Engine
    src/main.cpp
    src/core/Window.cpp
    src/core/Window.hpp
    src/core/InputState.hpp
    src/core/VulkanContext.cpp
    src/core/VulkanContext.hpp
    src/core/SDFEdit.hpp
    src/core/PhysicsSystem.cpp
    src/core/PhysicsSystem.hpp
    src/renderer/Swapchain.cpp
    src/renderer/Swapchain.hpp
    src/renderer/ResourceManager.cpp
    src/renderer/ResourceManager.hpp
    src/renderer/DescriptorManager.cpp
    src/renderer/DescriptorManager.hpp
    src/renderer/BrickAtlas.cpp
    src/renderer/BrickAtlas.hpp
    src/renderer/SparseMap.cpp
    src/renderer/SparseMap.hpp
    src/renderer/ComputePipeline.cpp
    src/renderer/ComputePipeline.hpp
    src/renderer/SDFRenderer.cpp
    src/renderer/SDFRenderer.hpp
    src/renderer/Terrain.cpp
    src/editor/EditorUI.cpp
    src/editor/EditorUI.hpp
    ${SPIRV_SHADERS}
)

target_include_directories(Engine PRIVATE src ${CMAKE_BINARY_DIR}/_deps/jolt-src)
target_compile_definitions(Engine PRIVATE IMGUI_IMPL_VULKAN_NO_PROTOTYPES VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)
target_link_libraries(Engine PRIVATE Vulkan::Vulkan glfw glm::glm Jolt imgui_lib)

if(MSVC)
    target_compile_options(Engine PRIVATE /W4)
else()
    target_compile_options(Engine PRIVATE -Wall -Wextra -Wno-error)
endif()
