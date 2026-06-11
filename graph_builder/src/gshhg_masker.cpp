#include "gshhg_masker.hpp"

#include <gdal.h>
#include <gdal_priv.h>
#include <gdal_alg.h>
#include <ogr_api.h>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace maritime::graph_builder {

// ---------------------------------------------------------------------------
// Constructor — rasterize GSHHG_h_L1.shp onto the 721×1440 graph grid.
//
// Strategy:
//   1. Open GSHHS_h_L1.shp (high-resolution level-1 land polygons).
//   2. Create a 1440×721 in-memory GDAL raster (all zeros = ocean).
//   3. Set GeoTransform so pixel [row=0, col=0] centres at (90°N, −180°W).
//   4. Call GDALRasterizeLayer — burns value 1 for every land polygon.
//   5. Copy raster pixels into land_mask_.
//
// After construction:
//   - is_land() is a single array read — O(1), noexcept.
//   - No GDAL handles are retained as members (Rule of Zero satisfied).
// ---------------------------------------------------------------------------
GshhgMasker::GshhgMasker(const std::string& path)
{
    GDALAllRegister();

    const std::string shp = path + "/GSHHS_h_L1.shp";
    GDALDataset* src = static_cast<GDALDataset*>(
        GDALOpenEx(shp.c_str(),
                   GDAL_OF_VECTOR | GDAL_OF_READONLY,
                   nullptr, nullptr, nullptr));
    if (!src)
        throw std::runtime_error("GshhgMasker: cannot open " + shp);

    OGRLayer* layer = src->GetLayer(0);
    if (!layer) {
        GDALClose(src);
        throw std::runtime_error("GshhgMasker: no layer in " + shp);
    }

    // Create in-memory raster: 1440 cols × 721 rows, 1 band (Byte), zeroed.
    GDALDriver* mem_drv = GetGDALDriverManager()->GetDriverByName("MEM");
    GDALDataset* raster = mem_drv->Create("", 1440, 721, 1, GDT_Byte, nullptr);
    if (!raster) {
        GDALClose(src);
        throw std::runtime_error("GshhgMasker: failed to create in-memory raster");
    }

    // GeoTransform maps pixel [row, col] to geographic coordinates.
    // Pixel centres must align with the graph grid:
    //   col 0 centre = −180°W  → left pixel edge = −180.125°
    //   row 0 centre = +90°N   → top pixel edge  = +90.125°
    //   pixel width  = 0.25°, pixel height = −0.25° (north-up)
    double gt[6] = {-180.125, 0.25, 0.0, 90.125, 0.0, -0.25};
    raster->SetGeoTransform(gt);

    // Burn all land polygons with value 1.
    int       burn_band = 1;
    double    burn_val  = 1.0;
    OGRLayerH layer_h   = static_cast<OGRLayerH>(layer);
    GDALRasterizeLayers(static_cast<GDALDatasetH>(raster),
                        1, &burn_band,
                        1, &layer_h,
                        nullptr, nullptr,
                        &burn_val, nullptr,
                        nullptr, nullptr);

    // Read raster into a flat uint8 buffer, then copy into land_mask_.
    std::vector<uint8_t> buf(721u * 1440u, 0u);
    GDALRasterBand* band = raster->GetRasterBand(1);
    band->RasterIO(GF_Read, 0, 0, 1440, 721,
                   buf.data(), 1440, 721, GDT_Byte, 0, 0);

    land_mask_.assign(buf.begin(), buf.end());

    GDALClose(raster);
    GDALClose(src);
}

bool GshhgMasker::is_land(float lat, float lon) const noexcept
{
    const int r = std::clamp(static_cast<int>((90.f - lat) / 0.25f), 0, 720);
    const int c = std::clamp(static_cast<int>((lon + 180.f) / 0.25f), 0, 1439);
    return land_mask_[static_cast<std::size_t>(r) * 1440u + static_cast<std::size_t>(c)];
}

} // namespace maritime::graph_builder
