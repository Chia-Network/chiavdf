import secrets

from chiavdf import (
    create_discriminant,
    prove,
    verify_wesolowski,
    verify_n_wesolowski,
    verify_n_wesolowski_with_b,
    get_b_from_n_wesolowski,
)


def prove_n_weso(discriminant_challenge, x, discriminant_size, form_size, iters, witness, wrong_segm):
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
    b_hex = get_b_from_n_wesolowski(discriminant, x, y_result + y_proof, iters, 0)
    is_valid, y_from_compression = verify_n_wesolowski_with_b(
        discriminant,
        b_hex,
        x,
        y_proof,
        iters,
        0,
    )
    assert is_valid
    assert y_from_compression == y_result
    inner_proof = b""
    for x, y, proof in reversed(partials):
        b_hex = get_b_from_n_wesolowski(discriminant, x, y + proof, iters_chunk, 0)
        b = int(b_hex, 16)
        assert verify_wesolowski(discriminant, x, y, proof, iters_chunk)
        is_valid, y_from_compression = verify_n_wesolowski_with_b(
            discriminant,
            b_hex,
            x,
            proof,
            iters_chunk,
            0,
        )
        assert is_valid
        assert y == y_from_compression
        if not wrong_segm:
            inner_proof += iters_chunk.to_bytes(8, byteorder='big')
        else:
            iters_wrong = iters_chunk + 1
            inner_proof += iters_wrong.to_bytes(8, byteorder='big')
            wrong_segm = False
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
        y, proof = prove_n_weso(discriminant_challenge, initial_el, discriminant_size, form_size, iters, 5, False)
        is_valid = verify_n_wesolowski(
            str(discriminant),
            initial_el,
            y + proof,
            iters,
            discriminant_size,
            5,
        )
        assert is_valid
        is_valid = verify_n_wesolowski(
            str(discriminant),
            initial_el,
            y + proof,
            iters + 1,
            discriminant_size,
            5,
        )
        assert not is_valid
        y, proof_wrong = prove_n_weso(discriminant_challenge, initial_el, discriminant_size, form_size, iters, 10, True)
        is_valid = verify_n_wesolowski(
            str(discriminant),
            initial_el,
            y + proof_wrong,
            iters,
            discriminant_size,
            10,
        )
        assert not is_valid
        b_hex = get_b_from_n_wesolowski(discriminant, initial_el, y + proof, iters, 5)
        is_valid, y_from_compression = verify_n_wesolowski_with_b(
            discriminant,
            b_hex,
            initial_el,
            proof,
            iters,
            5,
        )
        assert is_valid
        assert y_from_compression == y
        B = str(int(b_hex, 16))
        is_valid, y_from_compression = verify_n_wesolowski_with_b(
            discriminant,
            B,
            initial_el,
            proof,
            iters,
            5,
        )
        assert is_valid
        assert y_from_compression == y
        B_wrong = str(int(b_hex, 16) + 1)
        is_valid, y_from_compression = verify_n_wesolowski_with_b(
            discriminant,
            B_wrong,
            initial_el,
            proof,
            iters,
            5,
        )
        assert not is_valid
        assert y_from_compression == b""
        is_valid, y_from_compression = verify_n_wesolowski_with_b(
            discriminant,
            B,
            initial_el,
            proof_wrong,
            iters,
            10,
        )
        assert not is_valid
        assert y_from_compression == b""
        initial_el = y


test_prove_n_weso_and_verify()
