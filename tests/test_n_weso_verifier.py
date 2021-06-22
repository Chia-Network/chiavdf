import secrets

from chiavdf import (
    create_discriminant,
    prove,
    verify_wesolowski,
    verify_n_wesolowski,
    verify_n_wesolowski_y_compressed,
    compress_y_from_n_wesolowski,
)


def prove_n_weso(discriminant_challenge, x, discriminant_size, form_size, iters, witness):
    iters_chunk = iters // (witness + 1)
    partials = []
    discriminant = create_discriminant(discriminant_challenge, discriminant_size)
    for _ in range(witness):
        result = prove(discriminant_challenge, x, discriminant_size, iters_chunk)
        y = result[:form_size]
        proof = result[form_size : 2 * form_size]
        partials.append((x, y, proof))
        x = y
    iters -= iters_chunk * witness
    result = prove(discriminant_challenge, x, discriminant_size, iters)
    y_result = result[:form_size]
    y_proof = result[form_size : 2 * form_size]
    assert verify_wesolowski(discriminant, x, y_result, y_proof, iters)
    b_hex = compress_y_from_n_wesolowski(discriminant, x, y_result + y_proof, iters, discriminant_size, 0)
    assert verify_n_wesolowski_y_compressed(discriminant, b_hex, x, y_proof, iters, discriminant_size, 0)
    inner_proof = b""
    for x, y, proof in reversed(partials):
        b_hex = compress_y_from_n_wesolowski(discriminant, x, y + proof, iters_chunk, discriminant_size, 0)
        b = int(b_hex, 16)
        assert verify_wesolowski(discriminant, x, y, proof, iters_chunk)
        assert verify_n_wesolowski_y_compressed(discriminant, b_hex, x, proof, iters_chunk, discriminant_size, 0)
        inner_proof += iters_chunk.to_bytes(8, byteorder='big')
        inner_proof += b.to_bytes(33, byteorder='big')
        inner_proof += proof
    return y_result, y_proof + inner_proof


def test_prove_n_weso_and_verify():
    discriminant_challenge = secrets.token_bytes(10)
    discriminant_size = 512
    discriminant = create_discriminant(discriminant_challenge, discriminant_size)
    form_size = 100
    initial_el = b"\x08" + (b"\x00" * 99)

    for iters in [1000000, 5000000, 10000000]:
        y, proof = prove_n_weso(discriminant_challenge, initial_el, discriminant_size, form_size, iters, 5)
        is_valid = verify_n_wesolowski(
            str(discriminant),
            initial_el,
            y + proof,
            iters,
            discriminant_size,
            5,
        )
        assert is_valid
        b_hex = compress_y_from_n_wesolowski(discriminant, initial_el, y + proof, iters, discriminant_size, 5)
        assert verify_n_wesolowski_y_compressed(discriminant, b_hex, initial_el, proof, iters, discriminant_size, 5)
        B = str(int(b_hex, 16))
        assert verify_n_wesolowski_y_compressed(discriminant, B, initial_el, proof, iters, discriminant_size, 5)
        B_wrong = str(int(b_hex, 16) + 1)
        assert not verify_n_wesolowski_y_compressed(
            discriminant,
            B_wrong,
            initial_el,
            proof,
            iters,
            discriminant_size,
            5,
        )
        initial_el = y


test_prove_n_weso_and_verify()
