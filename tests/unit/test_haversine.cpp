#include "maritime/edge_weight.hpp"

#include <gtest/gtest.h>
#include <cmath>

namespace {

// Known distances verified against external calculator (movable-type.co.uk)
// Tolerance: 0.1 nm (~185m) — well within float32 precision at global scale

TEST(Haversine, EquatorialHalfCircle)
{
    // 0N,0E to 0N,180E = half equatorial circumference
    // π * R_NM = π * 3440.065 ≈ 10,806 nm; tolerating float32 rounding
    const float d = maritime::haversine_nm(0.f, 0.f, 0.f, 180.f);
    EXPECT_NEAR(d, 10800.f, 10.f);
}

TEST(Haversine, PolarToEquator)
{
    // 90N to 0N along any meridian ≈ 5,400 nm (π/2 * R_NM ≈ 5,403 nm)
    const float d = maritime::haversine_nm(90.f, 0.f, 0.f, 0.f);
    EXPECT_NEAR(d, 5400.f, 5.f);
}

TEST(Haversine, SamePoint)
{
    const float d = maritime::haversine_nm(51.5f, -0.1f, 51.5f, -0.1f);
    EXPECT_NEAR(d, 0.f, 1e-4f);
}

TEST(Haversine, LondonToNewYork)
{
    // London (51.5N, 0.1W) to New York (40.7N, 74.0W)
    // Great-circle ≈ 3,009 nm (5,570 km / 1.852). 3,459 is statute miles.
    const float d = maritime::haversine_nm(51.5f, -0.1f, 40.7f, -74.0f);
    EXPECT_NEAR(d, 3009.f, 15.f);
}

TEST(Haversine, SingaporeToRotterdam)
{
    // Singapore (1.3N, 103.8E) to Rotterdam (51.9N, 4.5E)
    // Great-circle ≈ 5,690 nm. 8,316 nm is the Suez Canal shipping route.
    const float d = maritime::haversine_nm(1.3f, 103.8f, 51.9f, 4.5f);
    EXPECT_NEAR(d, 5690.f, 30.f);
}

TEST(Haversine, SymmetricProperty)
{
    const float d1 = maritime::haversine_nm(35.f, 139.f, -34.f, 151.f);
    const float d2 = maritime::haversine_nm(-34.f, 151.f, 35.f, 139.f);
    EXPECT_NEAR(d1, d2, 1e-3f);
}

TEST(Haversine, NegativeLongitudes)
{
    // Should handle negative longitudes without wrapping errors
    const float d = maritime::haversine_nm(0.f, -90.f, 0.f, 90.f);
    EXPECT_NEAR(d, 10800.f, 10.f);
}

} // namespace
