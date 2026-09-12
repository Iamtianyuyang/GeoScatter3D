# ---------------------------------------------------------------------------
# Platform Subsystem: OS, CPU, and Memory-Mapped Files
# ---------------------------------------------------------------------------

add_library(gs3d_platform STATIC
    CpuInfo.cpp
    MemoryMappedFile.cpp
)

target_include_directories(gs3d_platform PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(gs3d_platform PUBLIC
    Threads::Threads
)
