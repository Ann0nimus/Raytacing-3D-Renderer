cmake_minimum_required(VERSION 3.24)

project(RaytacingRenderer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Vulkan QUIET)

add_library(raytacing_core
    src/render/RenderGraph.cpp
)

target_include_directories(raytacing_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
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

if(Vulkan_FOUND)
    add_library(raytacing_renderer
        src/vulkan/AccelerationStructure.cpp
        src/vulkan/Buffer.cpp
        src/vulkan/Commands.cpp
        src/vulkan/Device.cpp
        src/vulkan/Instance.cpp
        src/vulkan/RayTracingPipeline.cpp
        src/vulkan/VulkanError.cpp
    )

    target_link_libraries(raytacing_renderer
        PUBLIC
            raytacing_core
            Vulkan::Vulkan
    )

    if(MSVC)
        target_compile_options(raytacing_renderer PRIVATE /W4 /permissive-)
    else()
        target_compile_options(raytacing_renderer PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_executable(raytacing_sandbox
        src/main.cpp
    )

    target_link_libraries(raytacing_sandbox
        PRIVATE
            raytacing_renderer
    )
else()
    message(WARNING "Vulkan SDK was not found. raytacing_renderer and raytacing_sandbox will not be built.")
endif()
