#!/usr/bin/env python3
"""Identify Elite Force BSP shaders intersected by a camera ray."""

from __future__ import annotations

import argparse
import math
import struct
from dataclasses import dataclass
from pathlib import Path


LUMP_SHADERS = 1
LUMP_DRAWVERTS = 10
LUMP_DRAWINDEXES = 11
LUMP_SURFACES = 13
SHADER_SIZE = 72
DRAWVERT_SIZE = 44
SURFACE_SIZE = 104


@dataclass(frozen=True)
class Hit:
    distance: float
    surface: int
    triangle: int
    shader_index: int
    shader: str
    surface_type: int
    point: tuple[float, float, float]


def lumps(data: bytes) -> list[tuple[int, int]]:
    ident, version = struct.unpack_from("<4sI", data, 0)
    if ident != b"IBSP" or version != 46:
        raise ValueError(f"expected Elite Force IBSP 46, got {ident!r} version {version}")
    return [struct.unpack_from("<II", data, 8 + index * 8) for index in range(17)]


def cross(a: tuple[float, float, float], b: tuple[float, float, float]) -> tuple[float, float, float]:
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def dot(a: tuple[float, float, float], b: tuple[float, float, float]) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def subtract(a: tuple[float, float, float], b: tuple[float, float, float]) -> tuple[float, float, float]:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def ray_triangle(
    origin: tuple[float, float, float],
    direction: tuple[float, float, float],
    a: tuple[float, float, float],
    b: tuple[float, float, float],
    c: tuple[float, float, float],
) -> float | None:
    epsilon = 1.0e-6
    edge1 = subtract(b, a)
    edge2 = subtract(c, a)
    p = cross(direction, edge2)
    determinant = dot(edge1, p)
    if -epsilon < determinant < epsilon:
        return None
    inverse = 1.0 / determinant
    tvec = subtract(origin, a)
    u = dot(tvec, p) * inverse
    if u < 0.0 or u > 1.0:
        return None
    q = cross(tvec, edge1)
    v = dot(direction, q) * inverse
    if v < 0.0 or u + v > 1.0:
        return None
    distance = dot(edge2, q) * inverse
    return distance if distance > epsilon else None


def direction_for_angles(pitch: float, yaw: float) -> tuple[float, float, float]:
    pitch_rad = math.radians(pitch)
    yaw_rad = math.radians(yaw)
    pitch_cos = math.cos(pitch_rad)
    return (
        pitch_cos * math.cos(yaw_rad),
        pitch_cos * math.sin(yaw_rad),
        -math.sin(pitch_rad),
    )


def trace(
    data: bytes,
    origin: tuple[float, float, float],
    direction: tuple[float, float, float],
) -> list[Hit]:
    bsp_lumps = lumps(data)
    shader_offset, shader_length = bsp_lumps[LUMP_SHADERS]
    shader_names = [
        data[offset : offset + 64].split(b"\0", 1)[0].decode("latin1")
        for offset in range(shader_offset, shader_offset + shader_length, SHADER_SIZE)
    ]

    vertex_offset, vertex_length = bsp_lumps[LUMP_DRAWVERTS]
    vertices = [
        struct.unpack_from("<3f", data, offset)
        for offset in range(vertex_offset, vertex_offset + vertex_length, DRAWVERT_SIZE)
    ]
    index_offset, index_length = bsp_lumps[LUMP_DRAWINDEXES]
    indexes = struct.unpack_from(f"<{index_length // 4}i", data, index_offset)

    hits: list[Hit] = []
    surface_offset, surface_length = bsp_lumps[LUMP_SURFACES]
    for surface_index, offset in enumerate(
        range(surface_offset, surface_offset + surface_length, SURFACE_SIZE)
    ):
        (
            shader_index,
            _fog_index,
            surface_type,
            first_vertex,
            vertex_count,
            first_index,
            index_count,
        ) = struct.unpack_from("<7i", data, offset)
        if index_count < 3 or first_index < 0 or first_vertex < 0:
            continue
        for triangle_offset in range(0, index_count - 2, 3):
            local = indexes[first_index + triangle_offset : first_index + triangle_offset + 3]
            absolute = [first_vertex + value for value in local]
            if any(index < 0 or index >= len(vertices) for index in absolute):
                continue
            distance = ray_triangle(
                origin,
                direction,
                vertices[absolute[0]],
                vertices[absolute[1]],
                vertices[absolute[2]],
            )
            if distance is None:
                continue
            hits.append(
                Hit(
                    distance=distance,
                    surface=surface_index,
                    triangle=triangle_offset // 3,
                    shader_index=shader_index,
                    shader=shader_names[shader_index],
                    surface_type=surface_type,
                    point=tuple(origin[i] + direction[i] * distance for i in range(3)),
                )
            )
    return sorted(hits, key=lambda hit: hit.distance)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bsp", type=Path)
    parser.add_argument("--origin", type=float, nargs=3, required=True, metavar=("X", "Y", "Z"))
    parser.add_argument("--pitch", type=float, default=0.0)
    parser.add_argument("--yaw", type=float, required=True)
    parser.add_argument("--limit", type=int, default=12)
    args = parser.parse_args()

    origin = tuple(args.origin)
    direction = direction_for_angles(args.pitch, args.yaw)
    print(f"origin={origin} pitch={args.pitch:g} yaw={args.yaw:g} direction={direction}")
    for hit in trace(args.bsp.read_bytes(), origin, direction)[: args.limit]:
        print(
            f"distance={hit.distance:.3f} point=({hit.point[0]:.3f},{hit.point[1]:.3f},{hit.point[2]:.3f}) "
            f"surface={hit.surface} triangle={hit.triangle} type={hit.surface_type} "
            f"shader={hit.shader_index}:{hit.shader}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
