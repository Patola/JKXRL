#============================================================================
# Copyright (C) 2026 JKXRL contributors
#
# This file is part of the OpenJK source code.
#
# OpenJK is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#============================================================================

if(NOT InOpenJK)
	message(FATAL_ERROR "Use the top-level cmake script!")
endif(NOT InOpenJK)

if(BuildSPRdVulkan OR BuildJK2SPRdVulkan)
	find_package(Vulkan REQUIRED)
	find_library(OPENXR_LOADER_LIBRARY NAMES openxr_loader REQUIRED)

	set(SPRDVulkanShaderHeaders
		"${CMAKE_CURRENT_BINARY_DIR}/test_pattern.vert.inc"
		"${CMAKE_CURRENT_BINARY_DIR}/test_pattern.frag.inc"
		"${CMAKE_CURRENT_BINARY_DIR}/rect.vert.inc"
		"${CMAKE_CURRENT_BINARY_DIR}/rect.frag.inc"
		"${CMAKE_CURRENT_BINARY_DIR}/textured_rect.vert.inc"
		"${CMAKE_CURRENT_BINARY_DIR}/textured_rect.frag.inc"
		"${CMAKE_CURRENT_BINARY_DIR}/force_speed_motion_blur.vert.inc"
		"${CMAKE_CURRENT_BINARY_DIR}/force_speed_motion_blur.frag.inc"
		"${CMAKE_CURRENT_BINARY_DIR}/diagnostic3d.vert.inc"
		"${CMAKE_CURRENT_BINARY_DIR}/diagnostic3d.frag.inc"
		"${CMAKE_CURRENT_BINARY_DIR}/world.vert.inc"
		"${CMAKE_CURRENT_BINARY_DIR}/world.frag.inc"
		"${CMAKE_CURRENT_BINARY_DIR}/glm_skin.comp.inc"
		)

	add_custom_command(
		OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/test_pattern.vert.inc"
		COMMAND "${Vulkan_GLSLC_EXECUTABLE}" --target-env=vulkan1.3 -fshader-stage=vert -mfmt=c
			-o "${CMAKE_CURRENT_BINARY_DIR}/test_pattern.vert.inc"
			"${SPDir}/rd-vulkan/shaders/test_pattern.vert"
		DEPENDS "${SPDir}/rd-vulkan/shaders/test_pattern.vert"
		VERBATIM)

	add_custom_command(
		OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/test_pattern.frag.inc"
		COMMAND "${Vulkan_GLSLC_EXECUTABLE}" --target-env=vulkan1.3 -fshader-stage=frag -mfmt=c
			-o "${CMAKE_CURRENT_BINARY_DIR}/test_pattern.frag.inc"
			"${SPDir}/rd-vulkan/shaders/test_pattern.frag"
		DEPENDS "${SPDir}/rd-vulkan/shaders/test_pattern.frag"
		VERBATIM)

	add_custom_command(
		OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rect.vert.inc"
		COMMAND "${Vulkan_GLSLC_EXECUTABLE}" --target-env=vulkan1.3 -fshader-stage=vert -mfmt=c
			-o "${CMAKE_CURRENT_BINARY_DIR}/rect.vert.inc"
			"${SPDir}/rd-vulkan/shaders/rect.vert"
		DEPENDS "${SPDir}/rd-vulkan/shaders/rect.vert"
		VERBATIM)

	add_custom_command(
		OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/rect.frag.inc"
		COMMAND "${Vulkan_GLSLC_EXECUTABLE}" --target-env=vulkan1.3 -fshader-stage=frag -mfmt=c
			-o "${CMAKE_CURRENT_BINARY_DIR}/rect.frag.inc"
			"${SPDir}/rd-vulkan/shaders/rect.frag"
		DEPENDS "${SPDir}/rd-vulkan/shaders/rect.frag"
		VERBATIM)

	add_custom_command(
		OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/textured_rect.vert.inc"
		COMMAND "${Vulkan_GLSLC_EXECUTABLE}" --target-env=vulkan1.3 -fshader-stage=vert -mfmt=c
			-o "${CMAKE_CURRENT_BINARY_DIR}/textured_rect.vert.inc"
			"${SPDir}/rd-vulkan/shaders/textured_rect.vert"
		DEPENDS "${SPDir}/rd-vulkan/shaders/textured_rect.vert"
		VERBATIM)

	add_custom_command(
		OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/textured_rect.frag.inc"
		COMMAND "${Vulkan_GLSLC_EXECUTABLE}" --target-env=vulkan1.3 -fshader-stage=frag -mfmt=c
			-o "${CMAKE_CURRENT_BINARY_DIR}/textured_rect.frag.inc"
			"${SPDir}/rd-vulkan/shaders/textured_rect.frag"
		DEPENDS "${SPDir}/rd-vulkan/shaders/textured_rect.frag"
		VERBATIM)

	add_custom_command(
		OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/force_speed_motion_blur.vert.inc"
		COMMAND "${Vulkan_GLSLC_EXECUTABLE}" --target-env=vulkan1.3 -fshader-stage=vert -mfmt=c
			-o "${CMAKE_CURRENT_BINARY_DIR}/force_speed_motion_blur.vert.inc"
			"${SPDir}/rd-vulkan/shaders/force_speed_motion_blur.vert"
		DEPENDS "${SPDir}/rd-vulkan/shaders/force_speed_motion_blur.vert"
		VERBATIM)

	add_custom_command(
		OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/force_speed_motion_blur.frag.inc"
		COMMAND "${Vulkan_GLSLC_EXECUTABLE}" --target-env=vulkan1.3 -fshader-stage=frag -mfmt=c
			-o "${CMAKE_CURRENT_BINARY_DIR}/force_speed_motion_blur.frag.inc"
			"${SPDir}/rd-vulkan/shaders/force_speed_motion_blur.frag"
		DEPENDS "${SPDir}/rd-vulkan/shaders/force_speed_motion_blur.frag"
		VERBATIM)

	add_custom_command(
		OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/diagnostic3d.vert.inc"
		COMMAND "${Vulkan_GLSLC_EXECUTABLE}" --target-env=vulkan1.3 -fshader-stage=vert -mfmt=c
			-o "${CMAKE_CURRENT_BINARY_DIR}/diagnostic3d.vert.inc"
			"${SPDir}/rd-vulkan/shaders/diagnostic3d.vert"
		DEPENDS "${SPDir}/rd-vulkan/shaders/diagnostic3d.vert"
		VERBATIM)

	add_custom_command(
		OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/diagnostic3d.frag.inc"
		COMMAND "${Vulkan_GLSLC_EXECUTABLE}" --target-env=vulkan1.3 -fshader-stage=frag -mfmt=c
			-o "${CMAKE_CURRENT_BINARY_DIR}/diagnostic3d.frag.inc"
			"${SPDir}/rd-vulkan/shaders/diagnostic3d.frag"
		DEPENDS "${SPDir}/rd-vulkan/shaders/diagnostic3d.frag"
		VERBATIM)

	add_custom_command(
		OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/world.vert.inc"
		COMMAND "${Vulkan_GLSLC_EXECUTABLE}" --target-env=vulkan1.3 -fshader-stage=vert -mfmt=c
			-o "${CMAKE_CURRENT_BINARY_DIR}/world.vert.inc"
			"${SPDir}/rd-vulkan/shaders/world.vert"
		DEPENDS "${SPDir}/rd-vulkan/shaders/world.vert"
		VERBATIM)

	add_custom_command(
		OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/world.frag.inc"
		COMMAND "${Vulkan_GLSLC_EXECUTABLE}" --target-env=vulkan1.3 -fshader-stage=frag -mfmt=c
			-o "${CMAKE_CURRENT_BINARY_DIR}/world.frag.inc"
			"${SPDir}/rd-vulkan/shaders/world.frag"
		DEPENDS "${SPDir}/rd-vulkan/shaders/world.frag"
		VERBATIM)

	add_custom_command(
		OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/glm_skin.comp.inc"
		COMMAND "${Vulkan_GLSLC_EXECUTABLE}" --target-env=vulkan1.3 -fshader-stage=comp -mfmt=c
			-o "${CMAKE_CURRENT_BINARY_DIR}/glm_skin.comp.inc"
			"${SPDir}/rd-vulkan/shaders/glm_skin.comp"
		DEPENDS "${SPDir}/rd-vulkan/shaders/glm_skin.comp"
		VERBATIM)

	add_custom_target(rdvulkan_shaders DEPENDS ${SPRDVulkanShaderHeaders})

	set(SPRDVulkanDefines ${SharedDefines} "RENDERER" "_JK2EXE")

	set(SPRDVulkanFiles
		"${SPDir}/rd-vulkan/tr_init.cpp"
		"${SPDir}/rd-vulkan/tr_local.h"
		"${SPDir}/rd-vulkan/vk_backend.cpp"
		"${SPDir}/rd-vulkan/vk_backend.h"
		"${SPDir}/rd-vulkan/vk_ghoul2.cpp"
		"${SPDir}/rd-vulkan/vk_ghoul2.h"
		"${SPDir}/rd-vulkan/shaders/test_pattern.vert"
		"${SPDir}/rd-vulkan/shaders/test_pattern.frag"
		"${SPDir}/rd-vulkan/shaders/rect.vert"
		"${SPDir}/rd-vulkan/shaders/rect.frag"
		"${SPDir}/rd-vulkan/shaders/textured_rect.vert"
		"${SPDir}/rd-vulkan/shaders/textured_rect.frag"
		"${SPDir}/rd-vulkan/shaders/force_speed_motion_blur.vert"
		"${SPDir}/rd-vulkan/shaders/force_speed_motion_blur.frag"
		"${SPDir}/rd-vulkan/shaders/diagnostic3d.vert"
		"${SPDir}/rd-vulkan/shaders/diagnostic3d.frag"
		"${SPDir}/rd-vulkan/shaders/world.vert"
		"${SPDir}/rd-vulkan/shaders/world.frag"
		"${SPDir}/rd-vulkan/shaders/glm_skin.comp"
		${SPRDVulkanShaderHeaders}
		)

	set(SPRDVulkanRdCommonFiles
		"${SPDir}/rd-common/tr_public.h"
		"${SPDir}/rd-common/tr_types.h"
		"${SPDir}/rd-common/tr_common.h"
		"${SPDir}/rd-common/tr_font.cpp"
		"${SPDir}/rd-common/tr_font.h"
		"${SPDir}/rd-common/tr_image_load.cpp"
		"${SPDir}/rd-common/tr_image_jpg.cpp"
		"${SPDir}/rd-common/tr_image_png.cpp"
		"${SPDir}/rd-common/tr_image_tga.cpp"
		)

	set(SPRDVulkanCommonFiles
		"${SPDir}/qcommon/matcomp.cpp"
		"${SPDir}/qcommon/q_shared.cpp"
		"${SPDir}/qcommon/q_shared.h"
		${SharedCommonFiles}
		)

	list(APPEND SPRDVulkanFiles ${SPRDVulkanCommonFiles})

	set(SPRDVulkanRendererIncludeDirectories
		${SharedDir}
		${SPDir}
		"${SPDir}/rd-vulkan"
		"${SPDir}/rd-common"
		"${JKXRDir}"
		"${GSLIncludeDirectory}"
		"${OpenJKLibDir}"
		"${OpenXRLibDir}"
		"${CMAKE_CURRENT_BINARY_DIR}"
		${Vulkan_INCLUDE_DIRS}
		${JPEG_INCLUDE_DIR}
		${PNG_INCLUDE_DIRS}
		${ZLIB_INCLUDE_DIR}
		)

	set(SPRDVulkanRendererLibraries
		${Vulkan_LIBRARIES}
		${OPENXR_LOADER_LIBRARY}
		${JPEG_LIBRARIES}
		${PNG_LIBRARIES}
		${ZLIB_LIBRARIES}
		)

	function(add_sp_vulkan_project ProjectName Label EngineName InstallDir Component)
		add_library(${ProjectName} SHARED ${SPRDVulkanFiles} ${SPRDVulkanRdCommonFiles})
		add_dependencies(${ProjectName} rdvulkan_shaders)
		set_target_properties(${ProjectName} PROPERTIES PREFIX "")

		if(MakeApplicationBundles AND (BuildSPEngine OR BuildJK2SPEngine))
			install(TARGETS ${ProjectName}
				LIBRARY
				DESTINATION "${InstallDir}/${EngineName}.app/Contents/MacOS/"
				COMPONENT ${Component})
		else()
			install(TARGETS ${ProjectName}
				LIBRARY
				DESTINATION ${InstallDir}
				COMPONENT ${Component})
		endif()

		set_target_properties(${ProjectName} PROPERTIES COMPILE_DEFINITIONS "${SPRDVulkanDefines}")
		set_property(TARGET ${ProjectName} APPEND PROPERTY COMPILE_OPTIONS ${OPENJK_VISIBILITY_FLAGS})
		set_target_properties(${ProjectName} PROPERTIES INCLUDE_DIRECTORIES "${SPRDVulkanRendererIncludeDirectories}")
		set_target_properties(${ProjectName} PROPERTIES PROJECT_LABEL ${Label})
		target_link_libraries(${ProjectName} ${SPRDVulkanRendererLibraries})
		add_custom_command(TARGET ${ProjectName} POST_BUILD
			COMMAND "${CMAKE_COMMAND}" -E copy_if_different
				"$<TARGET_FILE:${ProjectName}>"
				"${CMAKE_BINARY_DIR}/$<TARGET_FILE_NAME:${ProjectName}>"
			COMMENT "Copying ${ProjectName} to executable load directory"
			VERBATIM)
	endfunction(add_sp_vulkan_project)

	if(BuildSPRdVulkan)
		add_sp_vulkan_project(${SPRDVulkanRenderer} "SP Vulkan Renderer" ${SPEngine} ${JKAInstallDir} ${JKASPClientComponent})
	endif(BuildSPRdVulkan)

	if(BuildJK2SPRdVulkan)
		set(SPRDVulkanDefines ${SPRDVulkanDefines} "JK2_MODE")
		add_sp_vulkan_project(${JK2SPVulkanRenderer} "JK2 SP Vulkan Renderer" ${JK2SPEngine} ${JK2InstallDir} ${JK2SPClientComponent})
	endif(BuildJK2SPRdVulkan)
endif(BuildSPRdVulkan OR BuildJK2SPRdVulkan)
