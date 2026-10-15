import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools import raw_frames


FRAME_SIZE = 17024
ROW_SIZE = 152
ROWS = 112


def make_frame(counter=0, *, bad_row=None, bad_header=False):
    rows = []
    for i in range(1, ROWS + 1):
        header = bytearray((1, 254, i, counter, 0xF0, 0xF7, 1, 0))
        if bad_row == i:
            header[2] = 113
        if bad_header and i == 1:
            header[4] = 0
        pixels = bytes([i % 256]) * 144
        rows.append(bytes(header) + pixels)
    return b"".join(rows)


class RawFramesTests(unittest.TestCase):
    def test_iter_frames_validates_and_yields_complete_frames(self):
        first, second = make_frame(255), make_frame(0)
        self.assertEqual(list(raw_frames.iter_frames(first + second)), [first, second])

    def test_iter_frames_rejects_empty_and_partial_tail(self):
        with self.assertRaises(ValueError):
            list(raw_frames.iter_frames(b""))
        with self.assertRaises(ValueError):
            list(raw_frames.iter_frames(make_frame() + b"x"))
        self.assertEqual(len(list(raw_frames.iter_frames(make_frame() + b"x", allow_partial_tail=True))), 1)

    def test_iter_frames_rejects_bad_headers_line_indices_and_frame_counter(self):
        for data in (make_frame(bad_header=True), make_frame(bad_row=37)):
            with self.subTest(data=data[:8]):
                with self.assertRaises(ValueError):
                    list(raw_frames.iter_frames(data))
        broken = bytearray(make_frame())
        broken[ROW_SIZE * 31 + 3] = 1
        with self.assertRaises(ValueError):
            list(raw_frames.iter_frames(bytes(broken)))

    def test_iter_frames_requires_counter_progression_modulo_256(self):
        with self.assertRaises(ValueError):
            list(raw_frames.iter_frames(make_frame(9) + make_frame(11)))

    def test_decode_frame_averages_adjacent_row_pairs_with_floor(self):
        data = bytearray(make_frame())
        for i, value in enumerate((2, 5, 8, 11)):
            start = i * ROW_SIZE + 8
            data[start:start + 144] = bytes([value]) * 144
        image = raw_frames.decode_frame(bytes(data))
        self.assertEqual(len(image), 56)
        self.assertTrue(all(len(row) == 144 for row in image))
        self.assertEqual(image[0], bytes([3]) * 144)
        self.assertEqual(image[1], bytes([9]) * 144)
        with self.assertRaises(ValueError):
            raw_frames.decode_frame(bytes(data[:-1]))

    def test_cli_writes_pgm_exclusively_with_private_permissions(self):
        with tempfile.TemporaryDirectory() as td:
            source, output = Path(td) / "capture.bin", Path(td) / "image.pgm"
            source.write_bytes(make_frame())
            result = raw_frames.main(["--input", str(source), "--output", str(output)])
            self.assertEqual(result, 0)
            contents = output.read_bytes()
            self.assertTrue(contents.startswith(b"P5\n144 56\n255\n"))
            self.assertEqual(len(contents.split(b"\n", 3)[3]), 144 * 56)
            self.assertEqual(os.stat(output).st_mode & 0o777, 0o600)
            with self.assertRaises(SystemExit):
                raw_frames.main(["--input", str(source), "--output", str(output)])

    def test_cli_refuses_symlink_output(self):
        with tempfile.TemporaryDirectory() as td:
            source, target, link = Path(td) / "capture.bin", Path(td) / "target", Path(td) / "link"
            source.write_bytes(make_frame())
            target.write_bytes(b"keep")
            link.symlink_to(target)
            with self.assertRaises(SystemExit):
                raw_frames.main(["--input", str(source), "--output", str(link)])
            self.assertEqual(target.read_bytes(), b"keep")

    def test_cli_reads_at_most_input_limit_plus_one(self):
        class Reader:
            def __init__(self):
                self.requested = []
            def __enter__(self): return self
            def __exit__(self, *args): pass
            def read(self, n=-1):
                self.requested.append(n)
                return b"x" * (raw_frames.MAX_INPUT_BYTES + 1)

        reader = Reader()
        with mock.patch("builtins.open", return_value=reader):
            with self.assertRaises(SystemExit):
                raw_frames.main(["--input", "synthetic", "--output", "unused.pgm"])
        self.assertEqual(reader.requested, [raw_frames.MAX_INPUT_BYTES + 1])


if __name__ == "__main__":
    unittest.main()
