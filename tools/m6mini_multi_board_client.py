"""Synthetic benchmark launcher: one client per ASIC, sequential USB discovery.

Thin executable wrappers call main(config_path, argv) with an experiment JSON.
The proof-delivery harness times and independently verifies serialized proofs.
This launcher restores all boards after clients have exited.
"""
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import threading
import time


def main(config_path, arguments):
    config = json.loads(Path(config_path).read_text())
    env = dict(os.environ, DYLD_LIBRARY_PATH=config['gmp_lib_dir'])
    for key in ('CHIAVDF_HW_SPI_MHZ', 'CHIAVDF_HW_ASYNC_TELEMETRY'):
        env.pop(key, None)
    env.update(config.get('environment', {}))
    binary = config['binary']
    if arguments == ['--version']:
        # System Python can strip DYLD variables before the wrapper starts.
        env['DYLD_PRINT_LIBRARIES'] = '1'
        os.execve(binary, [binary, '--version'], env)
    arguments = list(arguments)
    if len(arguments) < 2 or arguments[-1] != str(len(config['boards'])):
        raise ValueError('Harness engine count must match the configured board count')
    for flag in ('--device-loc', '--vdfs-mask'):
        if arguments.count(flag) != 1:
            raise ValueError('Expected one ' + flag + ' argument')
    stop = threading.Event()
    clients = []
    readers = []
    failure = None
    output_lock = threading.Lock()
    for sig in (signal.SIGTERM, signal.SIGINT):
        signal.signal(sig, lambda signum, frame: stop.set())

    def output(message):
        with output_lock:
            print(message, flush=True)

    def read_output(child, loc, ready):
        for line in child.stdout:
            output('BOARD_LOC=' + str(loc) + ' ' + line.rstrip('\n'))
            if 'Connected to timelord' in line:
                ready.set()

    try:
        for loc in config['boards']:
            argv = arguments[:]
            argv[argv.index('--device-loc') + 1] = str(loc)
            argv[argv.index('--vdfs-mask') + 1] = '1'
            argv[-1] = '1'
            ready = threading.Event()
            child = subprocess.Popen([binary, *argv], env=env, stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT, text=True, bufsize=1)
            clients.append((loc, child))
            reader = threading.Thread(target=read_output, args=(child, loc, ready))
            reader.start()
            readers.append(reader)
            deadline = time.monotonic() + 15
            while not ready.wait(.05):
                if stop.is_set() or child.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError('Client did not initialize and connect: ' + str(loc))
        while not stop.wait(.05):
            if any(child.poll() is not None for _, child in clients):
                raise RuntimeError('A hardware client exited before the harness stopped it')
    except Exception as exc:
        failure = str(exc)
        output('MULTIBOARD_ERROR ' + failure)
    finally:
        for loc, child in clients:
            if child.poll() is None:
                child.terminate()
        for loc, child in clients:
            try:
                child.wait(timeout=20)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()
                failure = 'Client did not drain: ' + str(loc)
            if child.returncode:
                failure = 'Client exit status ' + str(child.returncode) + ': ' + str(loc)
        for reader in readers:
            reader.join(timeout=5)
            if reader.is_alive():
                failure = 'Client output did not drain'
        restore_env = dict(os.environ, DYLD_LIBRARY_PATH=config['gmp_lib_dir'])
        restore_env.pop('CHIAVDF_HW_SPI_MHZ', None)
        restore_env.pop('CHIAVDF_HW_ASYNC_TELEMETRY', None)
        for loc in config['boards']:
            try:
                restored = subprocess.run([config['restore_tool'], '1', '1000000', str(loc), '1100', '0.88'],
                    env=restore_env, capture_output=True, text=True, timeout=60)
                raw = restored.stdout + restored.stderr
                verified = restored.returncode == 0 and 'Proof done for iters=1000000' in raw and 'Proof NOT VALID' not in raw
                output('MULTIBOARD_RESTORE ' + json.dumps(dict(board=loc, exit_status=restored.returncode,
                    proof_verified=verified, raw_log=raw)))
                if not verified:
                    failure = 'Restoration did not verify: ' + str(loc)
            except Exception as exc:
                failure = 'Restoration failed: ' + str(loc) + ': ' + str(exc)
                output('MULTIBOARD_ERROR ' + failure)
    return 1 if failure else 0


if __name__ == '__main__':
    if len(sys.argv) < 2:
        raise SystemExit('Usage: m6mini_multi_board_client.py CONFIG CLIENT_ARGUMENTS...')
    raise SystemExit(main(sys.argv[1], sys.argv[2:]))
