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

## Bottom line

An ADAS simulator is most useful when it explains system behavior at architecture level. It should make bottlenecks, tradeoffs, and policy consequences visible enough that an engineer can decide whether to add compute, reduce work, repartition the pipeline, or change the runtime policy.