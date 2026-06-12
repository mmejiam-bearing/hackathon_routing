#include "maritime/edge_weight.hpp"
#include "maritime/foc_model.hpp"

#include <gtest/gtest.h>
#include <cmath>

namespace {

using namespace maritime;

// ---------------------------------------------------------------------------
// Synthetic VesselParams with a deterministic FOC model for testing
// ---------------------------------------------------------------------------
VesselParams make_test_vessel(float draft_m = 8.f)
{
    VesselParams v;
    v.draft_m             = draft_m;
    v.beam_m              = 32.f;
    v.loa_m               = 200.f;
    v.service_speed_kts   = 14.f;
    v.block_coeff         = 0.80f;
    v.displacement_t      = 50000.f;
    v.transverse_area_m2  = 600.f;

    // Simple linear FOC model: speed=12kts flat, foc = 0.05 + 0.01*sig_wh
    v.foc_model = [](float sig_wh, float /*wind*/, float /*cur*/)
        -> std::pair<float, float>
    {
        return {12.f, 0.05f + 0.01f * sig_wh};
    };

    return v;
}

// Build a minimal 2-node graph-like structure in memory for testing
// compute_edge_cost without requiring binary files.
// We can't instantiate StaticGraph from memory, so we test haversine and
// bearing directly, and use a mock-based approach for edge cost logic.

// ---------------------------------------------------------------------------
// haversine
// ---------------------------------------------------------------------------
TEST(Haversine, ZeroDistance)
{
    EXPECT_NEAR(haversine_nm(0.f, 0.f, 0.f, 0.f), 0.f, 1e-4f);
}

TEST(Haversine, KnownDistanceLondonNY)
{
    // Great-circle ≈ 3,009 nm; 3,459 is statute miles, not nautical miles
    EXPECT_NEAR(haversine_nm(51.5f, -0.1f, 40.7f, -74.f), 3009.f, 15.f);
}

TEST(Haversine, IsSymmetric)
{
    const float d1 = haversine_nm(10.f, 20.f, 30.f, 40.f);
    const float d2 = haversine_nm(30.f, 40.f, 10.f, 20.f);
    EXPECT_NEAR(d1, d2, 1e-3f);
}

TEST(Haversine, AlwaysNonNegative)
{
    EXPECT_GE(haversine_nm(-90.f, -180.f, 90.f, 180.f), 0.f);
}

// ---------------------------------------------------------------------------
// bearing_rad
// ---------------------------------------------------------------------------
TEST(BearingRad, NorthIsZero)
{
    EXPECT_NEAR(bearing_rad(0.f, 0.f, 1.f, 0.f), 0.f, 1e-4f);
}

TEST(BearingRad, EastIsHalfPi)
{
    const float b = bearing_rad(0.f, 0.f, 0.f, 1.f);
    EXPECT_NEAR(b, std::numbers::pi_v<float> / 2.f, 1e-4f);
}

// ---------------------------------------------------------------------------
// VesselParams — FOC model callable
// ---------------------------------------------------------------------------
TEST(VesselParams, FocModelCalledCorrectly)
{
    auto vessel = make_test_vessel();
    const auto [spd, foc] = vessel.foc_model(2.f, 5.f, 0.f);
    EXPECT_NEAR(spd, 12.f, 1e-4f);
    EXPECT_NEAR(foc, 0.07f, 1e-4f);  // 0.05 + 0.01*2
}

TEST(VesselParams, CalmWaterFoc)
{
    auto vessel = make_test_vessel();
    const auto [spd, foc] = vessel.foc_model(0.f, 0.f, 0.f);
    EXPECT_NEAR(spd, 12.f, 1e-4f);
    EXPECT_NEAR(foc, 0.05f, 1e-4f);
}

// ---------------------------------------------------------------------------
// beaufort_from_knots / beaufort_from_ms
// ---------------------------------------------------------------------------
TEST(Beaufort, CalmIsZero)
{
    EXPECT_EQ(beaufort_from_knots(0.f), 0);
    EXPECT_EQ(beaufort_from_ms(0.f),    0);
}

TEST(Beaufort, KnownThresholdBoundaries)
{
    EXPECT_EQ(beaufort_from_knots(1.f),  1);   // exactly at BF1 threshold
    EXPECT_EQ(beaufort_from_knots(0.9f), 0);   // just below
    EXPECT_EQ(beaufort_from_knots(63.5f), 12); // BF12 threshold
    EXPECT_EQ(beaufort_from_knots(63.4f), 11); // just below BF12
}

TEST(Beaufort, Bf5FromMs)
{
    // 10.8 m/s ≈ 20.97 kts → Beaufort 5 (threshold 16.5–21.5 kts)
    EXPECT_EQ(beaufort_from_ms(10.8f), 5);
}

TEST(Beaufort, MonotonicWithWindSpeed)
{
    int prev = beaufort_from_knots(0.f);
    for (float kts = 1.f; kts <= 70.f; kts += 1.f) {
        const int bf = beaufort_from_knots(kts);
        EXPECT_GE(bf, prev) << "Beaufort decreased at " << kts << " kts";
        prev = bf;
    }
}

// ---------------------------------------------------------------------------
// universal_foc_model
// ---------------------------------------------------------------------------
TEST(UniversalFocModel, CalmWaterPositive)
{
    const auto [spd, foc] = universal_foc_model(0.f, 14.f);
    EXPECT_FLOAT_EQ(spd, 14.f);
    EXPECT_GT(foc, 0.f);
}

TEST(UniversalFocModel, FocIncreasesWithWind)
{
    // Higher wind → higher Beaufort → higher FOC per nm
    const auto [spd0, foc_calm]  = universal_foc_model(0.f,  14.f);
    const auto [spd1, foc_storm] = universal_foc_model(20.f, 14.f);  // ~39 kts, BF8
    (void)spd0; (void)spd1;
    EXPECT_GT(foc_storm, foc_calm);
}

TEST(UniversalFocModel, SpeedPassedThrough)
{
    const auto [spd12, foc12] = universal_foc_model(0.f, 12.f);
    const auto [spd14, foc14] = universal_foc_model(0.f, 14.f);
    (void)foc12; (void)foc14;
    EXPECT_FLOAT_EQ(spd12, 12.f);
    EXPECT_FLOAT_EQ(spd14, 14.f);
}

TEST(UniversalFocModel, SpotCheckCalm14Kts)
{
    // SHP = 76.1394 × 14³ + 2484.81 ≈ 211,412 kW
    // FOC = 211,412 × 180 / 1e6 ≈ 38.05 MT/h
    // foc_per_nm = 38.05 / 14 ≈ 2.718 MT/nm
    const auto [spd, foc] = universal_foc_model(0.f, 14.f);
    (void)spd;
    EXPECT_NEAR(foc, 2.718f, 0.05f);
}

// ---------------------------------------------------------------------------
// calm_water_resistance_kn
// ---------------------------------------------------------------------------
TEST(CalmWaterResistance, PositiveAtServiceSpeed)
{
    const auto v = make_test_vessel();
    EXPECT_GT(calm_water_resistance_kn(v.service_speed_kts, v), 0.f);
}

TEST(CalmWaterResistance, ZeroAtZeroSpeed)
{
    const auto v = make_test_vessel();
    EXPECT_FLOAT_EQ(calm_water_resistance_kn(0.f, v), 0.f);
}

TEST(CalmWaterResistance, IncreasesWithSpeed)
{
    const auto v = make_test_vessel();
    EXPECT_LT(calm_water_resistance_kn(10.f, v),
              calm_water_resistance_kn(14.f, v));
}

// ---------------------------------------------------------------------------
// wave_resistance_kn
// ---------------------------------------------------------------------------
TEST(WaveResistance, HeadOnWorseThanFollowing)
{
    const auto v = make_test_vessel();
    const float r_head   = wave_resistance_kn(5.f,   0.f, v);
    const float r_follow = wave_resistance_kn(5.f, 180.f, v);
    EXPECT_GT(r_head, r_follow);
}

TEST(WaveResistance, ZeroWaveHeightGivesZero)
{
    const auto v = make_test_vessel();
    EXPECT_FLOAT_EQ(wave_resistance_kn(0.f, 0.f, v), 0.f);
}

TEST(WaveResistance, PeakAt22p5Degrees)
{
    // Kreitner table peaks at 22.5° (coefficient 1.125), exceeds 0° (1.0)
    const auto v = make_test_vessel();
    EXPECT_GT(wave_resistance_kn(5.f, 22.5f, v),
              wave_resistance_kn(5.f,  0.f,  v));
}

// ---------------------------------------------------------------------------
// wind_resistance_kn
// ---------------------------------------------------------------------------
TEST(WindResistance, HeadOnPositive)
{
    const auto v = make_test_vessel();
    EXPECT_GT(wind_resistance_kn(15.f, 0.f, v.service_speed_kts, v), 0.f);
}

TEST(WindResistance, TailwindNearZero)
{
    // Following wind (180°) → C_wind = max(0, cos(π)) * 0.5 = 0 → zero drag
    const auto v = make_test_vessel();
    EXPECT_FLOAT_EQ(wind_resistance_kn(15.f, 180.f, v.service_speed_kts, v), 0.f);
}

TEST(WindResistance, ZeroWindGivesZero)
{
    const auto v = make_test_vessel();
    EXPECT_FLOAT_EQ(wind_resistance_kn(0.f, 0.f, v.service_speed_kts, v), 0.f);
}

// ---------------------------------------------------------------------------
// speed_loss_pct
// ---------------------------------------------------------------------------
TEST(SpeedLossPct, ZeroDeltaRGivesZero)
{
    EXPECT_FLOAT_EQ(speed_loss_pct(500.f, 0.f), 0.f);
}

TEST(SpeedLossPct, KwonIdentity)
{
    // speed_loss(R, R) = 100*(sqrt(2)-1) ≈ 41.42%
    EXPECT_NEAR(speed_loss_pct(500.f, 500.f), 41.42f, 0.1f);
}

TEST(SpeedLossPct, CappedAt100Pct)
{
    EXPECT_FLOAT_EQ(speed_loss_pct(10.f, 10000.f), 100.f);
}

TEST(SpeedLossPct, ZeroRcwGivesZero)
{
    EXPECT_FLOAT_EQ(speed_loss_pct(0.f, 500.f), 0.f);
}

// ---------------------------------------------------------------------------
// Speed-loss integration: calm vs storm vessel speed
// ---------------------------------------------------------------------------
TEST(SpeedLossIntegration, CalmWaterNoLoss)
{
    const auto v = make_test_vessel();
    const float R_cw  = calm_water_resistance_kn(v.service_speed_kts, v);
    const float R_wave = wave_resistance_kn(0.f, 0.f, v);
    const float R_wind = wind_resistance_kn(0.f, 0.f, v.service_speed_kts, v);
    const float sl = speed_loss_pct(R_cw, R_wave + R_wind);
    EXPECT_NEAR(sl, 0.f, 1e-3f);  // no weather → no speed loss
}

TEST(SpeedLossIntegration, StormReducesSpeed)
{
    // Peak storm: sig_wh=15m, was=45 m/s head-on → >20% speed loss
    const auto v = make_test_vessel();
    const float R_cw   = calm_water_resistance_kn(v.service_speed_kts, v);
    const float R_wave = wave_resistance_kn(15.f,  0.f, v);
    const float R_wind = wind_resistance_kn(45.f,  0.f, v.service_speed_kts, v);
    const float sl = speed_loss_pct(R_cw, R_wave + R_wind);
    EXPECT_GT(sl, 20.f);
}

TEST(SpeedLossIntegration, EqualPowerSlowerActualSpdBurnsMorePerNm)
{
    // Under equal-power: foc_per_nm = foc_mt_h(service_speed) / actual_spd
    // Slower actual_spd → higher cost per nm (engine runs the same; voyage takes longer)
    constexpr float service_spd = 14.f;
    const float foc_mt_h = universal_foc_model(0.f, service_spd).second * service_spd;

    const float foc_per_nm_fast = foc_mt_h / 14.f;
    const float foc_per_nm_slow = foc_mt_h / 10.f;

    EXPECT_GT(foc_per_nm_slow, foc_per_nm_fast);  // equal-power: slower = more per nm
}

TEST(SpeedLossIntegration, EqualPowerFocRatioMatchesTime)
{
    // foc_mt / time_h == foc_mt_h (constant engine burn rate)
    // Verify across two different actual speeds.
    constexpr float service_spd = 14.f;
    constexpr float dist_nm     = 100.f;
    const float foc_mt_h = universal_foc_model(0.f, service_spd).second * service_spd;

    for (float actual_spd : {14.f, 9.f}) {
        const float foc_per_nm = foc_mt_h / actual_spd;
        const float time_h     = dist_nm / actual_spd;
        const float foc_mt     = foc_per_nm * dist_nm;
        EXPECT_NEAR(foc_mt / time_h, foc_mt_h, foc_mt_h * 1e-4f);
    }
}

// ---------------------------------------------------------------------------
// NodeFlags bitmask — verify enum values are distinct and correct
// ---------------------------------------------------------------------------
TEST(NodeFlags, ValuesAreDistinct)
{
    EXPECT_NE(FLAG_ECA,           FLAG_TSS);
    EXPECT_NE(FLAG_ECA,           FLAG_RESTRICTED);
    EXPECT_NE(FLAG_ECA,           FLAG_CANAL_TRANSIT);
    EXPECT_NE(FLAG_TSS,           FLAG_RESTRICTED);
    EXPECT_NE(FLAG_TSS,           FLAG_CANAL_TRANSIT);
    EXPECT_NE(FLAG_RESTRICTED,    FLAG_CANAL_TRANSIT);
    EXPECT_NE(FLAG_CANAL_TRANSIT, FLAG_LOW_CONF_WX);
}

TEST(NodeFlags, NoBitOverlap)
{
    // Each flag must occupy exactly one bit
    const uint8_t all = FLAG_ECA | FLAG_TSS | FLAG_RESTRICTED
                      | FLAG_CANAL_TRANSIT | FLAG_LOW_CONF_WX;
    // Each flag is a power of 2
    for (uint8_t f : {FLAG_ECA, FLAG_TSS, FLAG_RESTRICTED,
                      FLAG_CANAL_TRANSIT, FLAG_LOW_CONF_WX}) {
        EXPECT_EQ(f & (f - 1u), 0u) << "flag 0x" << std::hex << +f
                                     << " is not a power of 2";
    }
    // Combined value equals sum of individual flags
    const uint8_t sum = FLAG_ECA + FLAG_TSS + FLAG_RESTRICTED
                      + FLAG_CANAL_TRANSIT + FLAG_LOW_CONF_WX;
    EXPECT_EQ(all, sum);
}

} // namespace
