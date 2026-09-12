# ---------------------------------------------------------------------------
# External Dependencies & Tooling
# ---------------------------------------------------------------------------

find_package(Vulkan REQUIRED)
find_package(Threads REQUIRED)

if(ANDROID)
    # nlohmann/json 单头文件（third_party 下载副本）
    set(NLOHMANN_JSON_INCLUDE_DIR
        "${PROJECT_SOURCE_DIR}/third_party/nlohmann/include"
        CACHE PATH "nlohmann/json include dir"
    )
    # Shader SPIR-V 在主机预编译后通过 assets 打包；Android 构建不运行 glslangValidator。
    set(GS3D_GLSLANG_VALIDATOR "" CACHE FILEPATH "" FORCE)
    set(GS3D_GENERATED_SHADER_DIR "" CACHE PATH "" FORCE)
else()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    # TIA-109: 控制面 JSON 依赖（header-only nlohmann/json）。
    # Windows 走 vcpkg（nlohmann-json），Linux 走发行版包（libnlohmann-json3-dev）。
    find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp REQUIRED)

    # Shader sources are the canonical inputs. Generated SPIR-V lives only in the
    # build tree, so a source edit is always visible to the build graph.
    set(
        GS3D_GLSLANG_VALIDATOR
        ""
        CACHE FILEPATH
        "Path to glslangValidator used to compile GeoScatter3D shaders"
    )

    if(NOT GS3D_GLSLANG_VALIDATOR)
        find_program(
            GS3D_GLSLANG_VALIDATOR_DISCOVERED
            NAMES glslangValidator
            HINTS ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}
        )
        set(
            GS3D_GLSLANG_VALIDATOR
            ${GS3D_GLSLANG_VALIDATOR_DISCOVERED}
            CACHE FILEPATH
            "Path to glslangValidator used to compile GeoScatter3D shaders"
            FORCE
        )
    endif()

    if(NOT GS3D_GLSLANG_VALIDATOR)
        message(FATAL_ERROR
            "glslangValidator is required to compile GLSL shaders. Install the "
            "Vulkan SDK or glslang tools, or set GS3D_GLSLANG_VALIDATOR."
        )
    endif()
endif()

if(NOT ANDROID)
    function(copy_runtime_assets target_name)
        add_dependencies(${target_name} gs3d_shaders)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    ${PROJECT_SOURCE_DIR}/assets
                    $<TARGET_FILE_DIR:${target_name}>/assets
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    ${GS3D_GENERATED_SHADER_DIR}
                    $<TARGET_FILE_DIR:${target_name}>/assets/shaders
        )
    endfunction()
else()
    # Android: assets 由 Flutter 打包，无需 CMake 复制。
    function(copy_runtime_assets target_name)
    endfunction()
endif()
