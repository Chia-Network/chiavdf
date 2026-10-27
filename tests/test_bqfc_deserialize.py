import pytest

from chiavdf import bqfc_deserialize


B0_DISCRIMINANT = (
    "-146212091130374364448271598629912687111631974722846603227183769906935970876483871782840562162445571052154480"
    "975719448767769767557905129461524079902394315542354994269060181795718055043487735056120915916768273200138311"
    "940357886024014124174476991145983171370265799623472241486347111977874193600694306566545523111"
)

B0_CANONICAL_FORM = bytes.fromhex(
    "0300d8262c430e78e7c06cf60c9b2049968f604f3b506a85bfe4fff319f8176760"
    "e06cab8ab45524458bf558101f9b4ce8c23cc1e053263272b808b76c6f26493a11"
    "3b62ded5707b28d9eedc0503ac2efcd32be670726725be0fa7ea01f0ef3f602502"
    "01"
)


def test_bqfc_deserialize_returns_signed_big_endian_bytes():
    a, b = bqfc_deserialize(B0_DISCRIMINANT, B0_CANONICAL_FORM)

    assert isinstance(a, bytes)
    assert isinstance(b, bytes)
    assert a[0] == 0
    assert b[0] in (0, 1)


def invalid_forms():
    for value in (0x04, 0x08):
        data = bytearray(B0_CANONICAL_FORM)
        data[-1] ^= value
        yield B0_DISCRIMINANT, bytes(data)
    for first in (0x04, 0x08):
        for index in range(1, 100):
            data = bytearray(100)
            data[0] = first
            data[index] = 1
            yield "-23", bytes(data)
        for bit in range(8):
            data = bytearray(100)
            data[0] = first ^ (1 << bit)
            yield "-23", bytes(data)


@pytest.mark.parametrize("strict", [None, True, False])
@pytest.mark.parametrize(
    "discriminant, data",
    [pytest.param(d, data, id=f"case{index}") for index, (d, data) in enumerate(invalid_forms())],
)
def test_bqfc_deserialize_rejects_invalid_form(strict, discriminant, data):
    kwargs = {} if strict is None else {"strict": strict}
    with pytest.raises(RuntimeError):
        bqfc_deserialize(discriminant, data, **kwargs)
