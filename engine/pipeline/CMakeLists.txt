# ---------------------------------------------------------------------------
# Pipeline Subsystem: stages, compute, writers, verify (no Vulkan dependency)
# ---------------------------------------------------------------------------

set(GS3D_ENGINE ${PROJECT_SOURCE_DIR}/engine)

add_library(gs3d_preprocess STATIC
    ${GS3D_ENGINE}/pipeline/compute/CsvToGs3dConverter.cpp
    ${GS3D_ENGINE}/pipeline/compute/StatisticsPass.cpp
    ${GS3D_ENGINE}/pipeline/stages/BundleBuild.cpp
    ${GS3D_ENGINE}/pipeline/stages/BundleJob.cpp
    ${GS3D_ENGINE}/pipeline/stages/PointStage.cpp
    ${GS3D_ENGINE}/pipeline/writers/Gs3dWriter.cpp
    ${GS3D_ENGINE}/pipeline/writers/Gs3dLodWriter.cpp
    ${GS3D_ENGINE}/pipeline/writers/Gs3dTileWriter.cpp
    ${GS3D_ENGINE}/pipeline/verify/Gs3dVerifier.cpp
)

target_include_directories(gs3d_preprocess PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${GS3D_ENGINE}
)

target_link_libraries(gs3d_preprocess PUBLIC
    gs3d_data
)
