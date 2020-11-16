from chiavdf import prove, verify_wesolowski, create_discriminant
import secrets
import time


def test_prove_and_verify():
    discriminant_challenge = secrets.token_bytes(10)
    discriminant_size = 512
    discriminant = create_discriminant(discriminant_challenge, discriminant_size)
    int_size = (discriminant_size + 16) >> 4

    iters = 1000000
    t1 = time.time()
    result = prove(discriminant_challenge, str(2), str(1), discriminant_size, iters)
    t2 = time.time()
    print(f"IPS: {iters / (t2 - t1)}")
    result_y_a = int.from_bytes(
        result[0:int_size],
        "big",
        signed=True,
    )
    result_y_b = int.from_bytes(
        result[int_size : 2 * int_size],
        "big",
        signed=True,
    )
    proof_a = int.from_bytes(
        result[2 * int_size : 3 * int_size],
        "big",
        signed=True,
    )
    proof_b = int.from_bytes(
        result[3 * int_size : 4 * int_size],
        "big",
        signed=True,
    )

    is_valid = verify_wesolowski(
        str(discriminant),
        str(2),
        str(1),
        str(result_y_a),
        str(result_y_b),
        str(proof_a),
        str(proof_b),
        iters,
    )
    assert is_valid

    # Creates another proof starting at the previous output
    iters_2 = 200000
    t1 = time.time()
    result_2 = prove(
        discriminant_challenge,
        str(result_y_a),
        str(result_y_b),
        discriminant_size,
        iters_2,
    )
    t2 = time.time()
    print(f"IPS: {iters_2 / (t2 - t1)}")

    is_valid = verify_wesolowski(
        str(discriminant),
        str(result_y_a),
        str(result_y_b),
        str(
            int.from_bytes(
                result_2[0:int_size],
                "big",
                signed=True,
            )
        ),
        str(
            int.from_bytes(
                result_2[int_size : 2 * int_size],
                "big",
                signed=True,
            )
        ),
        str(
            int.from_bytes(
                result_2[2 * int_size : 3 * int_size],
                "big",
                signed=True,
            )
        ),
        str(
            int.from_bytes(
                result_2[3 * int_size : 4 * int_size],
                "big",
                signed=True,
            )
        ),
        iters_2,
    )
    assert is_valid
