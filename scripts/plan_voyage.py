#!/usr/bin/env python3
"""
scripts/plan_voyage.py

Rolling-horizon multi-day voyage planner — the Python-bound equivalent of
the maritime-voyage-router CLI. For each ~24h period: compute that period's
calendar date (start-date + days elapsed), load its daily-average weather
(mapped onto the 2024 dataset — see average_weather_description.md),
re-customise the CCH, route from the vessel's current position to the
destination, advance ~24h, repeat until the destination is reached.

Calls maritime_router.plan_voyage() (router_server/python/bindings.cpp),
which itself calls maritime::router_server::plan_voyage()
(router_server/src/voyage_planner.cpp) — the exact same C++ function used by
the maritime-voyage-router binary. This script exists to drive that function
from Python and write the same per-day GeoJSON format the binary writes, so
plot_rolling_voyage.py can consume either one's output interchangeably.

Usage:
    python3 scripts/plan_voyage.py \\
        --avg-weather-dir /Users/mmejiam9206/exfat_mount/bearing_noaa/output \\
        --start-date 2026-06-08 \\
        --from-lat 40.7 --from-lon -74.0 --to-lat 51.5 --to-lon -0.1 \\
        --out /tmp/rolling_calm.geojson
"""

import argparse
import json
import sys
import time
from pathlib import Path

ROUTER_SERVER_DIR = Path(__file__).resolve().parent.parent / "router_server"
sys.path.insert(0, str(ROUTER_SERVER_DIR))

import maritime_router as mr  # noqa: E402  (path must be set up first)

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_ARTIFACTS = REPO_ROOT / "data" / "artifacts"


def parse_args():
    p = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--graph",     default=str(DEFAULT_ARTIFACTS / "graph.bin"))
    p.add_argument("--flags",     default=str(DEFAULT_ARTIFACTS / "flags.bin"))
    p.add_argument("--snap-wave", default=str(DEFAULT_ARTIFACTS / "snap_wave.bin"))
    p.add_argument("--snap-wind", default=str(DEFAULT_ARTIFACTS / "snap_wind.bin"))
    p.add_argument("--cch",       default=str(DEFAULT_ARTIFACTS / "cch_topo.bin"))
    p.add_argument("--avg-weather-dir", required=True,
                   help="base directory containing <YYYY>/<MM>/<DD>/<field>.npy "
                        "(the daily-average dataset; mapped onto 2024)")
    p.add_argument("--start-date", required=True,
                   help="voyage start date, YYYY-MM-DD")
    p.add_argument("--from-lat", type=float, required=True)
    p.add_argument("--from-lon", type=float, required=True)
    p.add_argument("--to-lat",   type=float, required=True)
    p.add_argument("--to-lon",   type=float, required=True)
    p.add_argument("--speed", type=float, default=14.0, help="vessel service speed [kts]")
    p.add_argument("--draft", type=float, default=10.0)
    p.add_argument("--beam",  type=float, default=32.0)
    p.add_argument("--loa",   type=float, default=200.0)
    p.add_argument("--sigma-along-nm", type=float, default=200.0)
    p.add_argument("--sigma-perp-nm",  type=float, default=400.0)
    p.add_argument("--out", default="voyage_rolling.geojson")
    return p.parse_args()


def to_geojson(plan) -> dict:
    features = []
    for seg in plan.segments:
        coords = list(zip(seg.lon.tolist(), seg.lat.tolist()))
        features.append({
            "type": "Feature",
            "properties": {
                "day":     seg.day,
                "dist_nm": seg.dist_nm,
                "foc_mt":  seg.foc_mt,
                "time_h":  seg.time_h,
            },
            "geometry": {"type": "LineString", "coordinates": coords},
        })
    return {"type": "FeatureCollection", "features": features}


def main():
    args = parse_args()

    t0 = time.perf_counter()
    plan = mr.plan_voyage(
        graph_path=args.graph, flags_path=args.flags,
        snap_wave_path=args.snap_wave, snap_wind_path=args.snap_wind,
        cch_topo_path=args.cch,
        avg_weather_dir=args.avg_weather_dir, start_date=args.start_date,
        origin_lat=args.from_lat, origin_lon=args.from_lon,
        dest_lat=args.to_lat,     dest_lon=args.to_lon,
        service_speed_kts=args.speed,
        draft_m=args.draft, beam_m=args.beam, loa_m=args.loa,
        sigma_along_nm=args.sigma_along_nm, sigma_perp_nm=args.sigma_perp_nm,
    )
    elapsed = time.perf_counter() - t0

    if not plan.reached_destination:
        print(f"Warning: destination not reached after {len(plan.segments)} period(s).",
              file=sys.stderr)

    print(f"[voyage] {len(plan.segments)} day(s)  "
          f"total_dist={plan.total_dist_nm:.1f} nm  "
          f"total_foc={plan.total_foc_mt:.1f} mt  "
          f"total_time={plan.total_time_h:.1f} h  "
          f"({elapsed:.1f}s)", file=sys.stderr)

    Path(args.out).write_text(json.dumps(to_geojson(plan), indent=2))
    print(f"Wrote {args.out}", file=sys.stderr)


if __name__ == "__main__":
    main()
