add_library(maritime_weather_etl STATIC
    src/weights_writer.cpp
    src/avg_weather_loader.cpp
)

target_link_libraries(maritime_weather_etl PUBLIC maritime_lib routingkit_static)

target_include_directories(maritime_weather_etl PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

add_executable(maritime_weights_writer src/main.cpp)

target_link_libraries(maritime_weights_writer PRIVATE
    maritime_weather_etl
    maritime_lib
)

set_target_properties(maritime_weights_writer PROPERTIES
    OUTPUT_NAME "maritime-weights-writer"
)

# ---------------------------------------------------------------------------
# Python bindings — backs the `maritime_weather_etl` import in etl.py.
# Output placed alongside etl.py so `import maritime_weather_etl` works with
# no PYTHONPATH setup (the importing script's own directory is on sys.path).
# ---------------------------------------------------------------------------
pybind11_add_module(maritime_weather_etl_python python/bindings.cpp)

target_link_libraries(maritime_weather_etl_python PRIVATE
    maritime_weather_etl
    maritime_lib
)

set_target_properties(maritime_weather_etl_python PROPERTIES
    OUTPUT_NAME "maritime_weather_etl"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
