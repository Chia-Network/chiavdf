"""Summarize paired, independently verified three-board USB trials."""
import argparse
from collections import defaultdict
import json
from pathlib import Path
import re
import statistics


def distribution(values):
    values = sorted(values)
    if not values:
        return None
    return dict(n=len(values), median=statistics.median(values),
                p90=values[min(len(values)-1, int(.9*len(values)))],
                maximum=max(values), mean=statistics.mean(values))


def timings(raw):
    """Board-local process/job identifiers must never be merged."""
    rows = defaultdict(dict)
    jobs = defaultdict(dict)
    telemetry = []
    for line in raw.splitlines():
        board = re.search(r'BOARD_LOC=(\d+) ', line)
        if not board:
            continue
        if 'PROOF_PROFILE ' in line or 'PROOF_SEGMENT ' in line:
            tag = 'PROOF_PROFILE ' if 'PROOF_PROFILE ' in line else 'PROOF_SEGMENT '
            fields = dict(re.findall(r'(\w+)=([^\s]+)', line.split(tag, 1)[1]))
            if tag == 'PROOF_PROFILE ' and fields.get('stage') in ('state_ready', 'asic_started'):
                jobs[(board[1], fields['job'], fields['engine'])][fields['stage']] = int(fields['at_us'])
            if 'iters' not in fields:
                continue
            key = (board[1], fields['job'], fields['engine'], fields['iters'])
            if tag == 'PROOF_SEGMENT ' and fields.get('checkpoint') == '0':
                rows[key]['published'] = int(fields['published_us'])
            elif tag == 'PROOF_PROFILE ' and fields.get('stage') in ('delivery_ready', 'retrieved', 'sent'):
                rows[key][fields['stage']] = int(fields['at_us'])
        elif 'TELEMETRY_PROFILE ' in line:
            fields = dict(re.findall(r'(\w+)=([^\s]+)', line.split('TELEMETRY_PROFILE ', 1)[1]))
            telemetry.append(dict(board=board[1], **{k:int(v) for k,v in fields.items()}))
    complete = [r for r in rows.values() if all(k in r for k in ('published','delivery_ready','retrieved','sent'))]
    invalid = [r for r in complete if not r['published'] <= r['delivery_ready'] <= r['retrieved'] <= r['sent']]
    samples = dict(checkpoint_wait_us=[r['delivery_ready']-r['published'] for r in complete],
        ready_to_retrieved_us=[r['retrieved']-r['delivery_ready'] for r in complete],
        retrieved_to_write_complete_us=[r['sent']-r['retrieved'] for r in complete],
        publication_to_write_complete_us=[r['sent']-r['published'] for r in complete],
        state_preparation_us=[r['state_ready'] for r in jobs.values() if 'state_ready' in r],
        state_ready_to_asic_started_us=[r['asic_started']-r['state_ready'] for r in jobs.values()
                                      if 'asic_started' in r and 'state_ready' in r])
    return dict(deliveries=len(complete), invalid_marker_order=invalid,
        **{k:distribution(v) for k,v in samples.items()}, samples=samples,
        async_telemetry=dict(samples=len(telemetry),
            pending_polls=distribution([r['pending_polls'] for r in telemetry]),
            conversion_elapsed_us=distribution([r['conversion_elapsed_us'] for r in telemetry]),
            invalid_early_reads=sum(r['conversion_elapsed_us']<10000 or r['pending_polls']<1 for r in telemetry)))


def summarize(directory):
    events = [json.loads(line) for line in (directory/'trial-events.jsonl').read_text().splitlines()]
    trials = []
    pairs = defaultdict(dict)
    stages = defaultdict(lambda:defaultdict(list))
    for event in events:
        path = directory/(event['label']+'.json')
        result = json.loads(path.read_text())
        timing = timings(result['raw_log'])
        samples = timing.pop('samples')
        verified = (event['exit_status'] == 0 and result['serialized_proofs_verified']
                    and result['restore_proof_verified'] and not result['errors']
                    and not timing['invalid_marker_order'])
        row = dict(event, accepted=verified, timings=timing,
                   wire_ips_by_target={str(n):statistics.median(r['estimated_ips'] for r in result['records'] if r['iterations']==n)
                                       for n in sorted({r['iterations'] for r in result['records']})},
                   wire_elapsed_by_chain_target={str(r['chain'])+':'+str(r['iterations']):r['elapsed_seconds'] for r in result['records']})
        trials.append(row)
        if event['label'].startswith('pair-') and verified:
            _, repeat, candidate, position, variant = event['label'].split('-')
            pairs[(repeat,candidate)][variant] = result
            for name,values in samples.items():
                stages[event['variant']][name].extend(values)
    comparisons = defaultdict(list)
    for (repeat,candidate), pair in sorted(pairs.items()):
        if 'baseline' not in pair or candidate not in pair:
            continue
        base = {(r['chain'],r['iterations']):r for r in pair['baseline']['records']}
        test = {(r['chain'],r['iterations']):r for r in pair[candidate]['records']}
        keys = sorted(base.keys() & test.keys())
        gains = [100*(test[k]['estimated_ips']/base[k]['estimated_ips']-1) for k in keys]
        comparisons[candidate].append(dict(repeat=int(repeat), matched_deliveries=len(keys),
            median_ips_gain_percent=statistics.median(gains),
            mean_ips_gain_percent=statistics.mean(gains),
            cumulative_elapsed_reduction_percent=100*(1-sum(test[k]['elapsed_seconds'] for k in keys)/sum(base[k]['elapsed_seconds'] for k in keys)),
            gain_by_target={str(n):statistics.median(100*(test[k]['estimated_ips']/base[k]['estimated_ips']-1) for k in keys if k[1]==n) for n in sorted({k[1] for k in keys})}))
    return dict(methodology='Three boards, one physical engine per board. Identical proof targets paired with alternating trial order and rotated discriminants. IPS is per-chain iterations divided by challenge-to-complete-proof receipt time. Chains are not additive. Timings separate checkpoint dependencies from retrieval and application write completion. Small synthetic samples do not establish mainnet hourly or long-term stability gains.',
                trials=trials, comparisons=dict(comparisons),
                aggregate_paired_timings={variant:{name:distribution(values) for name,values in sample.items()}
                                         for variant,sample in stages.items()},
                accepted_proofs=sum(t['proof_count'] for t in trials if t['accepted']),
                rejected_trials=[t['label'] for t in trials if not t['accepted']])


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory',type=Path)
    p.add_argument('--output',required=True,type=Path)
    args=p.parse_args()
    args.output.write_text(json.dumps(summarize(args.directory),indent=2)+'\n')
