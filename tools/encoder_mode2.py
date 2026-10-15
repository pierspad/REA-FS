"""Decode complete 16-byte groups for the observed mode-2 encoder."""


def decode_row(encoded: bytes, baseline: bytes | None = None) -> bytes:
    """Decode encoded bytes to grayscale pixels using the mode-2 transform.

    Each group maps to 16 consecutive row samples. The 4-by-4 indexing is
    algebraic, not a spatial image tile. A baseline, when
    supplied, is an OEM-style offset-code row, not an ordinary blank image;
    its correction is applied in the transform's pre-shift domain. This
    function does not generate or validate a device calibration.
    """
    if not isinstance(encoded, bytes):
        raise TypeError("encoded must be bytes")
    if not encoded or len(encoded) % 16:
        raise ValueError("encoded must be nonempty and a multiple of 16 bytes")
    if baseline is not None:
        if not isinstance(baseline, bytes):
            raise TypeError("baseline must be bytes or None")
        if len(baseline) != len(encoded):
            raise ValueError("baseline must have the same length as encoded")

    pixels = bytearray(len(encoded))
    for block_start in range(0, len(encoded), 16):
        for a in range(4):
            for b in range(4):
                total = 0
                for c in range(4):
                    row_sign = 1 if a == c else -1
                    for d in range(4):
                        index = block_start + 4 * c + d
                        coefficient = row_sign * (1 if b == d else -1)
                        total += coefficient * encoded[index]
                if baseline is not None:
                    total -= 4 * (baseline[block_start + 4 * a + b] - 128)
                value = (total >> 1) - 128
                pixels[block_start + 4 * a + b] = min(255, max(0, value))
    return bytes(pixels)
