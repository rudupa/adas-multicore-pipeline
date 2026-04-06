# ADAS Multicore Pipeline Simulator - Features Plan

**Current Phase**: Phase 3 - Accelerator Modeling (Planning)  
**Last Updated**: April 5, 2026  
**Build Status**: ✅ Phase 2 Complete

---

## 📋 Features Overview

### Phase 1: Configuration & Core Data Structures ✅ COMPLETE
**Commit**: `c84ed1c`

#### Supported Features:
- [x] Multi-core CPU topology definition (cpu_id, frequency, task capacity)
- [x] Accelerator configuration (GPU, NPU, DSP with latency/bandwidth/queue parameters)
- [x] Task priority assignment (0-99 scale)
- [x] CPU core affinity/pinning configuration (-1=no affinity, 0..n=specific core)
- [x] Sensor jitter modeling (per-sensor timing variation %)
- [x] Metrics tracking control flags (deadline_misses, staleness, fault_injection, thermal_throttling)
- [x] Per-stage accelerator assignment (gpu, npu, dsp, or "" for CPU)
- [x] JSON config parsing with defaults for all new fields
- [x] 8-core CPU example configuration
- [x] GPU/NPU accelerator example configs
- [x] Sensor jitter example configs (cameras 2.5%, radars 1.5%)

#### Config Schema (include/core/config_loader.h):
- `struct CPUCore` - core_id, freq_ghz, max_tasks
- `struct AcceleratorConfig` - name, inference_latency_us, max_queue_depth, bandwidth_mbps, scheduling
- `struct SensorJitterCfg` - sensor_name, jitter_percentage, enable_dma_interrupt, dma_arrival_jitter_us
- Extended `struct StageConfig` - priority, preferred_core, accelerator
- Extended `struct PipelineConfig` - cpu_cores[], accelerators[], sensor_jitters[], metrics flags
- Extended `struct SensorCfg` - jitter_percentage

---

### Phase 2: Scheduling & Execution Engine ✅ COMPLETE


#### Completed:
- [x] Implemented TaskScheduler class with priority queue support
- [x] Core affinity binding (soft: prefer core, hard: pin to core)
- [x] Deadline miss detection (track if stage exceeds expected exec time)
- [x] Jitter injection at sensor arrival (Gaussian distribution)
- [x] Per-core task tracking and utilization metrics
- [x] Deadline tracking and violation logging in metrics
#### Deliverables:
- [x] New TaskScheduler class (src/core/task_scheduler.h/cpp) - 150 LOC
- [x] Modified Pipeline executor to use scheduler - integrated into Pipeline constructor
- [x] Deadline miss detection in metrics collection - record_deadline_miss() method
- [x] Jitter injection in sensor timing - applied in sensor_loop()
- [x] Enhanced MetricsCollector with deadline tracking

**Commit**: `aae7446` - Phase 2 complete with 773+ lines
---

### Phase 3: Accelerator Modeling 📋 PLANNED

#### Features:
- [ ] GPU inference queue simulation
- [ ] NPU inference queue simulation
- [ ] DSP pipeline stages (FFT, CFAR, tracking)
- [ ] Accelerator task contention modeling
- [ ] Memory bandwidth contention between tasks
- [ ] Accelerator utilization metrics
- [ ] Queue depth tracking and overflow handling

#### Deliverables:
- [ ] AcceleratorQueue class for each accelerator type
- [ ] Task migration to accelerator clusters
- [ ] Contention-aware latency adjustment
- [ ] Accelerator metrics (utilization, queue depth, throughput)

---

### Phase 4: Sensor & Dataflow Timing 📋 PLANNED

#### Features:
- [ ] Sensor arrival jitter simulation (camera/radar frame drops)
- [ ] Frame drop modeling (missed deadlines at sensor)
- [ ] Dataflow staleness tracking (age of data at each stage)
- [ ] Synchronized multi-sensor snapshot acquisition
- [ ] Sensor-to-stage latency accounting
- [ ] DMA interrupt-driven arrivals
- [ ] Sporadic vs periodic sensor timing variations

#### Deliverables:
- [ ] SensorJitterSimulator with random jitter injection
- [ ] Staleness tracker in frame metadata
- [ ] Multi-sensor snapshot builder
- [ ] Sensor timing visualization overlay

---

### Phase 5: Metrics Collection 📋 PLANNED

#### Features:
- [ ] Per-core utilization percentage
- [ ] Deadline miss rate and histogram
- [ ] End-to-end latency distribution (sense→plan→act cycle)
- [ ] Per-stage latency breakdown
- [ ] Staleness histogram (data age in pipeline)
- [ ] Task migration frequency (core hopping)
- [ ] Accelerator utilization and queue stats
- [ ] Frame drop rate and causes
- [ ] Priority inversion detection

#### Deliverables:
- [ ] MetricsCollector class with per-core tracking
- [ ] Latency histogram builder
- [ ] Staleness analyzer
- [ ] Metrics export to JSON/CSV
- [ ] Summary statistics generator

---

### Phase 6: Visualization Enhancement 📋 PLANNED

#### Features:
- [ ] Gantt chart per CPU core (task timeline)
- [ ] Deadline miss indicators (red flag on violations)
- [ ] Per-task color coding (priority levels)
- [ ] Accelerator utilization bar
- [ ] Latency distribution plot (ImPlot histogram)
- [ ] Staleness waterfall chart (frame age progression)
- [ ] Timeline zoom/pan for detailed analysis
- [ ] Task migration flow lines between cores
- [ ] Legend for priority levels and accelerators

#### Deliverables:
- [ ] GanttRenderer class for ImGui
- [ ] Latency histogram plot (ImPlot)
- [ ] Staleness waterfall visualization
- [ ] Enhanced timeline with scheduling info overlay
- [ ] Metrics panel with core utilization gauges

---

### Phase 7: Configuration UI 📋 PLANNED

#### Features:
- [ ] Scheduler policy selector (fifo/priority/deadline)
- [ ] Core affinity mapper (drag-drop task→core)
- [ ] Accelerator enable/disable toggles
- [ ] Sensor jitter slider controls
- [ ] Fault injection controls (enable/config)
- [ ] Priority slider per stage (0-99)
- [ ] Thermal throttling config
- [ ] Deadline margin adjustment

#### Deliverables:
- [ ] Scheduler config panel in inspector
- [ ] Core affinity panel with visual mapping
- [ ] Accelerator control panel
- [ ] Sensor jitter controls panel
- [ ] Live parameter adjustment without restart

---

### Phase 8: Advanced Features (Optional) 📋 PLANNED

#### Features:
- [ ] Thermal throttling simulation (frequency scaling on overload)
- [ ] Power consumption modeling (per-core, per-accelerator)
- [ ] L1/L2/L3 cache effects on task timing
- [ ] Task migration overhead
- [ ] Critical path analysis
- [ ] Latency breakdown (compute vs memory vs I/O)
- [ ] Multi-frequency domain (DVFS) support
- [ ] Heterogeneous core scheduling (big.LITTLE style)

#### Deliverables:
- [ ] ThermalModel class with frequency mapping
- [ ] PowerModel with per-component consumption
- [ ] CacheSimulator for timing adjustment
- [ ] CriticalPathAnalyzer for bottleneck detection
- [ ] Advanced metrics export format

---

## 📊 Feature Implementation Matrix

| Feature | Phase | Status | Config | Parser | Engine | Metrics | Visualization | UI |
|---------|-------|--------|--------|--------|--------|---------|---------------|-----|
| Multi-core CPU topology | 1 | ✅ | ✅ | ✅ | ⏳ | ⏳ | ⏳ | ⏳ |
| Task priorities (0-99) | 1 | ✅ | ✅ | ✅ | ⏳ | ⏳ | ⏳ | ⏳ |
| Core affinity pinning | 1 | ✅ | ✅ | ✅ | ⏳ | ⏳ | ⏳ | ⏳ |
| Accelerator configs | 1 | ✅ | ✅ | ✅ | ⏳ | ⏳ | ⏳ | ⏳ |
| Sensor jitter | 1 | ✅ | ✅ | ✅ | ⏳ | ⏳ | ⏳ | ⏳ |
| **Priority scheduler** | 2 | ✅ | ✅ | ✅ | ✅ | ⏳ | ⏳ | ⏳ |
| **Deadline detection** | 2 | ✅ | ✅ | ✅ | ✅ | ✅ | ⏳ | ⏳ |
| **Per-core utilization** | 2 | ✅ | ✅ | ✅ | ✅ | ✅ | ⏳ | ⏳ |
| GPU queue simulation | 3 | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ |
| Staleness tracking | 4 | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ |
| Latency histogram | 5 | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ |
| Gantt chart viz | 6 | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ |
| Scheduler UI panel | 7 | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ |
| Thermal throttling | 8 | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ | ⏳ |

Legend: ✅ = Complete | 🔄 = In Progress | ⏳ = Planned | ❌ = Not Planned

---

## CDI Integration Track (VLM/VLA + Deterministic Stack)

### Current CDI Status
- [x] Parallel branch structure implemented in runtime flow:
   - Deterministic branch: Fusion -> Localization -> World Map
   - Cognitive branch: Semantic Reasoning -> Intent Generation -> Semantic Adapter -> SCE -> CDNP
- [x] Merge gate before planning implemented:
   - Context Fusion stage joins deterministic world model + cognitive negotiated context
- [x] Config updated with executable CDI stages and timing/priority/core/accelerator bindings
- [x] Execution-view descriptors added for CDI stages and sub-steps

### CDI Components Modeled as Pipeline Stages (Implemented)
- [x] Cognitive Agent inference (VLM/VLA semantic reasoning)
- [x] Intent generation
- [x] Semantic Adapter (model-specific output to SOC-like package)
- [x] SCE validation stage (simulated timing stage)
- [x] CDNP negotiation stage (simulated timing stage)
- [x] Planner context fusion stage

### CDI Behavioral Simulation (Next)
- [ ] SCE verdict engine with ACCEPT / LIMITED / REJECT outputs
- [ ] CDNP bounded negotiation rounds with ACCEPT / COUNTER / ABORT outcomes
- [ ] SSH mode machine: CDI_ENHANCED -> DETERMINISTIC_ONLY -> MRC
- [ ] SSH trigger simulation (10 triggers from CDI architecture)
- [ ] USTES-style audit/event logging for SCE/CDNP/SSH decisions

### ASIL/Authority Guardrail in Simulator
- [x] Cognitive channel modeled as advisory context only
- [x] Deterministic planner remains execution authority
- [ ] SSH hard override path and deterministic-only fallback enforcement (behavioral)

---

## 🎯 Alignment with User Requirements

### 9-Point Feature Requirements:

1. **Real-time Scheduling** (p, sp, p, d) → **Phase 2/3**
   - Priority-based scheduling ✅ Config, ✅ Engine
   - Sporadic task support ✅ Foundation in Phase 2
   - Deadline detection ✅ Phase 2
   
2. **Multi-core CPU Simulation** ✅ Config, ✅ Engine
   - Core topology ✅
   - Core affinity ✅ Config, ✅ Engine
   - Preemption ✅ Simplified model in Phase 2
   
3. **GPU/NPU/DSP Accelerators** ✅ Config, ⏳ Phase 3
   - Accelerator configs ✅
   - Inference queues ⏳
   - Contention modeling ⏳
   
4. **Sensor Timing & Dataflow** ✅ Config, ⏳ Phase 4
   - Jitter modeling ✅ Config
   - Drop modeling ⏳
   - Staleness tracking ⏳
   
5. **Pipeline Execution** ✅ Implemented (baseline)
   - Serial/parallel stages ✅
   - Shared memory ✅
   
6. **Overload & Fault Simulation** ✅ Config, ⏳ Phases 2/8
   - CPU overload ⏳
   - Bandwidth overload ⏳
   - Fault injection flags ✅ Config
   - Thermal throttling ⏳
   
7. **Metrics & Visualization** 🔄 Phases 2/5/6
   - Deadline miss rate ✅ Basic in Phase 2, ⏳ histogram in Phase 5
   - Per-core utilization ✅ Basic in Phase 2, ⏳ advanced in Phase 5
   - Latency histograms ⏳ Phase 5
   - Gantt charts ⏳ Phase 6
   
8. **Configurability** ✅ Config, 🔄 Engine, ⏳ Phase 7
   - Core counts ✅
   - GPU/NPU configs ✅
   - Task periods ✅
   - Scheduling policies ✅ Config, 🔄 Engine
   
9. **Advanced Features** ⏳ Phase 8
   - Thermal throttling ⏳
   - Cache/memory effects ⏳
   - Power modeling ⏳
   - E2E latency ⏳

---

## 🔄 Phase 2 Execution Plan

### Step 1: Core Scheduler Implementation
- Create `include/core/task_scheduler.h`
- Implement priority queue with core affinity
- Task selection algorithm (priority-aware, respects affinity)

### Step 2: Pipeline Integration
- Modify `src/pipeline/pipeline.cpp` to use scheduler
- Add deadline tracking per stage
- Inject jitter at sensor arrival and task execution

### Step 3: Metrics Enhancement
- Add deadline miss tracking to metrics
- Per-core utilization tracking
- Latency variation capture

### Step 4: Viewer Updates
- Display scheduler info (per-core task queue)
- Show deadline miss indicators
- Add utilization gauge per core

### Step 5: Config & Testing
- Update example config with priorities
- Build and verify scheduler operation
- Commit Phase 2 work

---

## 📝 Code Structure

### New Headers (Phase 2):
```
include/core/task_scheduler.h      // TaskScheduler, TaskQueue, CPUCoreState
```

### Modified Files (Phase 2):
```
src/pipeline/pipeline.cpp          // Add scheduler integration
src/core/metrics.cpp              // Add deadline miss tracking
src/viewer/adas_viewer.cpp        // Display scheduler info
```

### Example Config (Phase 1 ✅):
```
config/adas_pipeline_config.json   // cpu_cores, accelerators, sensor_jitter, metrics
```

---

## 🚀 Next Actions

1. **Immediate**: Start Phase 3 implementation (GPU/NPU/DSP accelerator queues)
2. **Short-term**: Add SCE/CDNP outcome metrics and SSH mode simulation
3. **Medium-term**: Phases 4-6 (sensor timing, metrics, visualization)
4. **Long-term**: Phases 7-8 (configuration UI, advanced safety/perf models)

---

## 📌 Key Decision Points

- **Scheduler Algorithm**: Priority queue (ready at compile time, not RTOS-style dynamic)
- **Affinity Model**: Soft affinity with core stealing under load
- **Preemption**: Allowed mid-timestamp, not mid-execution (simplified model)
- **Jitter Injection**: Random Gaussian in config-specified ranges
- **Metrics Collection**: Per-cycle accumulated (not real-time histogram)

---

## 📎 Related Documentation

- [Simulator Concepts](docs/System/Simulator_Concepts.md) - ADAS architecture and what simulator models
- [Config Schema](include/core/config_loader.h) - Data structures
- [Phase 1 Commit](commit/c84ed1c) - Config foundation

