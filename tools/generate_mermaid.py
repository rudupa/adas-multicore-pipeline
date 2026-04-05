#!/usr/bin/env python3
"""
Generate Mermaid diagrams from adas_pipeline_config.json (ADAS schema).

Outputs a Markdown file containing:
1) Execution pipeline diagram (Sense / Plan / Act tree)
2) ECU/network topology diagram

Usage:
  python tools/generate_mermaid.py \
    --input config/adas_pipeline_config.json \
    --output docs/system/ADAS_Pipeline_generated.md \
        --scenario-id adas_realtime_distributed \
        --format markdown

Or emit standalone Mermaid files:
    python tools/generate_mermaid.py \
        --input config/adas_pipeline_config.json \
        --output docs/system/adas_pipeline \
        --scenario-id adas_realtime_distributed \
        --format mmd
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any, Dict, List, Tuple


def _sanitize_id(raw: str) -> str:
    out = []
    for ch in raw:
        if ch.isalnum() or ch == "_":
            out.append(ch)
        else:
            out.append("_")
    s = "".join(out)
    if not s:
        s = "node"
    if s[0].isdigit():
        s = f"n_{s}"
    return s


def _timing_label(step: Dict[str, Any]) -> str:
    t = step.get("execution_times", {})
    if not isinstance(t, dict):
        return ""
    if all(k in t for k in ("min_ms", "avg_ms", "max_ms")):
        return f"\\n[{t['min_ms']}/{t['avg_ms']}/{t['max_ms']} ms]"
    return ""


def _kind_class(kind: str) -> str:
    if kind == "serial":
        return "serial"
    if kind == "parallel":
        return "parallel"
    return "task"


def build_pipeline_mermaid(scenario: Dict[str, Any]) -> str:
    pipeline = scenario.get("pipeline")
    if not isinstance(pipeline, dict):
        raise ValueError("Scenario has no valid 'pipeline' object")

    lines: List[str] = [
        "flowchart TD",
        "  %% Auto-generated from config",
    ]
    classes: List[Tuple[str, str]] = []

    def visit(step: Dict[str, Any], parent_id: str | None = None, idx: int = 0) -> str:
        sid_raw = str(step.get("id", f"step_{idx}"))
        sid = _sanitize_id(sid_raw)
        name = str(step.get("name", sid_raw)).replace('"', "'")
        kind = str(step.get("kind", "task"))
        label = f"{name}{_timing_label(step)}"

        lines.append(f'  {sid}["{label}"]')
        classes.append((sid, _kind_class(kind)))

        if parent_id:
            lines.append(f"  {parent_id} --> {sid}")

        children = step.get("steps", [])
        if isinstance(children, list) and children:
            prev_child_id = None
            for cidx, child in enumerate(children):
                cid = visit(child, sid, cidx)
                if kind == "serial" and prev_child_id is not None:
                    lines.append(f"  {prev_child_id} ==> {cid}")
                prev_child_id = cid

        return sid

    visit(pipeline)

    # Mermaid classes
    lines += [
        "",
        "  classDef task fill:#1f2937,stroke:#60a5fa,color:#f8fafc;",
        "  classDef serial fill:#14532d,stroke:#4ade80,color:#f8fafc;",
        "  classDef parallel fill:#78350f,stroke:#facc15,color:#f8fafc;",
    ]
    for nid, cls in classes:
        lines.append(f"  class {nid} {cls};")

    return "\n".join(lines)


def build_topology_mermaid(data: Dict[str, Any]) -> str:
    top = data.get("topology", {})
    nodes = top.get("nodes", [])
    links = top.get("links", [])

    lines: List[str] = [
        "flowchart LR",
        "  %% ECU / node topology",
    ]

    for n in nodes:
        nid = _sanitize_id(str(n.get("id", "node")))
        name = str(n.get("name", nid)).replace('"', "'")
        ntype = str(n.get("type", "node"))
        cores = n.get("cores", "?")
        sched = str(n.get("scheduler", "?")).replace('"', "'")
        label = f"{name}\\n({ntype}, cores={cores}, sched={sched})"
        lines.append(f'  {nid}["{label}"]')

    for l in links:
        src = _sanitize_id(str(l.get("from", "")))
        dst = _sanitize_id(str(l.get("to", "")))
        bw = l.get("bandwidth_mbps", "?")
        lat = l.get("latency_ms", "?")
        if src and dst:
            lines.append(f'  {src} -- "{bw} Mbps / {lat} ms" --> {dst}')

    return "\n".join(lines)


def _pick_scenario(data: Dict[str, Any], scenario_id: str | None) -> Dict[str, Any]:
    scenarios = data.get("scenarios", [])
    if not isinstance(scenarios, list) or not scenarios:
        raise ValueError("No scenarios found in input JSON")

    if scenario_id is None:
        return scenarios[0]

    for s in scenarios:
        if s.get("id") == scenario_id:
            return s

    available = ", ".join(str(s.get("id", "<unknown>")) for s in scenarios)
    raise ValueError(f"Scenario '{scenario_id}' not found. Available: {available}")


def generate_markdown(data: Dict[str, Any], scenario: Dict[str, Any]) -> str:
    scenario_name = scenario.get("name", scenario.get("id", "scenario"))

    pipeline_mermaid = build_pipeline_mermaid(scenario)
    topo_mermaid = build_topology_mermaid(data)

    return f"""# ADAS Pipeline Mermaid (Generated)\n\n## Scenario\n\n- id: {scenario.get('id', '')}\n- name: {scenario_name}\n- scope: {scenario.get('scope', '')}\n\n## Execution Pipeline\n\n```mermaid\n{pipeline_mermaid}\n```\n\n## Topology\n\n```mermaid\n{topo_mermaid}\n```\n"""


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate Mermaid diagrams from ADAS config")
    parser.add_argument("--input", required=True, help="Path to input JSON config")
    parser.add_argument("--output", required=True, help="Output path (markdown file or mmd prefix)")
    parser.add_argument("--scenario-id", default=None, help="Scenario ID to render (default: first)")
    parser.add_argument(
        "--format",
        choices=["markdown", "mmd"],
        default="markdown",
        help="Output format: markdown (single file) or mmd (two .mmd files)",
    )

    args = parser.parse_args()

    in_path = Path(args.input)
    out_path = Path(args.output)

    if not in_path.exists():
        raise SystemExit(f"Input file not found: {in_path}")

    with in_path.open("r", encoding="utf-8") as f:
        data = json.load(f)

    scenario = _pick_scenario(data, args.scenario_id)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    if args.format == "markdown":
        md = generate_markdown(data, scenario)
        out_path.write_text(md, encoding="utf-8")
        print(f"Generated Mermaid markdown: {out_path}")
    else:
        # --output is treated as file prefix for two files:
        # <prefix>_pipeline.mmd and <prefix>_topology.mmd
        pipeline_path = out_path.with_name(out_path.name + "_pipeline.mmd")
        topology_path = out_path.with_name(out_path.name + "_topology.mmd")

        pipeline_mmd = build_pipeline_mermaid(scenario) + "\n"
        topology_mmd = build_topology_mermaid(data) + "\n"

        pipeline_path.write_text(pipeline_mmd, encoding="utf-8")
        topology_path.write_text(topology_mmd, encoding="utf-8")

        print(f"Generated Mermaid file: {pipeline_path}")
        print(f"Generated Mermaid file: {topology_path}")


if __name__ == "__main__":
    main()
