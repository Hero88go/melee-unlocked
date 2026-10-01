"""Prepare source evidence for the character-kind and step-order advisory review.

This reads code only. The candidate inventory is deliberately conservative and is not
a C data-flow analysis or a completeness verdict.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[3]
DECOMP = ROOT / "sourceport/extern/melee"
SOURCE = DECOMP / "src/melee"
PLAN_FILES = ("gm/gm_1601.c", "gm/gm_181A.c", "pl/player.c",
              "gm/gm_1A9B.c", "lb/lbaudio_ax.c")
PLAN_COUNTS = dict(zip(PLAN_FILES, (57, 32, 20, 3, 1)))
DIRECT_INDEX = re.compile(r"\[[^\]\n]*(?:\bckind\b|\bc_kind\b|\bchara\b)[^\]\n]*\]")
KIND_INDEX = re.compile(r"\[[^\]\n]*\bkind\b[^\]\n]*\]")
CHAR_CONSTANT = re.compile(r"\b(?:ChKind_Max|ChKind_None|CKind_Playable_Count|CKind_MasterH)\b")


def read(path):
    return path.read_text(encoding="utf-8", errors="replace")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def commit(path):
    return subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"],
                                   text=True).strip()


def indexed_lines(path, pattern):
    return [{"file": path.relative_to(ROOT).as_posix(), "line": number,
             "source": line.strip(), "match_count": len(pattern.findall(line))}
            for number, line in enumerate(read(path).splitlines(), 1)
            if pattern.search(line) and not line.lstrip().startswith(("//", "/*", "*"))]


def function_excerpt(relative, name):
    path = ROOT / relative
    text = read(path)
    match = re.search(r"^[A-Za-z_][^\n;{}=()]*\b" + re.escape(name) +
                      r"\s*\([^;{}]*\)\s*\{", text, re.MULTILINE)
    if not match:
        raise ValueError("Missing source function " + name)
    start = match.start()
    cursor = text.index("{", match.start())
    depth = 1
    cursor += 1
    # These selected functions have no braces in strings or comments.
    while depth and cursor < len(text):
        depth += (text[cursor] == "{") - (text[cursor] == "}")
        cursor += 1
    if depth:
        raise ValueError("Unclosed source function " + name)
    return {"file": relative, "function": name,
            "line": text.count("\n", 0, start) + 1,
            "sha256": sha(path), "source": text[start:cursor]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)

    direct = [site for relative in PLAN_FILES
              for site in indexed_lines(SOURCE / relative, DIRECT_INDEX)]
    broad = [site for relative in PLAN_FILES
             for site in indexed_lines(SOURCE / relative, KIND_INDEX)]
    outside = [site for path in sorted(SOURCE.rglob("*"))
               if path.suffix in (".c", ".h") and path.relative_to(SOURCE).as_posix() not in PLAN_FILES
               for site in indexed_lines(path, CHAR_CONSTANT)]
    direct_counts = Counter(site["file"] for site in direct)
    broad_counts = Counter(site["file"] for site in broad)

    selections = [
        ("sourceport/game/akaneia/mu_ak_fighters.c", name) for name in (
            "mu_ak_kind_from_mex", "mu_ak_mex_internal", "mu_ak_ckind_from_kind",
            "mu_ak_ckind_from_mex", "mu_ak_mex_external", "mu_ak_css_selectable",
            "clear_slots", "mu_ak_apply")
    ] + [
        ("sourceport/extern/melee/src/melee/pl/player.c", "Player_MuSetAkKind"),
        ("sourceport/game/shim/mu_mex.c", "mu_mex_css_icons"),
        ("sourceport/game/shim/mu_mex.c", "mu_mex_external_of_internal"),
        ("sourceport/game/shim/mu_mex.c", "mu_mex_internal_of_external"),
        ("sourceport/game/shim/mu_replay.c", "convert_player"),
        ("sourceport/game/shim/mu_replay.c", "mu_replay_prepare_scene"),
        ("sourceport/game/shim/mu_replay.c", "mu_replay_post_frame"),
        ("sourceport/game/shim/mu_record_start.inc", "emit_game_start"),
        ("sourceport/extern/melee/src/melee/gm/gm_1601.c", "gm_CKindToSelKind"),
        ("sourceport/extern/melee/src/melee/gm/gm_1601.c", "gm_IsCKindUnlocked"),
        ("sourceport/extern/melee/src/melee/lb/lbaudio_ax.c", "lbAudioAx_80026E84"),
    ]
    excerpts = [function_excerpt(relative, name) for relative, name in selections]
    files = sorted({relative for relative, _ in selections} | {
        "sourceport/game/CMakeLists.txt", "sourceport/game/include/mu_native.h",
        "sourceport/extern/melee/src/melee/ft/forward.h",
        "sourceport/game/akaneia/CREATION_LAYER_PLAN.md",
        "sourceport/game/akaneia/INTEGRATION.md",
        "sourceport/game/tests/ak_character_kind.c",
        "sourceport/game/tests/record_start.c",
        "sourceport/game/shim/mu_slippi_sss.c",
        "sourceport/extern/melee/src/melee/mn/mncharsel.c",
        "sourceport/extern/melee/src/melee/pl/player.h",
        "sourceport/game/akaneia/creation_layer_audit.py",
    })
    state = {
        "milestone": "B1 native Akaneia creation layer, prerequisite review before step 2",
        "model_requirement": "jev-1.13.0, advisory only, one request, 15 second timeout, no retries",
        "source_commit": commit(ROOT), "decomp_commit": commit(DECOMP),
        "tree_status": "Uncommitted source edits. Commit ids do not identify the edited tree; file hashes do.",
        "requirement": "Source Port executes native C only. Retail ids and replay behavior remain unchanged. Added fighters stay locked until native creation and independent digest gates pass.",
        "validation_status": {
            "step1": "SOURCE EDITED, UNBUILT, UNRUN",
            "C_fixture": "Written, not compiled or executed. Tests production conversion functions against reordered m-ex data, missing slots, shifted special fighters and retail passthrough.",
            "runtime_gates": "No new build, replay, online, game boot or state-digest evidence exists for these edits.",
            "step2": "Not implemented. This packet does not authorize an unlock or establish correctness.",
        },
        "step_order": [
            "1 character kinds and wire conversions, keep CSS lock",
            "2 all per-character table users and save-write guards",
            "3 fighter, animation and demo files",
            "4 costumes", "5 PlCo common data", "6 per-kind resets",
            "7 effects", "8 sounds", "9 Kirby safe no-copy guard",
            "10 kind bounds and assertions", "11 native article creation",
            "12 results screen",
        ],
        "legacy_index_claim": {"total": 113, "by_file": PLAN_COUNTS,
                               "status": "Unverified legacy count. Its counting method and full per-site list were not supplied."},
        "measurements": {
            "method": "Count source lines and matches of array indices containing ckind, c_kind or chara in the five named files. Separately collect kind indices, and named character constants outside those files. This is lexical candidate discovery, not C data-flow analysis.",
            "direct_lines_total": len(direct), "direct_matches_total": sum(x["match_count"] for x in direct),
            "direct_lines_by_file": dict(direct_counts),
            "kind_candidate_lines_by_file": dict(broad_counts),
            "outside_constant_candidate_lines": len(outside),
        },
        "local_findings": [
            "The legacy 113 count cannot be treated as a completeness proof. Player mapping contains multiple references per logical site.",
            "The current player direct-index count includes three new writes in Player_MuSetAkKind. Lexical counts are not comparable to the legacy logical-site counts without their original definition.",
            "gm_CKindToSelKind and gm_IsCKindUnlocked directly index the retail ckind_to_selkind_map. They need added-kind guards before any unlock.",
            "lbAudioAx_80026E84 returns zero for ckind beyond ChKind_Max. Safe for bounds, incomplete for added sound banks.",
            "lbdvd.c loops to ChKind_Max for character preload. Its sites are outside the five-file list and need explicit creation-layer classification.",
            "mncharsel.c uses CKind_Playable_Count to restore selections; added kinds will be reset unless the later CSS checks are updated. The lock must remain until that audit.",
            "Replay post-frame events contain internal fighter kind; Game Start and matchmaking selection contain external character kind. Both directions need boundary conversions.",
            "The first external id of an internal fighter is used as the canonical export. Aliases and empty slots need measured disc evidence before accepting mod replay compatibility.",
            "Save reads and writes that pass through selector mappings must be audited by call chain; guarding a selector lookup alone does not establish that added fighters cannot alter retail records.",
            "The proposed order keeps all added fighters locked through the twelve steps. Kirby and article guards must exist before any experimental match can create a fighter.",
        ],
        "hypotheses": [
            "The twelve-step order is reasonable only if selectability and readiness remain separate gates and all unsafe per-character consumers are guarded before experimental runtime tests.",
            "Lexical inventory beyond the five files is necessary but insufficient; a per-site classified checklist is required for step 2 acceptance.",
        ],
        "file_hashes": {relative: sha(ROOT / relative) for relative in files},
        "relevant_excerpts": excerpts,
    }
    questions = {
        "order_safety": {
            "type": "choice",
            "instructions": "Given the edited but unbuilt step 1, twelve proposed steps, retained CSS lock and absence of runtime evidence, is this order safe for proceeding with source work on step 2? Identify prerequisites that must move earlier before any experimental match or unlock.",
            "criteria": {"proceed_source_only": "Order supports source work while locks remain; no runtime or readiness approval.",
                         "revise_order": "Reorder or add concrete prerequisites before continuing.",
                         "insufficient_evidence": "Cannot establish order safety from these source excerpts."},
        },
        "index_coverage": {
            "type": "choice",
            "instructions": "Compare the legacy 113-site claim with lexical measurements and outside-file findings. Is this enough evidence to claim the per-character index audit is complete, or what specific additional inventory and call-chain checks are needed?",
            "criteria": {"incomplete": "The legacy list and lexical scan are incomplete; identify concrete missing users or verification.",
                         "sufficient_inventory": "Evidence establishes complete per-character inventory, though implementations remain unvalidated.",
                         "insufficient_evidence": "Cannot determine coverage from the counting methods and excerpts."},
        },
        "kind_and_wire_design": {
            "type": "choice",
            "instructions": "Review native character ids past None, per-view mapping fill and clear, CSS resource indices and Slippi external/internal conversions. Identify concrete design flaws or missing data evidence. No builds or runtime results exist, and aliases use the first matching external id.",
            "criteria": {"plausible_requires_tests": "Source design is plausible but needs the listed independent build, replay and digest gates.",
                         "source_flaw": "A concrete source or boundary flaw must be fixed; identify it.",
                         "insufficient_evidence": "Disc mapping or source evidence is insufficient to evaluate the design."},
        },
        "acceptance_missing_items": {
            "type": "choice",
            "instructions": "Acceptance requires Wolf model, costumes, sounds, effects, HUD name/stock, results, no writes to added-character save records, retail replay/online gates and matching mod digests before MU_AK_READY. Does the staged checklist omit anything material, including preload, Kirby, item-kind users or results callback defaults?",
            "criteria": {"add_checks": "Add concrete missing checks or safeguards.",
                         "checklist_plausible": "Checklist is plausible; every runtime gate remains unrun.",
                         "insufficient_evidence": "Cannot assess acceptance completeness from the packet."},
        },
    }
    (out / "index-sites.json").write_text(json.dumps({"direct": direct, "kind_candidates": broad,
        "outside_constants": outside}, indent=2) + "\n", encoding="utf-8")
    (out / "state.json").write_text(json.dumps(state, indent=2) + "\n", encoding="utf-8")
    (out / "questions.json").write_text(json.dumps(questions, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(state["measurements"], indent=2))


if __name__ == "__main__":
    main()
