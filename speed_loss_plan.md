# Speed-Loss Implementation Plan

> **Status:** Ready to implement — universal FOC model is in place.
> Physics foundations in `speed_loss_summary.md`.

---

## Context

The universal FOC model (`lib/include/maritime/foc_model.hpp`) is now live.
It computes Shaft Horsepower from Beaufort number and service speed, then converts to
MT/nm via SFOC:

```
SHP (kW) = 76.14 × (1 + 6.16×10⁻⁴ × beaufort³) × speed_kts³ + 2484.8
FOC (MT/h) = SHP × 180 / 1,000,000
foc_per_nm  = FOC / speed_kts
```

The model is wired into both `voyage_router.cpp` and `route_query.cpp` via the
`VesselParams.foc_model` lambda, capturing `service_speed_kts = 14.f`.

**Remaining gap:** speed is still constant — the vessel always travels at its
design speed regardless of wave height or wind. Speed loss from weather
(Hollenbach calm-water resistance + Kreitner wave resistance + Kwon speed-loss
formula) is not yet applied. This means ETA and FOC are underestimated in heavy
weather because the engine still assumes 14 kts through a Force-12 storm.

**Integration approach:** the speed-loss physics should produce an
`actual_speed_kts` that is passed into `universal_foc_model` in place of the
fixed service speed. The FOC model already accepts an arbitrary speed, so no
change to `foc_model.hpp` is needed — only the caller changes.

**Current gaps in the routing engine:**

- `elapsed_h += d_nm / speed_kts` in `voyage_router.cpp:272` uses the fixed CLI
  speed instead of the weather-adjusted per-edge speed.
- `compute_edge_cost` in `edge_weight.hpp` does not compute speed loss; the
  `speed_kts` returned by `foc_model` is the design speed, not the actual speed.
- `CchRouteResult` has no `total_time_h` field.

---

## Architecture Changes

### 1. `lib/include/maritime/edge_weight.hpp`

**`VesselParams` — add hull geometry fields** needed for resistance calculations
(keep `foc_model` and `service_speed_kts`; the lambda will receive
`actual_speed_kts` from the physics layer):

```cpp
struct VesselParams {
    float draft_m             = 10.f;     // [m]
    float beam_m              = 32.f;     // [m]
    float loa_m               = 200.f;    // [m]
    float service_speed_kts   = 14.f;     // design speed [kts]
    float block_coeff         = 0.80f;    // Cb (0.6–0.85 typical)
    float displacement_t      = 50000.f;  // laden displacement [tonnes]
    float transverse_area_m2  = 600.f;    // frontal area above waterline [m²]

    std::function<
        std::pair<float,float>
        (float sig_wh, float wind_spd, float current_comp)
    > foc_model;                          // returns {speed_kts, foc_per_nm}
};
```

**New `EdgeCost` return struct:**

```cpp
struct EdgeCost { float foc_mt; float time_h; };
```

**New inline physics functions** (from `speed_loss_summary.md`):

| Function | Formula source |
|---|---|
| `calm_water_resistance_kn(speed_kts, vessel)` | ITTC 1957 C_f + simplified Hollenbach C_r + C_a |
| `wave_resistance_kn(sig_wh, wave_rel_deg, vessel)` | Kreitner §4.3 + 8-point angle table |
| `wind_resistance_kn(was_ms, wind_rel_deg, speed_kts, vessel)` | Simplified Fujiwara using `transverse_area_m2` |
| `speed_loss_pct(R_cw, delta_R)` | Kwon §5.1: `100 × (√(1 + ΔR/R_cw) − 1)`, capped 100 |

**Physics details:**

```
// Relative angle (0° = head-on, 180° = following) for both wind and waves:
rel_deg = acos(cos(hdg − dir_rad)) × 180/π

// Derived hull parameters:
L_wl  = loa_m × 0.95
∇     = displacement_t / 1.025              // volume [m³]
S_wet = 2.5 × sqrt(∇ × L_wl)               // Mumford approximation [m²]

// Calm-water resistance (ITTC 1957):
Re   = speed_ms × L_wl / 1.19e-6
C_f  = 0.075 / (log10(Re) − 2)²
C_a  = (0.5×log10(Δ) − 0.1×log10(Δ)²) / 1000
Fn   = speed_ms / sqrt(9.81 × loa_m)
C_r  = simplified Hollenbach polynomial (Fn, block_coeff, L_wl/∇^(1/3))
R_cw = 0.5 × 1025 × speed_ms² × S_wet × (C_f + C_r + C_a)  [kN]

// Kreitner wave resistance (angle table from §4.3):
//   0°→1.0, 22.5°→1.125, 45°→1.0, 67.5°→0.75, 90°→0.425,
//   112.5°→0.4, 135°→0.3, 157.5°→0.2, 180°→0.1
R_head = 0.64 × sig_wh² × beam_m² × block_coeff × (1025×9.81) / loa_m  [kN]
R_wave = angle_table_interp(wave_rel_deg) × R_head

// Simplified wind resistance:
vog_ms  = service_speed_kts × 0.5144
cos_rel = cos(wind_rel_rad)
V_rel   = sqrt((was_ms×cos_rel + vog_ms)² + (was_ms×sin_rel)²)
C_wind  = max(0, cos_rel) × 0.5     // head-on ≈ 0.5; beam/stern ≈ 0
R_wind  = 0.5 × 1.225 × V_rel² × C_wind × transverse_area_m2  [kN]
```

**`compute_edge_cost()` — speed-loss integration with `universal_foc_model`:**

```cpp
// read wind/wave direction for heading-relative angles
const float wad_deg = static_cast<float>(wx.wad[wx_idx]);   // wind going-to [°]
const float pwd_deg = static_cast<float>(wx.pwd[wx_idx]);   // wave going-to [°]

const float wind_rel_deg = std::acos(std::cos(hdg - wad_deg*(π/180))) * (180/π);
const float wave_rel_deg = std::acos(std::cos(hdg - pwd_deg*(π/180))) * (180/π);

const float R_cw   = calm_water_resistance_kn(vessel.service_speed_kts, vessel);
const float R_wave = wave_resistance_kn(sig_wh, wave_rel_deg, vessel);
const float R_wind = wind_resistance_kn(wind_spd, wind_rel_deg, vessel.service_speed_kts, vessel);
const float sl     = speed_loss_pct(R_cw, R_wave + R_wind);
const float actual_spd = std::max(1.f, vessel.service_speed_kts * (1.f - sl / 100.f));

// Pass weather-adjusted speed into the universal FOC model
const auto [_, foc_per_nm] = vessel.foc_model(sig_wh, wind_spd, current_comp);
// Re-invoke with actual speed — foc_model captures service_speed but we
// need to call universal_foc_model directly here with actual_spd:
const float foc_per_nm_actual = maritime::universal_foc_model(wind_spd, actual_spd).second;

return EdgeCost{foc_per_nm_actual * dist_nm * eca_factor + tss_penalty,
                dist_nm / actual_spd};
```

> **Note:** `compute_edge_cost` will call `maritime::universal_foc_model` directly
> with `actual_spd` rather than going through the `foc_model` lambda, since the
> lambda captures a fixed service speed. Alternatively the lambda signature can be
> updated to accept an override speed — either approach works.

Canal-transit path returns `EdgeCost{foc_calm × dist, dist / service_speed}`.

---

### 2. `lib/include/maritime/routing_engine.hpp`

- `CchRouteResult`: add `float total_time_h = 0.f`
- Accumulation loop: `EdgeCost ec = compute_edge_cost(...);`
  `result.total_foc_mt += ec.foc_mt;  result.total_time_h += ec.time_h;`

---

### 3. `router_server/src/voyage_router.cpp`

- Add `block_coeff`, `displacement_t`, `transverse_area_m2` to vessel setup
- Replace `elapsed_h += d_nm / speed_kts` with `elapsed_h += ec.time_h`
- Output `total_time_h` in GeoJSON segment properties
- Remove or ignore `--speed` CLI argument (actual speed derived per-edge from physics;
  `service_speed_kts` is still set as the calm-water design speed)

---

### 4. `router_server/src/route_query.cpp`

- Add `block_coeff`, `displacement_t`, `transverse_area_m2` to vessel setup

---

### 5. `tests/unit/test_edge_weight.cpp`

- Add hull geometry fields to `make_test_vessel()`
- Update `compute_edge_cost` call sites to use `EdgeCost`
- Add speed-loss unit tests:
  - Calm (sig_wh=0, was=0) → `sl_pct ≈ 0`, `actual_spd ≈ service_speed_kts`
  - Peak storm head-on (sig_wh=15 m, was=45 m/s) → `sl_pct > 20%`
  - Kreitner: `R_wave(0°) > R_wave(180°)` (head-on worse than following sea)
  - Kwon identity: `speed_loss_pct(R, R) ≈ 41.4%` (√2 − 1)
  - FOC spot-check: `universal_foc_model(0, 10.f).second < universal_foc_model(0, 14.f).second`
    (lower actual speed → lower FOC/nm despite more power fraction used)

---

## What Does NOT Change

- `lib/include/maritime/foc_model.hpp` — `universal_foc_model` signature and constants stay as-is;
  it already accepts any speed, so passing `actual_spd` requires no changes to the header.
- `weather_etl/src/weights_writer.cpp` and `weather_etl/etl.py` — CCH proxy weights
  (`sigwh/3 + headwind_f`) are a separate routing heuristic, not per-edge FOC/ETA.
- `scripts/` — plotting scripts unaffected.

---

## Verification

```bash
# 1. Build
cmake --build build -- -j$(sysctl -n hw.logicalcpu)

# 2. Unit tests
./build/tests/maritime_unit_tests

# 3. Fading storm voyage
./build/router_server/maritime-voyage-router \
  --graph data/artifacts/graph.bin --flags data/artifacts/flags.bin \
  --snap  data/artifacts/snap.bin  --cch   data/artifacts/cch_topo.bin \
  --npy data/weather/2026_06_08_fading --npy data/weather/2026_06_09_fading \
  --npy data/weather/2026_06_10_fading --npy data/weather/2026_06_11_fading \
  --npy data/weather/2026_06_12_fading --npy data/weather/2026_06_13_fading \
  --npy data/weather/2026_06_14_fading \
  --from-lat 40.7 --from-lon -74.0 --to-lat 51.5 --to-lon -0.1 \
  --out /tmp/rolling_fading.geojson
```

Pass criteria:
- Calm edge (sig_wh≈0, was≈0): `time_h ≈ dist_nm / 14`, `actual_spd ≈ 14 kts`
- Peak storm head-on (sig_wh=15 m, was=45 m/s): `actual_spd < 11 kts`, FOC/nm elevated vs calm
- Fading storm total `time_h` visibly exceeds calm voyage on storm-peak days
- FOC increases on storm days (higher Beaufort AND lower speed amplify fuel burn)
