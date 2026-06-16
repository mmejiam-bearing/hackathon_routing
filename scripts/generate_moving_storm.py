#!/usr/bin/env python3
"""
generate_moving_storm.py — Inject a spatially compact storm into a copy of
the average-weather dataset over a consecutive date range.

The storm is modelled as a 2-D spatial Gaussian that translates across the
ocean each day. sigwh (wave grid, 621x1440, south-first, -75°..+80°) and
was (wind grid, 721x1440, south-first, -90°..+90°) are patched; all other
fields are copied unmodified from the source dataset.

Output has the same <YYYY>/<MM>/<DD>/<field>.npy structure as the source
dataset, so plan_voyage can consume it directly via --avg-weather-dir:
  python3 scripts/plan_voyage.py --avg-weather-dir /tmp/storm_scenario ...

Usage:
    python3 scripts/generate_moving_storm.py \\
        --avg-weather-dir /path/to/output \\     # real average dataset
        --out-dir         /tmp/storm_scenario \\  # patched storm copy
        --dates           2026-06-08 2026-06-09 2026-06-10 2026-06-11 \\
        --start-lat 50.0 --start-lon -25.0 \\
        --vel-lat   0.0  --vel-lon   -5.0 \\
        --sigma-lat 5.0  --sigma-lon  6.0 \\
        --max-sigwh 15.0 --max-wind  45.0
"""

import argparse
import datetime
import os
import shutil

import numpy as np

# Wave grid: sigwh lives on (621, 1440), south-first, lat -75..+80
WAVE_ROWS = 621
WAVE_COLS = 1440
WAVE_LAT_MIN = -75.0
WAVE_RES = 0.25

# Wind grid: was lives on (721, 1440), south-first, lat -90..+90
WIND_ROWS = 721
WIND_COLS = 1440
WIND_LAT_MIN = -90.0
WIND_RES = 0.25

# All 8 fields per day — see average_weather_description.md Section 3.
ALL_FIELDS = ["sigwh", "wsh", "wsp", "wsd", "pwd", "swell_residual", "was", "wad"]
# Only these two fields are modified by the storm injector; others are copied as-is.
PATCHED_FIELDS = {"sigwh": (WAVE_ROWS, WAVE_COLS, WAVE_LAT_MIN, WAVE_RES),
                  "was":   (WIND_ROWS, WIND_COLS, WIND_LAT_MIN, WIND_RES)}


def build_intensity_field(
    center_lat: float, center_lon: float,
    sigma_lat: float, sigma_lon: float,
    nrows: int, ncols: int, lat_min: float, res: float,
) -> np.ndarray:
    """Return an (nrows, ncols) float32 Gaussian intensity field in [0, 1].

    Row 0 = lat_min (south-first), increasing northward at `res` degrees/row.
    """
    lats = lat_min + np.arange(nrows) * res            # shape (nrows,) south-first
    lons = np.arange(ncols) * res                      # shape (ncols,) 0..359.75E

    c_lon_norm = center_lon % 360.0

    lat_grid = lats[:, np.newaxis]                     # (nrows, 1)
    lon_grid = lons[np.newaxis, :]                     # (1, ncols)

    dlat = (lat_grid - center_lat) / sigma_lat
    dlon = (lon_grid - c_lon_norm) / sigma_lon

    # Wrap longitude difference to [-180/sigma, 180/sigma]
    half_wrap = 180.0 / sigma_lon
    dlon = (dlon + half_wrap) % (2 * half_wrap) - half_wrap

    return np.exp(-0.5 * (dlat**2 + dlon**2)).astype(np.float32)


def source_day_dir(avg_weather_dir: str, date: datetime.date) -> str:
    """Resolve the 2024-mapped directory for a given calendar date."""
    return os.path.join(avg_weather_dir, "2024", f"{date.month:02d}", f"{date.day:02d}")


def out_day_dir(out_dir: str, date: datetime.date) -> str:
    return os.path.join(out_dir, "2024", f"{date.month:02d}", f"{date.day:02d}")


def patch_storm(
    src_dir: str, dst_dir: str,
    center_lat: float, center_lon: float,
    sigma_lat: float, sigma_lon: float,
    max_sigwh: float, max_wind: float,
) -> None:
    os.makedirs(dst_dir, exist_ok=True)

    # Copy all files first, then overwrite the two patched fields.
    for fname in os.listdir(src_dir):
        if fname.endswith(".npy"):
            shutil.copy2(os.path.join(src_dir, fname), os.path.join(dst_dir, fname))

    for varname, (nrows, ncols, lat_min, res) in PATCHED_FIELDS.items():
        src_path = os.path.join(src_dir, f"{varname}.npy")
        dst_path = os.path.join(dst_dir, f"{varname}.npy")

        peak = max_sigwh if varname == "sigwh" else max_wind
        arr = np.load(src_path)
        assert arr.shape == (nrows, ncols), \
            f"{varname}.npy: expected ({nrows}, {ncols}), got {arr.shape}"

        intensity = build_intensity_field(
            center_lat, center_lon, sigma_lat, sigma_lon, nrows, ncols, lat_min, res)

        ocean = ~np.isnan(arr)
        patched = arr.copy()
        patched[ocean] = (intensity * peak).astype(arr.dtype)[ocean]
        np.save(dst_path, patched)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Generate a synthetic moving-storm scenario from the average-weather dataset.")
    parser.add_argument("--avg-weather-dir", required=True,
                         help="source average-weather dataset (contains 2024/<MM>/<DD>/)")
    parser.add_argument("--out-dir", required=True,
                         help="output directory (same layout; usable as --avg-weather-dir)")
    parser.add_argument("--dates", nargs="+", required=True,
                         type=datetime.date.fromisoformat,
                         help="YYYY-MM-DD dates to inject storm into (chronological order)")
    parser.add_argument("--start-lat",   type=float, default=50.0)
    parser.add_argument("--start-lon",   type=float, default=-25.0)
    parser.add_argument("--vel-lat",     type=float, default=0.0,
                         help="storm latitude velocity (°/day, positive = northward)")
    parser.add_argument("--vel-lon",     type=float, default=-5.0,
                         help="storm longitude velocity (°/day, negative = westward)")
    parser.add_argument("--sigma-lat",   type=float, default=5.0)
    parser.add_argument("--sigma-lon",   type=float, default=6.0)
    parser.add_argument("--max-sigwh",   type=float, default=15.0,
                         help="peak significant wave height at storm centre [m]")
    parser.add_argument("--max-wind",    type=float, default=45.0,
                         help="peak wind speed at storm centre [m/s]")
    args = parser.parse_args()

    print("Moving Gaussian storm")
    print(f"  Day-0 centre : {args.start_lat}°N, {args.start_lon}°E")
    print(f"  Velocity     : {args.vel_lat:+.1f}°/day lat, {args.vel_lon:+.1f}°/day lon")
    print(f"  σ_lat/σ_lon  : {args.sigma_lat}° / {args.sigma_lon}°")
    print(f"  Peak sigwh   : {args.max_sigwh} m   |   Peak wind: {args.max_wind} m/s")
    print(f"  {'Date':<12} {'Src dir':<40} {'Ctr lat':>8} {'Ctr lon':>8}")
    print("-" * 76)

    for day_idx, date in enumerate(args.dates):
        center_lat = args.start_lat + day_idx * args.vel_lat
        center_lon = args.start_lon + day_idx * args.vel_lon

        src_dir = source_day_dir(args.avg_weather_dir, date)
        dst_dir = out_day_dir(args.out_dir, date)

        if not os.path.isdir(src_dir):
            print(f"  [skip] {src_dir} not found")
            continue

        patch_storm(
            src_dir, dst_dir,
            center_lat, center_lon,
            args.sigma_lat, args.sigma_lon,
            args.max_sigwh, args.max_wind,
        )
        print(f"  {date.isoformat():<12} {src_dir:<40} {center_lat:>7.1f}°  {center_lon:>7.1f}°")

    print()
    print("Output base dir (use as --avg-weather-dir):", args.out_dir)
    print("Run plan_voyage with: python3 scripts/plan_voyage.py "
          f"--avg-weather-dir {args.out_dir} --start-date {args.dates[0].isoformat()} ...")


if __name__ == "__main__":
    main()
