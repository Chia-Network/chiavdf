import secrets
import pytest

from chiavdf import create_discriminant


def test_discriminant_size_bits_negative():
    """Test that discriminant_size_bits of -1 fails"""
    discriminant_challenge = secrets.token_bytes(10)
    with pytest.raises(ValueError, match="discriminant_size_bits must be positive"):
        create_discriminant(discriminant_challenge, -1)


def test_discriminant_size_bits_zero():
    """Test that discriminant_size_bits of 0 fails"""
    discriminant_challenge = secrets.token_bytes(10)
    with pytest.raises(ValueError, match="discriminant_size_bits must be positive"):
        create_discriminant(discriminant_challenge, 0)


def test_discriminant_size_bits_too_large():
    """Test that discriminant_size_bits of 16400 fails (exceeds max of 16384)"""
    discriminant_challenge = secrets.token_bytes(10)
    with pytest.raises(ValueError, match="discriminant_size_bits exceeds maximum allowed value"):
        create_discriminant(discriminant_challenge, 16400)


def test_discriminant_size_bits_valid():
    """Test that discriminant_size_bits of 1024 succeeds"""
    discriminant_challenge = secrets.token_bytes(10)
    discriminant = create_discriminant(discriminant_challenge, 1024)
    # If we get here without an exception, the test passes
    assert discriminant is not None
    assert len(discriminant) > 0  # Should return a string representation of the discriminant
