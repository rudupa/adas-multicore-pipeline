#!/usr/bin/env python3

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional

from nicegui import ui


REPO_ROOT = Path(__file__).resolve().parents[1]
CONFIG_DIR = REPO_ROOT / 'config'
DEFAULT_OUTPUT_PATH = CONFIG_DIR / 'system_config.json'

# Allow running as a script: `python tools/system_config_gui.py`
# (in that case, the repo root is not automatically on sys.path)
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from tools.generate_mermaid import build_topology_mermaid


class ConfigError(RuntimeError):
    pass


def _escape_html(text: str) -> str:
    return (
        text.replace('&', '&amp;')
        .replace('<', '&lt;')
        .replace('>', '&gt;')
        .replace('"', '&quot;')
        .replace("'", '&#39;')
    )


def _build_pipeline_timeline_html(scenario: Dict[str, Any], zoom: float = 1.0) -> str:
    pipeline = scenario.get('pipeline', {})
    if not isinstance(pipeline, dict):
        raise ConfigError("Scenario has no valid 'pipeline' object")

    rows: List[Dict[str, Any]] = []

    def avg_ms(step: Dict[str, Any]) -> float:
        t = step.get('execution_times', {})
        if isinstance(t, dict):
            try:
                v = float(t.get('avg_ms', 0.0))
                if v > 0.0:
                    return v
            except Exception:
                return 0.0
        return 0.0

    def place(step: Dict[str, Any], start_ms: float, depth: int) -> float:
        name = str(step.get('name', step.get('id', 'step')))
        kind = str(step.get('kind', 'task'))
        children = step.get('steps', [])
        if not isinstance(children, list):
            children = []

        implicit_duration = 0.0
        if children:
            if kind == 'parallel':
                child_ends = [place(child, start_ms, depth + 1) for child in children if isinstance(child, dict)]
                implicit_duration = max(child_ends) - start_ms if child_ends else 0.0
            else:
                cursor = start_ms
                for child in children:
                    if isinstance(child, dict):
                        cursor = place(child, cursor, depth + 1)
                implicit_duration = cursor - start_ms

        duration = avg_ms(step)
        if duration <= 0.0:
            duration = implicit_duration if implicit_duration > 0.0 else 0.1

        rows.append({
            'name': name,
            'kind': kind,
            'depth': depth,
            'start': start_ms,
            'duration': duration,
        })
        return start_ms + duration

    total_ms = place(pipeline, 0.0, 0)
    total_ms = max(total_ms, 1.0)

    ticks = 10
    tick_html = []
    for i in range(ticks + 1):
        left = (i / ticks) * 100.0
        val = (i / ticks) * total_ms
        tick_html.append(
            f'<div style="position:absolute;left:{left:.3f}%;top:0;bottom:0;border-left:1px solid #cbd5e1;">'
            f'<span style="position:absolute;top:-18px;left:2px;font-size:11px;color:#475569;">{val:.1f} ms</span>'
            '</div>'
        )

    color_by_kind = {
        'serial': '#2563eb',
        'parallel': '#d97706',
        'task': '#16a34a',
    }

    row_html = []
    for r in sorted(rows, key=lambda x: (x['start'], x['depth'])):
        left = (r['start'] / total_ms) * 100.0
        width = max((r['duration'] / total_ms) * 100.0, 0.8)
        color = color_by_kind.get(r['kind'], '#64748b')
        indent_px = int(r['depth']) * 14
        label = _escape_html(str(r['name']))
        kind = _escape_html(str(r['kind']))
        row_html.append(
            '<div style="display:flex;align-items:center;gap:10px;margin:4px 0;">'
            f'<div style="width:280px;min-width:280px;padding-left:{indent_px}px;font-size:12px;color:#0f172a;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;">{label}</div>'
            '<div style="position:relative;flex:1;height:24px;background:#f8fafc;border:1px solid #e2e8f0;border-radius:6px;overflow:hidden;">'
            f'<div style="position:absolute;left:{left:.3f}%;width:{width:.3f}%;top:2px;bottom:2px;background:{color};border-radius:4px;color:white;font-size:11px;display:flex;align-items:center;padding:0 6px;">{kind} {r["duration"]:.2f}ms</div>'
            '</div>'
            '</div>'
        )

    zoom = max(0.5, min(zoom, 3.0))
    width_pct = max(100.0, 100.0 * zoom)
    min_px = int(1200 * zoom)

    return (
        f'<div style="font-family:Segoe UI, Arial, sans-serif;border:1px solid #cbd5e1;border-radius:8px;padding:12px;background:white;width:{width_pct:.1f}%;min-width:{min_px}px;">'
        f'<div style="margin-left:290px;position:relative;height:24px;border-bottom:1px solid #cbd5e1;">{"".join(tick_html)}</div>'
        f'<div style="margin-top:8px;">{"".join(row_html)}</div>'
        '</div>'
    )


def _read_json(path: Path) -> Dict[str, Any]:
    with path.open('r', encoding='utf-8') as f:
        data = json.load(f)
    if not isinstance(data, dict):
        raise ConfigError(f'Root must be a JSON object: {path}')
    return data


def _write_json(path: Path, data: Dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, sort_keys=False) + '\n', encoding='utf-8')


def _is_adas_schema_like(data: Dict[str, Any]) -> bool:
    if not isinstance(data.get('schema_version', 'v2'), str):
        return False
    required = [
        'system',
        'sensors',
        'ecus',
        'real_time_execution_view',
        'timing',
        'bandwidth_budgets',
        'topology',
        'scenarios',
    ]
    if any(k not in data for k in required):
        return False

    if not isinstance(data.get('system'), dict):
        return False
    if not isinstance(data.get('sensors'), dict):
        return False
    if not isinstance(data.get('ecus'), list):
        return False
    if not isinstance(data.get('real_time_execution_view'), dict):
        return False
    if not isinstance(data.get('timing'), dict):
        return False
    if not isinstance(data.get('bandwidth_budgets'), dict):
        return False

    top = data.get('topology')
    if not isinstance(top, dict):
        return False
    if not isinstance(top.get('nodes', []), list):
        return False
    if not isinstance(top.get('links', []), list):
        return False
    if not isinstance(data.get('scenarios', []), list):
        return False
    return True


def _list_templates() -> List[Path]:
    if not CONFIG_DIR.exists():
        return []

    templates: List[Path] = []
    for p in sorted(CONFIG_DIR.glob('*.json')):
        try:
            if _is_adas_schema_like(_read_json(p)):
                templates.append(p)
        except Exception:
            continue
    return templates


def _validate_adas_config(data: Dict[str, Any]) -> None:
    if not _is_adas_schema_like(data):
        raise ConfigError(
            'ADAS config must include system, sensors, ecus, real_time_execution_view, timing, bandwidth_budgets, topology, and scenarios'
        )

    sensors = data.get('sensors', {})
    if not isinstance(sensors.get('cameras', []), list):
        raise ConfigError('sensors.cameras must be a list')
    if not isinstance(sensors.get('radars', []), list):
        raise ConfigError('sensors.radars must be a list')
    if not isinstance(sensors.get('vehicle_state_inputs', []), list):
        raise ConfigError('sensors.vehicle_state_inputs must be a list')

    scenarios = data.get('scenarios', [])
    if not scenarios:
        raise ConfigError('Config must include at least one scenario')

    def validate_step(step: Dict[str, Any], path: str) -> None:
        sid = step.get('id')
        name = step.get('name')
        if not isinstance(sid, str) or not sid.strip():
            raise ConfigError(f"{path}: step must include non-empty 'id'")
        if not isinstance(name, str) or not name.strip():
            raise ConfigError(f"{path}: step must include non-empty 'name'")

        kind = step.get('kind', 'task')
        if kind not in ('serial', 'parallel', 'task'):
            raise ConfigError(f"{path}: invalid kind '{kind}' (expected serial|parallel|task)")

        times = step.get('execution_times', {})
        if times is not None:
            if not isinstance(times, dict):
                raise ConfigError(f"{path}: execution_times must be an object")
            min_ms = float(times.get('min_ms', 0.0))
            avg_ms = float(times.get('avg_ms', 0.0))
            max_ms = float(times.get('max_ms', 0.0))
            if min_ms > avg_ms or avg_ms > max_ms:
                raise ConfigError(f"{path}: execution_times ordering must satisfy min<=avg<=max")

        mapped_nodes = step.get('mapped_nodes', [])
        if mapped_nodes is not None and not isinstance(mapped_nodes, list):
            raise ConfigError(f"{path}: mapped_nodes must be a list")

        children = step.get('steps', [])
        if children is not None:
            if not isinstance(children, list):
                raise ConfigError(f"{path}: steps must be a list")
            for i, child in enumerate(children):
                if not isinstance(child, dict):
                    raise ConfigError(f"{path}.steps[{i}]: child must be an object")
                validate_step(child, f"{path}.steps[{i}]")

    for i, sc in enumerate(scenarios):
        if not isinstance(sc, dict):
            raise ConfigError(f"scenarios[{i}] must be an object")
        if not isinstance(sc.get('id'), str) or not sc['id'].strip():
            raise ConfigError(f"scenarios[{i}]: scenario must include non-empty 'id'")
        if not isinstance(sc.get('name'), str) or not sc['name'].strip():
            raise ConfigError(f"scenarios[{i}]: scenario must include non-empty 'name'")
        pipeline = sc.get('pipeline')
        if not isinstance(pipeline, dict):
            raise ConfigError(f"scenarios[{i}]: missing required pipeline object")
        validate_step(pipeline, f"scenarios[{i}].pipeline")


def _scenario_ids(data: Dict[str, Any]) -> List[str]:
    scenarios = data.get('scenarios', [])
    if not isinstance(scenarios, list):
        return []
    out = []
    for s in scenarios:
        if isinstance(s, dict) and isinstance(s.get('id'), str):
            out.append(s['id'])
    return out


def _pick_scenario(data: Dict[str, Any], scenario_id: Optional[str]) -> Dict[str, Any]:
    scenarios = data.get('scenarios', [])
    if not isinstance(scenarios, list) or not scenarios:
        raise ConfigError('No scenarios in config')

    if scenario_id is None:
        sc = scenarios[0]
        if not isinstance(sc, dict):
            raise ConfigError('Invalid scenarios[0]')
        return sc

    for sc in scenarios:
        if isinstance(sc, dict) and sc.get('id') == scenario_id:
            return sc

    raise ConfigError(f"Scenario not found: {scenario_id}")


def _make_default_config() -> Dict[str, Any]:
    template = CONFIG_DIR / 'adas_pipeline_config.json'
    if template.exists():
        return _read_json(template)
    return {
        'schema_version': 'v2',
        'name': 'adas_system',
        'system': {},
        'sensors': {
            'cameras': [],
            'radars': [],
            'vehicle_state_inputs': [],
        },
        'ecus': [],
        'real_time_execution_view': {},
        'timing': {},
        'bandwidth_budgets': {},
        'topology': {'nodes': [], 'links': []},
        'scenarios': [],
    }


def main() -> None:
    ui.add_head_html(
        '<script src="https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.min.js"></script>'
        '<script>mermaid.initialize({ startOnLoad: false, securityLevel: "loose" });</script>'
        '<style>'
        '.nicegui-content{max-width:none !important;width:100vw !important;padding:8px 12px !important;}'
        '.q-page{max-width:none !important;}'
        '.q-card{width:100%;}'
        '</style>'
    )

    templates = _list_templates()
    initial_template = str(templates[0]) if templates else ''

    state: Dict[str, Any] = _make_default_config()
    selected_scenario_id: Optional[str] = None

    # --- UI elements (assigned during layout build) ---
    status: Any = None
    template_select: Any = None
    output_path_input: Any = None
    schema_input: Any = None
    name_input: Any = None
    scenario_select: Any = None
    system_json: Any = None
    cameras_json: Any = None
    radars_json: Any = None
    vehicle_inputs_json: Any = None
    ecus_json: Any = None
    timing_json: Any = None
    bandwidth_json: Any = None
    nodes_json: Any = None
    links_json: Any = None
    pipeline_json: Any = None
    full_json: Any = None
    pipeline_timeline_container: Any = None
    topo_mermaid_container: Any = None
    timeline_zoom_label: Any = None
    timeline_scroll_select: Any = None

    updating = {'flag': False}
    timeline_zoom = {'value': 1.0}
    timeline_scroll_mode = {'value': 'both'}

    def _scroll_style(mode: str) -> str:
        if mode == 'horizontal':
            return 'overflow-x:auto;overflow-y:hidden;'
        if mode == 'vertical':
            return 'overflow-x:hidden;overflow-y:auto;'
        if mode == 'auto':
            return 'overflow:auto;'
        return 'overflow:auto;'

    def set_status(ok: bool, message: str) -> None:
        status.text = ('OK: ' if ok else 'ERROR: ') + message
        status.style('color: #16a34a;' if ok else 'color: #dc2626;')

    def resolve_output_path() -> Path:
        raw = (output_path_input.value or str(DEFAULT_OUTPUT_PATH.relative_to(REPO_ROOT))).strip()
        p = Path(raw)
        if not p.is_absolute():
            p = REPO_ROOT / p
        return p

    def refresh_form_from_state() -> None:
        nonlocal selected_scenario_id
        if updating['flag']:
            return
        updating['flag'] = True
        try:
            schema_input.value = str(state.get('schema_version', 'v2'))
            name_input.value = str(state.get('name', 'adas_system'))

            system_json.value = json.dumps(state.get('system', {}), indent=2)

            sensors = state.get('sensors', {})
            cameras_json.value = json.dumps(sensors.get('cameras', []), indent=2)
            radars_json.value = json.dumps(sensors.get('radars', []), indent=2)
            vehicle_inputs_json.value = json.dumps(sensors.get('vehicle_state_inputs', []), indent=2)

            ecus_json.value = json.dumps(state.get('ecus', []), indent=2)
            timing_json.value = json.dumps(state.get('timing', {}), indent=2)
            bandwidth_json.value = json.dumps(state.get('bandwidth_budgets', {}), indent=2)

            nodes = state.get('topology', {}).get('nodes', [])
            links = state.get('topology', {}).get('links', [])
            nodes_json.value = json.dumps(nodes, indent=2)
            links_json.value = json.dumps(links, indent=2)

            scenario_select.options = _scenario_ids(state)
            if selected_scenario_id not in scenario_select.options:
                selected_scenario_id = scenario_select.options[0] if scenario_select.options else None
            scenario_select.value = selected_scenario_id

            if selected_scenario_id:
                sc = _pick_scenario(state, selected_scenario_id)
                pipeline_json.value = json.dumps(sc.get('pipeline', {}), indent=2)
            else:
                pipeline_json.value = '{}'

            full_json.value = json.dumps(state, indent=2)
        finally:
            updating['flag'] = False

    def render_visuals() -> None:
        try:
            sc = _pick_scenario(state, selected_scenario_id)
            pipeline_timeline = _build_pipeline_timeline_html(sc, zoom=timeline_zoom['value'])
            topo_mmd = build_topology_mermaid(state)

            pipeline_timeline_container.content = (
                f'<div style="width:100%;max-height:76vh;{_scroll_style(timeline_scroll_mode["value"])}">'
                f'{pipeline_timeline}'
                '</div>'
            )
            topo_mermaid_container.content = f'<pre id="topology_mermaid" class="mermaid">{topo_mmd}</pre>'
            timeline_zoom_label.text = f'Zoom: {timeline_zoom["value"]:.1f}x'

            ui.run_javascript('try { mermaid.run({querySelector: "#topology_mermaid"}); } catch(e) { console.error(e); }')
        except Exception as e:
            pipeline_timeline_container.content = '<pre>Timeline unavailable (config invalid)</pre>'
            topo_mermaid_container.content = '<pre>Mermaid unavailable (config invalid)</pre>'
            set_status(False, str(e))

    def on_zoom_in() -> None:
        timeline_zoom['value'] = min(3.0, round(timeline_zoom['value'] + 0.2, 1))
        render_visuals()

    def on_zoom_out() -> None:
        timeline_zoom['value'] = max(0.5, round(timeline_zoom['value'] - 0.2, 1))
        render_visuals()

    def on_zoom_reset() -> None:
        timeline_zoom['value'] = 1.0
        render_visuals()

    def on_scroll_mode_change(e: Any) -> None:
        timeline_scroll_mode['value'] = str(timeline_scroll_select.value or 'both')
        render_visuals()

    def write_output_if_valid() -> None:
        try:
            _validate_adas_config(state)
            out_path = resolve_output_path()
            _write_json(out_path, state)
            set_status(True, f'Wrote {out_path.relative_to(REPO_ROOT)}')
        except Exception as e:
            set_status(False, str(e))

    def recompute_all() -> None:
        refresh_form_from_state()
        render_visuals()
        write_output_if_valid()

    def on_template_load() -> None:
        nonlocal state
        nonlocal selected_scenario_id
        try:
            if not template_select.value:
                raise ConfigError('Choose a template')
            path = REPO_ROOT / Path(str(template_select.value))
            state = _read_json(path)
            if not _is_adas_schema_like(state):
                raise ConfigError('Selected template is not ADAS schema')
            # default scenario
            ids = _scenario_ids(state)
            selected_scenario_id = ids[0] if ids else None
            recompute_all()
        except Exception as e:
            set_status(False, str(e))

    def on_apply_top_level() -> None:
        if updating['flag']:
            return
        state['schema_version'] = str(schema_input.value or 'v2')
        state['name'] = str(name_input.value or 'adas_system')
        recompute_all()

    def on_apply_adas_sections() -> None:
        if updating['flag']:
            return
        try:
            system_val = json.loads(system_json.value or '{}')
            cameras_val = json.loads(cameras_json.value or '[]')
            radars_val = json.loads(radars_json.value or '[]')
            vehicle_inputs_val = json.loads(vehicle_inputs_json.value or '[]')
            ecus_val = json.loads(ecus_json.value or '[]')
            timing_val = json.loads(timing_json.value or '{}')
            bandwidth_val = json.loads(bandwidth_json.value or '{}')
            nodes_val = json.loads(nodes_json.value or '[]')
            links_val = json.loads(links_json.value or '[]')

            if not isinstance(system_val, dict):
                raise ConfigError('system must be a JSON object')
            if not isinstance(cameras_val, list):
                raise ConfigError('sensors.cameras must be a JSON list')
            if not isinstance(radars_val, list):
                raise ConfigError('sensors.radars must be a JSON list')
            if not isinstance(vehicle_inputs_val, list):
                raise ConfigError('sensors.vehicle_state_inputs must be a JSON list')
            if not isinstance(ecus_val, list):
                raise ConfigError('ecus must be a JSON list')
            if not isinstance(timing_val, dict):
                raise ConfigError('timing must be a JSON object')
            if not isinstance(bandwidth_val, dict):
                raise ConfigError('bandwidth_budgets must be a JSON object')
            if not isinstance(nodes_val, list):
                raise ConfigError('topology.nodes must be a JSON list')
            if not isinstance(links_val, list):
                raise ConfigError('topology.links must be a JSON list')

            state['system'] = system_val
            state['sensors'] = {
                'cameras': cameras_val,
                'radars': radars_val,
                'vehicle_state_inputs': vehicle_inputs_val,
            }
            state['ecus'] = ecus_val
            state['timing'] = timing_val
            state['bandwidth_budgets'] = bandwidth_val
            state.setdefault('topology', {})
            state['topology']['nodes'] = nodes_val
            state['topology']['links'] = links_val
            recompute_all()
        except Exception as e:
            set_status(False, str(e))

    def on_scenario_change(e: Any) -> None:
        nonlocal selected_scenario_id
        selected_scenario_id = scenario_select.value
        refresh_form_from_state()
        render_visuals()
        write_output_if_valid()

    def on_apply_pipeline() -> None:
        if updating['flag']:
            return
        try:
            if not selected_scenario_id:
                raise ConfigError('No scenario selected')
            pipeline_val = json.loads(pipeline_json.value or '{}')
            if not isinstance(pipeline_val, dict):
                raise ConfigError('pipeline must be a JSON object')

            for sc in state.get('scenarios', []):
                if isinstance(sc, dict) and sc.get('id') == selected_scenario_id:
                    sc['pipeline'] = pipeline_val
                    break
            recompute_all()
        except Exception as e:
            set_status(False, str(e))

    def on_apply_full_json() -> None:
        nonlocal state
        nonlocal selected_scenario_id
        if updating['flag']:
            return
        try:
            val = json.loads(full_json.value or '{}')
            if not isinstance(val, dict):
                raise ConfigError('Full config must be a JSON object')
            state = val
            ids = _scenario_ids(state)
            selected_scenario_id = ids[0] if ids else None
            recompute_all()
        except Exception as e:
            set_status(False, str(e))

    # --- Layout ---
    ui.page_title('System Config GUI')

    # Header
    with ui.header().classes('w-full bg-slate-800 text-white p-4'):
        with ui.row().classes('w-full items-center gap-2'):
            ui.label('System Config Generator').classes('text-h5 font-bold')
            ui.separator().classes('bg-slate-600')

    # Control Panel (compact)
    with ui.row().classes('w-full gap-3 p-4 bg-slate-50 items-center flex-wrap'):
        with ui.column().classes('gap-1'):
            template_select = ui.select(
                options=[str(p.relative_to(REPO_ROOT)) for p in templates],
                label='Template',
                value=str(Path(initial_template).relative_to(REPO_ROOT)) if initial_template else None,
            ).classes('w-64')
            ui.button('Load Template', on_click=on_template_load).classes('w-full')

        ui.separator().classes('bg-slate-200 h-12')

        with ui.column().classes('gap-1'):
            ui.label('Config Name').classes('text-xs font-bold')
            with ui.row().classes('gap-1'):
                schema_input = ui.input('schema_version', value=str(state.get('schema_version', 'v2'))).classes('w-32')
                name_input = ui.input('name', value=str(state.get('name', 'adas_system'))).classes('w-64')

        ui.separator().classes('bg-slate-200 h-12')

        with ui.column().classes('gap-1'):
            ui.label('Scenario & Output').classes('text-xs font-bold')
            with ui.row().classes('gap-1'):
                scenario_select = ui.select(options=[], label='Scenario', value=None).classes('w-48')
                scenario_select.on('update:model-value', on_scenario_change)
                ui.button('Apply Config', on_click=on_apply_top_level).classes('bg-blue-600 text-white')

        ui.button('', icon='refresh').props('flat').classes('ml-auto').on_click(recompute_all)

    # Status bar
    status = ui.label('').classes('w-full px-4 py-2 text-sm font-semibold')

    # Output path
    with ui.row().classes('w-full px-4 gap-2 items-center'):
        ui.label('Output file:').classes('font-semibold')
        output_path_input = ui.input('Output file', value=str(DEFAULT_OUTPUT_PATH.relative_to(REPO_ROOT))).classes('flex-grow')

    ui.separator().classes('my-2')

    # Main content: tabbed interface
    with ui.card().classes('w-full'):
        with ui.tabs().classes('w-full') as main_tabs:
            visual_tab = ui.tab('📊 Visual (Timeline + Topology)')
            adas_tab = ui.tab('🚗 ADAS Sections')
            pipeline_tab = ui.tab('🔄 Edit Pipeline')
            json_tab = ui.tab('📝 Raw JSON')

        with ui.tab_panels(main_tabs, value=visual_tab).classes('w-full'):
            # Visual tab: timeline + topology side-by-side
            with ui.tab_panel(visual_tab).classes('w-full p-4'):
                with ui.row().classes('w-full items-center gap-2 mb-2'):
                    ui.button('Zoom -', on_click=on_zoom_out).props('outline')
                    ui.button('Zoom +', on_click=on_zoom_in).props('outline')
                    ui.button('Reset', on_click=on_zoom_reset).props('flat')
                    timeline_zoom_label = ui.label('Zoom: 1.0x').classes('text-sm font-medium')
                    ui.separator().classes('bg-slate-200 h-6')
                    ui.label('Scroll').classes('text-sm')
                    timeline_scroll_select = ui.select(
                        options=['both', 'horizontal', 'vertical', 'auto'],
                        value='both',
                    ).classes('w-40')
                    timeline_scroll_select.on('update:model-value', on_scroll_mode_change)

                with ui.column().classes('w-full gap-4').style('min-height: 78vh;'):
                    with ui.column().classes('w-full gap-2').style('min-width: 0;'):
                        ui.label('Pipeline Execution Timeline').classes('text-h6 font-bold')
                        pipeline_timeline_container = ui.html('').classes('w-full').style('min-height: 52vh;')

                    with ui.column().classes('w-full gap-2').style('min-width: 0;'):
                        ui.label('System Topology (Mermaid)').classes('text-h6 font-bold')
                        topo_mermaid_container = ui.html('').classes('w-full').style('border:1px solid #cbd5e1; border-radius:8px; padding:8px; background:white;')

            # ADAS section editor tab
            with ui.tab_panel(adas_tab).classes('w-full p-4'):
                with ui.row().classes('w-full gap-4'):
                    with ui.column().classes('w-1/2'):
                        ui.label('system').classes('text-h6 font-bold')
                        system_json = ui.textarea('', value='{}').props('rows=8 input-class="font-mono"').classes('w-full')

                        ui.label('sensors.cameras').classes('text-h6 font-bold mt-4')
                        cameras_json = ui.textarea('', value='[]').props('rows=10 input-class="font-mono"').classes('w-full')

                        ui.label('sensors.radars').classes('text-h6 font-bold mt-4')
                        radars_json = ui.textarea('', value='[]').props('rows=10 input-class="font-mono"').classes('w-full')

                    with ui.column().classes('w-1/2'):
                        ui.label('sensors.vehicle_state_inputs').classes('text-h6 font-bold')
                        vehicle_inputs_json = ui.textarea('', value='[]').props('rows=8 input-class="font-mono"').classes('w-full')

                        ui.label('ecus').classes('text-h6 font-bold mt-4')
                        ecus_json = ui.textarea('', value='[]').props('rows=10 input-class="font-mono"').classes('w-full')

                        ui.label('timing').classes('text-h6 font-bold mt-4')
                        timing_json = ui.textarea('', value='{}').props('rows=6 input-class="font-mono"').classes('w-full')

                        ui.label('bandwidth_budgets').classes('text-h6 font-bold mt-4')
                        bandwidth_json = ui.textarea('', value='{}').props('rows=6 input-class="font-mono"').classes('w-full')

                ui.separator().classes('my-3')
                with ui.row().classes('w-full gap-4'):
                    with ui.column().classes('w-1/2'):
                        ui.label('topology.nodes').classes('text-h6 font-bold')
                        nodes_json = ui.textarea('', value='[]').props('rows=8 input-class="font-mono"').classes('w-full')
                    with ui.column().classes('w-1/2'):
                        ui.label('topology.links').classes('text-h6 font-bold')
                        links_json = ui.textarea('', value='[]').props('rows=8 input-class="font-mono"').classes('w-full')

                ui.button('Apply ADAS Section Changes', on_click=on_apply_adas_sections).classes('w-full mt-2 bg-green-600 text-white')

            # Pipeline editor tab
            with ui.tab_panel(pipeline_tab).classes('w-full p-4'):
                ui.label('Selected Scenario Pipeline').classes('text-h6 font-bold')
                pipeline_json = ui.textarea('', value='{}').props('rows=16 input-class="font-mono"').classes('w-full')
                ui.button('Apply Pipeline Changes', on_click=on_apply_pipeline).classes('w-full mt-2 bg-green-600 text-white')

            # Raw JSON editor tab
            with ui.tab_panel(json_tab).classes('w-full p-4'):
                ui.label('Full Configuration (JSON)').classes('text-h6 font-bold')
                full_json = ui.textarea('', value='{}').props('rows=22 input-class="font-mono"').classes('w-full')
                ui.button('Apply Full JSON', on_click=on_apply_full_json).classes('w-full mt-2 bg-blue-600 text-white')

    # Initialize
    ids = _scenario_ids(state)
    selected_scenario_id = ids[0] if ids else None

    # Defer the first recompute until the server event loop is running
    # (prevents "coroutine was never awaited" warnings on some setups)
    ui.timer(0.1, recompute_all, once=True)

    ui.run(title='System Config GUI', reload=False)


if __name__ in {'__main__', '__mp_main__'}:
    main()
