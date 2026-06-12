#!/usr/bin/env python3
"""
plot_fading_storm.py — Calm vs fading-storm rolling-voyage comparison.

Two-panel figure:
  TOP    — Storm intensity timeline per forecast day with CCH cost-factor axis.
  BOTTOM — Map: both routes with per-day segment colouring and annotations.

The key story:
  - Days 0-1  the two routes are nearly identical (storm too mild to re-route)
  - Day 2     router starts pushing north to hug the upper storm boundary
  - Day 3     peak storm — ship is already inside the box; escaping south
               would now cost more than pushing through the northern edge
  - Days 4-7  storm fades; ship locked in northern corridor at ~50 N

Usage:
    python scripts/plot_fading_storm.py \\
        --calm   /tmp/rolling_calm.geojson \\
        --fading /tmp/rolling_fading.geojson \\
        [--out   fading_storm_comparison.png]
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
    import matplotlib.patheffects as pe
    import matplotlib.gridspec as gridspec
    import numpy as np
except ImportError:
    sys.exit("pip install matplotlib numpy")

# Matches generate_fading_storm.py
INTENSITY_PROFILE = [0.10, 0.30, 0.65, 1.00, 0.80, 0.50, 0.15]
MAX_SIGWH = 15.0   # m at 100 %

STORM_LAT_MIN, STORM_LAT_MAX =  30.0,  65.0
STORM_LON_MIN, STORM_LON_MAX = -70.0, -10.0


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
            "lons":    [c[0] for c in coords],
            "lats":    [c[1] for c in coords],
        })
    return sorted(days, key=lambda d: d["day"])


# ---------------------------------------------------------------------------
# Plot helpers
# ---------------------------------------------------------------------------

def segment_color(cmap, i: int, n: int) -> tuple:
    return cmap(0.25 + 0.75 * i / max(n - 1, 1))


def label_end(ax, day, color, va_offset, fontsize=8):
    """Draw a dot + day-number label at the end of a segment."""
    ex, ey = day["lons"][-1], day["lats"][-1]
    ax.plot(ex, ey, "o", color=color, ms=4.5, zorder=6)
    ax.text(ex + 0.3, ey + va_offset, str(day["day"]),
            color=color, fontsize=fontsize, fontweight="bold", zorder=7,
            path_effects=[pe.withStroke(linewidth=2, foreground="#111827")])


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--calm",   default="/tmp/rolling_calm.geojson")
    parser.add_argument("--fading", default="/tmp/rolling_fading.geojson")
    parser.add_argument("--out",    default="fading_storm_comparison.png")
    args = parser.parse_args()

    calm_days   = load_days(args.calm)
    fading_days = load_days(args.fading)

    n_calm   = len(calm_days)
    n_fading = len(fading_days)

    calm_cmap   = matplotlib.colormaps["winter"]
    fading_cmap = matplotlib.colormaps["YlOrRd"]

    # ------------------------------------------------------------------ figure
    fig = plt.figure(figsize=(20, 13))
    fig.patch.set_facecolor("#111827")
    gs = gridspec.GridSpec(
        2, 1, height_ratios=[1, 3.2], hspace=0.38,
        top=0.95, bottom=0.06, left=0.07, right=0.97,
    )

    # ============================== TOP PANEL: intensity timeline ===========
    ax_top = fig.add_subplot(gs[0])
    ax_top.set_facecolor("#1f2937")
    for sp in ax_top.spines.values():
        sp.set_edgecolor("#374151")

    days_x    = np.arange(len(INTENSITY_PROFILE))
    sigwh_arr = np.array(INTENSITY_PROFILE) * MAX_SIGWH
    cost_arr  = 1.0 + sigwh_arr / 3.0   # beam-wind baseline; headwind adds up to +was/20

    # Bar colours match fading_cmap
    bar_colors = [segment_color(fading_cmap, i, len(INTENSITY_PROFILE))
                  for i in range(len(INTENSITY_PROFILE))]

    bars = ax_top.bar(days_x, sigwh_arr, color=bar_colors,
                      edgecolor="#374151", linewidth=0.8, zorder=3)

    # Annotate each bar
    for i, (bv, cv) in enumerate(zip(sigwh_arr, cost_arr)):
        ax_top.text(i, bv + 0.25, f"{bv:.1f} m\n×{cv:.2f}",
                    ha="center", va="bottom", fontsize=7.5,
                    color="white", fontweight="bold",
                    path_effects=[pe.withStroke(linewidth=1.5,
                                                foreground="#111827")])

    # Threshold line: cost ≥ 2× (sigwh = 6 m) — detour starts paying off
    threshold_sigwh = 6.0
    ax_top.axhline(threshold_sigwh, color="#facc15", lw=1.4,
                   linestyle="--", zorder=4)
    ax_top.text(len(INTENSITY_PROFILE) - 0.5, threshold_sigwh + 0.3,
                "Detour threshold\n(CCH cost ×2.0)",
                ha="right", va="bottom", fontsize=7.5,
                color="#facc15", fontweight="bold")

    ax_top.set_xlim(-0.5, len(INTENSITY_PROFILE) - 0.5)
    ax_top.set_ylim(0, MAX_SIGWH * 1.25)
    ax_top.set_xticks(days_x)
    ax_top.set_xticklabels(
        [f"Day {i}\n({'▲' if INTENSITY_PROFILE[i] > INTENSITY_PROFILE[i-1] else '▼'} storm)"
         if i > 0 else "Day 0\n(onset)"
         for i in days_x],
        color="white", fontsize=8,
    )
    ax_top.set_ylabel("Sig. wave height in\nstorm box (m)", color="#9ca3af", fontsize=8)
    ax_top.tick_params(colors="white", labelsize=8)
    ax_top.set_title(
        "Synthetic storm lifecycle — intensity builds over 7 daily forecasts then fades",
        color="white", fontsize=10, pad=6,
    )

    # Second y-axis: CCH cost factor
    ax_cost = ax_top.twinx()
    ax_cost.set_ylim(0, MAX_SIGWH * 1.25)
    cost_ticks = np.arange(0, MAX_SIGWH + 1, 3)
    ax_cost.set_yticks(cost_ticks)
    ax_cost.set_yticklabels([f"×{1 + v/6:.2f}" for v in cost_ticks],
                             color="#9ca3af", fontsize=7.5)
    ax_cost.set_ylabel("CCH edge cost factor", color="#9ca3af", fontsize=8)
    for sp in ax_cost.spines.values():
        sp.set_edgecolor("#374151")

    # ============================== BOTTOM PANEL: map =======================
    ax = fig.add_subplot(gs[1])
    ax.set_facecolor("#b8d8e8")
    for sp in ax.spines.values():
        sp.set_edgecolor("#374151")
    ax.tick_params(colors="white", labelsize=9)
    ax.set_xlabel("Longitude", color="#9ca3af", fontsize=10)
    ax.set_ylabel("Latitude",  color="#9ca3af", fontsize=10)

    # ---- Storm box: gradient fill per day (stacked semi-transparent rects)
    for i, intensity in enumerate(INTENSITY_PROFILE):
        # Each day's storm shrinks fractionally inward to create a "halo" effect
        pad = 0.4 * (1.0 - intensity) * 3  # more inset = lower intensity
        alpha = 0.06 + 0.10 * intensity
        rect = mpatches.FancyBboxPatch(
            (STORM_LON_MIN + pad, STORM_LAT_MIN + pad),
            (STORM_LON_MAX - STORM_LON_MIN) - 2 * pad,
            (STORM_LAT_MAX - STORM_LAT_MIN) - 2 * pad,
            boxstyle="square,pad=0",
            linewidth=0, facecolor="#ef4444",
            alpha=alpha, zorder=2,
        )
        ax.add_patch(rect)

    # Outer box outline
    outer = mpatches.FancyBboxPatch(
        (STORM_LON_MIN, STORM_LAT_MIN),
        STORM_LON_MAX - STORM_LON_MIN,
        STORM_LAT_MAX - STORM_LAT_MIN,
        boxstyle="square,pad=0",
        linewidth=1.8, edgecolor="#ef4444", facecolor="none", zorder=3,
    )
    ax.add_patch(outer)

    # Intensity labels along top of box
    for i, intensity in enumerate(INTENSITY_PROFILE):
        lx = STORM_LON_MIN + (STORM_LON_MAX - STORM_LON_MIN) * (i + 0.5) / len(INTENSITY_PROFILE)
        col = bar_colors[i]
        ax.text(lx, STORM_LAT_MAX + 0.8, f"D{i}\n{int(intensity*100)}%",
                ha="center", va="bottom", fontsize=6.5, color=col,
                fontweight="bold", zorder=5,
                path_effects=[pe.withStroke(linewidth=1.5, foreground="#111827")])

    # ---- Calm route
    for i, day in enumerate(calm_days):
        c = segment_color(calm_cmap, i, n_calm)
        ax.plot(day["lons"], day["lats"], color=c,
                lw=2.5, solid_capstyle="round", zorder=4)
        label_end(ax, day, c, va_offset=0.7, fontsize=8)

    # ---- Fading storm route
    for i, day in enumerate(fading_days):
        c = segment_color(fading_cmap, i, n_fading)
        ax.plot(day["lons"], day["lats"], color=c, lw=2.5,
                linestyle="--", solid_capstyle="round", zorder=4)
        label_end(ax, day, c, va_offset=-1.6, fontsize=8)

    # ---- Origin & destination stars
    ox, oy = calm_days[0]["lons"][0], calm_days[0]["lats"][0]
    dx, dy = calm_days[-1]["lons"][-1], calm_days[-1]["lats"][-1]
    for px, py, lbl in [(ox, oy, "New York"), (dx, dy, "London")]:
        ax.plot(px, py, "w*", ms=14, zorder=8)
        ax.text(px, py + 1.5, lbl, ha="center", color="white",
                fontsize=9, fontweight="bold", zorder=9,
                path_effects=[pe.withStroke(linewidth=2, foreground="#111827")])

    # ---- Draw latitude spread brackets for days 2, 3, 4 where divergence is clear
    for target_day in [2, 3, 4]:
        calm_match  = next((d for d in calm_days   if d["day"] == target_day), None)
        fading_match = next((d for d in fading_days if d["day"] == target_day), None)
        if calm_match is None or fading_match is None:
            continue
        cy = calm_match["lats"][-1]
        fy = fading_match["lats"][-1]
        bx = fading_match["lons"][-1] + 0.8  # bracket x position
        if abs(fy - cy) < 0.5:
            continue
        ax.annotate("", xy=(bx, cy), xytext=(bx, fy),
                    arrowprops=dict(arrowstyle="<->", color="#facc15",
                                   lw=1.2, linestyle="solid"),
                    zorder=9)
        ax.text(bx + 0.8, (cy + fy) / 2,
                f"+{abs(fy - cy):.1f}°N",
                color="#facc15", fontsize=7.5, va="center",
                fontweight="bold", zorder=10,
                path_effects=[pe.withStroke(linewidth=1.5, foreground="#111827")])

    # ---- Main divergence annotation on the biggest gap (day 3)
    first_diverge = next(
        (fd for cd, fd in zip(calm_days, fading_days)
         if abs(cd["lats"][-1] - fd["lats"][-1]) > 1.5),
        None,
    )
    if first_diverge is not None:
        fd_day = first_diverge["day"]
        calm_match = next(d for d in calm_days if d["day"] == fd_day)
        cx, cy = calm_match["lons"][-1], calm_match["lats"][-1]
        fx, fy = first_diverge["lons"][-1], first_diverge["lats"][-1]
        ax.annotate(
            f"Day {fd_day}: storm ×{1 + INTENSITY_PROFILE[fd_day] * MAX_SIGWH / 3:.2f} — "
            f"router shifts {abs(fy-cy):.1f}° north\nto minimise distance inside storm box",
            xy=(fx, fy), xytext=(fx - 18, fy + 6),
            color="#facc15", fontsize=8, fontweight="bold",
            arrowprops=dict(arrowstyle="->", color="#facc15", lw=1.5),
            zorder=10,
            path_effects=[pe.withStroke(linewidth=2, foreground="#111827")],
        )

    # ---- "Boiling frog" annotation near start
    ax.annotate(
        "Days 0-1: mild storm (≤30 %)\n— routes nearly identical",
        xy=(ox - 2, oy + 1),
        xytext=(ox - 16, oy + 8),
        color="#86efac", fontsize=8, fontweight="bold",
        arrowprops=dict(arrowstyle="->", color="#86efac", lw=1.3),
        zorder=10,
        path_effects=[pe.withStroke(linewidth=2, foreground="#111827")],
    )

    # ---- "Ship committed" annotation mid-route
    committed_day = next(
        (fd for fd in fading_days if fd["day"] >= 3), None
    )
    if committed_day:
        cx2 = committed_day["lons"][-1]
        cy2 = committed_day["lats"][-1]
        ax.annotate(
            f"Day {committed_day['day']}: peak storm (100 %)\n"
            "Ship is inside the box — escape\n"
            "south costs more than pushing east",
            xy=(cx2, cy2),
            xytext=(cx2 - 14, cy2 - 10),
            color="#fca5a5", fontsize=8, fontweight="bold",
            arrowprops=dict(arrowstyle="->", color="#fca5a5", lw=1.3),
            zorder=10,
            path_effects=[pe.withStroke(linewidth=2, foreground="#111827")],
        )

    # ---- Legend
    calm_dist  = sum(d["dist_nm"] for d in calm_days)
    fading_dist = sum(d["dist_nm"] for d in fading_days)
    calm_patch  = mpatches.Patch(
        facecolor=segment_color(calm_cmap, n_calm // 2, n_calm),
        label=f"Calm forecast  —  {calm_dist:.0f} nm, {n_calm} days")
    fading_patch = mpatches.Patch(
        facecolor=segment_color(fading_cmap, n_fading // 2, n_fading),
        label=f"Fading storm   —  {fading_dist:.0f} nm, {n_fading} days  (longer northern arc)")
    storm_patch = mpatches.Patch(
        facecolor="#ef444433", edgecolor="#ef4444", linewidth=1.5,
        label="Storm box  (opacity ∝ daily intensity)")
    ax.legend(
        handles=[calm_patch, fading_patch, storm_patch],
        loc="lower right",
        facecolor="#1f2937", labelcolor="white",
        fontsize=9, framealpha=0.92,
    )

    ax.set_xlim(-90, 20)
    ax.set_ylim(12, 75)
    ax.set_title(
        "Naive route vs fading storm  ·  New York → London\n"
        "Numbers = day index at end of each 24 h re-plan.  Dashes = fading storm route.",
        color="white", fontsize=11, pad=8,
    )

    plt.savefig(args.out, dpi=160, facecolor=fig.get_facecolor(),
                bbox_inches="tight")
    print(f"Saved: {args.out}")


if __name__ == "__main__":
    main()
