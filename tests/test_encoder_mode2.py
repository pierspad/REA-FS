import random
import unittest

from tools.encoder_mode2 import decode_row


_H4 = (
    (1, -1, -1, -1),
    (-1, 1, -1, -1),
    (-1, -1, 1, -1),
    (-1, -1, -1, 1),
)


def oracle(encoded, baseline=None):
    """Independent separable 2D H4 transform reference."""
    output = bytearray(len(encoded))
    for start in range(0, len(encoded), 16):
        tile = [list(encoded[start + 4 * row:start + 4 * row + 4])
                for row in range(4)]
        # Transform columns, then rows; this is separable from the tensor sum.
        column_pass = [
            [sum(_H4[out][source] * tile[source][col] for source in range(4))
             for col in range(4)]
            for out in range(4)
        ]
        transformed = [
            [sum(_H4[out][source] * column_pass[row][source]
                 for source in range(4))
             for out in range(4)]
            for row in range(4)
        ]
        for row in range(4):
            for col in range(4):
                index = start + 4 * row + col
                total = transformed[row][col]
                if baseline is not None:
                    total -= 4 * (baseline[index] - 128)
                output[index] = min(255, max(0, (total >> 1) - 128))
    return bytes(output)


class EncoderMode2Tests(unittest.TestCase):
    def test_known_basis_vectors(self):
        for index in range(16):
            encoded = bytearray([128] * 16)
            encoded[index] = 144
            self.assertEqual(decode_row(bytes(encoded)), oracle(bytes(encoded)))

    def test_first_basis_has_known_unsaturated_coefficients(self):
        encoded = bytes([144] + [128] * 15)
        self.assertEqual(decode_row(encoded), bytes([
            136, 120, 120, 120, 120, 136, 136, 136,
            120, 136, 136, 136, 120, 136, 136, 136]))

    def test_constant_zero_128_and_255(self):
        self.assertEqual(decode_row(bytes(16)), bytes(16))
        self.assertEqual(decode_row(bytes([128]) * 16), bytes([128]) * 16)
        self.assertEqual(decode_row(bytes([255]) * 16), bytes([255]) * 16)

    def test_odd_sums_are_floored_before_bias_and_clipping(self):
        encoded = bytes([127] + [128] * 15)
        self.assertEqual(decode_row(encoded), bytes([
            127, 128, 128, 128, 128, 127, 127, 127,
            128, 127, 127, 127, 128, 127, 127, 127]))
        self.assertEqual(decode_row(bytes([1]) + bytes(15)), bytes(16))

    def test_baseline_is_applied_before_shift(self):
        encoded = bytes([128]) * 16
        baseline = bytes([129]) * 16
        expected = bytes([126]) * 16
        self.assertEqual(decode_row(encoded, baseline), expected)
        self.assertEqual(decode_row(encoded, bytes(16)), bytes([255]) * 16)
        self.assertEqual(decode_row(encoded, bytes([255]) * 16), bytes(16))

    def test_deterministic_random_tiles_match_separable_oracle(self):
        rng = random.Random(0x4D4F4445)
        for blocks in (1, 2, 7):
            encoded = bytes(rng.randrange(112, 145) for _ in range(16 * blocks))
            baseline = bytes(rng.randrange(112, 145) for _ in range(16 * blocks))
            self.assertEqual(decode_row(encoded), oracle(encoded))
            self.assertEqual(decode_row(encoded, baseline), oracle(encoded, baseline))

    def test_rejects_invalid_types_and_lengths(self):
        for invalid in (bytearray(16), "0" * 16, None):
            with self.subTest(invalid_type=type(invalid).__name__):
                with self.assertRaises(TypeError):
                    decode_row(invalid)
        for invalid in (b"", bytes(15), bytes(17)):
            with self.subTest(length=len(invalid)):
                with self.assertRaises(ValueError):
                    decode_row(invalid)
        with self.assertRaises(TypeError):
            decode_row(bytes(16), bytearray(16))
        with self.assertRaises(ValueError):
            decode_row(bytes(16), bytes(15))


if __name__ == "__main__":
    unittest.main()
