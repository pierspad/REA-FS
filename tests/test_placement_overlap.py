"""Synthetic-only tests for tools/placement_overlap.py."""

import unittest

import numpy as np

from tools.placement_overlap import bandpass, best_overlap


def synthetic_ridges(h=80, w=200, seed=1):
    rng = np.random.default_rng(seed)
    yy, xx = np.mgrid[0:h, 0:w]
    phase = np.cumsum(rng.normal(0, 0.02, (h, w)), axis=1)
    return np.sin(2 * np.pi * (0.15 * xx + 0.05 * yy) + phase + 0.002 * yy * xx / 10)


class PlacementOverlapTest(unittest.TestCase):
    def test_recovers_known_translation(self):
        big = bandpass(synthetic_ridges())
        a = big[10:66, 20:164]
        b = big[4:60, 12:156]  # b shifted: content moved by (+6, +8)
        result = best_overlap(a, b, angles=[0])
        self.assertGreater(result["score"], 0.95)
        self.assertEqual((result["dy"], result["dx"]), (6, 8))

    def test_unrelated_texture_scores_lower(self):
        a = bandpass(synthetic_ridges(seed=1)[10:66, 20:164])
        rng = np.random.default_rng(7)
        noise = bandpass(rng.normal(size=a.shape))
        self.assertLess(best_overlap(a, noise)["score"], 0.5)

    def test_rejects_bad_input(self):
        with self.assertRaises(ValueError):
            best_overlap(np.zeros((10, 10)), np.zeros((10, 11)))
        with self.assertRaises(ValueError):
            bandpass(np.zeros((16, 16)))


if __name__ == "__main__":
    unittest.main()
