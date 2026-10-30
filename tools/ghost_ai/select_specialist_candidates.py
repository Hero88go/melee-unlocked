"""Choose each pro-only checkpoint using sequential development replay likelihood."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil

from players import PLAYERS

HOME = Path(os.environ.get('MELEE_GHOST_AI_HOME', Path(__file__).resolve().parent))


def read(path):
    return json.loads(path.read_text(encoding='utf-8'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pros', nargs='*', choices=list(PLAYERS), default=list(PLAYERS))
    parser.add_argument('--seeds', nargs='+', type=int, required=True)
    args = parser.parse_args()
    audit = read(HOME / 'runs' / 'specialist-data-audit.json')
    if not audit.get('all_pass'):
        raise SystemExit('Replay provenance audit must pass before candidate selection')
    audits = {row['pro']: row for row in audit['reports']}
    results = {}
    for pro in args.pros:
        audit_row = audits.get(pro)
        if not audit_row or audit_row.get('errors'):
            raise SystemExit(f'{pro}: missing or failed provenance audit')
        candidates = []
        for seed in args.seeds:
            folder = HOME / 'specialists' / 'candidates' / f'{pro}-seed{seed}'
            training = read(folder / 'training-manifest.json')
            development = read(folder / 'sequential-development.json')
            checkpoint = folder / 'model.pt'
            if (training.get('pro') != pro or training.get('seed') != seed or
                    training.get('identity_code') != PLAYERS[pro]['code'] or
                    training.get('named_pro_only') is not True or training.get('teacher_used') is not False or
                    training.get('general_ranked_used') is not False or training.get('final_test_used') is not False or
                    training.get('complete_corpus_exposure') is not True or
                    training.get('source_manifest_sha256') != audit_row.get('manifest_sha256') or
                    training.get('games') != audit_row.get('split_games') or
                    development.get('pro') != pro or development.get('split') != 'development' or
                    development.get('games_evaluated') != audit_row['split_games']['development'] or
                    development.get('independent_split_groups') != audit_row['split_group_counts']['development'] or
                    development.get('checkpoint_sha256') != hashlib.sha256(checkpoint.read_bytes()).hexdigest()):
                raise SystemExit(f'{pro} seed {seed}: candidate evidence is incomplete or mismatched')
            candidates.append({
                'seed': seed,
                'folder': str(folder),
                'mean_group_joint_action_nll': development['mean_group_joint_action_nll'],
                'group_bootstrap95': development['mean_group_joint_action_nll_bootstrap95'],
                'mean_game_joint_action_nll': development['mean_game_joint_action_nll'],
                'checkpoint_sha256': development['checkpoint_sha256'],
                'best_batch_development_loss': training.get('best_development_loss'),
                'complete_corpus_exposure': training.get('complete_corpus_exposure'),
            })
        selected = min(candidates, key=lambda item: item['mean_group_joint_action_nll'])
        source = Path(selected['folder'])
        target = HOME / 'specialists' / pro
        target.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source / 'model.pt', target / 'model.pt')
        shutil.copy2(source / 'sequential-development.json', target / 'sequential-development.json')
        training = read(source / 'training-manifest.json')
        training['selected_by'] = 'lowest full sequential-development mean_group_joint_action_nll'
        training['selected_candidate_seed'] = selected['seed']
        training['candidate_scores'] = candidates
        training['final_test_used'] = False
        (target / 'training-manifest.json').write_text(json.dumps(training, indent=2), encoding='utf-8')
        selection = {'pro': pro, 'selection_split': 'development', 'candidates': candidates, 'selected': selected}
        (target / 'selection.json').write_text(json.dumps(selection, indent=2), encoding='utf-8')
        results[pro] = {'selected_seed': selected['seed'],
                        'development_nll': selected['mean_group_joint_action_nll'],
                        'candidates': len(candidates)}
    print(json.dumps(results, indent=2))


if __name__ == '__main__':
    main()
