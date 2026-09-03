# ---------------------------------------------------------------------------
# Preprocess Subsystem: Statistics, CSV to GS3D Converter, and Tile Writers
# ---------------------------------------------------------------------------

add_library(gs3d_preprocess STATIC
    StatisticsPass.cpp
    Gs3dWriter.cpp
    CsvToGs3dConverter.cpp
    Gs3dLodWriter.cpp
    Gs3dTileWriter.cpp
)

target_include_directories(gs3d_preprocess PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(gs3d_preprocess PUBLIC
    gs3d_data
)
