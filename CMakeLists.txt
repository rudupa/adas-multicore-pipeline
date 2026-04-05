cmake_minimum_required(VERSION 3.16)
project(adas_multicore_pipeline
    VERSION 1.0.0
    LANGUAGES CXX
    DESCRIPTION "ADAS Multicore Compute Pipeline Simulator"
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ADAS_BUILD_IMGUI_VIEWER "Build ImGui + ImPlot desktop viewer" ON)

# ── Fetch nlohmann/json for config parsing ──────────────────────────
include(FetchContent)
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(json)

if(ADAS_BUILD_IMGUI_VIEWER)
    # Dear ImGui (with GLFW + OpenGL3 backends)
    FetchContent_Declare(
        imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG        v1.91.9b
    )

    # ImPlot (plot widgets for ImGui)
    FetchContent_Declare(
        implot
        GIT_REPOSITORY https://github.com/epezent/implot.git
        GIT_TAG        v0.16
    )

    # GLFW window/context management
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
    )

    FetchContent_MakeAvailable(imgui implot glfw)

    add_library(imgui_lib STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
        ${implot_SOURCE_DIR}/implot.cpp
        ${implot_SOURCE_DIR}/implot_items.cpp
    )

    target_include_directories(imgui_lib PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        ${implot_SOURCE_DIR}
    )

    target_link_libraries(imgui_lib PUBLIC glfw)

    if(WIN32)
        target_link_libraries(imgui_lib PUBLIC opengl32)
    else()
        find_package(OpenGL REQUIRED)
        target_link_libraries(imgui_lib PUBLIC OpenGL::GL)
    endif()
endif()

# ── Source files ────────────────────────────────────────────────────
set(CORE_SOURCES
    src/core/thread_pool.cpp
    src/core/bandwidth_manager.cpp
    src/core/config_loader.cpp
    src/core/task_scheduler.cpp
)

set(SENSOR_SOURCES
    src/sensors/camera_sensor.cpp
    src/sensors/radar_sensor.cpp
    src/sensors/vehicle_state_sensor.cpp
)

set(PIPELINE_SOURCES
    src/pipeline/configured_stage.cpp
    src/pipeline/pipeline.cpp
)

set(SCHEDULER_SOURCES
    src/scheduler/fifo_scheduler.cpp
)

set(METRICS_SOURCES
    src/metrics/metrics_collector.cpp
)

set(VISUALIZATION_SOURCES
    src/visualization/timeline_visualizer.cpp
)

# ── Library target ──────────────────────────────────────────────────
add_library(adas_pipeline_lib STATIC
    ${CORE_SOURCES}
    ${SENSOR_SOURCES}
    ${PIPELINE_SOURCES}
    ${SCHEDULER_SOURCES}
    ${METRICS_SOURCES}
    ${VISUALIZATION_SOURCES}
)

target_include_directories(adas_pipeline_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(adas_pipeline_lib PUBLIC nlohmann_json::nlohmann_json)

# Platform-specific threading
if(WIN32)
    # Windows threads are linked automatically
else()
    find_package(Threads REQUIRED)
    target_link_libraries(adas_pipeline_lib PUBLIC Threads::Threads)
endif()

# ── Main executable ─────────────────────────────────────────────────
add_executable(adas_pipeline src/main.cpp)
target_link_libraries(adas_pipeline PRIVATE adas_pipeline_lib)

if(ADAS_BUILD_IMGUI_VIEWER)
    add_executable(adas_viewer src/viewer/adas_viewer.cpp)
    target_link_libraries(adas_viewer PRIVATE adas_pipeline_lib imgui_lib)
endif()

# ── Copy config to build directory ──────────────────────────────────
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/config/adas_pipeline_config.json
    ${CMAKE_CURRENT_BINARY_DIR}/config/adas_pipeline_config.json
    COPYONLY
)
