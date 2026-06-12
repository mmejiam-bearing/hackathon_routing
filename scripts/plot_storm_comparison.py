#!/usr/bin/env python3
"""
plot_storm_comparison.py — Visualise calm vs storm routes over the Atlantic.

Usage:
    python scripts/plot_storm_comparison.py \\
        --calm  /tmp/route_calm.geojson \\
        --storm /tmp/route_storm.geojson \\
        [--out  storm_comparison.png]
"""

import argparse
import json
import sys

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import matplotlib.patches as mpatches
    import numpy as np
except ImportError:
    sys.exit("Install matplotlib and numpy: pip install matplotlib numpy")


def load_coords(geojson_path: str):
    with open(geojson_path) as f:
        gj = json.load(f)
    lons, lats = [], []
    for feature in gj["features"]:
        geom = feature["geometry"]
        if geom["type"] == "LineString":
            for lon, lat in geom["coordinates"]:
                lons.append(lon)
                lats.append(lat)
        elif geom["type"] == "MultiLineString":
            for line in geom["coordinates"]:
                for lon, lat in line:
                    lons.append(lon)
                    lats.append(lat)
    return lons, lats


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--calm",  default="/tmp/route_calm.geojson")
    parser.add_argument("--storm", default="/tmp/route_storm.geojson")
    parser.add_argument("--out",   default="storm_comparison.png")
    # Storm box used when generating the scenario
    parser.add_argument("--storm-lat-min", type=float, default=30.0)
    parser.add_argument("--storm-lat-max", type=float, default=65.0)
    parser.add_argument("--storm-lon-min", type=float, default=-70.0)
    parser.add_argument("--storm-lon-max", type=float, default=-10.0)
    args = parser.parse_args()

    calm_lons,  calm_lats  = load_coords(args.calm)
    storm_lons, storm_lats = load_coords(args.storm)

    fig, ax = plt.subplots(figsize=(14, 7))
    ax.set_facecolor("#d0e8f5")
    fig.patch.set_facecolor("#1a1a2e")

    # Storm box
    box_w = args.storm_lon_max - args.storm_lon_min
    box_h = args.storm_lat_max - args.storm_lat_min
    storm_rect = mpatches.FancyBboxPatch(
        (args.storm_lon_min, args.storm_lat_min), box_w, box_h,
        boxstyle="round,pad=0.5",
        linewidth=2, edgecolor="red", facecolor="#ff000033",
        zorder=2, label="Synthetic storm (sigwh=15 m, wind=45 m/s)",
    )
    ax.add_patch(storm_rect)
    ax.text(
        (args.storm_lon_min + args.storm_lon_max) / 2,
        (args.storm_lat_min + args.storm_lat_max) / 2,
        "STORM\nSIGWH 15 m\nWIND 45 m/s",
        ha="center", va="center", fontsize=9, color="white",
        fontweight="bold", zorder=3,
    )

    # Routes
    ax.plot(calm_lons,  calm_lats,  "c-",  lw=2.5, zorder=4, label="Calm route  (3 379 nm, 12 days)")
    ax.plot(storm_lons, storm_lats, "y--", lw=2.5, zorder=4, label="Storm route (5 750 nm, 20 days)")

    # Origin / destination markers
    ax.plot(-74.0, 40.7, "wo", ms=8, zorder=5)
    ax.text(-74.0, 40.7 - 1.5, "New York", ha="center", color="white", fontsize=8)
    ax.plot(-0.1,  51.5, "wo", ms=8, zorder=5)
    ax.text(-0.1,  51.5 + 1.5, "London",   ha="center", color="white", fontsize=8)

    ax.set_xlim(-90, 20)
    ax.set_ylim(10, 75)
    ax.set_xlabel("Longitude", color="white")
    ax.set_ylabel("Latitude",  color="white")
    ax.tick_params(colors="white")
    for spine in ax.spines.values():
        spine.set_edgecolor("white")

    ax.legend(loc="lower right", facecolor="#1a1a2e", labelcolor="white", fontsize=9)
    ax.set_title("Maritime re-routing due to synthetic North Atlantic storm",
                 color="white", fontsize=12, pad=12)

    plt.tight_layout()
    plt.savefig(args.out, dpi=150, facecolor=fig.get_facecolor())
    print(f"Saved: {args.out}")


if __name__ == "__main__":
    main()
