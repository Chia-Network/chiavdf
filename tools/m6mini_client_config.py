"""Configure benchmark clients after macOS has stripped launcher DYLD variables."""
import os
from pathlib import Path
import subprocess
import sys


def add_client_options(parser):
    parser.add_argument('--gmp-lib-dir', type=Path,
                        help='Native GMP directory; pass the client executable directly')
    parser.add_argument('--checkpoint-threshold', type=int)
    parser.add_argument('--retain-asic-values', action='store_true')
    parser.add_argument('--proof-profile', action='store_true')


def configure_client(args):
    environment = dict(os.environ)
    flags = []
    if args.checkpoint_threshold is not None:
        if not 8000 <= args.checkpoint_threshold <= 4294967295:
            raise ValueError('Checkpoint threshold must be 8000 through 4294967295')
        flags += ['--checkpoint-threshold', str(args.checkpoint_threshold)]
    if args.retain_asic_values:
        flags += ['--retain-asic-values']
    if args.proof_profile:
        flags += ['--proof-profile']
    library = None
    probe_environment = dict(environment)
    if args.gmp_lib_dir is not None:
        if sys.platform != 'darwin':
            raise ValueError('--gmp-lib-dir requires macOS')
        library = args.gmp_lib_dir.expanduser().resolve()
        for name in ('libgmp.10.dylib', 'libgmpxx.4.dylib'):
            if not (library / name).is_file():
                raise ValueError(f'Missing native GMP library: {library / name}')
        environment['DYLD_LIBRARY_PATH'] = str(library)
        probe_environment.update(environment, DYLD_PRINT_LIBRARIES='1')
    probe = subprocess.run([args.client, '--version'], env=probe_environment,
                           capture_output=True, text=True, timeout=10, check=True)
    if library is not None:
        for name in ('libgmp.10.dylib', 'libgmpxx.4.dylib'):
            if str(library / name) not in probe.stderr:
                raise ValueError('Native GMP load could not be verified; pass the client executable directly')
    if sys.platform == 'darwin':
        subprocess.Popen(['/usr/bin/caffeinate', '-is', '-w', str(os.getpid())],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    metadata = {'extra_flags':flags, 'gmp_lib_dir':str(library) if library else None,
                'native_gmp_loader_verified':library is not None,
                'version':probe.stdout.strip(),
                'loader_log':probe.stderr if library else ''}
    return environment, flags, metadata
