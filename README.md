# ADAS Multicore Pipeline Simulator

A configurable C++17 simulation of a real-time **Advanced Driver Assistance System (ADAS)** sense-plan-act compute pipeline with multicore execution, bandwidth management, and performance profiling.

> **Note:** This project does *not* include real AI/ML models. It simulates system-level behaviour — scheduling, latency, throughput, and bandwidth constraints — to support architecture exploration and performance analysis.

## Features

| Area | Details |
|------|---------|
| **Sensors** | Pluggable sensor framework (Camera, Radar, vehicle-state); extensible to Lidar and others |
| **Sensor Jitter** | Per-sensor Gaussian arrival-time jitter (configurable % per sensor) |
| **Bandwidth** | Global and per-sensor bandwidth caps with frame dropping on overload |
| **Pipeline** | Config-driven Sense → Plan → Act pipeline with executable stage groups and sub-steps |
| **CDI Architecture** | Parallel deterministic + cognitive branches with merge-gate context fusion before planning |
| **Multicore** | Thread pool with configurable worker count and per-core task-count tracking |
| **Scheduling** | Priority-based TaskScheduler (0–99 scale); soft/hard core affinity; deadline miss detection |
| **Accelerators** | Per-stage GPU/NPU assignment with configurable inference latency and queue depth |
| **Metrics** | End-to-end latency, per-stage time, queue wait, drop counts, deadline miss tracking |
| **Config** | JSON schema v2 — sensors, CPU topology, accelerators, execution policy, stage priorities |
| **Viewer** | Dear ImGui + ImPlot desktop GUI with timeline, throughput, phase inspector, zoom/pan |

## Project Structure

```
├── CMakeLists.txt
├── config/
│   └── adas_pipeline_config.json
├── include/
│   ├── core/          # Types, thread pool, concurrent queue, bandwidth manager, config
│   ├── sensors/       # Sensor interface + Camera / Radar implementations
│   ├── pipeline/      # Pipeline stage interface + stages + orchestrator
│   ├── scheduler/     # Scheduler interface + FIFO implementation
│   ├── metrics/       # Metrics collector
│   └── visualization/ # Timeline event buffer consumed by GUI viewer
└── src/
    ├── core/
    ├── sensors/
    ├── pipeline/
    ├── scheduler/
    ├── metrics/
    ├── visualization/
    ├── viewer/
    └── main.cpp
```

## Building

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## Running

```bash
# Uses default config at config/adas_pipeline_config.json
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
- live lane-based timeline plot (sensor ingress, Sense/Plan/Act stages, throttle/drop/overlap/cycle-miss markers)
- dual-line per-second throughput chart: completed FPS and sensor ingress FPS
- hover tooltip on throughput chart with interpolated time and value for each line
- crosshairs on both timeline and throughput plots
- stage lanes ordered by pipeline config (not alphabetically)
- left-side phase treeview with min/avg/max step execution times and configured sub-steps
- cycle count and drop/throttle counters
- larger UI font and auto-aligned windows
- zoom/scroll controls for timeline inspection (scroll wheel + drag to pan)

## Configuration

Default runtime config: `config/adas_pipeline_config.json`

This runtime config describes:

- 6 camera streams, 5 smart radars, and vehicle-state inputs
- camera ingress, radar ingress, and central ECU topology
- per-sensor camera/radar pipelines with vehicle-state inputs
- fixed-rate central loop for fusion, planning, and control
- scenario-level timing for each executable stage

Additional architecture-level example:

- [config/adas_pipeline_config.json](config/adas_pipeline_config.json) — 6 cameras, 5 radars, vehicle-state inputs, ingress links, and a central ECU pipeline for fusion/planning/control

## Documentation

- [ADAS Pipeline Architecture](docs/system/ADAS_Pipeline.md)
- [Simulator Concepts Guide](docs/System/Simulator_Concepts.md)
- [Simulator Features Plan](docs/SIMULATOR_FEATURES_PLAN.md) — phased development roadmap (Phases 1–8)

## Mermaid Generator Tool

Generate Mermaid diagrams from ADAS config (`adas_pipeline_config.json`):

```bash
python tools/generate_mermaid.py \
  --input config/adas_pipeline_config.json \
  --output docs/system/ADAS_Pipeline_generated.md \
  --scenario-id adas_realtime_distributed
```

Generate standalone Mermaid files (`.mmd`) for Mermaid Live Editor:

```bash
python tools/generate_mermaid.py \
  --input config/adas_pipeline_config.json \
  --output docs/system/adas_pipeline \
  --scenario-id adas_realtime_distributed \
  --format mmd
```

This emits:
- `docs/system/adas_pipeline_pipeline.mmd`
- `docs/system/adas_pipeline_topology.mmd`

Tool location:
- [tools/generate_mermaid.py](tools/generate_mermaid.py)

## System Config GUI Tool (Real-time JSON + Mermaid)

This repo includes a lightweight GUI to edit the **ADAS schema** config and generate `system_config.json` in real time (with a live preview).

Install:

```bash
python -m pip install -r tools/requirements_gui.txt
```

Run:

```bash
python tools/system_config_gui.py
```

By default it writes:

- `config/system_config.json`

Tool location:

- [tools/system_config_gui.py](tools/system_config_gui.py)

## Extension Points (Planned)

- **LidarSensor** — add a new sensor type implementing `Sensor` interface
- **Per-sensor bandwidth accounting** — track sliding-window usage per sensor independently
- **Accelerator queue simulation** — GPU/NPU/DSP contention and queue-depth overflow (Phase 3)
- **Dataflow staleness tracking** — age-of-data metadata propagated through pipeline stages (Phase 4)
- **Latency histograms and metrics export** — per-stage latency distribution and JSON/CSV export (Phase 5)
- **Gantt chart visualization** — per-CPU-core task timeline with deadline miss indicators (Phase 6)
- **Live config UI** — in-viewer scheduler policy, affinity, jitter, and priority controls (Phase 7)
- **Thermal throttling and power modeling** — frequency scaling under load, per-component power (Phase 8)

## Dependencies

- C++17 compiler
- CMake ≥ 3.16
- [nlohmann/json](https://github.com/nlohmann/json) (fetched automatically via CMake FetchContent)
- [GLFW](https://www.glfw.org/) (fetched automatically via CMake FetchContent)
- [Dear ImGui](https://github.com/ocornut/imgui) (fetched automatically via CMake FetchContent)
- [ImPlot](https://github.com/epezent/implot) (fetched automatically via CMake FetchContent)

## License

MIT
