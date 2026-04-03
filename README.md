# ADAS Multicore Pipeline Simulator

A configurable C++17 simulation of a real-time **Advanced Driver Assistance System (ADAS)** compute pipeline with multicore execution, bandwidth management, and performance profiling.

> **Note:** This project does *not* include real AI/ML models. It simulates system-level behaviour — scheduling, latency, throughput, and bandwidth constraints — to support architecture exploration and performance analysis.

## Features

| Area | Details |
|------|---------|
| **Sensors** | Pluggable sensor framework (Camera, Radar); extensible to Lidar and others |
| **Bandwidth** | Global and per-sensor bandwidth caps with frame dropping on overload |
| **Pipeline** | Three-stage pipeline: Preprocess → Detection → Tracking (all configurable) |
| **Multicore** | Thread pool with configurable worker count |
| **Scheduling** | Pluggable scheduler interface; ships with FIFO (priority & deadline TODOs) |
| **Metrics** | End-to-end latency, per-stage time, queue wait time, drop counts |
| **Config** | JSON-based configuration for sensors, bandwidth, pipeline, and execution |

## Project Structure

```
├── CMakeLists.txt
├── config/
│   └── pipeline_config.json
├── include/
│   ├── core/          # Types, thread pool, concurrent queue, bandwidth manager, config
│   ├── sensors/       # Sensor interface + Camera / Radar implementations
│   ├── pipeline/      # Pipeline stage interface + stages + orchestrator
│   ├── scheduler/     # Scheduler interface + FIFO implementation
│   └── metrics/       # Metrics collector
└── src/
    ├── core/
    ├── sensors/
    ├── pipeline/
    ├── scheduler/
    ├── metrics/
    └── main.cpp
```

## Building

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## Running

```bash
# Uses default config at config/pipeline_config.json
./build/adas_pipeline

# Or specify a custom config
./build/adas_pipeline path/to/config.json
```

## Interactive Desktop Viewer (ImGui + ImPlot)

This project also includes an optional desktop visualization app:

- executable: `adas_viewer`
- stack: Dear ImGui + ImPlot + GLFW + OpenGL
- mode: native desktop window (not browser)

Build (enabled by default):

```bash
cmake -B build -DADAS_BUILD_IMGUI_VIEWER=ON
cmake --build build --config Release
```

Run:

```bash
./build/adas_viewer
# or custom config
./build/adas_viewer path/to/config.json
```

Viewer features:
- live lane-based timeline plot (sensor/stage/throttle/drop)
- per-second throughput chart (frames/sec)
- controls for timeline window and auto-scroll

## Configuration

Edit `config/pipeline_config.json`:

```jsonc
{
  "sensors": [
    { "type": "camera", "name": "front_camera", "fps": 30, "frame_size_bytes": 2073600, "bandwidth_limit_mbps": 100.0 },
    { "type": "radar",  "name": "front_radar",  "fps": 20, "frame_size_bytes": 8192,    "bandwidth_limit_mbps": 10.0 }
  ],
  "bandwidth": { "global_limit_mbps": 200.0, "window_duration_ms": 1000 },
  "pipeline": { "preprocess_delay_us": 500, "detection_delay_us": 2000, "tracking_delay_us": 1000, "queue_capacity": 64 },
  "execution": { "thread_pool_size": 4, "scheduler": "fifo", "run_duration_seconds": 5 }
}
```

## Documentation

- [ADAS Pipeline Architecture & Timing Visualization](docs/system/ADAS_Pipeline.md)

## Extension Points (TODOs)

- **LidarSensor** — add a new sensor type implementing `Sensor` interface
- **PriorityScheduler** — order tasks by `TaskPriority`
- **DeadlineScheduler** — earliest-deadline-first ordering
- **CPU Affinity** — pin thread-pool workers to specific cores
- **Per-sensor bandwidth accounting** — track sliding-window usage per sensor independently

## Dependencies

- C++17 compiler
- CMake ≥ 3.16
- [nlohmann/json](https://github.com/nlohmann/json) (fetched automatically via CMake FetchContent)

## License

MIT
