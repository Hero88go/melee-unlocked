#!/usr/bin/env python3
"""Read-only validation of a saved Nucleus cosmetic project against a clean ISO."""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import struct
import sys
import time
import zipfile
from pathlib import Path

from cosmetic_import_diagnostic import (
    InvalidImport,
    MAX_ARCHIVE,
    MAX_ASSET,
    MAX_ENTRIES,
    inspect_dat,
    safe_member,
)
from companion_texture_diagnostic import read_disc_file
from effect_cosmetic_diagnostic import materialize_effect_dat
from stage_cosmetic_diagnostic import compare_stage_dat, dat_layout

MAX_METADATA = 16 * 1024 * 1024
MAX_IMAGE = 16 * 1024 * 1024
MAX_NESTED_EXPANDED = 128 * 1024 * 1024
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
STAGE_TARGETS = {
    "battlefield": "GrNBa.dat", "dreamland": "GrOp.dat", "final_destination": "GrNLa.dat",
    "fountain_of_dreams": "GrIz.dat", "poke_floats": "GrPu.dat",
    "pokemon_stadium": "GrPs.dat", "yoshis_story": "GrSt.dat",
}
EFFECT_TARGETS = {
    ("fox", "upb"): "EfFxData.dat", ("fox", "shine"): "EfFxData.dat",
    ("fox", "laser"): "PlFx.dat", ("fox", "sideb"): "PlFx.dat",
    ("falco", "laser"): "PlFc.dat",
}


def text_field(value: object, field: str, maximum: int = 256) -> str:
    if not isinstance(value, str) or not value or len(value) > maximum:
        raise InvalidImport(f"metadata field {field!r} must be a non-empty bounded string")
    if any(ord(char) < 0x20 for char in value):
        raise InvalidImport(f"metadata field {field!r} contains control characters")
    return value


def validate_entries(entries: list[zipfile.ZipInfo], *, aggregate_limit: int) -> dict[str, zipfile.ZipInfo]:
    if not entries or len(entries) > MAX_ENTRIES:
        raise InvalidImport("ZIP entry count is empty or exceeds the 4096-entry limit")
    seen: dict[str, zipfile.ZipInfo] = {}
    total = 0
    for entry in entries:
        if not safe_member(entry.filename):
            raise InvalidImport(f"unsafe ZIP path: {entry.filename}")
        folded = entry.filename.casefold()
        if folded in seen:
            raise InvalidImport(f"duplicate ZIP path: {entry.filename}")
        seen[folded] = entry
        if (entry.external_attr >> 16) & 0o170000 == 0o120000:
            raise InvalidImport(f"symbolic-link ZIP entry is unsupported: {entry.filename}")
        if entry.flag_bits & (1 | 0x40):
            raise InvalidImport(f"encrypted ZIP entry is unsupported: {entry.filename}")
        if entry.compress_type not in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED):
            raise InvalidImport(f"unsupported ZIP compression method: {entry.filename}")
        total += entry.file_size
        if total > aggregate_limit:
            raise InvalidImport("ZIP expands beyond its aggregate safety limit")
    return seen


def png_info(data: bytes, name: str) -> dict:
    if len(data) > MAX_IMAGE:
        raise InvalidImport(f"image exceeds the 16 MB limit: {name}")
    if len(data) < 24 or not data.startswith(PNG_SIGNATURE) or data[12:16] != b"IHDR":
        raise InvalidImport(f"companion is not a structurally identifiable PNG: {name}")
    width, height = struct.unpack_from(">2I", data, 16)
    if not width or not height or width > 8192 or height > 8192:
        raise InvalidImport(f"PNG dimensions are outside supported bounds: {name}")
    return {"path": name, "width": width, "height": height, "sha256": hashlib.sha256(data).hexdigest()}


def basename_index(entries: list[zipfile.ZipInfo]) -> dict[str, list[zipfile.ZipInfo]]:
    result: dict[str, list[zipfile.ZipInfo]] = {}
    for entry in entries:
        result.setdefault(entry.filename.rsplit("/", 1)[-1].casefold(), []).append(entry)
    return result


def unique_named(index: dict[str, list[zipfile.ZipInfo]], name: str, field: str) -> zipfile.ZipInfo:
    matches = index.get(name.casefold(), [])
    if len(matches) != 1:
        raise InvalidImport(f"{field} must resolve to exactly one vault entry: {name}")
    return matches[0]


def inspect_nested_character(data: bytes, expected_target: str) -> dict:
    if len(data) > MAX_ASSET:
        raise InvalidImport("nested character ZIP exceeds the 64 MB resource limit")
    with zipfile.ZipFile(io.BytesIO(data)) as nested:
        entries = nested.infolist()
        validate_entries(entries, aggregate_limit=MAX_NESTED_EXPANDED)
        matches: list[tuple[zipfile.ZipInfo, dict, bytes]] = []
        for entry in entries:
            if not entry.filename.lower().endswith(".dat"):
                continue
            if entry.file_size > MAX_ASSET:
                raise InvalidImport("DAT in nested ZIP exceeds the 64 MB resource limit")
            raw = nested.read(entry)
            try:
                inspected = inspect_dat(raw)
            except InvalidImport:
                continue
            matches.append((entry, inspected, raw))
        if len(matches) != 1:
            raise InvalidImport(f"character ZIP must contain exactly one supported costume DAT; found {len(matches)}")
        entry, inspected, raw = matches[0]
        if inspected["target_path"].casefold() != expected_target.casefold():
            raise InvalidImport(
                f"metadata target {expected_target} conflicts with DAT target {inspected['target_path']}"
            )
        return {
            "member": entry.filename,
            "target_path": inspected["target_path"],
            "character": inspected["character"],
            "costume": inspected["costume"],
            "sha256": hashlib.sha256(raw).hexdigest(),
            "md5": hashlib.md5(raw).hexdigest(),  # Nucleus metadata currently records MD5.
            "size": len(raw),
        }


def inspect_nested_visual(data: bytes, label: str) -> tuple[str, bytes]:
    if len(data) > MAX_ASSET:
        raise InvalidImport(f"nested {label} ZIP exceeds the 64 MB resource limit")
    with zipfile.ZipFile(io.BytesIO(data)) as nested:
        entries = nested.infolist()
        validate_entries(entries, aggregate_limit=MAX_NESTED_EXPANDED)
        forbidden = (".dol", ".elf", ".exe", ".gct")
        blocked = [entry.filename for entry in entries if entry.filename.lower().endswith(forbidden)]
        if blocked:
            raise InvalidImport(f"{label} archive contains executable/code resources: {blocked[0]}")
        dats = [entry for entry in entries if entry.filename.lower().endswith(".dat")]
        if len(dats) != 1:
            raise InvalidImport(f"{label} archive must contain exactly one DAT; found {len(dats)}")
        if dats[0].file_size > MAX_ASSET:
            raise InvalidImport(f"{label} DAT exceeds the 64 MB resource limit")
        return dats[0].filename, nested.read(dats[0])


def hash_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        while chunk := source.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def validate_vault(path: Path, iso: Path | None = None, *, progress: bool = False,
                   timeout_seconds: float | None = None) -> dict:
    deadline = time.monotonic() + timeout_seconds if timeout_seconds is not None else None
    last_completed = "none"

    def checkpoint(label: str | None = None) -> None:
        nonlocal last_completed
        if deadline is not None and time.monotonic() > deadline:
            raise InvalidImport(
                f"diagnostic exceeded {timeout_seconds:g}s; last completed record: {last_completed}"
            )
        if label is not None:
            last_completed = label
            if progress:
                print(f"COMPLETE {label}", file=sys.stderr, flush=True)

    if not path.is_file():
        raise InvalidImport(f"file does not exist: {path}")
    if path.stat().st_size > MAX_ARCHIVE:
        raise InvalidImport("vault exceeds the 512 MB archive limit")
    report: dict = {
        "source": str(path),
        "source_size": path.stat().st_size,
        "source_sha256": hash_file(path),
        "characters": [],
        "stages": [],
        "effects": [],
        "warnings": [],
    }
    with zipfile.ZipFile(path) as vault:
        entries = vault.infolist()
        by_path = validate_entries(entries, aggregate_limit=MAX_ARCHIVE)
        by_name = basename_index(entries)
        metadata_entry = by_path.get("metadata.json")
        if metadata_entry is None or metadata_entry.file_size > MAX_METADATA:
            raise InvalidImport("vault must contain a bounded metadata.json")
        try:
            metadata = json.loads(vault.read(metadata_entry))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise InvalidImport(f"metadata.json is invalid: {exc}") from exc
        if not isinstance(metadata, dict) or not isinstance(metadata.get("characters"), dict):
            raise InvalidImport("metadata.json does not contain a character catalog")

        ids: set[str] = set()
        for metadata_character, group in metadata["characters"].items():
            checkpoint()
            text_field(metadata_character, "character name")
            if not isinstance(group, dict) or not isinstance(group.get("skins", []), list):
                raise InvalidImport(f"invalid skin list for {metadata_character}")
            for skin in group.get("skins", []):
                if not isinstance(skin, dict):
                    raise InvalidImport(f"invalid skin record for {metadata_character}")
                skin_id = text_field(skin.get("id"), "skin id")
                if skin_id.casefold() in ids:
                    raise InvalidImport(f"duplicate skin id: {skin_id}")
                ids.add(skin_id.casefold())
                filename = text_field(skin.get("filename"), "skin filename", 512)
                if "/" in filename or "\\" in filename:
                    raise InvalidImport(f"skin filename must be a basename: {filename}")
                costume_code = text_field(skin.get("costume_code"), "costume code", 16)
                entry = unique_named(by_name, filename, "skin filename")
                archive_bytes = vault.read(entry)
                dat = inspect_nested_character(archive_bytes, costume_code + ".dat")
                recorded_hash = skin.get("dat_hash")
                hash_matches = not recorded_hash or str(recorded_hash).casefold() == dat["md5"]
                if not hash_matches:
                    report["warnings"].append(
                        f"skin {skin_id} has stale Nucleus dat_hash metadata; validated DAT identity was used"
                    )
                stem = filename[:-4] if filename.lower().endswith(".zip") else filename
                companions = {}
                parent = entry.filename.rsplit("/", 1)[0] + "/" if "/" in entry.filename else ""
                for key, suffix in (("csp", "_csp.png"), ("stock", "_stc.png")):
                    companion_path = (parent + stem + suffix).casefold()
                    companion = by_path.get(companion_path)
                    promised = bool(skin.get("has_" + key))
                    if promised and companion is None:
                        report["warnings"].append(
                            f"skin {skin_id} promises a missing {key} companion; vanilla UI fallback is required"
                        )
                    if companion is not None:
                        companions[key] = png_info(vault.read(companion), companion.filename)
                report["characters"].append({
                    "id": skin_id,
                    "display_name": text_field(skin.get("color"), "skin color/name"),
                    "metadata_character": metadata_character,
                    "archive": entry.filename,
                    "costume_code": costume_code,
                    "dat": dat,
                    "recorded_dat_hash": recorded_hash,
                    "recorded_dat_hash_matches": hash_matches,
                    "companions": companions,
                })
                checkpoint(f"character {metadata_character}/{skin_id}")

            extras = group.get("extras", {})
            if extras is None:
                extras = {}
            if not isinstance(extras, dict):
                raise InvalidImport(f"invalid extras catalog for {metadata_character}")
            for scope, variants in extras.items():
                if not isinstance(variants, list):
                    raise InvalidImport(f"invalid extra list {metadata_character}/{scope}")
                for variant in variants:
                    effect_id = text_field(variant.get("id"), "effect id")
                    model_file = text_field(variant.get("model_file"), "effect model path", 512)
                    candidates = [item for item in entries if item.filename.casefold().endswith(("/" + model_file).casefold())]
                    if len(candidates) != 1:
                        raise InvalidImport(f"effect model path must resolve once: {model_file}")
                    raw = vault.read(candidates[0])
                    if len(raw) > MAX_ASSET:
                        raise InvalidImport(f"effect model exceeds 64 MB: {effect_id}")
                    target = EFFECT_TARGETS.get((metadata_character.casefold(), str(scope).casefold()))
                    if str(scope).casefold() == "common_shield":
                        target = "EfCoData.dat"
                    result = {
                        "id": effect_id,
                        "name": text_field(variant.get("name"), "effect name"),
                        "character": metadata_character,
                        "scope": text_field(scope, "effect scope"),
                        "model_path": candidates[0].filename,
                        "sha256": hashlib.sha256(raw).hexdigest(),
                        "target_path": target,
                    }
                    if target is None:
                        result.update(status="rejected", error="unsupported effect scope")
                    elif iso is None:
                        result.update(status="cataloged_validation_deferred")
                    else:
                        try:
                            _, effect_result = materialize_effect_dat(
                                target, read_disc_file(iso, target), raw)
                            result.update(effect_result)
                        except (InvalidImport, OSError, ValueError, struct.error) as exc:
                            result.update(status="rejected", error=str(exc))
                    report["effects"].append(result)
                    checkpoint(f"effect {metadata_character}/{scope}/{effect_id}: {result['status']}")

        stages = metadata.get("stages", {})
        if not isinstance(stages, dict):
            raise InvalidImport("metadata stages catalog is invalid")
        for stage_id, group in stages.items():
            text_field(stage_id, "stage id")
            if not isinstance(group, dict) or not isinstance(group.get("variants", []), list):
                raise InvalidImport(f"invalid stage variant list for {stage_id}")
            for variant in group.get("variants", []):
                variant_id = text_field(variant.get("id"), "stage variant id")
                filename = variant.get("filename")
                if not filename:
                    report["warnings"].append(
                        f"stage {stage_id}/{variant_id} has metadata only and cannot be imported"
                    )
                    report["stages"].append({"stage": stage_id, "id": variant_id, "status": "metadata_only"})
                    checkpoint(f"stage {stage_id}/{variant_id}: metadata_only")
                    continue
                filename = text_field(filename, "stage filename", 512)
                expected_path = f"das/{stage_id}/{filename}".casefold()
                entry = by_path.get(expected_path)
                if entry is None:
                    entry = unique_named(by_name, filename, "stage filename")
                target = STAGE_TARGETS.get(stage_id.casefold())
                try:
                    member, candidate = inspect_nested_visual(vault.read(entry), "stage")
                    nested = {"dat_member": member, "target_path": target,
                              "sha256": hashlib.sha256(candidate).hexdigest()}
                    if target is None:
                        nested.update(status="rejected", error="unsupported stage target")
                    else:
                        # Full Nucleus stage DATs are supported offline even when HSD repacking
                        # changes their layout. dat_layout still proves a bounded structural DAT
                        # with identifiable GX resources before it reaches the native catalog.
                        layout = dat_layout(candidate)
                        nested.update(status="full_stage_project",
                                      image_descriptors=len(layout["images"]),
                                      palette_descriptors=len(layout["palettes"]),
                                      visual_only_compatible=None)
                        if iso is not None:
                            try:
                                nested["visual_only_comparison"] = compare_stage_dat(
                                    read_disc_file(iso, target), candidate)
                                nested["visual_only_compatible"] = True
                            except (InvalidImport, OSError, ValueError, struct.error) as exc:
                                nested["visual_only_compatible"] = False
                                nested["visual_only_difference"] = str(exc)
                except (InvalidImport, OSError, ValueError, struct.error, zipfile.BadZipFile, RuntimeError) as exc:
                    reason = str(exc)
                    report["warnings"].append(f"stage {stage_id}/{variant_id} is rejected: {reason}")
                    nested = {"status": "rejected", "error": reason, "target_path": target}
                report["stages"].append({
                    "stage": stage_id,
                    "id": variant_id,
                    "name": text_field(variant.get("name"), "stage name"),
                    "archive": entry.filename,
                    **nested,
                })
                checkpoint(f"stage {stage_id}/{variant_id}: {nested['status']}")

        report["archive_entries"] = len(entries)
        report["summary"] = {
            "character_variants": len(report["characters"]),
            "character_slots": len({item["costume_code"].casefold() for item in report["characters"]}),
            "stage_records": len(report["stages"]),
            "installable_stages": sum(item.get("status") == "full_stage_project" for item in report["stages"]),
            "visual_only_stages": sum(item.get("visual_only_compatible") is True for item in report["stages"]),
            "compatible_effects": sum(item.get("status") in (
                "texture_payload_only", "dedicated_effect_archive", "particle_color_merge"
            ) for item in report["effects"]),
            "rejected_stages": sum(item.get("status") in ("rejected", "metadata_only") for item in report["stages"]),
            "rejected_effects": sum(item.get("status") == "rejected" for item in report["effects"]),
            "warnings": len(report["warnings"]),
        }
        report["last_completed_record"] = last_completed
        return report


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="backslashreplace")
        sys.stderr.reconfigure(errors="backslashreplace")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("vault", type=Path)
    parser.add_argument("--iso", type=Path, help="clean NTSC 1.02 ISO used read-only for exact resource comparison")
    parser.add_argument("--progress", action="store_true", help="flush one progress line after every completed record")
    parser.add_argument("--timeout-seconds", type=float, help="fail closed after this bounded wall-clock duration")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    try:
        if args.timeout_seconds is not None and args.timeout_seconds <= 0:
            raise InvalidImport("--timeout-seconds must be positive")
        report = validate_vault(args.vault, args.iso, progress=args.progress,
                                timeout_seconds=args.timeout_seconds)
    except (InvalidImport, OSError, RuntimeError, zipfile.BadZipFile) as exc:
        if args.json:
            print(json.dumps({"ok": False, "error": str(exc)}, indent=2))
        else:
            print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    output = {"ok": True, **report}
    if args.json:
        print(json.dumps(output, indent=2))
    else:
        summary = report["summary"]
        print(f"PASS: {summary['character_variants']} character variants across {summary['character_slots']} slots")
        if args.iso:
            print(f"PROJECT STAGES: {summary['installable_stages']} full project replacements / {summary['rejected_stages']} rejected")
            print(f"PROJECT STAGES: {summary['visual_only_stages']} also satisfy the cosmetic-only byte gate")
            print(f"PROJECT VISUALS: {summary['compatible_effects']} compatible / {summary['rejected_effects']} rejected effects/shields")
        else:
            print(f"PROJECT STAGES: {summary['installable_stages']} full project replacements / {summary['rejected_stages']} rejected")
            print(f"DEFERRED: {len(report['effects'])} effect/shield records require --iso validation")
        for warning in report["warnings"]:
            print(f"WARNING: {warning}")
        print(f"Vault SHA-256: {report['source_sha256']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
