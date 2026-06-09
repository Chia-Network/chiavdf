__all__ = [
    "bqfc_deserialize",
    "create_discriminant",
    "create_discriminant_and_verify_n_wesolowski",
    "get_b_from_n_wesolowski",
    "prove",
    "verify_n_wesolowski",
    "verify_n_wesolowski_with_b",
    "verify_wesolowski",
]

def create_discriminant(challenge_hash: bytes, discriminant_size_bits: int) -> str: ...
def verify_wesolowski(
    discriminant: str,
    x_s: bytes | str,
    y_s: bytes | str,
    proof_s: bytes | str,
    num_iterations: int,
) -> bool: ...
def verify_n_wesolowski(
    discriminant: str,
    x_s: bytes | str,
    proof_blob: bytes,
    num_iterations: int,
    disc_size_bits: int,
    recursion: int,
) -> bool: ...
def create_discriminant_and_verify_n_wesolowski(
    challenge_hash: bytes,
    discriminant_size_bits: int,
    x_s: bytes | str,
    proof_blob: bytes,
    num_iterations: int,
    recursion: int,
) -> bool: ...
def prove(
    challenge_hash: bytes,
    x_s: bytes | str,
    discriminant_size_bits: int,
    num_iterations: int,
    shutdown_file_path: str,
) -> bytes: ...
def verify_n_wesolowski_with_b(
    discriminant: str,
    B: str,
    x_s: bytes | str,
    proof_blob: bytes,
    num_iterations: int,
    recursion: int,
) -> tuple[bool, bytes]: ...
def bqfc_deserialize(
    discriminant: str,
    data: bytes,
    strict: bool = True,
) -> tuple[bytes, bytes]: ...
def get_b_from_n_wesolowski(
    discriminant: str,
    x_s: bytes | str,
    proof_blob: bytes,
    num_iterations: int,
    recursion: int,
) -> str: ...
