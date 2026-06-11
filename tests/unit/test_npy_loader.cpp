#include "weather_etl/src/npy_loader.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Helpers — write synthetic .npy files to a tmp directory
// ---------------------------------------------------------------------------
namespace {

namespace fs = std::filesystem;

// Minimal NPY v1.0 header for shape (721, 1440), dtype '<f2'
static std::vector<uint8_t> make_npy_header()
{
    const std::string dict =
        "{'descr': '<f2', 'fortran_order': False, 'shape': (721, 1440), }";

    // Header must be padded to 64-byte alignment
    // Total prefix = 10 bytes (magic + version + header_len)
    const std::size_t prefix  = 10;
    const std::size_t dict_sz = dict.size() + 1;  // +1 for '\n' terminator
    const std::size_t pad     = 64 - (prefix + dict_sz) % 64;
    const std::size_t hdr_len = dict_sz + pad;

    std::vector<uint8_t> header;
    header.push_back(0x93);
    header.push_back('N'); header.push_back('U'); header.push_back('M');
    header.push_back('P'); header.push_back('Y');
    header.push_back(1);   // major
    header.push_back(0);   // minor
    const uint16_t hl16 = static_cast<uint16_t>(hdr_len);
    header.push_back(static_cast<uint8_t>(hl16 & 0xFF));
    header.push_back(static_cast<uint8_t>(hl16 >> 8));

    for (char c : dict) header.push_back(static_cast<uint8_t>(c));
    header.push_back('\n');
    for (std::size_t i = 0; i < pad; ++i) header.push_back(' ');

    return header;
}

static std::string write_test_npy(
    const fs::path&              dir,
    const std::string&           name,
    const std::vector<uint16_t>& data,
    bool                         double_write = false)
{
    const std::string path = (dir / name).string();
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    auto hdr = make_npy_header();
    f.write(reinterpret_cast<const char*>(hdr.data()), hdr.size());
    f.write(reinterpret_cast<const char*>(data.data()),
            data.size() * sizeof(uint16_t));
    if (double_write)
        f.write(reinterpret_cast<const char*>(data.data()),
                data.size() * sizeof(uint16_t));
    return path;
}

class NpyLoaderTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        tmp_dir_ = fs::temp_directory_path() / "maritime_npy_test";
        fs::create_directories(tmp_dir_);
    }

    void TearDown() override { fs::remove_all(tmp_dir_); }

    fs::path tmp_dir_;
};

} // namespace

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_F(NpyLoaderTest, LoadsSingleCopyCorrectly)
{
    constexpr std::size_t N = 721 * 1440;
    std::vector<uint16_t> data(N);
    for (std::size_t i = 0; i < N; ++i)
        data[i] = static_cast<uint16_t>(i % 1000);

    const std::string path = write_test_npy(tmp_dir_, "test.npy", data);
    const auto result = maritime::weather_etl::NpyLoader::load(path);

    ASSERT_EQ(result.size(), N);
    for (std::size_t i = 0; i < N; ++i)
        EXPECT_EQ(result[i], data[i]) << "mismatch at index " << i;
}

TEST_F(NpyLoaderTest, SkipsDoubleWriteDuplicate)
{
    // ETL double-write: file contains two identical copies of the array.
    // Loader must return only the first N elements.
    constexpr std::size_t N = 721 * 1440;
    std::vector<uint16_t> data(N, 0x3C00u);  // float16 = 1.0

    const std::string path =
        write_test_npy(tmp_dir_, "double.npy", data, /*double_write=*/true);

    const auto result = maritime::weather_etl::NpyLoader::load(path);

    ASSERT_EQ(result.size(), N);
    for (std::size_t i = 0; i < N; ++i)
        EXPECT_EQ(result[i], 0x3C00u);
}

TEST_F(NpyLoaderTest, ThrowsOnMissingFile)
{
    EXPECT_THROW(
        maritime::weather_etl::NpyLoader::load("/nonexistent/path/file.npy"),
        std::runtime_error);
}

TEST_F(NpyLoaderTest, ThrowsOnBadMagic)
{
    const std::string path = (tmp_dir_ / "bad.npy").string();
    std::ofstream f(path, std::ios::binary);
    f.write("NOT_NUMPY_DATA_HERE_12345678", 28);
    EXPECT_THROW(
        maritime::weather_etl::NpyLoader::load(path),
        std::runtime_error);
}

TEST_F(NpyLoaderTest, ThrowsOnTruncatedFile)
{
    // Write a valid header but only 100 data bytes (way too short)
    auto hdr = make_npy_header();
    const std::string path = (tmp_dir_ / "truncated.npy").string();
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(hdr.data()), hdr.size());
    const std::vector<uint16_t> tiny(100, 0);
    f.write(reinterpret_cast<const char*>(tiny.data()), tiny.size() * 2);

    EXPECT_THROW(
        maritime::weather_etl::NpyLoader::load(path),
        std::runtime_error);
}
