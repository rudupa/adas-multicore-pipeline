# CDI Architecture Diagrams (Text)

<!-- TOC START -->
- [1. Full CDI Pipeline — Data Flow](#1-full-cdi-pipeline--data-flow)
- [2. CDI Integration with Deterministic Stack](#2-cdi-integration-with-deterministic-stack)
- [3. ASIL Decomposition](#3-asil-decomposition)
- [4. SSH Mode Transition State Machine](#4-ssh-mode-transition-state-machine)
- [5. SSH Override Triggers](#5-ssh-override-triggers)
- [6. CDNP Negotiation Timeline](#6-cdnp-negotiation-timeline)
- [7. CDNP State Machines](#7-cdnp-state-machines)
- [8. SOC Semantic Package Structure](#8-soc-semantic-package-structure)
- [9. SCE Validation Pipeline](#9-sce-validation-pipeline)
- [10. Version 1 vs Version 2 Integration](#10-version-1-vs-version-2-integration)
<!-- TOC END -->

---

<div style="page-break-before: always;"></div>

## 1. Full CDI Pipeline — Data Flow

```
┌───────────────────────────────────────────────────────────────────────────────┐
│                             CDI Pipeline                                      │
│ ┌──────────────┐    ┌───────────────┐    ┌────────────┐    ┌──────────────┐   │
│ │  Cognitive   │    │   Semantic    │    │            │    │              │   │
│ │   Agent      │───►│   Adapter     │───►│    SCE     │───►│    CDNP      │   │
│ │ (VLA / VLM / │    │ (model-       │    │ (11 det.   │    │(negotiation  │   │
│ │  LLM / FM)   │    │  specific)    │    │  validators│    │ protocol)    │   │
│ └──────┬───────┘    └──────┬────────┘    └─────┬──────┘    └──────┬───────┘   │
│        │             SOC Package           Verdict +          Negotiated      │
│        │            (12 semantic          Compiled            Constraints     │
│        │              families)          Constraints             │            │
│        │                                     │              ┌────▼─────┐      │
│        │                                ┌────▼────┐         │   DN     │      │
│        │                                │  USTES  │         │ (motion  │      │
│        │                                │ (audit  │         │  planner)│      │
│        │                                │  trail) │         └────┬─────┘      │
│        │                                └─────────┘              │            │
│        │                                     ▲             Trajectory         │
│        │                                     │                   │            │
│        │                                     │              ┌────▼─────┐      │
│        │                                     └──────────────│   SSH    │      │
│        │                                        log         │ (safety  │      │
│        │            SSH monitors all ◄──────────────────────│supervisor│      │
│        │            components above                        │  hub)    │      │
│        │                                                    └────┬─────┘      │
│        │                                           Approved trajectory        │
│        │                                              or fallback             │
│        ▼                                                         │            │
│   Next inference cycle                                      Actuators         │
└───────────────────────────────────────────────────────────────────────────────┘
Legend:
  ───►  Data flow   - - ► Monitoring / logging
  SSH monitors: SCE, CDNP, CN (cognitive agent), DN (planner)
  SSH can inject fallback constraints directly into DN, bypassing CDNP
```

**Description:** This diagram presents the complete end-to-end data flow through the Cognitive Driving Intelligence (CDI) pipeline. The flow begins at the **Cognitive Agent** — which may be any Vision-Language-Action model (VLA), Vision-Language Model (VLM), Large Language Model (LLM), or Foundation Model (FM) — producing raw inference output from sensor inputs. This raw output is model-specific and non-standardised; it passes through the **Semantic Adapter**, a model-specific translation layer that converts heterogeneous AI outputs into a unified, model-agnostic **SOC Package** structured across 12 semantic families (SceneState, IntentHypothesis, TrajectorySet, BehaviorSuggestion, RiskAssessment, UncertaintySummary, Justification, SocialInteraction, AffordanceGrid, CounterfactualScenarios, TemporalForecast, and ModelArtifact). The SOC Package then enters the **Semantic Compliance Engine (SCE)**, which runs 11 sequential deterministic validators — from Schema Validation through Historical Coherence — each flagging PASS, WARN, or FAIL. The SCE produces a three-tier verdict (ACCEPT, LIMITED, or REJECT) plus compiled typed constraint primitives (speed envelope, lateral bounds, gap constraints). These constraints are forwarded to the **Cognitive Driving Negotiation Protocol (CDNP)**, which conducts a bounded, multi-round negotiation with the **Deterministic Node (DN)** — the classical motion planner — to agree on executable trajectory constraints. The **Safety Supervisor Hub (SSH)** oversees the entire pipeline: it monitors the SCE, CDNP, the Cognitive Agent (CN), and the DN. SSH can inject fallback constraints directly into the DN, bypassing CDNP entirely, if any safety triggers fire. Every decision — verdicts, negotiation rounds, overrides — is recorded by the **USTES** (Unified Safety-Traceability and Event Store) audit trail for post-hoc analysis and regulatory compliance. After the DN produces a trajectory, SSH performs a final safety check before forwarding the approved trajectory (or a fallback safe trajectory) to the vehicle actuators. The cognitive agent's inference cycle then restarts with fresh sensor data.

<div style="page-break-before: always;"></div>

## 2. CDI Integration with Deterministic Stack

```
                        ┌─────────────────────────────┐
                        │          Sensors            │
                        │  Camera  LiDAR  Radar  IMU  │
                        └──────────────┬──────────────┘
                                       ▼
                        ┌───────────────────────────────┐
                        │     Environment Fusion        │
                        │  (deterministic world model)  │
                        └──────┬───────────────┬────────┘
                               │               │
                    world model│               │world model
                     snapshot  │               │
                               ▼               │
         ┌─────────────────────────────────┐   │
         │         CDI Pipeline            │   │
         │  ┌──────────┐ ┌─────┐ ┌──────┐  │   │
         │  │Cognitive │ │     │ │      │  │   │
         │  │Agent +   │►│ SCE │►│ CDNP │──┼───┼──► negotiated
         │  │Semantic  │ │     │ │      │  │   │    constraints
         │  │Adapter   │ └─────┘ └──────┘  │   │        │
         │  └──────────┘                   │   │        │
         └─────────────────────────────────┘   │        │
                  ▲                            │        │
                  │ monitor all                │        │
                  │                            ▼        ▼
                  │                    ┌────────────────────────┐
                  │                    │     Driving Path       │
                  │                    │ (SIT · TrjPln · ManPln)│
                  │                    └───────────┬────────────┘
                  │                                ▼
                  │                    ┌─────────────────────────┐
                  │                    │  Trajectory Plausibility│
                  │                    └───────────┬─────────────┘
                  │                                ▼
                  │                    ┌────────────────────────┐
                  └────────────────────│         SSH            │
                                       │  (Safety Supervisor)   │
                         override ───► └───────────┬────────────┘
                        to Driving Path            ▼
                                       ┌────────────────────────┐
                                       │         ACI            │
                                       │ (Actuation / Action    │
                                       │      Execution)        │
                                       └───────────┬────────────┘
                                                   ▼
                                            Vehicle Motion
```

**Description:** This diagram illustrates how the CDI pipeline integrates into a conventional deterministic autonomous-driving software stack without replacing any existing safety-critical component. The flow starts with the vehicle's **sensor suite** (cameras, LiDAR, radar, IMU), whose raw data is consumed by the **Environment Fusion** module — a fully deterministic world-model that produces a consistent, time-stamped representation of the driving scene. This world model is shared along two parallel paths: (1) it feeds directly into the classical **Driving Path** modules (Scene Interpretation / SIT, Trajectory Planning / TrjPln, and Manoeuvre Planning / ManPln), and (2) a snapshot of it is provided to the **CDI Pipeline** as the ground-truth reference against which cognitive outputs are validated. Inside the CDI pipeline, the Cognitive Agent and Semantic Adapter produce the SOC Package, which passes through the SCE and CDNP to yield negotiated constraints. These constraints are delivered to the Driving Path modules as *additional inputs* — they augment the deterministic planner's decision-making but never override it unilaterally. The Driving Path generates a candidate trajectory using classical Model Predictive Control (MPC), which then undergoes **Trajectory Plausibility** checking (kinematic/dynamic feasibility verification). The **Safety Supervisor Hub (SSH)** sits downstream of Trajectory Plausibility and upstream of the **ACI** (Actuation / Action Execution) layer. SSH continuously monitors all upstream CDI components and has the authority to override the Driving Path by injecting fallback constraints or switching to deterministic-only mode. Only after SSH approval does the trajectory reach the ACI layer, which translates it into steering, throttle, and brake commands that produce **Vehicle Motion**. This architecture ensures that the cognitive channel can *inform* but never *command* vehicle motion directly.

<div style="page-break-before: always;"></div>

## 3. ASIL Decomposition

```
┌───────────────────────────────────────────────────────────────────────┐
│                      CDI Safety Architecture                          │
│                                                                       │
│  ┌ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ┐   ┌──────────────────────────────────┐   │
│  ╎ Cognitive Channel       ╎   │   Deterministic Channel          │   │
│  ╎ QM (not ASIL-rated)     ╎   │   ASIL-D                         │   │
│  ╎                         ╎   │                                  │   │
│  ╎  ┌──────────────────┐   ╎   │   ┌──────────────────────────┐   │   │
│  ╎  │ VLA / VLM /      │   ╎   │   │ SCE  (11 validators)     │   │   │
│  ╎  │ World Model      │   ╎   │   │ CDNP (state machines)    │   │   │
│  ╎  └────────┬─────────┘   ╎   │   │ DN   (motion planner)    │   │   │
│  ╎           │             ╎   │   │ SSH  (supervisor)        │   │   │
│  ╎  ┌────────▼─────────┐   ╎   │   │ Sensor Fusion (det.)     │   │   │
│  ╎  │ Semantic Adapter │   ╎   │   │ USTES (audit)            │   │   │
│  ╎  └────────┬─────────┘   ╎   │   └──────────────────────────┘   │   │
│  └ ─ ─ ─ ─ ─ │─ ─ ─ ─ ─ ─ ─┘   │                                  │   │
│              │                 │                                  │   │
│              │ SOC Package     │                                  │   │
│              │ (data only —    │                                  │   │
│              │  no execution   │                                  │   │
│              │  authority)     │                                  │   │
│              │                 │                                  │   │
│              └─────────────────┼──► SCE ──► CDNP ──► DN ──► SSH   │   │
│                                │                            │     │   │
│                                └────────────────────────────┼─────┘   │
│                                                             │         │
│                                                         Actuators     │
└───────────────────────────────────────────────────────────────────────┘

Key: ╎ ─ ─ ╎ = dashed boundary (QM, not safety-rated)
     │─────│ = solid boundary (ASIL-D, safety-rated)

Three deterministic gates between cognitive output and actuators:
  Gate 1: SCE   (validates semantics)
  Gate 2: CDNP  (negotiates constraints)
  Gate 3: SSH   (supervises pipeline, can override)
```

**Description:** This diagram illustrates the foundational safety-architecture principle of CDI: **ASIL decomposition** according to ISO 26262. The system is split into two clearly separated channels. The **Cognitive Channel** (shown with a dashed boundary) is rated **QM** (Quality Management) — meaning it is explicitly *not* ASIL-rated. This channel contains the VLA / VLM / World Model and the Semantic Adapter. Because neural networks and foundation models cannot currently be certified to any ASIL level due to their inherent opacity, non-determinism, and susceptibility to adversarial inputs, CDI classifies them as QM components whose outputs carry **no execution authority**. The cognitive channel's sole output is a data-only SOC Package — a structured semantic envelope that crosses the channel boundary as *information*, never as a *command*. The **Deterministic Channel** (shown with a solid boundary) is rated **ASIL-D** — the highest automotive safety integrity level. This channel contains all components that can influence vehicle motion: the SCE (11 deterministic validators), CDNP (deterministic state-machine-based negotiation), DN (classical motion planner), SSH (safety supervisor), deterministic sensor fusion, and the USTES audit trail. Between the cognitive output and the vehicle actuators stand **three deterministic safety gates**: **Gate 1 — SCE** validates the semantic content of the SOC Package against the deterministic world model and physical laws; **Gate 2 — CDNP** negotiates constraints through a bounded, auditable protocol with the motion planner; **Gate 3 — SSH** supervises the entire pipeline end-to-end and has ultimate override authority, including the ability to suppress the cognitive channel entirely. This triple-gate architecture ensures that even if the AI model hallucinates, produces adversarial outputs, or fails silently, no un-validated cognitive output can reach the actuators. The decomposition allows the cognitive channel to evolve rapidly (swap models, update adapters) without re-certifying the safety-critical deterministic channel.

<div style="page-break-before: always;"></div>

## 4. SSH Mode Transition State Machine

```
                          ┌───────────────────────┐
               ┌──────────│    CDI_ENHANCED       │◄─────────── Recovery
               │          │      (Normal)         │             (sustained health
               │          │                       │              for t_recovery,
               │          │ Full cognitive pipe-  │              default 10s of
               │          │ line active:          │              all-ACCEPT)
               │          │ SOC + SCE + CDNP +SSH │
               │          └──────────┬────────────┘
               │                     │
               │           Any 1 of 10
               │          override triggers
               │                     │
               │                     ▼
               │          ┌───────────────────────┐
               │          │  DETERMINISTIC_ONLY   │◄─────────── Partial recovery
               ├─────────►│     (Fallback)        │
               │          │                       │
               │          │ Cognitive channel     │
               │          │ suppressed; planner   │
               │          │ uses deterministic    │
               │          │ perception only       │
               │          └──────────┬────────────┘
               │                     │
               │            Multi-fault or
               │             ODD exit
               │                     │
               │                     ▼
               │          ┌─────────────────────┐
               └─────────►│  MINIMAL_RISK       │   Terminal state
                          │   CONDITION (MRC)   │   Controlled stop
                          │                     │   Requires human or
                          │                     │   remote clearance
                          └─────────────────────┘

Transition rules:
  DEGRADE  (toward safety):  requires ONE trigger   — immediate
  RECOVER  (toward capability): requires SUSTAINED health — delayed
```

**Description:** This diagram defines the **tri-modal state machine** that governs the Safety Supervisor Hub's operating modes and the rules for transitioning between them. The design embodies a core safety principle: **degrade immediately, recover cautiously**.

- **CDI_ENHANCED (Normal):** The full cognitive pipeline is active — the Cognitive Agent produces SOC Packages, the SCE validates them, CDNP negotiates constraints with the deterministic planner, and SSH monitors everything. This mode delivers the richest driving capability, leveraging the AI model's scene understanding, intent prediction, and risk assessment to augment deterministic planning. All 12 SOC families are available; negotiation rounds proceed normally.

- **DETERMINISTIC_ONLY (Fallback):** Entered when **any single one** of 10 SSH override triggers fires (see Diagram 5). The cognitive channel is immediately suppressed — no SOC Packages are processed, CDNP sessions are terminated, and the deterministic planner reverts to using only its own perception pipeline (sensor fusion, rule-based scene interpretation, classical MPC trajectory generation). The vehicle continues to drive safely but loses the cognitive augmentation (e.g., nuanced intent prediction, social interaction understanding, counterfactual reasoning). This transition is **instantaneous** — it occurs within one control cycle (~10–20 ms).

- **MINIMAL_RISK_CONDITION (MRC):** Entered when multiple simultaneous faults are detected, when the vehicle exits its Operational Design Domain (ODD), or when the DETERMINISTIC_ONLY mode itself encounters failures. MRC is a **terminal state** that triggers a controlled stop (e.g., pull over, hazard lights). Exiting MRC requires explicit human intervention or remote clearance — the system cannot autonomously recover from this state.

**Recovery** from DETERMINISTIC_ONLY back to CDI_ENHANCED is deliberately asymmetric. The system must observe **sustained health** — all SCE verdicts returning ACCEPT, no trigger conditions active — for a configurable recovery window (`t_recovery`, default 10 seconds). This hysteresis prevents oscillation between modes when the cognitive channel is intermittently unreliable. Partial recovery (from MRC to DETERMINISTIC_ONLY) follows similar sustained-health requirements.

<div style="page-break-before: always;"></div>

## 5. SSH Override Triggers

```
┌────────────────────────────────────────────────────────────────────┐
│                    SSH — 10 Override Triggers                      │
├─────┬──────────────────────┬───────────────────────────────────────┤
│  #  │ Trigger              │ Condition                             │
├─────┼──────────────────────┼───────────────────────────────────────┤
│  1  │ Anomaly Rate         │ SCE reject/LIMITED rate > threshold   │
│  2  │ Pipeline Latency     │ End-to-end latency > budget           │
│  3  │ Heartbeat Miss       │ CN KeepAlive not received             │
│  4  │ Inference Timeout    │ CN inference exceeds deadline         │
│  5  │ Session Aborts       │ Consecutive CDNP aborts > limit       │
│  6  │ ODD Exit             │ Vehicle exits Operational Design      │
│     │                      │ Domain                                │
│  7  │ Gap Violation        │ Planner violates negotiated           │
│     │                      │ constraints                           │
│  8  │ Abort Ignored        │ DN ignores abort command              │
│  9  │ REJECT Streak        │ Consecutive SCE REJECTs > limit       │
│ 10  │ Confidence Drift     │ CN confidence trend declining         │
├─────┴──────────────────────┴───────────────────────────────────────┤
│ Any single trigger ──► transition to DETERMINISTIC_ONLY            │
│ Multiple triggers  ──► may escalate to MINIMAL_RISK_CONDITION      │
└────────────────────────────────────────────────────────────────────┘

SSH also runs 6 continuous monitors:
  [1] Pipeline Health      [4] Planner Coherence
  [2] Anomaly Rate         [5] ODD Boundary
  [3] CDNP Session         [6] CN Health
```

**Description:** This diagram enumerates the **10 discrete override triggers** that the Safety Supervisor Hub continuously evaluates to determine whether the cognitive channel should be suppressed. Each trigger is independently sufficient to force an immediate transition from CDI_ENHANCED to DETERMINISTIC_ONLY mode:

1. **Anomaly Rate** — The ratio of SCE REJECT and LIMITED verdicts over a sliding time window exceeds a configurable threshold (e.g., >30% within the last 5 seconds). This indicates the cognitive agent is producing systematically unreliable outputs.
2. **Pipeline Latency** — The measured end-to-end latency from sensor snapshot to negotiated constraint exceeds the real-time budget (typically 100–150 ms). Late constraints are stale and potentially dangerous.
3. **Heartbeat Miss** — The Cognitive Node's periodic KeepAlive signal is not received within the expected interval, suggesting a crash, hang, or communication failure.
4. **Inference Timeout** — A single inference cycle from the Cognitive Node exceeds its deadline (e.g., 80 ms), even if the heartbeat is still alive. This catches slow-degradation scenarios.
5. **Session Aborts** — The number of consecutive CDNP negotiation sessions ending in ABORT exceeds a configurable limit (e.g., 3 consecutive aborts), indicating persistent incompatibility between cognitive proposals and planner feasibility.
6. **ODD Exit** — The vehicle has exited its defined Operational Design Domain (geographic area, weather conditions, road types, speed ranges) as determined by the deterministic perception stack.
7. **Gap Violation** — The deterministic planner's trajectory violates the negotiated constraints (e.g., exceeds the agreed speed envelope or lateral bounds), indicating a planner-side fault.
8. **Abort Ignored** — The DN fails to acknowledge or act on an abort command from CDNP, suggesting a control-path failure.
9. **REJECT Streak** — A consecutive run of SCE REJECT verdicts exceeds a limit (e.g., 5 in a row), even if the overall anomaly rate is below threshold. This catches sudden-onset failures.
10. **Confidence Drift** — The CN's reported confidence values show a sustained downward trend over a time window, indicating degrading model performance (e.g., domain shift, sensor degradation).

When **multiple triggers** fire simultaneously, SSH may escalate directly to Minimal Risk Condition rather than DETERMINISTIC_ONLY. In addition to these event-driven triggers, SSH runs **6 continuous background monitors**: Pipeline Health (overall system vitals), Anomaly Rate (rolling statistics), CDNP Session (negotiation health), Planner Coherence (trajectory consistency check), ODD Boundary (geographic/environmental compliance), and CN Health (cognitive node diagnostics).

<div style="page-break-before: always;"></div>

## 6. CDNP Negotiation Timeline

```
Timeline:  t=0ms              t=40ms             t=80ms             t=120ms
           │                  │                  │                  │
           │                  │                  │                  │
     CN ───┤─ConstraintProposal─────────────────►│                  │
           │  maneuver: LANE_CHANGE_LEFT         │                  │
           │  v_max: 14.0 m/s                    │                  │
           │  gap: 5.8 m                         │                  │
           │                  │                  │                  │
     DN ───│──────────────────┤─FeasibilityResp─►│                  │
           │                  │  feasible: true  │                  │
           │                  │  adjust: tighten │                  │
           │                  │  lateral by 0.3m │                  │
           │                  │                  │                  │
     CN ───│──────────────────│──────────────────┤─ConstraintUpdate─►
           │                  │                  │  accepted adj.   │
           │                  │                  │  lateral: -0.3m  │
           │                  │                  │                  │
     DN ───│──────────────────│──────────────────│──FeasibilityFinal
           │                  │                  │  decision: ACCEPT│
           │                  │                  │                  │
           ▼                  ▼                  ▼                  ▼

Full message flow:
  CN ──SOC Package──► SCE ──verdict──► CDNP ◄──negotiation──► DN
                                                                │
                                                           trajectory
                                                                │
                                                               SSH
                                                                │
                                                           Actuators
```

**Description:** This diagram walks through a **concrete, timed example** of a 4-round CDNP negotiation for a lane-change manoeuvre, illustrating how the Cognitive Node (CN) and Deterministic Node (DN) reach agreement within real-time constraints (~120 ms total).

- **Round 1 — ConstraintProposal (t = 0 ms):** The CN, having processed the scene through the VLA and produced a validated SOC Package (SCE verdict: ACCEPT), sends an initial constraint proposal to the DN via CDNP. The proposal specifies the intended manoeuvre (`LANE_CHANGE_LEFT`), the maximum speed envelope (`v_max: 14.0 m/s`), and the minimum gap requirement (`gap: 5.8 m`). These values are derived from the BehaviorSuggestion and RiskAssessment families in the SOC Package.

- **Round 2 — FeasibilityResp (t ≈ 40 ms):** The DN evaluates the proposal against its own deterministic perception, kinematic model, and the current world state. It determines the manoeuvre is feasible (`feasible: true`) but requests an adjustment: tighten the lateral offset by 0.3 m to increase safety margin from an adjacent vehicle detected by the deterministic sensor fusion. This counter-proposal is sent back to the CN.

- **Round 3 — ConstraintUpdate (t ≈ 80 ms):** The CN evaluates the DN's adjustment request. Since the tightened lateral bound is within the acceptable range indicated by the SOC Package's UncertaintySummary, the CN accepts the adjustment and sends a ConstraintUpdate confirming the new lateral offset (`lateral: -0.3 m`).

- **Round 4 — FeasibilityFinal (t ≈ 120 ms):** The DN confirms acceptance of the final constraint set (`decision: ACCEPT`). The negotiation session is complete. The DN now generates a trajectory within the agreed constraints using its classical MPC planner, which then flows through Trajectory Plausibility, SSH (final safety check), and on to the actuators.

The lower portion of the diagram shows the **full upstream message flow**: the CN first produces a SOC Package, which passes through the SCE for validation and verdict, then enters CDNP for the negotiation exchange with the DN. The negotiated trajectory is checked by SSH before reaching the actuators. The entire pipeline — from cognitive inference to actuator command — operates within a strict real-time budget, typically 100–200 ms end-to-end depending on the number of negotiation rounds (configurable, default max: 4 rounds).

<div style="page-break-before: always;"></div>

## 7. CDNP State Machines

```
CN (Cognitive Node) State Machine
═════════════════════════════════

  ┌──────┐  send       ┌──────────┐   counter    ┌──────────┐
  │      │  proposal   │          │   received   │          │
  │ IDLE ├────────────►│ PROPOSED ├─────────────►│ UPDATING │
  │      │             │          │◄─────────────┤          │
  └──────┘             └─────┬────┘  send update └─────┬────┘
                             │                         │
                   ┌─────────┼─────────┐               │
                   │         │         │               │
                   ▼         ▼         │               │
              ┌────────┐ ┌────────┐    │          max rounds
              │  DONE  │ │ABORTED │◄───┘               │
              │        │ │        │◄───────────────────┘
              └────────┘ └────────┘
                accepted   rejected
                           or timeout


DN (Deterministic Node) State Machine
══════════════════════════════════════

  ┌──────┐  proposal    ┌────────────┐
  │      │  received    │            │
  │ IDLE ├─────────────►│ EVALUATING │◄─────────────────────┐
  │      │              │            │                      │
  └──────┘              └──┬────┬──┬─┘                      │
                           │    │  │                        │
                 needs adj.│    │  │feasible         update received
                           │    │  │                        │
                           ▼    │  ▼                        │
            ┌──────────────┐    │  ┌──────────┐             │
            │  COUNTER_    │    │  │ ACCEPTED │             │
            │  PROPOSED    │    │  └──────────┘             │
            └──────┬───────┘    │                           │
                   │            │infeasible                 │
                   │            │                           │
                   │            ▼                           │
                   │      ┌──────────┐                      │
                   │      │ REJECTED │                      │
                   │      └──────────┘                      │
                   │                                        │
                   └────────────────────────────────────────┘
```

**Description:** This diagram defines the **two formal state machines** that govern each side of the CDNP negotiation protocol, ensuring that every negotiation session is deterministic, bounded, and fully auditable.

**CN (Cognitive Node) State Machine:**
- **IDLE:** The CN is waiting. No active negotiation session. Transitions to PROPOSED when a new SOC Package passes SCE validation and the CN generates a ConstraintProposal.
- **PROPOSED:** The CN has sent its initial constraint proposal and is awaiting a response from the DN. If the DN responds with a counter-proposal (e.g., requesting a tighter lateral bound), the CN transitions to UPDATING.
- **UPDATING:** The CN evaluates the DN's counter-proposal against the SOC Package's uncertainty bounds and risk assessment. It can either accept the adjustment and send a ConstraintUpdate (looping back to PROPOSED to await the DN's next response) or determine the adjustment is unacceptable. If the maximum number of negotiation rounds is reached, or if a timeout occurs, the session ends.
- **DONE:** Terminal state indicating the DN has accepted the final constraint set. The negotiation was successful; the DN will generate a trajectory within the agreed constraints.
- **ABORTED:** Terminal state indicating the negotiation failed — either the DN rejected the proposal as infeasible, a timeout occurred, or the maximum number of rounds was exceeded. When a session aborts, the system falls back to the previous valid constraint set or to deterministic-only mode if no prior set exists. Consecutive aborts are counted by SSH as an override trigger (Trigger 5).

**DN (Deterministic Node) State Machine:**
- **IDLE:** The DN is waiting for a constraint proposal from CDNP.
- **EVALUATING:** The DN has received a proposal (or an updated proposal) and is assessing it against its own deterministic perception, kinematic model, traffic rules, and the current world state. This evaluation is fully deterministic — identical inputs always produce identical outputs.
- **COUNTER_PROPOSED:** The DN determines the proposal is partially feasible but requires adjustment (e.g., tighten speed envelope, widen gap). It sends a counter-proposal back toward the CN and waits. When an updated proposal is received, it transitions back to EVALUATING for re-assessment.
- **ACCEPTED:** Terminal state indicating the DN has certified the final constraint set as feasible. Trajectory generation proceeds within the agreed bounds.
- **REJECTED:** Terminal state indicating the proposal is fundamentally infeasible (e.g., the requested manoeuvre violates hard safety limits). The CDNP session is aborted.

Both state machines enforce **bounded negotiation** — a configurable maximum round count (default: 4) and per-round timeout (default: 40 ms) prevent unbounded loops. Every state transition is logged to USTES with timestamp, message content, and outcome for full post-hoc auditability.

<div style="page-break-before: always;"></div>

## 8. SOC Semantic Package Structure

```
┌──────────────────────────────────────────────────────────────────┐
│                      SOC Package                                 │
│                                                                  │
│  version: "2.0"    timestamp: ISO-8601    model_id: "qwen-vl-7b" │
│  capabilities: { ... }                                           │
├──────────────────────────────────────────────────────────────────┤
│                                                                  │
│  REQUIRED FAMILIES (must always be present):                     │
│  ┌─────────────────────────────────────────────────────────────┐ │
│  │ 1. SceneState          agents, map features, occlusions,    │ │
│  │                        weather                              │ │
│  ├─────────────────────────────────────────────────────────────┤ │
│  │ 2. IntentHypothesis    agent_id, intent, confidence,        │ │
│  │                        time horizon                         │ │
│  ├─────────────────────────────────────────────────────────────┤ │
│  │ 4. BehaviorSuggestion  maneuver, hint, target speed,        │ │
│  │                        constraints                          │ │
│  ├─────────────────────────────────────────────────────────────┤ │
│  │ 5. RiskAssessment      hazard type, severity, time-to-risk, │ │
│  │                        confidence                           │ │
│  ├─────────────────────────────────────────────────────────────┤ │
│  │ 6. UncertaintySummary  epistemic, aleatoric, OOD score,     │ │
│  │                        calibration metadata                 │ │
│  └─────────────────────────────────────────────────────────────┘ │
│                                                                  │
│  OPTIONAL FAMILIES (present when model supports them):           │
│  ┌─────────────────────────────────────────────────────────────┐ │
│  │  3. TrajectorySet       multi-modal trajectories +          │ │
│  │                         probabilities + kinematic bounds    │ │
│  │  7. Justification       observations → inferences → action  │ │
│  │  8. SocialInteraction   gap availability, negotiation cues  │ │
│  │  9. AffordanceGrid      spatial traversability layers       │ │
│  │ 10. CounterfactualScen. hypothetical world states + outcomes│ │
│  │ 11. TemporalForecast    multi-step time-aligned predictions │ │
│  │ 12. ModelArtifact       attention maps, rationale spans     │ │
│  └─────────────────────────────────────────────────────────────┘ │
│                                                                  │
│  Also defines:                                                   │
│    - Required bundles (what must always be present together)     │
│    - Structural constraints (cross-family referential integrity) │
│    - Forbidden patterns (e.g., no OVERTAKE when risk >= HIGH)    │
│    - Versioning / capability flags (backward compatibility)      │
│                                                                  │
└──────────────────────────────────────────────────────────────────┘
```

**Description:** This diagram details the internal structure of the **Semantic Output Contract (SOC) Package** — the model-agnostic data envelope that serves as the sole interface between the cognitive channel and the deterministic channel. The SOC Package is CDI's central abstraction: it allows *any* cognitive model (Qwen-VL, GPT, LLaVA, proprietary VLAs, etc.) to communicate with the deterministic stack through a single, standardised format, enabling model-swappability without re-engineering downstream components.

**Package Header:** Every SOC Package carries metadata including a `version` field (currently "2.0"), an ISO-8601 `timestamp` (the instant the cognitive inference was produced), a `model_id` (identifying the source model), and a `capabilities` object that declares which optional semantic families the model can populate. This header enables the SCE to adapt its validator expectations per-model and supports backward compatibility when the SOC schema evolves.

**12 Semantic Families:** The families are divided into required and optional groups:

*Required families* (must always be present in every SOC Package):
1. **SceneState** — Describes the perceived driving scene: detected agents (vehicles, pedestrians, cyclists) with positions, velocities, and classifications; map features (lanes, intersections, traffic signs); occlusions (areas the model cannot observe); and weather/lighting conditions.
2. **IntentHypothesis** — Predicts what other road agents intend to do: each hypothesis includes an `agent_id`, an `intent` label (e.g., LANE_CHANGE_LEFT, YIELD, ACCELERATE), a `confidence` score (0–1), and a `time_horizon` (how far ahead the prediction extends).
3. **BehaviorSuggestion** — The model's recommended ego-vehicle behaviour: manoeuvre type, behavioural hint (e.g., AGGRESSIVE, CAUTIOUS), target speed, and initial constraint bounds.
4. **RiskAssessment** — Identified hazards: hazard type (e.g., COLLISION_RISK, ROAD_DEPARTURE), severity level (LOW / MEDIUM / HIGH / CRITICAL), estimated time-to-risk, and confidence.
5. **UncertaintySummary** — The model's self-reported uncertainty: epistemic uncertainty (what the model doesn't know), aleatoric uncertainty (inherent scene randomness), out-of-distribution (OOD) score, and calibration metadata.

*Optional families* (present when the model's capabilities support them):
3. **TrajectorySet** — Multi-modal trajectory predictions with per-trajectory probabilities and kinematic bounds (used primarily in Version 2 integration).
7. **Justification** — An explainability chain: observations → inferences → action rationale, enabling post-hoc audit of *why* the model suggested a particular behaviour.
8. **SocialInteraction** — Social-driving cues: gap availability between vehicles, negotiation signals (e.g., a vehicle flashing to yield), and interaction predictions.
9. **AffordanceGrid** — A spatial traversability map: layered grid indicating which areas are drivable, marginally drivable, or non-traversable.
10. **CounterfactualScenarios** — Hypothetical "what-if" world states and their predicted outcomes (e.g., "if the pedestrian steps off the curb...").
11. **TemporalForecast** — Multi-step, time-aligned predictions extending the scene state forward in time (e.g., 1 s, 2 s, 5 s lookaheads).
12. **ModelArtifact** — Raw model outputs for debugging and audit: attention maps, rationale text spans, token-level confidence scores.

**Structural rules** further constrain the package: *required bundles* define families that must appear together (e.g., BehaviorSuggestion requires RiskAssessment); *cross-family referential integrity* ensures agent IDs in IntentHypothesis match those in SceneState; *forbidden patterns* disallow contradictory combinations (e.g., a BehaviorSuggestion of OVERTAKE when RiskAssessment severity ≥ HIGH); and *versioning / capability flags* allow the schema to evolve without breaking existing Semantic Adapters.

<div style="page-break-before: always;"></div>

## 9. SCE Validation Pipeline

```
                    ┌───────────────────────────────┐
                    │  SOC Package                  │
                    │  (from Semantic Adapter)      │
                    └──────────────┬────────────────┘
                                   ▼
┌─────────────────────────────────────────────────────────────────────┐
│                   SCE — 11 Deterministic Validators                 │
│   ┌──────────────────────────────────────────────────────────────┐  │
│   │  1. Schema Validator          structural completeness        │  │
│   │  2. Temporal Consistency      timestamp coherence            │  │
│   │  3. Evidence Grounding        Mahalanobis gating vs. world   │  │
│   │                               model                          │  │
│   │  4. Kinodynamic Feasibility   accel / jerk / curvature       │  │
│   │  5. Traffic Rule Compliance   signals, right-of-way          │  │
│   │  6. Cross-Agent Consistency   multi-CN agreement             │  │
│   │  7. Uncertainty Calibration   confidence bounds              │  │
│   │  8. Forbidden Pattern Check   e.g., no OVERTAKE when         │  │
│   │                               risk >= HIGH                   │  │
│   │  9. Cross-Family Integrity    referential consistency        │  │
│   │ 10. ODD Boundary Check        operational domain             │  │
│   │ 11. Historical Coherence      drift detection                │  │
│   └──────────────────────────────────────────────────────────────┘  │
│   Validators run sequentially: 1 → 2 → 3 → ... → 11                 │
│   Any validator can flag: PASS, WARN, or FAIL                       │
└─────────────────────────────┬───────────────────────────────────────┘
                              ▼
                ┌───────────────────────────┐
                │         Verdict           │
                ├───────────┬───────┬───────┤
                │  ACCEPT   │LIMITED│REJECT │
                └─────┬─────┴───┬───┴───┬───┘
                      ▼         ▼       │
            ┌──────────────────────┐    │
            │  Constraint Compiler │    │
            │  → typed primitives  │    │
            │    (speed envelope,  │    │
            │     lateral bounds,  │    │
            │     gap constraints) │    │
            └──────────┬───────────┘    │
                       ▼                ▼
                 ┌───────────┐   ┌───────────────┐
                 │   CDNP    │   │   Fallback    │
                 │(negotiate)│   │ (deterministic│
                 └───────────┘   │  perception   │
                                 │  only)        │
                                 └───────────────┘
                    All decisions ──────┼──► USTES (audit log)
```

**Description:** This diagram details the **Semantic Compliance Engine (SCE)** — the first deterministic safety gate between cognitive output and vehicle motion. The SCE receives a SOC Package from the Semantic Adapter and runs it through **11 sequential deterministic validators**, each of which produces a per-validator flag of PASS, WARN, or FAIL:

1. **Schema Validator** — Verifies structural completeness: all required families present, all mandatory fields populated, correct data types, values within permitted ranges. This is a pure syntactic check.
2. **Temporal Consistency** — Checks that all timestamps within the SOC Package are coherent: the package timestamp is recent (not stale), intra-family timestamps are monotonically ordered, and predicted time horizons don't extend beyond allowable limits.
3. **Evidence Grounding** — Cross-references cognitive claims against the deterministic world model using **Mahalanobis distance gating**. For example, if the SOC Package's SceneState claims a vehicle is at position (x, y) with velocity v, this validator checks whether that claim is statistically consistent with the deterministic sensor fusion's own detections. Claims that fall outside the gating threshold are flagged.
4. **Kinodynamic Feasibility** — Validates that any trajectories or behaviour suggestions are physically achievable: checks acceleration limits, jerk bounds, curvature constraints, and tire-friction models against the vehicle's known dynamic capabilities.
5. **Traffic Rule Compliance** — Assesses whether the suggested behaviour respects traffic laws: red/green signal state, right-of-way rules, speed limits, no-passing zones, and other jurisdiction-specific regulations.
6. **Cross-Agent Consistency** — When multiple cognitive agents (multi-CN configurations) are running, this validator checks that their outputs agree within tolerance. Significant disagreement triggers a WARN or FAIL.
7. **Uncertainty Calibration** — Verifies that the model's reported confidence values are well-calibrated: confidence scores should correlate with actual correctness rates (checked against historical statistics), and the UncertaintySummary's epistemic/aleatoric split should be internally consistent.
8. **Forbidden Pattern Check** — Enforces hard business rules defined in the SOC specification: for example, a BehaviorSuggestion of OVERTAKE is forbidden when any RiskAssessment has severity ≥ HIGH. These are deterministic, non-negotiable constraints.
9. **Cross-Family Integrity** — Ensures referential consistency across semantic families: every `agent_id` referenced in IntentHypothesis must exist in SceneState; every trajectory in TrajectorySet must correspond to a valid BehaviorSuggestion; risk assessments must reference identified hazards.
10. **ODD Boundary Check** — Verifies that the cognitive output is consistent with the vehicle's Operational Design Domain. If the model suggests behaviours that implicitly assume conditions outside the ODD (e.g., highway-speed manoeuvres in a geo-fenced urban zone), it is flagged.
11. **Historical Coherence** — Compares the current SOC Package against a sliding window of recent packages to detect **drift**: sudden, unexplained changes in scene description, intent predictions, or risk levels that may indicate model degradation or adversarial perturbation.

The combined validator outputs yield a **three-tier verdict**: **ACCEPT** (all validators PASS; forward to CDNP for negotiation), **LIMITED** (some validators WARN; forward to CDNP but with tightened constraint bounds compiled by the Constraint Compiler), or **REJECT** (any validator FAIL; the SOC Package is discarded and the system falls back to deterministic perception only). The **Constraint Compiler** transforms ACCEPT/LIMITED verdicts into typed primitives — speed envelope, lateral bounds, gap constraints — that CDNP can negotiate. Every verdict, per-validator flag, and compiled constraint is logged to the **USTES** audit trail for regulatory compliance and post-incident analysis.

<div style="page-break-before: always;"></div>

## 10. Version 1 vs Version 2 Integration

### Version 1 — Intent-Level Integration (Recommended)

```
VLA / VLM                                        Deterministic Stack
─────────                                        ───────────────────

  ┌──────────┐                                  ┌─────────────────┐
  │   VLA    │   intent    ┌─────┐   ┌──────┐   │  Driving Path   │
  │ produces │──proposal──►│ SOC │──►│ SCE  │──►│(SIT · TrjPln ·  │
  │ intent   │             │     │   │      │   │  ManPln)        │
  │ ONLY     │             └─────┘   └──────┘   └────────┬────────┘
  └──────────┘                                           │
                                          ┌──────┐       │ classical
                                          │ CDNP │◄──────┘ MPC
                                          └──┬───┘         trajectory
                                             │
                                             ▼
                                       ┌───────────┐     ┌─────┐
                                       │Trajectory │────►│ SSH │──► ACI
                                       │Plausibil. │     └─────┘
                                       └───────────┘

  VLA does NOT generate trajectories.
  Deterministic planner independently generates trajectories using MPC.
  VLA influences behavior selection only through negotiated constraints.
```

<div style="page-break-before: always;"></div>

### Version 2 — Trajectory-Level Integration

```
VLA / VLM                                        Deterministic Stack
─────────                                        ───────────────────

  ┌───────────┐                                   ┌─────────────────┐
  │   VLA     │  trajectory  ┌─────┐   ┌──────┐   │ Safety Corridor │
  │ produces  │──+ maneuver─►│ SOC │──►│ SCE  │──►│ (hard bounds)   │
  │ trajectory│  proposal    │     │   │      │   └────────┬────────┘
  │ + intent  │              └─────┘   └──────┘            │
  └───────────┘                                            │
                                                   ┌───────▼───────┐
                                                   │     CDNP      │
                                                   │  (negotiate)  │
                                                   └───────┬───────┘
                                                           │
                                                           ▼
                                       ┌───────────┐     ┌─────┐
                                       │Trajectory │────►│ SSH │──► ACI
                                       │Plausibil. │     └─────┘
                                       └───────────┘

  VLA generates trajectory proposals that pass through Safety Corridor.
  CDNP negotiates between cognitive trajectory and deterministic bounds.
  Harder to certify but richer cognitive influence.
```

<div style="page-break-before: always;"></div>

### Side-by-Side Comparison

```
┌────────────────────────┬──────────────────────┬──────────────────────┐
│        Aspect          │  Version 1 (Intent)  │ Version 2 (Traject.) │
├────────────────────────┼──────────────────────┼──────────────────────┤
│ VLA Output             │ Intent only          │ Trajectory + maneuver│
│ Deterministic Planner  │ Generates trajectory │ Validates VLA        │
│                        │ independently        │ trajectory           │
│ Safety Corridor        │ Applies to MPC       │ Applies to VLA       │
│                        │ trajectory           │ trajectory           │
│ Risk                   │ Very low             │ Higher unless SCE +  │
│                        │                      │ corridor are strong  │
│ Cognitive Influence    │ Indirect             │ Direct but           │
│                        │                      │ constrained          │
│ Certification          │ Easier               │ Harder               │
│ Flexibility            │ Lower                │ Higher               │
│ ASIL Decomposition     │ Clean (QM → ASIL-D)  │ Requires stronger    │
│                        │                      │ SCE + corridor       │
│ Latency                │ Lower (fewer tokens) │ Higher (many         │
│                        │                      │ waypoints)           │
│ Recommendation         │ PREFERRED            │ Future evolution     │
└────────────────────────┴──────────────────────┴──────────────────────┘
```

**Description:** These three diagrams compare **two integration strategies** for connecting cognitive agents (VLAs / VLMs) to the deterministic autonomous-driving stack, representing different points on the trade-off spectrum between cognitive influence and certifiability.

**Version 1 — Intent-Level Integration (Recommended):** In this architecture, the VLA produces only **high-level semantic intents** — manoeuvre types (e.g., LANE_CHANGE_LEFT, FOLLOW, YIELD), target speeds, gap requirements, and behavioural hints. The VLA explicitly does **not** generate trajectories (sequences of waypoints). Its output flows through the SOC Package → SCE → CDNP pipeline, where the intent is validated and negotiated into executable constraints. The **deterministic planner** then independently generates a trajectory using classical Model Predictive Control (MPC), constrained by the negotiated bounds but fully responsible for the geometric path. This clean separation makes ASIL decomposition straightforward: the cognitive channel is QM (it only *suggests*), and the trajectory-generating planner is ASIL-D (it *decides* and *executes*). Certification is easier because the safety argument does not depend on the correctness of the AI model — only on the deterministic planner's ability to produce safe trajectories within the negotiated constraints. Latency is lower because intent-level SOC Packages contain fewer tokens than full trajectory proposals. The trade-off is lower cognitive influence: the VLA cannot express nuanced spatial preferences or complex multi-phase manoeuvres.

**Version 2 — Trajectory-Level Integration:** In this architecture, the VLA produces **full trajectory proposals** — sequences of time-stamped waypoints with associated manoeuvre labels — in addition to intent-level data. These trajectory proposals pass through the SOC Package → SCE pipeline but then enter a **Safety Corridor** rather than a classical planner. The Safety Corridor defines hard kinematic and spatial bounds (maximum acceleration, minimum clearance, lane boundaries) that the VLA's trajectory must satisfy. CDNP negotiates between the cognitive trajectory and the deterministic bounds, potentially modifying waypoints to fit within the corridor. This approach gives the VLA **richer, more direct influence** over the vehicle's path — it can express complex spatial manoeuvres, optimise for comfort, and leverage its scene understanding to produce smoother trajectories. However, certification is significantly harder: the safety argument must now demonstrate that the Safety Corridor's bounds are sufficient to prevent unsafe trajectories regardless of what the VLA proposes. The ASIL decomposition is less clean because the cognitive channel's output (trajectory waypoints) has more direct influence on vehicle motion, requiring a stronger SCE and tighter corridor bounds to maintain the same safety level.

**Side-by-Side Comparison:** The table summarises the key trade-offs across 9 dimensions: VLA output type, deterministic planner role, safety corridor application, risk level, cognitive influence degree, certification difficulty, flexibility, ASIL decomposition cleanliness, and latency. **Version 1 is PREFERRED** for current deployment because it offers the cleanest safety argument and easiest certification path. **Version 2 is positioned as a future evolution** for when SCE validators and Safety Corridor bounds have been sufficiently hardened and validated, and when regulatory frameworks for AI-in-the-loop trajectory generation mature.

---

*Diagrams generated from CDI architecture specifications.*
*See ID_Q&A.md and Architecture/0_AI_SafetyArch.md for full technical descriptions.*
