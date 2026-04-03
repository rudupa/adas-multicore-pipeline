### High-level ADAS sense–plan–act pipeline

1. **Sense:** Acquire and process raw sensor data into a consistent, fused environment model.  
2. **Plan:** Predict how the scene will evolve and compute a safe, comfortable trajectory.  
3. **Act:** Convert the planned trajectory into actuator commands for steering, braking, and powertrain.

---

### Sense stage

#### 1. Time synchronization and data acquisition

- **Global time base:**  
  All sensors (camera, radar, IMU, GNSS, wheel encoders) are synchronized to a common clock (e.g., PTP, GPS time).
- **Raw data capture:**  
  - **Camera:** RGB frames (or grayscale) at fixed FPS.  
  - **Radar:** Range–Doppler or range–azimuth–Doppler data, or preprocessed radar targets.  
  - **Vehicle state:** Speed, yaw rate, steering angle, acceleration, etc., from CAN/FlexRay/ETH.

#### 2. Camera processing pipeline

- **2.1 Preprocessing**
  - **Undistortion & rectification:** Remove lens distortion, apply rectification for stereo or multi-camera rigs.
  - **Color/illumination normalization:** White balance, gamma correction, exposure compensation.
  - **Resizing & formatting:** Resize to network input size, normalize, batch/tiling if needed.

- **2.2 Vision perception**
  - **Object detection:** CNN-based detectors (e.g., anchor-based/anchor-free) for vehicles, pedestrians, cyclists, etc.
  - **Semantic segmentation:** Road, lane markings, sidewalks, drivable area, static obstacles.
  - **Lane & road features:** Lane line detection, lane boundaries, road edges, traffic signs/lights.
  - **Depth/3D cues (optional):** Stereo disparity, monocular depth estimation, or structure-from-motion.

- **2.3 Camera tracking (optional pre-fusion)**
  - **2D tracking:** Associate detections frame-to-frame (e.g., Kalman filter + Hungarian, deep association).
  - **Track management:** Initiate, update, and delete 2D tracks with IDs and confidence.

#### 3. Radar processing pipeline

- **3.1 Signal-level processing**
  - **FFT processing:** Convert raw ADC to range–Doppler (and possibly angle) spectra.
  - **CFAR detection:** Detect peaks corresponding to potential objects.
  - **Clustering:** Group detections into radar targets (range, angle, radial velocity, RCS).

- **3.2 Target-level refinement**
  - **Static/dynamic separation:** Remove ground/guard rails if needed, highlight moving objects.
  - **Ego-motion compensation:** Correct radial velocities using vehicle speed and yaw rate.
  - **Track-level filtering:** Track radar targets over time (e.g., EKF/UKF) to estimate position and velocity.

#### 4. Multi-sensor fusion

- **4.1 Spatial and temporal alignment**
  - **Calibration:** Use extrinsic calibration (rotation/translation) to transform camera and radar data into a common frame (e.g., vehicle or map frame).   [arXiv.org](https://arxiv.org/abs/2304.10410)  
  - **Time alignment:** Interpolate/extrapolate sensor data to a common timestamp.

- **4.2 Association and fusion**
  - **Data association:** Match radar targets with camera detections using position, velocity, appearance, and gating.
  - **State fusion:** Use filters (e.g., EKF, UKF, particle filters) or learned fusion to combine measurements into unified object states (position, velocity, size, class).   [deepwiki.com](https://deepwiki.com/im-hashim/automotive-claude-code-agents/10.2-adas-perception-pipeline-example)  [arXiv.org](https://arxiv.org/abs/2304.10410)  
  - **Track management:** Maintain a global list of tracked objects with IDs, history, and uncertainty.

- **4.3 Environment model**
  - **Dynamic objects:** Fused tracks for vehicles, pedestrians, cyclists, etc.  
  - **Static structure:** Road geometry, lanes, curbs, barriers, traffic signs/lights.  
  - **Occupancy/drivable space:** Bird’s-eye-view (BEV) occupancy grid or drivable area map.

#### 5. Localization

- **5.1 Ego-motion estimation**
  - **Dead reckoning:** Integrate IMU + wheel odometry for short-term motion.  
  - **Visual/feature-based odometry (optional):** Use camera features or LiDAR (if present) for relative motion.

- **5.2 Global localization**
  - **GNSS:** Provide coarse global position.  
  - **Map matching:** Align ego pose with HD map using lane markings, landmarks, or point-cloud features.

- **5.3 Output**
  - **Ego pose:** \((x, y, z, \text{yaw}, \text{pitch}, \text{roll})\) in map or world frame with covariance.  
  - **Consistent frame:** All objects and road elements expressed in this frame.

---

### Plan stage

#### 6. Prediction

- **6.1 Input**
  - Fused object tracks (positions, velocities, headings, classes).  
  - Road topology and traffic rules from HD map.  
  - Ego state and planned route.

- **6.2 Behavior and motion prediction**
  - **Short-horizon kinematic prediction:** Constant velocity/acceleration or bicycle-model extrapolation.  
  - **Interaction-aware models:** ML-based predictors that consider right-of-way, lanes, signals, and interactions.  
  - **Uncertainty modeling:** Multi-modal trajectories or probabilistic occupancy over time.

- **6.3 Output**
  - Predicted trajectories or occupancy for each relevant actor over a time horizon (e.g., 3–10 seconds).

#### 7. Behavior planning

- **7.1 Situation assessment**
  - **Scene understanding:** Identify current lane, upcoming intersections, merges, crosswalks.  
  - **Rule evaluation:** Speed limits, traffic lights, stop signs, right-of-way, no-passing zones.  
  - **Risk assessment:** Time-to-collision, gap acceptance, safety margins.

- **7.2 High-level decisions**
  - **Maneuver selection:** Follow lane, change lane, yield, stop, overtake, turn, etc.  
  - **Goal setting:** Define local goals (target lane, target speed, stop line position).

#### 8. Trajectory planning

- **8.1 Constraints and objectives**
  - **Constraints:** Vehicle dynamics, road boundaries, obstacles, traffic rules, comfort limits (jerk, lateral accel).  
  - **Objectives:** Safety first, then comfort, efficiency, and progress along route.

- **8.2 Trajectory generation**
  - **Path planning:**  
    - Lattice-based, sampling-based (RRT variants), or optimization-based (MPC, QP) in Frenet or Cartesian space.  
  - **Speed planning:**  
    - Longitudinal profile respecting speed limits, lead vehicle, signals, and predicted obstacles.
  - **Coupled planning:**  
    - Joint optimization of path and speed to avoid collisions over the prediction horizon.

- **8.3 Output**
  - Time-parameterized trajectory: sequence of \((x, y, \text{yaw}, v, a)\) with timestamps and associated uncertainty.

---

### Act stage

#### 9. Control

- **9.1 Trajectory tracking control**
  - **Lateral control:**  
    - Controllers like Stanley, pure pursuit, or MPC to minimize lateral error and heading error.  
  - **Longitudinal control:**  
    - PID/MPC-based speed control, adaptive cruise control logic, distance-keeping to lead vehicle.

- **9.2 Actuator command generation**
  - **Steering:** Desired steering angle or torque.  
  - **Braking:** Brake pressure or deceleration request.  
  - **Powertrain:** Throttle/torque request, gear selection if needed.

- **9.3 Feedback and monitoring**
  - **Closed-loop feedback:** Use sensors and vehicle state to update control errors each cycle.  
  - **Safety supervision:** Monitor for faults, limit violations, or degraded performance; trigger fallback or safe stop if needed.   [dSPACE](https://www.dspace.com/en/inc/home/applicationfields/ind-appl/automotive-industry/autonomous-driving.cfm)  

---

### Putting it together in a loop

At each control cycle (e.g., 20–100 Hz):

1. **Sense:** Acquire and process camera/radar/vehicle data → fuse → update environment model and ego pose.   [dSPACE](https://www.dspace.com/en/inc/home/applicationfields/ind-appl/automotive-industry/autonomous-driving.cfm)  
2. **Plan:** Predict other actors → choose maneuver → compute a feasible, safe trajectory.  
3. **Act:** Track the trajectory with low-level controllers → send actuator commands → monitor safety.

specific ADAS feature like ACC, AEB, or LKA.

Let’s put both views side by side:  
1) **Logical data-flow sequence** (how information conceptually moves)  
2) **Real-time execution** (what actually runs in parallel/asynchronously)

---

## 1. Logical data-flow sequence (pipeline view)

This is the **conceptual Sense–Plan–Act chain**, assuming idealized, ordered flow.

### 1. Sense

**1.1 Sensor acquisition**  
1.1.1 Time sync  
1.1.2 Camera capture  
1.1.3 Radar capture  
1.1.4 Vehicle state capture  

**1.2 Camera processing**  
1.2.1 Camera preprocessing  
1.2.2 Object detection  
1.2.3 Lane & road detection  
1.2.4 Semantic segmentation  
1.2.5 Depth estimation  
1.2.6 Camera tracking  

**1.3 Radar processing**  
1.3.1 Signal processing  
1.3.2 CFAR detection  
1.3.3 Clustering  
1.3.4 Ego-motion compensation  
1.3.5 Radar tracking  

**1.4 Fusion**  
1.4.1 Spatial alignment  
1.4.2 Temporal alignment  
1.4.3 Data association  
1.4.4 State fusion  
1.4.5 Track management  
1.4.6 Environment model  

**1.5 Localization**  
1.5.1 Ego-motion estimation  
1.5.2 GNSS integration  
1.5.3 Map matching  
1.5.4 Pose estimation  

---

### 2. Plan

**2.1 Prediction**  
2.1.1 Input aggregation  
2.1.2 Kinematic prediction  
2.1.3 Interaction prediction  
2.1.4 Uncertainty modeling  
2.1.5 Predicted trajectories  

**2.2 Behavior planning**  
2.2.1 Situation assessment  
2.2.2 Rule evaluation  
2.2.3 Risk assessment  
2.2.4 Maneuver selection  
2.2.5 Goal setting  

**2.3 Trajectory planning**  
2.3.1 Constraints setup  
2.3.2 Path planning  
2.3.3 Speed planning  
2.3.4 Coupled optimization  
2.3.5 Final trajectory  

---

### 3. Act

**3.1 Control**  
3.1.1 Lateral control  
3.1.2 Longitudinal control  
3.1.3 Actuator commands  

**3.2 Feedback**  
3.2.1 State monitoring  
3.2.2 Error correction  
3.2.3 Safety supervision  

In this **logical view**, you can read it as:  
**1.1 → 1.2 & 1.3 → 1.4 → 1.5 → 2.1 → 2.2 → 2.3 → 3.1 → 3.2**

---

## 2. Real-time execution view (what runs in parallel)

Now, same pipeline, but marked by **real-time behavior**.

### 1. Sense (real time)

**1.1 Sensor acquisition (parallel sources)**  
- 1.1.1 Camera capture (continuous, periodic)  
- 1.1.2 Radar capture (continuous, periodic)  
- 1.1.3 Vehicle state capture (asynchronous bus messages)  
- 1.1.4 Time sync (applied when data is consumed, not before sensors run)  

**Real-time note:**  
- 1.1.1, 1.1.2, 1.1.3 run **in parallel**, each on its own hardware/driver/ISR.  
- 1.1.4 is effectively used when building a synchronized “snapshot” for downstream processing.

---

**1.2 Camera processing (mostly pipelined, partially parallel)**  
- 1.2.1 Preprocessing  
- 1.2.2 Object detection  
- 1.2.3 Lane & road detection  
- 1.2.4 Semantic segmentation  
- 1.2.5 Depth estimation  
- 1.2.6 Camera tracking  

**Real-time note:**  
- 1.2.1–1.2.5 often run as **one or more DNN pipelines** on GPU/accelerator.  
- Some heads (detection, lanes, segmentation) share a backbone and run **in parallel branches**.  
- 1.2.6 tracking runs **after detections** for that frame, but overlaps in time with processing of the next frame.

---

**1.3 Radar processing (pipelined per frame/burst)**  
- 1.3.1 Signal processing  
- 1.3.2 CFAR detection  
- 1.3.3 Clustering  
- 1.3.4 Ego-motion compensation  
- 1.3.5 Radar tracking  

**Real-time note:**  
- 1.3.1–1.3.3 are usually **sequential per radar cycle**, but run **in parallel with camera processing**.  
- 1.3.4–1.3.5 run after initial detections, but again overlap with other sensors’ work.

---

**1.4 Fusion (sync barrier + sequential)**  
- 1.4.1 Spatial alignment  
- 1.4.2 Temporal alignment  
- 1.4.3 Data association  
- 1.4.4 State fusion  
- 1.4.5 Track management  
- 1.4.6 Environment model  

**Real-time note:**  
- Fusion waits for a **time-aligned set** of camera outputs, radar outputs, and vehicle state.  
- Within fusion, 1.4.1–1.4.6 are mostly **sequential** per cycle, but can be multi-threaded internally.

---

**1.5 Localization (continuous, partially parallel)**  
- 1.5.1 Ego-motion estimation (IMU + wheels)  
- 1.5.2 GNSS integration  
- 1.5.3 Map matching  
- 1.5.4 Pose estimation  

**Real-time note:**  
- 1.5.1 runs at **high rate** (IMU loop).  
- 1.5.2 runs at **lower rate** (GNSS).  
- 1.5.3–1.5.4 often run in a **separate localization thread**, feeding pose into fusion and planning.

---

### 2. Plan (real time)

**2.1 Prediction (per planning cycle)**  
- 2.1.1 Input aggregation  
- 2.1.2 Kinematic prediction  
- 2.1.3 Interaction prediction  
- 2.1.4 Uncertainty modeling  
- 2.1.5 Predicted trajectories  

**Real-time note:**  
- Runs **after fusion/localization** produce an updated environment model.  
- 2.1.2–2.1.4 may run **per object in parallel** (e.g., vectorized or multi-threaded).

---

**2.2 Behavior planning (sequential decision)**  
- 2.2.1 Situation assessment  
- 2.2.2 Rule evaluation  
- 2.2.3 Risk assessment  
- 2.2.4 Maneuver selection  
- 2.2.5 Goal setting  

**Real-time note:**  
- Typically a **single-threaded decision step** per planning cycle (e.g., 10–50 Hz).

---

**2.3 Trajectory planning (optimization loop)**  
- 2.3.1 Constraints setup  
- 2.3.2 Path planning  
- 2.3.3 Speed planning  
- 2.3.4 Coupled optimization  
- 2.3.5 Final trajectory  

**Real-time note:**  
- 2.3.2 and 2.3.3 may be **separate modules** but are often tightly coupled.  
- Optimization (2.3.4) is usually **sequential** but can exploit parallelization internally (e.g., candidate trajectories).

---

### 3. Act (real time)

**3.1 Control (high-rate loops)**  
- 3.1.1 Lateral control  
- 3.1.2 Longitudinal control  
- 3.1.3 Actuator commands  

**Real-time note:**  
- 3.1.1 and 3.1.2 may run in **separate control loops** (e.g., 50–100 Hz), both consuming the latest trajectory.  
- 3.1.3 sends commands to steering, brakes, powertrain.

---

**3.2 Feedback (continuous monitoring)**  
- 3.2.1 State monitoring  
- 3.2.2 Error correction  
- 3.2.3 Safety supervision  

**Real-time note:**  
- Runs **continuously**, often in the same ECU as control or in a safety monitor.  
- Can trigger **fallback maneuvers** or system degradation.

---

### Compact summary

- **Logical data flow:**  
  1.1 → 1.2 & 1.3 → 1.4 → 1.5 → 2.1 → 2.2 → 2.3 → 3.1 → 3.2  

- **Real time:**  
  - Sensors (1.1.x) run **in parallel**.  
  - Camera (1.2.x) and radar (1.3.x) pipelines run **in parallel** and **pipelined over frames**.  
  - Fusion (1.4.x) and prediction/planning (2.x) run **periodically** on synchronized snapshots.  
  - Control (3.x) runs at **higher rate**, consuming the latest trajectory.


### example timing diagram with 2 cameras and 2 radars

Here’s an updated **real‑time timing diagram** for the **general ADAS pipeline** with **2 cameras** and **2 radars** over **0–200 ms**. I’ll assume:

- **Cam1, Cam2:** 30 Hz (≈33 ms), slightly phase‑shifted  
- **Rad1, Rad2:** 20 Hz (50 ms), slightly phase‑shifted  
- **Perception (per camera/radar):** runs right after each sensor frame/burst  
- **Fusion + prediction + planning:** 20 Hz (50 ms)  
- **Localization (IMU + wheels):** 100 Hz (10 ms)  
- **GNSS:** 10 Hz (100 ms)  
- **Control:** 100 Hz (10 ms)  

---

### Text timing diagram (0–200 ms, 2 cameras + 2 radars)

```text
Time (ms):    0    10    20    30    33    40    50    60    66    70    80    90   100   110   120   130   133  140   150   160   166  170   180   190   200
              |-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|

CAMERA 1 (30 Hz)
Cam1 Frames:  [C1_F0---------------------]        [C1_F1---------------------]        [C1_F2---------------------]        [C1_F3---------------------]
              0                           33       33                           66       66                           100      100                          133

Cam1 Proc:    [C1_P0----------------]            [C1_P1----------------]            [C1_P2----------------]            [C1_P3----------------]
              0                    20            33                    53            66                    86            100                   120


CAMERA 2 (30 Hz, phase-shifted by ~10 ms)
Cam2 Frames:        [C2_F0---------------------]        [C2_F1---------------------]        [C2_F2---------------------]
                    10                          43       43                          76       76                          110

Cam2 Proc:          [C2_P0----------------]            [C2_P1----------------]            [C2_P2----------------]
                    10                   30            43                   63            76                   96


RADAR 1 (20 Hz)
Rad1 Bursts: [R1_B0--------------------------]               [R1_B1--------------------------]               [R1_B2--------------------------]
             0                             50                50                             100              100                            150

Rad1 Proc:   [R1_P0-----------------]                       [R1_P1-----------------]                       [R1_P2-----------------]
             0                      25                      50                      75                      100                     125


RADAR 2 (20 Hz, phase-shifted by 25 ms)
Rad2 Bursts:        [R2_B0--------------------------]               [R2_B1--------------------------]               [R2_B2--------------------------]
                    25                             75                75                             125              125                            175

Rad2 Proc:          [R2_P0-----------------]                       [R2_P1-----------------]                       [R2_P2-----------------]
                    25                      50                      75                      100                     125                     150


LOCALIZATION (IMU + wheels, 100 Hz)
Loc:         L0  L1  L2  L3  L4  L5  L6  L7  L8  L9  L10 L11 L12 L13 L14 L15 L16 L17 L18 L19 L20
             |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
Time (ms):   0  10  20  30  40  50  60  70  80  90 100 110 120 130 140 150 160 170 180 190 200


GNSS (10 Hz)
GNSS Fix:    G0                                                                 G1
             0                                                                  100


FUSION + PREDICTION + PLANNING (20 Hz)
Fusion/Plan: [FP0-----------]                              [FP1-----------]                              [FP2-----------] 
             0            15                               50            65                               100           115

Inputs used (example):
- FP0 uses latest: C1_P0, C2_P0 (if ready), R1_P0, R2_P0 (if ready), Loc L0–L1, GNSS G0
- FP1 uses latest: C1_P1, C2_P1, R1_P1, R2_P1, Loc L5–L6, GNSS G0
- FP2 uses latest: C1_P2/C1_P3 (whichever ready), C2_P2, R1_P2, R2_P2, Loc L10–L11, GNSS G1


CONTROL (100 Hz)
Ctrl:        C0  C1  C2  C3  C4  C5  C6  C7  C8  C9  C10 C11 C12 C13 C14 C15 C16 C17 C18 C19 C20
             |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |   |
Time (ms):   0  10  20  30  40  50  60  70  80  90 100 110 120 130 140 150 160 170 180 190 200

- Each control tick Ck uses the **latest fused environment + planned trajectory** from FP0/FP1/FP2.
```

---

### What this shows, in plain terms

- **2 cameras** run in parallel, slightly phase‑shifted, each with its own perception pipeline.  
- **2 radars** also run in parallel, with their own bursts and processing, phase‑shifted to avoid interference and load peaks.  
- **Fusion + prediction + planning** (FPx) run periodically (e.g., every 50 ms), consuming the **freshest outputs from both cameras, both radars, localization, and GNSS**.  
- **Control** runs faster (e.g., every 10 ms), always tracking the **most recent planned trajectory**.

If you give me your **actual task periods and offsets** (e.g., Cam1 25 Hz, Cam2 20 Hz, Rad1 15 Hz, Rad2 10 Hz, Planning 40 Hz), I can regenerate this diagram to match your real system more precisely.

---
---

# Pipeline Timing Visualization Module

> Auto-generated timing diagrams are produced at runtime by the
> `TimelineVisualizer` module.  This section explains how to read
> and extend the generated diagrams.

## Visualization Table of Contents

1. [Visualization Overview](#visualization-overview)
2. [Full Timeline Diagram — How to Read](#full-timeline-diagram--how-to-read)
3. [Per-Frame Waterfall Diagram](#per-frame-waterfall-diagram)
4. [Glyph Reference](#glyph-reference)
5. [Example Output — Full Timeline](#example-output--full-timeline)
6. [Example Output — Waterfall](#example-output--waterfall)
7. [Bandwidth Throttle & Drop Visualization](#bandwidth-throttle--drop-visualization)
8. [Visualization Module API](#visualization-module-api)
9. [Extending the Visualizer](#extending-the-visualizer)

---

## Visualization Overview

The simulator automatically instruments every event in the pipeline and
renders two text-based timing diagrams at the end of each run:

1. **Full Timeline** — a horizontal lane chart showing all components
   over the entire run duration.
2. **Per-Frame Waterfall** — one row per frame showing its journey
   through each pipeline stage.

Both are printed to `stdout` after the metrics summary.

### Where events are captured

```
 ┌─────────────┐   ┌─────────────┐
 │ CameraSensor│   │ RadarSensor │
 └──────┬──────┘   └──────┬──────┘
        │ record_event(sensor_name, #)
        ▼
 ┌──────────────────────────────┐
 │      BandwidthManager        │
 │  record_event(BW-throttle,~) │
 │  record_marker(frame-drop,X) │
 └──────────────┬───────────────┘
                ▼
 ┌──────────────────────────────┐
 │ PreprocessStage  record(P)   │
 │ DetectionStage   record(D)   │
 │ TrackingStage    record(T)   │
 └──────────────┬───────────────┘
                ▼
 ┌──────────────────────────────┐
 │    TimelineVisualizer        │──► render() / print()
 └──────────────────────────────┘
```

---

## Full Timeline Diagram — How to Read

The full timeline is a **horizontal lane chart**.  Each row ("lane")
represents one component.  Time flows left → right.

```
                0    50   100  150  200  250  300  350  400  (ms)
                --------------------------------------------------------
front_camera    |###  ###  ###  ###  ###  ###  ###  ###  ### |
front_radar     |####    ####    ####    ####    ####    #### |
preprocess      | PP PP PP PP PP PP PP PP PP PP PP PP PP PP  |
detection       |  DDDD DDDD DDDD DDDD DDDD DDDD DDDD DDDD  |
tracking        |    TT TT TT TT TT TT TT TT TT TT TT TT  |
BW-throttle     |                  ~~                   ~~   |
frame-drop      |                    X                   X   |
                --------------------------------------------------------
```

### Reading the diagram

| Symbol | Meaning |
|--------|---------|
| `#` | Sensor is generating a frame (sleeping for 1/FPS then producing) |
| `P` | Preprocess stage is actively processing a frame |
| `D` | Detection stage is actively processing a frame |
| `T` | Tracking stage is actively processing a frame |
| `~` | Bandwidth throttle delay is being applied |
| `X` | Frame dropped (bandwidth exceeded or queue full) |
| ` ` (space) | Idle / waiting |

**Scale** is printed in the legend below the diagram.  One character
column represents `total_time / width_chars` milliseconds.

---

## Per-Frame Waterfall Diagram

The waterfall shows the **lifecycle of individual frames** — from sensor
generation through each processing stage.  Each row is one frame.

```
── Per-Frame Waterfall (first 20 frames) ──

Frame         | Stage Timeline
--------------|-----------------------------------------------------------------
F0  (3.5ms)   |###PPDDDDTT                                                     |
F1  (3.8ms)   | ###PPDDDDTT                                                    |
F2  (4.1ms)   |  ###PPDDDDTT                                                   |
F3  (3.2ms)   |###PPDDDDTT                                                     |
F4  (4.5ms)   |  ### PPDDDDTT                                                  |
--------------|-----------------------------------------------------------------

  Glyphs:  # = sensor   P = preprocess   D = detection   T = tracking
```

This is useful for spotting:
- **Pipeline bubbles** — gaps between stages (scheduling delay)
- **Stage dominance** — which stage takes the most wall-clock time
- **Jitter** — variation in per-frame end-to-end latency

---

## Glyph Reference

| Glyph | Lane(s) | Meaning |
|-------|---------|---------|
| `#` | Sensor names (`front_camera`, `front_radar`) | Frame generation period |
| `P` | `preprocess` | Preprocess stage active |
| `D` | `detection` | Detection stage active |
| `T` | `tracking` | Tracking stage active |
| `~` | `BW-throttle` | Bandwidth throttle delay injected |
| `X` | `frame-drop` | Frame dropped (overload) |

---

## Example Output — Full Timeline

Below is representative output from a 5-second run with the default
configuration (1 camera @ 30 FPS, 1 radar @ 20 FPS, 4 threads):

```
                0    500  1000 1500 2000 2500 3000 3500 4000 4500 5000 (ms)
                ────────────────────────────────────────────────────────────
front_camera    |#####################################################################|
front_radar     |#####################################################################|
preprocess      |PPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPP|
detection       |DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD|
tracking        |TTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT|
BW-throttle     |          ~~        ~~         ~~        ~~         ~~        ~~      |
frame-drop      |          X X       X X        X X       X X        X X       X X    |
                ────────────────────────────────────────────────────────────

  Legend:  # = sensor generate   P = preprocess   D = detection   T = tracking
           X = frame drop   ~ = BW throttle
  Scale:   1 col = 41.67 ms   |   total = 5000.0 ms
```

> **Tip:** At the default 120-column width, long runs compress heavily.
> Use a shorter `run_duration_seconds` or wider terminal for finer detail.

---

## Example Output — Waterfall

```
── Per-Frame Waterfall (first 10 frames) ──

Frame         | Stage Timeline
--------------|-----------------------------------------------------------------
F0  (3.5ms)   |##PPDDDDTT                                                      |
F1  (3.6ms)   |##PPDDDDTT                                                      |
F2  (3.5ms)   |##PPDDDDTT                                                      |
F3  (3.4ms)   |##PPDDDDTT                                                      |
F4  (4.2ms)   |## PP DDDDTT                                                    |
F5  (3.5ms)   |##PPDDDDTT                                                      |
F6  (3.9ms)   |##PPDDDDTT                                                      |
F7  (3.6ms)   |##PPDDDDTT                                                      |
F8  (3.7ms)   |##PP DDDDTT                                                     |
F9  (3.5ms)   |##PPDDDDTT                                                      |
--------------|-----------------------------------------------------------------
```

---

## Bandwidth Throttle & Drop Visualization

When total data throughput exceeds the configured bandwidth cap:

1. **Throttle (`~`)**: If the required delay is ≤ 5 ms, the sensor thread
   sleeps and the delay appears as a `~` span on the `BW-throttle` lane.

2. **Drop (`X`)**: If the delay exceeds 5 ms, the frame is discarded.  An
   `X` marker appears on the `frame-drop` lane.

### Tuning knobs

| Parameter | Effect |
|-----------|--------|
| `bandwidth.global_limit_mbps` | Total system bandwidth cap |
| Per-sensor `bandwidth_limit_mbps` | Individual sensor cap |
| `pipeline.queue_capacity` | Triggers drops when ingress queue is full |

---

## Visualization Module API

### `TimelineVisualizer` (`include/visualization/timeline_visualizer.h`)

```cpp
class TimelineVisualizer {
public:
    // ── Recording (thread-safe) ─────────────────────
    void record_event(lane, start, end, glyph, frame_id);
    void record_marker(lane, time, glyph);
    void set_origin(TimePoint origin);

    // ── Rendering ───────────────────────────────────
    std::string render(width_chars=120, time_window_ms=0);
    void        print(width_chars=120, time_window_ms=0);

    std::string render_waterfall(max_frames=20, width_chars=100);
    void        print_waterfall(max_frames=20, width_chars=100);

    void reset();
};
```

### Integration in `Pipeline`

The `Pipeline` class automatically instruments these events:

| Event | Lane | Glyph |
|-------|------|-------|
| Sensor `generateFrame()` | sensor name (e.g. `front_camera`) | `#` |
| Preprocess stage | `preprocess` | `P` |
| Detection stage | `detection` | `D` |
| Tracking stage | `tracking` | `T` |
| BW throttle sleep | `BW-throttle` | `~` |
| Frame dropped | `frame-drop` | `X` |

### Accessing the visualizer

```cpp
adas::Pipeline pipeline(cfg);
pipeline.run();  // prints metrics + timeline + waterfall automatically

// Or render manually with custom parameters:
pipeline.visualizer().print(/*width=*/150, /*window_ms=*/1000.0);
pipeline.visualizer().print_waterfall(/*max_frames=*/30);
```

---

## Extending the Visualizer

### Adding a new lane

Anywhere you have a `TimelineVisualizer&`, just record to a new lane name —
it will appear automatically:

```cpp
visualizer.record_event("fusion", start, end, 'F', frame->frame_id);
```

### Adding new pipeline stages

1. Create a new stage class (e.g. `FusionStage`).
2. Add it to the `stages_` vector in `Pipeline::Pipeline()`.
3. Add a glyph mapping in `Pipeline::process_frame()`:

```cpp
static const std::map<std::string, char> stage_glyph = {
    {"preprocess", 'P'}, {"detection", 'D'},
    {"tracking", 'T'},   {"fusion", 'F'},      // ← new
};
```

### Custom rendering parameters

```cpp
// Wider output for large monitors
visualizer.print(/*width=*/200, /*time_window_ms=*/2000.0);

// Focus on first 500ms only
std::string diagram = visualizer.render(120, 500.0);
```

### Future enhancements (TODO)

- **SVG / HTML export** — render to a visual file instead of text
- **Thread-lane view** — show which worker thread ran each frame
- **Gantt chart mode** — export to a format tools like PlotJuggler can read
- **Interactive mode** — ncurses-based live timeline during execution