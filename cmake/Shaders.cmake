# ---------------------------------------------------------------------------
# Shader Compilation Pipeline (GLSL -> SPIR-V)
# ---------------------------------------------------------------------------

set(GS3D_GENERATED_ASSETS_DIR
    ${CMAKE_BINARY_DIR}/generated-assets
)
set(GS3D_GENERATED_SHADER_DIR
    ${GS3D_GENERATED_ASSETS_DIR}/shaders
)
set(GS3D_SHADER_INCLUDE_DIR
    ${PROJECT_SOURCE_DIR}/assets/shaders
)
set(GS3D_SHADER_COMMON_HEADERS
    ${GS3D_SHADER_INCLUDE_DIR}/point_push_constants.glsl
)

set(GS3D_SHADER_OUTPUTS)
foreach(shader_source
        ${GS3D_SHADER_INCLUDE_DIR}/point.vert
        ${GS3D_SHADER_INCLUDE_DIR}/point.frag)
    get_filename_component(shader_name ${shader_source} NAME)
    set(shader_output
        ${GS3D_GENERATED_SHADER_DIR}/${shader_name}.spv
    )
    add_custom_command(
        OUTPUT ${shader_output}
        COMMAND ${CMAKE_COMMAND} -E make_directory
                ${GS3D_GENERATED_SHADER_DIR}
        COMMAND ${GS3D_GLSLANG_VALIDATOR}
                -V
                -I${GS3D_SHADER_INCLUDE_DIR}
                ${shader_source}
                -o ${shader_output}
        DEPENDS ${shader_source} ${GS3D_SHADER_COMMON_HEADERS}
        COMMENT "Compiling ${shader_name} to SPIR-V"
        VERBATIM
    )
    list(APPEND GS3D_SHADER_OUTPUTS ${shader_output})
endforeach()

add_custom_target(gs3d_shaders ALL DEPENDS ${GS3D_SHADER_OUTPUTS})
