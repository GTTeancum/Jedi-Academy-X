#!/usr/bin/env python3
import argparse
import math
import struct
import zipfile
from pathlib import Path


LUMP_MODELS = 7
LUMP_LIGHTGRID = 15
LIGHTGRID_RECORD_SIZE = 8
GRID_SIZE = (64.0, 64.0, 128.0)


def read_bsp(base_dir: Path, map_name: str) -> bytes:
    rel = f"maps/{map_name}.bsp"
    loose = base_dir / rel
    if loose.exists():
        return loose.read_bytes()

    for pk3 in sorted(base_dir.glob("*.pk3")):
        with zipfile.ZipFile(pk3) as zf:
            names = {name.lower(): name for name in zf.namelist()}
            hit = names.get(rel.lower())
            if hit:
                return zf.read(hit)

    raise FileNotFoundError(f"could not find {rel} under {base_dir}")


def lump(header: bytes, index: int) -> tuple[int, int]:
    off = 8 + index * 8
    return struct.unpack_from("<ii", header, off)


def color_shift(rgb: tuple[int, int, int], overbright_bits: int) -> tuple[int, int, int]:
    shift = 1 - overbright_bits if overbright_bits else 0
    if not shift:
        return rgb

    r, g, b = (c << shift for c in rgb)
    if (r | g | b) > 255:
        m = max(r, g, b)
        r = r * 255 // m
        g = g * 255 // m
        b = b * 255 // m
    return r, g, b


def grid_info(data: bytes):
    ident, version = struct.unpack_from("<4sI", data, 0)
    if ident != b"IBSP" or version != 46:
        raise ValueError(f"unexpected BSP header {ident!r} version {version}")

    models_off, models_len = lump(data, LUMP_MODELS)
    if models_len < 40:
        raise ValueError("models lump is too small")
    mins = struct.unpack_from("<fff", data, models_off)
    maxs = struct.unpack_from("<fff", data, models_off + 12)

    origin = []
    bounds = []
    for i, step in enumerate(GRID_SIZE):
        start = step * math.ceil(mins[i] / step)
        end = step * math.floor(maxs[i] / step)
        origin.append(start)
        bounds.append(int((end - start) / step + 1.0))

    lg_off, lg_len = lump(data, LUMP_LIGHTGRID)
    count = lg_len // LIGHTGRID_RECORD_SIZE
    expected = bounds[0] * bounds[1] * bounds[2]
    return tuple(origin), tuple(bounds), data[lg_off:lg_off + lg_len], count, expected


def sample_grid(records: bytes, origin, bounds, point, overbright_bits, ambient_scale, directed_scale):
    rel = [(point[i] - origin[i]) / GRID_SIZE[i] for i in range(3)]
    pos = []
    frac = []
    for i in range(3):
        p = math.floor(rel[i])
        f = rel[i] - p
        if p < 0:
            p = 0
        elif p >= bounds[i] - 1:
            p = bounds[i] - 1
        pos.append(int(p))
        frac.append(f)

    grid_step = (1, bounds[0], bounds[0] * bounds[1])
    start = pos[0] * grid_step[0] + pos[1] * grid_step[1] + pos[2] * grid_step[2]

    ambient = [0.0, 0.0, 0.0]
    directed = [0.0, 0.0, 0.0]
    total = 0.0
    samples = []

    for i in range(8):
        factor = 1.0
        index = start
        for axis in range(3):
            if i & (1 << axis):
                factor *= frac[axis]
                index += grid_step[axis]
            else:
                factor *= 1.0 - frac[axis]

        off = index * LIGHTGRID_RECORD_SIZE
        if off < 0 or off + LIGHTGRID_RECORD_SIZE > len(records):
            continue
        rec = records[off:off + LIGHTGRID_RECORD_SIZE]
        raw_amb = tuple(rec[0:3])
        raw_dir = tuple(rec[3:6])
        amb = color_shift(raw_amb, overbright_bits)
        drc = color_shift(raw_dir, overbright_bits)
        total += factor
        for c in range(3):
            ambient[c] += factor * amb[c]
            directed[c] += factor * drc[c]
        samples.append((index, factor, raw_amb, raw_dir, amb, drc, tuple(rec[6:8])))

    if 0.0 < total < 0.99:
        inv = 1.0 / total
        ambient = [v * inv for v in ambient]
        directed = [v * inv for v in directed]

    ambient = [v * ambient_scale + 32.0 for v in ambient]
    directed = [v * directed_scale for v in directed]
    ambient = [min(v, 255.0) for v in ambient]
    directed = [min(v, 255.0) for v in directed]
    return pos, frac, ambient, directed, total, samples


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-dir", default="build/release/BaseEF")
    parser.add_argument("--map", required=True)
    parser.add_argument("--point", required=True, nargs=3, type=float)
    parser.add_argument("--overbright-bits", default=0, type=int)
    parser.add_argument("--ambient-scale", default=0.5, type=float)
    parser.add_argument("--directed-scale", default=1.0, type=float)
    args = parser.parse_args()

    data = read_bsp(Path(args.base_dir), args.map)
    origin, bounds, records, count, expected = grid_info(data)
    pos, frac, ambient, directed, total, samples = sample_grid(
        records,
        origin,
        bounds,
        tuple(args.point),
        args.overbright_bits,
        args.ambient_scale,
        args.directed_scale,
    )

    print(f"map={args.map} origin={origin} bounds={bounds} records={count} expected={expected}")
    print(f"point={tuple(args.point)} pos={tuple(pos)} frac=({frac[0]:.3f},{frac[1]:.3f},{frac[2]:.3f}) total={total:.3f}")
    print(f"ambient_after_scale_plus_min=({ambient[0]:.1f},{ambient[1]:.1f},{ambient[2]:.1f}) directed=({directed[0]:.1f},{directed[1]:.1f},{directed[2]:.1f})")
    for index, factor, raw_amb, raw_dir, amb, drc, latlong in samples:
        print(f"sample index={index} factor={factor:.3f} rawAmb={raw_amb} rawDir={raw_dir} shiftedAmb={amb} shiftedDir={drc} latLong={latlong}")


if __name__ == "__main__":
    main()
