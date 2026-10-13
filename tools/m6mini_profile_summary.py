"""Summarize opt-in hardware proof timing logs and export observed request traces."""
import argparse
from collections import Counter
import json
from pathlib import Path
import re
import statistics


def parse_records(raw, tag):
    records = []
    for line in raw.splitlines():
        if tag + ' ' not in line:
            continue
        fields = dict(re.findall(r'(\w+)=([^\s]+)', line.split(tag + ' ', 1)[1]))
        records.append({k: int(v) if k not in ('initial_form', 'discriminant', 'core_samples') and v.isdigit() else v
                        for k, v in fields.items()})
    return records


def summarize(raw):
    segments = parse_records(raw, 'PROOF_SEGMENT')
    workers = parse_records(raw, 'PROOF_WORKER')
    cores = Counter()
    for worker in workers:
        for cpu, count in re.findall(r'(\d+):(\d+)', worker.get('core_samples', '')):
            cores[int(cpu)] += int(count)
    result = {'segments': len(segments), 'workers': len(workers),
              'core_samples': dict(sorted(cores.items())),
              'core_sample_note': 'Samples are taken at form-access boundaries; these are not time-weighted CPU residency percentages.'}
    if workers:
        result['median_worker_cpu_over_wall'] = statistics.median(
            worker['cpu_us'] / worker['wall_us'] for worker in workers if worker['wall_us'])
        result['median_worker_value_wait_us'] = statistics.median(worker['wait_us'] for worker in workers)
    stages = {'value_wait': ('started_us', 'values_ready_us'),
              'catchup': ('values_ready_us', 'tail_ready_us'),
              'challenge': ('tail_ready_us', 'b_ready_us'),
              'construction': ('b_ready_us', 'proof_done_us'),
              'verification': ('verify_started_us', 'verified_us'),
              'publication': ('verified_us', 'published_us')}
    targets = {}
    for iters in sorted(set(segment['iters'] for segment in segments if not segment['checkpoint'])):
        rows = [segment for segment in segments if segment['iters'] == iters and not segment['checkpoint']]
        targets[iters] = {'samples': len(rows), 'median_segment_length': statistics.median(s['length'] for s in rows),
                          'median_catchup_iters': statistics.median(s['catchup_iters'] for s in rows),
                          'median_us': {name: statistics.median(s[end] - s[start] for s in rows)
                                        for name, (start, end) in stages.items()}}
    result['targets'] = targets
    natural_resets = {(r['job'], r['engine']) for r in parse_records(raw, 'PROOF_REQUEST')
                      if r['iters'] == 0}
    completed = {(r['job'], r['engine'], r['iters']) for r in segments}
    reset_workers = [w for w in workers if (w['job'], w['engine']) in natural_resets]
    unfinished = [w for w in reset_workers if (w['job'], w['engine'], w['iters']) not in completed]
    total_cpu = sum(w['cpu_us'] for w in reset_workers)
    unfinished_cpu = sum(w['cpu_us'] for w in unfinished)
    result['reset_construction'] = {
        'natural_reset_jobs': len(natural_resets), 'worker_records': len(reset_workers),
        'unfinished_worker_records': len(unfinished), 'total_worker_cpu_us': total_cpu,
        'unfinished_worker_cpu_us': unfinished_cpu,
        'unfinished_cpu_fraction': unfinished_cpu / total_cpu if total_cpu else None,
        'note': 'Only jobs with a recorded natural reset request are included. Workers without a completed segment measure unfinished construction; completed but unused checkpoints and tail squaring are not counted. This is a lower bound on discarded CPU work.'}
    return result


def export_trace(raw):
    jobs = {}
    for record in parse_records(raw, 'PROOF_PROFILE'):
        job = jobs.setdefault(record['job'], {'job': record['job'], 'engine': record['engine'], 'requests': []})
        if record.get('stage') == 'state_ready':
            job['epoch_us'] = record.get('epoch_us')
        if record.get('stage') == 'stopping':
            job['stop_us'] = record['at_us']
    for record in parse_records(raw, 'PROOF_REQUEST'):
        job = jobs.setdefault(record['job'], {'job': record['job'], 'engine': record['engine'], 'requests': []})
        if record['iters']:
            job['requests'].append({'at_us': record['at_us'], 'iterations': record['iters']})
        else:
            job['reset_request_us'] = record['at_us']
    for record in parse_records(raw, 'PROOF_CHALLENGE'):
        job = jobs.setdefault(record['job'], {'job': record['job'], 'engine': record['engine'], 'requests': []})
        job['discriminant'] = record['discriminant']
        job['initial_form_hex'] = record['initial_form']
    complete = [job for job in jobs.values() if job.get('epoch_us') and 'reset_request_us' in job]
    complete.sort(key=lambda job: job['epoch_us'])
    origin = complete[0]['epoch_us'] if complete else 0
    for job in complete:
        job['start_us'] = job['epoch_us'] - origin
    return {'methodology': 'Client-observed mainnet request arrivals and natural reset requests, relative to challenge parsing. Final guard-induced cancellation is excluded. Chain mapping/network time before the client parses a challenge is not measured.',
            'complete_jobs': len(complete), 'jobs': complete}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('inputs', nargs='+', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--trace-output', type=Path)
    args = parser.parse_args()
    if args.trace_output and len(args.inputs) != 1:
        parser.error('--trace-output requires one continuous client log to keep job IDs unique')
    raw = ''
    per_input = []
    for path in args.inputs:
        if path.suffix == '.json':
            text = json.loads(path.read_text())['raw_log']
        else:
            text = path.read_text()
        raw += text + '\n'
        per_input.append({'input': str(path), **summarize(text)['reset_construction']})
    summary = summarize(raw)
    if len(per_input) > 1:
        # Job IDs restart in each process; do not match across independent logs.
        summary['reset_construction'] = {'per_input': per_input}
    args.output.write_text(json.dumps(summary, indent=2) + '\n')
    if args.trace_output:
        args.trace_output.write_text(json.dumps(export_trace(raw), indent=2) + '\n')


if __name__ == '__main__':
    main()
