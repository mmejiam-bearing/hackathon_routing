#!/usr/bin/env python3
"""
plot_rolling_voyage.py — Rolling-horizon voyage comparison (up to 3 routes).

Usage:
    python scripts/plot_rolling_voyage.py \\
        --calm   /tmp/rolling_calm.geojson \\
        --fading /tmp/rolling_fading.geojson \\
        --storm  /tmp/rolling_storm.geojson \\
        [--out   rolling_comparison.png]
"""

import argparse
import json
import sys

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import matplotlib.patches as mpatches
    import matplotlib.colors as mcolors
    import matplotlib.cm as cm
    import matplotlib.patheffects as pe
    import numpy as np
except ImportError:
    sys.exit("pip install matplotlib numpy")


# ---------------------------------------------------------------------------
# Data loading
# ---------------------------------------------------------------------------

def load_days(path: str) -> list[dict]:
    with open(path) as f:
        gj = json.load(f)
    days = []
    for feat in gj["features"]:
        p = feat["properties"]
        coords = feat["geometry"]["coordinates"]
        days.append({
            "day":     p["day"],
            "dist_nm": p["dist_nm"],
            "foc_mt":  p["foc_mt"],
            "time_h":  p.get("time_h", 0.0),
            "lons":    [c[0] for c in coords],
            "lats":    [c[1] for c in coords],
        })
    days.sort(key=lambda d: d["day"])
    return days


def totals(days: list[dict]) -> tuple[float, float, float]:
    dist = sum(d["dist_nm"] for d in days)
    foc  = sum(d["foc_mt"]  for d in days)
    time = sum(d["time_h"]  for d in days)
    return dist, foc, time


def day_end(d: dict) -> tuple[float, float]:
    return d["lons"][-1], d["lats"][-1]


def day_start(d: dict) -> tuple[float, float]:
    return d["lons"][0], d["lats"][0]


# ---------------------------------------------------------------------------
# Route style registry — one entry per scenario
# ---------------------------------------------------------------------------

ROUTE_STYLES = [
    # calm
    dict(cmap="winter",  lw=2.4, ls="-",   marker="o", ms=5,   label_dy=+0.9,
         name="Calm"),
    # fading storm
    dict(cmap="YlOrBr",  lw=2.2, ls="--",  marker="s", ms=4.5, label_dy=-1.6,
         name="Fading storm"),
    # sustained storm
    dict(cmap="RdPu",    lw=2.0, ls=":",   marker="D", ms=4.0, label_dy=+2.2,
         name="Sustained storm"),
]


def make_color_fn(cmap_name: str, n: int, offset: int = 0):
    cmap = matplotlib.colormaps[cmap_name].resampled(n + offset + 4)
    def color(i: int):
        return cmap((i + offset + 2) / (n + offset + 3))
    return color, cmap


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--calm",   default="/tmp/rolling_calm.geojson")
    parser.add_argument("--fading", default=None)
    parser.add_argument("--storm",  default=None)
    parser.add_argument("--out",    default="rolling_comparison.png")
    parser.add_argument("--storm-lat-min", type=float, default=30.0)
    parser.add_argument("--storm-lat-max", type=float, default=65.0)
    parser.add_argument("--storm-lon-min", type=float, default=-70.0)
    parser.add_argument("--storm-lon-max", type=float, default=-10.0)
    args = parser.parse_args()

    # Build list of (days, style) pairs for whichever files were supplied
    routes: list[tuple[list[dict], dict]] = []
    for path, style in zip(
        [args.calm, args.fading, args.storm],
        ROUTE_STYLES,
    ):
        if path:
            routes.append((load_days(path), style))

    if not routes:
        sys.exit("No GeoJSON files supplied.")

    # ---- figure ---------------------------------------------------------------
    fig, axes = plt.subplots(
        1, 2,
        figsize=(22, 11),
        gridspec_kw={"width_ratios": [3, 1]},
    )
    ax_map, ax_bar = axes

    fig.patch.set_facecolor("#111827")
    ax_map.set_facecolor("#b8d8e8")
    ax_bar.set_facecolor("#1f2937")

    for sp in ax_map.spines.values():
        sp.set_edgecolor("#444444")
    ax_map.tick_params(colors="white", labelsize=9)
    ax_map.set_xlabel("Longitude", color="#aaaaaa", fontsize=10)
    ax_map.set_ylabel("Latitude",  color="#aaaaaa", fontsize=10)

    # ---- storm box ------------------------------------------------------------
    bw = args.storm_lon_max - args.storm_lon_min
    bh = args.storm_lat_max - args.storm_lat_min
    ax_map.add_patch(mpatches.FancyBboxPatch(
        (args.storm_lon_min, args.storm_lat_min), bw, bh,
        boxstyle="square,pad=0",
        linewidth=1.8, edgecolor="#ff5555", facecolor="#ff000020", zorder=2,
    ))
    ax_map.text(
        (args.storm_lon_min + args.storm_lon_max) / 2,
        args.storm_lat_max - 2.5,
        "STORM REGION  ·  sigwh 15 m  ·  wind 45 m/s",
        ha="center", va="top", fontsize=8, color="#ffaaaa",
        fontweight="bold", zorder=3,
    )

    # ---- draw routes ----------------------------------------------------------
    legend_handles = []
    bar_data: list[tuple[str, float, float, float, object]] = []  # name,dist,foc,time,color

    for days, style in routes:
        n = len(days)
        color_fn, raw_cmap = make_color_fn(style["cmap"], n)
        mid_color = color_fn(n // 2)

        for i, day in enumerate(days):
            c = color_fn(i)
            ax_map.plot(day["lons"], day["lats"],
                        color=c, lw=style["lw"], ls=style["ls"],
                        solid_capstyle="round", zorder=4)
            ex, ey = day_end(day)
            ax_map.plot(ex, ey, style["marker"], color=c,
                        ms=style["ms"], zorder=5)
            ax_map.text(
                ex + 0.4, ey + style["label_dy"], str(day["day"]),
                color=c, fontsize=7.5, fontweight="bold", zorder=6,
                path_effects=[pe.withStroke(linewidth=1.8, foreground="black")],
            )

        dist, foc, time = totals(days)
        lbl = (f"{style['name']} — {n} days\n"
               f"{dist:,.0f} nm  ·  {foc:,.0f} MT  ·  {time:.0f} h")
        legend_handles.append(
            mpatches.Patch(facecolor=mid_color, label=lbl)
        )
        bar_data.append((style["name"], dist, foc, time, mid_color))

    # ---- origin & destination -------------------------------------------------
    origin_days = routes[0][0]
    ox, oy = day_start(origin_days[0])
    dx, dy = day_end(origin_days[-1])
    for px, py, lbl, dy_off in [
        (ox, oy, "New York", -1.8),
        (dx, dy, "London",   +1.4),
    ]:
        ax_map.plot(px, py, "w*", ms=13, zorder=7)
        ax_map.text(px, py + dy_off, lbl,
                    ha="center", color="white", fontsize=9, fontweight="bold",
                    zorder=8,
                    path_effects=[pe.withStroke(linewidth=2, foreground="black")])

    # ---- storm-box legend entry -----------------------------------------------
    legend_handles.append(mpatches.Patch(
        facecolor="#ff000020", edgecolor="#ff5555", linewidth=1.5,
        label="Storm region  (sigwh=15 m, wind=45 m/s)",
    ))

    ax_map.legend(
        handles=legend_handles,
        loc="lower right",
        facecolor="#1f2937", labelcolor="white",
        fontsize=8, framealpha=0.92,
    )

    ax_map.set_xlim(-95, 20)
    ax_map.set_ylim(5, 78)
    ax_map.set_title(
        "Rolling-horizon re-routing: New York → London\n"
        "Each day replans from current position using that day's forecast",
        color="white", fontsize=11, pad=8,
    )

    # ---- bar chart (right panel) ----------------------------------------------
    names   = [b[0]   for b in bar_data]
    dists   = [b[1]   for b in bar_data]
    focs    = [b[2]   for b in bar_data]
    times   = [b[3]   for b in bar_data]
    colors  = [b[4]   for b in bar_data]

    x       = np.arange(len(names))
    bar_w   = 0.26

    ax_bar.tick_params(colors="white", labelsize=8)
    ax_bar.set_facecolor("#1f2937")
    for sp in ax_bar.spines.values():
        sp.set_edgecolor("#444444")

    # Three grouped bars: dist / foc / time — each normalised to calm=1
    base_dist, base_foc, base_time = dists[0], focs[0], times[0]
    norm_dist  = [d / base_dist for d in dists]
    norm_foc   = [f / base_foc  for f in focs]
    norm_time  = [t / base_time for t in times]

    b1 = ax_bar.bar(x - bar_w, norm_dist, bar_w, color=colors,
                    alpha=0.85, label="Distance", edgecolor="#555")
    b2 = ax_bar.bar(x,          norm_foc,  bar_w, color=colors,
                    alpha=0.60, label="FOC",      edgecolor="#555", hatch="//")
    b3 = ax_bar.bar(x + bar_w,  norm_time, bar_w, color=colors,
                    alpha=0.40, label="Time",     edgecolor="#555", hatch="xx")

    # Value labels on bars
    for bars, vals in [(b1, dists), (b2, focs), (b3, times)]:
        for bar, raw, nv in zip(bars, vals, [norm_dist, norm_foc, norm_time]):
            h = bar.get_height()
            unit = "nm" if bars is b1 else ("MT" if bars is b2 else "h")
            ax_bar.text(
                bar.get_x() + bar.get_width() / 2, h + 0.02,
                f"{raw:,.0f}\n{unit}",
                ha="center", va="bottom", color="white", fontsize=6.5,
                path_effects=[pe.withStroke(linewidth=1.5, foreground="black")],
            )

    ax_bar.axhline(1.0, color="#888", lw=1, ls="--")
    ax_bar.set_xticks(x)
    ax_bar.set_xticklabels(names, color="white", fontsize=8)
    ax_bar.set_ylabel("Relative to Calm baseline", color="#aaaaaa", fontsize=8)
    ax_bar.yaxis.label.set_color("#aaaaaa")
    ax_bar.set_title("Cost comparison\n(normalised to calm = 1×)",
                     color="white", fontsize=9)

    bar_legend = ax_bar.legend(
        handles=[
            mpatches.Patch(facecolor="#888", alpha=0.85, label="Distance"),
            mpatches.Patch(facecolor="#888", alpha=0.60, hatch="//", label="FOC"),
            mpatches.Patch(facecolor="#888", alpha=0.40, hatch="xx", label="Time"),
        ],
        facecolor="#111827", labelcolor="white", fontsize=7.5,
        loc="upper left",
    )

    # ---- save -----------------------------------------------------------------
    plt.tight_layout()
    plt.savefig(args.out, dpi=160, facecolor=fig.get_facecolor(),
                bbox_inches="tight")
    print(f"Saved: {args.out}")


if __name__ == "__main__":
    main()
