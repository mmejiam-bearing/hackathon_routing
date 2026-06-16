add_executable(maritime_router_server
    src/main.cpp
    src/server.cpp
    src/weights_loader.cpp
)

target_include_directories(maritime_router_server PRIVATE
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(maritime_router_server PRIVATE
    maritime_lib
    maritime_weather_etl
    routingkit_static
)

set_target_properties(maritime_router_server PROPERTIES
    OUTPUT_NAME "maritime-router-server"
)

add_executable(maritime_route_query
    src/route_query.cpp
    src/server.cpp
    src/weights_loader.cpp
)

target_include_directories(maritime_route_query PRIVATE
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(maritime_route_query PRIVATE
    maritime_lib
    maritime_weather_etl
    routingkit_static
)

set_target_properties(maritime_route_query PROPERTIES
    OUTPUT_NAME "maritime-route-query"
)

add_executable(maritime_voyage_router
    src/voyage_router.cpp
    src/voyage_planner.cpp
)

target_include_directories(maritime_voyage_router PRIVATE
    ${PROJECT_SOURCE_DIR}
)

target_link_libraries(maritime_voyage_router PRIVATE
    maritime_lib
    maritime_weather_etl
    routingkit_static
)

set_target_properties(maritime_voyage_router PROPERTIES
    OUTPUT_NAME "maritime-voyage-router"
)

# ---------------------------------------------------------------------------
# Python bindings — lat/lon -> CCH route, callable from Python.
# Output placed alongside this CMakeLists.txt so `import maritime_router`
# works with no PYTHONPATH setup when scripts add this directory to
# sys.path (see scripts/route.py).
# ---------------------------------------------------------------------------
pybind11_add_module(maritime_router_python
    python/bindings.cpp
    src/server.cpp
    src/weights_loader.cpp
    src/voyage_planner.cpp
)

target_include_directories(maritime_router_python PRIVATE
    ${PROJECT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(maritime_router_python PRIVATE
    maritime_lib
    maritime_weather_etl
    routingkit_static
)

set_target_properties(maritime_router_python PROPERTIES
    OUTPUT_NAME "maritime_router"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
