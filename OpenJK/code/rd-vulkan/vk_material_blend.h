/*
Copyright (C) 2026 JKXRL contributors
SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef JKXR_VK_MATERIAL_BLEND_H
#define JKXR_VK_MATERIAL_BLEND_H

#include <vulkan/vulkan.h>

enum vk_blend_mode_t
{
	VK_BLEND_ALPHA,
	VK_BLEND_OPAQUE,
	VK_BLEND_ADDITIVE,
	VK_BLEND_SOURCE_ALPHA_ADDITIVE,
	VK_BLEND_INVERSE_SOURCE_ALPHA_ADDITIVE,
	VK_BLEND_ONE_SOURCE_ALPHA,
	VK_BLEND_DESTINATION_COLOR_ADDITIVE,
	VK_BLEND_ONE_MINUS_DESTINATION_ALPHA_ADDITIVE,
	VK_BLEND_MODULATE,
	VK_BLEND_DOUBLE_MODULATE,
	VK_BLEND_INVERSE_SOURCE_COLOR_MODULATE,
	VK_BLEND_SCREEN,
	VK_BLEND_ONE_SOURCE_COLOR,
	VK_BLEND_INVERSE_ALPHA,
	VK_BLEND_INVERSE_ALPHA_BOTH,
	VK_BLEND_COUNT,
};

enum vk_material_cull_t
{
	VK_MATERIAL_FRONT_SIDED,
	VK_MATERIAL_BACK_SIDED,
	VK_MATERIAL_TWO_SIDED,
};

inline VkCullModeFlags VK_BlendedMD3CullMode( vk_material_cull_t cull, bool lateMD3 )
{
	if ( !lateMD3 || cull == VK_MATERIAL_TWO_SIDED ) return VK_CULL_MODE_NONE;
	// Legacy MD3 winding is retained, with the world projection's Y inversion.
	return cull == VK_MATERIAL_BACK_SIDED ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_FRONT_BIT;
}

inline bool VK_DepthAlphaGLMCull( bool glm, bool alphaWave,
	vk_blend_mode_t firstBlend, bool firstDepthWrite, bool hasOpaqueStage )
{
	return glm && alphaWave && firstBlend == VK_BLEND_ALPHA &&
		firstDepthWrite && !hasOpaqueStage;
}

enum vk_model_composite_phase_t
{
	VK_MODEL_COMPOSITE_ALL,
	VK_MODEL_COMPOSITE_SOLID,
	VK_MODEL_COMPOSITE_BLENDED,
};

inline bool VK_ModelSurfaceIsLate(bool opaque, bool writesDepth, bool blended)
{
	return blended && !opaque && !writesDepth;
}

inline bool VK_ModelCompositePhaseIncludes(
	vk_model_composite_phase_t phase, bool fullyBlended )
{
	return phase == VK_MODEL_COMPOSITE_ALL ||
		( ( phase == VK_MODEL_COMPOSITE_BLENDED ) == fullyBlended );
}

// Shared by pipeline creation and the material-compositing regression tests.
inline VkPipelineColorBlendAttachmentState VK_MaterialBlendAttachment( vk_blend_mode_t blendMode )
{
	VkPipelineColorBlendAttachmentState colorAttachment = {};
	colorAttachment.colorWriteMask =
		VK_COLOR_COMPONENT_R_BIT |
		VK_COLOR_COMPONENT_G_BIT |
		VK_COLOR_COMPONENT_B_BIT |
		VK_COLOR_COMPONENT_A_BIT;
	colorAttachment.blendEnable = blendMode == VK_BLEND_OPAQUE ? VK_FALSE : VK_TRUE;
	colorAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	colorAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	colorAttachment.colorBlendOp = VK_BLEND_OP_ADD;
	colorAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	colorAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	colorAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
	if ( blendMode == VK_BLEND_INVERSE_ALPHA || blendMode == VK_BLEND_INVERSE_ALPHA_BOTH )
	{
		colorAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		colorAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		colorAttachment.dstColorBlendFactor = blendMode == VK_BLEND_INVERSE_ALPHA
			? VK_BLEND_FACTOR_SRC_ALPHA : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		colorAttachment.dstAlphaBlendFactor = colorAttachment.dstColorBlendFactor;
	}
	else if ( blendMode == VK_BLEND_ADDITIVE )
	{
		colorAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		colorAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
		colorAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		colorAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	}
	else if ( blendMode == VK_BLEND_SOURCE_ALPHA_ADDITIVE )
	{
		colorAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		colorAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
		colorAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		colorAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	}
	else if ( blendMode == VK_BLEND_INVERSE_SOURCE_ALPHA_ADDITIVE )
	{
		colorAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		colorAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
		colorAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		colorAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	}
	else if ( blendMode == VK_BLEND_DESTINATION_COLOR_ADDITIVE )
	{
		colorAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
		colorAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
		colorAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
		colorAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	}
	else if ( blendMode == VK_BLEND_ONE_SOURCE_COLOR )
	{
		colorAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		colorAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
		colorAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		colorAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	}
	else if ( blendMode == VK_BLEND_ONE_SOURCE_ALPHA )
	{
		colorAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		colorAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		colorAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		colorAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	}
	else if ( blendMode == VK_BLEND_ONE_MINUS_DESTINATION_ALPHA_ADDITIVE )
	{
		colorAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
		colorAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
		colorAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
		colorAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
	}
	else if ( blendMode == VK_BLEND_MODULATE )
	{
		colorAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
		colorAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
		colorAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
		colorAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
	}
	else if ( blendMode == VK_BLEND_DOUBLE_MODULATE )
	{
		colorAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
		colorAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
		colorAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
		colorAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	}
	else if ( blendMode == VK_BLEND_INVERSE_SOURCE_COLOR_MODULATE )
	{
		colorAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
		colorAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
		colorAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
		colorAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	}
	else if ( blendMode == VK_BLEND_SCREEN )
	{
		colorAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		colorAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
		colorAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
		colorAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	}
	return colorAttachment;
}

#endif
