#include "graph_serialiser.hpp"

#include "maritime/static_graph.hpp"

#include <fstream>
#include <stdexcept>

namespace maritime::graph_builder {

void serialise_graph(
    const GraphData&   data,
    const std::string& graph_out_path,
    const std::string& flags_out_path)
{
    // -------------------------------------------------------------------
    // graph.bin
    // -------------------------------------------------------------------
    {
        std::ofstream f(graph_out_path, std::ios::binary | std::ios::trunc);
        if (!f.is_open())
            throw std::runtime_error(
                "serialise_graph: cannot open " + graph_out_path);

        maritime::GraphHeader hdr{};
        hdr.magic   = 0x4752'414Du;  // "MARG"
        hdr.version = 1;
        hdr.n_nodes = data.n_nodes();
        hdr.n_edges = data.n_edges();

        auto write = [&](const void* ptr, std::size_t bytes) {
            f.write(static_cast<const char*>(ptr),
                    static_cast<std::streamsize>(bytes));
        };

        write(&hdr,                  sizeof(hdr));
        write(data.lat.data(),       data.lat.size()          * sizeof(float));
        write(data.lon.data(),       data.lon.size()          * sizeof(float));
        write(data.depth.data(),     data.depth.size()        * sizeof(uint16_t));
        write(data.row_ptr.data(),   data.row_ptr.size()      * sizeof(uint32_t));
        write(data.col_idx.data(),   data.col_idx.size()      * sizeof(uint32_t));
        write(data.base_dist_nm.data(), data.base_dist_nm.size() * sizeof(float));

        if (!f)
            throw std::runtime_error(
                "serialise_graph: write failed to " + graph_out_path);
    }

    // -------------------------------------------------------------------
    // flags.bin — flat uint8 array, no header needed
    // -------------------------------------------------------------------
    {
        std::ofstream f(flags_out_path, std::ios::binary | std::ios::trunc);
        if (!f.is_open())
            throw std::runtime_error(
                "serialise_graph: cannot open " + flags_out_path);

        f.write(reinterpret_cast<const char*>(data.flags.data()),
                static_cast<std::streamsize>(data.flags.size()));

        if (!f)
            throw std::runtime_error(
                "serialise_graph: write failed to " + flags_out_path);
    }
}

} // namespace maritime::graph_builder
