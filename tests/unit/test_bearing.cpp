#include "maritime/edge_weight.hpp"

#include <gtest/gtest.h>
#include <cmath>
#include <numbers>

namespace {

constexpr float PI = std::numbers::pi_v<float>;

// bearing_rad returns clockwise-from-north in [-π, π]

TEST(Bearing, DueNorth)
{
    // Moving north along a meridian
    const float b = maritime::bearing_rad(0.f, 0.f, 10.f, 0.f);
    EXPECT_NEAR(b, 0.f, 1e-4f);
}

TEST(Bearing, DueSouth)
{
    const float b = maritime::bearing_rad(10.f, 0.f, 0.f, 0.f);
    EXPECT_NEAR(std::abs(b), PI, 1e-4f);
}

TEST(Bearing, DueEast)
{
    // On the equator moving east
    const float b = maritime::bearing_rad(0.f, 0.f, 0.f, 10.f);
    EXPECT_NEAR(b, PI / 2.f, 1e-4f);
}

TEST(Bearing, DueWest)
{
    const float b = maritime::bearing_rad(0.f, 10.f, 0.f, 0.f);
    EXPECT_NEAR(b, -PI / 2.f, 1e-4f);
}

TEST(Bearing, NorthEastDiagonal)
{
    // Moving NE should give bearing in (0, π/2)
    const float b = maritime::bearing_rad(0.f, 0.f, 10.f, 10.f);
    EXPECT_GT(b, 0.f);
    EXPECT_LT(b, PI / 2.f);
}

TEST(Bearing, RangeIsMinusPiToPi)
{
    // All bearings must stay in [-π, π]
    const std::vector<std::pair<float,float>> pairs = {
        {51.5f, -0.1f}, {40.7f, -74.0f},
        {-33.9f, 151.2f}, {35.7f, 139.7f},
        {1.3f, 103.8f}, {29.9f, 32.6f},
    };
    for (std::size_t i = 0; i + 1 < pairs.size(); ++i) {
        const float b = maritime::bearing_rad(
            pairs[i].first, pairs[i].second,
            pairs[i+1].first, pairs[i+1].second);
        EXPECT_GE(b, -PI);
        EXPECT_LE(b, PI);
    }
}

} // namespace
