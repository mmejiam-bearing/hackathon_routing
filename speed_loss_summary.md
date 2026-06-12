# Speed Loss Calculation — Technical Reference

This document describes, end-to-end, how speed loss is calculated for a vessel. It is intended as a hand-off document for engineers who need to understand, extend, or debug the pipeline. Implementation references point to the current Python codebase, but the logic is described in terms of formulas and pseudocode.

---

## 1. Conceptual Overview

**Speed loss** is the percentage reduction in a vessel's speed caused by weather resistance (wind, waves, or both) on top of the baseline calm-water resistance. The core physical assumption is **constant effective power**: the engine delivers a fixed output, so any additional resistance from weather directly reduces the speed the vessel can maintain.

The pipeline takes three categories of input — vessel hull geometry, load state, and a weather condition — and produces a single scalar: **speed loss as a percentage of calm-water speed**.

The high-level flow is:

```
Vessel properties + Load state + Weather
        │
        ▼
  Compute calm-water resistance (R_cw)
  Compute weather-induced resistance (ΔR = R_wind, R_wave, or both)
        │
        ▼
  Apply empirical speed-loss formula
        │
        ▼
  speed_loss_percentage  (0–100%)
```

---

## 2. System Components

| Component | Responsibility |
|---|---|
| **Vessel property store** | Looks up hull geometry and superstructure dimensions for a given IMO number |
| **Weather normaliser** | Converts raw weather inputs (knots, compass directions) to SI units and vessel-relative angles |
| **Calm-water resistance model** | Computes hydrodynamic resistance at a given speed and displacement (Hollenbach/ITTC method) |
| **Wind resistance model** | Computes aerodynamic resistance using the Fujiwara method |
| **Wave resistance model** | Computes wave-added resistance using Kreitner's method |
| **Speed-loss calculator** | Combines the resistance components with the appropriate empirical formula |
| **Batch calculator** | Evaluates speed loss over Cartesian products of input ranges |
| **SHP scaling factor calculator** | Converts weather resistance into a multiplicative scaling factor for SHP curves |

---

## 3. Inputs

### 3.1 Vessel identity and load state

| Field | Unit | Description |
|---|---|---|
| IMO number | — | Unique vessel identifier; used to retrieve all hull and superstructure properties |
| Displacement | tonnes | Current load condition; drives geometry parameters (wetted surface, block coefficient, etc.) |
| Speed | knots | Vessel speed over ground |
| Mean draft | metres | Optional; derived from displacement if omitted |
| Aft draft | metres | Optional; defaults to mean draft |
| Fore draft | metres | Optional; defaults to mean draft |

### 3.2 Weather state

Weather is specified by the following fields. Directions use the **16-point compass** internally (values 0–15, where each step is 22.5°); external interfaces accept 360° degrees and convert before processing.

| Field | Unit | Notes |
|---|---|---|
| Wind speed | knots | See zero-wind guard in §6 |
| Wind direction | 16-pt compass | Direction the wind is coming *from* |
| Significant wave height | metres | |
| Wave direction | 16-pt compass | Direction the waves are coming *from* |
| Current speed | knots | Always 0 in speed-loss calculations |
| Current direction | 16-pt compass | Direction the current is going *to* |
| Vessel heading | degrees from north | Always 0 in speed-loss batch calculations |

### 3.3 Direction conventions and conversions

**360° ↔ 16-point compass:**

```
compass_16 = angle_360 × 16 / 360
angle_360  = compass_16 × 360 / 16
```

This is a linear mapping; each 16-point unit equals 22.5°.

**Compass direction → relative angle to vessel heading:**

The resistance models work with the angle between the weather component and the vessel's bow (0° = head-on, 180° = following). The conversion folds any absolute bearing into the [0°, 180°] range, eliminating the port/starboard distinction:

```
absolute_angle_degrees = compass_16 × 22.5
relative_angle = arccos(cos((heading − absolute_angle_degrees) × π/180)) × 180/π
```

Current uses the complementary convention ("going to" rather than "coming from"):

```
relative_angle_current = 180° − arccos(cos((heading − absolute_angle_degrees) × π/180)) × 180/π
```

**Unit conversion — speed:**

```
speed_ms = speed_knots × 0.5144
```

---

## 4. Resistance Models

Three independent resistance components are computed and used in the speed-loss formulas.

### 4.1 Calm-Water Resistance (R_cw)

Source: Hollenbach resistance prediction / ITTC 1957 friction line.

The total calm-water resistance coefficient is the sum of three terms:

**Frictional resistance coefficient (C_f)** — ITTC 1957 line:

```
Re = V × L_wl / ν

C_f = 0.075 / (log₁₀(Re) − 2)²
```

where `V` is speed over ground in m/s, `L_wl` is the waterline length, and `ν` is the kinematic viscosity of seawater.

**Residuary resistance coefficient (C_r)** — empirical polynomial in Froude number (Fn), length-displacement ratio (L/∇^(1/3)), and prismatic coefficient (Cp). Full expression (after a beam/draft correction and a 0.7 scaling factor applied to the raw polynomial result):

```
Fn = V / √(g × L_char)

A0 = 1.35 − 0.23 × (L/∇^(1/3)) + 0.012 × (L/∇^(1/3))²
A1 = 0.0011 × (L/∇^(1/3))^9.1
N1 = 2 × (L/∇^(1/3)) − 3.7

E = (A0 + 1.5 × Fn^1.8 + A1 × Fn^N1) × (0.98 + 2.5 / ((L/∇^(1/3)) − 2)^4)
    + ((L/∇^(1/3)) − 5)^4 × (Fn − 0.1)^4

B1 = 7 − 0.09 × (L/∇^(1/3))²
B2 = (5 × Cp − 2.5)²
B3 = (600 × (Fn − 0.315)² + 1)^1.5
G  = (B1 × B2) / B3

H = exp(80 × (Fn − (0.04 + 0.59 × Cp) − 0.015 × ((L/∇^(1/3)) − 5)))
K = 180 × Fn^3.7 × exp(20 × Cp − 16)

C_r_raw = (E + G + H + K) / 1000
beam_draft_correction = 0.16 × (B/T_mid − 2.5) / 1000
C_r = (C_r_raw + beam_draft_correction) × 0.7
```

**Incremental resistance coefficient (C_a):**

```
C_a = (0.5 × log₁₀(Δ) − 0.1 × (log₁₀(Δ))²) / 1000
```

where `Δ` is the displacement in tonnes.

**Total calm-water resistance:**

```
R_cw = 0.5 × ρ_water × V² × S_wet × (C_f + C_r + C_a)   [kN]
```

where `ρ_water` is seawater density and `S_wet` is the wetted surface area.

**Current resistance:** computed as the difference in calm-water resistance between speed-over-ground and speed-through-water:

```
R_current = R_cw(V_og + V_current × cos(θ_current)) − R_cw(V_og)
```

### 4.2 Wind Resistance (R_wind)

Source: Fujiwara method.

The aerodynamic resistance depends on the **relative wind speed** (the vector sum of true wind and vessel motion) and a dimensionless resistance coefficient that varies with the relative wind angle and hull geometry.

**Relative wind speed:**

```
V_rel = √[(V_wind × cos(θ_wind) + V_og)² + (V_wind × sin(θ_wind))²]
```

where `θ_wind` is the relative wind angle (0° = head wind) and `V_og` is vessel speed over ground, both in m/s.

**Wind resistance coefficient (C_wind):** Computed differently for bow-sector wind (0° ≤ θ < 90°) and stern-sector wind (90° < θ ≤ 180°). Each sector uses three sub-coefficients — a longitudinal force coefficient (C_lf), a lateral force coefficient (C_xli), and a heel moment coefficient (C_alf) — which are polynomial functions of hull ratios (lateral area, transverse section area, superstructure height, beam, length). Their coefficients are fixed empirical matrices (BETA, DELTA, EPSILON).

The resistance coefficient for both sectors follows the same trigonometric combination:

```
C_wind(θ) = C_lf × cos(θ)
           + C_xli × sin(θ) × (sin(θ) − 0.5 × sin(θ) × cos²(θ)) × cos(θ)
           + C_alf × sin(θ) × cos³(θ)
```

Special cases:
- **θ = 90°**: linearly interpolated between C_wind(80°) and C_wind(100°).
- **Stern sector with V_wind < V_og**: the complementary angle (180° − θ) is used for C_wind.

**Wind resistance:**

```
R_wind = 0.5 × ρ_air × V_rel² × C_wind(θ_wind) × A_T   [kN]
```

where `A_T` is the maximum transverse section area above the waterline and `ρ_air` is air density.

Note: wind resistance is **speed-dependent** because V_rel depends on vessel speed.

### 4.3 Wave Resistance (R_wave)

Source: Kreitner's method.

Wave resistance is assumed **speed-independent** — the same value applies regardless of vessel speed.

**Head-wave resistance:**

```
R_head = 0.64 × H_s² × B² × C_b × (ρ_water × g) / L   [kN]
```

where:
- `H_s` = significant wave height [m]
- `B` = beam [m]
- `C_b` = block coefficient
- `L` = vessel length [m]

**Angle correction** (linear interpolation over the table below):

| Wave angle (°) | 0 | 22.5 | 45 | 67.5 | 90 | 112.5 | 135 | 157.5 | 180 |
|---|---|---|---|---|---|---|---|---|---|
| Coefficient | 1.0 | 1.125 | 1.0 | 0.75 | 0.425 | 0.4 | 0.3 | 0.2 | 0.1 |

0° is head-on (worst case), 180° is following sea (near-zero resistance). The peak at 22.5° reflects the additional resonance effects of quartering head-seas.

**Final wave resistance:**

```
R_wave = angle_coefficient(θ_wave) × R_head
```

---

## 5. Empirical Speed-Loss Formulas

Two formulas are available, both grounded in the **equal-power assumption** (the engine delivers constant effective power `P_E = R × V`). The appropriate formula is selected based on the weather type.

### 5.1 Kwon Estimation — used for waves and combined

Source: Eq. 2.5 in *Jens Christoffer Gjølme, "Estimation of Speed Loss due to Current, Wind and Waves: A Data-Driven Approach"*.

Derivation: under constant power, `R_cw × V₀ = (R_cw + ΔR) × V₁`. Solving for the speed ratio and expressing as a percentage loss:

```
speed_loss% = 100 × (√(1 + ΔR / R_cw) − 1)
```

Behaviour:
- If `R_cw = 0`: return 0%.
- Result is capped at 100%.
- If the radicand is negative (physically impossible but numerically possible due to model error): result is undefined — treated as a calculation failure (see §8).

### 5.2 Theoretical Derivation — used for wind

Derived from the equal-power assumption with the second-order cross-term `ΔR × Δv` neglected:

```
if ΔR < R_cw:
    speed_loss% = 100 × (1 − √(1 − ΔR / R_cw))
else:
    speed_loss% = 100%
```

Behaviour:
- If `R_cw = 0`: return 0%.

### 5.3 Formula and ΔR Selection by Weather Type

| Weather type | ΔR used | Formula |
|---|---|---|
| Wind only | R_wind | Theoretical derivation |
| Wave only | R_wave | Kwon estimation |
| Combined | R_wind + R_wave | Kwon estimation |

The asymmetry — using different formulas for wind vs. waves — reflects the different physical regimes. Wind resistance interacts with the calm-water baseline (the vessel already pushes through air), making the first-order approximation more appropriate. Wave resistance is additive and better captured by the Kwon form.

---

## 6. Single-Point Calculation — Step by Step

Given one vessel, one displacement, one speed, and one weather state:

```
FUNCTION compute_speed_loss(imo, displacement, speed, weather_type, weather):

  1. Load vessel properties for (imo, displacement)
       → hull geometry: length, beam, block coefficient, wetted surface,
         lateral area, transverse section area, superstructure height, etc.

  2. Normalise weather to SI and relative angles
       wind_speed_ms     = wind_speed_knots × 0.5144
       wind_relative_deg = arccos(cos((heading − wind_dir_abs) × π/180)) × 180/π
       wave_relative_deg = arccos(cos((heading − wave_dir_abs) × π/180)) × 180/π
       (current uses the "going to" convention)

  3. Compute resistances
       R_cw     = calm_water_resistance(speed, displacement, vessel_geometry)
       R_wind   = wind_resistance(speed, wind_speed_ms, wind_relative_deg, vessel_geometry)
       R_wave   = wave_resistance(wave_height_m, wave_relative_deg, vessel_geometry)
       R_current = current_resistance(speed, current_speed, current_relative_deg, vessel_geometry)

  4. Zero-wind guard
       IF weather_type IN {wind, combined} AND wind_speed_knots ≤ 1e-8:
           R_wind = 0

  5. Select ΔR
       IF weather_type = "wind":     ΔR = R_wind
       IF weather_type = "wave":     ΔR = R_wave
       IF weather_type = "combined": ΔR = R_wind + R_wave

  6. Apply empirical formula
       IF weather_type = "wind":
           speed_loss% = theoretical_derivation(R_cw, ΔR)
       ELSE:
           speed_loss% = kwon_estimation(R_cw, ΔR)

  7. Return speed_loss% along with all intermediate values
       (R_cw, R_wind, R_wave, R_current, input fields)
```

---

## 7. Batch Computation

The batch calculator evaluates speed loss across Cartesian products of parameter ranges, producing one result record per combination.

### 7.1 Wind-only or wave-only

```
FOR EACH (displacement, direction, magnitude) IN
    product(displacement_range, direction_range, magnitude_range):

    Build weather state (zero out the non-active component)

    FOR EACH speed IN speed_range:
        result = compute_speed_loss(imo, displacement, speed, weather_type, weather)
        Append result (with direction converted back to 360°, load_condition label)
```

### 7.2 Combined wind and wave

```
FOR EACH (displacement, wind_dir, wave_dir, wind_speed, wave_height, draft_trim)
    IN product(displacement_range,
               wind_direction_range,
               wave_direction_range,
               wind_speed_range,
               wave_height_range,
               draft_trim_combinations):

    Build combined weather state

    FOR EACH speed IN speed_range:
        result = compute_speed_loss(imo, displacement, speed, "combined", weather)
        Append result (with both directions in 360°, load_condition label)
```

**Draft/trim expansion:** When draft and trim ranges are provided, each (draft, trim) pair is expanded into the three values passed to the resistance model:

```
draft_mean = draft
draft_fore = draft − 0.5 × trim
draft_aft  = draft + 0.5 × trim
```

If no trim range is given, trim defaults to 0. If no draft range is given, the vessel model derives draft from displacement internally.

---

## 8. Weather State Construction

For each batch iteration a weather state is constructed with only the relevant components populated; unused components are zeroed out:

| Mode | Wind fields | Wave fields | Current fields |
|---|---|---|---|
| Wind only | speed + direction set | zero | zero |
| Wave only | zero | height + direction set | zero |
| Combined | speed + direction set | height + direction set | zero |

**Zero-wind substitution:** If wind speed is exactly 0, it is replaced with a negligibly small positive value (≈ 10⁻⁹ knots) before constructing the weather state. This prevents the normalisation step from producing a degenerate state. Downstream, the zero-wind guard in step 4 of the single-point calculation (threshold ≤ 10⁻⁸ knots) then forces R_wind to 0, ensuring no phantom wind resistance enters the formula.

---

## 9. SHP Weather Scaling Factor

This is a separate product of the resistance pipeline. Instead of a speed-loss percentage, it outputs a **multiplicative factor** that adjusts a vessel's calm-water SHP–speed curve to account for weather conditions.

**Inputs:**

| Field | Description |
|---|---|
| IMO | Vessel identifier |
| Load condition | `ballast` or `laden` |
| Displacement | Tonnes |
| Wave height | Metres |
| Wind speed | Knots |
| Wind direction range | List of 360° directions to sample |
| Wave direction range | List of 360° directions to sample |
| Reference SHP | Shaft horsepower at the reference speed on the calm-water curve |
| Reference speed | Vessel speed corresponding to the reference SHP |

**Calculation:**

```
1. Run the combined batch calculator across all supplied wind and wave
   direction combinations at the single provided speed and displacement.

2. weather_resistance_median = median(R_wind + R_wave)  over all direction combinations

3. weather_power = weather_resistance_median × reference_speed

4. IF reference_shp > 0:
       scaling_factor = 1 + weather_power / reference_shp
   ELSE:
       scaling_factor = 1
```

**Interpretation:** A scaling factor of 1.05 means that weather conditions require approximately 5% more shaft power to maintain the reference speed. Multiplying the calm-water SHP curve by this factor yields a weather-adjusted SHP curve.

The median over directions captures a representative omnidirectional weather exposure rather than the worst- or best-case heading.

---

## 10. Beaufort Scale Reference Table

When callers specify weather by Beaufort number rather than direct measurements, the following table maps each level to representative average values (mid-points of the Beaufort intervals, per the Royal Meteorological Society):

| Beaufort | Wind speed (knots) | Wave height (m) |
|---|---|---|
| 0 | 0 | 0 |
| 1 | 2 | 0.1 |
| 2 | 5 | 0.25 |
| 3 | 8.5 | 0.8 |
| 4 | 13.5 | 1.25 |
| 5 | 19 | 2.25 |
| 6 | 24.5 | 3.5 |
| 7 | 30.5 | 4.75 |
| 8 | 37 | 6.5 |
| 9 | 44 | 8.5 |
| 10 | 51.5 | 10.75 |
| 11 | 59.5 | 13.75 |
| 12 | 64 | 14 |

Default directions (used when no specific angles are provided): **0° and 180°** (head-on and following) for both wind and waves.

---

## 11. Output Record

Each single-point calculation produces a record containing:

| Field | Description |
|---|---|
| IMO | As provided |
| Displacement | As provided [tonnes] |
| Speed | As provided [knots] |
| Draft, draft_aft, draft_fore | As provided [m] |
| Wind speed, direction | As provided |
| Wave height, direction | As provided |
| Current speed, direction | As provided (always 0 in batch context) |
| Heading | As provided (always 0 in batch context) |
| R_cw | Calm-water resistance [kN] |
| R_wind | Wind resistance [kN] |
| R_wave | Wave resistance [kN] |
| R_current | Current resistance [kN] |
| **speed_loss_perc** | Speed loss percentage (0–100), or undefined if calculation failed |

Batch records additionally include:
- `wind_direction_360` or `wave_direction_360`: the direction in 360° degrees
- `load_condition`: `"ballast"` or `"laden"`

---

## 12. Edge Cases and Numerical Guards

| Scenario | Handling |
|---|---|
| R_cw = 0 | Both formulas return 0% |
| ΔR ≥ R_cw (theoretical derivation / wind) | Returns 100% |
| Negative radicand in Kwon formula | Result is undefined; record is flagged; upstream consumers are responsible for filtering these out |
| Wind speed ≤ 1e-8 knots | R_wind is forced to 0 before the formula (prevents double-counting with calm-water baseline) |
| Wind speed = 0 exactly | Replaced with ~1e-9 before weather state construction (prevents degenerate angle computation) |
| Reference SHP = 0 (SHP scaling) | Scaling factor returned as 1 |
| Unsupported weather type | Error; valid values are `"wind"`, `"wave"`, `"combined"` |
| Missing required range arguments (combined batch) | Error raised before any computation begins |

---

## 13. Key Assumptions and Limitations

- **Equal-power assumption:** Both empirical formulas assume the engine delivers constant effective power (P_E = R × V). The approximation becomes less accurate at high resistance ratios (ΔR / R_cw > 0.5).

- **Heading fixed at 0° in batch calculations:** Wind and wave directions are interpreted as absolute bearings, which equals the relative angle when heading is 0°. The system does not currently model heading-dependent speed loss across a voyage.

- **Current excluded from speed loss:** Current resistance is computed and returned but is never used as ΔR. It is available for downstream use only.

- **Wave resistance is speed-independent:** Kreitner's model gives a single value applied uniformly across all vessel speeds. In reality, added wave resistance has a weak speed dependence that is neglected here.

- **Wind resistance is speed-dependent:** Relative wind speed depends on vessel speed over ground, so R_wind changes with speed. This is correctly modelled.

- **Symmetry assumption:** The [0°, 180°] relative-angle convention treats port and starboard exposures as identical. Asymmetric hull features are not captured.

- **One speed per call:** The single-point calculator evaluates exactly one vessel speed per invocation. Evaluation across a speed range is handled by looping at the batch level.
