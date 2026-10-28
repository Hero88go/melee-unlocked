"""Ask Jev bounded questions about the fidelity plan; preserve the evidence and answers."""
import hashlib
import json
import os
from pathlib import Path
import urllib.request

HERE = Path(os.environ.get('MELEE_GHOST_AI_HOME', Path(__file__).resolve().parent))


def main():
    plan = (HERE / 'PRO_FIDELITY_PLAN.md').read_text(encoding='utf-8')
    questions = {}
    checks = {
        'identity': 'Does the plan require replay player identity evidence beyond character choice or an AI guess?',
        'separation': 'Does the plan keep general-ranked demonstrations, named-pro demonstrations, teacher targets and simulated games separately attributable?',
        'fidelity_gate': 'Does the plan test player fidelity separately from win rate and require same-character controls?',
        'no_false_completion': 'Does the plan clearly state that Phillip distillation, five-player fidelity and quantization are not completed?',
        'jev_grounding': 'Does the plan require validating Jev judgments against reviewed evidence before using its labels as training supervision?',
        'compression_gate': 'Does the plan reject compression that materially harms measured style or reliable play?',
        'readiness': 'Does the supplied evidence establish that the current build is a validated five-player carbon copy ready for release?',
        'data_sufficiency': 'Does a count of 20,000 to 40,000 games per player alone establish coverage of nearly every interactive situation?',
    }
    for name, instructions in checks.items():
        questions[name] = {'type': 'noul', 'instructions': instructions}
    questions['next_work'] = {
        'type': 'choice',
        'instructions': 'Given the verified starting point, choose the most appropriate immediate prerequisite for faithful five-player training.',
        'criteria': {
            'audit_and_schema': 'Audit identity/provenance, select a fifth corpus and align trainer/export/runtime schemas before scaling training.',
            'train_longer_now': 'Immediately run the current training program longer without fixing provenance or schema.',
            'ship_now': 'Release the existing four-player checkpoint as the complete five-player distilled result.',
            'insufficient_evidence': 'The supplied plan does not support choosing between these work items.',
        },
    }
    request = {'model': 'jev-latest', 'state': {'plan': plan}, 'questions': questions}
    folder = HERE / 'runs' / 'pro-fidelity-plan'
    folder.mkdir(parents=True, exist_ok=True)
    (folder / 'jev-request.json').write_text(json.dumps(request, indent=2), encoding='utf-8')
    req = urllib.request.Request(
        'https://api.typesafe.ai/v1/systemone', data=json.dumps(request).encode(),
        headers={'Authorization': 'Bearer ' + os.environ['TYPESAFE_API_KEY'], 'Content-Type': 'application/json'},
    )
    with urllib.request.urlopen(req, timeout=60) as response:
        result = json.load(response)
    result['plan_sha256'] = hashlib.sha256(plan.encode()).hexdigest()
    (folder / 'jev-review.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
