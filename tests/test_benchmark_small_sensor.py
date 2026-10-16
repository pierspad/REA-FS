"""Unit tests for tools/benchmark_small_sensor.py."""

import unittest

import numpy as np

from tools.benchmark_small_sensor import (
    crop_sensor_slice,
    evaluate_minutiae_starvation,
    generate_synthetic_canvas,
    run_separation_benchmark,
)


class BenchmarkSmallSensorTest(unittest.TestCase):
    def test_minutiae_starvation_calculation(self):
        res = evaluate_minutiae_starvation()
        self.assertEqual(res["aperture_dimensions_mm"], (7.2, 2.8))
        self.assertEqual(res["full_area_mm2"], 20.16)
        self.assertEqual(res["overlap_area_mm2"], 15.12)
        # Expected minutiae should be well below 10
        self.assertLess(res["expected_minutiae_full"], 8.0)
        self.assertLess(res["expected_minutiae_overlap"], 6.0)
        # Starvation failure rate for Bozorth3 threshold 10 should be > 95%
        self.assertGreater(res["bozorth_starvation_probability"], 0.95)

    def test_synthetic_canvas_generation(self):
        canvas = generate_synthetic_canvas(seed=42, height=120, width=200, wavelength=6.2)
        self.assertEqual(canvas.shape, (120, 200))
        self.assertTrue(np.all(np.isfinite(canvas)))
        self.assertGreater(canvas.std(), 0.1)

    def test_sensor_slice_cropping(self):
        canvas = generate_synthetic_canvas(seed=42, height=120, width=200, wavelength=6.2)
        slice_0 = crop_sensor_slice(canvas, dy=0, dx=0, angle_deg=0.0, noise_std=0.0)
        self.assertEqual(slice_0.shape, (56, 144))
        self.assertTrue(np.all(np.isfinite(slice_0)))

        # Slices with offset should differ
        slice_shift = crop_sensor_slice(canvas, dy=14, dx=0, angle_deg=5.0, noise_std=0.02)
        self.assertEqual(slice_shift.shape, (56, 144))
        self.assertFalse(np.allclose(slice_0, slice_shift))

    def test_separation_benchmark(self):
        res = run_separation_benchmark(n_subjects=3, wavelength=6.2)
        self.assertGreater(res["genuine_min"], 0.70)
        self.assertLess(res["impostor_max"], 0.50)
        self.assertGreater(res["separation_margin"], 0.30)


if __name__ == "__main__":
    unittest.main()
