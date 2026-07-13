#!/usr/bin/env python3
"""Audit Elite Force actor materials outside the game runtime.

This follows the campaign content path instead of screenshot symptoms:
BSP entities -> EF NPC definitions -> segmented player skins -> shader/image
resolution using the same Xbox image lookup order as the BSP material audit.
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
import zipfile
from dataclasses import dataclass, field
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from audit_ef_bsp_materials import (  # noqa: E402
    BSP_LUMP_COUNT,
    index_assets,
    load_shader_defs,
    normalized,
    resolve_image,
)


BSP_ENTITIES_LUMP = 0
EF_BORG_DEFAULT_VARIANTS = (
    "borgthin",
    "borgthin2",
    "borgthin3",
    "borgthin4",
    "borgbig",
    "borgbig2",
    "borgbig3",
    "borgbig4",
)


@dataclass
class Entity:
    pairs: dict[str, str]


@dataclass
class NpcDef:
    name: str
    source: str
    line: int
    values: dict[str, str] = field(default_factory=dict)


@dataclass
class SkinRef:
    surface: str
    token: str
    line: int


@dataclass
class ModelSurfaces:
    path: str
    source: str
    kind: str
    surfaces: list[str]
    shaders: dict[str, str] = field(default_factory=dict)


def strip_line_comment(line: str) -> str:
    return line.split("//", 1)[0].strip()


def read_text_asset(base_dir: Path, rel_path: str) -> tuple[str, str]:
    rel_norm = normalized(rel_path)
    loose = base_dir / rel_norm
    if loose.is_file():
        return loose.read_text(errors="ignore"), loose.relative_to(base_dir).as_posix()

    for pk3 in sorted(base_dir.glob("*.pk3")):
        with zipfile.ZipFile(pk3) as archive:
            for entry in archive.infolist():
                if normalized(entry.filename) == rel_norm:
                    return archive.read(entry).decode("latin1", errors="ignore"), f"{pk3.name}:{entry.filename}"

    raise FileNotFoundError(rel_path)


def read_binary_asset(base_dir: Path, rel_path: str) -> tuple[bytes, str]:
    rel_norm = normalized(rel_path)
    loose = base_dir / rel_norm
    if loose.is_file():
        return loose.read_bytes(), loose.relative_to(base_dir).as_posix()

    for pk3 in sorted(base_dir.glob("*.pk3")):
        with zipfile.ZipFile(pk3) as archive:
            for entry in archive.infolist():
                if normalized(entry.filename) == rel_norm:
                    return archive.read(entry), f"{pk3.name}:{entry.filename}"

    raise FileNotFoundError(rel_path)


def parse_entities(path: Path) -> list[Entity]:
    data = path.read_bytes()
    if data[:4] != b"IBSP":
        raise ValueError(f"{path} is not an IBSP file")

    lumps = [struct.unpack_from("<ii", data, 8 + i * 8) for i in range(BSP_LUMP_COUNT)]
    entity_offset, entity_len = lumps[BSP_ENTITIES_LUMP]
    text = data[entity_offset : entity_offset + entity_len].decode("latin1", errors="ignore")

    entities: list[Entity] = []
    for block in re.findall(r"\{(.*?)\}", text, flags=re.S):
        pairs: dict[str, str] = {}
        for key, value in re.findall(r'"([^"]+)"\s+"([^"]*)"', block):
            pairs[key.lower()] = value
        if pairs:
            entities.append(Entity(pairs))
    return entities


def parse_npc_defs(text: str, source: str) -> dict[str, NpcDef]:
    defs: dict[str, NpcDef] = {}
    lines = text.splitlines()
    i = 0
    while i < len(lines):
        raw = strip_line_comment(lines[i])
        i += 1
        if not raw or raw in ("{", "}"):
            continue

        name = raw.split()[0]
        j = i
        while j < len(lines) and not strip_line_comment(lines[j]):
            j += 1
        if j >= len(lines) or strip_line_comment(lines[j]) != "{":
            continue

        start_line = j + 1
        i = j + 1
        values: dict[str, str] = {}
        while i < len(lines):
            line = strip_line_comment(lines[i])
            i += 1
            if not line:
                continue
            if line == "}":
                break
            parts = line.split(None, 1)
            if len(parts) == 2:
                values[parts[0].lower()] = parts[1].strip().strip('"')

        defs[normalized(name)] = NpcDef(name=name, source=source, line=start_line, values=values)
    return defs


def actor_type_from_entity(entity: Entity) -> str | None:
    pairs = entity.pairs
    for key in ("npc_type", "npctype"):
        if key in pairs and pairs[key].strip():
            return normalized(pairs[key].strip())

    classname = pairs.get("classname", "")
    if classname.lower().startswith("npc_"):
        return normalized(classname[4:])
    return None


def resolve_model_part(model_token: str, part: str, assets: dict[str, list[tuple[str, int]]]) -> dict[str, object]:
    if not model_token or normalized(model_token) == "none":
        return {"model_token": model_token, "part": part, "status": "none"}

    token = normalized(model_token)
    pieces = token.split("/")
    model_dir = pieces[0]
    skin = pieces[1] if len(pieces) > 1 else "default"

    prefix = {"headmodel": "head", "torsomodel": "upper", "legsmodel": "lower"}[part]
    model_files = {
        "headmodel": ["head.md3", "head_1.md3", "head_2.md3"],
        "torsomodel": ["upper.mdr", "upper.md3"],
        "legsmodel": ["lower.mdr", "lower.md3"],
    }[part]

    candidates: list[tuple[str, str]] = []
    for root in ("models/players", "models/players2"):
        skin_path = f"{root}/{model_dir}/{prefix}_{skin}.skin"
        candidates.append((root, skin_path))
        if skin != "default":
            candidates.append((root, f"{root}/{model_dir}/{prefix}_default.skin"))

    skin_path = None
    skin_root = None
    for root, candidate in candidates:
        if candidate in assets:
            skin_path = candidate
            skin_root = root
            break

    model_hits: list[str] = []
    for root in ("models/players", "models/players2"):
        for model_file in model_files:
            model_path = f"{root}/{model_dir}/{model_file}"
            if model_path in assets:
                model_hits.append(model_path)

    preferred_model = None
    if skin_root:
        for model_path in model_hits:
            if model_path.startswith(f"{skin_root}/"):
                preferred_model = model_path
                break
    if not preferred_model and model_hits:
        preferred_model = model_hits[0]

    return {
        "model_token": model_token,
        "part": part,
        "status": "ok" if skin_path and model_hits else "missing",
        "skin": skin_path,
        "skin_root": skin_root,
        "model": preferred_model,
        "models": model_hits,
        "missing_skin": skin_path is None,
        "missing_model": not model_hits,
    }


def load_skin_refs(base_dir: Path, skin_path: str) -> tuple[list[SkinRef], str]:
    text, source = read_text_asset(base_dir, skin_path)
    refs: list[SkinRef] = []
    for line_no, raw in enumerate(text.splitlines(), start=1):
        line = strip_line_comment(raw)
        if not line or "," not in line:
            continue
        surface, token = [piece.strip() for piece in line.split(",", 1)]
        if not surface or not token:
            continue
        if normalized(surface).startswith("tag_"):
            continue
        surface_norm = normalized(surface)
        if surface_norm.endswith("_off"):
            if normalized(token) == "*off":
                continue
            surface_norm = surface_norm[:-4]
        refs.append(SkinRef(surface=surface, token=token, line=line_no))
    return refs, source


def c_string(raw: bytes) -> str:
    return raw.split(b"\0", 1)[0].decode("latin1", errors="ignore").lower()


def ef_md3_surface_name(name: str) -> str:
    # R_LoadMD3 lowercases surface names, then strips q3data's trailing _1/_2.
    if len(name) > 2 and name[-2] == "_":
        return name[:-2]
    return name


def parse_md3_model(path: str, data: bytes, source: str) -> ModelSurfaces:
    if len(data) < 108 or data[:4] != b"IDP3":
        raise ValueError(f"{path} is not an MD3 model")

    num_surfaces = struct.unpack_from("<i", data, 84)[0]
    ofs_surfaces = struct.unpack_from("<i", data, 100)[0]
    surfaces: list[str] = []
    shaders: dict[str, str] = {}
    offset = ofs_surfaces
    for _ in range(num_surfaces):
        if offset + 108 > len(data):
            raise ValueError(f"{path} MD3 surface offset out of range")
        name = ef_md3_surface_name(c_string(data[offset + 4 : offset + 68]))
        num_shaders = struct.unpack_from("<i", data, offset + 76)[0]
        ofs_shaders = struct.unpack_from("<i", data, offset + 92)[0]
        ofs_end = struct.unpack_from("<i", data, offset + 104)[0]
        surfaces.append(name)
        if num_shaders > 0 and offset + ofs_shaders + 64 <= len(data):
            shaders[name] = c_string(data[offset + ofs_shaders : offset + ofs_shaders + 64])
        if ofs_end <= 0:
            raise ValueError(f"{path} MD3 invalid surface end offset")
        offset += ofs_end
    return ModelSurfaces(path=path, source=source, kind="md3", surfaces=surfaces, shaders=shaders)


def parse_mdr_model(path: str, data: bytes, source: str) -> ModelSurfaces:
    if len(data) < 104 or data[:4] != b"RDM5":
        raise ValueError(f"{path} is not an MDR model")

    num_lods = struct.unpack_from("<i", data, 84)[0]
    ofs_lods = struct.unpack_from("<i", data, 88)[0]
    if num_lods < 1 or ofs_lods + 12 > len(data):
        raise ValueError(f"{path} MDR has no readable LOD 0")

    lod = ofs_lods
    num_surfaces = struct.unpack_from("<i", data, lod)[0]
    ofs_surfaces = struct.unpack_from("<i", data, lod + 4)[0]
    offset = lod + ofs_surfaces
    surfaces: list[str] = []
    shaders: dict[str, str] = {}
    for _ in range(num_surfaces):
        if offset + 168 > len(data):
            raise ValueError(f"{path} MDR surface offset out of range")
        name = c_string(data[offset + 4 : offset + 68])
        shader = c_string(data[offset + 68 : offset + 132])
        ofs_end = struct.unpack_from("<i", data, offset + 164)[0]
        surfaces.append(name)
        shaders[name] = shader
        if ofs_end <= 0:
            raise ValueError(f"{path} MDR invalid surface end offset")
        offset += ofs_end
    return ModelSurfaces(path=path, source=source, kind="mdr", surfaces=surfaces, shaders=shaders)


def parse_model_surfaces(base_dir: Path, model_path: str) -> ModelSurfaces:
    data, source = read_binary_asset(base_dir, model_path)
    if data[:4] == b"IDP3":
        return parse_md3_model(model_path, data, source)
    if data[:4] == b"RDM5":
        return parse_mdr_model(model_path, data, source)
    raise ValueError(f"{model_path} has unrecognized model ident {data[:4]!r}")


def resolve_skin_token(
    token: str,
    shader_defs: dict[str, object],
    assets: dict[str, list[tuple[str, int]]],
    patch_pk3_assets: set[str],
) -> list[dict[str, object]]:
    token_norm = normalized(token)
    if token_norm in shader_defs:
        results: list[dict[str, object]] = []
        for shader in shader_defs[token_norm]:
            if not shader.stages:
                results.append({"token": token, "status": "shader_no_stages", "path": token_norm, "sources": []})
                continue
            for stage in shader.stages:
                for stage_map in stage.maps:
                    result = resolve_image(stage_map, assets, patch_pk3_assets)
                    result["shader"] = token_norm
                    result["shader_source"] = f"{shader.source}:{shader.line}"
                    results.append(result)
        return results

    result = resolve_image(token, assets, patch_pk3_assets)
    result["shader"] = None
    result["shader_source"] = None
    return [result]


def audit_npc_def(
    base_dir: Path,
    actor_type: str,
    count: int,
    npc_def: NpcDef,
    assets: dict[str, list[tuple[str, int]]],
    shader_defs: dict[str, object],
    patch_pk3_assets: set[str],
) -> int:
    missing = 0
    part_results = []
    for part in ("headmodel", "torsomodel", "legsmodel"):
        part_results.append(resolve_model_part(npc_def.values.get(part, ""), part, assets))

    part_status = "ok" if all(p.get("status") in ("ok", "none") for p in part_results) else "missing"
    print(f"  actor={actor_type} count={count} def={npc_def.source}:{npc_def.line} parts={part_status}")

    for part_result in part_results:
        if part_result.get("status") == "none":
            continue
        if part_result.get("status") != "ok":
            print(
                "    MISSING_PART"
                f" {part_result['part']} token={part_result['model_token']}"
                f" skin={part_result.get('skin')} models={part_result.get('models')}"
            )
            missing += 1
            continue

        refs, skin_source = load_skin_refs(base_dir, str(part_result["skin"]))
        model_surfaces: ModelSurfaces | None = None
        model_surface_set: set[str] = set()
        skin_surface_set = {ref.surface for ref in refs}
        surface_misses: list[str] = []
        skin_extras: list[str] = []
        model_parse_error = None

        try:
            model_surfaces = parse_model_surfaces(base_dir, str(part_result["model"]))
            model_surface_set = set(model_surfaces.surfaces)
            surface_misses = [surface for surface in model_surfaces.surfaces if surface not in skin_surface_set]
            skin_extras = [surface for surface in sorted(skin_surface_set) if surface not in model_surface_set]
        except Exception as exc:  # noqa: BLE001 - audit should report and continue.
            model_parse_error = str(exc)

        missing_refs = []
        for ref in refs:
            for resolved in resolve_skin_token(ref.token, shader_defs, assets, patch_pk3_assets):
                if resolved["status"] == "missing":
                    missing_refs.append((ref, resolved))

        print(
            f"    {part_result['part']} token={part_result['model_token']}"
            f" model={part_result.get('model')} skin={part_result['skin']}"
            f" refs={len(refs)} missing={len(missing_refs)}"
            f" surface_misses={len(surface_misses)} skin_extras={len(skin_extras)}"
        )
        if model_surfaces:
            print(
                f"      MODEL_SURFACES {model_surfaces.source}"
                f" kind={model_surfaces.kind} count={len(model_surfaces.surfaces)}"
                f" names={','.join(model_surfaces.surfaces[:16])}"
            )
            if surface_misses:
                missing += len(surface_misses)
                print(
                    "      MODEL_SURFACE_NOT_IN_SKIN "
                    + ",".join(surface_misses[:32])
                )
            if skin_extras:
                print(
                    "      SKIN_SURFACE_NOT_IN_MODEL "
                    + ",".join(skin_extras[:32])
                )
        elif model_parse_error:
            missing += 1
            print(f"      MODEL_PARSE_ERROR {model_parse_error}")
        if missing_refs:
            missing += len(missing_refs)
            for ref, resolved in missing_refs[:20]:
                print(
                    f"      MISSING_REF {skin_source}:{ref.line}"
                    f" surface={ref.surface} token={ref.token} wanted={resolved['path']}"
                )

    return missing


def audit_maps(base_dir: Path, maps: list[str]) -> int:
    assets, patch_pk3_assets = index_assets(base_dir)
    shader_defs = load_shader_defs(base_dir)
    npc_text, npc_source = read_text_asset(base_dir, "ext_data/NPCs.cfg")
    npc_defs = parse_npc_defs(npc_text, npc_source)

    total_missing = 0
    for map_name in maps:
        bsp = base_dir / "maps" / f"{map_name}.bsp"
        entities = parse_entities(bsp)
        actor_counts: dict[str, int] = {}
        for entity in entities:
            actor_type = actor_type_from_entity(entity)
            if actor_type:
                actor_counts[actor_type] = actor_counts.get(actor_type, 0) + 1

        print(f"{map_name}: actor_types={len(actor_counts)} actors={sum(actor_counts.values())}")
        for actor_type, count in sorted(actor_counts.items()):
            npc_def = npc_defs.get(actor_type)
            if not npc_def:
                if actor_type == "borg":
                    print(f"  actor=borg count={count} expands={','.join(EF_BORG_DEFAULT_VARIANTS)}")
                    for variant in EF_BORG_DEFAULT_VARIANTS:
                        variant_def = npc_defs.get(variant)
                        if not variant_def:
                            print(f"  MISSING_NPC_DEF actor={variant} count={count}")
                            total_missing += 1
                            continue
                        total_missing += audit_npc_def(
                            base_dir,
                            variant,
                            count,
                            variant_def,
                            assets,
                            shader_defs,
                            patch_pk3_assets,
                        )
                    continue
                print(f"  MISSING_NPC_DEF actor={actor_type} count={count}")
                total_missing += 1
                continue

            total_missing += audit_npc_def(
                base_dir,
                actor_type,
                count,
                npc_def,
                assets,
                shader_defs,
                patch_pk3_assets,
            )

    return total_missing


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base-dir", default="build/release/BaseEF", type=Path)
    parser.add_argument("--maps", default="borg3", help="Comma-separated BSP basenames")
    args = parser.parse_args()

    maps = [piece.strip() for piece in args.maps.split(",") if piece.strip()]
    missing = audit_maps(args.base_dir, maps)
    return 1 if missing else 0


if __name__ == "__main__":
    raise SystemExit(main())
