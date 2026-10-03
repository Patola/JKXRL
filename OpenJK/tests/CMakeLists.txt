#============================================================================
# Copyright (C) 2013 - 2018, OpenJK contributors
#
# This file is part of the OpenJK source code.
#
# OpenJK is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <http://www.gnu.org/licenses/>.
#============================================================================

# Make sure the user is not executing this script directly
if(NOT InOpenJK)
	message(FATAL_ERROR "Use the top-level cmake script!")
endif(NOT InOpenJK)

set(TestFiles
	"main.cpp"
	"safe/string.cpp"
	"safe/limited_vector.cpp"
	"vegetation.cpp"
	"weather.cpp"
	"projected_marks.cpp"
	"texture_transform.cpp"
	"view_fog.cpp"
	"local_fog.cpp"
	"electricity.cpp"
	"waveform.cpp"
	"deform.cpp"
	"billboard.cpp"
	"flare.cpp"
	"renderer_queries.cpp"
	"shader_animation.cpp"
	"ui_list_selection.cpp"
	"md3_animation.cpp"
	"material_blend.cpp"
	"lightmap_styles.cpp"
	"rock_boundary.cpp"
	"quest_color.cpp"
	"console_projection.cpp"
	"security_camera.cpp"
	"vr_jump_input.cpp"
	"vr_trigger_hints.cpp"
	"vr_text_placement.cpp"
	"vr_walker_view.cpp"
	"vr_mounted_aim.cpp"
	"vr_throw_gesture.cpp"
	"vr_force_cast.cpp"
	"${SharedDir}/qcommon/safe/string.cpp"
	"${SharedDir}/qcommon/q_math.c"
	)
source_group( "tests" REGULAR_EXPRESSION ".*")
source_group( "tests\\safe" REGULAR_EXPRESSION "safe/.*" )
source_group( "qcommon\\safe" REGULAR_EXPRESSION "${SharedDir}/qcommon/safe/.*" )

find_package( Boost COMPONENTS unit_test_framework REQUIRED )
find_package( Vulkan REQUIRED )

set(TestTarget "UnitTests")
set(TestLibraries "${Boost_UNIT_TEST_FRAMEWORK_LIBRARY}")
set(TestIncludeDirectories
	"${Boost_INCLUDE_DIRS}"
	${Vulkan_INCLUDE_DIRS}
	"${SharedDir}"
	"${GSLIncludeDirectory}"
	)
set(TestDefines "${SharedDefines}")

add_executable(${TestTarget} ${TestFiles})
set_target_properties(${TestTarget} PROPERTIES COMPILE_DEFINITIONS "${TestDefines}")
set_target_properties(${TestTarget} PROPERTIES INCLUDE_DIRECTORIES "${TestIncludeDirectories}")
set_target_properties(${TestTarget} PROPERTIES PROJECT_LABEL "Unit Tests")
target_link_libraries(${TestTarget} ${TestLibraries})
install(TARGETS ${TestTarget} DESTINATION ".")

#Generate a main function
target_compile_definitions(${TestTarget} PRIVATE BOOST_TEST_DYN_LINK)

add_test(NAME unittests COMMAND ${TestTarget})

add_executable(SavePreviewTests save_preview.cpp "${SPDir}/rd-vulkan/vk_save_preview.cpp")
target_include_directories(SavePreviewTests PRIVATE ${JPEG_INCLUDE_DIR})
target_link_libraries(SavePreviewTests PRIVATE ${JPEG_LIBRARIES})
add_test(NAME save-preview COMMAND SavePreviewTests)

# Keep the two games' GNPC layouts in separate executables.
foreach(Game IN ITEMS jka jko)
    add_executable(NpcSaveTests_${Game} npc_save.cpp)
    target_include_directories(NpcSaveTests_${Game} PRIVATE
        "${SharedDir}" "${GSLIncludeDirectory}" "${CMAKE_SOURCE_DIR}/code")
    target_compile_definitions(NpcSaveTests_${Game} PRIVATE ${SharedDefines} SP_GAME)
    if(Game STREQUAL "jko")
        target_compile_definitions(NpcSaveTests_${Game} PRIVATE JK2_MODE)
        target_include_directories(NpcSaveTests_${Game} PRIVATE "${CMAKE_SOURCE_DIR}/codeJK2/game")
    endif()
    add_test(NAME npc-save-${Game} COMMAND NpcSaveTests_${Game})
endforeach()

# Optional real-driver test: no window, OpenXR runtime, or proprietary assets.
option(BuildVulkanGpuTests "Build headless Vulkan driver tests" OFF)
if(BuildVulkanGpuTests)
	if(NOT Vulkan_GLSLC_EXECUTABLE)
		message(FATAL_ERROR "Vulkan GPU tests require glslc")
	endif()
	set(LightmapVertex "${CMAKE_BINARY_DIR}/lightmap-test.vert.spv")
	set(LightmapFragment "${CMAKE_BINARY_DIR}/lightmap-test.frag.spv")
	add_custom_command(OUTPUT "${LightmapVertex}" "${LightmapFragment}"
		COMMAND "${Vulkan_GLSLC_EXECUTABLE}" --target-env=vulkan1.3 -o "${LightmapVertex}"
			"${CMAKE_CURRENT_SOURCE_DIR}/shaders/lightmap_probe.vert"
		COMMAND "${Vulkan_GLSLC_EXECUTABLE}" --target-env=vulkan1.3 -o "${LightmapFragment}"
			"${CMAKE_SOURCE_DIR}/code/rd-vulkan/shaders/world.frag"
		DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/lightmap_probe.vert"
			"${CMAKE_SOURCE_DIR}/code/rd-vulkan/shaders/world.frag"
			"${CMAKE_SOURCE_DIR}/code/rd-vulkan/shaders/local_fog.glsl" VERBATIM)
	add_executable(LightmapGpuTests "${CMAKE_SOURCE_DIR}/../tools/check_lightmap_gpu.cpp"
		"${LightmapVertex}" "${LightmapFragment}")
	target_link_libraries(LightmapGpuTests PRIVATE Vulkan::Vulkan)
	add_test(NAME vulkan-lightmap-raster COMMAND LightmapGpuTests "${LightmapVertex}" "${LightmapFragment}")
	set_tests_properties(vulkan-lightmap-raster PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
	set(DeformProbe "${CMAKE_BINARY_DIR}/deform-probe.comp.spv")
	add_custom_command(OUTPUT "${DeformProbe}"
		COMMAND "${Vulkan_GLSLC_EXECUTABLE}" --target-env=vulkan1.3 -o "${DeformProbe}"
			"${CMAKE_CURRENT_SOURCE_DIR}/shaders/deform_probe.comp"
		DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/deform_probe.comp"
			"${CMAKE_SOURCE_DIR}/code/rd-vulkan/shaders/deform.glsl"
			"${CMAKE_SOURCE_DIR}/code/rd-vulkan/shaders/specular.glsl" VERBATIM)
	add_executable(DeformGpuTests "${CMAKE_SOURCE_DIR}/../tools/check_deform_gpu.cpp" "${DeformProbe}")
	target_link_libraries(DeformGpuTests PRIVATE Vulkan::Vulkan)
	add_test(NAME vulkan-deformation COMMAND DeformGpuTests "${DeformProbe}")
	set_tests_properties(vulkan-deformation PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    set(ConsoleVertex "${CMAKE_BINARY_DIR}/console-test.vert.spv")
    set(ConsoleFragment "${CMAKE_BINARY_DIR}/console-test.frag.spv")
    add_custom_command(OUTPUT "${ConsoleVertex}" "${ConsoleFragment}"
        COMMAND "${Vulkan_GLSLC_EXECUTABLE}" --target-env=vulkan1.3
            -o "${ConsoleVertex}" "${CMAKE_SOURCE_DIR}/code/rd-vulkan/shaders/textured_rect.vert"
        COMMAND "${Vulkan_GLSLC_EXECUTABLE}" --target-env=vulkan1.3
            -o "${ConsoleFragment}" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/console_probe.frag"
        DEPENDS "${CMAKE_SOURCE_DIR}/code/rd-vulkan/shaders/textured_rect.vert"
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders/console_probe.frag"
        VERBATIM)
    add_executable(ConsoleGpuTests "${CMAKE_SOURCE_DIR}/../tools/check_console_gpu.cpp"
        "${ConsoleVertex}" "${ConsoleFragment}")
    target_link_libraries(ConsoleGpuTests PRIVATE Vulkan::Vulkan)
    add_test(NAME vulkan-console-raster COMMAND ConsoleGpuTests "${ConsoleVertex}" "${ConsoleFragment}")
    set_tests_properties(vulkan-console-raster PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    add_executable(Md3UploadTests "${CMAKE_SOURCE_DIR}/../tools/check_md3_upload.cpp")
    target_link_libraries(Md3UploadTests PRIVATE Vulkan::Vulkan)
    add_test(NAME vulkan-md3-upload COMMAND Md3UploadTests)
    set_tests_properties(vulkan-md3-upload PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
endif()

find_program(PYTHON3_EXECUTABLE python3)
if(PYTHON3_EXECUTABLE AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME openxr-startup
        COMMAND "${PYTHON3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/../tools/check_xr_startup.py")
    add_test(NAME release-identity
        COMMAND "${PYTHON3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/../tools/check_release_identity.py")
    add_test(NAME vegetation-fog-contracts
        COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/check_vegetation_fog_contracts.py)
    add_test(NAME vertex-lighting-contracts
        COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/check_vertex_lighting_contracts.py)
    add_test(NAME save-preview-contracts
        COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/check_save_preview_contracts.py)
    add_test(NAME lightmap-contracts
        COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/check_lightmap_contracts.py)
    add_test(NAME specular-contracts
        COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/check_specular_contracts.py)
    add_test(NAME glass-contracts
        COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/check_glass_contracts.py)
    if(TARGET LightmapGpuTests)
        add_test(NAME vulkan-rock-seam-raster
            COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/audit_rock_seam_raster.py
                --build ${CMAKE_BINARY_DIR})
        set_tests_properties(vulkan-rock-seam-raster PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    endif()
    add_test(NAME vulkan-upload-boundary
        COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/check_vulkan_upload_boundary.py)
    add_test(NAME vr-input-contracts
        COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/check_vr_input_contracts.py)
    add_test(NAME teleport-view
        COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/check_teleport_view.py)
    add_test(NAME weather-contracts
        COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/check_weather_contracts.py)
    add_test(NAME mark-contracts
        COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/check_mark_contracts.py)
    add_test(NAME view-fog-contracts
        COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/check_view_fog_contracts.py)
    add_test(NAME electricity-contracts
        COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/check_electricity_contracts.py)
    add_test(NAME alpha-wave-contracts
        COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/check_alpha_wave_contracts.py)
    add_test(NAME deform-contracts
        COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/check_deform_contracts.py)
    add_test(NAME console-click-asset
        COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/generate_console_key_click.py --check)
    add_test(NAME cvar-reference-scanner
        COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/cvar_reference.py --self-test)
    add_test(NAME cvar-reference-current
        COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/cvar_reference.py --check)
    add_test(NAME performance-capture-cleanup
        COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/capture_vr_performance.py --self-test)
    add_test(NAME linux-install-contracts
        COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/check_linux_runtime.py)
    if(BuildSPEngine AND BuildJK2SPEngine AND BuildSPGame AND BuildJK2SPGame
        AND BuildSPRdVulkan AND BuildJK2SPRdVulkan AND Architecture STREQUAL "x86_64")
        add_test(NAME linux-runtime-dependencies
            COMMAND ${PYTHON3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/../tools/check_linux_runtime.py ${CMAKE_BINARY_DIR})
    endif()
endif()
