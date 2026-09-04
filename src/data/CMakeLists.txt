# ---------------------------------------------------------------------------
# Data Subsystem: CSV Readers, GS3D v2, LOD, and Tile Formats
# ---------------------------------------------------------------------------

add_library(gs3d_data STATIC
    CsvStreamReader.cpp
    CsvSniffer.cpp
    CsvChunkUtils.cpp
    CsvChunkPlanner.cpp
    CsvChunkReader.cpp
    DataSchema.cpp
    Delimiter.cpp
    Gs3dFormat.cpp
    Gs3dReader.cpp
    Gs3dVerifier.cpp
    Gs3dDataset.cpp
    Gs3dLodDataset.cpp
    Gs3dLodFormat.cpp
    Gs3dLodReader.cpp
    Gs3dTileFormat.cpp
    Gs3dTileReader.cpp
)

target_include_directories(gs3d_data PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(gs3d_data PUBLIC
    gs3d_platform
    Threads::Threads
)
