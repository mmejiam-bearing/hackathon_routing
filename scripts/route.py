#!/usr/bin/env python3
"""
scripts/route.py

Routes a vessel from point A to point B over the maritime CCH graph,
returning the waypoint geometry together with the weather sampled at each
leg of the route (significant wave height, wind, wave direction).

Thin CLI over the maritime_router pybind11 module (router_server/python/
bindings.cpp), which wraps router_server's QueryServer/RoutingEngine —
the same C++ code path used by maritime-route-query and maritime-router-server.

Usage:
    python3 scripts/route.py \\
        --from-lat 51.9 --from-lon 4.5 \\
        --to-lat   1.3  --to-lon   103.8 \\
        --weights-dir data/artifacts \\
        --avg-weather-dir /Users/mmejiam9206/exfat_mount/bearing_noaa/output
"""

import argparse
import json
import sys
from pathlib import Path

ROUTER_SERVER_DIR = Path(__file__).resolve().parent.parent / "router_server"
sys.path.insert(0, str(ROUTER_SERVER_DIR))

import maritime_router as mr  # noqa: E402  (path must be set up first)

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_ARTIFACTS = REPO_ROOT / "data" / "artifacts"

STATUS_NAMES = {
    mr.Status.OK:                  "ok",
    mr.Status.NO_ROUTE:            "no_route",
    mr.Status.WEATHER_UNAVAILABLE: "weather_unavailable",
}


def parse_args():
    p = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--graph",        default=str(DEFAULT_ARTIFACTS / "graph.bin"))
    p.add_argument("--flags",        default=str(DEFAULT_ARTIFACTS / "flags.bin"))
    p.add_argument("--snap-wave",    default=str(DEFAULT_ARTIFACTS / "snap_wave.bin"))
    p.add_argument("--snap-wind",    default=str(DEFAULT_ARTIFACTS / "snap_wind.bin"))
    p.add_argument("--cch",          default=str(DEFAULT_ARTIFACTS / "cch_topo.bin"))
    p.add_argument("--weights-dir",  required=True, help="directory containing weights.bin")
    p.add_argument("--avg-weather-dir", default="",
                   help="base directory containing <YYYY>/<MM>/<DD>/<field>.npy "
                        "(average-weather dataset); omit to route on weights alone")
    p.add_argument("--from-lat", type=float, required=True)
    p.add_argument("--from-lon", type=float, required=True)
    p.add_argument("--to-lat",   type=float, required=True)
    p.add_argument("--to-lon",   type=float, required=True)
    p.add_argument("--speed",    type=float, default=14.0, help="vessel service speed [kts]")
    p.add_argument("--draft",    type=float, default=10.0, help="vessel draft [m]")
    p.add_argument("--beam",     type=float, default=32.0, help="vessel beam [m]")
    p.add_argument("--loa",      type=float, default=200.0, help="vessel length overall [m]")
    p.add_argument("--base-time-step", type=int, default=0)
    p.add_argument("--out", help="write the route + weather as GeoJSON to this path")
    return p.parse_args()


def to_geojson(result) -> dict:
    coords = list(zip(result.waypoint_lon.tolist(), result.waypoint_lat.tolist()))
    # Per-edge weather arrays have one fewer entry than waypoints; pad the
    # last value so every property array lines up with `coords` for GIS tools.
    def padded(arr):
        vals = arr.tolist()
        return vals + vals[-1:] if vals else []

    return {
        "type": "FeatureCollection",
        "features": [{
            "type": "Feature",
            "properties": {
                "status":        STATUS_NAMES[result.status],
                "total_dist_nm": result.total_dist_nm,
                "total_foc_mt":  result.total_foc_mt,
                "total_time_h":  result.total_time_h,
                "sig_wh":        padded(result.sig_wh),
                "wind_spd":      padded(result.wind_spd),
                "wind_dir":      padded(result.wind_dir),
                "wave_dir":      padded(result.wave_dir),
            },
            "geometry": {"type": "LineString", "coordinates": coords},
        }],
    }


def main():
    args = parse_args()

    engine = mr.RoutingEngine(
        graph_path=args.graph,
        flags_path=args.flags,
        snap_wave_path=args.snap_wave,
        snap_wind_path=args.snap_wind,
        cch_topo_path=args.cch,
        weights_dir=args.weights_dir,
        avg_weather_dir=args.avg_weather_dir,
    )
    print(f"Graph: {engine.n_nodes} nodes, {engine.n_edges} edges", file=sys.stderr)

    result = engine.route(
        origin_lat=args.from_lat, origin_lon=args.from_lon,
        dest_lat=args.to_lat,     dest_lon=args.to_lon,
        service_speed_kts=args.speed,
        draft_m=args.draft, beam_m=args.beam, loa_m=args.loa,
        base_time_step=args.base_time_step,
    )

    status = STATUS_NAMES[result.status]
    if result.status != mr.Status.OK:
        print(f"Route failed: {status}", file=sys.stderr)
        sys.exit(1)

    print(f"status={status} waypoints={len(result.waypoint_lat)} "
          f"dist_nm={result.total_dist_nm:.1f} foc_mt={result.total_foc_mt:.2f} "
          f"time_h={result.total_time_h:.1f}", file=sys.stderr)

    if args.out:
        Path(args.out).write_text(json.dumps(to_geojson(result), indent=2))
        print(f"Wrote {args.out}", file=sys.stderr)
    else:
        print("lat,lon,sig_wh,wind_spd,wind_dir,wave_dir")
        for i in range(len(result.waypoint_lat)):
            sw = result.sig_wh[i]     if i < len(result.sig_wh)     else ""
            ws = result.wind_spd[i]   if i < len(result.wind_spd)   else ""
            wd = result.wind_dir[i]   if i < len(result.wind_dir)   else ""
            vd = result.wave_dir[i]   if i < len(result.wave_dir)   else ""
            print(f"{result.waypoint_lat[i]},{result.waypoint_lon[i]},"
                  f"{sw},{ws},{wd},{vd}")


if __name__ == "__main__":
    main()
