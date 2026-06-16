---
marp: true
theme: default
paginate: true
---

# maritime-router

**Fuel-optimal vessel routing at global scale**

C++23 · Customizable Contraction Hierarchies · Daily-average weather · Vessel physics

<!-- Speaker notes:
This system computes FOC-minimising routes for merchant vessels anywhere on the ocean,
using real pre-computed weather averages and per-vessel resistance physics.
Three independent programs (graph_builder, weather_etl, router_server) communicate
only through binary files, each running at its own cadence.
Performance summary: ~11.4 s per rolling-horizon day-period in Release build, ~1.2 s
fixed startup. Rotterdam→Singapore (28 days) completes in ~5 min on a laptop.
-->

---

# Stage 1 — Artifact Generation (Offline Build Pipeline)

![](diagrams/01_offline_build_pipeline.svg)

<!-- Speaker notes:
Runs once per data-source update — typically months between runs.
The diagram shows the full aspirational pipeline; the repo implements the core
graph topology (GEBCO bathymetry + GSHHG coastlines + NOAA ENC hazards + canal
waypoints) but the S-57 micro-validation and OpenSeaMap post-query checks shown
in the diagram are not yet in this codebase.
Key outputs: graph.bin, flags.bin, snap_wave.bin, snap_wind.bin, cch_topo.bin —
five static artifacts memory-mapped read-only at server startup.
Diagram note: "snap.bin" in the diagram is now two files (snap_wave.bin /
snap_wind.bin), one per weather grid, to match the real data's two distinct grids.
-->

---

# Stage 1 — What Gets Built

- **graph.bin** — CSR graph: ~2.6 M nodes, ~18 M edges, 0.25° global ocean grid
- **flags.bin** — per-node bitmask (ECA, TSS, restricted, canal, low-confidence wx)
- **snap_wave.bin / snap_wind.bin** — BFS nearest-ocean redirect per weather grid
- **cch_topo.bin** — RoutingKit CCH contraction order; built once, loaded in milliseconds
- Rebuilt only when GEBCO/GSHHG/ENC source data changes — months between runs

<!-- Speaker notes:
Two snap tables because the real weather data uses two distinct grids:
  Wave grid (sigwh, wsh, wsp, wsd, pwd, swell_residual): 621×1440, -75°→+80°, south-first
  Wind grid (was, wad): 721×1440, -90°→+90°, south-first
The snap tables redirect coastal graph nodes whose nearest weather cell is NaN (land)
to the nearest valid ocean cell via BFS. Built from sigwh.npy and was.npy NaN masks
by maritime-snap-builder — does not require GEBCO/GSHHG, runs in seconds.
CCH topology preprocessing is the slow step: 15–60 min on a typical server.
Serialised to cch_topo.bin so the router loads it in milliseconds on each restart.
-->

---

# Stage 2 — Weather Loading

![](diagrams/02_runtime_memory_layout.svg)

<!-- Speaker notes:
The diagram shows a live ETL updating every 6 hours. The current implementation uses a
static daily-average dataset (average_weather_description.md) covering all 366 days of
2024. The atomic<shared_ptr> swap and WeatherBuffer struct shown are accurate; the
"double-buffer standby" description applies whenever weights.bin is updated on disk and
QueryServer's poll thread detects the new file.
AvgWeatherLoader maps any calendar date to 2024's same month/day (2024 is a leap year —
every real calendar date, including Feb 29, resolves to a real file).
-->

---

# Stage 2 — Two-Grid WeatherBuffer

- **Wave fields** (621×1440, -75°→+80°, south-first): `sigwh`, `wsh`, `wsp`, `wsd`, `pwd`, `swell_residual`
- **Wind fields** (721×1440, -90°→+90°, south-first): `was`, `wad`
- `AvgWeatherLoader` reads float64 snapshots → narrows to float16, broadcasts across 24 hourly slots
- `base_epoch` in `weights.bin` = midnight UTC of the weather date
- `QueryServer` derives which day's weather to load from that `base_epoch` field

<!-- Speaker notes:
Old pipeline: 14 fields on a single 721×1440 north-first grid — wrong on shape,
orientation, and field list relative to the real NOAA source data.
Fields confirmed in the average dataset (see Section 3, average_weather_description.md):
sigwh, wsh, wsp, wsd, pwd, swell_residual (wave grid) + was, wad (wind grid) = 8 fields.
Fields not in the average dataset, removed from pipeline: pwh, pwp, pswh, pswp, pswd,
ocs/ocd (RTOFS ocean current), sst.
pwd (peak wave direction, energy-weighted by pwh²) IS present — it drives wave_resistance_kn().
swell_residual = sqrt(max(sigwh² - wsh², 0)) is loaded but not yet wired into any formula.
-->

---

# Stage 3 — Route Re-calculation (Rolling Horizon)

![](diagrams/03_query_flow.svg)

<!-- Speaker notes:
The query_flow diagram shows a FastAPI layer, S-57 micro-validation, and OpenSeaMap check.
These are present in the aspirational product architecture but not in this repo.
The actual entry point is plan_voyage() (voyage_planner.cpp), called via:
  - maritime-voyage-router CLI
  - maritime_router.plan_voyage() Python binding
The CCH bidirectional query step, edge-weight hot path (compute_edge_cost), and
vessel params shown in the diagram are all accurately implemented in this codebase.
Canal transit bypass (FLAG_CANAL_TRANSIT → calm-water FOC, no weather lookup)
is also implemented as shown.
-->

---

# Stage 3 — Rolling Horizon Algorithm

- For each day: `current_date = start_date + period_idx days`
- Load that date's average weather (`AvgWeatherLoader`, date→2024 mapping)
- Re-customise the CCH with `WeightsWriter::compute_blended()` — anisotropic Gaussian
- Extract shortest path from vessel's current position; advance ~24 h
- Repeat until destination reached or 366-day safety cap exhausted

<!-- Speaker notes:
compute_blended() integrates edge cost over all 24 hourly timesteps, weighting each by:
  g(u,ts) = exp(-½ [(d_along/σ_along)² + (d_perp/σ_perp)²])
  d_along = haversine(origin, u) − ts × service_speed_kts
A storm timed near the expected arrival time at node u gets full weight; storms at
other hours get attenuated proportionally. σ_along=200 nm (default, ~14 h at 14 kts),
σ_perp=400 nm (±6° latitude).
Since weather is daily-average (no intra-day variation), the Gaussian blending
within a day is between identical values — it still correctly weights which
day's weather is most relevant for a given node based on expected transit time.
The key discrimination is BETWEEN days (different calendar snapshots), not within a day.
-->

---

# Stage 3 — Speed-Loss Physics

- `calm_water_resistance_kn` — ITTC 1957 friction + Hollenbach residual resistance
- `wave_resistance_kn(sigwh, wave_rel_deg, vessel)` — Kreitner head-wave, 8-segment angle table
- `wind_resistance_kn(was, wind_rel_deg, vessel)` — Fujiwara aerodynamic drag
- `speed_loss_pct = 100 × (√(1 + ΔR/R_cw) − 1)` — equal-power Kwon model
- FOC = `universal_foc_model(wind_spd, service_speed_kts)` at the actual reduced speed

<!-- Speaker notes:
All physics in lib/include/maritime/edge_weight.hpp.
wave_rel_deg is computed from pwd ("going-to" convention) converted to "coming-from"
then folded into [0°, 180°] — 0° = head-on (worst resistance), 180° = following sea.
The Kreitner angle coefficient peaks at 22.5° (not 0°): coeffs = {1.0, 1.125, 1.0, 0.75, ...}.
weather_penalty_multiplier adds an exponential routing-cost amplifier above 4 m sigwh
to strongly discourage routing through severe conditions even at minimum speed (1 kt floor):
  below 4 m: 1×; at 8 m: ~11×; at 12 m: ~121×; at 15 m: ~735×.
-->

---

# Performance (Release build, real dataset, laptop)

| Voyage | Days | Wall time | Rate |
|---|---|---|---|
| Rotterdam → Irish Sea | 3 | 35.5 s | 11.8 s/day |
| Rotterdam → New York | 12 | 138.4 s | 11.5 s/day |
| Rotterdam → Singapore | 28 | 313.6 s | 11.2 s/day |

- ~11.4 s/day, ~1.2 s fixed startup — independent of voyage length
- Bottleneck: `compute_blended()` over ~18 M edges × 24 timesteps per day
- Python binding (`mr.plan_voyage`) matches CLI output exactly; 39 s for the 3-day test

<!-- Speaker notes:
All timings from Release build (-O3 -march=native), real 2024 average-weather dataset
on an external exFAT disk, full production graph (graph.bin = 56 MB).
Debug+ASan/UBSan build is 10-50× slower — use Release for any real-data runs.
The 11.4 s/day scales linearly because each day's compute_blended() is an independent
O(n_edges × 24) pass over the full graph, with no caching between days.
Parallelising the edge loop in compute_blended() across CPU cores is the single highest-
impact optimisation available.
Production extrapolation: 7-day voyage ≈ 82 s; 30-day ≈ 342 s (5.7 min).
Rotterdam→Singapore distance: 8837 nm — consistent with real-world Suez routing distance.
-->
