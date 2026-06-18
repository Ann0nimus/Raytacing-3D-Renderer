cmake_minimum_required(VERSION 3.24)

project(RaytacingRenderer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

find_package(Vulkan REQUIRED)
find_package(glfw3 CONFIG REQUIRED)

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.9b
)

FetchContent_Declare(
    tinyobjloader
    GIT_REPOSITORY https://github.com/tinyobjloader/tinyobjloader.git
    GIT_TAG v2.0.0rc13
)

FetchContent_MakeAvailable(imgui tinyobjloader)

add_library(imgui_vulkan_glfw
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)

target_include_directories(imgui_vulkan_glfw
    PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(imgui_vulkan_glfw
    PUBLIC
        glfw
        Vulkan::Vulkan
)

if(MSVC)
    target_compile_options(imgui_vulkan_glfw PRIVATE /W4 /permissive-)
else()
    target_compile_options(imgui_vulkan_glfw PRIVATE -Wall -Wextra -Wpedantic)
endif()

find_program(GLSLC_EXECUTABLE
    NAMES glslc
    HINTS
        "$ENV{VULKAN_SDK}/Bin"
        "C:/msys64/ucrt64/bin"
)

if(NOT GLSLC_EXECUTABLE)
    message(FATAL_ERROR "glslc was not found. Install Vulkan SDK or MSYS2 shaderc.")
endif()

set(RT_SHADER_SOURCES
    shaders/pathtrace.rgen
    shaders/pathtrace.rmiss
    shaders/pathtrace.rchit
)

set(RT_SHADER_OUTPUTS)
foreach(shader ${RT_SHADER_SOURCES})
    get_filename_component(shader_name ${shader} NAME)
    set(shader_output ${CMAKE_CURRENT_BINARY_DIR}/shaders/${shader_name}.spv)
    add_custom_command(
        OUTPUT ${shader_output}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/shaders
        COMMAND ${GLSLC_EXECUTABLE} --target-env=vulkan1.2 ${CMAKE_CURRENT_SOURCE_DIR}/${shader} -o ${shader_output}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${shader}
        VERBATIM
    )
    list(APPEND RT_SHADER_OUTPUTS ${shader_output})
endforeach()

add_custom_target(raytacing_shaders DEPENDS ${RT_SHADER_OUTPUTS})

add_library(raytacing_core
    src/core/Paths.cpp
    src/render/GpuScene.cpp
    src/render/RenderGraph.cpp
    src/scene/SceneLoader.cpp
)

target_include_directories(raytacing_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${tinyobjloader_SOURCE_DIR}
)

if(MSVC)
    target_compile_options(raytacing_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(raytacing_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(raytacing_graph_test
    tests/render_graph_test.cpp
)
target_link_libraries(raytacing_graph_test PRIVATE raytacing_core)

add_library(raytacing_renderer
    src/app/RendererApp.cpp
    src/platform/Window.cpp
    src/vulkan/AccelerationStructure.cpp
    src/vulkan/Buffer.cpp
    src/vulkan/Commands.cpp
    src/vulkan/Device.cpp
    src/vulkan/Image.cpp
    src/vulkan/Instance.cpp
    src/vulkan/RayTracingPipeline.cpp
    src/vulkan/Swapchain.cpp
    src/vulkan/VulkanError.cpp
)

target_link_libraries(raytacing_renderer
    PUBLIC
        raytacing_core
        imgui_vulkan_glfw
        glfw
        Vulkan::Vulkan
)

if(MSVC)
    target_compile_options(raytacing_renderer PRIVATE /W4 /permissive-)
else()
    target_compile_options(raytacing_renderer PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(raytacing_app
    src/main.cpp
)

target_link_libraries(raytacing_app
    PRIVATE
        raytacing_renderer
)

add_dependencies(raytacing_app raytacing_shaders)

add_custom_command(TARGET raytacing_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_BINARY_DIR}/shaders
            $<TARGET_FILE_DIR:raytacing_app>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:raytacing_app>/assets
    VERBATIM
)
