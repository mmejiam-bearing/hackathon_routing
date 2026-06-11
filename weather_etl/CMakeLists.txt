add_library(maritime_weather_etl STATIC
    src/weights_writer.cpp
    src/npy_loader.cpp
    src/ocs_loader.cpp
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
