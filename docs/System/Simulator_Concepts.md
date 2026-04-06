# ADAS Simulator Concepts Guide

This viewer is not only a timeline plot. It is a system architecture simulator for an ADAS sense-plan-act pipeline, which means it should help explain why a design behaves the way it does under timing, bandwidth, scheduling, and compute pressure.

## What this simulator is modeling

- Sensor ingress timing, bandwidth limits, and frame production rates
- Stage-level execution timing across Sense, Plan, and Act workloads
- Multicore execution with contention for workers and shared resources
- Central-cycle orchestration policies when a new control cycle arrives before the previous one finishes
- System-level effects such as drops, throttling, missed cycles, and overlap events
- End-to-end pipeline latency and throughput under different runtime profiles

## Core ADAS architecture concepts

### Sense, Plan, Act

- Sense turns raw sensor streams into usable perception state.
- Plan converts perception and ego-state into predictions, intent, and an executable trajectory.
- Act converts the planned trajectory into control outputs with strict timing requirements.
- The overall system is only as good as its slowest stage on the critical path.

### Distributed ingress plus centralized decision making

- Camera and radar pipelines often begin near the sensor or at zonal ECUs because transport cost is high.
- Fusion, planning, and control usually converge on a central compute domain that needs a coherent world model.
- This creates two different pressure zones:
- Edge-side preprocessing pressure from sensor fan-in.
- Central ECU pressure from synchronization, fusion, and decision deadlines.

### Control-loop freshness matters as much as raw throughput

- A pipeline can show high FPS and still be unsafe if decisions are based on stale data.
- For ADAS, bounded latency and deterministic completion are usually more important than average throughput.
- The simulator should make it obvious when the system is producing work faster than it can produce useful decisions.

## Bottlenecks this simulator should expose

### Compute saturation

- Thread-pool exhaustion increases queueing delay even if individual stage code is unchanged.
- Long-running perception or planning stages can starve downstream control updates.
- A stage with high variance is often more dangerous than a stage with a slightly worse mean.

### Memory and interconnect bandwidth

- Camera-heavy systems are frequently bandwidth-limited before they are compute-limited.
- Copy-heavy data movement, serialization, or redundant buffering can dominate end-to-end cost.
- Sensor drops and throttle events are often symptoms of transport pressure, not algorithm quality.

### Synchronization and fan-in barriers

- Fusion waits for multiple upstream inputs, so the effective latency is often defined by the slowest required input.
- Joining too many streams in one cycle increases coordination cost and staleness risk.
- The simulator should make visible when barriers, not stage execution itself, are causing missed deadlines.

### Scheduling overhead and policy mismatch

- FIFO can be simple and predictable, but it may not preserve the most safety-critical work under overload.
- A system can be busy doing valid work while still making the wrong scheduling choice for real-time control.

## Tradeoffs the viewer should help explain

### Throughput vs latency

- Maximizing worker utilization may improve throughput while hurting deadline satisfaction.
- Batching and deep queues can increase efficiency but also age the data.

### Fidelity vs determinism

- Richer perception or prediction models improve quality but may add latency variance.
- Simpler models can preserve real-time guarantees at the cost of scene understanding accuracy.

### Centralization vs partitioning

- Centralized fusion simplifies reasoning and coordination.
- Partitioned or staged processing reduces transport cost and may improve scalability.
- The tradeoff is usually between coherence of decisions and cost of moving data.

### Utilization vs headroom

- A system that runs near 100 percent utilization in nominal mode has little resilience to burst load.
- ADAS platforms need operational margin for weather, clutter, degraded sensors, and corner cases.

## Scheduling policies and why they matter

### Allow overlap

- A new central cycle begins even if the previous one is still active.
- This can maximize freshness in some paths but also creates overlapping work and resource contention.
- The simulator should expose overlap markers because overlap is not automatically good or bad; it is a signal that the system is trading determinism for responsiveness.

### Skip if active

- If a cycle is still running, the next requested cycle is dropped.
- This protects the system from runaway backlog.
- The tradeoff is reduced effective control rate and possible loss of responsiveness.

### Preempt previous

- A new cycle invalidates or replaces in-flight work from the previous one.
- This favors freshness and can be appropriate when stale results are less valuable than incomplete work.
- The tradeoff is wasted compute and more complex state management.

### What a stronger simulator should eventually compare

- FIFO vs fixed-priority scheduling
- Earliest-deadline-first behavior under overload
- Safety-critical fast lanes for actuation and watchdog work
- Affinity-aware scheduling where cache locality and NUMA effects matter

## What a simulator tool like this should be able to do

## Task priority, core affinity, and deadline tracking

### Task priority model

- Every pipeline stage is assigned a priority on a 0–99 scale, configurable per stage in the JSON config.
- The task scheduler uses a priority queue to dispatch work. Higher-priority tasks are selected first when multiple tasks are ready and worker threads are available.
- In the reference configuration, cognitive stages (VLM semantic reasoning, VLA intent generation) carry priorities 70–75, safety-critical arbitration stages (SCE validation, CDNP negotiation) carry 84–85, and deterministic compute stages carry lower values. This reflects real ADAS authority ordering.
- Under nominal load, priority ordering has little visible effect because the thread pool has enough capacity. Under overload, priority ordering determines which stages make progress and which get delayed — and that difference will appear as selective queueing pressure in the timeline.

### Core affinity and pinning

- Stages can declare a `preferred_core` value in the config: -1 means no affinity, a non-negative integer means the stage prefers a particular core.
- Soft affinity allows the scheduler to migrate a stage if the preferred core is at capacity; a strict pin would bind it exclusively to that core.
- Affinity matters for real ADAS systems because perception and inference models often benefit from cache locality when a core repeatedly processes similar workloads.
- In the simulator, affinity affects load distribution across the thread pool. If multiple high-frequency stages compete for the same preferred core, queueing delay will accumulate and appear as stretched stage bars in the timeline.

### Deadline miss detection

- Each stage has min/avg/max execution time bounds from the config. When a stage runs longer than its maximum expected duration the metrics collector records a deadline miss.
- Deadline misses are distinct from cycle miss events. A cycle miss means the central loop requested a new cycle before the previous one finished. A deadline miss means a specific stage exceeded its own per-stage timing budget within a cycle.
- Cycle misses expose overall cycle budget exhaustion. Deadline misses expose unexpected behavior within a single stage. Reading both together narrows root-cause analysis.

### Per-core utilization tracking

- The scheduler tracks how many active tasks are assigned to each core at any point in time.
- Utilization is currently collected as concurrent task count per core rather than as an elapsed-time fraction. High counts during peak periods reflect queueing pressure even when aggregate thread-pool capacity is not fully consumed.
- This tracking is the foundation for the planned per-core Gantt chart visualization in Phase 6.

## Accelerator routing and inference modeling

- In ADAS systems, perception and cognitive stages routinely offload compute to dedicated accelerators: a GPU for image feature extraction and scene understanding, an NPU for neural policy inference, and a DSP for radar signal processing.
- The simulator models accelerator assignment at the config level. Each stage declares which accelerator it targets, or `""` for CPU-only execution.
- Each accelerator has a configured inference latency, maximum queue depth, and scheduling policy (FIFO or priority). Stages routed to an accelerator inherit that latency in the timing model regardless of CPU thread-pool state.
- In the reference configuration, GPU-routed stages (camera processing, VLM reasoning) use 5 ms inference latency; NPU-routed stages (VLA intent generation) use 8 ms.
- Contention modeling — where multiple stages simultaneously queuing on the same accelerator extend each other's actual dispatch latency — is planned for Phase 3. Until then, each stage applies its accelerator latency independently.
- The practical consequence for architecture analysis: systems with many GPU-routed stages will eventually be accelerator-queue-bound, not just CPU-thread-pool-bound. The simulator is designed to make that distinction visible once Phase 3 is implemented.

## Sensor jitter and arrival timing variation

- Real sensors do not deliver frames at perfectly uniform intervals. Clock drift, bus arbitration, interrupt latency, and measurement physics all introduce small timing offsets at the sensor boundary.
- The simulator models this as Gaussian jitter applied at sensor frame arrival. The magnitude is a per-sensor `jitter_percentage` in the config.
- In the reference configuration, cameras carry 2.5% jitter and radars carry 1.5% jitter. A camera running at 30 Hz with 2.5% jitter sees arrival times that vary by roughly ±0.83 ms around the nominal 33.3 ms period.
- Jitter propagates into the timeline: sensor ingress events will cluster near but not exactly at their nominal period. Fusion stages that wait for specific sensor inputs will see slightly variable inter-arrival gaps.
- Diagnostic use: if a timeline shows clustering of cycle miss events correlated with sensor ingress timing spikes, the root cause may be jitter amplification through the multi-sensor fusion barrier rather than stage execution cost alone.

## CDI: parallel cognitive and deterministic branches

- The pipeline architecture implements a Cognitive-Deterministic Integration (CDI) track as a parallel branch within the Sense phase.
- The deterministic branch executes Fusion → Localization → World Map Build and produces a verified world model with bounded, auditable confidence.
- The cognitive branch executes Semantic Reasoning (VLM) → Intent Generation (VLA) → Semantic Adapter → SCE Validation → CDNP Negotiation and produces an intent context with safety annotations.
- Both branches run concurrently. The merge gate is the Context Fusion stage at the start of the Plan phase, which joins the deterministic world model with the negotiated cognitive intent before the planner executes. This means the planner always waits for the slower of the two branches.
- The cognitive channel is advisory only. The deterministic planner holds execution authority. Priority and core-affinity settings enforce this: deterministic stages hold scheduling priority for safety-critical outcomes; cognitive stages are modeled as high-latency advisory inputs that enrich, not override, the planner context.
- The SCE validation and CDNP negotiation stages serve as bounded-time arbiters. Their configured timing bounds represent the maximum time the system allocates for cognitive context validation before the planner proceeds with or without the cognitive context.
- The practical consequence visible in the timeline: cognitive stages have higher per-stage latency than deterministic stages, so Context Fusion typically starts late relative to the deterministic branch outputs. That gap is the cost of cognitive integration and should be weighed against the planning quality improvement it provides.

## What a simulator tool like this should be able to do

### Timing analysis

- Show stage min, average, max, and sampled execution behavior
- Separate service time from queue wait time
- Highlight critical-path contributors, not just total latency

### Overload analysis

- Show where drops, throttles, missed cycles, and overlap events originate
- Make backlog visible over time rather than only reporting terminal counters
- Compare behavior across nominal, eco, and performance profiles

### Architecture what-if exploration

- Change thread-pool size, cycle mode, and timing model
- Disable selected sensors to study dependency and scaling behavior
- Explore whether the bottleneck moves when compute is added or removed

### Policy validation

- Reveal whether a scheduling policy improves freshness, stability, or neither
- Help answer whether overload handling is graceful or chaotic
- Show when a policy protects safety-critical timing at the expense of throughput

### Communication value

- Support design reviews by explaining not just what happened, but why it happened
- Help non-implementation stakeholders understand the consequences of architecture decisions
- Provide a shared artifact for discussions around ECU partitioning, compute sizing, and runtime policy

## How to read the viewer with the right mental model

- Treat the timeline as a causality view, not only a performance chart.
- Look for where latency accumulates: sensor ingress, barriers, queueing, or stage execution.
- When drops or missed cycles occur, ask whether the root cause is compute, transport, or policy.
- When overlap appears, ask whether freshness improved enough to justify the added contention.
- When changing profiles, watch whether the bottleneck moves or simply becomes hidden.

## Practical questions this tool should help answer

- Which stage is on the critical path for a control cycle?
- Are missed cycles caused by one slow stage or by repeated fan-in delay?
- Does adding threads improve latency, or only improve average throughput?
- Is the system bandwidth-bound, compute-bound, or synchronization-bound?
- Which scheduling policy degrades most gracefully when the system is overloaded?
- How much headroom exists before the control loop becomes unstable or stale?
- What priority level is needed for a stage to avoid being delayed under overload?
- Which CPU core is becoming a hotspot due to affinity-pinned stages competing for capacity?
- Is the parallel cognitive branch completing before or after the deterministic branch, and does the merge gate stall frequently?
- How much does sensor jitter amplify into fusion barrier delay compared to nominal zero-jitter timing?

## Understanding the Throughput window metrics
## Planned capabilities (Phases 3–8)

The simulator is built in phases. The following capabilities are designed and roadmapped but not yet executing in the simulation engine.

**Phase 3 — Accelerator queue contention:** When multiple high-priority stages simultaneously dispatch to the same GPU or NPU, queue depth constraints will delay later arrivals. This will make accelerator throughput limits visible independently of CPU thread-pool pressure. A Gantt-style accelerator utilization bar will show queue occupancy over time.

**Phase 4 — Dataflow staleness tracking:** Each data frame will carry an age timestamp from creation to consumption. By the time a frame reaches the Act stage, its end-to-end latency will be computed and recorded as a staleness value. Staleness histograms will expose when plans are being built on data that is several cycles old, a condition that may not be visible from throughput or drop metrics alone. This phase also includes synchronized multi-sensor snapshot acquisition that enforces temporal alignment across cameras and radars within a configurable time window.

**Phase 5 — Advanced metrics and export:** Per-stage latency distributions will be collected as histograms rather than just min/avg/max. Metrics will be exportable to JSON or CSV for offline comparison across config variants. Priority inversion detection, task migration frequency, and staleness histograms will be added to the metrics collector.

**Phase 6 — Gantt chart per-core visualization:** Each CPU core will be rendered as a row in a Gantt chart with tasks shown as colored bars. Deadline miss markers (red flags) and task migration flow lines between cores will make affinity effects and scheduling choices directly readable without inferring from stage-level timeline bars.

**Phase 7 — Live configuration UI:** Scheduler policy, stage priorities, core affinity, and sensor jitter will be adjustable inside the viewer without restarting the simulation. This will enable direct interactive comparison of policy decisions during a live run, including a core-affinity drag-and-drop mapper and per-stage priority sliders.

**Phase 8 — Thermal throttling and power modeling:** The simulator will model CPU frequency scaling under sustained thermal load, adding realistic timing variance when the ECU approaches limits. Per-core and per-accelerator power consumption will be tracked as a secondary analysis axis alongside latency and throughput, relevant for power-budgeted automotive ECU platforms.


The Throughput window shows two time-series lines sampled at one-second intervals throughout the simulation. They measure different things at different points in the system and should be read together, not in isolation.

### Completed FPS

- Counts how many full Sense-Plan-Act control cycles completed in each one-second window.
- A cycle is counted only when it has passed through every pipeline stage and recorded a pipeline exit. That means all of Sense (including fusion, localization, world map, cognitive reasoning, and negotiation), all of Plan, and all of Act must finish before the count increments.
- The theoretical ceiling is the configured central loop rate, which is 50 Hz in the reference configuration. In practice the ceiling is lower because stage execution takes time, workers may be contended, and the cycle mode may allow or restrict overlap.
- This metric answers: how many useful control decisions did the system produce per second?
- A plateau below the configured rate is the first indicator of compute pressure. A sudden drop is the first indicator of a burst or overload event.

### Sensor ingress FPS

- Counts how many new sensor ingress events appeared in the timeline per one-second window.
- Each ingress event corresponds to one sensor frame arriving and being recorded, regardless of whether the pipeline ever acted on it. Cameras, radars, IMU, wheel odometry, and GNSS all produce ingress events independently at their own configured rates.
- In the reference configuration with six cameras at 30 Hz, five radars at 20 Hz, IMU at 200 Hz, wheel odometry at 100 Hz, and GNSS at 10 Hz, the total sensor production rate is approximately 520 ingress events per second at steady state.
- This metric answers: how much data is the sensing layer producing per second, independent of whether the pipeline can consume it?
- Sensor ingress FPS is structurally decoupled from the control loop. Sensors do not pause when the pipeline falls behind.

### The relationship between the two lines

- Sensor ingress FPS will almost always be higher than completed FPS. That is expected and by design. Sensors produce data continuously; the control loop runs at a lower rate and selects or fuses the most recent data from each source when it executes.
- The meaningful question is not whether sensor ingress FPS exceeds completed FPS, but whether the gap between them is widening, stable, or narrowing over time.
- A stable gap means the pipeline is keeping up with the sensor stream at its current rate. Sensor data arrives, the control loop picks up what it needs on each cycle, and excess frames are either batched or discarded at the ingress boundary without accumulating pressure.
- A widening gap over time is a signal that data is arriving faster than the pipeline can close loops. This may manifest as drop events, cycle miss markers, or overlap markers on the timeline, depending on the configured cycle mode.
- A narrowing gap late in the run can indicate that sensors are approaching their end-of-burst periods or that the pipeline has caught up after an initial overload.

### Connecting the Throughput chart to the timeline

- Each timeline row corresponds to a sensor lane or a pipeline stage lane. The ingress markers near the top of the timeline represent the same events that the sensor ingress FPS line counts.
- When sensor ingress FPS is high and completed FPS is low, look at the Sense phase lanes in the timeline. Longer bars there mean stages are taking more time per cycle, reducing the number of cycles that can complete per second.
- When completed FPS drops while sensor ingress FPS remains steady, look for overlap markers and cycle miss markers in the timeline. These indicate the control loop is being triggered at its configured rate but cannot finish cycles within one period.
- When both lines drop together, the likely cause is a resource constraint that affects everything: thread pool exhaustion, a bandwidth throttle event, or a scheduled workload spike.
- Drop markers on the timeline mean the pipeline explicitly discarded a cycle or a sensor frame rather than queuing it. A single drop during a transient is usually tolerable. Sustained drops alongside a falling completed FPS line indicate the system is in structural overload for the current configuration.

### What to watch for when changing configuration

- Increasing thread pool size may raise completed FPS if the bottleneck is compute capacity, but it will not raise completed FPS if the bottleneck is a serial stage or a synchronization barrier.
- Changing the central cycle mode from allow_overlap to skip_if_active will typically reduce completed FPS but may reduce cycle miss markers because the system stops starting work it cannot finish.
- Increasing run_duration_seconds stretches the chart horizontally and makes warmup effects at the start and wind-down effects at the end more visible relative to steady-state behavior.
- Adjusting sensor rates changes sensor ingress FPS directly. Reducing camera rates brings the ingress line down without changing the control loop ceiling, which may make the gap between the two lines appear smaller even if pipeline capability has not improved.

## Bottom line

An ADAS simulator is most useful when it explains system behavior at architecture level. It should make bottlenecks, tradeoffs, and policy consequences visible enough that an engineer can decide whether to add compute, reduce work, repartition the pipeline, or change the runtime policy.