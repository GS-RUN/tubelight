# SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
# Copyright (c) 2026 Alonso J. Núñez (GS·RUN)
#
# Phase 3c of ADR-0002 — shader build pipeline.
#
# Each shaders/pass*.frag is translated GLSL → SPIR-V → HLSL → DXIL via:
#   glslang   -V -S frag --target-env vulkan1.0 -e main
#   spirv-cross --hlsl --shader-model 60 --output <out.hlsl>
#   dxc       -T ps_6_0 -E main -Fo <out.dxil>
#
# Same path for shaders/fullscreen.vert (-S vert / -T vs_6_0).
#
# Outputs land under ${CMAKE_BINARY_DIR}/shaders/{spirv,hlsl,dxil}/.
# A POST_BUILD step on the consuming target copies the .dxil files to
# the runtime directory (next to tubelight.exe).
#
# Constitution C3c-1 enforced: no .hlsl / .spv / .dxil committed to repo.
#
# Tool discovery order:
#   - glslang:     vcpkg tools/glslang/glslangValidator.exe
#   - spirv-cross: vcpkg tools/spirv-cross/spirv-cross.exe
#   - dxc:         Windows SDK ${WindowsSdkBinPath}/x64/dxc.exe
#
# All three are required when TUBELIGHT_BUILD_DX12=ON. On Linux dxc is
# absent (no DX12) and the function is a no-op.

if(NOT TUBELIGHT_BUILD_DX12)
    function(tubelight_compile_shaders target)
        # No-op when DX12 backend is disabled.
    endfunction()
    return()
endif()

# ----- locate tools ----------------------------------------------------

# glslang (vcpkg). Falls back to PATH if vcpkg layout changes.
find_program(TL_GLSLANG_EXE
    NAMES glslangValidator glslang
    HINTS
        "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/tools/glslang"
        "${VCPKG_INSTALLED_DIR}/x64-windows/tools/glslang"
    DOC "glslang executable (GLSL → SPIR-V compiler)"
)
if(NOT TL_GLSLANG_EXE)
    message(FATAL_ERROR
        "Phase 3c needs glslangValidator. vcpkg installs it via "
        "'glslang[tools]' (in vcpkg.json). Did the install step run?")
endif()

# spirv-cross (vcpkg). The executable name is just `spirv-cross`.
find_program(TL_SPIRV_CROSS_EXE
    NAMES spirv-cross
    HINTS
        "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-windows/tools/spirv-cross"
        "${VCPKG_INSTALLED_DIR}/x64-windows/tools/spirv-cross"
    DOC "SPIRV-Cross executable (SPIR-V → HLSL transpiler)"
)
if(NOT TL_SPIRV_CROSS_EXE)
    message(FATAL_ERROR
        "Phase 3c needs spirv-cross. vcpkg should provide it; check the "
        "install log under build/.../vcpkg_installed/.")
endif()

# dxc from Windows SDK. We prefer the SDK version over vcpkg's
# directx-shader-compiler because (a) the SDK is already required by the
# project, (b) the vcpkg port is not in our current builtin-baseline.
# Search the same SDK version cmake selected for the compiler.
if(NOT TL_DXC_EXE)
    set(_sdk_root "$ENV{WindowsSdkDir}")
    set(_sdk_ver  "$ENV{WindowsSDKVersion}")
    string(REGEX REPLACE "\\\\$" "" _sdk_ver "${_sdk_ver}")
    if(_sdk_root AND _sdk_ver)
        find_program(TL_DXC_EXE
            NAMES dxc
            HINTS "${_sdk_root}/bin/${_sdk_ver}/x64"
            NO_DEFAULT_PATH
        )
    endif()
    # Fallback: PATH (e.g. installed Vulkan SDK ships dxc.exe).
    if(NOT TL_DXC_EXE)
        find_program(TL_DXC_EXE NAMES dxc DOC "DirectX Shader Compiler")
    endif()
endif()
if(NOT TL_DXC_EXE)
    message(FATAL_ERROR
        "Phase 3c needs dxc.exe (Windows SDK 10.0.20348+). Searched: "
        "$ENV{WindowsSdkDir}/bin/$ENV{WindowsSDKVersion}/x64/dxc.exe and PATH.")
endif()

message(STATUS "Phase 3c shader toolchain:")
message(STATUS "  glslang:     ${TL_GLSLANG_EXE}")
message(STATUS "  spirv-cross: ${TL_SPIRV_CROSS_EXE}")
message(STATUS "  dxc:         ${TL_DXC_EXE}")

# ----- per-shader command graph ----------------------------------------

# tubelight_add_shader(<source> <stage>)
#   <source>  absolute path to shader/<file>.{frag,vert}
#   <stage>   "vert" or "frag" — selects -S / -T flags
# Appends the produced .dxil to the parent's TL_DXIL_OUTPUTS variable.
function(tubelight_add_shader source stage)
    get_filename_component(_name "${source}" NAME_WE)
    set(_spv  "${CMAKE_BINARY_DIR}/shaders/spirv/${_name}.spv")
    set(_hlsl "${CMAKE_BINARY_DIR}/shaders/hlsl/${_name}.hlsl")
    set(_dxil "${CMAKE_BINARY_DIR}/shaders/dxil/${_name}.dxil")

    if(stage STREQUAL "frag")
        set(_glslang_stage "frag")
        set(_dxc_profile  "ps_6_0")
    elseif(stage STREQUAL "vert")
        set(_glslang_stage "vert")
        set(_dxc_profile  "vs_6_0")
    else()
        message(FATAL_ERROR "tubelight_add_shader: unknown stage '${stage}'")
    endif()

    # Step 1: GLSL → SPIR-V. --target-env vulkan1.0 to get separated
    # samplers (SPIRV-Cross can emit HLSL SM 6.0-compatible code).
    add_custom_command(
        OUTPUT "${_spv}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/shaders/spirv"
        # Phase 3c F3c-2: switched to Vulkan SPIR-V target. Scalar uniforms
        # are now wrapped in explicit `layout(std140, binding=0) uniform U
        # { ... };` blocks so the HLSL output is a deterministic cbuffer
        # (not the $Globals roulette). gl_VertexID → gl_VertexIndex shim
        # in fullscreen.vert is guarded by TUBELIGHT_VULKAN.
        COMMAND "${TL_GLSLANG_EXE}"
                -V
                -S ${_glslang_stage}
                --target-env vulkan1.0
                --auto-map-bindings
                --auto-map-locations
                -DTUBELIGHT_VULKAN
                -e main
                -o "${_spv}"
                "${source}"
        DEPENDS "${source}"
        COMMENT "[shaders] glslang ${_name}.${stage} → SPIR-V"
        VERBATIM
    )

    # Step 2: SPIR-V → HLSL SM 6.0. --separate-image-samplers ensures the
    # HLSL is SM 6.0-compatible (R3c-1 mitigation).
    add_custom_command(
        OUTPUT "${_hlsl}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/shaders/hlsl"
        COMMAND "${TL_SPIRV_CROSS_EXE}"
                --hlsl
                --shader-model 60
                --output "${_hlsl}"
                "${_spv}"
        DEPENDS "${_spv}"
        COMMENT "[shaders] spirv-cross ${_name} → HLSL SM 6.0"
        VERBATIM
    )

    # Step 3: HLSL → DXIL. -Qstrip_reflect for smaller bytecode; we don't
    # need the reflection at runtime (Pipeline knows the layout statically).
    add_custom_command(
        OUTPUT "${_dxil}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/shaders/dxil"
        COMMAND "${TL_DXC_EXE}"
                -T ${_dxc_profile}
                -E main
                -Qstrip_reflect
                -Fo "${_dxil}"
                "${_hlsl}"
        DEPENDS "${_hlsl}"
        COMMENT "[shaders] dxc ${_name} → DXIL (${_dxc_profile})"
        VERBATIM
    )

    # Append to the parent's list of outputs.
    set(_outs ${TL_DXIL_OUTPUTS} "${_dxil}")
    set(TL_DXIL_OUTPUTS "${_outs}" PARENT_SCOPE)
endfunction()

# tubelight_compile_shaders(<consumer_target>)
#   Registers all known shaders. Creates a `tubelight_shaders` custom
#   target that <consumer_target> depends on, then copies the .dxil files
#   next to the consumer's output binary in POST_BUILD.
function(tubelight_compile_shaders consumer_target)
    set(TL_DXIL_OUTPUTS "")
    set(_shader_dir "${CMAKE_SOURCE_DIR}/shaders")

    # Fragment shaders (8 passes).
    foreach(_f
        pass_minus1_signal
        pass0_analysis
        pass1_dither_reconstruct
        pass2_beam_scanlines
        pass3_mask
        pass4_bloom
        pass5_temporal
        pass6_composition
    )
        tubelight_add_shader("${_shader_dir}/${_f}.frag" frag)
    endforeach()

    # Single vertex shader (fullscreen triangle).
    tubelight_add_shader("${_shader_dir}/fullscreen.vert" vert)

    # Aggregate target that builds all 9 dxil files.
    add_custom_target(tubelight_shaders ALL DEPENDS ${TL_DXIL_OUTPUTS})
    add_dependencies(${consumer_target} tubelight_shaders)

    # Copy the .dxil dir next to the consumer binary so runtime load can
    # find them at $exe_dir/shaders/dxil/*.dxil. We copy on every build
    # — ${TL_DXIL_OUTPUTS} are inputs so the step skips when nothing
    # changed (CMake stamp file).
    add_custom_command(
        TARGET ${consumer_target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory
                "$<TARGET_FILE_DIR:${consumer_target}>/shaders/dxil"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                ${TL_DXIL_OUTPUTS}
                "$<TARGET_FILE_DIR:${consumer_target}>/shaders/dxil/"
        COMMENT "[shaders] copy 9 DXIL bytecodes next to ${consumer_target}"
        VERBATIM
    )

    # Surface the runtime DXIL dir to C++ via a compile definition so the
    # D3D12 backend can locate them at any working directory. tubelight_core
    # is where backend_d3d12.cpp actually compiles, so the def must reach
    # IT — applying to the consumer target alone leaves the library blind.
    set(_dxil_dir "$<TARGET_FILE_DIR:${consumer_target}>/shaders/dxil")
    target_compile_definitions(${consumer_target} PUBLIC
        TUBELIGHT_DXIL_DIR="${_dxil_dir}"
    )
    if(TARGET tubelight_core)
        target_compile_definitions(tubelight_core PUBLIC
            TUBELIGHT_DXIL_DIR="${_dxil_dir}"
        )
    endif()
endfunction()
