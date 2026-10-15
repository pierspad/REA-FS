"""Decode the observed fixed-row USB byte layout into a grayscale PGM.

This module implements only the locally observed framing contract. It does not
apply calibration, baseline subtraction, denoising, or other image processing.
"""

import argparse
import os
import sys
from typing import Iterator

ROW_BYTES = 152
HEADER_BYTES = 8
PIXEL_BYTES = 144
ROWS_PER_FRAME = 112
OUTPUT_ROWS = 56
FRAME_BYTES = ROW_BYTES * ROWS_PER_FRAME
MAX_INPUT_BYTES = 64 * 1024 * 1024
_ROW_PREFIX = b"\x01\xfe"
_ROW_SUFFIX = b"\xf0\xf7\x01\x00"


def _validate_frame(frame: bytes) -> int:
    if len(frame) != FRAME_BYTES:
        raise ValueError(f"frame must be exactly {FRAME_BYTES} bytes")
    counter = None
    for row_index in range(ROWS_PER_FRAME):
        offset = row_index * ROW_BYTES
        header = frame[offset:offset + HEADER_BYTES]
        expected_index = row_index + 1
        if header[:2] != _ROW_PREFIX:
            raise ValueError(f"invalid row header at row {expected_index}")
        if header[2] != expected_index:
            raise ValueError(f"invalid row index at row {expected_index}")
        if header[4:8] != _ROW_SUFFIX:
            raise ValueError(f"invalid row trailer at row {expected_index}")
        if counter is None:
            counter = header[3]
        elif header[3] != counter:
            raise ValueError(f"frame counter changes at row {expected_index}")
    return counter


def iter_frames(data: bytes, allow_partial_tail: bool = False) -> Iterator[bytes]:
    """Yield validated complete frames; reject empty, malformed, or partial data."""
    if not isinstance(data, bytes):
        raise TypeError("data must be bytes")
    if not data:
        raise ValueError("input is empty")
    if len(data) > MAX_INPUT_BYTES:
        raise ValueError(f"input exceeds {MAX_INPUT_BYTES} byte limit")
    complete_bytes, tail_bytes = divmod(len(data), FRAME_BYTES)
    if tail_bytes and not allow_partial_tail:
        raise ValueError("input ends with a partial frame")
    if complete_bytes == 0:
        raise ValueError("input contains no complete frame")

    previous_counter = None
    for index in range(complete_bytes):
        start = index * FRAME_BYTES
        frame = data[start:start + FRAME_BYTES]
        counter = _validate_frame(frame)
        if previous_counter is not None and counter != ((previous_counter + 1) & 0xFF):
            raise ValueError(f"frame counter progression invalid at frame {index}")
        previous_counter = counter
        yield frame


def decode_frame(frame: bytes) -> list[bytes]:
    """Return a validated 56-row by 144-column image, averaging adjacent rows."""
    _validate_frame(frame)
    image = []
    for row_index in range(0, ROWS_PER_FRAME, 2):
        first = row_index * ROW_BYTES + HEADER_BYTES
        second = first + ROW_BYTES
        pixels = bytes((a + b) // 2 for a, b in zip(
            frame[first:first + PIXEL_BYTES],
            frame[second:second + PIXEL_BYTES],
        ))
        image.append(pixels)
    return image


def _read_input(path: str) -> bytes:
    with open(path, "rb") as stream:
        data = stream.read(MAX_INPUT_BYTES + 1)
    if len(data) > MAX_INPUT_BYTES:
        raise ValueError(f"input exceeds {MAX_INPUT_BYTES} byte limit")
    return data


def _write_pgm_exclusive(path: str, image: list[bytes]) -> None:
    payload = b"P5\n144 56\n255\n" + b"".join(image)
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    fd = os.open(path, flags, 0o600)
    try:
        with os.fdopen(fd, "wb") as stream:
            stream.write(payload)
    except BaseException:
        try:
            os.unlink(path)
        except OSError:
            pass
        raise


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, help="raw observed-layout capture")
    parser.add_argument("--output", required=True, help="new PGM output path")
    parser.add_argument("--frame", type=int, default=0, help="zero-based frame index (default: 0)")
    parser.add_argument("--allow-partial-tail", action="store_true",
                        help="ignore trailing bytes after the last complete frame")
    args = parser.parse_args(argv)
    if args.frame < 0:
        parser.error("--frame must be nonnegative")
    try:
        data = _read_input(args.input)
        frames = list(iter_frames(data, allow_partial_tail=args.allow_partial_tail))
        if args.frame >= len(frames):
            raise ValueError(f"frame index {args.frame} outside {len(frames)} available frame(s)")
        image = decode_frame(frames[args.frame])
        _write_pgm_exclusive(args.output, image)
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    print(f"144x56; {len(frames)} frame(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
