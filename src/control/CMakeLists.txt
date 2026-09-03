# ---------------------------------------------------------------------------
# Control Subsystem: JSON-RPC 2.0, TCP Server, and Component Registry
# ---------------------------------------------------------------------------

add_library(gs3d_control STATIC
    Base64.cpp
    ComponentRegistry.cpp
    ControlPlane.cpp
    JsonRpc.cpp
    MenuComponents.cpp
    PanelComponents.cpp
    ToolbarComponents.cpp
)

target_include_directories(gs3d_control PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${NLOHMANN_JSON_INCLUDE_DIR}
)

target_link_libraries(gs3d_control PUBLIC
    gs3d_app_support
    gs3d_platform
    Threads::Threads
)

if(WIN32)
    target_link_libraries(gs3d_control PRIVATE ws2_32)
endif()
