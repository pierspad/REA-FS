"""Estimate rigid overlap between two decoded fingerprint images (NumPy only).

Offline diagnostic: band-pass both images, search a rotation grid and all
translations with FFT normalized cross-correlation over the overlapping area.
A high score with consistent transforms across placements indicates shared
ridge structure; it is NOT a recognition decision and has no calibrated
false-accept rate. Always compare against controls (flips, phase noise).
"""

import numpy as np


def bandpass(image: np.ndarray, low: float = 0.08, high: float = 0.30) -> np.ndarray:
    """Keep radial frequencies in [low, high) cycles/pixel; return unit variance."""
    image = np.asarray(image, dtype=float)
    if image.ndim != 2 or min(image.shape) < 8:
        raise ValueError("image must be 2-D and at least 8x8")
    fy = np.fft.fftfreq(image.shape[0])[:, None]
    fx = np.fft.fftfreq(image.shape[1])[None, :]
    radius = np.hypot(fy, fx)
    spectrum = np.fft.fft2(image - image.mean())
    out = np.real(np.fft.ifft2(spectrum * ((radius >= low) & (radius < high))))
    std = out.std()
    if std == 0:
        raise ValueError("image has no energy in the selected band")
    return out / std


def rotate_nearest(image: np.ndarray, degrees: float) -> tuple[np.ndarray, np.ndarray]:
    """Rotate about the centre with nearest sampling; return image and valid mask."""
    h, w = image.shape
    t = np.radians(degrees)
    yy, xx = np.mgrid[0:h, 0:w]
    cy, cx = (h - 1) / 2, (w - 1) / 2
    ys = cy + (yy - cy) * np.cos(t) - (xx - cx) * np.sin(t)
    xs = cx + (yy - cy) * np.sin(t) + (xx - cx) * np.cos(t)
    ok = (ys >= 0) & (ys <= h - 1) & (xs >= 0) & (xs <= w - 1)
    out = np.zeros_like(image)
    out[ok] = image[np.rint(ys[ok]).astype(int), np.rint(xs[ok]).astype(int)]
    return out, ok.astype(float)


def best_overlap(a: np.ndarray, b: np.ndarray, min_overlap: float = 0.7,
                 angles=range(-30, 31, 2)) -> dict:
    """Return the best normalized correlation of b onto a.

    Translation (dy, dx) is the shift applied to b, in pixels.
    """
    a = np.asarray(a, dtype=float)
    b = np.asarray(b, dtype=float)
    if a.shape != b.shape:
        raise ValueError("images must have the same shape")
    if not 0 < min_overlap <= 1:
        raise ValueError("min_overlap must be in (0, 1]")
    h, w = a.shape
    shape = (2 * h, 2 * w)
    ones = np.ones_like(a)
    fa, fa2, f1 = (np.fft.rfft2(v, shape) for v in (a, a * a, ones))
    best = {"score": -1.0}
    for angle in angles:
        br, mask = rotate_nearest(b, angle)
        fb, fb2, fm = (np.conj(np.fft.rfft2(v, shape)) for v in (br, br * br, mask))
        num = np.fft.irfft2(fa * fb, shape)
        overlap = np.fft.irfft2(f1 * fm, shape)
        energy_a = np.fft.irfft2(fa2 * fm, shape)
        energy_b = np.fft.irfft2(f1 * fb2, shape)
        valid = overlap > min_overlap * h * w
        corr = np.where(valid, num / np.sqrt(np.maximum(energy_a * energy_b, 1e-9)), -1.0)
        k = np.unravel_index(int(np.argmax(corr)), corr.shape)
        if corr[k] > best["score"]:
            dy = int(k[0]) if k[0] < h else int(k[0]) - 2 * h
            dx = int(k[1]) if k[1] < w else int(k[1]) - 2 * w
            best = {"score": float(corr[k]), "angle": float(angle), "dy": -dy,
                    "dx": -dx, "overlap": float(overlap[k] / (h * w))}
    return best
