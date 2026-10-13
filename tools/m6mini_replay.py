"""Replay captured mainnet challenge inputs, request arrivals and natural resets.

Run on the prepared M6 checkout with exclusive access to the selected ASIC.
Logical streams preserve their recorded overlap; reconnecting physical engines
are assigned queued jobs in accept order, as the protocol has no engine ID.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import select
import socket
import subprocess
import threading
import time
from m6mini_client_config import add_client_options, configure_client


def read_exact(conn, size):
    data = bytearray()
    while len(data) < size:
        chunk = conn.recv(size - len(data))
        if not chunk:
            raise RuntimeError('Unexpected EOF')
        data.extend(chunk)
    return bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--trace', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--client', required=True)
    parser.add_argument('--verifier', default='./src/verify_delivery')
    parser.add_argument('--device-loc', type=int, default=12305)
    parser.add_argument('--first-job', type=int, default=1)
    parser.add_argument('--jobs', type=int, default=6)
    add_client_options(parser)
    args = parser.parse_args()
    trace = json.loads(args.trace.read_text())
    jobs = [j for j in trace['jobs'] if j['job'] >= args.first_job][:args.jobs]
    if not jobs or len(jobs) != args.jobs:
        parser.error('Requested jobs are not present in the trace')
    origin = min(j['start_us'] for j in jobs)
    for j in jobs:
        j['replay_start_us'] = j['start_us'] - origin
        if len(j['initial_form_hex']) != 200 or int(j['discriminant'], 0) >= 0:
            parser.error('Invalid captured challenge')
    try:
        client_env, client_flags, client_configuration = configure_client(args)
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        parser.error(str(error))
    started_utc = datetime.now(timezone.utc).isoformat()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    log_path = args.output.with_suffix('.log')
    records, errors, starts = [], [], []
    halt = threading.Event()
    shutting_down = threading.Event()
    listener = socket.socket()
    listener.bind(('127.0.0.1', 0)); listener.listen(3); listener.settimeout(.1)
    epoch = time.monotonic() + 1
    client = None

    def service(conn, job):
        conn.settimeout(10)
        try:
            if job is None:
                if read_exact(conn, 4) != b'STOP':
                    raise RuntimeError('Unexpected message on idle connection')
                conn.sendall(b'ACK')
                return
            scheduled = epoch + job['replay_start_us'] / 1e6
            if halt.wait(max(0, scheduled-time.monotonic())):
                return
            d = str(int(job['discriminant'], 0)).encode()
            initial = bytes.fromhex(job['initial_form_hex'])
            started = time.monotonic()
            conn.sendall(b'T' + f'{len(d):03d}'.encode() + d + bytes([100]) + initial)
            if read_exact(conn, 2) != b'OK':
                raise RuntimeError('Bad challenge handshake')
            starts.append({'job':job['job'], 'start_lateness_us':(started-scheduled)*1e6,
                           'handshake_us':(time.monotonic()-started)*1e6})
            requests = job['requests']; cursor = 0; requested = set(); received = set()
            pending = bytearray(); reset = False
            deadline = scheduled + job['reset_request_us']/1e6
            while not halt.is_set():
                now = time.monotonic()
                while not reset and cursor < len(requests) and now >= scheduled + requests[cursor]['at_us']/1e6:
                    iters = requests[cursor]['iterations']
                    conn.sendall(f'{len(str(iters)):02d}{iters}'.encode())
                    requested.add(iters); cursor += 1
                if not reset and now >= deadline:
                    conn.sendall(b'010'); reset = True
                if now > deadline + 10:
                    raise RuntimeError('Client failed to acknowledge natural reset')
                next_time = deadline if not reset else now+.05
                if not reset and cursor < len(requests):
                    next_time = min(next_time, scheduled + requests[cursor]['at_us']/1e6)
                ready, _, _ = select.select([conn], [], [], max(0,min(.05,next_time-now)))
                if not ready:
                    continue
                chunk = conn.recv(65536)
                if not chunk:
                    raise RuntimeError('Client closed before STOP')
                pending.extend(chunk)
                while len(pending) >= 4:
                    if pending[:4] == b'STOP':
                        if not reset:
                            raise RuntimeError('Unexpected early reset')
                        conn.sendall(b'ACK')
                        return
                    size = int.from_bytes(pending[:4], 'big')
                    if size < 434 or size > 1048576:
                        raise RuntimeError('Invalid proof message size')
                    if len(pending) < 4+size:
                        break
                    proof = bytes.fromhex(pending[4:4+size].decode()); del pending[:4+size]
                    iters = int.from_bytes(proof[:8], 'big')
                    if iters not in requested or iters in received:
                        raise RuntimeError('Unexpected or duplicate proof')
                    received.add(iters); elapsed = time.monotonic()-started
                    records.append({'job':job['job'],'logical_engine':job['engine'],
                                    'iterations':iters,'elapsed_seconds':elapsed,
                                    'estimated_ips':iters/elapsed,'before_reset':not reset,
                                    'witness_depth':proof[116]+1,'discriminant':job['discriminant'],
                                    'initial_form_hex':job['initial_form_hex'],
                                    'serialized_proof_hex':proof.hex()})
        except Exception as error:
            if job is None and shutting_down.is_set() and str(error) == 'Unexpected EOF':
                return
            errors.append({'job':job['job'] if job else None,'error':str(error)})
            halt.set()
            if client is not None and client.poll() is None:
                client.terminate()
        finally:
            conn.close()

    restore = None
    with log_path.open('w') as log:
        try:
            client = subprocess.Popen([args.client,*client_flags,'--device-loc',str(args.device_loc),
                '--vdfs-mask','7','--freq','1540','--voltage','1.0','--vdf-threads','4',
                '--proof-threads','3','--ip','127.0.0.1',str(listener.getsockname()[1]),'3'],
                stdout=log,stderr=subprocess.STDOUT,env=dict(client_env,CHIAVDF_HW_TELEMETRY='1'))
            def temperature_monitor():
                while not halt.wait(.5):
                    temps = re.findall(r'ASIC Temp = ([\d.]+) C',log_path.read_text())
                    if temps and max(map(float,temps)) >= 55:
                        errors.append({'error':'55 C temperature stop'}); halt.set(); client.terminate()
            monitor = threading.Thread(target=temperature_monitor,daemon=True); monitor.start()
            futures = []; idle = []
            end = epoch + max(j['replay_start_us']+j['reset_request_us'] for j in jobs)/1e6+15
            with ThreadPoolExecutor(max_workers=6) as pool:
                cursor = 0
                while not halt.is_set() and (cursor < len(jobs) or not all(f.done() for f in futures)):
                    if time.monotonic() > end:
                        raise RuntimeError('Replay timed out')
                    try:conn,_ = listener.accept()
                    except socket.timeout:continue
                    if cursor < len(jobs):
                        futures.append(pool.submit(service,conn,jobs[cursor]));cursor+=1
                    else:idle.append(pool.submit(service,conn,None))
                shutting_down.set();client.terminate()
                # Drain any reconnects initiated while the last jobs were stopping.
                while client.poll() is None and time.monotonic()<end:
                    try:conn,_ = listener.accept();idle.append(pool.submit(service,conn,None))
                    except socket.timeout:pass
                if client.poll() is None:
                    raise RuntimeError('Client shutdown timed out')
                for f in futures+idle:f.result()
        except Exception as error:
            errors.append({'error':str(error)})
        finally:
            shutting_down.set();halt.set();listener.close()
            if client is not None and client.poll() is None:
                client.terminate()
                try:client.wait(timeout=15)
                except subprocess.TimeoutExpired:client.kill();client.wait()
            restore = subprocess.run(['./src/hw_test','1','1000000',str(args.device_loc),'1100','.88'],
                                     capture_output=True,text=True,timeout=60,env=client_env)
    raw = log_path.read_text()
    if 'Restarting VDF' in raw or 'Proof NOT VALID' in raw:
        errors.append({'error':'Hardware restart or invalid segment'})
    verification = subprocess.run([args.verifier],input=''.join(
        f"{r['discriminant']} {r['serialized_proof_hex']} {r['initial_form_hex']}\n" for r in records),
        capture_output=True,text=True,timeout=120,env=client_env)
    verified = bool(records) and verification.returncode==0 and verification.stdout.splitlines()==['1']*len(records)
    if not verified:errors.append({'error':'Independent wire verification/tamper rejection failed'})
    temps = list(map(float,re.findall(r'ASIC Temp = ([\d.]+) C',raw)))
    result={'client':args.client,'trace':str(args.trace),'jobs':[j['job'] for j in jobs],
            'client_configuration':client_configuration,
            'started_utc':started_utc,'finished_utc':datetime.now(timezone.utc).isoformat(),
            'methodology':__doc__+' Timing is from challenge send, not original timelord chain mapping. Future targets cancelled by recorded resets are expected.',
            'records':sorted(records,key=lambda r:(r['job'],r['iterations'])),'starts':starts,
            'serialized_proofs_verified':verified,'verification_log':verification.stderr,
            'client_exit_status':client.returncode,'max_temperature_c':max(temps) if temps else None,
            'restore_proof_verified':restore.returncode==0 and 'Proof done for iters=1000000' in restore.stdout+restore.stderr,
            'raw_log':raw,'restore_log':restore.stdout+restore.stderr,'errors':errors}
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:result[k] for k in ('jobs','serialized_proofs_verified','client_exit_status','max_temperature_c','restore_proof_verified','errors')}),flush=True)
    raise SystemExit(0 if not errors and verified and result['client_exit_status']==0 and result['restore_proof_verified'] else 1)


if __name__=='__main__':main()
