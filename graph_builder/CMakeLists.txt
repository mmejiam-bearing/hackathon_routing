find_package(netCDF REQUIRED)
find_package(GDAL REQUIRED)

add_executable(maritime_graph_builder
    src/main.cpp
    src/canal_injector.cpp
    src/gebco_loader.cpp
    src/gshhg_masker.cpp
    src/graph_builder.cpp
    src/snap_table_builder.cpp
    src/cch_preprocessor.cpp
    src/graph_serialiser.cpp
)

target_link_libraries(maritime_graph_builder PRIVATE
    maritime_lib
    netCDF::netcdf
    GDAL::GDAL
    routingkit_static
)

set_target_properties(maritime_graph_builder PROPERTIES
    OUTPUT_NAME "maritime-graph-builder"
)

# Standalone snap table builder — does not require GEBCO or GSHHG
add_executable(maritime_snap_builder
    src/snap_builder_main.cpp
    src/snap_table_builder.cpp
)

target_link_libraries(maritime_snap_builder PRIVATE
    maritime_lib
)

set_target_properties(maritime_snap_builder PROPERTIES
    OUTPUT_NAME "maritime-snap-builder"
)
