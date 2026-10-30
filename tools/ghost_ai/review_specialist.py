"""Ask Jev to triage verified specialist evidence; it is not a gameplay verdict."""
import hashlib
import json
import os
from pathlib import Path
import urllib.request

from build_release_manifest import paired_baseline_gain

HERE = Path(os.environ.get('MELEE_GHOST_AI_HOME', Path(__file__).resolve().parent))


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8')) if path.exists() else None


def compact(report):
    if not isinstance(report, dict):
        return report
    keys = (
        'pro', 'split', 'checkpoint_sha256', 'games_evaluated', 'independent_split_groups',
        'total_sequential_decision_frames', 'mean_group_joint_action_nll',
        'mean_group_joint_action_nll_bootstrap95', 'mean_game_joint_action_nll',
        'mean_game_joint_action_nll_bootstrap95', 'mean_game_stick_accuracy',
        'mean_game_cstick_accuracy', 'mean_game_button_brier', 'frame_weighted_joint_action_nll',
        'mean_group_factorized_action_nll', 'mean_group_factorized_action_nll_bootstrap95',
        'mean_game_factorized_action_nll', 'mean_game_factorized_action_nll_bootstrap95',
        'frame_weighted_factorized_action_nll', 'baseline', 'limitations',
    )
    return {key: report[key] for key in keys if key in report}


def main():
    pro = os.environ['MELEE_GHOST_REVIEW_PRO']
    folder = HERE / 'specialists' / pro
    training = read_json(folder / 'training-manifest.json')
    development = read_json(folder / 'development-pilot.json')
    development_sequential = read_json(folder / 'sequential-development.json')
    final_test = read_json(folder / 'sequential-test.json')
    int8_development = read_json(folder / 'sequential-development-int8.json')
    int8_test = read_json(folder / 'sequential-test-int8.json')
    frequency_development = read_json(HERE / 'runs' / f'{pro}-frequency-baseline-development.json')
    frequency_test = read_json(HERE / 'runs' / f'{pro}-frequency-baseline-test.json')
    final_test_sampled = read_json(folder / 'final-test.json')
    audit = read_json(HERE / 'runs' / 'specialist-data-audit.json')
    audit = next((row for row in (audit or {}).get('reports', []) if row.get('pro') == pro), None)
    if not training or not development:
        raise SystemExit('Need a training manifest and development evaluation before requesting review')
    baseline_dev_gain = (paired_baseline_gain(development_sequential, frequency_development)
                         if development_sequential and frequency_development else None)
    baseline_test_gain = (paired_baseline_gain(final_test, frequency_test)
                          if final_test and frequency_test else None)
    evidence = {
        'pro': pro,
        'identity_code': training.get('identity_code'),
        'named_pro_only': training.get('named_pro_only'),
        'teacher_used': training.get('teacher_used'),
        'general_ranked_used': training.get('general_ranked_used'),
        'source_manifest_sha256': training.get('source_manifest_sha256'),
        'verified_data_audit': audit,
        'game_split_counts': training.get('games'),
        'training_steps': training.get('steps'),
        'training_seconds': training.get('seconds'),
        'development': compact(development),
        'development_sequential': compact(development_sequential),
        'final_test': compact(final_test),
        'final_test_sampled': compact(final_test_sampled),
        'int8_development': compact(int8_development),
        'int8_test': compact(int8_test),
        'frequency_baseline_development': compact(frequency_development),
        'frequency_baseline_test': compact(frequency_test),
        'frequency_baseline_paired_gain': {
            'development': baseline_dev_gain,
            'test': baseline_test_gain,
        },
        'interpretation_limits': [
            'Per-frame replay prediction is not live match strength or a carbon-copy test.',
            'Frames sampled from a replay are correlated; game/session is the unit for generalization.',
            'Jev is a reviewer/triage aid; it cannot verify replay ownership or certify release readiness.',
        ],
    }
    questions = {
        'data_separation': {
            'type': 'noul',
            'instructions': 'Does the supplied evidence show this checkpoint used only the named player’s verified corpus, with no general-ranked examples or other named players?',
        },
        'heldout_is_not_strength': {
            'type': 'noul',
            'instructions': 'Does replay action likelihood alone establish that this policy can play at pro level or copy the player in live matches?',
        },
        'ready_as_carbon_copy': {
            'type': 'noul',
            'instructions': 'Does the evidence establish a carbon-copy pro ghost ready to claim as exact imitation?',
        },
        'int8_development_gate': {
            'type': 'noul',
            'instructions': 'Does the paired development replay evaluation show that the int8 checkpoint stays within the explicit 0.01 joint-action-NLL increase limit?',
        },
        'beats_frequency_baseline': {
            'type': 'noul',
            'instructions': 'Does the paired development group comparison show that the selected recurrent policy has lower factorized action NLL than the same pro’s train-only frequency baseline, with the 95% group-bootstrap interval entirely in the policy’s favor?',
            'criteria': {
                'true': 'The paired baseline-minus-policy group-NLL gain is positive and its full 95% bootstrap interval is above zero.',
                'false': 'The gain is zero or negative, or the 95% bootstrap interval includes zero.',
            },
        },
        'next_gate': {
            'type': 'choice',
            'instructions': 'Choose the next evidence-based gate for this player-specific policy.',
            'criteria': {
                'independent_match_eval': 'Run controlled matches against same-character baselines and inspect behavior; report results separately from replay likelihood.',
                'improve_replay_prediction': 'Tune solely against development likelihood without using the final test split.',
                'expand_verified_corpus': 'Seek additional identity-verified games or sessions and preserve separate splits.',
                'ship_as_exact': 'The current evidence already proves an exact, pro-level carbon copy.',
                'insufficient_evidence': 'There is no concrete next evaluation or data step supported by the supplied evidence.',
            },
        },
    }
    request = {'model': 'jev-latest', 'state': evidence, 'questions': questions}
    body = json.dumps(request, separators=(',', ':')).encode()
    run = HERE / 'runs' / ('specialist-review-' + pro)
    run.mkdir(parents=True, exist_ok=True)
    (run / 'jev-request.json').write_bytes(json.dumps(request, indent=2).encode())
    req = urllib.request.Request(
        'https://api.typesafe.ai/v1/systemone', data=body,
        headers={'Authorization': 'Bearer ' + os.environ['TYPESAFE_API_KEY'], 'Content-Type': 'application/json'},
    )
    with urllib.request.urlopen(req, timeout=60) as response:
        result = json.load(response)
    result['evidence_sha256'] = hashlib.sha256(json.dumps(evidence, sort_keys=True).encode()).hexdigest()
    (run / 'jev-review.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
