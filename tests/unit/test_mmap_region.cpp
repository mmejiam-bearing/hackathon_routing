#include "maritime/mmap_region.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <type_traits>

namespace {

namespace fs = std::filesystem;

class MmapRegionTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        tmp_dir_ = fs::temp_directory_path() / "maritime_mmap_test";
        fs::create_directories(tmp_dir_);
    }

    void TearDown() override { fs::remove_all(tmp_dir_); }

    std::string write_file(const std::string& name, const std::string& content)
    {
        const std::string path = (tmp_dir_ / name).string();
        std::ofstream f(path, std::ios::binary);
        f.write(content.data(), static_cast<std::streamsize>(content.size()));
        return path;
    }

    fs::path tmp_dir_;
};

// ---------------------------------------------------------------------------
// Ownership contract — verified at compile time elsewhere, but also at
// runtime so test failures are reported clearly.
// ---------------------------------------------------------------------------

TEST(MmapRegionOwnership, IsNotCopyConstructible)
{
    EXPECT_FALSE(std::is_copy_constructible_v<maritime::MmapRegion>);
}

TEST(MmapRegionOwnership, IsNotCopyAssignable)
{
    EXPECT_FALSE(std::is_copy_assignable_v<maritime::MmapRegion>);
}

TEST(MmapRegionOwnership, IsMoveConstructible)
{
    EXPECT_TRUE(std::is_move_constructible_v<maritime::MmapRegion>);
}

TEST(MmapRegionOwnership, IsMoveAssignable)
{
    EXPECT_TRUE(std::is_move_assignable_v<maritime::MmapRegion>);
}

// ---------------------------------------------------------------------------
// Functional tests
// ---------------------------------------------------------------------------

TEST_F(MmapRegionTest, MapsFileAndReadsContent)
{
    const std::string content = "Hello maritime router";
    const std::string path    = write_file("test.bin", content);

    maritime::MmapRegion region(path);

    EXPECT_EQ(region.size(), content.size());
    EXPECT_EQ(
        std::string(static_cast<const char*>(region.data()), region.size()),
        content);
}

TEST_F(MmapRegionTest, AsSpanFloat)
{
    // Write 4 floats
    const std::vector<float> values = {1.f, 2.f, 3.f, 4.f};
    const std::string path = (tmp_dir_ / "floats.bin").string();
    {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(values.data()),
                values.size() * sizeof(float));
    }

    maritime::MmapRegion region(path);
    const auto span = region.as_span<float>();

    ASSERT_EQ(span.size(), 4u);
    for (std::size_t i = 0; i < 4; ++i)
        EXPECT_FLOAT_EQ(span[i], values[i]);
}

TEST_F(MmapRegionTest, AsSpanWithOffset)
{
    const std::vector<uint32_t> values = {10u, 20u, 30u, 40u};
    const std::string path = (tmp_dir_ / "ints.bin").string();
    {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(values.data()),
                values.size() * sizeof(uint32_t));
    }

    maritime::MmapRegion region(path);
    // Skip first element (4 bytes offset) and read 3
    const auto span = region.as_span<uint32_t>(sizeof(uint32_t), 3);

    ASSERT_EQ(span.size(), 3u);
    EXPECT_EQ(span[0], 20u);
    EXPECT_EQ(span[1], 30u);
    EXPECT_EQ(span[2], 40u);
}

TEST_F(MmapRegionTest, ThrowsOnMissingFile)
{
    EXPECT_THROW(
        maritime::MmapRegion("/nonexistent/path/file.bin"),
        std::system_error);
}

TEST_F(MmapRegionTest, ThrowsOnEmptyFile)
{
    const std::string path = write_file("empty.bin", "");
    EXPECT_THROW({
        maritime::MmapRegion region(path);
    }, std::invalid_argument);
}

TEST_F(MmapRegionTest, MoveConstructorTransfersOwnership)
{
    const std::string content = "move test";
    const std::string path    = write_file("move.bin", content);

    maritime::MmapRegion a(path);
    const void* orig_ptr = a.data();

    maritime::MmapRegion b(std::move(a));

    // b now owns the mapping
    EXPECT_EQ(b.data(),   orig_ptr);
    EXPECT_EQ(b.size(),   content.size());

    // a is in moved-from state
    EXPECT_EQ(a.data(),   nullptr);
    EXPECT_EQ(a.size(),   0u);
}

TEST_F(MmapRegionTest, MoveAssignmentTransfersOwnership)
{
    const std::string path = write_file("assign.bin", "hello");
    maritime::MmapRegion a(path);
    const void* orig_ptr = a.data();

    const std::string path2 = write_file("assign2.bin", "world!");
    maritime::MmapRegion b(path2);

    b = std::move(a);

    EXPECT_EQ(b.data(), orig_ptr);
    EXPECT_EQ(a.data(), nullptr);
}

TEST_F(MmapRegionTest, AsSpanThrowsOnOversizedCount)
{
    const std::string path = write_file("small.bin", "ABCD");
    maritime::MmapRegion region(path);

    // 4 bytes → 1 uint32_t max; requesting 2 should throw
    EXPECT_THROW(region.as_span<uint32_t>(0, 2), std::out_of_range);
}

} // namespace
