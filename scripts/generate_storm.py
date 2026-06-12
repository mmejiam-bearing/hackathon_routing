#!/usr/bin/env python3
"""
generate_storm.py — Inject a synthetic storm into a weather .npy directory.

Creates a copy of an existing forecast directory with artificially high wave
heights and wind speeds over a specified ocean region, so the maritime router
demonstrably avoids the storm corridor.

Usage:
    python scripts/generate_storm.py \\
        --src  data/weather/2026_06_11_12 \\
        --out  data/weather/2026_06_11_storm \\
        [--lat-min 30] [--lat-max 65] \\
        [--lon-min -70] [--lon-max -10] \\
        [--sigwh 15.0] [--wind 45.0]

Grid convention:
    Latitude rows  : index = (90 - lat_deg) / 0.25   (row 0 = 90 N, row 720 = 90 S)
    Longitude cols : index = lon_deg_0_360 / 0.25     (col 0 = 0 E, col 1439 = 359.75 E)
    Land cells     : NaN — left untouched so the router snap-table stays valid.
"""

import argparse
import os
import shutil

import numpy as np


# ---------------------------------------------------------------------------
# Grid helpers
# ---------------------------------------------------------------------------

GRID_ROWS = 721    # 0.25° from 90 N to 90 S
GRID_COLS = 1440   # 0.25° from 0 E to 359.75 E


def lat_to_row(lat: float) -> int:
    """Latitude (degrees N) → row index.  Clamps to [0, 720]."""
    return max(0, min(GRID_ROWS - 1, round((90.0 - lat) / 0.25)))


def lon_to_col(lon: float) -> int:
    """Longitude (degrees, −180–180 or 0–360) → column index."""
    lon = lon % 360.0  # normalise to 0–360
    return max(0, min(GRID_COLS - 1, round(lon / 0.25)))


def build_ocean_mask(arr: np.ndarray) -> np.ndarray:
    """Boolean mask: True where the cell is ocean (not NaN)."""
    return ~np.isnan(arr)


def apply_storm(
    arr: np.ndarray,
    ocean_mask: np.ndarray,
    row_min: int,
    row_max: int,
    col_min: int,
    col_max: int,
    value: float,
) -> np.ndarray:
    """Set storm value on ocean cells inside the bounding box.

    Row indices increase southward, so row_min < row_max for a north-to-south
    storm band.  The function handles the wrap-around case (col_min > col_max)
    for storms that straddle the antimeridian.
    """
    patched = arr.copy()
    if col_min <= col_max:
        region = np.s_[row_min:row_max + 1, col_min:col_max + 1]
        mask = ocean_mask[region]
        patched[region][mask] = np.float16(value)
    else:
        # Straddles antimeridian: two sub-regions
        r1 = np.s_[row_min:row_max + 1, col_min:]
        r2 = np.s_[row_min:row_max + 1, : col_max + 1]
        patched[r1][ocean_mask[r1]] = np.float16(value)
        patched[r2][ocean_mask[r2]] = np.float16(value)
    return patched


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(description="Inject synthetic storm into weather .npy files")
    parser.add_argument("--src",     required=True, help="Source .npy directory (real forecast)")
    parser.add_argument("--out",     required=True, help="Output directory for storm scenario")

    # Storm bounding box in geographic coordinates
    parser.add_argument("--lat-min", type=float, default=30.0,
                        help="Southern edge of storm (degrees N, default 30)")
    parser.add_argument("--lat-max", type=float, default=65.0,
                        help="Northern edge of storm (degrees N, default 65)")
    parser.add_argument("--lon-min", type=float, default=-70.0,
                        help="Western edge of storm (degrees, default -70 / 290 E)")
    parser.add_argument("--lon-max", type=float, default=-10.0,
                        help="Eastern edge of storm (degrees, default -10 / 350 E)")

    # Storm intensity
    parser.add_argument("--sigwh", type=float, default=15.0,
                        help="Significant wave height inside storm (m, default 15)")
    parser.add_argument("--wind",  type=float, default=45.0,
                        help="Wind speed inside storm (m/s, default 45 ≈ 90 kt)")
    args = parser.parse_args()

    # Validate source
    if not os.path.isdir(args.src):
        raise SystemExit(f"Source directory not found: {args.src}")

    # Convert geographic bounds → grid indices
    # Latitudes: larger lat → smaller row index
    row_north = lat_to_row(args.lat_max)   # northern edge → smaller row
    row_south = lat_to_row(args.lat_min)   # southern edge → larger row
    col_west  = lon_to_col(args.lon_min)
    col_east  = lon_to_col(args.lon_max)

    print(f"Storm bounding box:")
    print(f"  Latitude  : {args.lat_min}°N – {args.lat_max}°N  (rows {row_north}–{row_south})")
    print(f"  Longitude : {args.lon_min}° – {args.lon_max}°  (cols {col_west}–{col_east})")
    print(f"  sigwh     : {args.sigwh} m")
    print(f"  wind      : {args.wind} m/s")

    # Create output directory; copy everything first
    os.makedirs(args.out, exist_ok=True)
    for fname in os.listdir(args.src):
        shutil.copy2(os.path.join(args.src, fname), os.path.join(args.out, fname))
    print(f"\nCopied {len(os.listdir(args.src))} files → {args.out}")

    # Load sigwh to derive the ocean mask (NaN = land, shared by all float16 vars)
    sigwh_path = os.path.join(args.src, "sigwh.npy")
    sigwh_orig = np.load(sigwh_path)  # (721, 1440) float16
    ocean_mask = build_ocean_mask(sigwh_orig)

    storm_cells = (
        ocean_mask[row_north:row_south + 1, col_west:col_east + 1].sum()
        if col_west <= col_east
        else (
            ocean_mask[row_north:row_south + 1, col_west:].sum()
            + ocean_mask[row_north:row_south + 1, :col_east + 1].sum()
        )
    )
    print(f"Ocean cells in storm box: {storm_cells:,}")

    # Patch sigwh
    sigwh_storm = apply_storm(
        sigwh_orig, ocean_mask,
        row_north, row_south,
        col_west,  col_east,
        args.sigwh,
    )
    out_sigwh = os.path.join(args.out, "sigwh.npy")
    np.save(out_sigwh, sigwh_storm)
    print(f"Patched sigwh  : max={float(np.nanmax(sigwh_storm)):.1f} m  "
          f"(was {float(np.nanmax(sigwh_orig)):.1f} m)")

    # Patch was (wind speed)
    was_path  = os.path.join(args.src, "was.npy")
    was_orig  = np.load(was_path)
    was_storm = apply_storm(
        was_orig, ocean_mask,
        row_north, row_south,
        col_west,  col_east,
        args.wind,
    )
    out_was = os.path.join(args.out, "was.npy")
    np.save(out_was, was_storm)
    print(f"Patched was    : max={float(np.nanmax(was_storm)):.1f} m/s  "
          f"(was {float(np.nanmax(was_orig)):.1f} m/s)")

    print(f"\nStorm scenario ready: {args.out}")
    headwind_max = args.wind / 20.0
    print(
        "\nCCH re-routing effect (beam wind baseline, headwind_f = 0):"
        f"\n  Calm water edge cost  = dist × 1.00"
        f"\n  Storm edge cost       = dist × {1 + args.sigwh / 3:.2f}   "
        f"(sigwh={args.sigwh} m)"
        f"\n  Storm + full headwind = dist × {1 + args.sigwh / 3 + headwind_max:.2f}   "
        f"(adds headwind_f={headwind_max:.2f} for {args.wind} m/s into-wind)"
    )


if __name__ == "__main__":
    main()
