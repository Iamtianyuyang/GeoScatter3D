# ---------------------------------------------------------------------------
# Data Subsystem: CSV Readers, GS3D v2, LOD, and Tile Formats
# ---------------------------------------------------------------------------

set(GS3D_ENGINE ${PROJECT_SOURCE_DIR}/engine)

add_library(gs3d_data STATIC
    ${GS3D_ENGINE}/data/csv/CsvStreamReader.cpp
    ${GS3D_ENGINE}/data/csv/CsvSniffer.cpp
    ${GS3D_ENGINE}/data/csv/CsvChunkUtils.cpp
    ${GS3D_ENGINE}/data/csv/CsvChunkPlanner.cpp
    ${GS3D_ENGINE}/data/csv/CsvChunkReader.cpp
    ${GS3D_ENGINE}/data/csv/DataSchema.cpp
    ${GS3D_ENGINE}/data/csv/Delimiter.cpp
    ${GS3D_ENGINE}/data/format/Gs3dFormat.cpp
    ${GS3D_ENGINE}/data/format/PointChunk.cpp
    ${GS3D_ENGINE}/data/format/Gs3dReader.cpp
    ${GS3D_ENGINE}/data/format/Gs3dDataset.cpp
    ${GS3D_ENGINE}/data/format/Gs3dExporter.cpp
    ${GS3D_ENGINE}/data/lod/Gs3dLodDataset.cpp
    ${GS3D_ENGINE}/data/lod/Gs3dLodFormat.cpp
    ${GS3D_ENGINE}/data/lod/Gs3dLodReader.cpp
    ${GS3D_ENGINE}/data/tile/Gs3dTileFormat.cpp
    ${GS3D_ENGINE}/data/tile/Gs3dTileReader.cpp
)

target_include_directories(gs3d_data PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${GS3D_ENGINE}
)

target_link_libraries(gs3d_data PUBLIC
    gs3d_platform
    Threads::Threads
)
