import secrets

import pytest

from chiavdf import (
    create_discriminant,
    create_discriminant_and_verify_n_wesolowski,
    get_b_from_n_wesolowski,
    prove,
    verify_n_wesolowski,
    verify_n_wesolowski_with_b,
)

DISCRIMINANT_SIZE = 512
FORM_SIZE = 100
ITERS = 100000
INITIAL_EL = b"\x08" + (b"\x00" * 99)
# Lengths other than 100 must be rejected before the verifier reads x_s.
BAD_X_S = [b"", b"\x08", INITIAL_EL[:-1], INITIAL_EL + b"\x00", INITIAL_EL * 2]


@pytest.fixture(scope="module")
def vdf_proof():
    challenge = secrets.token_bytes(10)
    discriminant = create_discriminant(challenge, DISCRIMINANT_SIZE)
    result = prove(challenge, INITIAL_EL, DISCRIMINANT_SIZE, ITERS, "")
    return challenge, discriminant, result[: 2 * FORM_SIZE]


def test_valid_x_s_still_verifies(vdf_proof):
    challenge, discriminant, blob = vdf_proof
    assert verify_n_wesolowski(discriminant, INITIAL_EL, blob, ITERS, DISCRIMINANT_SIZE, 0)
    assert create_discriminant_and_verify_n_wesolowski(challenge, DISCRIMINANT_SIZE, INITIAL_EL, blob, ITERS, 0)


@pytest.mark.parametrize("x_s", BAD_X_S)
def test_verify_n_wesolowski_rejects_bad_x_s_length(vdf_proof, x_s):
    _, discriminant, blob = vdf_proof
    assert verify_n_wesolowski(discriminant, x_s, blob, ITERS, DISCRIMINANT_SIZE, 0) is False


@pytest.mark.parametrize("x_s", BAD_X_S)
def test_create_discriminant_and_verify_n_wesolowski_rejects_bad_x_s_length(vdf_proof, x_s):
    challenge, _, blob = vdf_proof
    assert create_discriminant_and_verify_n_wesolowski(challenge, DISCRIMINANT_SIZE, x_s, blob, ITERS, 0) is False


@pytest.mark.parametrize("x_s", BAD_X_S)
def test_verify_n_wesolowski_with_b_rejects_bad_x_s_length(vdf_proof, x_s):
    _, discriminant, blob = vdf_proof
    b_hex = get_b_from_n_wesolowski(discriminant, INITIAL_EL, blob, ITERS, 0)
    assert verify_n_wesolowski_with_b(discriminant, b_hex, x_s, blob[FORM_SIZE:], ITERS, 0) == (False, b"")


@pytest.mark.parametrize("x_s", BAD_X_S)
def test_get_b_from_n_wesolowski_rejects_bad_x_s_length(vdf_proof, x_s):
    _, discriminant, blob = vdf_proof
    with pytest.raises(RuntimeError):
        get_b_from_n_wesolowski(discriminant, x_s, blob, ITERS, 0)
