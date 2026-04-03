cmake_minimum_required(VERSION 3.16)
project(adas_multicore_pipeline
    VERSION 1.0.0
    LANGUAGES CXX
    DESCRIPTION "ADAS Multicore Compute Pipeline Simulator"
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Fetch nlohmann/json for config parsing ──────────────────────────
include(FetchContent)
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(json)

# ── Source files ────────────────────────────────────────────────────
set(CORE_SOURCES
    src/core/thread_pool.cpp
    src/core/bandwidth_manager.cpp
    src/core/config_loader.cpp
)

set(SENSOR_SOURCES
    src/sensors/camera_sensor.cpp
    src/sensors/radar_sensor.cpp
)

set(PIPELINE_SOURCES
    src/pipeline/preprocess_stage.cpp
    src/pipeline/detection_stage.cpp
    src/pipeline/tracking_stage.cpp
    src/pipeline/pipeline.cpp
)

set(SCHEDULER_SOURCES
    src/scheduler/fifo_scheduler.cpp
)

set(METRICS_SOURCES
    src/metrics/metrics_collector.cpp
)

# ── Library target ──────────────────────────────────────────────────
add_library(adas_pipeline_lib STATIC
    ${CORE_SOURCES}
    ${SENSOR_SOURCES}
    ${PIPELINE_SOURCES}
    ${SCHEDULER_SOURCES}
    ${METRICS_SOURCES}
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

# ── Copy config to build directory ──────────────────────────────────
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/config/pipeline_config.json
    ${CMAKE_CURRENT_BINARY_DIR}/config/pipeline_config.json
    COPYONLY
)
