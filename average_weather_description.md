# Average Weather Description

Daily energy-averaged GFS-Wave / WAVEWATCH III fields, produced by `weather_daily_avg`
(see the `weather_averaging` repository for source and full derivations: `README.md` Sections
2-5). This document is a self-contained reference for systems consuming these grids: what each
field means, how it was calculated, where the files live, and the complete manifest of files
produced for this run (full year 2024).

---

## 1. Grids

All fields are regular lat/lon (equirectangular) grids at 0.25 degree resolution, 1440 columns
(`lon = col * 0.25`, 0 to 359.75 E). Row 0 is the **south** end for both grids; latitude
increases **northward** with row (this is the same convention for both grids, despite their
different extents):

| Grid | Fields | Shape (rows x cols) | Latitude |
|---|---|---|---|
| Wave grid | `sigwh`, `wsh`, `wsp`, `wsd`, `pwd`, `swell_residual` | 621 x 1440 | -75 (row 0) to +80 (row 620) |
| Wind grid | `was`, `wad` | 721 x 1440 | -90 (row 0) to +90 (row 720), full pole-to-pole |

All output files are `float64` (`.npy`, C-order/row-major), regardless of the source archive's
on-disk dtype (`float16`/`int8`).

## 2. NaN Policy

- `NaN` means land/ice (no data), **not** zero. A zero value is a valid observation (e.g. a calm
  sea or a true geometric direction).
- A grid cell is `NaN` in the output if it had fewer than 2 valid hourly timesteps that day
  (out of up to 8 possible, at 00/03/06/09/12/15/18/21 UTC).
- Direction fields (`wsd`, `wad`, `pwd`) are degrees, 0-360, measured clockwise from geographic
  North (meteorological "coming from" convention: 0 = arriving from North, 90 = from East).

## 3. Field Definitions and Calculations

All "energy averages" use the physical reasoning that added resistance/drag scales with the
*square* of wave height or wind speed, so the representative daily value must preserve the total
energy/impulse over the day, not the arithmetic mean (see README.md Section 4.1 for the full
derivation). `N_valid` is the count of non-NaN hourly values at a given cell that day.

| File | Symbol | Units | Calculation |
|---|---|---|---|
| `sigwh.npy` | `H_eff,sig` | m | Energy (RMS) average of significant wave height: `sqrt(mean(H_sigwh_t^2))` over valid hours. |
| `wsh.npy` | `H_eff,ws` | m | Energy (RMS) average of wind-sea significant wave height: `sqrt(mean(H_wsh_t^2))` over valid hours. |
| `wsp.npy` | `T_eff,ws` | s | Energy-flux-weighted mean wind-sea period: `sum(wsh_t^2 * wsp_t) / sum(wsh_t^2)`. Weighted by `wsh^2`, not a simple mean -- a calm (wsh=0) hour contributes zero weight. |
| `wsd.npy` | `theta_eff,ws` | degrees | Energy-weighted circular mean of wind-sea direction, weighted by `wsh^2`: `atan2(sum(wsh_t^2*sin(wsd_t)), sum(wsh_t^2*cos(wsd_t))) mod 360`. |
| `was.npy` | `U_eff` | m/s | Drag-equivalent (RMS) average of wind speed: `sqrt(mean(was_t^2))` over valid hours -- mirrors `sigwh`'s formula, since aerodynamic drag also scales as speed^2. |
| `wad.npy` | `theta_eff,wind` | degrees | Energy-weighted circular mean of wind direction, weighted by `was^2` (consistent with `F_wind ~ U^2`). |
| `pwd.npy` | `theta_eff,peak` | degrees | Energy-weighted circular mean of peak wave direction, weighted by `pwh^2` (peak wave height) -- its own paired height field, not `sigwh` or `wsh`. `pwh` itself is not an output field. |
| `swell_residual.npy` | `H_swell` | m | Non-local (swell) wave energy: `sqrt(max(sigwh_daily^2 - wsh_daily^2, 0))`, computed from the already-daily-averaged `sigwh`/`wsh` above (not averaged independently). `H_swell ~ 0` means wind-sea explains essentially all wave energy; `H_swell >> 0` means a non-local (distant storm) swell component is present that local wind fields don't capture. |

Each direction field is weighted by its **own** physically-paired intensity field (height for
wave directions, speed for wind direction) rather than a generic combined-energy proxy -- this
is deliberate (see README.md Section 4.7) and avoids cross-component bias.

## 4. File Location

Base directory (this run, on an external disk -- only reachable while the disk is mounted):

```
/Users/mmejiam9206/exfat_mount/bearing_noaa/output
```

Path pattern for any given day:

```
/Users/mmejiam9206/exfat_mount/bearing_noaa/output/<YYYY>/<MM>/<DD>/<field>.npy
```

e.g. `/Users/mmejiam9206/exfat_mount/bearing_noaa/output/2024/06/15/sigwh.npy`.

If the external disk needs to be remounted in a future session (e.g. after disconnect/reboot),
it is exFAT-formatted; mount with:

```
mkdir -p ~/exfat_mount
sudo mount_exfat /dev/disk4s1 ~/exfat_mount
```

(`disk4s1` may differ if disk enumeration changes -- check with `diskutil list` first.)

## 5. File Manifest

Full year 2024: 366 days, 2928 files (8 fields/day). All paths below are
relative to the base directory in Section 4.

| Date | sigwh | wsh | wsp | wsd | was | wad | pwd | swell_residual |
|---|---|---|---|---|---|---|---|---|
| 2024-01-01 | 2024/01/01/sigwh.npy | 2024/01/01/wsh.npy | 2024/01/01/wsp.npy | 2024/01/01/wsd.npy | 2024/01/01/was.npy | 2024/01/01/wad.npy | 2024/01/01/pwd.npy | 2024/01/01/swell_residual.npy |
| 2024-01-02 | 2024/01/02/sigwh.npy | 2024/01/02/wsh.npy | 2024/01/02/wsp.npy | 2024/01/02/wsd.npy | 2024/01/02/was.npy | 2024/01/02/wad.npy | 2024/01/02/pwd.npy | 2024/01/02/swell_residual.npy |
| 2024-01-03 | 2024/01/03/sigwh.npy | 2024/01/03/wsh.npy | 2024/01/03/wsp.npy | 2024/01/03/wsd.npy | 2024/01/03/was.npy | 2024/01/03/wad.npy | 2024/01/03/pwd.npy | 2024/01/03/swell_residual.npy |
| 2024-01-04 | 2024/01/04/sigwh.npy | 2024/01/04/wsh.npy | 2024/01/04/wsp.npy | 2024/01/04/wsd.npy | 2024/01/04/was.npy | 2024/01/04/wad.npy | 2024/01/04/pwd.npy | 2024/01/04/swell_residual.npy |
| 2024-01-05 | 2024/01/05/sigwh.npy | 2024/01/05/wsh.npy | 2024/01/05/wsp.npy | 2024/01/05/wsd.npy | 2024/01/05/was.npy | 2024/01/05/wad.npy | 2024/01/05/pwd.npy | 2024/01/05/swell_residual.npy |
| 2024-01-06 | 2024/01/06/sigwh.npy | 2024/01/06/wsh.npy | 2024/01/06/wsp.npy | 2024/01/06/wsd.npy | 2024/01/06/was.npy | 2024/01/06/wad.npy | 2024/01/06/pwd.npy | 2024/01/06/swell_residual.npy |
| 2024-01-07 | 2024/01/07/sigwh.npy | 2024/01/07/wsh.npy | 2024/01/07/wsp.npy | 2024/01/07/wsd.npy | 2024/01/07/was.npy | 2024/01/07/wad.npy | 2024/01/07/pwd.npy | 2024/01/07/swell_residual.npy |
| 2024-01-08 | 2024/01/08/sigwh.npy | 2024/01/08/wsh.npy | 2024/01/08/wsp.npy | 2024/01/08/wsd.npy | 2024/01/08/was.npy | 2024/01/08/wad.npy | 2024/01/08/pwd.npy | 2024/01/08/swell_residual.npy |
| 2024-01-09 | 2024/01/09/sigwh.npy | 2024/01/09/wsh.npy | 2024/01/09/wsp.npy | 2024/01/09/wsd.npy | 2024/01/09/was.npy | 2024/01/09/wad.npy | 2024/01/09/pwd.npy | 2024/01/09/swell_residual.npy |
| 2024-01-10 | 2024/01/10/sigwh.npy | 2024/01/10/wsh.npy | 2024/01/10/wsp.npy | 2024/01/10/wsd.npy | 2024/01/10/was.npy | 2024/01/10/wad.npy | 2024/01/10/pwd.npy | 2024/01/10/swell_residual.npy |
| 2024-01-11 | 2024/01/11/sigwh.npy | 2024/01/11/wsh.npy | 2024/01/11/wsp.npy | 2024/01/11/wsd.npy | 2024/01/11/was.npy | 2024/01/11/wad.npy | 2024/01/11/pwd.npy | 2024/01/11/swell_residual.npy |
| 2024-01-12 | 2024/01/12/sigwh.npy | 2024/01/12/wsh.npy | 2024/01/12/wsp.npy | 2024/01/12/wsd.npy | 2024/01/12/was.npy | 2024/01/12/wad.npy | 2024/01/12/pwd.npy | 2024/01/12/swell_residual.npy |
| 2024-01-13 | 2024/01/13/sigwh.npy | 2024/01/13/wsh.npy | 2024/01/13/wsp.npy | 2024/01/13/wsd.npy | 2024/01/13/was.npy | 2024/01/13/wad.npy | 2024/01/13/pwd.npy | 2024/01/13/swell_residual.npy |
| 2024-01-14 | 2024/01/14/sigwh.npy | 2024/01/14/wsh.npy | 2024/01/14/wsp.npy | 2024/01/14/wsd.npy | 2024/01/14/was.npy | 2024/01/14/wad.npy | 2024/01/14/pwd.npy | 2024/01/14/swell_residual.npy |
| 2024-01-15 | 2024/01/15/sigwh.npy | 2024/01/15/wsh.npy | 2024/01/15/wsp.npy | 2024/01/15/wsd.npy | 2024/01/15/was.npy | 2024/01/15/wad.npy | 2024/01/15/pwd.npy | 2024/01/15/swell_residual.npy |
| 2024-01-16 | 2024/01/16/sigwh.npy | 2024/01/16/wsh.npy | 2024/01/16/wsp.npy | 2024/01/16/wsd.npy | 2024/01/16/was.npy | 2024/01/16/wad.npy | 2024/01/16/pwd.npy | 2024/01/16/swell_residual.npy |
| 2024-01-17 | 2024/01/17/sigwh.npy | 2024/01/17/wsh.npy | 2024/01/17/wsp.npy | 2024/01/17/wsd.npy | 2024/01/17/was.npy | 2024/01/17/wad.npy | 2024/01/17/pwd.npy | 2024/01/17/swell_residual.npy |
| 2024-01-18 | 2024/01/18/sigwh.npy | 2024/01/18/wsh.npy | 2024/01/18/wsp.npy | 2024/01/18/wsd.npy | 2024/01/18/was.npy | 2024/01/18/wad.npy | 2024/01/18/pwd.npy | 2024/01/18/swell_residual.npy |
| 2024-01-19 | 2024/01/19/sigwh.npy | 2024/01/19/wsh.npy | 2024/01/19/wsp.npy | 2024/01/19/wsd.npy | 2024/01/19/was.npy | 2024/01/19/wad.npy | 2024/01/19/pwd.npy | 2024/01/19/swell_residual.npy |
| 2024-01-20 | 2024/01/20/sigwh.npy | 2024/01/20/wsh.npy | 2024/01/20/wsp.npy | 2024/01/20/wsd.npy | 2024/01/20/was.npy | 2024/01/20/wad.npy | 2024/01/20/pwd.npy | 2024/01/20/swell_residual.npy |
| 2024-01-21 | 2024/01/21/sigwh.npy | 2024/01/21/wsh.npy | 2024/01/21/wsp.npy | 2024/01/21/wsd.npy | 2024/01/21/was.npy | 2024/01/21/wad.npy | 2024/01/21/pwd.npy | 2024/01/21/swell_residual.npy |
| 2024-01-22 | 2024/01/22/sigwh.npy | 2024/01/22/wsh.npy | 2024/01/22/wsp.npy | 2024/01/22/wsd.npy | 2024/01/22/was.npy | 2024/01/22/wad.npy | 2024/01/22/pwd.npy | 2024/01/22/swell_residual.npy |
| 2024-01-23 | 2024/01/23/sigwh.npy | 2024/01/23/wsh.npy | 2024/01/23/wsp.npy | 2024/01/23/wsd.npy | 2024/01/23/was.npy | 2024/01/23/wad.npy | 2024/01/23/pwd.npy | 2024/01/23/swell_residual.npy |
| 2024-01-24 | 2024/01/24/sigwh.npy | 2024/01/24/wsh.npy | 2024/01/24/wsp.npy | 2024/01/24/wsd.npy | 2024/01/24/was.npy | 2024/01/24/wad.npy | 2024/01/24/pwd.npy | 2024/01/24/swell_residual.npy |
| 2024-01-25 | 2024/01/25/sigwh.npy | 2024/01/25/wsh.npy | 2024/01/25/wsp.npy | 2024/01/25/wsd.npy | 2024/01/25/was.npy | 2024/01/25/wad.npy | 2024/01/25/pwd.npy | 2024/01/25/swell_residual.npy |
| 2024-01-26 | 2024/01/26/sigwh.npy | 2024/01/26/wsh.npy | 2024/01/26/wsp.npy | 2024/01/26/wsd.npy | 2024/01/26/was.npy | 2024/01/26/wad.npy | 2024/01/26/pwd.npy | 2024/01/26/swell_residual.npy |
| 2024-01-27 | 2024/01/27/sigwh.npy | 2024/01/27/wsh.npy | 2024/01/27/wsp.npy | 2024/01/27/wsd.npy | 2024/01/27/was.npy | 2024/01/27/wad.npy | 2024/01/27/pwd.npy | 2024/01/27/swell_residual.npy |
| 2024-01-28 | 2024/01/28/sigwh.npy | 2024/01/28/wsh.npy | 2024/01/28/wsp.npy | 2024/01/28/wsd.npy | 2024/01/28/was.npy | 2024/01/28/wad.npy | 2024/01/28/pwd.npy | 2024/01/28/swell_residual.npy |
| 2024-01-29 | 2024/01/29/sigwh.npy | 2024/01/29/wsh.npy | 2024/01/29/wsp.npy | 2024/01/29/wsd.npy | 2024/01/29/was.npy | 2024/01/29/wad.npy | 2024/01/29/pwd.npy | 2024/01/29/swell_residual.npy |
| 2024-01-30 | 2024/01/30/sigwh.npy | 2024/01/30/wsh.npy | 2024/01/30/wsp.npy | 2024/01/30/wsd.npy | 2024/01/30/was.npy | 2024/01/30/wad.npy | 2024/01/30/pwd.npy | 2024/01/30/swell_residual.npy |
| 2024-01-31 | 2024/01/31/sigwh.npy | 2024/01/31/wsh.npy | 2024/01/31/wsp.npy | 2024/01/31/wsd.npy | 2024/01/31/was.npy | 2024/01/31/wad.npy | 2024/01/31/pwd.npy | 2024/01/31/swell_residual.npy |
| 2024-02-01 | 2024/02/01/sigwh.npy | 2024/02/01/wsh.npy | 2024/02/01/wsp.npy | 2024/02/01/wsd.npy | 2024/02/01/was.npy | 2024/02/01/wad.npy | 2024/02/01/pwd.npy | 2024/02/01/swell_residual.npy |
| 2024-02-02 | 2024/02/02/sigwh.npy | 2024/02/02/wsh.npy | 2024/02/02/wsp.npy | 2024/02/02/wsd.npy | 2024/02/02/was.npy | 2024/02/02/wad.npy | 2024/02/02/pwd.npy | 2024/02/02/swell_residual.npy |
| 2024-02-03 | 2024/02/03/sigwh.npy | 2024/02/03/wsh.npy | 2024/02/03/wsp.npy | 2024/02/03/wsd.npy | 2024/02/03/was.npy | 2024/02/03/wad.npy | 2024/02/03/pwd.npy | 2024/02/03/swell_residual.npy |
| 2024-02-04 | 2024/02/04/sigwh.npy | 2024/02/04/wsh.npy | 2024/02/04/wsp.npy | 2024/02/04/wsd.npy | 2024/02/04/was.npy | 2024/02/04/wad.npy | 2024/02/04/pwd.npy | 2024/02/04/swell_residual.npy |
| 2024-02-05 | 2024/02/05/sigwh.npy | 2024/02/05/wsh.npy | 2024/02/05/wsp.npy | 2024/02/05/wsd.npy | 2024/02/05/was.npy | 2024/02/05/wad.npy | 2024/02/05/pwd.npy | 2024/02/05/swell_residual.npy |
| 2024-02-06 | 2024/02/06/sigwh.npy | 2024/02/06/wsh.npy | 2024/02/06/wsp.npy | 2024/02/06/wsd.npy | 2024/02/06/was.npy | 2024/02/06/wad.npy | 2024/02/06/pwd.npy | 2024/02/06/swell_residual.npy |
| 2024-02-07 | 2024/02/07/sigwh.npy | 2024/02/07/wsh.npy | 2024/02/07/wsp.npy | 2024/02/07/wsd.npy | 2024/02/07/was.npy | 2024/02/07/wad.npy | 2024/02/07/pwd.npy | 2024/02/07/swell_residual.npy |
| 2024-02-08 | 2024/02/08/sigwh.npy | 2024/02/08/wsh.npy | 2024/02/08/wsp.npy | 2024/02/08/wsd.npy | 2024/02/08/was.npy | 2024/02/08/wad.npy | 2024/02/08/pwd.npy | 2024/02/08/swell_residual.npy |
| 2024-02-09 | 2024/02/09/sigwh.npy | 2024/02/09/wsh.npy | 2024/02/09/wsp.npy | 2024/02/09/wsd.npy | 2024/02/09/was.npy | 2024/02/09/wad.npy | 2024/02/09/pwd.npy | 2024/02/09/swell_residual.npy |
| 2024-02-10 | 2024/02/10/sigwh.npy | 2024/02/10/wsh.npy | 2024/02/10/wsp.npy | 2024/02/10/wsd.npy | 2024/02/10/was.npy | 2024/02/10/wad.npy | 2024/02/10/pwd.npy | 2024/02/10/swell_residual.npy |
| 2024-02-11 | 2024/02/11/sigwh.npy | 2024/02/11/wsh.npy | 2024/02/11/wsp.npy | 2024/02/11/wsd.npy | 2024/02/11/was.npy | 2024/02/11/wad.npy | 2024/02/11/pwd.npy | 2024/02/11/swell_residual.npy |
| 2024-02-12 | 2024/02/12/sigwh.npy | 2024/02/12/wsh.npy | 2024/02/12/wsp.npy | 2024/02/12/wsd.npy | 2024/02/12/was.npy | 2024/02/12/wad.npy | 2024/02/12/pwd.npy | 2024/02/12/swell_residual.npy |
| 2024-02-13 | 2024/02/13/sigwh.npy | 2024/02/13/wsh.npy | 2024/02/13/wsp.npy | 2024/02/13/wsd.npy | 2024/02/13/was.npy | 2024/02/13/wad.npy | 2024/02/13/pwd.npy | 2024/02/13/swell_residual.npy |
| 2024-02-14 | 2024/02/14/sigwh.npy | 2024/02/14/wsh.npy | 2024/02/14/wsp.npy | 2024/02/14/wsd.npy | 2024/02/14/was.npy | 2024/02/14/wad.npy | 2024/02/14/pwd.npy | 2024/02/14/swell_residual.npy |
| 2024-02-15 | 2024/02/15/sigwh.npy | 2024/02/15/wsh.npy | 2024/02/15/wsp.npy | 2024/02/15/wsd.npy | 2024/02/15/was.npy | 2024/02/15/wad.npy | 2024/02/15/pwd.npy | 2024/02/15/swell_residual.npy |
| 2024-02-16 | 2024/02/16/sigwh.npy | 2024/02/16/wsh.npy | 2024/02/16/wsp.npy | 2024/02/16/wsd.npy | 2024/02/16/was.npy | 2024/02/16/wad.npy | 2024/02/16/pwd.npy | 2024/02/16/swell_residual.npy |
| 2024-02-17 | 2024/02/17/sigwh.npy | 2024/02/17/wsh.npy | 2024/02/17/wsp.npy | 2024/02/17/wsd.npy | 2024/02/17/was.npy | 2024/02/17/wad.npy | 2024/02/17/pwd.npy | 2024/02/17/swell_residual.npy |
| 2024-02-18 | 2024/02/18/sigwh.npy | 2024/02/18/wsh.npy | 2024/02/18/wsp.npy | 2024/02/18/wsd.npy | 2024/02/18/was.npy | 2024/02/18/wad.npy | 2024/02/18/pwd.npy | 2024/02/18/swell_residual.npy |
| 2024-02-19 | 2024/02/19/sigwh.npy | 2024/02/19/wsh.npy | 2024/02/19/wsp.npy | 2024/02/19/wsd.npy | 2024/02/19/was.npy | 2024/02/19/wad.npy | 2024/02/19/pwd.npy | 2024/02/19/swell_residual.npy |
| 2024-02-20 | 2024/02/20/sigwh.npy | 2024/02/20/wsh.npy | 2024/02/20/wsp.npy | 2024/02/20/wsd.npy | 2024/02/20/was.npy | 2024/02/20/wad.npy | 2024/02/20/pwd.npy | 2024/02/20/swell_residual.npy |
| 2024-02-21 | 2024/02/21/sigwh.npy | 2024/02/21/wsh.npy | 2024/02/21/wsp.npy | 2024/02/21/wsd.npy | 2024/02/21/was.npy | 2024/02/21/wad.npy | 2024/02/21/pwd.npy | 2024/02/21/swell_residual.npy |
| 2024-02-22 | 2024/02/22/sigwh.npy | 2024/02/22/wsh.npy | 2024/02/22/wsp.npy | 2024/02/22/wsd.npy | 2024/02/22/was.npy | 2024/02/22/wad.npy | 2024/02/22/pwd.npy | 2024/02/22/swell_residual.npy |
| 2024-02-23 | 2024/02/23/sigwh.npy | 2024/02/23/wsh.npy | 2024/02/23/wsp.npy | 2024/02/23/wsd.npy | 2024/02/23/was.npy | 2024/02/23/wad.npy | 2024/02/23/pwd.npy | 2024/02/23/swell_residual.npy |
| 2024-02-24 | 2024/02/24/sigwh.npy | 2024/02/24/wsh.npy | 2024/02/24/wsp.npy | 2024/02/24/wsd.npy | 2024/02/24/was.npy | 2024/02/24/wad.npy | 2024/02/24/pwd.npy | 2024/02/24/swell_residual.npy |
| 2024-02-25 | 2024/02/25/sigwh.npy | 2024/02/25/wsh.npy | 2024/02/25/wsp.npy | 2024/02/25/wsd.npy | 2024/02/25/was.npy | 2024/02/25/wad.npy | 2024/02/25/pwd.npy | 2024/02/25/swell_residual.npy |
| 2024-02-26 | 2024/02/26/sigwh.npy | 2024/02/26/wsh.npy | 2024/02/26/wsp.npy | 2024/02/26/wsd.npy | 2024/02/26/was.npy | 2024/02/26/wad.npy | 2024/02/26/pwd.npy | 2024/02/26/swell_residual.npy |
| 2024-02-27 | 2024/02/27/sigwh.npy | 2024/02/27/wsh.npy | 2024/02/27/wsp.npy | 2024/02/27/wsd.npy | 2024/02/27/was.npy | 2024/02/27/wad.npy | 2024/02/27/pwd.npy | 2024/02/27/swell_residual.npy |
| 2024-02-28 | 2024/02/28/sigwh.npy | 2024/02/28/wsh.npy | 2024/02/28/wsp.npy | 2024/02/28/wsd.npy | 2024/02/28/was.npy | 2024/02/28/wad.npy | 2024/02/28/pwd.npy | 2024/02/28/swell_residual.npy |
| 2024-02-29 | 2024/02/29/sigwh.npy | 2024/02/29/wsh.npy | 2024/02/29/wsp.npy | 2024/02/29/wsd.npy | 2024/02/29/was.npy | 2024/02/29/wad.npy | 2024/02/29/pwd.npy | 2024/02/29/swell_residual.npy |
| 2024-03-01 | 2024/03/01/sigwh.npy | 2024/03/01/wsh.npy | 2024/03/01/wsp.npy | 2024/03/01/wsd.npy | 2024/03/01/was.npy | 2024/03/01/wad.npy | 2024/03/01/pwd.npy | 2024/03/01/swell_residual.npy |
| 2024-03-02 | 2024/03/02/sigwh.npy | 2024/03/02/wsh.npy | 2024/03/02/wsp.npy | 2024/03/02/wsd.npy | 2024/03/02/was.npy | 2024/03/02/wad.npy | 2024/03/02/pwd.npy | 2024/03/02/swell_residual.npy |
| 2024-03-03 | 2024/03/03/sigwh.npy | 2024/03/03/wsh.npy | 2024/03/03/wsp.npy | 2024/03/03/wsd.npy | 2024/03/03/was.npy | 2024/03/03/wad.npy | 2024/03/03/pwd.npy | 2024/03/03/swell_residual.npy |
| 2024-03-04 | 2024/03/04/sigwh.npy | 2024/03/04/wsh.npy | 2024/03/04/wsp.npy | 2024/03/04/wsd.npy | 2024/03/04/was.npy | 2024/03/04/wad.npy | 2024/03/04/pwd.npy | 2024/03/04/swell_residual.npy |
| 2024-03-05 | 2024/03/05/sigwh.npy | 2024/03/05/wsh.npy | 2024/03/05/wsp.npy | 2024/03/05/wsd.npy | 2024/03/05/was.npy | 2024/03/05/wad.npy | 2024/03/05/pwd.npy | 2024/03/05/swell_residual.npy |
| 2024-03-06 | 2024/03/06/sigwh.npy | 2024/03/06/wsh.npy | 2024/03/06/wsp.npy | 2024/03/06/wsd.npy | 2024/03/06/was.npy | 2024/03/06/wad.npy | 2024/03/06/pwd.npy | 2024/03/06/swell_residual.npy |
| 2024-03-07 | 2024/03/07/sigwh.npy | 2024/03/07/wsh.npy | 2024/03/07/wsp.npy | 2024/03/07/wsd.npy | 2024/03/07/was.npy | 2024/03/07/wad.npy | 2024/03/07/pwd.npy | 2024/03/07/swell_residual.npy |
| 2024-03-08 | 2024/03/08/sigwh.npy | 2024/03/08/wsh.npy | 2024/03/08/wsp.npy | 2024/03/08/wsd.npy | 2024/03/08/was.npy | 2024/03/08/wad.npy | 2024/03/08/pwd.npy | 2024/03/08/swell_residual.npy |
| 2024-03-09 | 2024/03/09/sigwh.npy | 2024/03/09/wsh.npy | 2024/03/09/wsp.npy | 2024/03/09/wsd.npy | 2024/03/09/was.npy | 2024/03/09/wad.npy | 2024/03/09/pwd.npy | 2024/03/09/swell_residual.npy |
| 2024-03-10 | 2024/03/10/sigwh.npy | 2024/03/10/wsh.npy | 2024/03/10/wsp.npy | 2024/03/10/wsd.npy | 2024/03/10/was.npy | 2024/03/10/wad.npy | 2024/03/10/pwd.npy | 2024/03/10/swell_residual.npy |
| 2024-03-11 | 2024/03/11/sigwh.npy | 2024/03/11/wsh.npy | 2024/03/11/wsp.npy | 2024/03/11/wsd.npy | 2024/03/11/was.npy | 2024/03/11/wad.npy | 2024/03/11/pwd.npy | 2024/03/11/swell_residual.npy |
| 2024-03-12 | 2024/03/12/sigwh.npy | 2024/03/12/wsh.npy | 2024/03/12/wsp.npy | 2024/03/12/wsd.npy | 2024/03/12/was.npy | 2024/03/12/wad.npy | 2024/03/12/pwd.npy | 2024/03/12/swell_residual.npy |
| 2024-03-13 | 2024/03/13/sigwh.npy | 2024/03/13/wsh.npy | 2024/03/13/wsp.npy | 2024/03/13/wsd.npy | 2024/03/13/was.npy | 2024/03/13/wad.npy | 2024/03/13/pwd.npy | 2024/03/13/swell_residual.npy |
| 2024-03-14 | 2024/03/14/sigwh.npy | 2024/03/14/wsh.npy | 2024/03/14/wsp.npy | 2024/03/14/wsd.npy | 2024/03/14/was.npy | 2024/03/14/wad.npy | 2024/03/14/pwd.npy | 2024/03/14/swell_residual.npy |
| 2024-03-15 | 2024/03/15/sigwh.npy | 2024/03/15/wsh.npy | 2024/03/15/wsp.npy | 2024/03/15/wsd.npy | 2024/03/15/was.npy | 2024/03/15/wad.npy | 2024/03/15/pwd.npy | 2024/03/15/swell_residual.npy |
| 2024-03-16 | 2024/03/16/sigwh.npy | 2024/03/16/wsh.npy | 2024/03/16/wsp.npy | 2024/03/16/wsd.npy | 2024/03/16/was.npy | 2024/03/16/wad.npy | 2024/03/16/pwd.npy | 2024/03/16/swell_residual.npy |
| 2024-03-17 | 2024/03/17/sigwh.npy | 2024/03/17/wsh.npy | 2024/03/17/wsp.npy | 2024/03/17/wsd.npy | 2024/03/17/was.npy | 2024/03/17/wad.npy | 2024/03/17/pwd.npy | 2024/03/17/swell_residual.npy |
| 2024-03-18 | 2024/03/18/sigwh.npy | 2024/03/18/wsh.npy | 2024/03/18/wsp.npy | 2024/03/18/wsd.npy | 2024/03/18/was.npy | 2024/03/18/wad.npy | 2024/03/18/pwd.npy | 2024/03/18/swell_residual.npy |
| 2024-03-19 | 2024/03/19/sigwh.npy | 2024/03/19/wsh.npy | 2024/03/19/wsp.npy | 2024/03/19/wsd.npy | 2024/03/19/was.npy | 2024/03/19/wad.npy | 2024/03/19/pwd.npy | 2024/03/19/swell_residual.npy |
| 2024-03-20 | 2024/03/20/sigwh.npy | 2024/03/20/wsh.npy | 2024/03/20/wsp.npy | 2024/03/20/wsd.npy | 2024/03/20/was.npy | 2024/03/20/wad.npy | 2024/03/20/pwd.npy | 2024/03/20/swell_residual.npy |
| 2024-03-21 | 2024/03/21/sigwh.npy | 2024/03/21/wsh.npy | 2024/03/21/wsp.npy | 2024/03/21/wsd.npy | 2024/03/21/was.npy | 2024/03/21/wad.npy | 2024/03/21/pwd.npy | 2024/03/21/swell_residual.npy |
| 2024-03-22 | 2024/03/22/sigwh.npy | 2024/03/22/wsh.npy | 2024/03/22/wsp.npy | 2024/03/22/wsd.npy | 2024/03/22/was.npy | 2024/03/22/wad.npy | 2024/03/22/pwd.npy | 2024/03/22/swell_residual.npy |
| 2024-03-23 | 2024/03/23/sigwh.npy | 2024/03/23/wsh.npy | 2024/03/23/wsp.npy | 2024/03/23/wsd.npy | 2024/03/23/was.npy | 2024/03/23/wad.npy | 2024/03/23/pwd.npy | 2024/03/23/swell_residual.npy |
| 2024-03-24 | 2024/03/24/sigwh.npy | 2024/03/24/wsh.npy | 2024/03/24/wsp.npy | 2024/03/24/wsd.npy | 2024/03/24/was.npy | 2024/03/24/wad.npy | 2024/03/24/pwd.npy | 2024/03/24/swell_residual.npy |
| 2024-03-25 | 2024/03/25/sigwh.npy | 2024/03/25/wsh.npy | 2024/03/25/wsp.npy | 2024/03/25/wsd.npy | 2024/03/25/was.npy | 2024/03/25/wad.npy | 2024/03/25/pwd.npy | 2024/03/25/swell_residual.npy |
| 2024-03-26 | 2024/03/26/sigwh.npy | 2024/03/26/wsh.npy | 2024/03/26/wsp.npy | 2024/03/26/wsd.npy | 2024/03/26/was.npy | 2024/03/26/wad.npy | 2024/03/26/pwd.npy | 2024/03/26/swell_residual.npy |
| 2024-03-27 | 2024/03/27/sigwh.npy | 2024/03/27/wsh.npy | 2024/03/27/wsp.npy | 2024/03/27/wsd.npy | 2024/03/27/was.npy | 2024/03/27/wad.npy | 2024/03/27/pwd.npy | 2024/03/27/swell_residual.npy |
| 2024-03-28 | 2024/03/28/sigwh.npy | 2024/03/28/wsh.npy | 2024/03/28/wsp.npy | 2024/03/28/wsd.npy | 2024/03/28/was.npy | 2024/03/28/wad.npy | 2024/03/28/pwd.npy | 2024/03/28/swell_residual.npy |
| 2024-03-29 | 2024/03/29/sigwh.npy | 2024/03/29/wsh.npy | 2024/03/29/wsp.npy | 2024/03/29/wsd.npy | 2024/03/29/was.npy | 2024/03/29/wad.npy | 2024/03/29/pwd.npy | 2024/03/29/swell_residual.npy |
| 2024-03-30 | 2024/03/30/sigwh.npy | 2024/03/30/wsh.npy | 2024/03/30/wsp.npy | 2024/03/30/wsd.npy | 2024/03/30/was.npy | 2024/03/30/wad.npy | 2024/03/30/pwd.npy | 2024/03/30/swell_residual.npy |
| 2024-03-31 | 2024/03/31/sigwh.npy | 2024/03/31/wsh.npy | 2024/03/31/wsp.npy | 2024/03/31/wsd.npy | 2024/03/31/was.npy | 2024/03/31/wad.npy | 2024/03/31/pwd.npy | 2024/03/31/swell_residual.npy |
| 2024-04-01 | 2024/04/01/sigwh.npy | 2024/04/01/wsh.npy | 2024/04/01/wsp.npy | 2024/04/01/wsd.npy | 2024/04/01/was.npy | 2024/04/01/wad.npy | 2024/04/01/pwd.npy | 2024/04/01/swell_residual.npy |
| 2024-04-02 | 2024/04/02/sigwh.npy | 2024/04/02/wsh.npy | 2024/04/02/wsp.npy | 2024/04/02/wsd.npy | 2024/04/02/was.npy | 2024/04/02/wad.npy | 2024/04/02/pwd.npy | 2024/04/02/swell_residual.npy |
| 2024-04-03 | 2024/04/03/sigwh.npy | 2024/04/03/wsh.npy | 2024/04/03/wsp.npy | 2024/04/03/wsd.npy | 2024/04/03/was.npy | 2024/04/03/wad.npy | 2024/04/03/pwd.npy | 2024/04/03/swell_residual.npy |
| 2024-04-04 | 2024/04/04/sigwh.npy | 2024/04/04/wsh.npy | 2024/04/04/wsp.npy | 2024/04/04/wsd.npy | 2024/04/04/was.npy | 2024/04/04/wad.npy | 2024/04/04/pwd.npy | 2024/04/04/swell_residual.npy |
| 2024-04-05 | 2024/04/05/sigwh.npy | 2024/04/05/wsh.npy | 2024/04/05/wsp.npy | 2024/04/05/wsd.npy | 2024/04/05/was.npy | 2024/04/05/wad.npy | 2024/04/05/pwd.npy | 2024/04/05/swell_residual.npy |
| 2024-04-06 | 2024/04/06/sigwh.npy | 2024/04/06/wsh.npy | 2024/04/06/wsp.npy | 2024/04/06/wsd.npy | 2024/04/06/was.npy | 2024/04/06/wad.npy | 2024/04/06/pwd.npy | 2024/04/06/swell_residual.npy |
| 2024-04-07 | 2024/04/07/sigwh.npy | 2024/04/07/wsh.npy | 2024/04/07/wsp.npy | 2024/04/07/wsd.npy | 2024/04/07/was.npy | 2024/04/07/wad.npy | 2024/04/07/pwd.npy | 2024/04/07/swell_residual.npy |
| 2024-04-08 | 2024/04/08/sigwh.npy | 2024/04/08/wsh.npy | 2024/04/08/wsp.npy | 2024/04/08/wsd.npy | 2024/04/08/was.npy | 2024/04/08/wad.npy | 2024/04/08/pwd.npy | 2024/04/08/swell_residual.npy |
| 2024-04-09 | 2024/04/09/sigwh.npy | 2024/04/09/wsh.npy | 2024/04/09/wsp.npy | 2024/04/09/wsd.npy | 2024/04/09/was.npy | 2024/04/09/wad.npy | 2024/04/09/pwd.npy | 2024/04/09/swell_residual.npy |
| 2024-04-10 | 2024/04/10/sigwh.npy | 2024/04/10/wsh.npy | 2024/04/10/wsp.npy | 2024/04/10/wsd.npy | 2024/04/10/was.npy | 2024/04/10/wad.npy | 2024/04/10/pwd.npy | 2024/04/10/swell_residual.npy |
| 2024-04-11 | 2024/04/11/sigwh.npy | 2024/04/11/wsh.npy | 2024/04/11/wsp.npy | 2024/04/11/wsd.npy | 2024/04/11/was.npy | 2024/04/11/wad.npy | 2024/04/11/pwd.npy | 2024/04/11/swell_residual.npy |
| 2024-04-12 | 2024/04/12/sigwh.npy | 2024/04/12/wsh.npy | 2024/04/12/wsp.npy | 2024/04/12/wsd.npy | 2024/04/12/was.npy | 2024/04/12/wad.npy | 2024/04/12/pwd.npy | 2024/04/12/swell_residual.npy |
| 2024-04-13 | 2024/04/13/sigwh.npy | 2024/04/13/wsh.npy | 2024/04/13/wsp.npy | 2024/04/13/wsd.npy | 2024/04/13/was.npy | 2024/04/13/wad.npy | 2024/04/13/pwd.npy | 2024/04/13/swell_residual.npy |
| 2024-04-14 | 2024/04/14/sigwh.npy | 2024/04/14/wsh.npy | 2024/04/14/wsp.npy | 2024/04/14/wsd.npy | 2024/04/14/was.npy | 2024/04/14/wad.npy | 2024/04/14/pwd.npy | 2024/04/14/swell_residual.npy |
| 2024-04-15 | 2024/04/15/sigwh.npy | 2024/04/15/wsh.npy | 2024/04/15/wsp.npy | 2024/04/15/wsd.npy | 2024/04/15/was.npy | 2024/04/15/wad.npy | 2024/04/15/pwd.npy | 2024/04/15/swell_residual.npy |
| 2024-04-16 | 2024/04/16/sigwh.npy | 2024/04/16/wsh.npy | 2024/04/16/wsp.npy | 2024/04/16/wsd.npy | 2024/04/16/was.npy | 2024/04/16/wad.npy | 2024/04/16/pwd.npy | 2024/04/16/swell_residual.npy |
| 2024-04-17 | 2024/04/17/sigwh.npy | 2024/04/17/wsh.npy | 2024/04/17/wsp.npy | 2024/04/17/wsd.npy | 2024/04/17/was.npy | 2024/04/17/wad.npy | 2024/04/17/pwd.npy | 2024/04/17/swell_residual.npy |
| 2024-04-18 | 2024/04/18/sigwh.npy | 2024/04/18/wsh.npy | 2024/04/18/wsp.npy | 2024/04/18/wsd.npy | 2024/04/18/was.npy | 2024/04/18/wad.npy | 2024/04/18/pwd.npy | 2024/04/18/swell_residual.npy |
| 2024-04-19 | 2024/04/19/sigwh.npy | 2024/04/19/wsh.npy | 2024/04/19/wsp.npy | 2024/04/19/wsd.npy | 2024/04/19/was.npy | 2024/04/19/wad.npy | 2024/04/19/pwd.npy | 2024/04/19/swell_residual.npy |
| 2024-04-20 | 2024/04/20/sigwh.npy | 2024/04/20/wsh.npy | 2024/04/20/wsp.npy | 2024/04/20/wsd.npy | 2024/04/20/was.npy | 2024/04/20/wad.npy | 2024/04/20/pwd.npy | 2024/04/20/swell_residual.npy |
| 2024-04-21 | 2024/04/21/sigwh.npy | 2024/04/21/wsh.npy | 2024/04/21/wsp.npy | 2024/04/21/wsd.npy | 2024/04/21/was.npy | 2024/04/21/wad.npy | 2024/04/21/pwd.npy | 2024/04/21/swell_residual.npy |
| 2024-04-22 | 2024/04/22/sigwh.npy | 2024/04/22/wsh.npy | 2024/04/22/wsp.npy | 2024/04/22/wsd.npy | 2024/04/22/was.npy | 2024/04/22/wad.npy | 2024/04/22/pwd.npy | 2024/04/22/swell_residual.npy |
| 2024-04-23 | 2024/04/23/sigwh.npy | 2024/04/23/wsh.npy | 2024/04/23/wsp.npy | 2024/04/23/wsd.npy | 2024/04/23/was.npy | 2024/04/23/wad.npy | 2024/04/23/pwd.npy | 2024/04/23/swell_residual.npy |
| 2024-04-24 | 2024/04/24/sigwh.npy | 2024/04/24/wsh.npy | 2024/04/24/wsp.npy | 2024/04/24/wsd.npy | 2024/04/24/was.npy | 2024/04/24/wad.npy | 2024/04/24/pwd.npy | 2024/04/24/swell_residual.npy |
| 2024-04-25 | 2024/04/25/sigwh.npy | 2024/04/25/wsh.npy | 2024/04/25/wsp.npy | 2024/04/25/wsd.npy | 2024/04/25/was.npy | 2024/04/25/wad.npy | 2024/04/25/pwd.npy | 2024/04/25/swell_residual.npy |
| 2024-04-26 | 2024/04/26/sigwh.npy | 2024/04/26/wsh.npy | 2024/04/26/wsp.npy | 2024/04/26/wsd.npy | 2024/04/26/was.npy | 2024/04/26/wad.npy | 2024/04/26/pwd.npy | 2024/04/26/swell_residual.npy |
| 2024-04-27 | 2024/04/27/sigwh.npy | 2024/04/27/wsh.npy | 2024/04/27/wsp.npy | 2024/04/27/wsd.npy | 2024/04/27/was.npy | 2024/04/27/wad.npy | 2024/04/27/pwd.npy | 2024/04/27/swell_residual.npy |
| 2024-04-28 | 2024/04/28/sigwh.npy | 2024/04/28/wsh.npy | 2024/04/28/wsp.npy | 2024/04/28/wsd.npy | 2024/04/28/was.npy | 2024/04/28/wad.npy | 2024/04/28/pwd.npy | 2024/04/28/swell_residual.npy |
| 2024-04-29 | 2024/04/29/sigwh.npy | 2024/04/29/wsh.npy | 2024/04/29/wsp.npy | 2024/04/29/wsd.npy | 2024/04/29/was.npy | 2024/04/29/wad.npy | 2024/04/29/pwd.npy | 2024/04/29/swell_residual.npy |
| 2024-04-30 | 2024/04/30/sigwh.npy | 2024/04/30/wsh.npy | 2024/04/30/wsp.npy | 2024/04/30/wsd.npy | 2024/04/30/was.npy | 2024/04/30/wad.npy | 2024/04/30/pwd.npy | 2024/04/30/swell_residual.npy |
| 2024-05-01 | 2024/05/01/sigwh.npy | 2024/05/01/wsh.npy | 2024/05/01/wsp.npy | 2024/05/01/wsd.npy | 2024/05/01/was.npy | 2024/05/01/wad.npy | 2024/05/01/pwd.npy | 2024/05/01/swell_residual.npy |
| 2024-05-02 | 2024/05/02/sigwh.npy | 2024/05/02/wsh.npy | 2024/05/02/wsp.npy | 2024/05/02/wsd.npy | 2024/05/02/was.npy | 2024/05/02/wad.npy | 2024/05/02/pwd.npy | 2024/05/02/swell_residual.npy |
| 2024-05-03 | 2024/05/03/sigwh.npy | 2024/05/03/wsh.npy | 2024/05/03/wsp.npy | 2024/05/03/wsd.npy | 2024/05/03/was.npy | 2024/05/03/wad.npy | 2024/05/03/pwd.npy | 2024/05/03/swell_residual.npy |
| 2024-05-04 | 2024/05/04/sigwh.npy | 2024/05/04/wsh.npy | 2024/05/04/wsp.npy | 2024/05/04/wsd.npy | 2024/05/04/was.npy | 2024/05/04/wad.npy | 2024/05/04/pwd.npy | 2024/05/04/swell_residual.npy |
| 2024-05-05 | 2024/05/05/sigwh.npy | 2024/05/05/wsh.npy | 2024/05/05/wsp.npy | 2024/05/05/wsd.npy | 2024/05/05/was.npy | 2024/05/05/wad.npy | 2024/05/05/pwd.npy | 2024/05/05/swell_residual.npy |
| 2024-05-06 | 2024/05/06/sigwh.npy | 2024/05/06/wsh.npy | 2024/05/06/wsp.npy | 2024/05/06/wsd.npy | 2024/05/06/was.npy | 2024/05/06/wad.npy | 2024/05/06/pwd.npy | 2024/05/06/swell_residual.npy |
| 2024-05-07 | 2024/05/07/sigwh.npy | 2024/05/07/wsh.npy | 2024/05/07/wsp.npy | 2024/05/07/wsd.npy | 2024/05/07/was.npy | 2024/05/07/wad.npy | 2024/05/07/pwd.npy | 2024/05/07/swell_residual.npy |
| 2024-05-08 | 2024/05/08/sigwh.npy | 2024/05/08/wsh.npy | 2024/05/08/wsp.npy | 2024/05/08/wsd.npy | 2024/05/08/was.npy | 2024/05/08/wad.npy | 2024/05/08/pwd.npy | 2024/05/08/swell_residual.npy |
| 2024-05-09 | 2024/05/09/sigwh.npy | 2024/05/09/wsh.npy | 2024/05/09/wsp.npy | 2024/05/09/wsd.npy | 2024/05/09/was.npy | 2024/05/09/wad.npy | 2024/05/09/pwd.npy | 2024/05/09/swell_residual.npy |
| 2024-05-10 | 2024/05/10/sigwh.npy | 2024/05/10/wsh.npy | 2024/05/10/wsp.npy | 2024/05/10/wsd.npy | 2024/05/10/was.npy | 2024/05/10/wad.npy | 2024/05/10/pwd.npy | 2024/05/10/swell_residual.npy |
| 2024-05-11 | 2024/05/11/sigwh.npy | 2024/05/11/wsh.npy | 2024/05/11/wsp.npy | 2024/05/11/wsd.npy | 2024/05/11/was.npy | 2024/05/11/wad.npy | 2024/05/11/pwd.npy | 2024/05/11/swell_residual.npy |
| 2024-05-12 | 2024/05/12/sigwh.npy | 2024/05/12/wsh.npy | 2024/05/12/wsp.npy | 2024/05/12/wsd.npy | 2024/05/12/was.npy | 2024/05/12/wad.npy | 2024/05/12/pwd.npy | 2024/05/12/swell_residual.npy |
| 2024-05-13 | 2024/05/13/sigwh.npy | 2024/05/13/wsh.npy | 2024/05/13/wsp.npy | 2024/05/13/wsd.npy | 2024/05/13/was.npy | 2024/05/13/wad.npy | 2024/05/13/pwd.npy | 2024/05/13/swell_residual.npy |
| 2024-05-14 | 2024/05/14/sigwh.npy | 2024/05/14/wsh.npy | 2024/05/14/wsp.npy | 2024/05/14/wsd.npy | 2024/05/14/was.npy | 2024/05/14/wad.npy | 2024/05/14/pwd.npy | 2024/05/14/swell_residual.npy |
| 2024-05-15 | 2024/05/15/sigwh.npy | 2024/05/15/wsh.npy | 2024/05/15/wsp.npy | 2024/05/15/wsd.npy | 2024/05/15/was.npy | 2024/05/15/wad.npy | 2024/05/15/pwd.npy | 2024/05/15/swell_residual.npy |
| 2024-05-16 | 2024/05/16/sigwh.npy | 2024/05/16/wsh.npy | 2024/05/16/wsp.npy | 2024/05/16/wsd.npy | 2024/05/16/was.npy | 2024/05/16/wad.npy | 2024/05/16/pwd.npy | 2024/05/16/swell_residual.npy |
| 2024-05-17 | 2024/05/17/sigwh.npy | 2024/05/17/wsh.npy | 2024/05/17/wsp.npy | 2024/05/17/wsd.npy | 2024/05/17/was.npy | 2024/05/17/wad.npy | 2024/05/17/pwd.npy | 2024/05/17/swell_residual.npy |
| 2024-05-18 | 2024/05/18/sigwh.npy | 2024/05/18/wsh.npy | 2024/05/18/wsp.npy | 2024/05/18/wsd.npy | 2024/05/18/was.npy | 2024/05/18/wad.npy | 2024/05/18/pwd.npy | 2024/05/18/swell_residual.npy |
| 2024-05-19 | 2024/05/19/sigwh.npy | 2024/05/19/wsh.npy | 2024/05/19/wsp.npy | 2024/05/19/wsd.npy | 2024/05/19/was.npy | 2024/05/19/wad.npy | 2024/05/19/pwd.npy | 2024/05/19/swell_residual.npy |
| 2024-05-20 | 2024/05/20/sigwh.npy | 2024/05/20/wsh.npy | 2024/05/20/wsp.npy | 2024/05/20/wsd.npy | 2024/05/20/was.npy | 2024/05/20/wad.npy | 2024/05/20/pwd.npy | 2024/05/20/swell_residual.npy |
| 2024-05-21 | 2024/05/21/sigwh.npy | 2024/05/21/wsh.npy | 2024/05/21/wsp.npy | 2024/05/21/wsd.npy | 2024/05/21/was.npy | 2024/05/21/wad.npy | 2024/05/21/pwd.npy | 2024/05/21/swell_residual.npy |
| 2024-05-22 | 2024/05/22/sigwh.npy | 2024/05/22/wsh.npy | 2024/05/22/wsp.npy | 2024/05/22/wsd.npy | 2024/05/22/was.npy | 2024/05/22/wad.npy | 2024/05/22/pwd.npy | 2024/05/22/swell_residual.npy |
| 2024-05-23 | 2024/05/23/sigwh.npy | 2024/05/23/wsh.npy | 2024/05/23/wsp.npy | 2024/05/23/wsd.npy | 2024/05/23/was.npy | 2024/05/23/wad.npy | 2024/05/23/pwd.npy | 2024/05/23/swell_residual.npy |
| 2024-05-24 | 2024/05/24/sigwh.npy | 2024/05/24/wsh.npy | 2024/05/24/wsp.npy | 2024/05/24/wsd.npy | 2024/05/24/was.npy | 2024/05/24/wad.npy | 2024/05/24/pwd.npy | 2024/05/24/swell_residual.npy |
| 2024-05-25 | 2024/05/25/sigwh.npy | 2024/05/25/wsh.npy | 2024/05/25/wsp.npy | 2024/05/25/wsd.npy | 2024/05/25/was.npy | 2024/05/25/wad.npy | 2024/05/25/pwd.npy | 2024/05/25/swell_residual.npy |
| 2024-05-26 | 2024/05/26/sigwh.npy | 2024/05/26/wsh.npy | 2024/05/26/wsp.npy | 2024/05/26/wsd.npy | 2024/05/26/was.npy | 2024/05/26/wad.npy | 2024/05/26/pwd.npy | 2024/05/26/swell_residual.npy |
| 2024-05-27 | 2024/05/27/sigwh.npy | 2024/05/27/wsh.npy | 2024/05/27/wsp.npy | 2024/05/27/wsd.npy | 2024/05/27/was.npy | 2024/05/27/wad.npy | 2024/05/27/pwd.npy | 2024/05/27/swell_residual.npy |
| 2024-05-28 | 2024/05/28/sigwh.npy | 2024/05/28/wsh.npy | 2024/05/28/wsp.npy | 2024/05/28/wsd.npy | 2024/05/28/was.npy | 2024/05/28/wad.npy | 2024/05/28/pwd.npy | 2024/05/28/swell_residual.npy |
| 2024-05-29 | 2024/05/29/sigwh.npy | 2024/05/29/wsh.npy | 2024/05/29/wsp.npy | 2024/05/29/wsd.npy | 2024/05/29/was.npy | 2024/05/29/wad.npy | 2024/05/29/pwd.npy | 2024/05/29/swell_residual.npy |
| 2024-05-30 | 2024/05/30/sigwh.npy | 2024/05/30/wsh.npy | 2024/05/30/wsp.npy | 2024/05/30/wsd.npy | 2024/05/30/was.npy | 2024/05/30/wad.npy | 2024/05/30/pwd.npy | 2024/05/30/swell_residual.npy |
| 2024-05-31 | 2024/05/31/sigwh.npy | 2024/05/31/wsh.npy | 2024/05/31/wsp.npy | 2024/05/31/wsd.npy | 2024/05/31/was.npy | 2024/05/31/wad.npy | 2024/05/31/pwd.npy | 2024/05/31/swell_residual.npy |
| 2024-06-01 | 2024/06/01/sigwh.npy | 2024/06/01/wsh.npy | 2024/06/01/wsp.npy | 2024/06/01/wsd.npy | 2024/06/01/was.npy | 2024/06/01/wad.npy | 2024/06/01/pwd.npy | 2024/06/01/swell_residual.npy |
| 2024-06-02 | 2024/06/02/sigwh.npy | 2024/06/02/wsh.npy | 2024/06/02/wsp.npy | 2024/06/02/wsd.npy | 2024/06/02/was.npy | 2024/06/02/wad.npy | 2024/06/02/pwd.npy | 2024/06/02/swell_residual.npy |
| 2024-06-03 | 2024/06/03/sigwh.npy | 2024/06/03/wsh.npy | 2024/06/03/wsp.npy | 2024/06/03/wsd.npy | 2024/06/03/was.npy | 2024/06/03/wad.npy | 2024/06/03/pwd.npy | 2024/06/03/swell_residual.npy |
| 2024-06-04 | 2024/06/04/sigwh.npy | 2024/06/04/wsh.npy | 2024/06/04/wsp.npy | 2024/06/04/wsd.npy | 2024/06/04/was.npy | 2024/06/04/wad.npy | 2024/06/04/pwd.npy | 2024/06/04/swell_residual.npy |
| 2024-06-05 | 2024/06/05/sigwh.npy | 2024/06/05/wsh.npy | 2024/06/05/wsp.npy | 2024/06/05/wsd.npy | 2024/06/05/was.npy | 2024/06/05/wad.npy | 2024/06/05/pwd.npy | 2024/06/05/swell_residual.npy |
| 2024-06-06 | 2024/06/06/sigwh.npy | 2024/06/06/wsh.npy | 2024/06/06/wsp.npy | 2024/06/06/wsd.npy | 2024/06/06/was.npy | 2024/06/06/wad.npy | 2024/06/06/pwd.npy | 2024/06/06/swell_residual.npy |
| 2024-06-07 | 2024/06/07/sigwh.npy | 2024/06/07/wsh.npy | 2024/06/07/wsp.npy | 2024/06/07/wsd.npy | 2024/06/07/was.npy | 2024/06/07/wad.npy | 2024/06/07/pwd.npy | 2024/06/07/swell_residual.npy |
| 2024-06-08 | 2024/06/08/sigwh.npy | 2024/06/08/wsh.npy | 2024/06/08/wsp.npy | 2024/06/08/wsd.npy | 2024/06/08/was.npy | 2024/06/08/wad.npy | 2024/06/08/pwd.npy | 2024/06/08/swell_residual.npy |
| 2024-06-09 | 2024/06/09/sigwh.npy | 2024/06/09/wsh.npy | 2024/06/09/wsp.npy | 2024/06/09/wsd.npy | 2024/06/09/was.npy | 2024/06/09/wad.npy | 2024/06/09/pwd.npy | 2024/06/09/swell_residual.npy |
| 2024-06-10 | 2024/06/10/sigwh.npy | 2024/06/10/wsh.npy | 2024/06/10/wsp.npy | 2024/06/10/wsd.npy | 2024/06/10/was.npy | 2024/06/10/wad.npy | 2024/06/10/pwd.npy | 2024/06/10/swell_residual.npy |
| 2024-06-11 | 2024/06/11/sigwh.npy | 2024/06/11/wsh.npy | 2024/06/11/wsp.npy | 2024/06/11/wsd.npy | 2024/06/11/was.npy | 2024/06/11/wad.npy | 2024/06/11/pwd.npy | 2024/06/11/swell_residual.npy |
| 2024-06-12 | 2024/06/12/sigwh.npy | 2024/06/12/wsh.npy | 2024/06/12/wsp.npy | 2024/06/12/wsd.npy | 2024/06/12/was.npy | 2024/06/12/wad.npy | 2024/06/12/pwd.npy | 2024/06/12/swell_residual.npy |
| 2024-06-13 | 2024/06/13/sigwh.npy | 2024/06/13/wsh.npy | 2024/06/13/wsp.npy | 2024/06/13/wsd.npy | 2024/06/13/was.npy | 2024/06/13/wad.npy | 2024/06/13/pwd.npy | 2024/06/13/swell_residual.npy |
| 2024-06-14 | 2024/06/14/sigwh.npy | 2024/06/14/wsh.npy | 2024/06/14/wsp.npy | 2024/06/14/wsd.npy | 2024/06/14/was.npy | 2024/06/14/wad.npy | 2024/06/14/pwd.npy | 2024/06/14/swell_residual.npy |
| 2024-06-15 | 2024/06/15/sigwh.npy | 2024/06/15/wsh.npy | 2024/06/15/wsp.npy | 2024/06/15/wsd.npy | 2024/06/15/was.npy | 2024/06/15/wad.npy | 2024/06/15/pwd.npy | 2024/06/15/swell_residual.npy |
| 2024-06-16 | 2024/06/16/sigwh.npy | 2024/06/16/wsh.npy | 2024/06/16/wsp.npy | 2024/06/16/wsd.npy | 2024/06/16/was.npy | 2024/06/16/wad.npy | 2024/06/16/pwd.npy | 2024/06/16/swell_residual.npy |
| 2024-06-17 | 2024/06/17/sigwh.npy | 2024/06/17/wsh.npy | 2024/06/17/wsp.npy | 2024/06/17/wsd.npy | 2024/06/17/was.npy | 2024/06/17/wad.npy | 2024/06/17/pwd.npy | 2024/06/17/swell_residual.npy |
| 2024-06-18 | 2024/06/18/sigwh.npy | 2024/06/18/wsh.npy | 2024/06/18/wsp.npy | 2024/06/18/wsd.npy | 2024/06/18/was.npy | 2024/06/18/wad.npy | 2024/06/18/pwd.npy | 2024/06/18/swell_residual.npy |
| 2024-06-19 | 2024/06/19/sigwh.npy | 2024/06/19/wsh.npy | 2024/06/19/wsp.npy | 2024/06/19/wsd.npy | 2024/06/19/was.npy | 2024/06/19/wad.npy | 2024/06/19/pwd.npy | 2024/06/19/swell_residual.npy |
| 2024-06-20 | 2024/06/20/sigwh.npy | 2024/06/20/wsh.npy | 2024/06/20/wsp.npy | 2024/06/20/wsd.npy | 2024/06/20/was.npy | 2024/06/20/wad.npy | 2024/06/20/pwd.npy | 2024/06/20/swell_residual.npy |
| 2024-06-21 | 2024/06/21/sigwh.npy | 2024/06/21/wsh.npy | 2024/06/21/wsp.npy | 2024/06/21/wsd.npy | 2024/06/21/was.npy | 2024/06/21/wad.npy | 2024/06/21/pwd.npy | 2024/06/21/swell_residual.npy |
| 2024-06-22 | 2024/06/22/sigwh.npy | 2024/06/22/wsh.npy | 2024/06/22/wsp.npy | 2024/06/22/wsd.npy | 2024/06/22/was.npy | 2024/06/22/wad.npy | 2024/06/22/pwd.npy | 2024/06/22/swell_residual.npy |
| 2024-06-23 | 2024/06/23/sigwh.npy | 2024/06/23/wsh.npy | 2024/06/23/wsp.npy | 2024/06/23/wsd.npy | 2024/06/23/was.npy | 2024/06/23/wad.npy | 2024/06/23/pwd.npy | 2024/06/23/swell_residual.npy |
| 2024-06-24 | 2024/06/24/sigwh.npy | 2024/06/24/wsh.npy | 2024/06/24/wsp.npy | 2024/06/24/wsd.npy | 2024/06/24/was.npy | 2024/06/24/wad.npy | 2024/06/24/pwd.npy | 2024/06/24/swell_residual.npy |
| 2024-06-25 | 2024/06/25/sigwh.npy | 2024/06/25/wsh.npy | 2024/06/25/wsp.npy | 2024/06/25/wsd.npy | 2024/06/25/was.npy | 2024/06/25/wad.npy | 2024/06/25/pwd.npy | 2024/06/25/swell_residual.npy |
| 2024-06-26 | 2024/06/26/sigwh.npy | 2024/06/26/wsh.npy | 2024/06/26/wsp.npy | 2024/06/26/wsd.npy | 2024/06/26/was.npy | 2024/06/26/wad.npy | 2024/06/26/pwd.npy | 2024/06/26/swell_residual.npy |
| 2024-06-27 | 2024/06/27/sigwh.npy | 2024/06/27/wsh.npy | 2024/06/27/wsp.npy | 2024/06/27/wsd.npy | 2024/06/27/was.npy | 2024/06/27/wad.npy | 2024/06/27/pwd.npy | 2024/06/27/swell_residual.npy |
| 2024-06-28 | 2024/06/28/sigwh.npy | 2024/06/28/wsh.npy | 2024/06/28/wsp.npy | 2024/06/28/wsd.npy | 2024/06/28/was.npy | 2024/06/28/wad.npy | 2024/06/28/pwd.npy | 2024/06/28/swell_residual.npy |
| 2024-06-29 | 2024/06/29/sigwh.npy | 2024/06/29/wsh.npy | 2024/06/29/wsp.npy | 2024/06/29/wsd.npy | 2024/06/29/was.npy | 2024/06/29/wad.npy | 2024/06/29/pwd.npy | 2024/06/29/swell_residual.npy |
| 2024-06-30 | 2024/06/30/sigwh.npy | 2024/06/30/wsh.npy | 2024/06/30/wsp.npy | 2024/06/30/wsd.npy | 2024/06/30/was.npy | 2024/06/30/wad.npy | 2024/06/30/pwd.npy | 2024/06/30/swell_residual.npy |
| 2024-07-01 | 2024/07/01/sigwh.npy | 2024/07/01/wsh.npy | 2024/07/01/wsp.npy | 2024/07/01/wsd.npy | 2024/07/01/was.npy | 2024/07/01/wad.npy | 2024/07/01/pwd.npy | 2024/07/01/swell_residual.npy |
| 2024-07-02 | 2024/07/02/sigwh.npy | 2024/07/02/wsh.npy | 2024/07/02/wsp.npy | 2024/07/02/wsd.npy | 2024/07/02/was.npy | 2024/07/02/wad.npy | 2024/07/02/pwd.npy | 2024/07/02/swell_residual.npy |
| 2024-07-03 | 2024/07/03/sigwh.npy | 2024/07/03/wsh.npy | 2024/07/03/wsp.npy | 2024/07/03/wsd.npy | 2024/07/03/was.npy | 2024/07/03/wad.npy | 2024/07/03/pwd.npy | 2024/07/03/swell_residual.npy |
| 2024-07-04 | 2024/07/04/sigwh.npy | 2024/07/04/wsh.npy | 2024/07/04/wsp.npy | 2024/07/04/wsd.npy | 2024/07/04/was.npy | 2024/07/04/wad.npy | 2024/07/04/pwd.npy | 2024/07/04/swell_residual.npy |
| 2024-07-05 | 2024/07/05/sigwh.npy | 2024/07/05/wsh.npy | 2024/07/05/wsp.npy | 2024/07/05/wsd.npy | 2024/07/05/was.npy | 2024/07/05/wad.npy | 2024/07/05/pwd.npy | 2024/07/05/swell_residual.npy |
| 2024-07-06 | 2024/07/06/sigwh.npy | 2024/07/06/wsh.npy | 2024/07/06/wsp.npy | 2024/07/06/wsd.npy | 2024/07/06/was.npy | 2024/07/06/wad.npy | 2024/07/06/pwd.npy | 2024/07/06/swell_residual.npy |
| 2024-07-07 | 2024/07/07/sigwh.npy | 2024/07/07/wsh.npy | 2024/07/07/wsp.npy | 2024/07/07/wsd.npy | 2024/07/07/was.npy | 2024/07/07/wad.npy | 2024/07/07/pwd.npy | 2024/07/07/swell_residual.npy |
| 2024-07-08 | 2024/07/08/sigwh.npy | 2024/07/08/wsh.npy | 2024/07/08/wsp.npy | 2024/07/08/wsd.npy | 2024/07/08/was.npy | 2024/07/08/wad.npy | 2024/07/08/pwd.npy | 2024/07/08/swell_residual.npy |
| 2024-07-09 | 2024/07/09/sigwh.npy | 2024/07/09/wsh.npy | 2024/07/09/wsp.npy | 2024/07/09/wsd.npy | 2024/07/09/was.npy | 2024/07/09/wad.npy | 2024/07/09/pwd.npy | 2024/07/09/swell_residual.npy |
| 2024-07-10 | 2024/07/10/sigwh.npy | 2024/07/10/wsh.npy | 2024/07/10/wsp.npy | 2024/07/10/wsd.npy | 2024/07/10/was.npy | 2024/07/10/wad.npy | 2024/07/10/pwd.npy | 2024/07/10/swell_residual.npy |
| 2024-07-11 | 2024/07/11/sigwh.npy | 2024/07/11/wsh.npy | 2024/07/11/wsp.npy | 2024/07/11/wsd.npy | 2024/07/11/was.npy | 2024/07/11/wad.npy | 2024/07/11/pwd.npy | 2024/07/11/swell_residual.npy |
| 2024-07-12 | 2024/07/12/sigwh.npy | 2024/07/12/wsh.npy | 2024/07/12/wsp.npy | 2024/07/12/wsd.npy | 2024/07/12/was.npy | 2024/07/12/wad.npy | 2024/07/12/pwd.npy | 2024/07/12/swell_residual.npy |
| 2024-07-13 | 2024/07/13/sigwh.npy | 2024/07/13/wsh.npy | 2024/07/13/wsp.npy | 2024/07/13/wsd.npy | 2024/07/13/was.npy | 2024/07/13/wad.npy | 2024/07/13/pwd.npy | 2024/07/13/swell_residual.npy |
| 2024-07-14 | 2024/07/14/sigwh.npy | 2024/07/14/wsh.npy | 2024/07/14/wsp.npy | 2024/07/14/wsd.npy | 2024/07/14/was.npy | 2024/07/14/wad.npy | 2024/07/14/pwd.npy | 2024/07/14/swell_residual.npy |
| 2024-07-15 | 2024/07/15/sigwh.npy | 2024/07/15/wsh.npy | 2024/07/15/wsp.npy | 2024/07/15/wsd.npy | 2024/07/15/was.npy | 2024/07/15/wad.npy | 2024/07/15/pwd.npy | 2024/07/15/swell_residual.npy |
| 2024-07-16 | 2024/07/16/sigwh.npy | 2024/07/16/wsh.npy | 2024/07/16/wsp.npy | 2024/07/16/wsd.npy | 2024/07/16/was.npy | 2024/07/16/wad.npy | 2024/07/16/pwd.npy | 2024/07/16/swell_residual.npy |
| 2024-07-17 | 2024/07/17/sigwh.npy | 2024/07/17/wsh.npy | 2024/07/17/wsp.npy | 2024/07/17/wsd.npy | 2024/07/17/was.npy | 2024/07/17/wad.npy | 2024/07/17/pwd.npy | 2024/07/17/swell_residual.npy |
| 2024-07-18 | 2024/07/18/sigwh.npy | 2024/07/18/wsh.npy | 2024/07/18/wsp.npy | 2024/07/18/wsd.npy | 2024/07/18/was.npy | 2024/07/18/wad.npy | 2024/07/18/pwd.npy | 2024/07/18/swell_residual.npy |
| 2024-07-19 | 2024/07/19/sigwh.npy | 2024/07/19/wsh.npy | 2024/07/19/wsp.npy | 2024/07/19/wsd.npy | 2024/07/19/was.npy | 2024/07/19/wad.npy | 2024/07/19/pwd.npy | 2024/07/19/swell_residual.npy |
| 2024-07-20 | 2024/07/20/sigwh.npy | 2024/07/20/wsh.npy | 2024/07/20/wsp.npy | 2024/07/20/wsd.npy | 2024/07/20/was.npy | 2024/07/20/wad.npy | 2024/07/20/pwd.npy | 2024/07/20/swell_residual.npy |
| 2024-07-21 | 2024/07/21/sigwh.npy | 2024/07/21/wsh.npy | 2024/07/21/wsp.npy | 2024/07/21/wsd.npy | 2024/07/21/was.npy | 2024/07/21/wad.npy | 2024/07/21/pwd.npy | 2024/07/21/swell_residual.npy |
| 2024-07-22 | 2024/07/22/sigwh.npy | 2024/07/22/wsh.npy | 2024/07/22/wsp.npy | 2024/07/22/wsd.npy | 2024/07/22/was.npy | 2024/07/22/wad.npy | 2024/07/22/pwd.npy | 2024/07/22/swell_residual.npy |
| 2024-07-23 | 2024/07/23/sigwh.npy | 2024/07/23/wsh.npy | 2024/07/23/wsp.npy | 2024/07/23/wsd.npy | 2024/07/23/was.npy | 2024/07/23/wad.npy | 2024/07/23/pwd.npy | 2024/07/23/swell_residual.npy |
| 2024-07-24 | 2024/07/24/sigwh.npy | 2024/07/24/wsh.npy | 2024/07/24/wsp.npy | 2024/07/24/wsd.npy | 2024/07/24/was.npy | 2024/07/24/wad.npy | 2024/07/24/pwd.npy | 2024/07/24/swell_residual.npy |
| 2024-07-25 | 2024/07/25/sigwh.npy | 2024/07/25/wsh.npy | 2024/07/25/wsp.npy | 2024/07/25/wsd.npy | 2024/07/25/was.npy | 2024/07/25/wad.npy | 2024/07/25/pwd.npy | 2024/07/25/swell_residual.npy |
| 2024-07-26 | 2024/07/26/sigwh.npy | 2024/07/26/wsh.npy | 2024/07/26/wsp.npy | 2024/07/26/wsd.npy | 2024/07/26/was.npy | 2024/07/26/wad.npy | 2024/07/26/pwd.npy | 2024/07/26/swell_residual.npy |
| 2024-07-27 | 2024/07/27/sigwh.npy | 2024/07/27/wsh.npy | 2024/07/27/wsp.npy | 2024/07/27/wsd.npy | 2024/07/27/was.npy | 2024/07/27/wad.npy | 2024/07/27/pwd.npy | 2024/07/27/swell_residual.npy |
| 2024-07-28 | 2024/07/28/sigwh.npy | 2024/07/28/wsh.npy | 2024/07/28/wsp.npy | 2024/07/28/wsd.npy | 2024/07/28/was.npy | 2024/07/28/wad.npy | 2024/07/28/pwd.npy | 2024/07/28/swell_residual.npy |
| 2024-07-29 | 2024/07/29/sigwh.npy | 2024/07/29/wsh.npy | 2024/07/29/wsp.npy | 2024/07/29/wsd.npy | 2024/07/29/was.npy | 2024/07/29/wad.npy | 2024/07/29/pwd.npy | 2024/07/29/swell_residual.npy |
| 2024-07-30 | 2024/07/30/sigwh.npy | 2024/07/30/wsh.npy | 2024/07/30/wsp.npy | 2024/07/30/wsd.npy | 2024/07/30/was.npy | 2024/07/30/wad.npy | 2024/07/30/pwd.npy | 2024/07/30/swell_residual.npy |
| 2024-07-31 | 2024/07/31/sigwh.npy | 2024/07/31/wsh.npy | 2024/07/31/wsp.npy | 2024/07/31/wsd.npy | 2024/07/31/was.npy | 2024/07/31/wad.npy | 2024/07/31/pwd.npy | 2024/07/31/swell_residual.npy |
| 2024-08-01 | 2024/08/01/sigwh.npy | 2024/08/01/wsh.npy | 2024/08/01/wsp.npy | 2024/08/01/wsd.npy | 2024/08/01/was.npy | 2024/08/01/wad.npy | 2024/08/01/pwd.npy | 2024/08/01/swell_residual.npy |
| 2024-08-02 | 2024/08/02/sigwh.npy | 2024/08/02/wsh.npy | 2024/08/02/wsp.npy | 2024/08/02/wsd.npy | 2024/08/02/was.npy | 2024/08/02/wad.npy | 2024/08/02/pwd.npy | 2024/08/02/swell_residual.npy |
| 2024-08-03 | 2024/08/03/sigwh.npy | 2024/08/03/wsh.npy | 2024/08/03/wsp.npy | 2024/08/03/wsd.npy | 2024/08/03/was.npy | 2024/08/03/wad.npy | 2024/08/03/pwd.npy | 2024/08/03/swell_residual.npy |
| 2024-08-04 | 2024/08/04/sigwh.npy | 2024/08/04/wsh.npy | 2024/08/04/wsp.npy | 2024/08/04/wsd.npy | 2024/08/04/was.npy | 2024/08/04/wad.npy | 2024/08/04/pwd.npy | 2024/08/04/swell_residual.npy |
| 2024-08-05 | 2024/08/05/sigwh.npy | 2024/08/05/wsh.npy | 2024/08/05/wsp.npy | 2024/08/05/wsd.npy | 2024/08/05/was.npy | 2024/08/05/wad.npy | 2024/08/05/pwd.npy | 2024/08/05/swell_residual.npy |
| 2024-08-06 | 2024/08/06/sigwh.npy | 2024/08/06/wsh.npy | 2024/08/06/wsp.npy | 2024/08/06/wsd.npy | 2024/08/06/was.npy | 2024/08/06/wad.npy | 2024/08/06/pwd.npy | 2024/08/06/swell_residual.npy |
| 2024-08-07 | 2024/08/07/sigwh.npy | 2024/08/07/wsh.npy | 2024/08/07/wsp.npy | 2024/08/07/wsd.npy | 2024/08/07/was.npy | 2024/08/07/wad.npy | 2024/08/07/pwd.npy | 2024/08/07/swell_residual.npy |
| 2024-08-08 | 2024/08/08/sigwh.npy | 2024/08/08/wsh.npy | 2024/08/08/wsp.npy | 2024/08/08/wsd.npy | 2024/08/08/was.npy | 2024/08/08/wad.npy | 2024/08/08/pwd.npy | 2024/08/08/swell_residual.npy |
| 2024-08-09 | 2024/08/09/sigwh.npy | 2024/08/09/wsh.npy | 2024/08/09/wsp.npy | 2024/08/09/wsd.npy | 2024/08/09/was.npy | 2024/08/09/wad.npy | 2024/08/09/pwd.npy | 2024/08/09/swell_residual.npy |
| 2024-08-10 | 2024/08/10/sigwh.npy | 2024/08/10/wsh.npy | 2024/08/10/wsp.npy | 2024/08/10/wsd.npy | 2024/08/10/was.npy | 2024/08/10/wad.npy | 2024/08/10/pwd.npy | 2024/08/10/swell_residual.npy |
| 2024-08-11 | 2024/08/11/sigwh.npy | 2024/08/11/wsh.npy | 2024/08/11/wsp.npy | 2024/08/11/wsd.npy | 2024/08/11/was.npy | 2024/08/11/wad.npy | 2024/08/11/pwd.npy | 2024/08/11/swell_residual.npy |
| 2024-08-12 | 2024/08/12/sigwh.npy | 2024/08/12/wsh.npy | 2024/08/12/wsp.npy | 2024/08/12/wsd.npy | 2024/08/12/was.npy | 2024/08/12/wad.npy | 2024/08/12/pwd.npy | 2024/08/12/swell_residual.npy |
| 2024-08-13 | 2024/08/13/sigwh.npy | 2024/08/13/wsh.npy | 2024/08/13/wsp.npy | 2024/08/13/wsd.npy | 2024/08/13/was.npy | 2024/08/13/wad.npy | 2024/08/13/pwd.npy | 2024/08/13/swell_residual.npy |
| 2024-08-14 | 2024/08/14/sigwh.npy | 2024/08/14/wsh.npy | 2024/08/14/wsp.npy | 2024/08/14/wsd.npy | 2024/08/14/was.npy | 2024/08/14/wad.npy | 2024/08/14/pwd.npy | 2024/08/14/swell_residual.npy |
| 2024-08-15 | 2024/08/15/sigwh.npy | 2024/08/15/wsh.npy | 2024/08/15/wsp.npy | 2024/08/15/wsd.npy | 2024/08/15/was.npy | 2024/08/15/wad.npy | 2024/08/15/pwd.npy | 2024/08/15/swell_residual.npy |
| 2024-08-16 | 2024/08/16/sigwh.npy | 2024/08/16/wsh.npy | 2024/08/16/wsp.npy | 2024/08/16/wsd.npy | 2024/08/16/was.npy | 2024/08/16/wad.npy | 2024/08/16/pwd.npy | 2024/08/16/swell_residual.npy |
| 2024-08-17 | 2024/08/17/sigwh.npy | 2024/08/17/wsh.npy | 2024/08/17/wsp.npy | 2024/08/17/wsd.npy | 2024/08/17/was.npy | 2024/08/17/wad.npy | 2024/08/17/pwd.npy | 2024/08/17/swell_residual.npy |
| 2024-08-18 | 2024/08/18/sigwh.npy | 2024/08/18/wsh.npy | 2024/08/18/wsp.npy | 2024/08/18/wsd.npy | 2024/08/18/was.npy | 2024/08/18/wad.npy | 2024/08/18/pwd.npy | 2024/08/18/swell_residual.npy |
| 2024-08-19 | 2024/08/19/sigwh.npy | 2024/08/19/wsh.npy | 2024/08/19/wsp.npy | 2024/08/19/wsd.npy | 2024/08/19/was.npy | 2024/08/19/wad.npy | 2024/08/19/pwd.npy | 2024/08/19/swell_residual.npy |
| 2024-08-20 | 2024/08/20/sigwh.npy | 2024/08/20/wsh.npy | 2024/08/20/wsp.npy | 2024/08/20/wsd.npy | 2024/08/20/was.npy | 2024/08/20/wad.npy | 2024/08/20/pwd.npy | 2024/08/20/swell_residual.npy |
| 2024-08-21 | 2024/08/21/sigwh.npy | 2024/08/21/wsh.npy | 2024/08/21/wsp.npy | 2024/08/21/wsd.npy | 2024/08/21/was.npy | 2024/08/21/wad.npy | 2024/08/21/pwd.npy | 2024/08/21/swell_residual.npy |
| 2024-08-22 | 2024/08/22/sigwh.npy | 2024/08/22/wsh.npy | 2024/08/22/wsp.npy | 2024/08/22/wsd.npy | 2024/08/22/was.npy | 2024/08/22/wad.npy | 2024/08/22/pwd.npy | 2024/08/22/swell_residual.npy |
| 2024-08-23 | 2024/08/23/sigwh.npy | 2024/08/23/wsh.npy | 2024/08/23/wsp.npy | 2024/08/23/wsd.npy | 2024/08/23/was.npy | 2024/08/23/wad.npy | 2024/08/23/pwd.npy | 2024/08/23/swell_residual.npy |
| 2024-08-24 | 2024/08/24/sigwh.npy | 2024/08/24/wsh.npy | 2024/08/24/wsp.npy | 2024/08/24/wsd.npy | 2024/08/24/was.npy | 2024/08/24/wad.npy | 2024/08/24/pwd.npy | 2024/08/24/swell_residual.npy |
| 2024-08-25 | 2024/08/25/sigwh.npy | 2024/08/25/wsh.npy | 2024/08/25/wsp.npy | 2024/08/25/wsd.npy | 2024/08/25/was.npy | 2024/08/25/wad.npy | 2024/08/25/pwd.npy | 2024/08/25/swell_residual.npy |
| 2024-08-26 | 2024/08/26/sigwh.npy | 2024/08/26/wsh.npy | 2024/08/26/wsp.npy | 2024/08/26/wsd.npy | 2024/08/26/was.npy | 2024/08/26/wad.npy | 2024/08/26/pwd.npy | 2024/08/26/swell_residual.npy |
| 2024-08-27 | 2024/08/27/sigwh.npy | 2024/08/27/wsh.npy | 2024/08/27/wsp.npy | 2024/08/27/wsd.npy | 2024/08/27/was.npy | 2024/08/27/wad.npy | 2024/08/27/pwd.npy | 2024/08/27/swell_residual.npy |
| 2024-08-28 | 2024/08/28/sigwh.npy | 2024/08/28/wsh.npy | 2024/08/28/wsp.npy | 2024/08/28/wsd.npy | 2024/08/28/was.npy | 2024/08/28/wad.npy | 2024/08/28/pwd.npy | 2024/08/28/swell_residual.npy |
| 2024-08-29 | 2024/08/29/sigwh.npy | 2024/08/29/wsh.npy | 2024/08/29/wsp.npy | 2024/08/29/wsd.npy | 2024/08/29/was.npy | 2024/08/29/wad.npy | 2024/08/29/pwd.npy | 2024/08/29/swell_residual.npy |
| 2024-08-30 | 2024/08/30/sigwh.npy | 2024/08/30/wsh.npy | 2024/08/30/wsp.npy | 2024/08/30/wsd.npy | 2024/08/30/was.npy | 2024/08/30/wad.npy | 2024/08/30/pwd.npy | 2024/08/30/swell_residual.npy |
| 2024-08-31 | 2024/08/31/sigwh.npy | 2024/08/31/wsh.npy | 2024/08/31/wsp.npy | 2024/08/31/wsd.npy | 2024/08/31/was.npy | 2024/08/31/wad.npy | 2024/08/31/pwd.npy | 2024/08/31/swell_residual.npy |
| 2024-09-01 | 2024/09/01/sigwh.npy | 2024/09/01/wsh.npy | 2024/09/01/wsp.npy | 2024/09/01/wsd.npy | 2024/09/01/was.npy | 2024/09/01/wad.npy | 2024/09/01/pwd.npy | 2024/09/01/swell_residual.npy |
| 2024-09-02 | 2024/09/02/sigwh.npy | 2024/09/02/wsh.npy | 2024/09/02/wsp.npy | 2024/09/02/wsd.npy | 2024/09/02/was.npy | 2024/09/02/wad.npy | 2024/09/02/pwd.npy | 2024/09/02/swell_residual.npy |
| 2024-09-03 | 2024/09/03/sigwh.npy | 2024/09/03/wsh.npy | 2024/09/03/wsp.npy | 2024/09/03/wsd.npy | 2024/09/03/was.npy | 2024/09/03/wad.npy | 2024/09/03/pwd.npy | 2024/09/03/swell_residual.npy |
| 2024-09-04 | 2024/09/04/sigwh.npy | 2024/09/04/wsh.npy | 2024/09/04/wsp.npy | 2024/09/04/wsd.npy | 2024/09/04/was.npy | 2024/09/04/wad.npy | 2024/09/04/pwd.npy | 2024/09/04/swell_residual.npy |
| 2024-09-05 | 2024/09/05/sigwh.npy | 2024/09/05/wsh.npy | 2024/09/05/wsp.npy | 2024/09/05/wsd.npy | 2024/09/05/was.npy | 2024/09/05/wad.npy | 2024/09/05/pwd.npy | 2024/09/05/swell_residual.npy |
| 2024-09-06 | 2024/09/06/sigwh.npy | 2024/09/06/wsh.npy | 2024/09/06/wsp.npy | 2024/09/06/wsd.npy | 2024/09/06/was.npy | 2024/09/06/wad.npy | 2024/09/06/pwd.npy | 2024/09/06/swell_residual.npy |
| 2024-09-07 | 2024/09/07/sigwh.npy | 2024/09/07/wsh.npy | 2024/09/07/wsp.npy | 2024/09/07/wsd.npy | 2024/09/07/was.npy | 2024/09/07/wad.npy | 2024/09/07/pwd.npy | 2024/09/07/swell_residual.npy |
| 2024-09-08 | 2024/09/08/sigwh.npy | 2024/09/08/wsh.npy | 2024/09/08/wsp.npy | 2024/09/08/wsd.npy | 2024/09/08/was.npy | 2024/09/08/wad.npy | 2024/09/08/pwd.npy | 2024/09/08/swell_residual.npy |
| 2024-09-09 | 2024/09/09/sigwh.npy | 2024/09/09/wsh.npy | 2024/09/09/wsp.npy | 2024/09/09/wsd.npy | 2024/09/09/was.npy | 2024/09/09/wad.npy | 2024/09/09/pwd.npy | 2024/09/09/swell_residual.npy |
| 2024-09-10 | 2024/09/10/sigwh.npy | 2024/09/10/wsh.npy | 2024/09/10/wsp.npy | 2024/09/10/wsd.npy | 2024/09/10/was.npy | 2024/09/10/wad.npy | 2024/09/10/pwd.npy | 2024/09/10/swell_residual.npy |
| 2024-09-11 | 2024/09/11/sigwh.npy | 2024/09/11/wsh.npy | 2024/09/11/wsp.npy | 2024/09/11/wsd.npy | 2024/09/11/was.npy | 2024/09/11/wad.npy | 2024/09/11/pwd.npy | 2024/09/11/swell_residual.npy |
| 2024-09-12 | 2024/09/12/sigwh.npy | 2024/09/12/wsh.npy | 2024/09/12/wsp.npy | 2024/09/12/wsd.npy | 2024/09/12/was.npy | 2024/09/12/wad.npy | 2024/09/12/pwd.npy | 2024/09/12/swell_residual.npy |
| 2024-09-13 | 2024/09/13/sigwh.npy | 2024/09/13/wsh.npy | 2024/09/13/wsp.npy | 2024/09/13/wsd.npy | 2024/09/13/was.npy | 2024/09/13/wad.npy | 2024/09/13/pwd.npy | 2024/09/13/swell_residual.npy |
| 2024-09-14 | 2024/09/14/sigwh.npy | 2024/09/14/wsh.npy | 2024/09/14/wsp.npy | 2024/09/14/wsd.npy | 2024/09/14/was.npy | 2024/09/14/wad.npy | 2024/09/14/pwd.npy | 2024/09/14/swell_residual.npy |
| 2024-09-15 | 2024/09/15/sigwh.npy | 2024/09/15/wsh.npy | 2024/09/15/wsp.npy | 2024/09/15/wsd.npy | 2024/09/15/was.npy | 2024/09/15/wad.npy | 2024/09/15/pwd.npy | 2024/09/15/swell_residual.npy |
| 2024-09-16 | 2024/09/16/sigwh.npy | 2024/09/16/wsh.npy | 2024/09/16/wsp.npy | 2024/09/16/wsd.npy | 2024/09/16/was.npy | 2024/09/16/wad.npy | 2024/09/16/pwd.npy | 2024/09/16/swell_residual.npy |
| 2024-09-17 | 2024/09/17/sigwh.npy | 2024/09/17/wsh.npy | 2024/09/17/wsp.npy | 2024/09/17/wsd.npy | 2024/09/17/was.npy | 2024/09/17/wad.npy | 2024/09/17/pwd.npy | 2024/09/17/swell_residual.npy |
| 2024-09-18 | 2024/09/18/sigwh.npy | 2024/09/18/wsh.npy | 2024/09/18/wsp.npy | 2024/09/18/wsd.npy | 2024/09/18/was.npy | 2024/09/18/wad.npy | 2024/09/18/pwd.npy | 2024/09/18/swell_residual.npy |
| 2024-09-19 | 2024/09/19/sigwh.npy | 2024/09/19/wsh.npy | 2024/09/19/wsp.npy | 2024/09/19/wsd.npy | 2024/09/19/was.npy | 2024/09/19/wad.npy | 2024/09/19/pwd.npy | 2024/09/19/swell_residual.npy |
| 2024-09-20 | 2024/09/20/sigwh.npy | 2024/09/20/wsh.npy | 2024/09/20/wsp.npy | 2024/09/20/wsd.npy | 2024/09/20/was.npy | 2024/09/20/wad.npy | 2024/09/20/pwd.npy | 2024/09/20/swell_residual.npy |
| 2024-09-21 | 2024/09/21/sigwh.npy | 2024/09/21/wsh.npy | 2024/09/21/wsp.npy | 2024/09/21/wsd.npy | 2024/09/21/was.npy | 2024/09/21/wad.npy | 2024/09/21/pwd.npy | 2024/09/21/swell_residual.npy |
| 2024-09-22 | 2024/09/22/sigwh.npy | 2024/09/22/wsh.npy | 2024/09/22/wsp.npy | 2024/09/22/wsd.npy | 2024/09/22/was.npy | 2024/09/22/wad.npy | 2024/09/22/pwd.npy | 2024/09/22/swell_residual.npy |
| 2024-09-23 | 2024/09/23/sigwh.npy | 2024/09/23/wsh.npy | 2024/09/23/wsp.npy | 2024/09/23/wsd.npy | 2024/09/23/was.npy | 2024/09/23/wad.npy | 2024/09/23/pwd.npy | 2024/09/23/swell_residual.npy |
| 2024-09-24 | 2024/09/24/sigwh.npy | 2024/09/24/wsh.npy | 2024/09/24/wsp.npy | 2024/09/24/wsd.npy | 2024/09/24/was.npy | 2024/09/24/wad.npy | 2024/09/24/pwd.npy | 2024/09/24/swell_residual.npy |
| 2024-09-25 | 2024/09/25/sigwh.npy | 2024/09/25/wsh.npy | 2024/09/25/wsp.npy | 2024/09/25/wsd.npy | 2024/09/25/was.npy | 2024/09/25/wad.npy | 2024/09/25/pwd.npy | 2024/09/25/swell_residual.npy |
| 2024-09-26 | 2024/09/26/sigwh.npy | 2024/09/26/wsh.npy | 2024/09/26/wsp.npy | 2024/09/26/wsd.npy | 2024/09/26/was.npy | 2024/09/26/wad.npy | 2024/09/26/pwd.npy | 2024/09/26/swell_residual.npy |
| 2024-09-27 | 2024/09/27/sigwh.npy | 2024/09/27/wsh.npy | 2024/09/27/wsp.npy | 2024/09/27/wsd.npy | 2024/09/27/was.npy | 2024/09/27/wad.npy | 2024/09/27/pwd.npy | 2024/09/27/swell_residual.npy |
| 2024-09-28 | 2024/09/28/sigwh.npy | 2024/09/28/wsh.npy | 2024/09/28/wsp.npy | 2024/09/28/wsd.npy | 2024/09/28/was.npy | 2024/09/28/wad.npy | 2024/09/28/pwd.npy | 2024/09/28/swell_residual.npy |
| 2024-09-29 | 2024/09/29/sigwh.npy | 2024/09/29/wsh.npy | 2024/09/29/wsp.npy | 2024/09/29/wsd.npy | 2024/09/29/was.npy | 2024/09/29/wad.npy | 2024/09/29/pwd.npy | 2024/09/29/swell_residual.npy |
| 2024-09-30 | 2024/09/30/sigwh.npy | 2024/09/30/wsh.npy | 2024/09/30/wsp.npy | 2024/09/30/wsd.npy | 2024/09/30/was.npy | 2024/09/30/wad.npy | 2024/09/30/pwd.npy | 2024/09/30/swell_residual.npy |
| 2024-10-01 | 2024/10/01/sigwh.npy | 2024/10/01/wsh.npy | 2024/10/01/wsp.npy | 2024/10/01/wsd.npy | 2024/10/01/was.npy | 2024/10/01/wad.npy | 2024/10/01/pwd.npy | 2024/10/01/swell_residual.npy |
| 2024-10-02 | 2024/10/02/sigwh.npy | 2024/10/02/wsh.npy | 2024/10/02/wsp.npy | 2024/10/02/wsd.npy | 2024/10/02/was.npy | 2024/10/02/wad.npy | 2024/10/02/pwd.npy | 2024/10/02/swell_residual.npy |
| 2024-10-03 | 2024/10/03/sigwh.npy | 2024/10/03/wsh.npy | 2024/10/03/wsp.npy | 2024/10/03/wsd.npy | 2024/10/03/was.npy | 2024/10/03/wad.npy | 2024/10/03/pwd.npy | 2024/10/03/swell_residual.npy |
| 2024-10-04 | 2024/10/04/sigwh.npy | 2024/10/04/wsh.npy | 2024/10/04/wsp.npy | 2024/10/04/wsd.npy | 2024/10/04/was.npy | 2024/10/04/wad.npy | 2024/10/04/pwd.npy | 2024/10/04/swell_residual.npy |
| 2024-10-05 | 2024/10/05/sigwh.npy | 2024/10/05/wsh.npy | 2024/10/05/wsp.npy | 2024/10/05/wsd.npy | 2024/10/05/was.npy | 2024/10/05/wad.npy | 2024/10/05/pwd.npy | 2024/10/05/swell_residual.npy |
| 2024-10-06 | 2024/10/06/sigwh.npy | 2024/10/06/wsh.npy | 2024/10/06/wsp.npy | 2024/10/06/wsd.npy | 2024/10/06/was.npy | 2024/10/06/wad.npy | 2024/10/06/pwd.npy | 2024/10/06/swell_residual.npy |
| 2024-10-07 | 2024/10/07/sigwh.npy | 2024/10/07/wsh.npy | 2024/10/07/wsp.npy | 2024/10/07/wsd.npy | 2024/10/07/was.npy | 2024/10/07/wad.npy | 2024/10/07/pwd.npy | 2024/10/07/swell_residual.npy |
| 2024-10-08 | 2024/10/08/sigwh.npy | 2024/10/08/wsh.npy | 2024/10/08/wsp.npy | 2024/10/08/wsd.npy | 2024/10/08/was.npy | 2024/10/08/wad.npy | 2024/10/08/pwd.npy | 2024/10/08/swell_residual.npy |
| 2024-10-09 | 2024/10/09/sigwh.npy | 2024/10/09/wsh.npy | 2024/10/09/wsp.npy | 2024/10/09/wsd.npy | 2024/10/09/was.npy | 2024/10/09/wad.npy | 2024/10/09/pwd.npy | 2024/10/09/swell_residual.npy |
| 2024-10-10 | 2024/10/10/sigwh.npy | 2024/10/10/wsh.npy | 2024/10/10/wsp.npy | 2024/10/10/wsd.npy | 2024/10/10/was.npy | 2024/10/10/wad.npy | 2024/10/10/pwd.npy | 2024/10/10/swell_residual.npy |
| 2024-10-11 | 2024/10/11/sigwh.npy | 2024/10/11/wsh.npy | 2024/10/11/wsp.npy | 2024/10/11/wsd.npy | 2024/10/11/was.npy | 2024/10/11/wad.npy | 2024/10/11/pwd.npy | 2024/10/11/swell_residual.npy |
| 2024-10-12 | 2024/10/12/sigwh.npy | 2024/10/12/wsh.npy | 2024/10/12/wsp.npy | 2024/10/12/wsd.npy | 2024/10/12/was.npy | 2024/10/12/wad.npy | 2024/10/12/pwd.npy | 2024/10/12/swell_residual.npy |
| 2024-10-13 | 2024/10/13/sigwh.npy | 2024/10/13/wsh.npy | 2024/10/13/wsp.npy | 2024/10/13/wsd.npy | 2024/10/13/was.npy | 2024/10/13/wad.npy | 2024/10/13/pwd.npy | 2024/10/13/swell_residual.npy |
| 2024-10-14 | 2024/10/14/sigwh.npy | 2024/10/14/wsh.npy | 2024/10/14/wsp.npy | 2024/10/14/wsd.npy | 2024/10/14/was.npy | 2024/10/14/wad.npy | 2024/10/14/pwd.npy | 2024/10/14/swell_residual.npy |
| 2024-10-15 | 2024/10/15/sigwh.npy | 2024/10/15/wsh.npy | 2024/10/15/wsp.npy | 2024/10/15/wsd.npy | 2024/10/15/was.npy | 2024/10/15/wad.npy | 2024/10/15/pwd.npy | 2024/10/15/swell_residual.npy |
| 2024-10-16 | 2024/10/16/sigwh.npy | 2024/10/16/wsh.npy | 2024/10/16/wsp.npy | 2024/10/16/wsd.npy | 2024/10/16/was.npy | 2024/10/16/wad.npy | 2024/10/16/pwd.npy | 2024/10/16/swell_residual.npy |
| 2024-10-17 | 2024/10/17/sigwh.npy | 2024/10/17/wsh.npy | 2024/10/17/wsp.npy | 2024/10/17/wsd.npy | 2024/10/17/was.npy | 2024/10/17/wad.npy | 2024/10/17/pwd.npy | 2024/10/17/swell_residual.npy |
| 2024-10-18 | 2024/10/18/sigwh.npy | 2024/10/18/wsh.npy | 2024/10/18/wsp.npy | 2024/10/18/wsd.npy | 2024/10/18/was.npy | 2024/10/18/wad.npy | 2024/10/18/pwd.npy | 2024/10/18/swell_residual.npy |
| 2024-10-19 | 2024/10/19/sigwh.npy | 2024/10/19/wsh.npy | 2024/10/19/wsp.npy | 2024/10/19/wsd.npy | 2024/10/19/was.npy | 2024/10/19/wad.npy | 2024/10/19/pwd.npy | 2024/10/19/swell_residual.npy |
| 2024-10-20 | 2024/10/20/sigwh.npy | 2024/10/20/wsh.npy | 2024/10/20/wsp.npy | 2024/10/20/wsd.npy | 2024/10/20/was.npy | 2024/10/20/wad.npy | 2024/10/20/pwd.npy | 2024/10/20/swell_residual.npy |
| 2024-10-21 | 2024/10/21/sigwh.npy | 2024/10/21/wsh.npy | 2024/10/21/wsp.npy | 2024/10/21/wsd.npy | 2024/10/21/was.npy | 2024/10/21/wad.npy | 2024/10/21/pwd.npy | 2024/10/21/swell_residual.npy |
| 2024-10-22 | 2024/10/22/sigwh.npy | 2024/10/22/wsh.npy | 2024/10/22/wsp.npy | 2024/10/22/wsd.npy | 2024/10/22/was.npy | 2024/10/22/wad.npy | 2024/10/22/pwd.npy | 2024/10/22/swell_residual.npy |
| 2024-10-23 | 2024/10/23/sigwh.npy | 2024/10/23/wsh.npy | 2024/10/23/wsp.npy | 2024/10/23/wsd.npy | 2024/10/23/was.npy | 2024/10/23/wad.npy | 2024/10/23/pwd.npy | 2024/10/23/swell_residual.npy |
| 2024-10-24 | 2024/10/24/sigwh.npy | 2024/10/24/wsh.npy | 2024/10/24/wsp.npy | 2024/10/24/wsd.npy | 2024/10/24/was.npy | 2024/10/24/wad.npy | 2024/10/24/pwd.npy | 2024/10/24/swell_residual.npy |
| 2024-10-25 | 2024/10/25/sigwh.npy | 2024/10/25/wsh.npy | 2024/10/25/wsp.npy | 2024/10/25/wsd.npy | 2024/10/25/was.npy | 2024/10/25/wad.npy | 2024/10/25/pwd.npy | 2024/10/25/swell_residual.npy |
| 2024-10-26 | 2024/10/26/sigwh.npy | 2024/10/26/wsh.npy | 2024/10/26/wsp.npy | 2024/10/26/wsd.npy | 2024/10/26/was.npy | 2024/10/26/wad.npy | 2024/10/26/pwd.npy | 2024/10/26/swell_residual.npy |
| 2024-10-27 | 2024/10/27/sigwh.npy | 2024/10/27/wsh.npy | 2024/10/27/wsp.npy | 2024/10/27/wsd.npy | 2024/10/27/was.npy | 2024/10/27/wad.npy | 2024/10/27/pwd.npy | 2024/10/27/swell_residual.npy |
| 2024-10-28 | 2024/10/28/sigwh.npy | 2024/10/28/wsh.npy | 2024/10/28/wsp.npy | 2024/10/28/wsd.npy | 2024/10/28/was.npy | 2024/10/28/wad.npy | 2024/10/28/pwd.npy | 2024/10/28/swell_residual.npy |
| 2024-10-29 | 2024/10/29/sigwh.npy | 2024/10/29/wsh.npy | 2024/10/29/wsp.npy | 2024/10/29/wsd.npy | 2024/10/29/was.npy | 2024/10/29/wad.npy | 2024/10/29/pwd.npy | 2024/10/29/swell_residual.npy |
| 2024-10-30 | 2024/10/30/sigwh.npy | 2024/10/30/wsh.npy | 2024/10/30/wsp.npy | 2024/10/30/wsd.npy | 2024/10/30/was.npy | 2024/10/30/wad.npy | 2024/10/30/pwd.npy | 2024/10/30/swell_residual.npy |
| 2024-10-31 | 2024/10/31/sigwh.npy | 2024/10/31/wsh.npy | 2024/10/31/wsp.npy | 2024/10/31/wsd.npy | 2024/10/31/was.npy | 2024/10/31/wad.npy | 2024/10/31/pwd.npy | 2024/10/31/swell_residual.npy |
| 2024-11-01 | 2024/11/01/sigwh.npy | 2024/11/01/wsh.npy | 2024/11/01/wsp.npy | 2024/11/01/wsd.npy | 2024/11/01/was.npy | 2024/11/01/wad.npy | 2024/11/01/pwd.npy | 2024/11/01/swell_residual.npy |
| 2024-11-02 | 2024/11/02/sigwh.npy | 2024/11/02/wsh.npy | 2024/11/02/wsp.npy | 2024/11/02/wsd.npy | 2024/11/02/was.npy | 2024/11/02/wad.npy | 2024/11/02/pwd.npy | 2024/11/02/swell_residual.npy |
| 2024-11-03 | 2024/11/03/sigwh.npy | 2024/11/03/wsh.npy | 2024/11/03/wsp.npy | 2024/11/03/wsd.npy | 2024/11/03/was.npy | 2024/11/03/wad.npy | 2024/11/03/pwd.npy | 2024/11/03/swell_residual.npy |
| 2024-11-04 | 2024/11/04/sigwh.npy | 2024/11/04/wsh.npy | 2024/11/04/wsp.npy | 2024/11/04/wsd.npy | 2024/11/04/was.npy | 2024/11/04/wad.npy | 2024/11/04/pwd.npy | 2024/11/04/swell_residual.npy |
| 2024-11-05 | 2024/11/05/sigwh.npy | 2024/11/05/wsh.npy | 2024/11/05/wsp.npy | 2024/11/05/wsd.npy | 2024/11/05/was.npy | 2024/11/05/wad.npy | 2024/11/05/pwd.npy | 2024/11/05/swell_residual.npy |
| 2024-11-06 | 2024/11/06/sigwh.npy | 2024/11/06/wsh.npy | 2024/11/06/wsp.npy | 2024/11/06/wsd.npy | 2024/11/06/was.npy | 2024/11/06/wad.npy | 2024/11/06/pwd.npy | 2024/11/06/swell_residual.npy |
| 2024-11-07 | 2024/11/07/sigwh.npy | 2024/11/07/wsh.npy | 2024/11/07/wsp.npy | 2024/11/07/wsd.npy | 2024/11/07/was.npy | 2024/11/07/wad.npy | 2024/11/07/pwd.npy | 2024/11/07/swell_residual.npy |
| 2024-11-08 | 2024/11/08/sigwh.npy | 2024/11/08/wsh.npy | 2024/11/08/wsp.npy | 2024/11/08/wsd.npy | 2024/11/08/was.npy | 2024/11/08/wad.npy | 2024/11/08/pwd.npy | 2024/11/08/swell_residual.npy |
| 2024-11-09 | 2024/11/09/sigwh.npy | 2024/11/09/wsh.npy | 2024/11/09/wsp.npy | 2024/11/09/wsd.npy | 2024/11/09/was.npy | 2024/11/09/wad.npy | 2024/11/09/pwd.npy | 2024/11/09/swell_residual.npy |
| 2024-11-10 | 2024/11/10/sigwh.npy | 2024/11/10/wsh.npy | 2024/11/10/wsp.npy | 2024/11/10/wsd.npy | 2024/11/10/was.npy | 2024/11/10/wad.npy | 2024/11/10/pwd.npy | 2024/11/10/swell_residual.npy |
| 2024-11-11 | 2024/11/11/sigwh.npy | 2024/11/11/wsh.npy | 2024/11/11/wsp.npy | 2024/11/11/wsd.npy | 2024/11/11/was.npy | 2024/11/11/wad.npy | 2024/11/11/pwd.npy | 2024/11/11/swell_residual.npy |
| 2024-11-12 | 2024/11/12/sigwh.npy | 2024/11/12/wsh.npy | 2024/11/12/wsp.npy | 2024/11/12/wsd.npy | 2024/11/12/was.npy | 2024/11/12/wad.npy | 2024/11/12/pwd.npy | 2024/11/12/swell_residual.npy |
| 2024-11-13 | 2024/11/13/sigwh.npy | 2024/11/13/wsh.npy | 2024/11/13/wsp.npy | 2024/11/13/wsd.npy | 2024/11/13/was.npy | 2024/11/13/wad.npy | 2024/11/13/pwd.npy | 2024/11/13/swell_residual.npy |
| 2024-11-14 | 2024/11/14/sigwh.npy | 2024/11/14/wsh.npy | 2024/11/14/wsp.npy | 2024/11/14/wsd.npy | 2024/11/14/was.npy | 2024/11/14/wad.npy | 2024/11/14/pwd.npy | 2024/11/14/swell_residual.npy |
| 2024-11-15 | 2024/11/15/sigwh.npy | 2024/11/15/wsh.npy | 2024/11/15/wsp.npy | 2024/11/15/wsd.npy | 2024/11/15/was.npy | 2024/11/15/wad.npy | 2024/11/15/pwd.npy | 2024/11/15/swell_residual.npy |
| 2024-11-16 | 2024/11/16/sigwh.npy | 2024/11/16/wsh.npy | 2024/11/16/wsp.npy | 2024/11/16/wsd.npy | 2024/11/16/was.npy | 2024/11/16/wad.npy | 2024/11/16/pwd.npy | 2024/11/16/swell_residual.npy |
| 2024-11-17 | 2024/11/17/sigwh.npy | 2024/11/17/wsh.npy | 2024/11/17/wsp.npy | 2024/11/17/wsd.npy | 2024/11/17/was.npy | 2024/11/17/wad.npy | 2024/11/17/pwd.npy | 2024/11/17/swell_residual.npy |
| 2024-11-18 | 2024/11/18/sigwh.npy | 2024/11/18/wsh.npy | 2024/11/18/wsp.npy | 2024/11/18/wsd.npy | 2024/11/18/was.npy | 2024/11/18/wad.npy | 2024/11/18/pwd.npy | 2024/11/18/swell_residual.npy |
| 2024-11-19 | 2024/11/19/sigwh.npy | 2024/11/19/wsh.npy | 2024/11/19/wsp.npy | 2024/11/19/wsd.npy | 2024/11/19/was.npy | 2024/11/19/wad.npy | 2024/11/19/pwd.npy | 2024/11/19/swell_residual.npy |
| 2024-11-20 | 2024/11/20/sigwh.npy | 2024/11/20/wsh.npy | 2024/11/20/wsp.npy | 2024/11/20/wsd.npy | 2024/11/20/was.npy | 2024/11/20/wad.npy | 2024/11/20/pwd.npy | 2024/11/20/swell_residual.npy |
| 2024-11-21 | 2024/11/21/sigwh.npy | 2024/11/21/wsh.npy | 2024/11/21/wsp.npy | 2024/11/21/wsd.npy | 2024/11/21/was.npy | 2024/11/21/wad.npy | 2024/11/21/pwd.npy | 2024/11/21/swell_residual.npy |
| 2024-11-22 | 2024/11/22/sigwh.npy | 2024/11/22/wsh.npy | 2024/11/22/wsp.npy | 2024/11/22/wsd.npy | 2024/11/22/was.npy | 2024/11/22/wad.npy | 2024/11/22/pwd.npy | 2024/11/22/swell_residual.npy |
| 2024-11-23 | 2024/11/23/sigwh.npy | 2024/11/23/wsh.npy | 2024/11/23/wsp.npy | 2024/11/23/wsd.npy | 2024/11/23/was.npy | 2024/11/23/wad.npy | 2024/11/23/pwd.npy | 2024/11/23/swell_residual.npy |
| 2024-11-24 | 2024/11/24/sigwh.npy | 2024/11/24/wsh.npy | 2024/11/24/wsp.npy | 2024/11/24/wsd.npy | 2024/11/24/was.npy | 2024/11/24/wad.npy | 2024/11/24/pwd.npy | 2024/11/24/swell_residual.npy |
| 2024-11-25 | 2024/11/25/sigwh.npy | 2024/11/25/wsh.npy | 2024/11/25/wsp.npy | 2024/11/25/wsd.npy | 2024/11/25/was.npy | 2024/11/25/wad.npy | 2024/11/25/pwd.npy | 2024/11/25/swell_residual.npy |
| 2024-11-26 | 2024/11/26/sigwh.npy | 2024/11/26/wsh.npy | 2024/11/26/wsp.npy | 2024/11/26/wsd.npy | 2024/11/26/was.npy | 2024/11/26/wad.npy | 2024/11/26/pwd.npy | 2024/11/26/swell_residual.npy |
| 2024-11-27 | 2024/11/27/sigwh.npy | 2024/11/27/wsh.npy | 2024/11/27/wsp.npy | 2024/11/27/wsd.npy | 2024/11/27/was.npy | 2024/11/27/wad.npy | 2024/11/27/pwd.npy | 2024/11/27/swell_residual.npy |
| 2024-11-28 | 2024/11/28/sigwh.npy | 2024/11/28/wsh.npy | 2024/11/28/wsp.npy | 2024/11/28/wsd.npy | 2024/11/28/was.npy | 2024/11/28/wad.npy | 2024/11/28/pwd.npy | 2024/11/28/swell_residual.npy |
| 2024-11-29 | 2024/11/29/sigwh.npy | 2024/11/29/wsh.npy | 2024/11/29/wsp.npy | 2024/11/29/wsd.npy | 2024/11/29/was.npy | 2024/11/29/wad.npy | 2024/11/29/pwd.npy | 2024/11/29/swell_residual.npy |
| 2024-11-30 | 2024/11/30/sigwh.npy | 2024/11/30/wsh.npy | 2024/11/30/wsp.npy | 2024/11/30/wsd.npy | 2024/11/30/was.npy | 2024/11/30/wad.npy | 2024/11/30/pwd.npy | 2024/11/30/swell_residual.npy |
| 2024-12-01 | 2024/12/01/sigwh.npy | 2024/12/01/wsh.npy | 2024/12/01/wsp.npy | 2024/12/01/wsd.npy | 2024/12/01/was.npy | 2024/12/01/wad.npy | 2024/12/01/pwd.npy | 2024/12/01/swell_residual.npy |
| 2024-12-02 | 2024/12/02/sigwh.npy | 2024/12/02/wsh.npy | 2024/12/02/wsp.npy | 2024/12/02/wsd.npy | 2024/12/02/was.npy | 2024/12/02/wad.npy | 2024/12/02/pwd.npy | 2024/12/02/swell_residual.npy |
| 2024-12-03 | 2024/12/03/sigwh.npy | 2024/12/03/wsh.npy | 2024/12/03/wsp.npy | 2024/12/03/wsd.npy | 2024/12/03/was.npy | 2024/12/03/wad.npy | 2024/12/03/pwd.npy | 2024/12/03/swell_residual.npy |
| 2024-12-04 | 2024/12/04/sigwh.npy | 2024/12/04/wsh.npy | 2024/12/04/wsp.npy | 2024/12/04/wsd.npy | 2024/12/04/was.npy | 2024/12/04/wad.npy | 2024/12/04/pwd.npy | 2024/12/04/swell_residual.npy |
| 2024-12-05 | 2024/12/05/sigwh.npy | 2024/12/05/wsh.npy | 2024/12/05/wsp.npy | 2024/12/05/wsd.npy | 2024/12/05/was.npy | 2024/12/05/wad.npy | 2024/12/05/pwd.npy | 2024/12/05/swell_residual.npy |
| 2024-12-06 | 2024/12/06/sigwh.npy | 2024/12/06/wsh.npy | 2024/12/06/wsp.npy | 2024/12/06/wsd.npy | 2024/12/06/was.npy | 2024/12/06/wad.npy | 2024/12/06/pwd.npy | 2024/12/06/swell_residual.npy |
| 2024-12-07 | 2024/12/07/sigwh.npy | 2024/12/07/wsh.npy | 2024/12/07/wsp.npy | 2024/12/07/wsd.npy | 2024/12/07/was.npy | 2024/12/07/wad.npy | 2024/12/07/pwd.npy | 2024/12/07/swell_residual.npy |
| 2024-12-08 | 2024/12/08/sigwh.npy | 2024/12/08/wsh.npy | 2024/12/08/wsp.npy | 2024/12/08/wsd.npy | 2024/12/08/was.npy | 2024/12/08/wad.npy | 2024/12/08/pwd.npy | 2024/12/08/swell_residual.npy |
| 2024-12-09 | 2024/12/09/sigwh.npy | 2024/12/09/wsh.npy | 2024/12/09/wsp.npy | 2024/12/09/wsd.npy | 2024/12/09/was.npy | 2024/12/09/wad.npy | 2024/12/09/pwd.npy | 2024/12/09/swell_residual.npy |
| 2024-12-10 | 2024/12/10/sigwh.npy | 2024/12/10/wsh.npy | 2024/12/10/wsp.npy | 2024/12/10/wsd.npy | 2024/12/10/was.npy | 2024/12/10/wad.npy | 2024/12/10/pwd.npy | 2024/12/10/swell_residual.npy |
| 2024-12-11 | 2024/12/11/sigwh.npy | 2024/12/11/wsh.npy | 2024/12/11/wsp.npy | 2024/12/11/wsd.npy | 2024/12/11/was.npy | 2024/12/11/wad.npy | 2024/12/11/pwd.npy | 2024/12/11/swell_residual.npy |
| 2024-12-12 | 2024/12/12/sigwh.npy | 2024/12/12/wsh.npy | 2024/12/12/wsp.npy | 2024/12/12/wsd.npy | 2024/12/12/was.npy | 2024/12/12/wad.npy | 2024/12/12/pwd.npy | 2024/12/12/swell_residual.npy |
| 2024-12-13 | 2024/12/13/sigwh.npy | 2024/12/13/wsh.npy | 2024/12/13/wsp.npy | 2024/12/13/wsd.npy | 2024/12/13/was.npy | 2024/12/13/wad.npy | 2024/12/13/pwd.npy | 2024/12/13/swell_residual.npy |
| 2024-12-14 | 2024/12/14/sigwh.npy | 2024/12/14/wsh.npy | 2024/12/14/wsp.npy | 2024/12/14/wsd.npy | 2024/12/14/was.npy | 2024/12/14/wad.npy | 2024/12/14/pwd.npy | 2024/12/14/swell_residual.npy |
| 2024-12-15 | 2024/12/15/sigwh.npy | 2024/12/15/wsh.npy | 2024/12/15/wsp.npy | 2024/12/15/wsd.npy | 2024/12/15/was.npy | 2024/12/15/wad.npy | 2024/12/15/pwd.npy | 2024/12/15/swell_residual.npy |
| 2024-12-16 | 2024/12/16/sigwh.npy | 2024/12/16/wsh.npy | 2024/12/16/wsp.npy | 2024/12/16/wsd.npy | 2024/12/16/was.npy | 2024/12/16/wad.npy | 2024/12/16/pwd.npy | 2024/12/16/swell_residual.npy |
| 2024-12-17 | 2024/12/17/sigwh.npy | 2024/12/17/wsh.npy | 2024/12/17/wsp.npy | 2024/12/17/wsd.npy | 2024/12/17/was.npy | 2024/12/17/wad.npy | 2024/12/17/pwd.npy | 2024/12/17/swell_residual.npy |
| 2024-12-18 | 2024/12/18/sigwh.npy | 2024/12/18/wsh.npy | 2024/12/18/wsp.npy | 2024/12/18/wsd.npy | 2024/12/18/was.npy | 2024/12/18/wad.npy | 2024/12/18/pwd.npy | 2024/12/18/swell_residual.npy |
| 2024-12-19 | 2024/12/19/sigwh.npy | 2024/12/19/wsh.npy | 2024/12/19/wsp.npy | 2024/12/19/wsd.npy | 2024/12/19/was.npy | 2024/12/19/wad.npy | 2024/12/19/pwd.npy | 2024/12/19/swell_residual.npy |
| 2024-12-20 | 2024/12/20/sigwh.npy | 2024/12/20/wsh.npy | 2024/12/20/wsp.npy | 2024/12/20/wsd.npy | 2024/12/20/was.npy | 2024/12/20/wad.npy | 2024/12/20/pwd.npy | 2024/12/20/swell_residual.npy |
| 2024-12-21 | 2024/12/21/sigwh.npy | 2024/12/21/wsh.npy | 2024/12/21/wsp.npy | 2024/12/21/wsd.npy | 2024/12/21/was.npy | 2024/12/21/wad.npy | 2024/12/21/pwd.npy | 2024/12/21/swell_residual.npy |
| 2024-12-22 | 2024/12/22/sigwh.npy | 2024/12/22/wsh.npy | 2024/12/22/wsp.npy | 2024/12/22/wsd.npy | 2024/12/22/was.npy | 2024/12/22/wad.npy | 2024/12/22/pwd.npy | 2024/12/22/swell_residual.npy |
| 2024-12-23 | 2024/12/23/sigwh.npy | 2024/12/23/wsh.npy | 2024/12/23/wsp.npy | 2024/12/23/wsd.npy | 2024/12/23/was.npy | 2024/12/23/wad.npy | 2024/12/23/pwd.npy | 2024/12/23/swell_residual.npy |
| 2024-12-24 | 2024/12/24/sigwh.npy | 2024/12/24/wsh.npy | 2024/12/24/wsp.npy | 2024/12/24/wsd.npy | 2024/12/24/was.npy | 2024/12/24/wad.npy | 2024/12/24/pwd.npy | 2024/12/24/swell_residual.npy |
| 2024-12-25 | 2024/12/25/sigwh.npy | 2024/12/25/wsh.npy | 2024/12/25/wsp.npy | 2024/12/25/wsd.npy | 2024/12/25/was.npy | 2024/12/25/wad.npy | 2024/12/25/pwd.npy | 2024/12/25/swell_residual.npy |
| 2024-12-26 | 2024/12/26/sigwh.npy | 2024/12/26/wsh.npy | 2024/12/26/wsp.npy | 2024/12/26/wsd.npy | 2024/12/26/was.npy | 2024/12/26/wad.npy | 2024/12/26/pwd.npy | 2024/12/26/swell_residual.npy |
| 2024-12-27 | 2024/12/27/sigwh.npy | 2024/12/27/wsh.npy | 2024/12/27/wsp.npy | 2024/12/27/wsd.npy | 2024/12/27/was.npy | 2024/12/27/wad.npy | 2024/12/27/pwd.npy | 2024/12/27/swell_residual.npy |
| 2024-12-28 | 2024/12/28/sigwh.npy | 2024/12/28/wsh.npy | 2024/12/28/wsp.npy | 2024/12/28/wsd.npy | 2024/12/28/was.npy | 2024/12/28/wad.npy | 2024/12/28/pwd.npy | 2024/12/28/swell_residual.npy |
| 2024-12-29 | 2024/12/29/sigwh.npy | 2024/12/29/wsh.npy | 2024/12/29/wsp.npy | 2024/12/29/wsd.npy | 2024/12/29/was.npy | 2024/12/29/wad.npy | 2024/12/29/pwd.npy | 2024/12/29/swell_residual.npy |
| 2024-12-30 | 2024/12/30/sigwh.npy | 2024/12/30/wsh.npy | 2024/12/30/wsp.npy | 2024/12/30/wsd.npy | 2024/12/30/was.npy | 2024/12/30/wad.npy | 2024/12/30/pwd.npy | 2024/12/30/swell_residual.npy |
| 2024-12-31 | 2024/12/31/sigwh.npy | 2024/12/31/wsh.npy | 2024/12/31/wsp.npy | 2024/12/31/wsd.npy | 2024/12/31/was.npy | 2024/12/31/wad.npy | 2024/12/31/pwd.npy | 2024/12/31/swell_residual.npy |
