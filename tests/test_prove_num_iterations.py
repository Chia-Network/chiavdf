import subprocess
import sys

import pytest

from chiavdf import prove

CHALLENGE = b"\x00\x00\x01\x02\x03\x03\x04\x04"
X = b"\x08" + b"\x00" * 99
DISCRIMINANT_SIZE = 1024

PROBE = """
import sys
from chiavdf import prove

CHALLENGE = b"\\x00\\x00\\x01\\x02\\x03\\x03\\x04\\x04"
X = b"\\x08" + b"\\x00" * 99

try:
    prove(CHALLENGE, X, 1024, int(sys.argv[1]), "")
except (MemoryError, ValueError, OverflowError):
    print("rejected")
"""


@pytest.mark.parametrize(
    "num_iterations",
    [
        2**64 - 1,
        2**64 - 359,
        2**64 - 360,
        2**63,
        2**62 + 1,
    ],
)
def test_prove_large_num_iterations_does_not_terminate_abnormally(num_iterations):
    result = subprocess.run(
        [sys.executable, "-c", PROBE, str(num_iterations)],
        capture_output=True,
        text=True,
        timeout=300,
        check=False,
    )
    assert result.returncode == 0, (
        f"prove({num_iterations}) returned {result.returncode}: {result.stderr[-500:]}"
    )


def test_prove_accepts_small_num_iterations():
    result = prove(CHALLENGE, X, DISCRIMINANT_SIZE, 100, "")
    assert isinstance(result, bytes)
    assert len(result) > 0
