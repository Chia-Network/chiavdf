"""Synthetic three-chain timelord protocol benchmark; does not join mainnet."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import re
import socket
import subprocess
import threading
import time

p = argparse.ArgumentParser()
p.add_argument('--threads', type=int, default=4)
p.add_argument('--client', default='./src/hw_vdf_client')
p.add_argument('--output', required=True)
p.add_argument('--scenario', choices=['balanced', 'staggered', 'irregular'], default='balanced')
p.add_argument('--stop-after', type=int, default=0,
               help='Stop each chain after this many deliveries, with later requests still queued')
p.add_argument('--verifier', default='./src/verify_delivery')
a = p.parse_args()
targets = [1000000, 2000000, 4000000, 8000000, 12000000, 16000000, 20000000]
chain_targets = [targets] * 3
challenge_delays = [0, 0, 0]
if a.scenario == 'staggered':
    chain_targets = [[4000000, 8000000, 12000000, 16000000, 20000000],
                     [1000000, 2000000, 4000000, 8000000, 12000000],
                     [1000000, 2000000, 4000000, 8000000, 12000000]]
    challenge_delays = [0, 3, 7]
elif a.scenario == 'irregular':
    chain_targets = [[100003, 250007, 1000123, 2000333, 4000777, 8000111, 20009999]] * 3
targets = sorted(set(n for chain in chain_targets for n in chain))
if not Path(a.verifier).is_file():
    p.error('Build the serialized-proof verifier before starting the benchmark')
expected_proofs = sum(map(len, chain_targets))
if a.stop_after:
    if a.stop_after < 1:
        p.error('--stop-after must be positive')
    expected_proofs = sum(min(a.stop_after, len(t)) for t in chain_targets)
discriminants = re.findall(r'"(-0x[0-9a-f]+)"', Path('src/hw/hw_test.cpp').read_text())[:3]
assert len(discriminants) == 3
barrier = threading.Barrier(3, timeout=20)
stop = threading.Event()
records = []
errors = []
out = Path(a.output)
out.parent.mkdir(parents=True, exist_ok=True)
log_path = out.with_suffix('.log')

def read_exact(conn, size):
    data = bytearray()
    while len(data) < size:
        chunk = conn.recv(size - len(data))
        if not chunk:
            raise RuntimeError('Client closed before requested proofs were delivered')
        data.extend(chunk)
    return bytes(data)

def chain(conn, idx):
    conn.settimeout(60)
    try:
        d = str(-int(discriminants[idx][1:], 16)).encode()
        form = bytes([8]) + bytes(99)
        barrier.wait()
        time.sleep(challenge_delays[idx])
        started = time.monotonic()
        conn.sendall(b'T' + f'{len(d):03d}'.encode() + d + bytes([len(form)]) + form)
        if read_exact(conn, 2) != b'OK':
            raise RuntimeError('Bad challenge handshake')
        requested = chain_targets[idx]
        delivery_count = min(a.stop_after, len(requested)) if a.stop_after else len(requested)
        requests = b''.join(f'{len(str(n)):02d}{n}'.encode() for n in requested)
        conn.sendall(requests)
        received = set()
        while len(received) < delivery_count:
            header = read_exact(conn, 4)
            if header == b'STOP':
                conn.sendall(b'ACK')
                raise RuntimeError('Client stopped before delivering all requested proofs')
            size = int.from_bytes(header, 'big')
            if size <= 0 or size > 1048576:
                raise RuntimeError(f'Unexpected proof message length {size}')
            proof = bytes.fromhex(read_exact(conn, size).decode())
            iterations = int.from_bytes(proof[:8], 'big', signed=True)
            if iterations not in requested or iterations in received:
                raise RuntimeError(f'Unexpected proof iterations {iterations}')
            elapsed = time.monotonic() - started
            received.add(iterations)
            records.append(dict(chain=idx, iterations=iterations, elapsed_seconds=elapsed,
                                estimated_ips=iterations/elapsed, proof_bytes=len(proof),
                                serialized_proof_hex=proof.hex()))
        # Stop globally while all sockets are still serviced. Closing a
        # synthetic server socket during reconnect can otherwise cause SIGPIPE.
        barrier.wait()
        if idx == 0:
            client.terminate()
        if read_exact(conn, 4) != b'STOP':
            raise RuntimeError('Missing STOP acknowledgement')
        conn.sendall(b'ACK')
    except Exception as exc:
        errors.append(dict(chain=idx, error=str(exc)))
        barrier.abort()
    finally:
        conn.close()

listener = socket.socket()
listener.bind(('127.0.0.1', 0))
listener.listen(3)
listener.settimeout(20)
port = listener.getsockname()[1]
client = None
restore = None
with log_path.open('w') as log:
    try:
        client = subprocess.Popen([a.client, '--device-loc', '12305',
            '--vdfs-mask', '7', '--freq', '1540', '--voltage', '1.0',
            '--vdf-threads', str(a.threads), '--proof-threads', str(a.threads-1),
            '--ip', '127.0.0.1', str(port), '3'], stdout=log, stderr=subprocess.STDOUT,
            env=dict(os.environ, CHIAVDF_HW_TELEMETRY='1'))
        def temperature_monitor():
            while not stop.wait(0.5):
                raw = log_path.read_text()
                temps = list(map(float, re.findall(r'ASIC Temp = ([\d.]+) C', raw)))
                if temps and max(temps) >= 55:
                    errors.append(dict(error='55 C temperature stop'))
                    client.terminate()
                    return
        monitor = threading.Thread(target=temperature_monitor, daemon=True)
        monitor.start()
        with ThreadPoolExecutor(max_workers=3) as pool:
            futures = []
            for idx in range(3):
                conn, _ = listener.accept()
                futures.append(pool.submit(chain, conn, idx))
            for future in futures:
                future.result()
    except Exception as exc:
        errors.append(dict(error=str(exc)))
        barrier.abort()
    finally:
        stop.set()
        listener.close()
        if client is not None and client.poll() is None:
            client.terminate()
            try:
                client.wait(timeout=15)
            except subprocess.TimeoutExpired:
                client.kill()
                client.wait()
        restore = subprocess.run(['./src/hw_test', '1', '1000000', '12305', '1100', '0.88'],
                                 capture_output=True, text=True, timeout=60)
raw = log_path.read_text()
if 'Restarting VDF' in raw or 'Proof NOT VALID' in raw:
    errors.append(dict(error='Hardware restart or invalid segment during benchmark'))
verification = subprocess.run([a.verifier], input=''.join(
    f"{discriminants[r['chain']]} {r['serialized_proof_hex']}\n" for r in records),
    capture_output=True, text=True, timeout=60)
serialized_verified = (bool(records) and verification.returncode == 0 and
                       verification.stdout.splitlines() == ['1'] * len(records))
if not serialized_verified:
    errors.append(dict(error='Complete serialized-proof verification or tamper rejection failed',
                       verifier_stdout=verification.stdout, verifier_stderr=verification.stderr))
temps = list(map(float, re.findall(r'ASIC Temp = ([\d.]+) C', raw)))
result = dict(machine='m6mini', device_location=12305, engines=3, frequency_mhz=1540,
              client_binary=a.client,
              voltage_requested_v=1.0, threads_per_engine=a.threads,
              proof_threads_per_engine=a.threads-1, targets=targets,
              scenario=a.scenario, chain_targets=chain_targets,
              challenge_delays_seconds=challenge_delays,
              stop_after=a.stop_after,
              methodology='Synthetic test discriminants and proof requests over loopback using the real hardware client. Estimated IPS is iterations divided by elapsed time from challenge send to receipt of the complete proof. Complete serialized proofs and tampered negative controls are independently checked after timing. This is not a mainnet metric or a 24-hour stability result.',
              serialized_proofs_verified=serialized_verified,
              records=sorted(records, key=lambda r:(r['chain'],r['iterations'])), errors=errors,
              max_temperature_c=max(temps) if temps else None,
              client_exit_status=client.returncode if client else None,
              restore_proof_verified=restore.returncode == 0 and 'Proof done for iters=1000000' in restore.stdout+restore.stderr,
              raw_log=raw, restore_log=restore.stdout+restore.stderr)
out.write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k not in ('raw_log','restore_log','methodology')}),flush=True)
raise SystemExit(0 if len(records)==expected_proofs and not errors and result['client_exit_status']==0 and result['restore_proof_verified'] else 1)
