add_library(maritime_lib INTERFACE)

target_include_directories(maritime_lib INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(maritime_lib INTERFACE cxx_std_23)
