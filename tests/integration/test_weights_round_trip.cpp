#include "weather_etl/src/weights_writer.hpp"
#include "maritime/weights_header.hpp"
#include "router_server/src/weights_loader.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

// ---------------------------------------------------------------------------
// Integration tests for the weights.bin write → load round trip.
// These tests exercise the full file I/O path: WeightsWriter::write() 
// produces a binary file that WeightsLoader::load() must reconstruct exactly.
// ---------------------------------------------------------------------------

namespace {

namespace fs = std::filesystem;

class WeightsIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        tmp_  = fs::temp_directory_path() / "maritime_weights_int_test";
        fs::create_directories(tmp_);
        path_ = (tmp_ / "weights.bin").string();
    }
    void TearDown() override { fs::remove_all(tmp_); }

    fs::path    tmp_;
    std::string path_;
};

TEST_F(WeightsIntegrationTest, SingleEdgeRoundTrip)
{
    const std::vector<uint32_t> w = {12345u};
    const int64_t epoch = 1000LL;

    maritime::weather_etl::WeightsWriter::write(w, epoch, path_);
    const auto p = maritime::router_server::WeightsLoader::load(path_);

    ASSERT_EQ(p.weights.size(), 1u);
    EXPECT_EQ(p.weights[0], 12345u);
    EXPECT_EQ(p.base_epoch, epoch);
}

TEST_F(WeightsIntegrationTest, FileExistsAfterWrite)
{
    maritime::weather_etl::WeightsWriter::write({1u, 2u, 3u}, 0LL, path_);
    EXPECT_TRUE(fs::exists(path_));
    EXPECT_GT(fs::file_size(path_), sizeof(maritime::WeightsHeader));
}

TEST_F(WeightsIntegrationTest, FileSizeIsCorrect)
{
    const std::size_t n_edges = 500u;
    std::vector<uint32_t> w(n_edges, 1000u);
    maritime::weather_etl::WeightsWriter::write(w, 0LL, path_);

    const std::size_t expected_size =
        sizeof(maritime::WeightsHeader)
        + n_edges * sizeof(uint32_t);

    EXPECT_EQ(fs::file_size(path_), expected_size);
}

TEST_F(WeightsIntegrationTest, OverwritesPreviousFile)
{
    // Write once
    maritime::weather_etl::WeightsWriter::write({1u}, 0LL, path_);
    // Write again with different content
    maritime::weather_etl::WeightsWriter::write({42u, 43u}, 999LL, path_);

    const auto p = maritime::router_server::WeightsLoader::load(path_);
    ASSERT_EQ(p.weights.size(), 2u);
    EXPECT_EQ(p.weights[0], 42u);
    EXPECT_EQ(p.weights[1], 43u);
    EXPECT_EQ(p.base_epoch, 999LL);
}

TEST_F(WeightsIntegrationTest, MaxUint32EdgeWeight)
{
    // Verify large weights survive without truncation
    const std::vector<uint32_t> w = {0xFFFFFFFEu};
    maritime::weather_etl::WeightsWriter::write(w, 0LL, path_);
    const auto p = maritime::router_server::WeightsLoader::load(path_);
    ASSERT_EQ(p.weights.size(), 1u);
    EXPECT_EQ(p.weights[0], 0xFFFFFFFEu);
}

TEST_F(WeightsIntegrationTest, HeaderVersionIsOne)
{
    maritime::weather_etl::WeightsWriter::write({1u}, 0LL, path_);

    std::ifstream f(path_, std::ios::binary);
    maritime::WeightsHeader hdr{};
    f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    EXPECT_EQ(hdr.version, 1u);
}

TEST_F(WeightsIntegrationTest, NegativeEpochPreserved)
{
    // Epochs before Unix epoch (e.g. historical data) must not be corrupted
    const int64_t epoch = -1LL;
    maritime::weather_etl::WeightsWriter::write({1u}, epoch, path_);
    const auto p = maritime::router_server::WeightsLoader::load(path_);
    EXPECT_EQ(p.base_epoch, epoch);
}

} // namespace
