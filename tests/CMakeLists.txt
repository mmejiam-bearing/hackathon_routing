enable_testing()

add_executable(maritime_unit_tests
    unit/test_haversine.cpp
    unit/test_bearing.cpp
    unit/test_snap_table.cpp
    unit/test_npy_loader.cpp
    unit/test_weather_buffer.cpp
    unit/test_mmap_region.cpp
    unit/test_edge_weight.cpp
    unit/test_weights_writer.cpp
    unit/test_canal_injector.cpp
    ../graph_builder/src/canal_injector.cpp
    ../graph_builder/src/graph_serialiser.cpp
    ../router_server/src/weights_loader.cpp
)

target_include_directories(maritime_unit_tests PRIVATE
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(maritime_unit_tests PRIVATE
    maritime_lib
    maritime_weather_etl
    GTest::gtest_main
)

add_executable(maritime_integration_tests
    integration/test_graph_round_trip.cpp
    integration/test_snap_round_trip.cpp
    integration/test_weights_round_trip.cpp
    ../graph_builder/src/graph_serialiser.cpp
    ../graph_builder/src/snap_table_builder.cpp
    ../router_server/src/weights_loader.cpp
)

target_include_directories(maritime_integration_tests PRIVATE
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(maritime_integration_tests PRIVATE
    maritime_lib
    maritime_weather_etl
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(maritime_unit_tests)
gtest_discover_tests(maritime_integration_tests)
