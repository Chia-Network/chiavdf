"""prove_low_memory gives the same proof as prove, byte for byte, with memory that
does not grow with the number of iterations."""
import os
import secrets
import tempfile

import pytest

from chiavdf import create_discriminant, prove, prove_low_memory, verify_wesolowski

FORM_SIZE = 100
INITIAL = b"\x08" + (b"\x00" * 99)


@pytest.mark.parametrize("size", [512, 1024])
@pytest.mark.parametrize("iters", [1, 2, 7, 100, 1000, 30000, 65537, 150000])
def test_same_proof_as_prove(size, iters):
    challenge = secrets.token_bytes(10)
    expected = prove(challenge, INITIAL, size, iters, "")
    for interval in (0, 1, 160, 1000, 10**9):          # every iteration ... one checkpoint
        got = prove_low_memory(challenge, INITIAL, size, iters, "", interval)
        assert got == expected, (size, iters, interval)
    discriminant = create_discriminant(challenge, size)
    half = len(expected) // 2                          # y, then the proof
    assert verify_wesolowski(str(discriminant), INITIAL, expected[:half], expected[half:], iters)


def test_from_a_later_form_and_with_keyword_arguments():
    challenge, size = secrets.token_bytes(10), 1024
    y = prove(challenge, INITIAL, size, 5000, "")[:FORM_SIZE]
    expected = prove(challenge, y, size, 12345, "")
    assert prove_low_memory(challenge, y, size, 12345) == expected
    assert prove_low_memory(challenge_hash=challenge, x_s=y, discriminant_size_bits=size,
                            num_iterations=12345, checkpoint_interval=777) == expected


def test_checkpoint_interval_near_2_to_the_64():
    # Rounding the interval to a multiple of k*l must not wrap to 0.
    challenge = secrets.token_bytes(10)
    expected = prove(challenge, INITIAL, 1024, 3000, "")
    for interval in (2**64 - 1, 2**64 - 2, 2**63):
        assert prove_low_memory(challenge, INITIAL, 1024, 3000, "", interval) == expected


def test_stops_when_the_shutdown_file_is_gone():
    challenge = secrets.token_bytes(10)
    with tempfile.TemporaryDirectory() as tmp:
        missing = os.path.join(tmp, "not-there")
        assert prove_low_memory(challenge, INITIAL, 1024, 200000, missing) == b""
        present = os.path.join(tmp, "run")
        open(present, "w").close()
        assert len(prove_low_memory(challenge, INITIAL, 1024, 2000, present)) == 2 * FORM_SIZE
