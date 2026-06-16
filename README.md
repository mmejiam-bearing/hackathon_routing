# maritime-router

A C++23 global maritime route optimisation system. Computes fuel-optimal (FOC-minimising) vessel routes using a Customizable Contraction Hierarchy (CCH) graph engine, daily-average NOAA GFS-Wave weather (see `average_weather_description.md`), and vessel-specific performance models.

---

## Quick start — run a voyage

Assumes `data/artifacts/` already contains the five static artifacts
(`graph.bin`, `flags.bin`, `snap_wave.bin`, `snap_wind.bin`, `cch_topo.bin`)
and that the average-weather dataset is mounted at the path shown (see
**Step-by-step: generating all artifacts** below for how to build them).

### C++

```bash
# Build (Release recommended for real-data runs)
cmake -B build_release -DCMAKE_BUILD_TYPE=Release
cmake --build build_release -j$(nproc || sysctl -n hw.logicalcpu) \
  --target maritime_voyage_router

# Route Rotterdam → Singapore, starting 2026-06-08
./build_release/router_server/maritime-voyage-router \
  --graph           data/artifacts/graph.bin              \
  --flags           data/artifacts/flags.bin              \
  --snap-wave       data/artifacts/snap_wave.bin          \
  --snap-wind       data/artifacts/snap_wind.bin          \
  --cch             data/artifacts/cch_topo.bin           \
  --avg-weather-dir /Users/mmejiam9206/exfat_mount/bearing_noaa/output \
  --start-date      2026-06-08                            \
  --from-lat 51.9   --from-lon 4.5                        \
  --to-lat    1.3   --to-lon  103.8                       \
  --out voyage.geojson
```

Expected output:

```
[voyage] Routing from (51.9, 4.5) to (1.3, 103.8) starting 2026-06-08 (average weather)
[voyage] Day 0: 330.0 nm
...
[voyage] Day 27: 123.7 nm
[voyage] Complete: 28 day(s)  total_dist=8837.0 nm  total_foc=29483.1 mt  total_time=675.7 h
[voyage] Written: voyage.geojson
```

### Python

```python
import sys
sys.path.insert(0, "router_server")   # so Python can find maritime_router.so
import maritime_router as mr

plan = mr.plan_voyage(
    graph_path     = "data/artifacts/graph.bin",
    flags_path     = "data/artifacts/flags.bin",
    snap_wave_path = "data/artifacts/snap_wave.bin",
    snap_wind_path = "data/artifacts/snap_wind.bin",
    cch_topo_path  = "data/artifacts/cch_topo.bin",
    avg_weather_dir= "/Users/mmejiam9206/exfat_mount/bearing_noaa/output",
    start_date     = "2026-06-08",
    origin_lat=51.9, origin_lon=4.5,
    dest_lat=1.3,    dest_lon=103.8,
)

print(f"{len(plan.segments)} days  "
      f"{plan.total_dist_nm:.0f} nm  "
      f"{plan.total_foc_mt:.0f} mt FOC  "
      f"{plan.total_time_h:.0f} h")
# → 28 days  8837 nm  29483 mt FOC  676 h
```

Or use the script directly:

```bash
python3 scripts/plan_voyage.py \
  --avg-weather-dir /Users/mmejiam9206/exfat_mount/bearing_noaa/output \
  --start-date 2026-06-08 \
  --from-lat 51.9 --from-lon 4.5 \
  --to-lat    1.3 --to-lon  103.8 \
  --out voyage.geojson
```

### Timing

Measured on Apple M-series (Release build, `-O3 -march=native`, real dataset on external
disk, full production graph ~2.6 M nodes / 18 M edges):

| Route | Voyage days | C++ wall time | Python wall time |
|---|---|---|---|
| Rotterdam → Irish Sea | 3 | **35.5 s** | **39.3 s** |
| Rotterdam → New York | 12 | **138.4 s** | ~153 s |
| Rotterdam → Singapore | 28 | **313.6 s** | ~345 s |

**~11.4 s per rolling-horizon day-period**, plus ~1.2 s fixed startup (graph mmap + CCH
topology load). C++ and Python follow the same code path (Python calls into the same C++
`plan_voyage()` via pybind11); the small overhead is Python interpreter startup and binding
dispatch. Debug builds with AddressSanitizer/UBSan are 10–50× slower — always use Release
for real-data runs.

---

## Three programs, one shared library

The system is split into three separate executables with distinct cadences and responsibilities. They communicate only through binary files in S3.

```
┌──────────────────────────────────────────────────────────────────────────────┐
│  graph_builder       (run once per data update — months between runs)         │
│                                                                                │
│  GEBCO + GSHHG + OpenSeaMap + NOAA ENC                                        │
│  + sigwh.npy (wave-grid NaN mask)  + was.npy (wind-grid NaN mask)            │
│      → graph.bin  flags.bin  snap_wave.bin  snap_wind.bin  cch_topo.bin      │
└────────────────────────────────────┬─────────────────────────────────────────┘
                                     │ (static artifacts — S3 / local disk)
                                     ▼
┌──────────────────────────────────────────────────────────────────────────────┐
│  weather_etl         (run for each voyage date — on demand or scheduled)      │
│                                                                                │
│  average-weather dataset  <YYYY>/<MM>/<DD>/{sigwh,wsh,wsp,wsd,pwd,…}.npy    │
│  + graph.bin/flags.bin/snap_*.bin                                              │
│      → weights.bin  (CCH edge weight vector, base_epoch = midnight UTC date) │
└────────────────────────────────────┬─────────────────────────────────────────┘
                                     │ (weights.bin — S3 / local disk)
                                     ▼
┌──────────────────────────────────────────────────────────────────────────────┐
│  router_server       (long-running query server)                               │
│                                                                                │
│  mmap: graph.bin  flags.bin  snap_wave.bin  snap_wind.bin  cch_topo.bin      │
│  poll: weights.bin   (atomic metric swap on new file)                          │
│  load: average-weather/<YYYY>/<MM>/<DD>/ keyed by weights.bin's base_epoch   │
│      → route queries via pybind11 (maritime_router.RoutingEngine)             │
└──────────────────────────────────────────────────────────────────────────────┘
```

![Offline Build Pipeline](diagrams/01_offline_build_pipeline.svg)

### Why three programs

The three programs have incompatible runtime characteristics:

| Program | Cadence | CPU profile | Memory |
|---|---|---|---|
| `graph_builder` | Months | CPU-bound for minutes–hours | Ephemeral |
| `weather_etl` | Per voyage date (on demand / scheduled) | I/O + compute burst | Ephemeral |
| `router_server` | Always running | Low CPU, high concurrency | ~2.2 GB resident |

The graph builder has no business being in the query server's process. The weather ETL has no business reading raw weather files inside the query server. Each program does exactly one job.

![Runtime Memory Layout](diagrams/02_runtime_memory_layout.svg)

---

## Repository layout

```
maritime-router/
├── CMakeLists.txt              Root — composes all targets, fetches dependencies
├── AGENTS.md                   Authoritative ruleset for agents and developers
├── README.md
├── SUMMARY.md                  Agent handoff document — design decisions and status
│
├── lib/
│   ├── CMakeLists.txt          Defines maritime_lib INTERFACE target
│   └── include/maritime/
│       ├── mmap_region.hpp     Rule of Five — the only type with a user destructor
│       ├── static_graph.hpp    Rule of Zero — memory-mapped graph + snap table
│       ├── weather_manager.hpp Rule of Zero — atomic<shared_ptr> double buffer
│       ├── edge_weight.hpp     Free functions — haversine, bearing, per-edge FOC
│       ├── query.hpp           Rule of Zero — time-expanded A* (fallback path)
│       ├── routing_engine.hpp  Rule of Zero — CCH facade, top-level server entry
│       └── weights_header.hpp  Shared WeightsHeader struct (weights.bin contract)
│
├── graph_builder/
│   ├── CMakeLists.txt
│   └── src/
│       ├── main.cpp             CLI entry point
│       ├── graph_builder.hpp/cpp  Pipeline orchestrator
│       ├── gebco_loader.hpp/cpp   GEBCO NetCDF bathymetry
│       ├── gshhg_masker.hpp/cpp   GSHHG land polygon masking
│       ├── canal_injector.hpp/cpp Hardcoded strait and canal waypoint chains
│       ├── snap_table_builder.hpp/cpp  BFS nearest-ocean for each weather grid
│       ├── cch_preprocessor.hpp/cpp   RoutingKit CCH topology build + save
│       ├── graph_serialiser.hpp/cpp   Write graph.bin and flags.bin
│       └── graph_builder.hpp    Public interface (BuildConfig + run())
│
├── weather_etl/
│   ├── CMakeLists.txt           Static library — no main(); called from Python
│   ├── etl.py                   Python ETL entry point (S3 or local mode)
│   └── src/
│       ├── avg_weather_loader.hpp/cpp  Load one day's average weather → WeatherBuffer
│       └── weights_writer.hpp/cpp     Compute + serialise weights.bin
│
├── router_server/
│   ├── CMakeLists.txt
│   └── src/
│       ├── main.cpp             CLI entry point + blocking loop
│       ├── server.hpp/cpp       QueryServer — owns RoutingEngine + polling thread
│       ├── weights_loader.hpp/cpp  Read weights.bin written by weather_etl
│       ├── route_query.cpp      Smoke-test binary — single static-weather query
│       └── voyage_router.cpp    Rolling-horizon multi-day voyage binary
│
└── tests/
    ├── CMakeLists.txt
    ├── unit/
    │   ├── test_haversine.cpp
    │   ├── test_bearing.cpp
    │   ├── test_snap_table.cpp
    │   ├── test_weather_buffer.cpp
    │   ├── test_mmap_region.cpp
    │   ├── test_edge_weight.cpp
    │   └── test_weights_writer.cpp
    └── integration/
        ├── test_graph_round_trip.cpp
        ├── test_snap_round_trip.cpp
        └── test_weights_round_trip.cpp
```

---

## Step-by-step: generating all artifacts

### Prerequisites

```bash
# macOS
brew install netcdf gdal cmake

# Ubuntu / Debian
sudo apt-get install zlib1g-dev libnetcdf-dev libgdal-dev cmake

# Data directories expected by the commands below
data/gebco/GEBCO_2023.nc          # GEBCO 2023 global bathymetry NetCDF
data/gshhg/GSHHS_shp/h/           # GSHHG high-resolution shapefiles
/path/to/average-weather/          # daily-average dataset (see average_weather_description.md)
                                   # layout: <YYYY>/<MM>/<DD>/<field>.npy, float64
```

### Step 1 — Build all targets

```bash
cmake -B build \
  -DnetCDF_DIR=/opt/homebrew/Cellar/netcdf/4.10.0/lib/cmake/netCDF \
  -DCMAKE_BUILD_TYPE=Release

cmake --build build -j$(nproc || sysctl -n hw.logicalcpu)
```

Produces five binaries:

```
build/graph_builder/maritime-graph-builder
build/weather_etl/maritime-weights-writer
build/router_server/maritime-router-server
build/router_server/maritime-route-query      # single-query smoke test
build/router_server/maritime-voyage-router    # rolling-horizon multi-day
```

### Step 2 — Build graph artifacts (run once per data update)

This step is slow (~15 minutes at 0.25°) because it runs RoutingKit inertial-flow
nested dissection over 700 K+ nodes.

```bash
./build/graph_builder/maritime-graph-builder \
  --gebco  data/gebco/GEBCO_2023.nc     \
  --gshhg  data/gshhg/GSHHS_shp/h      \
  --sigwh  /path/to/average-weather/2024/06/15/sigwh.npy \
  --was    /path/to/average-weather/2024/06/15/was.npy   \
  --out    data/artifacts/
```

Produces five files in `data/artifacts/`:

| File | Size | Contents |
|---|---|---|
| `graph.bin` | ~54 MB | CSR graph: lat/lon/depth per node, row_ptr/col_idx/base_dist per edge |
| `flags.bin` | ~715 KB | Per-node bitmask (ECA, TSS, restricted zones, canal transit) |
| `snap_wave.bin` | ~3.4 MB | Wave-grid (621×1440) BFS nearest-ocean snap table |
| `snap_wind.bin` | ~4.0 MB | Wind-grid (721×1440) BFS nearest-ocean snap table |
| `cch_topo.bin` | ~2.8 MB | RoutingKit CCH node order vector |

`snap_wave.bin` and `snap_wind.bin` can also be built without a full graph rebuild
using the standalone `maritime-snap-builder` binary:

```bash
./build/graph_builder/maritime-snap-builder \
  --sigwh    /path/to/average-weather/2024/06/15/sigwh.npy \
  --was      /path/to/average-weather/2024/06/15/was.npy   \
  --out-wave data/artifacts/snap_wave.bin                   \
  --out-wind data/artifacts/snap_wind.bin
```

Add `--no-restrictions` to disable the default Arctic passage restrictions
(useful for debugging; produces an unrealistic but unrestricted routing graph).

### Step 3 — Compute edge weights for a voyage date

Run once per voyage date before serving single-query lookups. Fast (~seconds).
The resulting `weights.bin` encodes `base_epoch` = midnight UTC of `--date`,
which `router_server` uses to derive which day's average weather to load.

```bash
./build/weather_etl/maritime-weights-writer \
  --graph           data/artifacts/graph.bin     \
  --flags           data/artifacts/flags.bin     \
  --snap-wave       data/artifacts/snap_wave.bin \
  --snap-wind       data/artifacts/snap_wind.bin \
  --avg-weather-dir /path/to/average-weather     \
  --date            2026-06-08                   \
  --out             data/artifacts/weights.bin
```

The `--step N` flag selects which hour `[0..23]` of the 24-hour buffer to use
as the reference sea state for CCH edge weights (default: 0).

### Step 4a — Single-query smoke test

Confirms the artifacts are self-consistent and the graph is routable.

```bash
./build/router_server/maritime-route-query \
  --graph           data/artifacts/graph.bin     \
  --flags           data/artifacts/flags.bin     \
  --snap-wave       data/artifacts/snap_wave.bin \
  --snap-wind       data/artifacts/snap_wind.bin \
  --cch             data/artifacts/cch_topo.bin  \
  --weights         data/artifacts               \
  --avg-weather-dir /path/to/average-weather     \
  --from-lat 51.9   --from-lon 4.5               \
  --to-lat    1.3   --to-lon  103.8
```

Prints waypoint count, total distance (nm), and FOC (MT), then outputs a
`lat,lon` CSV suitable for GeoJSON conversion. The weather loaded for FOC
reporting is keyed to the date encoded in `weights.bin`'s `base_epoch`.

### Step 4b — Rolling-horizon multi-day voyage

Produces a full voyage GeoJSON with per-day segments. For each day, the date
is computed as `start-date + N days` and that day's average weather is loaded
(mapped onto the 2024 dataset — see `average_weather_description.md`).

```bash
./build/router_server/maritime-voyage-router \
  --graph           data/artifacts/graph.bin     \
  --flags           data/artifacts/flags.bin     \
  --snap-wave       data/artifacts/snap_wave.bin \
  --snap-wind       data/artifacts/snap_wind.bin \
  --cch             data/artifacts/cch_topo.bin  \
  --avg-weather-dir /path/to/average-weather     \
  --start-date      2026-06-08                   \
  --from-lat 51.9   --from-lon 4.5               \
  --to-lat    1.3   --to-lon  103.8              \
  --speed    12.0                                \
  --out      voyage_rolling.geojson
```

![Route Query Flow](diagrams/03_query_flow.svg)

---

## Python bindings

The CLI binaries above (`maritime-route-query`, `maritime-voyage-router`) are
thin wrappers over the same C++ engine that's also exposed to Python via two
pybind11 extension modules. The bindings call the production code directly —
they do not reimplement anything — so a route computed from Python and a
route computed from the CLI for the same inputs are identical.

| Module | Source | Wraps |
|---|---|---|
| `maritime_router` | `router_server/python/bindings.cpp` | `RoutingEngine` / `QueryServer` (single query) and `plan_voyage()` (rolling multi-day) |
| `maritime_weather_etl` | `weather_etl/python/bindings.cpp` | `StaticGraph` and `WeightsWriter` (offline CCH weight computation) |

Both **minimise for weather and fuel consumption (FOC), not just distance**.
The CCH metric that the route is computed against is customised from the
vessel's weather-adjusted **speed loss** at every edge — see
[Weather in edge cost calculations](#weather-in-edge-cost-calculations) for
the formulas. Concretely:

- Each edge's traversal time is `distance / actual_speed`, where `actual_speed`
  is the vessel's service speed reduced by the Hollenbach calm-water
  resistance, Kreitner wave resistance, and Fujiwara wind resistance at that
  edge's sea state (`lib/include/maritime/edge_weight.hpp`,
  `compute_edge_cost()` / `actual_speed_kts()`).
- FOC is `universal_foc_model()` evaluated at that same weather-adjusted
  speed, so heavy weather costs more nm⁻¹ both because the vessel is slower
  *and* because of the cubic power-speed relationship.
- For the rolling-horizon voyage planner, the CCH is re-customised once per
  forecast period with `WeightsWriter::compute_blended()`, which integrates
  this speed-loss cost over all 24 forecast timesteps, weighted by an
  anisotropic Gaussian over where the vessel is likely to be — so the CCH
  doesn't just optimise for *today's* weather along the great-circle route,
  it accounts for where the storm is forecast to move while the vessel is
  still en route (see [Temporal weight blending](#temporal-weight-blending)).

Both modules build as part of the normal `cmake --build build` and are
written to the same directory as their `bindings.cpp` (`router_server/` and
`weather_etl/` respectively), so Python scripts that live under `scripts/`
need only add that directory to `sys.path` — no `pip install` or packaging
step required. `pybind11` itself is fetched by CMake (see root
`CMakeLists.txt`); building the modules requires the same Python headers
`pybind11` needs (`python3-config --includes` must succeed).

```bash
cmake --build build --target maritime_router_python maritime_weather_etl_python
```

### Single-shot routing — `scripts/route.py`

Routes from one lat/lon to another against the currently customised CCH
metric, returning the waypoint geometry **and** the weather sampled at each
leg (the same values that drove the speed-loss/FOC cost for that leg):

```bash
python3 scripts/route.py \
  --from-lat 51.9 --from-lon 4.5 \
  --to-lat    1.3 --to-lon  103.8 \
  --weights-dir data/artifacts \
  --avg-weather-dir /path/to/average-weather
```

Or directly from Python:

```python
import sys
sys.path.insert(0, "router_server")
import maritime_router as mr

engine = mr.RoutingEngine(
    graph_path="data/artifacts/graph.bin",
    flags_path="data/artifacts/flags.bin",
    snap_wave_path="data/artifacts/snap_wave.bin",
    snap_wind_path="data/artifacts/snap_wind.bin",
    cch_topo_path="data/artifacts/cch_topo.bin",
    weights_dir="data/artifacts",                      # must contain weights.bin
    avg_weather_dir="/path/to/average-weather",         # optional; enables FOC + weather samples
)

result = engine.route(
    origin_lat=51.9, origin_lon=4.5,
    dest_lat=1.3,    dest_lon=103.8,
    service_speed_kts=14.0,
)

result.status                                 # mr.Status.OK / NO_ROUTE / WEATHER_UNAVAILABLE
result.waypoint_lat, result.waypoint_lon      # numpy arrays, one entry per waypoint
result.sig_wh, result.wind_spd, result.wind_dir  # weather sampled per edge
result.wave_dir
result.total_dist_nm, result.total_foc_mt, result.total_time_h
```

`RoutingEngine` snaps each endpoint to its nearest graph node, then runs the
same `CchQueryState` query the server uses. `weights_dir` must contain a
`weights.bin` written by `weather_etl` (Step 3 above). `avg_weather_dir` is
optional — omit it to route on the CCH metric alone, with FOC totals and
weather samples left empty. When supplied, the weather date is derived from
`weights.bin`'s `base_epoch` (see Step 3).

### Rolling-horizon multi-day voyage — `scripts/plan_voyage.py`

The Python equivalent of `maritime-voyage-router`: loads average weather
day-by-day as the vessel progresses, re-customises the CCH for each day, and
advances the vessel ~24h per period until the destination is reached (see
[Rolling-horizon weather routing](#rolling-horizon-weather-routing)).

```bash
python3 scripts/plan_voyage.py \
  --avg-weather-dir /path/to/average-weather \
  --start-date 2026-06-08 \
  --from-lat 40.7 --from-lon -74.0 \
  --to-lat   51.5 --to-lon   -0.1 \
  --speed 14 \
  --out /tmp/rolling_calm.geojson
```

`mr.plan_voyage(...)` returns a `VoyagePlan` with `.segments` (one
`DaySegment` per ~24h period: `.day`, `.dist_nm`, `.foc_mt`, `.time_h`,
`.lat`/`.lon` waypoint arrays), plus `.total_dist_nm`, `.total_foc_mt`,
`.total_time_h`, and `.reached_destination`. The GeoJSON it writes is
byte-for-byte the same format `maritime-voyage-router --out` writes, so
`scripts/plot_rolling_voyage.py` consumes either one's output interchangeably.

### Weather ETL bindings — `maritime_weather_etl`

For the offline weight-computation side (`weather_etl/etl.py`):

```python
import sys
sys.path.insert(0, "weather_etl")
import maritime_weather_etl as mwe

graph = mwe.StaticGraph(
    "data/artifacts/graph.bin",
    "data/artifacts/flags.bin",
    "data/artifacts/snap_wave.bin",
    "data/artifacts/snap_wind.bin",
)
# Load one day's average weather (mapped onto the 2024 dataset)
wx = mwe.load_avg_weather_buffer(
    "/path/to/average-weather", year=2026, month=6, day=8,
    base_epoch=1780876800)                      # midnight UTC of 2026-06-08
weights = mwe.WeightsWriter.compute(graph, wx, ref_time_step=0)   # uint32 numpy array
mwe.WeightsWriter.write(weights, base_epoch=1780876800,
                         out_path="data/artifacts/weights.bin")
```

This is the vessel-agnostic proxy weight (distance × wave-height factor) used
to seed the CCH offline, per
[CCH weight heuristic — path selection](#1-cch-weight-heuristic--path-selection) —
not the per-vessel speed-loss cost `maritime_router` uses at query time.

---

## Hardcoded straits and canals

GEBCO classifies man-made canals (Suez, Panama, Kiel) and many narrow straits as
land or shallow water at 0.25° resolution (~28 km/cell). Without intervention
the graph has no path through these chokepoints, forcing every route around the
relevant continent.

### How it works

`graph_builder/src/canal_injector.cpp` defines nine **waypoint chains** —
sequences of WGS84 `{lat, lon}` coordinates that trace the navigable centreline
of each passage. All coordinates were sourced from the
[searoute-py / Marnet](https://github.com/genthalili/searoute-py) dataset
(Apache 2.0) and cross-checked against nautical charts.

| Passage | Waypoints | Min depth |
|---|---|---|
| Suez Canal | 6 | 24 m |
| Panama Canal | 11 | 13.7 m |
| Bosphorus | 7 | 27.5 m |
| Dardanelles | 4 | 18.0 m |
| Strait of Malacca | 2 | 23.0 m |
| Strait of Hormuz | 3 | 30.0 m |
| Strait of Gibraltar | 4 | 30.0 m |
| Bab-el-Mandeb | 2 | 30.0 m |
| Kiel Canal | 3 | 11.0 m |

During the graph build (`inject_canal_nodes` / `add_canal_edges_to_adj`):

1. Each waypoint is inserted as an artificial graph node tagged `FLAG_CANAL_TRANSIT`.
2. Consecutive waypoints within a chain are connected with bidirectional edges.
3. The **first and last** waypoints of each chain are connected to the *K* nearest
   ocean nodes on their respective sides (default K = 3), creating seamless
   transitions between open water and the canal corridor.

At routing time, `compute_edge_cost()` detects `FLAG_CANAL_TRANSIT` and bypasses
the weather lookup entirely, using a calm-water FOC model instead. This reflects
the sheltered, controlled conditions inside canals and enclosed straits.

### Geographic passage restrictions

In addition to canal injection, the graph builder applies **bounding-box
restrictions** that mark nodes as `FLAG_RESTRICTED` (impassable) to prevent
commercially unrealistic shortcuts:

| Zone | Latitude | Longitude | Rationale |
|---|---|---|---|
| Arctic polar cap | 75°N – 90°N | all | Transpolar routes are not commercially viable |
| Northwest Passage | 65°N – 75°N | 170°W – 60°W | Ice-covered Canadian archipelago |

These defaults mirror the passage restrictions in
[searoute-py](https://github.com/genthalili/searoute-py). They can be disabled
with `--no-restrictions` when building the graph.

---

## Rolling-horizon weather routing

A Rotterdam → Singapore voyage takes ~30 days at 12 knots. A single weather
snapshot is inadequate: conditions on day 14 (Red Sea) bear no relation to
conditions at departure. The rolling-horizon router solves this by
**re-optimising from the vessel's current position every 24 hours** using the
most current available forecast.

### Algorithm

```
current_node  = nearest graph node to origin
dest_node     = nearest graph node to destination
period        = 0

while current_node ≠ dest_node:

    effective = min(period, n_forecasts - 1)   # clamp when horizon is exhausted

    if effective changed since last iteration:
        load npy_dir[effective]  →  WeatherBuffer
        WeightsWriter::compute(graph, wx, ref_step=0)  →  weights vector
        RoutingEngine::update_weights(weights)          # CCH re-customized (~1–2 s)
        RoutingEngine::update_weather(wx)               # FOC buffer swapped atomically

    result = RoutingEngine::route(current_node → dest_node)

    follow result.node_path for 24 h of vessel travel:
        for each edge (from, to):
            elapsed_h += haversine(from, to) / speed_kts
            record waypoint
            if elapsed_h ≥ 24 h or to == dest_node:
                current_node = to
                break

    period++
```

### Forecast exhaustion

When the vessel has not yet reached the destination after all available forecast
cycles, the router continues with the **last available forecast** until arrival.
A log message marks the handover:

```
[voyage] Forecast exhausted after day 7 — extending with last available weather.
```

With the average-weather dataset, every day has its own distinct snapshot (keyed
by month/day, mapped onto 2024's values). No "forecast exhaustion" occurs —
the daily lookup always resolves to a real snapshot for that calendar day.

### Why path diversity appears

Each CCH re-customization reflects a genuinely different sea state. A gale in
the Bay of Biscay on day 3 raises edge costs along the Atlantic coast, pushing
the optimizer to hug the Portuguese coast more tightly — a path it would not
have chosen on the calm day-0 forecast. The path through the Suez Canal, Red
Sea, and Indian Ocean similarly shifts corridor-by-corridor as the forecast
evolves.

### Output format

`maritime-voyage-router` writes a GeoJSON `FeatureCollection` with one
`LineString` Feature per day. Each Feature carries:

```json
{
  "properties": {
    "day": 5,
    "dist_nm": 291.4,
    "foc_mt": 19.1
  }
}
```

Load in any GIS tool (QGIS, Kepler.gl, geojson.io) and colour by `day` to
visualise the daily segments and corridor shifts.

---

## Temporal weight blending

### The single-timestep limitation

The CCH customization step bakes one `uint32_t` weight into each edge. The rolling-horizon router calls `WeightsWriter::compute()` with `ref_time_step = 0` — the *current* forecast snapshot. The CCH therefore treats today's weather as permanent for the entire voyage. When a storm fades on day 2, the optimizer re-discovers a shorter route, but the vessel has already committed to the storm-avoidance corridor. The only correction available is a physically implausible U-turn.

### Mathematical derivation

Let `ρ(lat, lon, t)` be the probability density of the vessel's position at time `t`. It evolves under the advection-diffusion PDE:

```
∂ρ/∂t = D ∇²ρ − v · ∇ρ
```

where `v` is drift toward the destination and `D` is path diffusion (uncertainty in which corridor the optimizer selects). The correct edge weight is the *expected* traversal cost integrated over all plausible transit times:

```
w(u, v) = ∫₀ᵀ ρ(lat_u, lon_u, t) × cost(u, v, t) dt
```

**Anisotropic Gaussian approximation.** Rather than solving the PDE, approximate `ρ` as a Gaussian with two principal axes aligned to the route:

```
ρ(u, t) ≈ exp(-½ [(d_along(u,t)/σ_along)² + (d_perp(u)/σ_perp)²])
```

where:

| Symbol | Meaning | Default |
|---|---|---|
| `d_along(u,t)` | `haversine(origin, u) − t × service_speed_kts` [nm] | — |
| `d_perp(u)` | `\|cross_track_dist(origin→dest great circle, u)\|` [nm] | — |
| `σ_along` | Timing spread — vessel is on schedule to within ±σ_along/speed hours | 100 nm |
| `σ_perp` | Path-choice spread — optimizer may select any corridor within ±σ_perp | 300 nm |

The anisotropy (`σ_perp >> σ_along`) reflects reality: the vessel arrives roughly on schedule, but may take any of many parallel corridors.

**Discrete integral over 24 forecast timesteps:**

```
w(u,v) = Σ_{ts=0}^{23}  g(u,ts) × cost(u,v,ts)
         ─────────────────────────────────────────
                  Σ_{ts=0}^{23} g(u,ts)

g(u,ts) = exp(-½ [(d_along(u,ts)/σ_along)² + (d_perp(u)/σ_perp)²])
```

Note: `d_perp` is constant across timesteps for a given node, so it factors out of the ratio and cancels. Route preference inversion is driven by *along-track* position, not lateral offset.

When all 24 Gaussian values fall below `1e-6` (node is far outside the expected voyage window), the weight falls back to calm-water transit time.

### Implementation

`WeightsWriter::compute_blended()` in [weather_etl/src/weights_writer.cpp](weather_etl/src/weights_writer.cpp) implements the formula above. `voyage_router.cpp` calls it on every forecast reload, passing the current vessel position as origin and the fixed destination. As the vessel advances each day, the Gaussian naturally shifts forward — early-route nodes become irrelevant and late-route nodes gain weight.

### Computational cost

O(n_edges × 24) per planning horizon. For a 700 K-node graph (~4 M edges) and a 24-timestep forecast, this is ~100 M floating-point operations per day. On a modern CPU this takes about 2–5 seconds — well within the once-per-day budget of the rolling-horizon router.

---

## Advection-diffusion model: full derivation

This section derives the temporal blending formula from first principles, for readers familiar with PDEs, stochastic processes, and numerical linear algebra. Readers who only need the implementation can skip to [Temporal weight blending](#temporal-weight-blending).

### 1. Why a scalar edge weight is insufficient

The CCH customization step requires one `uint32_t` weight per directed edge. It is baked into the acceleration structure at forecast-reload time and used unchanged for every route query until the next reload. The fundamental question is:

> Which value of `cost(u, v, t)` — over which time `t` — should define `w(u, v)`?

Choosing `t = 0` (the current forecast snapshot) treats today's sea state as permanent for the entire voyage. For a node 2,000 nm ahead of the vessel, `t = 0` weather is the weather at departure, not at the time the vessel will actually traverse that edge (~6 days from now at 14 kts). This systematic mispricing causes the optimizer to commit to corridors that become sub-optimal as the forecast evolves, and produces U-turns when the CCH is re-customized under a new snapshot.

The correct weight is the **expected** traversal cost under the probability distribution of vessel position over time:

```
w(u, v) = E_ρ[cost(u, v, T(u))] = ∫₀^∞ ρ(xᵤ, t) × cost(u, v, t) dt
```

where `ρ(x, t)` is the probability density of the vessel's position at time `t`, and `T(u)` is the (random) time of first passage through node `u`. Everything that follows is a tractable approximation to this integral.

---

### 2. The forward Kolmogorov (Fokker-Planck) equation

Let the vessel's position on the ocean surface evolve as an Itô diffusion on ℝ²:

```
dXₜ = v(Xₜ, t) dt + √(2D) dWₜ
```

where `v(x, t)` is the deterministic drift field (the planned route direction at service speed), `D` is the diffusion coefficient (path uncertainty arising from corridor choice), and `Wₜ` is standard 2D Brownian motion. The law `ρ(x, t)` of this SDE satisfies the **forward Kolmogorov equation** (Fokker-Planck):

```
∂ρ/∂t = −∇·(v ρ) + D ∇²ρ
```

For a drift aligned purely along the great circle — `v = v₀ ê_ξ` where `ξ` is the along-track coordinate and `v₀` is the service speed — and with spatially constant `D`:

```
∂ρ/∂t = D ∇²ρ − v₀ ∂ρ/∂ξ
```

This is the standard linear advection-diffusion PDE. The advective term `−v₀ ∂ρ/∂ξ` translates the density rightward at speed `v₀`; the diffusive term `D ∇²ρ` spreads it isotropically. The density `ρ` is the probability of finding the vessel at position `x` at time `t`.

---

### 3. Exact solution and its intractability

**On ℝ¹ with constant coefficients**, the fundamental solution (Green's function) of the 1D advection-diffusion equation with initial condition `ρ(x, 0) = δ(x − x₀)` is:

```
G(x, t; x₀) = 1/√(4πDt) × exp(−(x − x₀ − v₀t)² / (4Dt))
```

This is a Gaussian that advects at velocity `v₀` and diffuses with variance `σ²(t) = 2Dt`. The along-track mean is `μ(t) = x₀ + v₀t`, exactly linear in `t`.

**On the ocean routing graph**, exact computation is intractable for three reasons:

1. **Irregular domain with variable coefficients**: The ocean surface is a subset of S² with complex irregular boundaries (coastlines). The diffusion coefficient `D` and the drift `v` are spatially variable and time-dependent (following-sea adds effective drift; head-sea reduces it). The eigenfunctions of the Laplace-Beltrami operator on this domain have no closed form, so spectral methods are ruled out.

2. **Non-autonomous system**: Weather changes the drift field `v(x, t)` over time. The PDE is non-autonomous, so the fundamental solution `G(x, t; x₀, s)` depends on both `t` and `s` separately (not just `t − s`). The solution must be integrated forward numerically.

3. **Dimensionality**: On the ~700K-node graph, the spatial discretization requires tracking a 700K-dimensional density vector. A single forward Euler step costs O(n_edges) ≈ O(4M) operations; a full 240-step integration (10 days at 1-hour resolution) costs O(1B) — and must be repeated for every forecast reload, for every edge query. The resulting computation is several orders of magnitude too slow.

---

### 4. Graph-Laplacian state-space form

The spatial discretization of the advection-diffusion PDE on the routing graph yields a linear autonomous ODE system. Let `ρ ∈ ℝⁿ` be the vector of probability mass at each node. The discrete system is:

```
dρ/dt = (D L − V) ρ
```

where:

| Symbol | Definition | Size |
|---|---|---|
| `L` | Normalized graph Laplacian: `L = Dₑ − A`, where `Dₑ` is the diagonal weighted-degree matrix and `A` is the adjacency matrix | n × n, sparse |
| `V` | Diagonal advection operator: `Vᵢᵢ = v₀ · hᵢ⁻¹`, where `hᵢ` is the local grid spacing at node `i` | n × n, diagonal |
| `D` | Scalar diffusion coefficient | scalar |

The formal solution with initial condition `ρ(0) = eₛ` (unit mass at the source node) is:

```
ρ(t) = exp((DL − V)t) · eₛ
```

This is the **matrix exponential** of `(DL − V)`. Since `L` is symmetric positive semi-definite (for an undirected graph) and `V` is a positive diagonal perturbation, the operator `(DL − V)` is stable (all eigenvalues have non-positive real part), ensuring the density remains normalizable.

**Why we do not use this directly**: Computing `exp(Mt)` for an `n × n` matrix `M` via Krylov subspace methods (Arnoldi/Lanczos) requires O(n × k) operations per matrix-vector product and k Krylov steps for accuracy. For `n = 700K` with `k ≈ 50` Krylov vectors and T = 240 timesteps, this costs approximately:

```
240 × 50 × 4M × (float ops per multiply-add) ≈ 48B operations per forecast reload
```

This exceeds the available compute budget by roughly 3 orders of magnitude. Furthermore, the operator `(DL − V)` changes on every forecast reload (because the edge weights in `L` depend on weather), making cached decompositions invalid.

---

### 5. Anisotropic Gaussian approximation

Rather than solving the full PDE, we approximate `ρ` as an **anisotropic Gaussian** in a route-aligned coordinate frame. This approximation is exact on ℝ¹ with constant coefficients (§3) and degrades gracefully on the curved, irregular ocean domain.

**Route-aligned frame**: Define two orthogonal axes at each point along the great circle O→D:
- `ξ`: along-track coordinate — signed distance along the great circle from O [nm]
- `η`: cross-track coordinate — perpendicular distance from the great circle [nm]

In this frame, the approximate density at time `t` (= `ts × Δt`, discrete timestep) is:

```
ρ(ξ, η, ts) ∝ exp(−½ [(ξ − ts × v₀)² / σ_along² + η² / σ_perp²])
```

The along-track mean `μ(ts) = ts × v₀` advances linearly — consistent with the exact Green's function. The standard deviations `σ_along` and `σ_perp` are **time-independent**, which is a simplification (in the exact solution `σ_along(t) = √(2Dt)` grows with `t`). Using fixed `σ` replaces the time-growing variance with a *worst-case* spread over the relevant portion of the voyage horizon.

**Physical interpretation of the anisotropy** (`σ_perp >> σ_along`):
- `σ_along` controls **timing uncertainty**: the vessel is on schedule to within ±σ_along/v₀ hours. At `σ_along = 200 nm` and 14 kts, this is ±14 hours — a realistic scheduling spread over multi-day voyages.
- `σ_perp` controls **path-choice uncertainty**: the CCH may select any of many parallel corridors spanning ±σ_perp cross-track. At `σ_perp = 400 nm`, this spans ±6.7° latitude at 50°N, covering the full corridor width of the North Atlantic.

The diffusion process in the cross-track direction is much weaker than the advection in the along-track direction: `D_perp ≪ v₀ × σ_along`. This justifies treating the cross-track spread as approximately constant, governed by optimizer diversity rather than physical diffusion.

**Evaluation at graph nodes**: For node `u = (lat_u, lon_u)` at forecast timestep `ts`:

```
g(u, ts) = exp(−½ [(d_along(u, ts) / σ_along)² + (d_perp(u) / σ_perp)²])

d_along(u, ts) = haversine(O, u) − ts × v₀        [nm]
d_perp(u)      = |cross_track_nm(O→D great circle, u)|   [nm, constant in ts]
```

The function `d_along(u, ts) = 0` when `ts = haversine(O, u) / v₀`, i.e., when the vessel's expected position coincides with node `u`. This is the Gaussian's peak timestep for node `u`.

---

### 6. Discrete integral and the blended weight formula

Substituting the Gaussian approximation into the expected-cost integral and discretizing over `T = 24` forecast timesteps (one per hour of the NOAA GFS cycle):

```
w(u, v) = Σ_{ts=0}^{T-1} g(u, ts) × cost(u, v, ts)
           ──────────────────────────────────────────
                    Σ_{ts=0}^{T-1} g(u, ts)
```

This is the **Gaussian-weighted time average** of the traversal cost. Timesteps where node `u` falls near the vessel's expected position receive high weight; timesteps where node `u` is far from the expected position are down-weighted exponentially.

The per-timestep traversal cost is:

```
cost(u, v, ts) = (base_nm / actual_speed(wx[u,ts], hdg, vessel)) × P(sig_wh[u,ts])
```

where `actual_speed` computes the Hollenbach/Kreitner/Kwon physics-based speed (see §8 below) and `P(sig_wh)` is the exponential penalty multiplier (see §7 below).

**Truncation**: Gaussian values below `1e-6` are skipped. At `σ_along = 200 nm` and `v₀ = 14 kts`, the Gaussian for node `u` is non-negligible (> 1e-6) for timesteps `ts ∈ [t_mean − 5σ_along/v₀, t_mean + 5σ_along/v₀]`, where `t_mean = haversine(O, u) / v₀`. At `σ_along = 200 nm`, this window is ±71 hours ≈ ±3 days. Since the forecast is only 24 timesteps, the effective window is always contained within the forecast horizon for nodes within 3 days of travel from the origin.

**Fallback**: If all 24 Gaussian weights are below `1e-6` (node `u` is far outside the expected voyage window — either ahead of the destination or far behind the origin), the blended weight falls back to calm-water transit time `base_nm / v₀`. This ensures well-defined weights for all graph nodes regardless of voyage geometry.

---

### 7. Algebraic cancellation of σ_perp

The cross-track term `d_perp(u)² / σ_perp²` is **constant across all timesteps** for a fixed node `u`. Factor it from the summation:

```
w(u,v) = [C × Σ_ts exp(−½ d_along²/σ_along²) × cost_ts]
          ────────────────────────────────────────────────
          [C × Σ_ts exp(−½ d_along²/σ_along²)           ]

where C = exp(−½ d_perp²/σ_perp²)
```

`C` cancels from numerator and denominator. The blended weight `w(u, v)` is **independent of σ_perp**.

**Consequence for route design**: Two paths P₁ and P₂ that differ only in cross-track offset (one goes 200 nm north of the great circle, the other 200 nm south) have identical Gaussian weight profiles across all timesteps — because their `d_along` sequences are identical. The temporal blending assigns them equal cost, regardless of `σ_perp`. Route preference inversion between P₁ and P₂ can only arise if they differ in **along-track distance from the origin**, i.e., if they arrive at their respective "branch points" at different times, exposing them to different dominant timesteps and thus different weather conditions.

This is the correct physical result: the optimizer should distinguish routes by their temporal exposure to weather events, not by their lateral offset within a calm corridor.

---

### 8. Speed-loss physics and the exponential penalty

**Speed-loss model** (`actual_speed_kts()` in `weights_writer.cpp`):

The vessel's speed through water is reduced by wave-induced added resistance via the **Kwon formula** (equal-power assumption):

```
ΔR = R_wave(sig_wh, θ_wave_rel) + R_wind(was, θ_wind_rel)
speed_loss% = 100 × (√(1 + ΔR/R_cw) − 1)
actual_speed = max(v_min, v₀ × (1 − speed_loss%/100))
```

where `R_cw` is calm-water resistance (Hollenbach formula), `R_wave` is wave resistance (Kreitner formula, 8-segment angular table), `R_wind` is aerodynamic resistance (Fujiwara formula), and `v_min = 1 kt` is a safety floor.

At `sig_wh = 15 m` with head seas, `ΔR/R_cw ≈ 36`, giving `speed_loss% ≈ 100%` and `actual_speed = 1 kt`. The edge cost is therefore 14× above calm water — significant, but still finite and passable.

**Exponential penalty** (`weather_penalty_multiplier()` in `edge_weight.hpp`):

```
P(sig_wh) = exp(κ × max(0, sig_wh − θ))     κ = 0.6 neper/m,  θ = 4 m
```

This is a **log-domain barrier** in the optimal control sense. In interior-point methods, a log-barrier `−δ × log(c − x)` prevents a trajectory from reaching the constraint boundary `x = c`. Here, the constraint is "the vessel cannot make progress in sig_wh ≥ 15 m hurricane conditions." The exponential barrier grows without bound as `sig_wh → ∞`, making conditions above the threshold progressively more costly while remaining finite (the optimizer can still route through moderate weather if no bypass exists).

The combined effect:

| sig_wh | Speed loss | Penalty P | Net cost multiplier vs calm |
|---|---|---|---|
| 0 m   | 0%   | 1×    | 1×      |
| 4 m   | ~15% | 1×    | ~1.2×   |
| 8 m   | ~50% | ~11×  | ~22×    |
| 12 m  | ~85% | ~121× | ~807×   |
| 15 m  | 100% | ~735× | ~10290× |

At 12 m sig_wh, any detour shorter than ~807× the storm path distance is preferred. For a 500 nm storm stretch, any bypass adding less than ~400,000 nm is cheaper — effectively forcing avoidance. At 15 m, the edge weight is clamped to `MAX_W = RoutingKit::inf_weight − 1`, making the edge near-impassable.

---

### 9. Implementation summary

| Component | Location | Role |
|---|---|---|
| `compute_blended()` | `weather_etl/src/weights_writer.cpp` | Main blending loop: computes `g(u,ts)`, accumulates weighted cost |
| `actual_speed_kts()` | `weather_etl/src/weights_writer.cpp` | Speed-loss physics (Hollenbach + Kreitner + Kwon) |
| `weather_penalty_multiplier()` | `lib/include/maritime/edge_weight.hpp` | Exponential barrier term |
| `cross_track_nm()` | `lib/include/maritime/edge_weight.hpp` | Computes `d_perp(u)` via spherical trigonometry |
| `along_track_nm()` | `lib/include/maritime/edge_weight.hpp` | Computes along-track projection (used in tests) |
| `voyage_router.cpp` | `router_server/src/voyage_router.cpp` | Calls `compute_blended()` daily with updated origin |

The origin passed to `compute_blended()` updates each day as the vessel advances along the route. This shifts the Gaussian forward — nodes already traversed become negligible, nodes approaching become dominant — without requiring any explicit state machine.

---

## Weather in edge cost calculations

Weather influences routing at **two separate points** in the pipeline with different roles and different files. It is important to understand both, because changing one without the other produces inconsistent results.

### 1. CCH weight heuristic — path selection

**File:** `weather_etl/src/weights_writer.cpp`, function `WeightsWriter::compute()`

**When it runs:** offline, once per voyage date (or once per day in the rolling-horizon router)

**Formula:**
```cpp
const float sig_wh  = static_cast<float>(wx.sigwh[wx_idx]);
const float proxy   = base_nm * (1.f + sig_wh / 6.f);
const uint32_t w    = static_cast<uint32_t>(std::min(proxy * 1e3f, MAX_W));
```

This scales each edge's great-circle distance by a wave-height factor. Calm water (`sig_wh = 0`) leaves the cost equal to distance. A 6 m swell doubles the effective cost. The resulting `uint32_t` vector is passed to the RoutingKit CCH customizer, which bakes it into the acceleration structure used for all path queries.

**Role:** determines which **path** the optimizer chooses. Higher edge costs in rough seas steer the CCH toward calmer corridors.

**What weather variables are used:** `sigwh` only (significant combined wave height).

**How to change it:** edit the proxy formula in `WeightsWriter::compute()`. For example, to also penalize strong headwinds:

```cpp
const float sig_wh   = static_cast<float>(wx.sigwh[wx_idx]);
const float wind_spd = static_cast<float>(wx.was[wx_idx]);
const float proxy    = base_nm * (1.f + sig_wh / 6.f + wind_spd / 20.f);
```

Any variable already present in `WeatherBuffer` (`sigwh`, `was`, `ocs_u`, `ocs_v`, `pwp`, etc.) can be incorporated here without structural changes.

### 2. FOC accumulation heuristic — cost reporting

**File:** `lib/include/maritime/edge_weight.hpp`, function `compute_edge_cost()`

**When it runs:** at query time, once per edge in the route path

**Formula:**
```cpp
// Weather lookup (O(1), snap table already applied)
const float sig_wh      = static_cast<float>(wx.sigwh[wx_idx]);
const float wind_spd    = static_cast<float>(wx.was[wx_idx]);
const float cur_u       = static_cast<float>(wx.ocs_u[wx_idx]);
const float cur_v       = static_cast<float>(wx.ocs_v[wx_idx]);

// Project current onto vessel heading
const float current_comp = cur_u * sin(hdg) + cur_v * cos(hdg);

// Inject vessel model
const auto [speed_kts, foc_per_nm] =
    vessel.foc_model(sig_wh, wind_spd, current_comp);

return foc_per_nm * dist_nm * eca_factor + tss_penalty;
```

**Role:** computes the **reported fuel cost** (MT) of each edge. It uses `sig_wh`, wind speed, and ocean current projected onto the vessel heading. The current component is signed: a following current reduces cost, a head current increases it.

**What weather variables are used:** `sigwh`, `was` (wind speed), `ocs_u`/`ocs_v` (ocean current U/V components).

**How to change it:** the `vessel.foc_model` callable in `VesselParams` (defined in `lib/include/maritime/edge_weight.hpp`) is injected at call sites and is the intended extension point. Replacing the flat lambda with a physics-based model requires no changes to `compute_edge_cost()` itself:

```cpp
vessel.foc_model = [](float sig_wh, float wind_spd, float current_comp)
    -> std::pair<float, float>
{
    // Example: speed decreases with wave height (Beaufort-style)
    const float speed = std::max(4.f, 14.f - sig_wh * 0.8f - wind_spd * 0.1f);
    const float foc   = base_daily_consumption / 24.f / speed;
    return {speed, foc};
};
```

To consume additional weather variables (e.g. wave period `pwp` for resonance effects) the signature of `foc_model` and `compute_edge_cost()` would need to be extended to pass those values through.

### The relationship between the two

The two heuristics are **independent computations** operating on the same weather data. This creates an important constraint: **they must agree on what makes an edge expensive**.

If the FOC model heavily penalises head seas but the CCH weight heuristic only penalises wave height, the CCH will choose a path it considers optimal that the FOC model then reports as expensive. The route looks good to the optimizer but bad to the cost reporter.

The ideal state is for the CCH weight formula to be a **fast, integer approximation** of what `compute_edge_cost()` would return — close enough that the shortest CCH path is also the lowest-FOC path. When you update the FOC model, update the CCH weight formula to match.

| | CCH weight heuristic | FOC accumulation |
|---|---|---|
| **File** | `weather_etl/src/weights_writer.cpp` | `lib/include/maritime/edge_weight.hpp` |
| **Output type** | `uint32_t` (integer, RoutingKit-compatible) | `float` (MT of fuel) |
| **Runs** | Offline, once per voyage date | At query time, per edge in path |
| **Controls** | Which path is chosen | What the path costs |
| **Weather variables** | `sigwh`, `was`, `wad`, `pwd` | `sigwh`, `was`, `wad`, `pwd` |
| **Extension point** | Edit proxy formula directly | Replace `VesselParams::foc_model` lambda |

---

## Current FOC model

### What the model does today

The FOC model is a `std::function` stored inside `VesselParams` (defined in
`lib/include/maritime/edge_weight.hpp`). It is injected at the call site before
each route query and is intentionally decoupled from the routing engine — the
engine never knows what vessel it is routing for, only that it can call
`foc_model(sig_wh, wind_spd, current_comp)` to get `{speed_kts, foc_per_nm}`.

The placeholder currently in use in both `route_query.cpp` and
`voyage_router.cpp` ignores all three arguments:

```cpp
vessel.foc_model = [](float /*sig_wh*/, float /*wind_spd*/, float /*current_comp*/)
    -> std::pair<float, float>
{
    return {12.f, 20.f / 24.f / 12.f};
    //      ^^^^  ^^^^^^^^^^^^^^^^^^^^^
    //      12 knots flat (speed never changes)
    //            20 MT/day ÷ 24 h ÷ 12 kts = 0.0694 MT/nm (rate never changes)
};
```

**Effect on routing:** because `speed_kts` is constant, elapsed travel time is
proportional to distance, and `foc_per_nm` is constant, the reported FOC is
simply proportional to the total route distance. Weather variables `sig_wh`,
`wind_spd`, and `current_comp` are received by the function but discarded. The
route path is influenced by weather only through the CCH weight heuristic
(`sigwh` scaling in `weights_writer.cpp`); the reported fuel cost is
weather-blind.

### Where to change it

There is **one place** per binary where the model is injected:

| Binary | File | Line |
|---|---|---|
| `maritime-route-query` | `router_server/src/route_query.cpp` | `vessel.foc_model = ...` |
| `maritime-voyage-router` | `router_server/src/voyage_router.cpp` | `vessel.foc_model = ...` |

In production (via pybind11 / FastAPI) the lambda would be populated from a
Python-side vessel model before each query, so only the binding layer changes.
The C++ engine and `compute_edge_cost()` are unchanged.

No changes are required to `lib/include/maritime/edge_weight.hpp` to improve the
model — the function signature already receives all the necessary environmental
inputs.

### What needs to change for the future implementation

The future improvements described in [Future improvements](#future-improvements)
require two coordinated changes.

#### Change 1 — Speed-loss model inside `foc_model`

Replace the fixed `12.f` speed with a physics-based estimate that accounts for
wave-induced added resistance. A minimal parametric form:

```cpp
vessel.foc_model = [](float sig_wh, float wind_spd, float current_comp)
    -> std::pair<float, float>
{
    // Speed through water: starts at design speed, reduced by sea state.
    // current_comp > 0 = following current (increases effective SOG).
    const float design_speed  = 14.f;                      // kts, calm water
    const float wave_penalty  = sig_wh * 0.8f;             // kts lost per metre of Hs
    const float wind_penalty  = std::max(0.f, wind_spd - 10.f) * 0.05f;
    const float speed_stw     = std::max(4.f, design_speed - wave_penalty - wind_penalty);
    const float speed_sog     = speed_stw + current_comp * 1.944f; // m/s → kts

    // FOC: engine works harder to overcome wave resistance → same power, less speed.
    const float base_daily_mt = 45.f;                      // MT/day at design speed
    // Power ~ speed³; same power at reduced speed means same daily consumption.
    const float foc_per_nm    = base_daily_mt / 24.f / std::max(speed_sog, 1.f);

    return {speed_sog, foc_per_nm};
};
```

The parameters (`design_speed`, `wave_penalty`, `base_daily_mt`) should come
from vessel-specific trial data or a calibrated performance model rather than
being hardcoded.

#### Change 2 — CCH weight heuristic must be updated to match

Because the CCH weight formula in `weather_etl/src/weights_writer.cpp` controls
which **path** is chosen, it must reflect the same speed-loss logic. If the FOC
model now makes head-sea edges significantly more expensive, the CCH must also
penalise them — otherwise the optimizer selects a path that the FOC model rates
as poor.

The weight proxy should approximate what `foc_model` would return for a typical
transit, expressed as an integer cost. For example:

```cpp
// In WeightsWriter::compute(), replace the existing proxy line:
// Before:
const float proxy = base_nm * (1.f + sig_wh / 6.f);

// After (consistent with the speed-loss model above):
const float wave_penalty = sig_wh * 0.8f;
const float speed        = std::max(4.f, 14.f - wave_penalty);
const float foc_per_nm   = 45.f / 24.f / speed;
const float proxy        = foc_per_nm * base_nm;
```

This makes the CCH optimization objective (minimize proxy cost) and the reported
FOC objective (minimize `foc_per_nm × dist_nm`) consistent, so the path the
optimizer chooses is also the path the FOC model considers cheapest.

#### Summary of files to touch

| Step | File | Change |
|---|---|---|
| Speed-loss + FOC model | `router_server/src/route_query.cpp` | Replace `foc_model` lambda |
| Speed-loss + FOC model | `router_server/src/voyage_router.cpp` | Replace `foc_model` lambda |
| CCH weight alignment | `weather_etl/src/weights_writer.cpp` | Update `proxy` formula to match |
| (optional) Extra variables | `lib/include/maritime/edge_weight.hpp` | Extend `compute_edge_cost()` signature if wave period or direction is needed |

The `VesselParams` struct and the `foc_model` signature do not need to change —
the three existing arguments (`sig_wh`, `wind_spd`, `current_comp`) are
sufficient for a first-order speed-loss model.

---

## RAII contract

**Rule of Five** — `MmapRegion` only. `mmap()` returns OS-managed virtual memory that no standard smart pointer can manage. Copy deleted; move nulls the source pointer.

**Rule of Zero** — everything else. `StaticGraph`, `WeatherManager`, `CchIndex`, `RoutingEngine`, `WeatherBuffer`, `QueryServer`, `WeightsWriter` all compose from standard library types whose compiler-generated lifecycle is correct.

**Explicit delete** — `QueryState` deletes copy because copying a partially-explored A\* state produces inconsistent results. Move is compiler-generated.

See `AGENTS.md` for the full enforcement rules.

---

## Weather grid

Two distinct grids, both **south-first** (row 0 = south), 0.25° resolution,
lon 0→359.75°E — see `average_weather_description.md`:

```
Wave grid    sigwh, wsh, wsp, wsd, pwd, swell_residual    (621, 1440)   -75°..+80°
Wind grid    was, wad                                      (721, 1440)   -90°..+90°
Dtype:       float64 on disk (average dataset) → float16 in WeatherBuffer
NaN:         land mask — redirected via snap_wave.bin / snap_wind.bin, never left as NaN
Timesteps:   24 slots per WeatherBuffer (daily average broadcast uniformly)
```

Total per `WeatherBuffer`: wave fields (6 × 24 × 894,240 × 2 bytes) + wind fields
(2 × 24 × 1,038,240 × 2 bytes) ≈ 340 MB.

**Narrow-strait coverage** — at 0.25° resolution (~28 km/cell), several chokepoints fall on land cells:

| Location | Issue | Resolution |
|---|---|---|
| Singapore Strait | ALL\_LAND | Snap table → nearest ocean cell |
| Strait of Malacca S | ALL\_LAND | Snap table → nearest ocean cell |
| Tokyo Bay approach | ALL\_LAND | Snap table → nearest ocean cell |
| Panama, Suez, Kiel, Bosphorus | Enclosed | `FLAG_CANAL_TRANSIT` → calm-water FOC |

---

## Binary file formats

### `graph.bin`

```
[GraphHeader: magic=0x4752414D "MARG", version=1, n_nodes, n_edges]
[float32  lat[n_nodes]]         lon stored in −180..180
[float32  lon[n_nodes]]
[uint16   depth[n_nodes]]       float16 depth in metres, positive down
[uint32   row_ptr[n_nodes+1]]   CSR row pointer array
[uint32   col_idx[n_edges]]     CSR destination node indices
[float32  base_dist_nm[n_edges]] great-circle distances, weather-independent
```

### `flags.bin`

One `uint8_t` per graph node, bitmask:

```
FLAG_ECA            0x01   Emission Control Area
FLAG_TSS            0x02   Traffic Separation Scheme
FLAG_RESTRICTED     0x04   RESARE / ATBA
FLAG_CANAL_TRANSIT  0x08   Enclosed waterway — suppress weather lookup
FLAG_LOW_CONF_WX    0x10   Snap distance >2 cells — warn in API response
```

### `snap_wave.bin` / `snap_wind.bin`

Two snap tables, one per weather grid. Same binary layout:

```
[SnapHeader: magic=0x50414E53 "SNAP", version=1, n_lat, n_lon]
[uint16  snap_lat[n_lat × n_lon]]
[uint16  snap_lon[n_lat × n_lon]]
```

`snap_wave.bin`: n_lat=621, n_lon=1440 — built from `sigwh.npy` NaN mask.
`snap_wind.bin`: n_lat=721, n_lon=1440 — built from `was.npy` NaN mask.
BFS nearest-ocean; identity for cells that are already ocean.

### `cch_topo.bin`

RoutingKit's internal `CustomizableContractionHierarchy` serialisation format. Written by `CchPreprocessor::save()` in `graph_builder`. Loaded by `CchIndex(path)` in `routing_engine.hpp`. Fast load (~milliseconds); avoids rebuilding the topology (~minutes) on each server restart.

### `weights.bin`

```
[WeightsHeader: magic=0x54484757 "WGHT", version=1, n_edges, reserved, base_epoch]
[uint32  weight[n_edges]]   proxy FOC cost scaled by 1e3
```

Written by `WeightsWriter` in `weather_etl`. Read by `WeightsLoader` in `router_server`. The interface contract between the two programs.

---

## Building

### Requirements

- GCC ≥ 13 or Clang ≥ 17, C++23
- CMake ≥ 3.28
- x86-64 with AVX (`-march=native` for float16 → float32 `vcvtph2ps`)
- Linux (`mmap`, `MAP_POPULATE`)
- `zlib` development headers (required by RoutingKit)
- Internet access at first build (FetchContent clones RoutingKit + GoogleTest)

```bash
sudo apt-get install zlib1g-dev
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

---

## Running tests

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
cd build && ctest --output-on-failure
```

Unit tests run in < 1 second, require no artifacts, no network. Integration tests write synthetic binary files to `fs::temp_directory_path()` and clean up after themselves.

---

## Data sources

All sources are freely available; no commercial chart licensing required.

| Artifact | Primary source | Licence |
|---|---|---|
| Bathymetry (`graph.bin` depth) | [GEBCO 2023](https://www.gebco.net/data_and_products/gridded_bathymetry_data/) | Open |
| Land mask | [GSHHG](https://www.soest.hawaii.edu/pwessel/gshhg/) | LGPL |
| Coastlines | [OSM coastlines](https://osmdata.openstreetmap.de/data/coastlines.html) | ODbL |
| Base topology | [searoute-py / Marnet](https://github.com/genthalili/searoute-py) | Apache 2.0 |
| Hazards | [OpenSeaMap](https://www.openseamap.org) | CC BY-SA |
| US coastal charts | [NOAA ENC](https://charts.noaa.gov/ENCs/ENCs.shtml) | Public domain |
| Port locations | [NGA World Port Index](https://msi.nga.mil/Publications/WPI) | Public domain |
| Routing measures EU | [EMODnet Human Activities](https://www.emodnet-humanactivities.eu) | Free |
| ECA zones | MARPOL Annex VI / EPA | Public domain |
| Weather arrays | NOAA GFS wave model, processed `.npy` | Public domain |
| CCH implementation | [RoutingKit](https://github.com/RoutingKit/RoutingKit) | BSD-2-Clause |

---

## Performance

On x86-64 with AVX, `r5.large`-class server (32 vCPU, 16 GB):

| Operation | Latency |
|---|---|
| Server startup (mmap + CCH topology load) | < 5 seconds |
| CCH metric customisation (per weather update) | 2–10 seconds |
| Atomic weights swap | < 1 ms |
| Single CCH route query | 10–500 μs |
| Throughput (32 threads) | 50,000–160,000 queries/s |
| graph_builder full run (0.25° global) | 15–60 minutes |

---

## Limitations

- **Platform:** Linux only (`mmap`, `MAP_POPULATE`).
- **Weather resolution:** daily-average energy-weighted snapshots — no intra-day variation. All 24 hourly slots in `WeatherBuffer` are filled with the same daily value; finer temporal resolution would require reprocessing the source data.
- **Bathymetry resolution:** GEBCO at 0.25° (~28 km) for open ocean; NOAA ENC at chart resolution for US waters. Outside US ENC coverage, coastal depth accuracy degrades.
- **float16 compiler support:** requires `_Float16`, available in GCC ≥ 12 and Clang ≥ 15 on x86-64 with `-march=native`.
- **RoutingKit commit pin:** no versioned releases exist. Pin is in `CMakeLists.txt`. Update deliberately, not automatically.
- **NetCDF dependency for GEBCO:** `GebcoLoader` requires linking against `netCDF-C` (`find_package(netCDF)` in `graph_builder/CMakeLists.txt`).
- **GDAL dependency for GSHHG/ENC:** `GshhgMasker` and S-57 micro-validation require GDAL. Add `find_package(GDAL)` to `graph_builder/CMakeLists.txt` when implementing the stubs.

---

## Future improvements

### Speed-loss model (wave-induced resistance)

The current FOC model returns a fixed `{speed_kts, foc_per_nm}` pair regardless
of sea state. In reality, wave-induced added resistance causes a vessel to lose
speed even when maintaining constant engine output. A vessel that is designed for
14 knots in calm water may achieve only 11 knots in 4 m significant wave height —
consuming the same fuel but covering less distance per hour.

The fix requires replacing the flat lambda with a **polar-diagram model**:

```
speed_kts = f(engine_power, sig_wh, wave_period, heading_relative_to_waves)
foc_per_nm = engine_power / (speed_kts * propulsive_efficiency)
```

This can be derived from a vessel's speed-power trial data and parameterised by
wave height and encounter angle. At the graph level, edges with a significant
tailwind or beam sea will have a lower per-nm cost than the same edge in a head
sea — which changes not just the FOC accumulation but the **path** the CCH
chooses, since detours around rough patches become genuinely cost-effective.

### Vessel-specific FOC model

The current placeholder (`20 MT/day flat at 12 kts`) is disconnected from any
physical vessel. A real FOC model requires:

- **SFOC curve** (specific fuel oil consumption, g/kWh vs engine load)
- **Hull resistance model** (calm-water resistance as a function of speed and
  displacement; augmented by added resistance in waves)
- **Propeller efficiency curve** (RPM → thrust → speed through water)

The `VesselParams::foc_model` callable in `edge_weight.hpp` already has the
right signature — `(sig_wh, wind_spd, current_comp) → {speed_kts, foc_per_nm}`.
A future implementation will populate this from a per-vessel lookup table or a
lightweight parametric model trained on noon-report data. Once the model is in
place, the optimizer will produce genuinely vessel-specific routes rather than
geometric approximations.
