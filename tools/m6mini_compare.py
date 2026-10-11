"""Alternating before/after runs on an already prepared M6 benchmark host.

Run from the remote chiavdf checkout. Candidate and baseline must be separate
executables (or wrappers); the hardware harness restores the board after each run.
"""
import argparse
import json
import math
from pathlib import Path
import re
import statistics
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--baseline', required=True)
p.add_argument('--candidate', required=True)
p.add_argument('--output', required=True)
p.add_argument('--rounds', type=int, default=3)
p.add_argument('--software', action='store_true')
p.add_argument('--baseline-threads', type=int, default=4)
p.add_argument('--candidate-threads', type=int, default=4)
p.add_argument('--scenario', choices=['balanced', 'staggered', 'irregular'], default='balanced')
a = p.parse_args()
root = Path(a.output)
root.mkdir(parents=True, exist_ok=True)
if not a.software:
    # Moving an executable can break its @executable_path USB-library lookup.
    # Catch loader errors before starting a hardware timing run.
    for executable in [a.baseline, a.candidate]:
        subprocess.run([executable, '--version'], check=True,
                       stdout=subprocess.DEVNULL)
runs = []
for round_idx in range(a.rounds):
    order = ['baseline', 'candidate'] if round_idx % 2 == 0 else ['candidate', 'baseline']
    for label in order:
        executable = getattr(a, label)
        path = root / f'{label}-{round_idx}.json'
        if a.software:
            completed = subprocess.run([executable, 'square', '3000000'],
                                       capture_output=True, text=True, check=True)
            match = re.search(r'Time: (\d+) ms', completed.stdout)
            if not match:
                raise RuntimeError(completed.stdout + completed.stderr)
            result = dict(label=label, round=round_idx,
                          milliseconds=int(match[1]), output=completed.stdout,
                          stderr=completed.stderr)
            path.write_text(json.dumps(result, indent=2) + '\n')
        else:
            subprocess.run(['python3', 'tools/m6mini_proof_delivery.py', '--client',
                            executable, '--threads', str(getattr(a, label + '_threads')),
                            '--scenario', a.scenario, '--output', str(path)], check=True,
                           stdout=subprocess.DEVNULL)
            result = json.loads(path.read_text())
            if result.get('serialized_proofs_verified') is not True:
                raise RuntimeError('Complete serialized proofs were not independently verified')
            result.update(label=label, round=round_idx)
        runs.append(result)
        print(json.dumps(dict(label=label, round=round_idx, completed=True)), flush=True)
if a.software:
    forms = [re.findall(r'^[abc] = .+$', run['output'], re.MULTILINE) for run in runs]
    if any(len(form) != 3 or form != forms[0] for form in forms):
        raise RuntimeError('Software VDF outputs differ between comparison runs')
summary = {}
targets = [3000000] if a.software else runs[0]['targets']
for target in targets:
    values = {}
    for label in ['baseline', 'candidate']:
        samples = []
        for run in runs:
            if run['label'] != label:
                continue
            if a.software:
                samples.append(target * 1000 / run['milliseconds'])
            else:
                samples.extend(r['estimated_ips'] for r in run['records']
                               if r['iterations'] == target)
        values[label] = dict(median_ips=statistics.median(samples),
                             geomean_ips=math.exp(statistics.mean(map(math.log, samples))),
                             minimum_ips=min(samples), samples=samples)
    values['change_percent'] = 100 * (values['candidate']['median_ips'] /
                                     values['baseline']['median_ips'] - 1)
    values['geomean_change_percent'] = 100 * (values['candidate']['geomean_ips'] /
                                             values['baseline']['geomean_ips'] - 1)
    summary[target] = values
(root / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
print(json.dumps(summary, indent=2), flush=True)
