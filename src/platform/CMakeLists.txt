# ---------------------------------------------------------------------------
# Platform Subsystem: OS-specific helpers (CpuInfo, mmap live in foundation)
# ---------------------------------------------------------------------------

add_library(gs3d_platform STATIC
    ${PROJECT_SOURCE_DIR}/engine/foundation/diagnostics/CpuInfo.cpp
    ${PROJECT_SOURCE_DIR}/engine/foundation/io/MemoryMappedFile.cpp
)

target_include_directories(gs3d_platform PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(gs3d_platform PUBLIC
    Threads::Threads
)
