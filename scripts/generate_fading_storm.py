#!/usr/bin/env python3
"""
generate_fading_storm.py — Inject a storm that gradually builds then fades.

Creates one output .npy directory per forecast cycle, each with a different
storm intensity so the CCH re-weighting sees the weather gradually worsen and
then improve — mimicking a real synoptic storm lifecycle.

Intensity profile (default):
    Day 0 → 10 %   barely choppy
    Day 1 → 30 %   noticeable swell
    Day 2 → 65 %   rough seas
    Day 3 → 100 %  peak storm
    Day 4 → 80 %   still severe
    Day 5 → 50 %   moderating
    Day 6 → 15 %   back to light chop

CCH edge-weight formula:  cost = dist × (1 + sigwh/3 + headwind_f)
    headwind_f = max(0, -was · cos(hdg - wad_rad)) / 20
    → at 10 % calm sea (1.5 m, beam wind): ×1.50  — detour unlikely
    → at 65 % (9.75 m, beam wind):         ×4.25  — detour attractive
    → at 100 % + full headwind (45 m/s):   ×8.25  — reversal blocked

Usage:
    python scripts/generate_fading_storm.py \\
        --weather-dir data/weather \\
        --max-sigwh 15.0 --max-wind 45.0 \\
        [--lat-min 30] [--lat-max 65] \\
        [--lon-min -70] [--lon-max -10]
"""

import argparse
import os
import shutil

import numpy as np

GRID_ROWS = 721
GRID_COLS = 1440

DEFAULT_PROFILE = [0.10, 0.30, 0.65, 1.00, 0.80, 0.50, 0.15]

FORECAST_DIRS = [
    "2026_06_08_12",
    "2026_06_09_12",
    "2026_06_10_12",
    "2026_06_11_12",
    "2026_06_12_12",
    "2026_06_13_12",
    "2026_06_14_12",
]


def lat_to_row(lat):
    return max(0, min(GRID_ROWS - 1, round((90.0 - lat) / 0.25)))


def lon_to_col(lon):
    return max(0, min(GRID_COLS - 1, round((lon % 360.0) / 0.25)))


def patch_variable(src_path: str, dst_path: str, ocean_mask, row_min, row_max,
                   col_min, col_max, value: float) -> None:
    arr = np.load(src_path)
    patched = arr.copy()
    region = np.s_[row_min:row_max + 1, col_min:col_max + 1]
    patched[region][ocean_mask[region]] = np.float16(value)
    np.save(dst_path, patched)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--weather-dir", default="data/weather")
    parser.add_argument("--max-sigwh",   type=float, default=15.0)
    parser.add_argument("--max-wind",    type=float, default=45.0)
    parser.add_argument("--lat-min",     type=float, default=30.0)
    parser.add_argument("--lat-max",     type=float, default=65.0)
    parser.add_argument("--lon-min",     type=float, default=-70.0)
    parser.add_argument("--lon-max",     type=float, default=-10.0)
    args = parser.parse_args()

    row_north = lat_to_row(args.lat_max)
    row_south = lat_to_row(args.lat_min)
    col_west  = lon_to_col(args.lon_min)
    col_east  = lon_to_col(args.lon_max)

    print("Fading storm lifecycle")
    print(f"  Box        : {args.lat_min}–{args.lat_max}°N, {args.lon_min}–{args.lon_max}°E")
    print(f"  Peak sigwh : {args.max_sigwh} m  |  Peak wind: {args.max_wind} m/s")
    print()
    print(f"{'Day':<5} {'Forecast dir':<22} {'Intensity':>10} {'sigwh (m)':>10} "
          f"{'wind (m/s)':>11} {'CCH cost factor':>16}")
    print("-" * 78)

    for day_idx, (forecast_name, intensity) in enumerate(
            zip(FORECAST_DIRS, DEFAULT_PROFILE)):

        sigwh_val = args.max_sigwh * intensity
        wind_val  = args.max_wind  * intensity
        cost_factor = 1.0 + sigwh_val / 3.0  # beam-wind baseline (headwind_f = 0)

        src_dir = os.path.join(args.weather_dir, forecast_name)
        out_dir = os.path.join(args.weather_dir,
                               forecast_name.rsplit("_12", 1)[0] + "_fading")
        if not os.path.isdir(src_dir):
            print(f"  [skip] {src_dir} not found")
            continue

        os.makedirs(out_dir, exist_ok=True)
        for fname in os.listdir(src_dir):
            shutil.copy2(os.path.join(src_dir, fname),
                         os.path.join(out_dir, fname))

        # Build ocean mask from sigwh (land = NaN)
        sigwh_orig = np.load(os.path.join(src_dir, "sigwh.npy"))
        ocean_mask = ~np.isnan(sigwh_orig)

        patch_variable(
            os.path.join(src_dir, "sigwh.npy"),
            os.path.join(out_dir, "sigwh.npy"),
            ocean_mask, row_north, row_south, col_west, col_east, sigwh_val,
        )
        patch_variable(
            os.path.join(src_dir, "was.npy"),
            os.path.join(out_dir, "was.npy"),
            ocean_mask, row_north, row_south, col_west, col_east, wind_val,
        )

        bar = "█" * int(intensity * 20)
        print(f"  {day_idx:<3}  {forecast_name:<22}  {intensity:>8.0%}  "
              f"{sigwh_val:>9.2f}  {wind_val:>10.2f}  {cost_factor:>14.2f}×   {bar}")

    print()
    print("Output directories (suffix _fading):")
    for name in FORECAST_DIRS:
        out = os.path.join(args.weather_dir, name.rsplit("_12", 1)[0] + "_fading")
        print(f"  {out}")


if __name__ == "__main__":
    main()
