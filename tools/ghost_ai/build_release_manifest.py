"""Assemble a release-side provenance and evaluation manifest for the released quantized students."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct

import numpy as np
from players import PLAYERS

ROOT = Path(__file__).resolve().parents[2]
HOME = Path(os.environ.get('MELEE_GHOST_AI_HOME', Path(__file__).resolve().parent))
# Released players. Hungrybox stays in the runtime roster (indices) but is not packaged yet.
ROSTER = ('mang0', 'zain', 'cody', 'amsa')


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8'))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def paired_group_delta(float_report, int8_report):
    float_games = {row['replay_id']: row for row in float_report['per_game']}
    int8_games = {row['replay_id']: row for row in int8_report['per_game']}
    if float_games.keys() != int8_games.keys():
        raise SystemExit('Float and int8 evaluation must score the same held-out replays')
    groups = {}
    for replay_id, float_game in float_games.items():
        int8_game = int8_games[replay_id]
        group = str(float_game.get('split_group') or replay_id)
        if group != str(int8_game.get('split_group') or replay_id):
            raise SystemExit('Float and int8 evaluation disagree on a replay split group')
        groups.setdefault(group, []).append(
            int8_game['joint_action_nll'] - float_game['joint_action_nll'])
    values = np.asarray([np.mean(group) for group in groups.values()], dtype=np.float64)
    rng = np.random.default_rng(25109)
    if len(values) < 2:
        bounds = [float(values.mean()), float(values.mean())]
    else:
        picks = rng.integers(0, len(values), size=(2000, len(values)))
        means = values[picks].mean(axis=1)
        bounds = [float(np.quantile(means, .025)), float(np.quantile(means, .975))]
    return {'groups': len(groups), 'mean_paired_joint_nll_delta': float(values.mean()),
            'bootstrap95': bounds}


def paired_baseline_gain(model_report, baseline_report):
    model_games = {row['replay_id']: row for row in model_report['per_game']}
    baseline_games = {row['replay_id']: row for row in baseline_report['per_game']}
    if model_games.keys() != baseline_games.keys():
        raise SystemExit('Model and frequency baseline must score the same held-out replays')
    groups = {}
    for replay_id, model_game in model_games.items():
        baseline_game = baseline_games[replay_id]
        group = str(model_game.get('split_group') or replay_id)
        if group != str(baseline_game.get('split_group') or replay_id):
            raise SystemExit('Model and frequency baseline disagree on a replay split group')
        groups.setdefault(group, []).append(
            baseline_game['factorized_action_nll'] - model_game['joint_action_nll'])
    values = np.asarray([np.mean(group) for group in groups.values()], dtype=np.float64)
    rng = np.random.default_rng(25109)
    if len(values) < 2:
        bounds = [float(values.mean()), float(values.mean())]
    else:
        picks = rng.integers(0, len(values), size=(2000, len(values)))
        means = values[picks].mean(axis=1)
        bounds = [float(np.quantile(means, .025)), float(np.quantile(means, .975))]
    return {'groups': len(groups), 'mean_paired_baseline_nll_minus_model_nll': float(values.mean()),
            'bootstrap95': bounds}


def check_recipe(pro, audit, training):
    if (training.get('pro') != pro or training.get('identity_code') != PLAYERS[pro]['code'] or
            training.get('named_pro_only') is not True or training.get('teacher_used') is not False or
            training.get('general_ranked_used') is not False or training.get('final_test_used') is not False):
        raise SystemExit(f'{pro}: training manifest does not prove the pro-only recipe')
    if training.get('source_manifest_sha256') != audit.get('manifest_sha256'):
        raise SystemExit(f'{pro}: trained source manifest differs from the audited source manifest')
    if training.get('games') != audit.get('split_games'):
        raise SystemExit(f'{pro}: trained split counts differ from audited split counts')


def check_model(pro, model_dir):
    model = model_dir / f'ghost_ai_{pro}.bin'
    if not model.is_file():
        raise SystemExit(f'{pro}: missing quantized policy file {model}')
    data = model.read_bytes()
    if len(data) < 56 or data[:4] != b'MUGA':
        raise SystemExit(f'{pro}: invalid runtime model header')
    version = struct.unpack_from('<I', data, 4)[0]
    dims = struct.unpack_from('<12I', data, 8)
    if version != 2 or dims[7] != 1:
        raise SystemExit(f'{pro}: expected an int8, one-player independent policy')
    return {'file': model.name, 'sha256': digest(model), 'bytes': len(data),
            'format_version': version, 'dimensions': list(dims), 'player_embeddings': dims[7]}


def pending_entry(pro, audit, training, directory, model_dir):
    """A pro whose sequential held-out evaluations have not finished: provenance, recipe and file only."""
    check_recipe(pro, audit, training)
    checkpoint = directory / 'model.pt'
    if not checkpoint.is_file():
        raise SystemExit(f'{pro}: missing selected float checkpoint')
    return {
        'identity_code': PLAYERS[pro]['code'],
        'character_id': PLAYERS[pro]['css_character'],
        'identity_methods': training.get('identity_methods'),
        'games': audit['split_games'],
        'split_groups': audit.get('split_group_counts'),
        'canonical_frames': audit['canonical_frames'],
        'source_manifest_sha256': audit['manifest_sha256'],
        'checkpoint_sha256': digest(checkpoint),
        'training': {key: training.get(key) for key in (
            'architecture', 'training_objective', 'initialization', 'seed', 'learning_rate',
            'weight_decay', 'steps', 'seconds', 'best_development_loss', 'complete_corpus_exposure')},
        'evaluation': 'pending: sequential development and test evaluations not finished for this build',
        'model': check_model(pro, model_dir),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--model-dir', type=Path, required=True,
                        help='directory containing ghost_ai_<pro>.bin for all five pros')
    parser.add_argument('--version', default=(ROOT / 'VERSION').read_text(encoding='utf-8').strip())
    parser.add_argument('--out', type=Path, default=None)
    parser.add_argument('--pending', default='',
                        help='comma-separated pros whose held-out evaluations are still running: their provenance, '
                             'recipe and file are checked and the evaluations are written as pending')
    args = parser.parse_args()
    model_dir = args.model_dir.resolve()
    pending = {pro for pro in args.pending.split(',') if pro}

    audit_payload = read_json(HOME / 'runs' / 'specialist-data-audit.json')
    if not audit_payload.get('all_pass'):
        raise SystemExit('All-player replay provenance audit must pass before making the release manifest')
    audits = {row['pro']: row for row in audit_payload['reports']}
    result = {
        'schema_version': 1,
        'release_version': args.version,
        'source_branch': f'rel/{args.version}',
        'model_format': 'MUGA v2, per-row symmetric int8 weights; float32 dequantization at load',
        'training_targets': "Each player's own verified replays only; no other player's games, ranked games or teacher outputs.",
        'fidelity_limit': 'Held-out replay prediction is not a test of live match strength or a carbon-copy guarantee.',
        'players': {},
    }

    for pro in ROSTER:
        audit = audits.get(pro)
        if audit is None or audit.get('errors'):
            raise SystemExit(f'{pro}: missing or failed identity/hash/frame audit')
        directory = HOME / 'specialists' / pro
        training = read_json(directory / 'training-manifest.json')
        if pro in pending:
            result['players'][pro] = pending_entry(pro, audit, training, directory, model_dir)
            continue
        development = read_json(directory / 'sequential-development.json')
        test = read_json(directory / 'sequential-test.json')
        quantized_development = read_json(directory / 'sequential-development-int8.json')
        quantized_test = read_json(directory / 'sequential-test-int8.json')
        baseline_development = read_json(HOME / 'runs' / f'{pro}-frequency-baseline-development.json')
        baseline_test = read_json(HOME / 'runs' / f'{pro}-frequency-baseline-test.json')
        checkpoint = directory / 'model.pt'
        quantized_checkpoint = directory / 'model-int8-dequantized.pt'
        if not checkpoint.is_file():
            raise SystemExit(f'{pro}: missing selected float checkpoint')
        checkpoint_hash = digest(checkpoint)
        quantized_checkpoint_hash = digest(quantized_checkpoint) if quantized_checkpoint.is_file() else None
        if (training.get('pro') != pro or training.get('identity_code') != PLAYERS[pro]['code'] or
                training.get('named_pro_only') is not True or training.get('teacher_used') is not False or
                training.get('general_ranked_used') is not False or training.get('final_test_used') is not False):
            raise SystemExit(f'{pro}: training manifest does not prove the pro-only recipe')
        if training.get('source_manifest_sha256') != audit.get('manifest_sha256'):
            raise SystemExit(f'{pro}: trained source manifest differs from the audited source manifest')
        if training.get('games') != audit.get('split_games'):
            raise SystemExit(f'{pro}: trained split counts differ from audited split counts')
        for label, report, split in (('development', development, 'development'), ('test', test, 'test')):
            if (report.get('pro') != pro or report.get('split') != split or
                    report.get('checkpoint_sha256') != checkpoint_hash or report.get('games_evaluated', 0) <= 0 or
                    report.get('total_sequential_decision_frames', 0) <= 0 or
                    report.get('independent_split_groups') != audit.get('split_group_counts', {}).get(split)):
                raise SystemExit(f'{pro}: missing or mismatched sequential {label} evaluation')
        for label, report, split in (('quantized development', quantized_development, 'development'),
                                     ('quantized test', quantized_test, 'test')):
            if (not quantized_checkpoint_hash or report.get('pro') != pro or report.get('split') != split or
                    report.get('checkpoint_sha256') != quantized_checkpoint_hash or
                    report.get('games_evaluated', 0) <= 0 or report.get('total_sequential_decision_frames', 0) <= 0 or
                    report.get('independent_split_groups') != audit.get('split_group_counts', {}).get(split)):
                raise SystemExit(f'{pro}: missing or mismatched sequential {label} evaluation')
        for label, report, split in (('frequency baseline development', baseline_development, 'development'),
                                     ('frequency baseline test', baseline_test, 'test')):
            if (report.get('pro') != pro or report.get('split') != split or
                    report.get('training_manifest_sha256') != audit.get('manifest_sha256') or
                    report.get('games_evaluated') != audit['split_games'][split] or
                    report.get('independent_split_groups') != audit.get('split_group_counts', {}).get(split)):
                raise SystemExit(f'{pro}: missing or mismatched {label} evaluation')
        if test.get('games_evaluated') != audit['split_games']['test']:
            raise SystemExit(f'{pro}: sequential test did not score every audited test replay')
        if quantized_test.get('games_evaluated') != audit['split_games']['test']:
            raise SystemExit(f'{pro}: quantized sequential test did not score every audited test replay')
        dev_quantization = paired_group_delta(development, quantized_development)
        test_quantization = paired_group_delta(test, quantized_test)
        dev_baseline = paired_baseline_gain(development, baseline_development)
        test_baseline = paired_baseline_gain(test, baseline_test)
        if dev_quantization['mean_paired_joint_nll_delta'] > 0.01:
            raise SystemExit(f'{pro}: int8 dev joint-NLL degradation exceeds the 0.01 gate')
        if training.get('final_test_used') is not False:
            raise SystemExit(f'{pro}: final test split influenced training')

        filename = f'ghost_ai_{pro}.bin'
        model = model_dir / filename
        if not model.is_file():
            raise SystemExit(f'{pro}: missing quantized policy file {model}')
        data = model.read_bytes()
        if len(data) < 56 or data[:4] != b'MUGA':
            raise SystemExit(f'{pro}: invalid runtime model header')
        version = struct.unpack_from('<I', data, 4)[0]
        dims = struct.unpack_from('<12I', data, 8)
        if version != 2 or dims[7] != 1:
            raise SystemExit(f'{pro}: expected an int8, one-player independent policy')

        review_path = HOME / 'runs' / f'specialist-review-{pro}' / 'jev-review.json'
        review = read_json(review_path) if review_path.is_file() else None
        result['players'][pro] = {
            'identity_code': PLAYERS[pro]['code'],
            'character_id': PLAYERS[pro]['css_character'],
            'identity_methods': training.get('identity_methods'),
            'games': audit['split_games'],
            'split_groups': audit.get('split_group_counts'),
            'canonical_frames': audit['canonical_frames'],
            'source_manifest_sha256': audit['manifest_sha256'],
            'checkpoint_sha256': checkpoint_hash,
            'training': {key: training.get(key) for key in (
                'architecture', 'training_objective', 'initialization', 'seed', 'learning_rate',
                'weight_decay', 'steps', 'seconds', 'best_development_loss', 'complete_corpus_exposure',
                'selected_candidate_seed', 'selected_by', 'candidate_scores')},
            'development': {key: development.get(key) for key in (
                'games_evaluated', 'independent_split_groups', 'total_sequential_decision_frames',
                'mean_group_joint_action_nll', 'mean_group_joint_action_nll_bootstrap95', 'mean_game_joint_action_nll',
                'mean_game_joint_action_nll_bootstrap95', 'mean_game_stick_accuracy',
                'mean_game_cstick_accuracy', 'mean_game_button_brier')},
            'test': {key: test.get(key) for key in (
                'games_evaluated', 'independent_split_groups', 'total_sequential_decision_frames',
                'mean_group_joint_action_nll', 'mean_group_joint_action_nll_bootstrap95', 'mean_game_joint_action_nll',
                'mean_game_joint_action_nll_bootstrap95', 'mean_game_stick_accuracy',
                'mean_game_cstick_accuracy', 'mean_game_button_brier')},
            'frequency_baseline': {
                'description': baseline_development.get('baseline'),
                'development_model_gain': dev_baseline,
                'test_model_gain': test_baseline,
                'development': {key: baseline_development.get(key) for key in (
                    'games_evaluated', 'independent_split_groups', 'total_sequential_decision_frames',
                    'mean_group_factorized_action_nll', 'mean_group_factorized_action_nll_bootstrap95',
                    'mean_game_factorized_action_nll', 'mean_game_factorized_action_nll_bootstrap95')},
                'test': {key: baseline_test.get(key) for key in (
                    'games_evaluated', 'independent_split_groups', 'total_sequential_decision_frames',
                    'mean_group_factorized_action_nll', 'mean_group_factorized_action_nll_bootstrap95',
                    'mean_game_factorized_action_nll', 'mean_game_factorized_action_nll_bootstrap95')},
            },
            'int8': {
                'test_checkpoint_sha256': quantized_checkpoint_hash,
                'development_paired_group_delta': dev_quantization,
                'development_max_allowed_joint_nll_delta': 0.01,
                'test_paired_group_delta': test_quantization,
                'development': {key: quantized_development.get(key) for key in (
                    'games_evaluated', 'independent_split_groups', 'total_sequential_decision_frames',
                    'mean_group_joint_action_nll', 'mean_group_joint_action_nll_bootstrap95', 'mean_game_joint_action_nll',
                    'mean_game_joint_action_nll_bootstrap95', 'mean_game_stick_accuracy',
                    'mean_game_cstick_accuracy', 'mean_game_button_brier')},
                'test': {key: quantized_test.get(key) for key in (
                    'games_evaluated', 'independent_split_groups', 'total_sequential_decision_frames',
                    'mean_group_joint_action_nll', 'mean_group_joint_action_nll_bootstrap95', 'mean_game_joint_action_nll',
                    'mean_game_joint_action_nll_bootstrap95', 'mean_game_stick_accuracy',
                    'mean_game_cstick_accuracy', 'mean_game_button_brier')},
                'test_joint_nll_delta_vs_float': quantized_test['mean_game_joint_action_nll'] - test['mean_game_joint_action_nll'],
            },
            'model': {'file': filename, 'sha256': digest(model), 'bytes': len(data),
                      'format_version': version, 'dimensions': list(dims), 'player_embeddings': dims[7]},
            'jev_review': review.get('answers') if review else None,
        }

    out = args.out or (model_dir / 'ghost_ai_models.json')
    out.parent.mkdir(parents=True, exist_ok=True)
    tmp = out.with_name(out.name + '.part')
    tmp.write_text(json.dumps(result, indent=2), encoding='utf-8')
    tmp.replace(out)
    print(json.dumps({'output': str(out), 'players': {
        pro: {'bytes': row['model']['bytes'], 'sha256': row['model']['sha256'],
              'games': row['games'], 'test_nll': row.get('test', {}).get('mean_game_joint_action_nll')}
        for pro, row in result['players'].items()}}, indent=2))


if __name__ == '__main__':
    main()
