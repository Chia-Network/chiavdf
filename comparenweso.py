import time

from chiavdf import verify_n_wesolowski, verify_wesolowski


class ClassGroup(tuple):
    @classmethod
    def identity_for_discriminant(class_, d):
        return class_.from_ab_discriminant(1, 1, d)

    @classmethod
    def from_ab_discriminant(class_, a, b, discriminant):
        if discriminant >= 0:
            raise ValueError("Positive discriminant.")
        if discriminant % 4 != 1:
            raise ValueError("Invalid discriminant mod 4.")
        if a == 0:
            raise ValueError("a can't be 0.")
        c = (b * b - discriminant) // (4 * a)
        p = class_((a, b, c)).reduced()
        if p.discriminant() != discriminant:
            raise ValueError("No classgroup element given the discriminant.")
        return p

    @classmethod
    def from_bytes(class_, bytearray, discriminant):
        int_size = (discriminant.bit_length() + 16) >> 4
        a = int.from_bytes(bytearray[0:int_size], "big", signed=True)
        b = int.from_bytes(bytearray[int_size:], "big", signed=True)
        return class_.from_ab_discriminant(a, b, discriminant)

    def __new__(cls, t):
        a, b, c = t
        return tuple.__new__(cls, (a, b, c))

    def __init__(self, t):
        super(ClassGroup, self).__init__()
        self._discriminant = None

    def __eq__(self, obj):
        return (
            isinstance(obj, ClassGroup)
            and obj[0] == self[0]
            and obj[1] == self[1]
            and obj[2] == self[2]
        )

    def identity(self):
        return self.identity_for_discriminant(self.discriminant())

    def discriminant(self):
        if self._discriminant is None:
            a, b, c = self
            self._discriminant = b * b - 4 * a * c
        return self._discriminant

    def reduced(self):
        a, b, c = self.normalized()
        while a > c or (a == c and b < 0):
            if c == 0:
                raise ValueError("Can't reduce the form.")
            s = (c + b) // (c + c)
            a, b, c = c, -b + 2 * s * c, c * s * s - b * s + a
        return self.__class__((a, b, c)).normalized()

    def normalized(self):
        a, b, c = self
        if -a < b <= a:
            return self
        r = (a - b) // (2 * a)
        b, c = b + 2 * r * a, a * r * r + b * r + c
        return self.__class__((a, b, c))

    def serialize(self):
        r = self.reduced()
        int_size_bits = int(self.discriminant().bit_length())
        int_size = (int_size_bits + 16) >> 4
        return b"".join(
            [x.to_bytes(int_size, "big", signed=True) for x in [r[0], r[1]]]
        )


def deserialize_proof(proof_blob, discriminant):
    int_size = (discriminant.bit_length() + 16) >> 4
    proof_arr = [
        proof_blob[_ : _ + 2 * int_size]
        for _ in range(0, len(proof_blob), 2 * int_size)
    ]
    return [ClassGroup.from_bytes(blob, discriminant) for blob in proof_arr]


def check_proof_of_time_nwesolowski(
    discriminant, x, proof_blob, iterations, int_size_bits, depth
):
    """
    Check the nested wesolowski proof. The proof blob
    includes the output of the VDF, along with the proof. The following
    table gives an example of the checks for a depth of 2.
    x   |  proof_blob
    ---------------------------------------------
    x   |  y3, proof3, y2, proof2, y1, proof1
    y1  |  y3, proof3, y2, proof2
    y2  |  y3, proof3
    """

    try:
        int_size = (int_size_bits + 16) >> 4
        if len(proof_blob) != 4 * int_size + depth * (8 + 4 * int_size):
            return False
        new_proof_blob = proof_blob[: 4 * int_size]
        iter_list = []
        for i in range(4 * int_size, len(proof_blob), 4 * int_size + 8):
            iter_list.append(int.from_bytes(proof_blob[i : (i + 8)], byteorder="big"))
            new_proof_blob = (
                new_proof_blob + proof_blob[(i + 8) : (i + 8 + 4 * int_size)]
            )
        proof_blob = new_proof_blob

        result_bytes = proof_blob[: (2 * int_size)]
        proof_bytes = proof_blob[(2 * int_size) :]
        y = ClassGroup.from_bytes(result_bytes, discriminant)

        proof = deserialize_proof(proof_bytes, discriminant)
        if depth * 2 + 1 != len(proof):
            return False

        for _ in range(depth):
            iterations_1 = iter_list[-1]
            if not verify_wesolowski(
                str(discriminant),
                str(x[0]),
                str(x[1]),
                str(proof[-2][0]),
                str(proof[-2][1]),
                str(proof[-1][0]),
                str(proof[-1][1]),
                iterations_1,
            ):
                return False
            x = proof[-2]
            iterations = iterations - iterations_1
            proof = proof[:-2]
            iter_list = iter_list[:-1]

        return verify_wesolowski(
            str(discriminant),
            str(x[0]),
            str(x[1]),
            str(y[0]),
            str(y[1]),
            str(proof[-1][0]),
            str(proof[-1][1]),
            iterations,
        )
    except Exception:
        return False


def oldie(disc, a, b, proof, iter, sb, witness, doold):
    if z == 0:
        x = ClassGroup.from_ab_discriminant(int(a), int(b), int(disc))

        return check_proof_of_time_nwesolowski(
            int(disc),
            x,
            proof,
            iter,
            sb,
            witness,
        )

    return verify_n_wesolowski(
        disc,
        a,
        b,
        proof,
        iter,
        sb,
        witness,
    )


for z in range(2):
    iters = 1000
    t1 = time.time()
    for i in range(iters):
        is_valid = oldie(
            str(
                -131653324254138636653163861414331698305531090221496467927360326686715180966094250598321899621249972220387687148397451395672779897144571112116763666653213748473909547482437246405018707472153290116227072825447643324530509016778432769802300913461285128339119844239772697652504835780459732685000796733645621728639
            ),
            str(2),
            str(1),
            bytes.fromhex(
                "003f360be667de706fe886f766fe20240de04fe2c2f91207f1bbdddf20c554ab8d168b2ce9664d75f4613375a0ab12bf8158983574c9f5cd61c6b8a905fd3fa6bbffc5401b4ccedbe093b560293263a226e46302e720726586251116bc689ef09dc70d99e0a090c4409f928e218e85032fdbee02fedd563073be555b75a70a2d6a430033bc7a4926e3504e87698a0ace0dee6364cced2e9142b4e4cbe55a6371aab41e501ceed21d79d3a0dbbd82ce913c5de40b13eb7c59b1b52b6ef270ee603bd5e7fffcc9f5fae6dbd5aeec394181af130c0fdd195b22be745449b7a584ac80fc75ed49acfdb4d650f5cd344f86377ebbbaef5b19a0af3ae08101d1697f5656a52193000000000071c6f40024c342868a0c2a201b1b26a5d52c5d2f92a106c19ff926deb3fba1e74a444ecee3f8f507c062b949a2eaadd442b049417f82e8811526fa83c6d099d75323e068ffeca9dcd163761000c65d21dede72787ac350f25bdd3d29db6e9cb0e22c8124c724db33660c88784e2871b62ecf816846db7b469c71cad9a5dcfc5548ed2dd781006fa15b968facf4d79219646267eb187a670306d1ff1a59fc28ae00d36bb5a1cba659f48aa64a9022711a66105ef14401ff3948add265240aaad329ee76ba4c2300496746b86bcccacff5947c3fcb956cde2cffae10435960d7097f989aac742cf1047887f11584d20297958385e1715fe0f9b69141750c20d8134420eafec68fd10000000001555540006958aabfe4cc5d870e61fef82bcf1f2c3859e2bd8b1177e8a8872376b5cabace5dcb59b6fecada7e522d05f6f0e352939a6bfdf8c454fbe822cfa5ce97d17be0ffde44a4812cde9d04ec5c08dce6f9146586fdc8e081e05ec690b7effe24ea756f3d300f361203b61e1a39220c6eafa7852842674e317dcae5549c78c7144296ff004a6d0d2854c55e4c1de2f17dc4480b81652cfec37124ef41560a28c853482732434d1c006763b2e341528ae0bcc29fb76f1a4dafd99ade4fd75ec9cc9ca3f3d7001bcb6eb71e43eb22169ab721637551a8ec93838eb0825e9ecba9175297a00b146e9fdd244c5b722f29d3c46ec38840ba18f1f06ddec3dea844867386c2e1ac95"
            ),
            33554432,
            1024,
            2,
            z,
        )
        assert is_valid

    t2 = time.time()
    print(f"{z} IPS: {iters / (t2 - t1)}")
