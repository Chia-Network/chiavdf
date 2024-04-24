from chiavdf import create_discriminant
import secrets

seed = secrets.token_bytes(10)

first = create_discriminant(seed, 512)

print(first)

for _ in range(100):
    assert create_discriminant(seed, 512) == first
