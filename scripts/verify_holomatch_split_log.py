#!/usr/bin/env python3
"""Verify runtime proof for local Holomatch split-screen."""

from __future__ import annotations

import argparse
import math
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable


HEARTBEAT_MARKER = "FRAME_HEARTBEAT"
PAIR_RE = re.compile(r"([A-Za-z][A-Za-z0-9]*)=([^\s]+)")
FPS_RE = re.compile(r"^(\d+)\.(\d+)$")
LAUNCH_RE = re.compile(r"STEFX_HM_SPLIT_LAUNCH:")
STATE_RE = re.compile(
    r"STEFX_HM_SPLIT_STATE: slot=(?P<slot>\d+) players=(?P<players>\d+) "
    r"bots=(?P<bots>\d+) state=(?P<state>\d+) local=(?P<local>\d+) "
    r"bot=(?P<bot>\d+).*?p1Dist=(?P<p1_dist>[-+0-9.eE]+) origin=\((?P<origin>[^)]*)\) "
    r"view=\((?P<view>[^)]*)\) time=(?P<time>\d+)"
)
CMD_RE = re.compile(
    r"STEFX_HM_SPLIT_CMD: client=(?P<client>\d+).*?move=\((?P<move>[^)]*)\)"
    r".*?buttons=0x(?P<buttons>[0-9a-fA-F]+).*?angles=\((?P<angles>[^)]*)\)"
)
REFDEF_RE = re.compile(
    r"STEFX_HM_SPLIT_REFDEF: slot=(?P<slot>\d+) client=(?P<client>\d+).*?origin=\((?P<origin>[^)]*)\)"
)
SNAPSHOT_RE = re.compile(r"STEFX_HM_SPLIT_SNAPSHOT: slot=(?P<slot>\d+).*?added=(?P<added>-?\d+)")
RENDER_ARMED_RE = re.compile(r"STEFX_HM_SPLIT_RENDER: armed players=(?P<players>\d+)")
RENDER_SLOT_RE = re.compile(
    r"STEFX_HM_SPLIT_RENDER: slot=(?P<slot>\d+) external=(?P<external>\d+)"
    r"(?: externalClient=(?P<external_client>-?\d+))?"
    r".*?ref=(?P<x>-?\d+),(?P<y>-?\d+) (?P<w>-?\d+)x(?P<h>-?\d+)"
    r".*?view=\((?P<view>[^)]*)\)"
)
RENDER_DONE_RE = re.compile(
    r"STEFX_HM_SPLIT_RENDER_DONE: slot=(?P<slot>\d+) external=(?P<external>\d+) "
    r"(?:externalClient=(?P<external_client>-?\d+) )?"
    r"drawDelta=(?P<draw_delta>-?\d+) drawAfter=(?P<draw_after>-?\d+) "
    r"cluster=(?P<cluster>-?\d+) cluster2=(?P<cluster2>-?\d+).*?view=\((?P<view>[^)]*)\)"
)
FP_FILTER_RE = re.compile(r"STEFX_HM_SPLIT_FP_FILTER: slot=(?P<slot>\d+)")
SELF_FILTER_RE = re.compile(r"STEFX_HM_SPLIT_SELF_FILTER: slot=(?P<slot>\d+)")
VIEWWEAPON_RE = re.compile(r"STEFX_HM_SPLIT_VIEWWEAPON: slot=(?P<slot>\d+).*?added=(?P<added>-?\d+)")
HUD_RE = re.compile(
    r"STEFX_HM_SPLIT_HUD:(?:.*?slot=(?P<slot>\d+))?"
    r"(?:.*?shared=(?P<shared>\d+))?"
    r"(?:.*?dst=\((?P<x>[-+0-9.eE]+),(?P<y>[-+0-9.eE]+) "
    r"(?P<w>[-+0-9.eE]+)x(?P<h>[-+0-9.eE]+)\))?"
)
HUD_STATUS_RE = re.compile(
    r"STEFX_HM_SPLIT_HUD_STATUS: slot=(?P<slot>\d+) players=(?P<players>\d+) "
    r"valid=(?P<valid>\d+) health=(?P<health>-?\d+) weapon=(?P<weapon>-?\d+) "
    r"score=(?P<score>-?\d+) dst=\((?P<x>[-+0-9.eE]+),(?P<y>[-+0-9.eE]+) "
    r"(?P<w>[-+0-9.eE]+)x(?P<h>[-+0-9.eE]+)\)"
)
HUD_DIVIDER_RE = re.compile(
    r"STEFX_HM_SPLIT_HUD_DIVIDER: players=(?P<players>\d+) "
    r"vertical=\((?P<vx>[-+0-9.eE]+),(?P<vy>[-+0-9.eE]+) (?P<vw>[-+0-9.eE]+)x(?P<vh>[-+0-9.eE]+)\) "
    r"horizontal=\((?P<hx>[-+0-9.eE]+),(?P<hy>[-+0-9.eE]+) (?P<hw>[-+0-9.eE]+)x(?P<hh>[-+0-9.eE]+)\)"
)
XBLOG_AUDIO_RE = re.compile(r"\bxblogaudio\b")
AUDIO_LISTENER_RE = re.compile(
    r"STEFX_HM_AUDIO_LISTENER: slot=(?P<slot>\d+) ent=(?P<ent>-?\d+) "
    r"activeListeners=(?P<active>\d+) compiledListeners=(?P<compiled>\d+) "
    r"mask=0x(?P<mask>[0-9a-fA-F]+)"
)
AUDIO_BACKEND_RE = re.compile(
    r"STEFX_HM_AUDIO_BACKEND: respatialize .*?listener=(?P<listener>\d+) "
    r"activeListeners=(?P<active>\d+) compiledListeners=(?P<compiled>\d+)"
)


@dataclass
class SlotState:
    players: int = 0
    bots: int = 0
    state: int = 0
    local: int = 0
    bot: int = 0
    origin: tuple[float, float, float] | None = None
    view: tuple[float, float, float] | None = None
    p1_dist: float = 0.0
    time: int = 0


@dataclass
class Proof:
    launch_records: list[dict[str, object]] = field(default_factory=list)
    max_armed_players: int = 0
    states: dict[int, SlotState] = field(default_factory=dict)
    state_history: dict[int, list[SlotState]] = field(default_factory=dict)
    cmd_clients: set[int] = field(default_factory=set)
    cmd_attack_clients: set[int] = field(default_factory=set)
    cmd_moves: dict[int, set[tuple[int, int, int]]] = field(default_factory=dict)
    cmd_angles: dict[int, set[tuple[int, int, int]]] = field(default_factory=dict)
    refdef_slots: set[int] = field(default_factory=set)
    refdef_origins: dict[int, tuple[float, float, float]] = field(default_factory=dict)
    snapshot_slots: set[int] = field(default_factory=set)
    snapshot_positive_slots: set[int] = field(default_factory=set)
    render_slots: set[int] = field(default_factory=set)
    render_external_slots: set[int] = field(default_factory=set)
    render_external_clients: dict[int, int] = field(default_factory=dict)
    render_rects: dict[int, tuple[int, int, int, int]] = field(default_factory=dict)
    render_views: dict[int, tuple[float, float, float]] = field(default_factory=dict)
    render_done_slots: set[int] = field(default_factory=set)
    render_done_external_slots: set[int] = field(default_factory=set)
    render_done_external_clients: dict[int, int] = field(default_factory=dict)
    render_done_positive_slots: set[int] = field(default_factory=set)
    render_done_views: dict[int, tuple[float, float, float]] = field(default_factory=dict)
    fp_filter_slots: set[int] = field(default_factory=set)
    self_filter_slots: set[int] = field(default_factory=set)
    self_filter_ref_numbers: dict[int, set[int]] = field(default_factory=dict)
    self_filter_model_parts: dict[int, set[str]] = field(default_factory=dict)
    viewweapon_slots: set[int] = field(default_factory=set)
    viewweapon_positive_slots: set[int] = field(default_factory=set)
    hud_slots: set[int] = field(default_factory=set)
    hud_independent_slots: set[int] = field(default_factory=set)
    hud_rects: dict[int, tuple[float, float, float, float]] = field(default_factory=dict)
    hud_status_slots: set[int] = field(default_factory=set)
    hud_status_valid_slots: set[int] = field(default_factory=set)
    hud_status_rects: dict[int, tuple[float, float, float, float]] = field(default_factory=dict)
    hud_status_values: dict[int, dict[str, int]] = field(default_factory=dict)
    hud_divider_records: list[dict[str, object]] = field(default_factory=list)
    audio_backend_states: list[int] = field(default_factory=list)
    audio_listener_states: list[int] = field(default_factory=list)
    audio_listener_masks: list[int] = field(default_factory=list)
    audio_listener_slots: set[int] = field(default_factory=set)
    audio_listener_active: list[int] = field(default_factory=list)
    audio_listener_compiled: list[int] = field(default_factory=list)
    audio_register_counts: list[int] = field(default_factory=list)
    audio_start_counts: list[int] = field(default_factory=list)
    audio_loop_counts: list[int] = field(default_factory=list)
    audio_respatialize_counts: list[int] = field(default_factory=list)
    audio_voice_counts: list[int] = field(default_factory=list)
    audio_lip_active_counts: list[int] = field(default_factory=list)
    heartbeats: list[dict[str, object]] = field(default_factory=list)
    hud_remaps: int = 0
    max_bots: int = 0
    lines: int = 0


def parse_scalar(value: str) -> int | float | str | list[int]:
    value = value.rstrip(",;")
    fps_match = FPS_RE.match(value)
    if fps_match:
        fraction = fps_match.group(2)
        return int(fps_match.group(1)) + int(fraction) / (10 ** len(fraction))
    if "/" in value:
        fields = value.split("/")
        if all(field.lstrip("-").isdigit() for field in fields):
            return [int(field) for field in fields]
    try:
        return int(value, 0)
    except ValueError:
        return value


def parse_vec3(raw: str) -> tuple[float, float, float] | None:
    parts = [part.strip() for part in raw.split(",")]
    if len(parts) != 3:
        return None
    try:
        return (float(parts[0]), float(parts[1]), float(parts[2]))
    except ValueError:
        return None


def parse_int3(raw: str) -> tuple[int, int, int] | None:
    parts = [part.strip() for part in raw.split(",")]
    if len(parts) != 3:
        return None
    try:
        return (int(parts[0], 0), int(parts[1], 0), int(parts[2], 0))
    except ValueError:
        return None


def parse_lines(lines: Iterable[str]) -> Proof:
    proof = Proof()
    for line in lines:
        proof.lines += 1

        if HEARTBEAT_MARKER in line:
            record: dict[str, object] = {}
            payload = line.split(HEARTBEAT_MARKER, 1)[1]
            for key, value in PAIR_RE.findall(payload):
                record[key] = parse_scalar(value)
            proof.heartbeats.append(record)

        if LAUNCH_RE.search(line):
            record = {}
            for key, value in PAIR_RE.findall(line):
                if value.startswith("'") and value.endswith("'") and len(value) >= 2:
                    value = value[1:-1]
                record[key] = parse_scalar(value)
            proof.launch_records.append(record)

        match = RENDER_ARMED_RE.search(line)
        if match:
            proof.max_armed_players = max(proof.max_armed_players, int(match.group("players")))

        match = STATE_RE.search(line)
        if match:
            slot = int(match.group("slot"))
            state = SlotState(
                players=int(match.group("players")),
                bots=int(match.group("bots")),
                state=int(match.group("state")),
                local=int(match.group("local")),
                bot=int(match.group("bot")),
                origin=parse_vec3(match.group("origin")),
                view=parse_vec3(match.group("view")),
                p1_dist=float(match.group("p1_dist")),
                time=int(match.group("time")),
            )
            previous = proof.states.get(slot)
            if previous is None or state.time >= previous.time:
                proof.states[slot] = state
            proof.state_history.setdefault(slot, []).append(state)
            proof.max_bots = max(proof.max_bots, state.bots)

        match = CMD_RE.search(line)
        if match:
            client = int(match.group("client"))
            move = parse_int3(match.group("move"))
            angles = parse_int3(match.group("angles"))
            proof.cmd_clients.add(client)
            if int(match.group("buttons"), 16) != 0:
                proof.cmd_attack_clients.add(client)
            if move is not None:
                proof.cmd_moves.setdefault(client, set()).add(move)
            if angles is not None:
                proof.cmd_angles.setdefault(client, set()).add(angles)

        match = REFDEF_RE.search(line)
        if match:
            slot = int(match.group("slot"))
            origin = parse_vec3(match.group("origin"))
            proof.refdef_slots.add(slot)
            if origin is not None:
                proof.refdef_origins[slot] = origin

        match = SNAPSHOT_RE.search(line)
        if match:
            slot = int(match.group("slot"))
            proof.snapshot_slots.add(slot)
            if int(match.group("added")) > 0:
                proof.snapshot_positive_slots.add(slot)

        match = RENDER_SLOT_RE.search(line)
        if match:
            slot = int(match.group("slot"))
            view = parse_vec3(match.group("view"))
            rect = (
                int(match.group("x")),
                int(match.group("y")),
                int(match.group("w")),
                int(match.group("h")),
            )
            proof.render_slots.add(slot)
            if int(match.group("external")):
                proof.render_external_slots.add(slot)
            if match.group("external_client") is not None:
                proof.render_external_clients[slot] = int(match.group("external_client"))
            proof.render_rects[slot] = rect
            if view is not None:
                proof.render_views[slot] = view

        match = RENDER_DONE_RE.search(line)
        if match:
            slot = int(match.group("slot"))
            view = parse_vec3(match.group("view"))
            proof.render_done_slots.add(slot)
            if int(match.group("external")):
                proof.render_done_external_slots.add(slot)
            if match.group("external_client") is not None:
                proof.render_done_external_clients[slot] = int(match.group("external_client"))
            if int(match.group("draw_delta")) > 0:
                proof.render_done_positive_slots.add(slot)
            if view is not None:
                proof.render_done_views[slot] = view

        match = FP_FILTER_RE.search(line)
        if match:
            proof.fp_filter_slots.add(int(match.group("slot")))

        match = SELF_FILTER_RE.search(line)
        if match:
            slot = int(match.group("slot"))
            fields = {key: value for key, value in PAIR_RE.findall(line)}
            proof.self_filter_slots.add(slot)
            if "refNumber" in fields:
                try:
                    proof.self_filter_ref_numbers.setdefault(slot, set()).add(int(fields["refNumber"], 0))
                except ValueError:
                    pass
            if "modelPart" in fields:
                proof.self_filter_model_parts.setdefault(slot, set()).add(fields["modelPart"])

        match = VIEWWEAPON_RE.search(line)
        if match:
            slot = int(match.group("slot"))
            proof.viewweapon_slots.add(slot)
            if int(match.group("added")) > 0:
                proof.viewweapon_positive_slots.add(slot)

        match = HUD_RE.search(line)
        if match:
            proof.hud_remaps += 1
            if match.group("slot") is not None:
                slot = int(match.group("slot"))
                proof.hud_slots.add(slot)
                if match.group("shared") == "0":
                    proof.hud_independent_slots.add(slot)
                if match.group("x") is not None:
                    proof.hud_rects[slot] = (
                        float(match.group("x")),
                        float(match.group("y")),
                        float(match.group("w")),
                        float(match.group("h")),
                    )

        match = HUD_STATUS_RE.search(line)
        if match:
            slot = int(match.group("slot"))
            proof.hud_status_slots.add(slot)
            if int(match.group("valid")):
                proof.hud_status_valid_slots.add(slot)
            proof.hud_status_rects[slot] = (
                float(match.group("x")),
                float(match.group("y")),
                float(match.group("w")),
                float(match.group("h")),
            )
            proof.hud_status_values[slot] = {
                "players": int(match.group("players")),
                "health": int(match.group("health")),
                "weapon": int(match.group("weapon")),
                "score": int(match.group("score")),
            }

        match = HUD_DIVIDER_RE.search(line)
        if match:
            proof.hud_divider_records.append(
                {
                    "players": int(match.group("players")),
                    "vertical": (
                        float(match.group("vx")),
                        float(match.group("vy")),
                        float(match.group("vw")),
                        float(match.group("vh")),
                    ),
                    "horizontal": (
                        float(match.group("hx")),
                        float(match.group("hy")),
                        float(match.group("hw")),
                        float(match.group("hh")),
                    ),
                }
            )

        if XBLOG_AUDIO_RE.search(line):
            fields = {key: value for key, value in PAIR_RE.findall(line)}
            for key, target in (
                ("backend", proof.audio_backend_states),
                ("listener", proof.audio_listener_states),
                ("listenerMask", proof.audio_listener_masks),
                ("register", proof.audio_register_counts),
                ("start", proof.audio_start_counts),
                ("loop", proof.audio_loop_counts),
                ("respat", proof.audio_respatialize_counts),
                ("voice", proof.audio_voice_counts),
                ("lipActive", proof.audio_lip_active_counts),
            ):
                if key in fields:
                    value = parse_scalar(fields[key])
                    if isinstance(value, int):
                        target.append(value)

        match = AUDIO_LISTENER_RE.search(line)
        if match:
            proof.audio_listener_slots.add(int(match.group("slot")))
            proof.audio_listener_active.append(int(match.group("active")))
            proof.audio_listener_compiled.append(int(match.group("compiled")))
            proof.audio_listener_masks.append(int(match.group("mask"), 16))

        match = AUDIO_BACKEND_RE.search(line)
        if match:
            proof.audio_listener_active.append(int(match.group("active")))
            proof.audio_listener_compiled.append(int(match.group("compiled")))

    return proof


def unique_origin_count(origins: Iterable[tuple[float, float, float] | None], tolerance: float) -> int:
    unique: list[tuple[float, float, float]] = []
    for origin in origins:
        if origin is None:
            continue
        if all(math.dist(origin, existing) > tolerance for existing in unique):
            unique.append(origin)
    return len(unique)


def slot_movement_distances(history: dict[int, list[SlotState]]) -> dict[int, float]:
    distances: dict[int, float] = {}
    for slot, states in sorted(history.items()):
        origins = [state.origin for state in states if state.origin is not None]
        if len(origins) < 2:
            distances[slot] = 0.0
            continue
        max_distance = 0.0
        for i, origin in enumerate(origins):
            for other in origins[i + 1:]:
                max_distance = max(max_distance, math.dist(origin, other))
        distances[slot] = max_distance
    return distances


def split_quadrant_errors(rects: dict[int, tuple[int, int, int, int]], players: int) -> list[str]:
    errors: list[str] = []
    slots = set(range(players))
    missing_rects = slots - set(rects)
    if missing_rects:
        errors.append(f"missing render viewport rectangles for slots {sorted(missing_rects)}")
        return errors

    for slot in sorted(slots):
        x, y, w, h = rects[slot]
        if w <= 0 or h <= 0:
            errors.append(f"slot {slot} render viewport has non-positive size {w}x{h}")

    unique_rects = {rects[slot] for slot in slots}
    if len(unique_rects) < players:
        errors.append(f"only {len(unique_rects)} unique render viewport rectangles observed; need {players}")

    if players == 4:
        x0, y0, w0, h0 = rects[0]
        x1, y1, w1, h1 = rects[1]
        x2, y2, w2, h2 = rects[2]
        x3, y3, w3, h3 = rects[3]
        if not (x0 == x2 and x1 == x3 and x1 > x0):
            errors.append(f"4P viewport columns are not arranged left/right by slot: {rects}")
        if not (y0 == y1 and y2 == y3 and y2 > y0):
            errors.append(f"4P viewport rows are not arranged top/bottom by slot: {rects}")
        if not (w0 > 0 and w1 > 0 and w2 > 0 and w3 > 0 and abs(w0 - w2) <= 1 and abs(w1 - w3) <= 1):
            errors.append(f"4P viewport column widths are inconsistent: {rects}")
        if not (h0 > 0 and h1 > 0 and h2 > 0 and h3 > 0 and abs(h0 - h1) <= 1 and abs(h2 - h3) <= 1):
            errors.append(f"4P viewport row heights are inconsistent: {rects}")

    return errors


def split_divider_errors(records: list[dict[str, object]], players: int) -> list[str]:
    errors: list[str] = []
    valid = []

    for record in records:
        record_players = record.get("players")
        vertical = record.get("vertical")
        horizontal = record.get("horizontal")
        if not isinstance(record_players, int) or record_players < players:
            continue
        if not isinstance(vertical, tuple) or not isinstance(horizontal, tuple):
            continue

        vx, vy, vw, vh = vertical
        hx, hy, hw, hh = horizontal
        if vw <= 0 or vh <= 0 or hw <= 0 or hh <= 0:
            continue
        if abs((vx + vw * 0.5) - 320.0) > 6.0:
            continue
        if abs((hy + hh * 0.5) - 240.0) > 6.0:
            continue
        if vy > 1.0 or vh < 470.0:
            continue
        if hx > 1.0 or hw < 630.0:
            continue
        valid.append(record)

    if not valid:
        errors.append(
            "missing 4P HUD divider proof centered at 320/240 with full-height vertical and full-width horizontal bars"
        )
    return errors


def numeric(records: list[dict[str, object]], key: str) -> list[float]:
    values: list[float] = []
    for record in records:
        value = record.get(key)
        if isinstance(value, (int, float)):
            values.append(float(value))
    return values


def heartbeat_elapsed_seconds(records: list[dict[str, object]]) -> float | None:
    realtime = numeric(records, "realtime")
    if len(realtime) < 2:
        return None
    return max(0.0, (realtime[-1] - realtime[0]) / 1000.0)


def heartbeat_frame_delta(records: list[dict[str, object]]) -> int | None:
    frames = numeric(records, "completedFrame")
    if len(frames) < 2:
        return None
    return int(frames[-1] - frames[0])


def heartbeat_memory_summary(records: list[dict[str, object]]) -> dict[str, int] | None:
    samples = [record["mem"] for record in records if isinstance(record.get("mem"), list)]
    samples = [sample for sample in samples if len(sample) >= 3]
    if not samples:
        return None
    used = [int(sample[0]) for sample in samples]
    free = [int(sample[1]) for sample in samples]
    largest = [int(sample[2]) for sample in samples]
    return {
        "samples": len(samples),
        "usedFirst": used[0],
        "usedLast": used[-1],
        "usedDelta": used[-1] - used[0],
        "freeMinimum": min(free),
        "largestFreeMinimum": min(largest),
    }


def audio_listener_state_parts(state: int) -> tuple[int, int]:
    return ((state >> 16) & 0xFFFF, state & 0xFFFF)


def audio_summary(proof: Proof) -> dict[str, object]:
    compiled_from_states = [
        audio_listener_state_parts(state)[0]
        for state in proof.audio_listener_states
    ]
    active_from_states = [
        audio_listener_state_parts(state)[1]
        for state in proof.audio_listener_states
    ]
    compiled = proof.audio_listener_compiled + compiled_from_states
    active = proof.audio_listener_active + active_from_states
    return {
        "backendStates": proof.audio_backend_states,
        "listenerStates": proof.audio_listener_states,
        "listenerMasks": proof.audio_listener_masks,
        "listenerSlots": sorted(proof.audio_listener_slots),
        "maxCompiledListeners": max(compiled) if compiled else None,
        "maxActiveListeners": max(active) if active else None,
        "maxRegisterCount": max(proof.audio_register_counts) if proof.audio_register_counts else None,
        "maxStartCount": max(proof.audio_start_counts) if proof.audio_start_counts else None,
        "maxLoopCount": max(proof.audio_loop_counts) if proof.audio_loop_counts else None,
        "maxRespatializeCount": max(proof.audio_respatialize_counts) if proof.audio_respatialize_counts else None,
        "maxVoiceCount": max(proof.audio_voice_counts) if proof.audio_voice_counts else None,
        "maxLipActive": max(proof.audio_lip_active_counts) if proof.audio_lip_active_counts else None,
    }


def verify_audio(proof: Proof, args: argparse.Namespace) -> list[str]:
    errors: list[str] = []

    if args.require_audio_backend:
        if not proof.audio_backend_states:
            errors.append("missing xblogaudio backend state proof")
        elif not any(state == 4 for state in proof.audio_backend_states):
            errors.append(f"audio backend never reached state 4; observed={proof.audio_backend_states}")

    if args.require_audio_listeners:
        summary = audio_summary(proof)
        max_compiled = summary["maxCompiledListeners"]
        max_active = summary["maxActiveListeners"]
        if not isinstance(max_compiled, int) or max_compiled < args.min_audio_compiled_listeners:
            errors.append(
                f"audio compiled listener max {max_compiled} below required {args.min_audio_compiled_listeners}"
            )
        if not isinstance(max_active, int) or max_active < args.min_audio_active_listeners:
            errors.append(
                f"audio active listener max {max_active} below required {args.min_audio_active_listeners}"
            )
        if args.required_audio_listener_mask is not None:
            if not proof.audio_listener_masks:
                errors.append("missing audio listener update mask proof")
            elif not any((mask & args.required_audio_listener_mask) == args.required_audio_listener_mask
                         for mask in proof.audio_listener_masks):
                masks = [f"0x{mask:08x}" for mask in proof.audio_listener_masks]
                errors.append(
                    f"audio listener masks {masks} never covered required mask 0x{args.required_audio_listener_mask:08x}"
                )

    if args.min_audio_starts is not None:
        max_start = max(proof.audio_start_counts) if proof.audio_start_counts else None
        if max_start is None or max_start < args.min_audio_starts:
            errors.append(f"audio start count max {max_start} below required {args.min_audio_starts}")
    if args.min_audio_voice_starts is not None:
        max_voice = max(proof.audio_voice_counts) if proof.audio_voice_counts else None
        if max_voice is None or max_voice < args.min_audio_voice_starts:
            errors.append(f"audio voice-start count max {max_voice} below required {args.min_audio_voice_starts}")
    if args.require_audio_lip_active:
        max_lip = max(proof.audio_lip_active_counts) if proof.audio_lip_active_counts else None
        if max_lip is None or max_lip <= 0:
            errors.append(f"audio lip-active proof missing or zero; maxLipActive={max_lip}")

    return errors


def verify(proof: Proof, args: argparse.Namespace) -> list[str]:
    errors: list[str] = []
    all_slots = set(range(args.players))
    local_slots = set(range(1, args.players))
    command_slots = all_slots if args.require_p1_virtual_controls else local_slots

    if proof.max_armed_players < args.players:
        errors.append(f"missing renderer armed proof for {args.players} players")

    if args.require_launch:
        valid_launches = []
        for record in proof.launch_records:
            players = record.get("players")
            local_players = record.get("localPlayers")
            if (
                record.get("mode") == "holomatch"
                and isinstance(players, int)
                and players >= args.players
                and isinstance(local_players, int)
                and local_players >= args.players
                and int(record.get("split", 0)) == 1
                and int(record.get("virtual", 0)) == 1
                and int(record.get("virtualP1", 0)) == 1
            ):
                valid_launches.append(record)
        if not valid_launches:
            errors.append(
                "missing 4P local Holomatch launch proof "
                "(STEFX_HM_SPLIT_LAUNCH mode=holomatch split=1 players/localPlayers>=4 virtual=1 virtualP1=1)"
            )
        elif args.require_launch_source and not any(
            record.get("source") == args.require_launch_source for record in valid_launches
        ):
            errors.append(
                f"4P local Holomatch launch proof never reported source={args.require_launch_source}"
            )

    missing_state = all_slots - set(proof.states)
    if missing_state:
        errors.append(f"missing split state for slots {sorted(missing_state)}")

    for slot in sorted(all_slots):
        state = proof.states.get(slot)
        if not state:
            continue
        if state.players < args.players:
            errors.append(f"slot {slot} state reported only {state.players} players")
        if state.state < args.min_client_state:
            errors.append(f"slot {slot} client state {state.state} is below {args.min_client_state}")
        if slot in local_slots and state.local != 1:
            errors.append(f"slot {slot} is not marked as a local split client")
        if args.require_local_non_bot and slot in local_slots and state.bot != 0:
            errors.append(f"slot {slot} is marked as both local split client and bot")

    if proof.max_bots < args.min_bots:
        errors.append(f"max observed bot count {proof.max_bots} is below {args.min_bots}")

    missing_cmds = command_slots - proof.cmd_clients
    if missing_cmds:
        errors.append(f"missing virtual usercmds for clients {sorted(missing_cmds)}")

    if args.require_attack:
        missing_attacks = command_slots - proof.cmd_attack_clients
        if missing_attacks:
            errors.append(f"missing attack-button proof for clients {sorted(missing_attacks)}")

    if args.require_unique_controls:
        missing_move_profiles = command_slots - set(proof.cmd_moves)
        missing_angle_profiles = command_slots - set(proof.cmd_angles)
        if missing_move_profiles:
            errors.append(f"missing movement command profiles for clients {sorted(missing_move_profiles)}")
        if missing_angle_profiles:
            errors.append(f"missing angle command profiles for clients {sorted(missing_angle_profiles)}")

        rightmove_values = {
            move[1]
            for client in command_slots
            for move in proof.cmd_moves.get(client, set())
        }
        if len(rightmove_values) < len(command_slots):
            errors.append(
                f"only {len(rightmove_values)} distinct rightmove values observed; need {len(command_slots)}"
            )
        if any(
            not any(move[1] < 0 for move in proof.cmd_moves.get(client, set()))
            and not any(move[1] > 0 for move in proof.cmd_moves.get(client, set()))
            for client in command_slots
        ):
            errors.append("one or more virtual clients never emitted non-zero rightmove")

        yaw_values = {
            angles[1]
            for client in command_slots
            for angles in proof.cmd_angles.get(client, set())
        }
        if len(yaw_values) < len(command_slots):
            errors.append(f"only {len(yaw_values)} distinct yaw command values observed; need {len(command_slots)}")

    if args.require_control_movement:
        movement = slot_movement_distances(proof.state_history)
        missing_movement_slots = command_slots - set(movement)
        if missing_movement_slots:
            errors.append(f"missing state history for virtual-control clients {sorted(missing_movement_slots)}")
        stuck_slots = [
            slot for slot in sorted(command_slots & set(movement))
            if movement[slot] < args.min_control_movement_distance
        ]
        if stuck_slots:
            observed = {slot: round(movement.get(slot, 0.0), 2) for slot in stuck_slots}
            errors.append(
                f"virtual-control clients did not move enough; observed={observed} "
                f"required>={args.min_control_movement_distance:.1f}"
            )

    missing_refdefs = local_slots - proof.refdef_slots
    if missing_refdefs:
        errors.append(f"missing local refdefs for slots {sorted(missing_refdefs)}")

    unique_refdef_origins = unique_origin_count(
        (proof.refdef_origins.get(slot) for slot in local_slots),
        args.origin_tolerance,
    )
    if unique_refdef_origins < min(args.min_unique_refdef_origins, len(local_slots)):
        errors.append(
            f"only {unique_refdef_origins} unique local refdef origins observed; "
            f"need {min(args.min_unique_refdef_origins, len(local_slots))}"
        )

    missing_snapshots = local_slots - proof.snapshot_slots
    if missing_snapshots:
        errors.append(f"missing snapshot merge proof for slots {sorted(missing_snapshots)}")

    if args.require_positive_snapshot_adds:
        missing_positive_snapshots = local_slots - proof.snapshot_positive_slots
        if missing_positive_snapshots:
            errors.append(f"missing positive snapshot entity adds for slots {sorted(missing_positive_snapshots)}")

    missing_render = all_slots - proof.render_slots
    if missing_render:
        errors.append(f"missing render setup proof for slots {sorted(missing_render)}")

    if args.require_quadrant_viewports:
        errors.extend(split_quadrant_errors(proof.render_rects, args.players))

    missing_external = local_slots - proof.render_external_slots
    if missing_external:
        errors.append(f"missing external render refdef proof for slots {sorted(missing_external)}")

    if args.require_external_client_map:
        missing_external_clients = all_slots - set(proof.render_external_clients)
        if missing_external_clients:
            errors.append(f"missing render externalClient proof for slots {sorted(missing_external_clients)}")
        for slot in sorted(all_slots & set(proof.render_external_clients)):
            expected_client = slot
            observed_client = proof.render_external_clients[slot]
            if observed_client != expected_client:
                errors.append(
                    f"render slot {slot} used externalClient={observed_client}; expected {expected_client}"
                )

    missing_render_done = all_slots - proof.render_done_slots
    if missing_render_done:
        errors.append(f"missing render completion proof for slots {sorted(missing_render_done)}")

    missing_done_external = local_slots - proof.render_done_external_slots
    if missing_done_external:
        errors.append(f"missing external render completion proof for slots {sorted(missing_done_external)}")

    if args.require_external_client_map:
        missing_done_external_clients = all_slots - set(proof.render_done_external_clients)
        if missing_done_external_clients:
            errors.append(f"missing render completion externalClient proof for slots {sorted(missing_done_external_clients)}")
        for slot in sorted(all_slots & set(proof.render_done_external_clients)):
            expected_client = slot
            observed_client = proof.render_done_external_clients[slot]
            if observed_client != expected_client:
                errors.append(
                    f"render completion slot {slot} used externalClient={observed_client}; expected {expected_client}"
                )

    missing_positive_draws = all_slots - proof.render_done_positive_slots
    if missing_positive_draws:
        errors.append(f"missing positive draw-surface deltas for slots {sorted(missing_positive_draws)}")

    unique_render_views = unique_origin_count(
        (proof.render_done_views.get(slot) for slot in all_slots),
        args.origin_tolerance,
    )
    if unique_render_views < args.min_unique_render_views:
        errors.append(
            f"only {unique_render_views} unique completed render views observed; "
            f"need {args.min_unique_render_views}"
        )

    required_fp_filter_slots = set(args.require_fp_filter_slot)
    missing_fp_filters = required_fp_filter_slots - proof.fp_filter_slots
    if missing_fp_filters:
        errors.append(f"missing first-person filter proof for slots {sorted(missing_fp_filters)}")

    required_self_filter_slots = set(args.require_self_filter_slot)
    missing_self_filters = required_self_filter_slots - proof.self_filter_slots
    if missing_self_filters:
        errors.append(f"missing self-model filter proof for slots {sorted(missing_self_filters)}")
    for slot in sorted(required_self_filter_slots & proof.self_filter_slots):
        ref_numbers = proof.self_filter_ref_numbers.get(slot, set())
        model_parts = proof.self_filter_model_parts.get(slot, set())
        if not ref_numbers:
            errors.append(f"self-model filter proof for slot {slot} did not include refNumber=")
        elif slot not in ref_numbers:
            errors.append(
                f"self-model filter proof for slot {slot} had refNumber values {sorted(ref_numbers)}, "
                f"expected {slot}"
            )
        if not model_parts:
            errors.append(f"self-model filter proof for slot {slot} did not include modelPart=")
        elif not (model_parts & {"lower", "upper", "head", "local"}):
            errors.append(
                f"self-model filter proof for slot {slot} had unexpected modelPart values {sorted(model_parts)}"
            )

    required_viewweapon_slots = set(args.require_viewweapon_slot)
    missing_viewweapons = required_viewweapon_slots - proof.viewweapon_positive_slots
    if missing_viewweapons:
        errors.append(f"missing positive first-person view-weapon proof for slots {sorted(missing_viewweapons)}")

    if proof.hud_remaps < args.min_hud_remaps:
        errors.append(f"HUD remap count {proof.hud_remaps} is below {args.min_hud_remaps}")

    if args.require_hud_slots:
        missing_hud_slots = all_slots - proof.hud_slots
        if missing_hud_slots:
            errors.append(f"missing HUD draw proof for slots {sorted(missing_hud_slots)}")

    if args.require_independent_hud:
        missing_independent_hud = all_slots - proof.hud_independent_slots
        if missing_independent_hud:
            errors.append(
                f"missing independent shared=0 HUD routing for slots {sorted(missing_independent_hud)}"
            )

    if args.require_hud_quadrants:
        errors.extend(f"HUD {error}" for error in split_quadrant_errors(proof.hud_rects, args.players))

    if args.require_hud_status_slots:
        missing_hud_status = all_slots - proof.hud_status_slots
        if missing_hud_status:
            errors.append(f"missing per-player HUD status proof for slots {sorted(missing_hud_status)}")
        missing_valid_hud_status = all_slots - proof.hud_status_valid_slots
        if missing_valid_hud_status:
            errors.append(f"missing valid per-player HUD server state for slots {sorted(missing_valid_hud_status)}")

    if args.require_hud_status_quadrants:
        errors.extend(f"HUD status {error}" for error in split_quadrant_errors(proof.hud_status_rects, args.players))

    if args.require_hud_dividers:
        errors.extend(split_divider_errors(proof.hud_divider_records, args.players))

    unique_origins = unique_origin_count((proof.states.get(slot).origin if slot in proof.states else None for slot in all_slots), args.origin_tolerance)
    if unique_origins < args.min_unique_origins:
        errors.append(f"only {unique_origins} unique local origins observed; need {args.min_unique_origins}")

    if args.min_local_p1_distance > 0.0:
        for slot in sorted(local_slots):
            state = proof.states.get(slot)
            if state and state.p1_dist < args.min_local_p1_distance:
                errors.append(
                    f"slot {slot} is only {state.p1_dist:.1f} units from P1; "
                    f"need at least {args.min_local_p1_distance:.1f}"
                )

    if len(proof.heartbeats) < args.min_heartbeat_samples:
        errors.append(f"heartbeat samples {len(proof.heartbeats)} below required {args.min_heartbeat_samples}")

    frame_delta = heartbeat_frame_delta(proof.heartbeats)
    if args.require_frame_progress and (frame_delta is None or frame_delta <= 0):
        errors.append("completedFrame did not advance in heartbeat records")

    elapsed = heartbeat_elapsed_seconds(proof.heartbeats)
    if args.min_elapsed_seconds > 0.0 and (elapsed is None or elapsed < args.min_elapsed_seconds):
        observed = "unavailable" if elapsed is None else f"{elapsed:.1f}s"
        errors.append(f"heartbeat elapsed time {observed} below required {args.min_elapsed_seconds:.1f}s")

    fps_values = numeric(proof.heartbeats, "fps")
    if args.min_heartbeat_fps is not None:
        if not fps_values:
            errors.append("heartbeat FPS samples unavailable")
        elif min(fps_values) < args.min_heartbeat_fps:
            errors.append(f"heartbeat FPS min {min(fps_values):.1f} below required {args.min_heartbeat_fps:.1f}")

    if args.require_retail_path:
        paths = numeric(proof.heartbeats, "path")
        if len(paths) != len(proof.heartbeats):
            errors.append("not every heartbeat contains path=")
        elif any(int(path) != 1 for path in paths):
            errors.append(f"non-retail renderer path values observed: {sorted({int(path) for path in paths})}")

    memory = heartbeat_memory_summary(proof.heartbeats)
    if args.min_free is not None:
        if memory is None:
            errors.append("heartbeat mem= samples unavailable")
        elif memory["freeMinimum"] < args.min_free:
            errors.append(f"heartbeat free memory min {memory['freeMinimum']} below required {args.min_free}")
    if args.min_largest_free is not None:
        if memory is None:
            errors.append("heartbeat mem= samples unavailable")
        elif memory["largestFreeMinimum"] < args.min_largest_free:
            errors.append(
                f"heartbeat largest-free memory min {memory['largestFreeMinimum']} below required {args.min_largest_free}"
            )
    if args.max_used_delta is not None:
        if memory is None:
            errors.append("heartbeat mem= samples unavailable")
        elif memory["usedDelta"] > args.max_used_delta:
            errors.append(f"heartbeat used-memory delta {memory['usedDelta']} above allowed {args.max_used_delta}")

    errors.extend(verify_audio(proof, args))

    return errors


def print_summary(proof: Proof) -> None:
    print(f"lines={proof.lines}")
    print(f"launchRecords={proof.launch_records}")
    print(f"armedPlayers={proof.max_armed_players}")
    print(f"stateSlots={sorted(proof.states)} maxBots={proof.max_bots}")
    print(f"stateMovement={slot_movement_distances(proof.state_history)}")
    print(f"cmdClients={sorted(proof.cmd_clients)} attackClients={sorted(proof.cmd_attack_clients)}")
    print(
        "cmdProfiles="
        f"moves={{{', '.join(f'{client}:{sorted(values)}' for client, values in sorted(proof.cmd_moves.items()))}}} "
        f"angles={{{', '.join(f'{client}:{sorted(values)}' for client, values in sorted(proof.cmd_angles.items()))}}}"
    )
    print(f"refdefSlots={sorted(proof.refdef_slots)} refdefOrigins={proof.refdef_origins}")
    print(f"snapshotSlots={sorted(proof.snapshot_slots)} positiveSnapshotSlots={sorted(proof.snapshot_positive_slots)}")
    print(
        "renderSlots="
        f"{sorted(proof.render_slots)} external={sorted(proof.render_external_slots)} "
        f"done={sorted(proof.render_done_slots)} doneExternal={sorted(proof.render_done_external_slots)} "
        f"positiveDraws={sorted(proof.render_done_positive_slots)}"
    )
    print(
        "renderExternalClients="
        f"{proof.render_external_clients} doneExternalClients={proof.render_done_external_clients}"
    )
    print(f"renderRects={proof.render_rects}")
    print(f"renderViews={proof.render_views} renderDoneViews={proof.render_done_views}")
    print(f"firstPersonFilterSlots={sorted(proof.fp_filter_slots)}")
    print(
        "selfModelFilterSlots="
        f"{sorted(proof.self_filter_slots)} "
        f"refNumbers={{{', '.join(f'{slot}:{sorted(values)}' for slot, values in sorted(proof.self_filter_ref_numbers.items()))}}} "
        f"modelParts={{{', '.join(f'{slot}:{sorted(values)}' for slot, values in sorted(proof.self_filter_model_parts.items()))}}}"
    )
    print(
        "viewWeaponSlots="
        f"{sorted(proof.viewweapon_slots)} positive={sorted(proof.viewweapon_positive_slots)}"
    )
    print(
        f"hudRemaps={proof.hud_remaps} hudSlots={sorted(proof.hud_slots)} "
        f"independent={sorted(proof.hud_independent_slots)} hudRects={proof.hud_rects}"
    )
    print(
        "hudStatus="
        f"slots={sorted(proof.hud_status_slots)} valid={sorted(proof.hud_status_valid_slots)} "
        f"rects={proof.hud_status_rects} values={proof.hud_status_values}"
    )
    print(f"hudDividers={proof.hud_divider_records}")
    fps_values = numeric(proof.heartbeats, "fps")
    elapsed = heartbeat_elapsed_seconds(proof.heartbeats)
    frame_delta = heartbeat_frame_delta(proof.heartbeats)
    memory = heartbeat_memory_summary(proof.heartbeats)
    if fps_values:
        elapsed_text = "unavailable" if elapsed is None else f"{elapsed:.1f}s"
        frame_delta_text = "unavailable" if frame_delta is None else str(frame_delta)
        print(
            "heartbeats="
            f"{len(proof.heartbeats)} elapsed={elapsed_text} frameDelta={frame_delta_text} "
            f"fpsMin={min(fps_values):.1f} fpsMax={max(fps_values):.1f}"
        )
    else:
        print(f"heartbeats={len(proof.heartbeats)} elapsed={elapsed} frameDelta={frame_delta} fps=unavailable")
    if memory:
        print(
            "memory="
            f"samples={memory['samples']} usedDelta={memory['usedDelta']} "
            f"freeMin={memory['freeMinimum']} largestFreeMin={memory['largestFreeMinimum']}"
        )
    else:
        print("memory=unavailable")
    print(f"audio={audio_summary(proof)}")


def self_test() -> int:
    sample = """
STEFX_HM_SPLIT_LAUNCH: source=menu map='hm_borg1' split=1 players=4 mode='holomatch' localPlayers=4 virtual=1 virtualP1=1
STEFX_HM_SPLIT_STATE: slot=0 players=4 bots=3 state=4 local=0 bot=0 svFlags=0x0 health=100 weapon=1 area=1 cluster=10 p1Area=1 p1Cluster=10 p1Pvs=1 p1Dist=0 origin=(0,0,0) view=(0,0,0) time=1000 sample=1 interval=500
STEFX_HM_SPLIT_STATE: slot=1 players=4 bots=3 state=4 local=1 bot=0 svFlags=0x0 health=100 weapon=1 area=2 cluster=20 p1Area=1 p1Cluster=10 p1Pvs=1 p1Dist=100 origin=(100,0,0) view=(0,20,0) time=1000 sample=1 interval=500
STEFX_HM_SPLIT_STATE: slot=2 players=4 bots=3 state=4 local=1 bot=0 svFlags=0x0 health=100 weapon=1 area=3 cluster=30 p1Area=1 p1Cluster=10 p1Pvs=1 p1Dist=100 origin=(0,100,0) view=(0,40,0) time=1000 sample=1 interval=500
STEFX_HM_SPLIT_STATE: slot=3 players=4 bots=3 state=4 local=1 bot=0 svFlags=0x0 health=100 weapon=1 area=4 cluster=40 p1Area=1 p1Cluster=10 p1Pvs=1 p1Dist=100 origin=(0,0,100) view=(0,60,0) time=1000 sample=1 interval=500
STEFX_HM_SPLIT_CMD: client=0 time=1000 move=(90,-36,0) buttons=0x1 weapon=1 angles=(0,0,0)
STEFX_HM_SPLIT_CMD: client=1 time=1000 move=(90,44,0) buttons=0x1 weapon=1 angles=(0,1,0)
STEFX_HM_SPLIT_CMD: client=2 time=1000 move=(90,-54,0) buttons=0x1 weapon=1 angles=(0,2,0)
STEFX_HM_SPLIT_CMD: client=3 time=1000 move=(90,28,0) buttons=0x1 weapon=1 angles=(0,3,0)
STEFX_HM_SPLIT_STATE: slot=0 players=4 bots=3 state=4 local=0 bot=0 svFlags=0x0 health=100 weapon=1 area=1 cluster=10 p1Area=1 p1Cluster=10 p1Pvs=1 p1Dist=0 origin=(24,-10,0) view=(0,12,0) time=2200 sample=2 interval=500
STEFX_HM_SPLIT_STATE: slot=1 players=4 bots=3 state=4 local=1 bot=0 svFlags=0x0 health=100 weapon=1 area=2 cluster=20 p1Area=1 p1Cluster=10 p1Pvs=1 p1Dist=118 origin=(122,20,0) view=(0,32,0) time=2200 sample=2 interval=500
STEFX_HM_SPLIT_STATE: slot=2 players=4 bots=3 state=4 local=1 bot=0 svFlags=0x0 health=100 weapon=1 area=3 cluster=30 p1Area=1 p1Cluster=10 p1Pvs=1 p1Dist=118 origin=(20,122,0) view=(0,52,0) time=2200 sample=2 interval=500
STEFX_HM_SPLIT_STATE: slot=3 players=4 bots=3 state=4 local=1 bot=0 svFlags=0x0 health=100 weapon=1 area=4 cluster=40 p1Area=1 p1Cluster=10 p1Pvs=1 p1Dist=118 origin=(22,20,100) view=(0,72,0) time=2200 sample=2 interval=500
STEFX_HM_SPLIT_REFDEF: slot=1 client=1 time=1000 origin=(100,0,48) angles=(0,20,0)
STEFX_HM_SPLIT_REFDEF: slot=2 client=2 time=1000 origin=(0,100,48) angles=(0,40,0)
STEFX_HM_SPLIT_REFDEF: slot=3 client=3 time=1000 origin=(0,0,148) angles=(0,60,0)
STEFX_HM_SPLIT_SNAPSHOT: slot=1 entsBefore=20 entsAfter=24 added=4 areaBytes=1 view=(100,0,48) state=4 local=1
STEFX_HM_SPLIT_SNAPSHOT: slot=2 entsBefore=24 entsAfter=28 added=4 areaBytes=1 view=(0,100,48) state=4 local=1
STEFX_HM_SPLIT_SNAPSHOT: slot=3 entsBefore=28 entsAfter=32 added=4 areaBytes=1 view=(0,0,148) state=4 local=1
STEFX_HM_SPLIT_RENDER: armed players=4 source=0,0 640x480 gl=0,0 640x480 fov=90/73
STEFX_HM_SPLIT_RENDER: slot=0 external=0 externalClient=0 drawBase=0 ref=0,0 320x240 gl=0,240 320x240 fov=90/53 view=(0,0,48) pvs=(0,0,48)
STEFX_HM_SPLIT_RENDER_DONE: slot=0 external=0 externalClient=0 drawDelta=10 drawAfter=10 cluster=10 cluster2=-1 view=(0,0,48)
STEFX_HM_SPLIT_RENDER: slot=1 external=1 externalClient=1 drawBase=10 ref=320,0 320x240 gl=320,240 320x240 fov=90/53 view=(100,0,48) pvs=(100,0,48)
STEFX_HM_SPLIT_RENDER_DONE: slot=1 external=1 externalClient=1 drawDelta=11 drawAfter=21 cluster=20 cluster2=-1 view=(100,0,48)
STEFX_HM_SPLIT_RENDER: slot=2 external=1 externalClient=2 drawBase=21 ref=0,240 320x240 gl=0,0 320x240 fov=90/53 view=(0,100,48) pvs=(0,100,48)
STEFX_HM_SPLIT_RENDER_DONE: slot=2 external=1 externalClient=2 drawDelta=12 drawAfter=33 cluster=30 cluster2=-1 view=(0,100,48)
STEFX_HM_SPLIT_RENDER: slot=3 external=1 externalClient=3 drawBase=33 ref=320,240 320x240 gl=320,0 320x240 fov=90/53 view=(0,0,148) pvs=(0,0,148)
STEFX_HM_SPLIT_RENDER_DONE: slot=3 external=1 externalClient=3 drawDelta=13 drawAfter=46 cluster=40 cluster2=-1 view=(0,0,148)
STEFX_HM_SPLIT_FP_FILTER: slot=1 entity=2 renderfx=0x4 hModel=12
STEFX_HM_SPLIT_FP_FILTER: slot=2 entity=2 renderfx=0x4 hModel=12
STEFX_HM_SPLIT_FP_FILTER: slot=3 entity=2 renderfx=0x4 hModel=12
STEFX_HM_SPLIT_SELF_FILTER: slot=1 entity=12 refNumber=1 renderfx=0x0 hModel=40 modelPart=lower origin=(100,0,0) view=(100,0,48) xyDist=0 zDelta=48
STEFX_HM_SPLIT_SELF_FILTER: slot=2 entity=13 refNumber=2 renderfx=0x0 hModel=41 modelPart=upper origin=(0,100,0) view=(0,100,48) xyDist=0 zDelta=48
STEFX_HM_SPLIT_SELF_FILTER: slot=3 entity=14 refNumber=3 renderfx=0x0 hModel=42 modelPart=head origin=(0,0,100) view=(0,0,148) xyDist=0 zDelta=48
STEFX_HM_SPLIT_HUD: slot=0 players=4 shared=0 shader=1 src=(0,0 640x480) dst=(0,0 320x240)
STEFX_HM_SPLIT_HUD: slot=1 players=4 shared=0 shader=1 src=(0,0 640x480) dst=(320,0 320x240)
STEFX_HM_SPLIT_HUD: slot=2 players=4 shared=0 shader=1 src=(0,0 640x480) dst=(0,240 320x240)
STEFX_HM_SPLIT_HUD: slot=3 players=4 shared=0 shader=1 src=(0,0 640x480) dst=(320,240 320x240)
STEFX_HM_SPLIT_HUD_STATUS: slot=0 players=4 valid=1 health=100 weapon=1 score=0 dst=(4,4 152x22)
STEFX_HM_SPLIT_HUD_STATUS: slot=1 players=4 valid=1 health=100 weapon=1 score=0 dst=(324,4 152x22)
STEFX_HM_SPLIT_HUD_STATUS: slot=2 players=4 valid=1 health=100 weapon=1 score=0 dst=(4,244 152x22)
STEFX_HM_SPLIT_HUD_STATUS: slot=3 players=4 valid=1 health=100 weapon=1 score=0 dst=(324,244 152x22)
STEFX_HM_SPLIT_HUD_DIVIDER: players=4 vertical=(318,0 4x480) horizontal=(0,238 640x4)
JA: FRAME_HEARTBEAT completedFrame=100 realtime=10000 serverTime=1000 fps=30.0 path=1 mem=1000/2000/1500/0
JA: FRAME_HEARTBEAT completedFrame=220 realtime=22000 serverTime=7000 fps=28.5 path=1 mem=1100/1900/1450/0
xblogaudio t=30.6 backend=0x00000004 begin=2 register=168 start=114 local=4 loop=239 respat=488 listener=0x00040004 listenerMask=0x0000000e voice=9 lipActive=1 last=0x00000000/100
"""
    parser = build_parser()
    args = parser.parse_args([
        "--self-test",
        "--require-attack",
        "--require-positive-snapshot-adds",
        "--require-fp-filter-slot",
        "1",
        "--require-fp-filter-slot",
        "2",
        "--require-fp-filter-slot",
        "3",
        "--require-self-filter-slot",
        "1",
        "--require-self-filter-slot",
        "2",
        "--require-self-filter-slot",
        "3",
        "--require-audio-backend",
        "--require-audio-listeners",
        "--min-audio-starts",
        "100",
        "--min-audio-voice-starts",
        "8",
        "--require-audio-lip-active",
    ])
    proof = parse_lines(sample.strip().splitlines())
    errors = verify(proof, args)
    if errors:
        print("self-test failed:")
        for error in errors:
            print(f"- {error}")
        return 1
    print("verify_holomatch_split_log self-test passed")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", nargs="?", type=Path, help="Returned ef_mp_log.txt path")
    parser.add_argument("--self-test", action="store_true", help="Run built-in parser self-test")
    parser.add_argument("--audio-only", action="store_true", help="Verify only Xbox audio telemetry in filtered proof reports")
    parser.add_argument("--players", type=int, default=4)
    parser.add_argument("--require-launch", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--require-launch-source", choices=("xbe", "menu", "direct"))
    parser.add_argument("--min-bots", type=int, default=1)
    parser.add_argument("--min-client-state", type=int, default=4, help="CS_ACTIVE is 4 in the current engine")
    parser.add_argument("--require-local-non-bot", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--min-hud-remaps", type=int, default=1)
    parser.add_argument("--require-hud-slots", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--require-independent-hud", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--require-hud-quadrants", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--require-hud-status-slots", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--require-hud-status-quadrants", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--require-hud-dividers", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--min-unique-origins", type=int, default=4)
    parser.add_argument("--min-unique-refdef-origins", type=int, default=3)
    parser.add_argument("--min-unique-render-views", type=int, default=4)
    parser.add_argument("--require-quadrant-viewports", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--require-external-client-map", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--origin-tolerance", type=float, default=8.0)
    parser.add_argument("--min-local-p1-distance", type=float, default=32.0)
    parser.add_argument("--require-attack", action="store_true")
    parser.add_argument("--require-unique-controls", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--require-control-movement", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--min-control-movement-distance", type=float, default=8.0)
    parser.add_argument("--require-positive-snapshot-adds", action="store_true")
    parser.add_argument("--require-p1-virtual-controls", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--min-heartbeat-samples", type=int, default=2)
    parser.add_argument("--min-elapsed-seconds", type=float, default=0.0)
    parser.add_argument("--min-heartbeat-fps", type=float)
    parser.add_argument("--min-free", type=int)
    parser.add_argument("--min-largest-free", type=int)
    parser.add_argument("--max-used-delta", type=int)
    parser.add_argument("--require-frame-progress", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--require-retail-path", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--require-audio-backend", action="store_true")
    parser.add_argument("--require-audio-listeners", action="store_true")
    parser.add_argument("--min-audio-compiled-listeners", type=int, default=4)
    parser.add_argument("--min-audio-active-listeners", type=int, default=4)
    parser.add_argument(
        "--required-audio-listener-mask",
        type=lambda value: int(value, 0),
        default=0x0E,
        help="Required audio listener update bits; default requires P2-P4 slots.",
    )
    parser.add_argument("--min-audio-starts", type=int)
    parser.add_argument("--min-audio-voice-starts", type=int)
    parser.add_argument("--require-audio-lip-active", action="store_true")
    parser.add_argument(
        "--require-fp-filter-slot",
        action="append",
        type=int,
        default=[],
        help="Require STEFX_HM_SPLIT_FP_FILTER proof for a zero-based render slot; repeatable",
    )
    parser.add_argument(
        "--require-self-filter-slot",
        action="append",
        type=int,
        default=[],
        help="Require STEFX_HM_SPLIT_SELF_FILTER proof for a zero-based render slot; repeatable",
    )
    parser.add_argument(
        "--require-viewweapon-slot",
        action="append",
        type=int,
        default=[],
        help="Require positive STEFX_HM_SPLIT_VIEWWEAPON proof for a zero-based render slot; repeatable",
    )
    return parser


def main(argv: list[str]) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    if args.self_test:
        return self_test()

    if not args.log:
        parser.error("log path is required unless --self-test is used")
    if not args.log.is_file():
        print(f"log not found: {args.log}", file=sys.stderr)
        return 2

    proof = parse_lines(args.log.read_text(errors="replace").splitlines())
    errors = verify_audio(proof, args) if args.audio_only else verify(proof, args)
    print_summary(proof)
    if errors:
        proof_name = "Holomatch audio proof" if args.audio_only else "Holomatch split-screen proof"
        print(f"{proof_name} failed:")
        for error in errors:
            print(f"- {error}")
        return 1
    proof_name = "Holomatch audio proof" if args.audio_only else "Holomatch split-screen proof"
    print(f"{proof_name} passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
