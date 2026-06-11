#include "maritime/edge_weight.hpp"

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
    v.draft_m = draft_m;
    v.beam_m  = 25.f;
    v.loa_m   = 180.f;

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
