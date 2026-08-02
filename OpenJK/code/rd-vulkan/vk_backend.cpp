/*
===========================================================================
Copyright (C) 2026 JKXRL contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.
===========================================================================
*/

#include "tr_local.h"
#include "vk_backend.h"
#include "vk_ghoul2.h"
#include "../qcommon/matcomp.h"

#define XR_USE_GRAPHICS_API_VULKAN
#include <vulkan/vulkan.h>
#include <openxr.h>
#include <openxr_platform.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <deque>
#include <functional>
#include <limits>
#include <memory>
#include <numeric>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

enum { VK_BACKEND_EYE_COUNT = 2 };

enum vk_blend_mode_t
{
	VK_BLEND_ALPHA,
	VK_BLEND_OPAQUE,
	VK_BLEND_ADDITIVE,
	VK_BLEND_SOURCE_ALPHA_ADDITIVE,
	VK_BLEND_ONE_SOURCE_ALPHA,
	VK_BLEND_DESTINATION_COLOR_ADDITIVE,
	VK_BLEND_ONE_MINUS_DESTINATION_ALPHA_ADDITIVE,
	VK_BLEND_MODULATE,
	VK_BLEND_DOUBLE_MODULATE,
};

enum vk_alpha_test_t
{
	VK_ALPHA_TEST_NONE,
	VK_ALPHA_TEST_GREATER_ZERO,
	VK_ALPHA_TEST_LESS_HALF,
	VK_ALPHA_TEST_GREATER_EQUAL_HALF,
	VK_ALPHA_TEST_GREATER_EQUAL_THREE_QUARTER,
};

enum vk_surface_sprite_type_t
{
	VK_SURFACE_SPRITE_NONE,
	VK_SURFACE_SPRITE_VERTICAL,
	VK_SURFACE_SPRITE_ORIENTED,
	VK_SURFACE_SPRITE_EFFECT,
	VK_SURFACE_SPRITE_FLATTENED,
};

enum vk_surface_sprite_facing_t
{
	VK_SURFACE_SPRITE_FACING_NORMAL,
	VK_SURFACE_SPRITE_FACING_UP,
	VK_SURFACE_SPRITE_FACING_DOWN,
	VK_SURFACE_SPRITE_FACING_ANY,
};

struct vk_surface_sprite_config_t
{
	vk_surface_sprite_type_t type;
	vk_surface_sprite_facing_t facing;
	float width;
	float height;
	float density;
	float fadeDist;
	float fadeMax;
	float fadeScale;
	float variance[2];
	float wind;
	float windIdle;
	float vertSkew;
	float fxDuration;
	float fxGrow[2];
	float fxAlphaStart;
	float fxAlphaEnd;
	bool weatherAffected;
};

enum vk_waveform_t
{
	VK_WAVE_NONE,
	VK_WAVE_SIN,
	VK_WAVE_TRIANGLE,
	VK_WAVE_SQUARE,
	VK_WAVE_SAWTOOTH,
	VK_WAVE_INVERSE_SAWTOOTH,
};

static const uint32_t testPatternVertSpv[] =
#include "test_pattern.vert.inc"
;

static const uint32_t testPatternFragSpv[] =
#include "test_pattern.frag.inc"
;

static const uint32_t rectVertSpv[] =
#include "rect.vert.inc"
;

static const uint32_t rectFragSpv[] =
#include "rect.frag.inc"
;

static const uint32_t texturedRectVertSpv[] =
#include "textured_rect.vert.inc"
;

static const uint32_t texturedRectFragSpv[] =
#include "textured_rect.frag.inc"
;

static const uint32_t diagnostic3dVertSpv[] =
#include "diagnostic3d.vert.inc"
;

static const uint32_t diagnostic3dFragSpv[] =
#include "diagnostic3d.frag.inc"
;

static const uint32_t worldVertSpv[] =
#include "world.vert.inc"
;

static const uint32_t worldFragSpv[] =
#include "world.frag.inc"
;

struct vk_rect_t
{
	float rect[4];
	float uv[4];
	float color[4];
	qhandle_t texture;
	vk_blend_mode_t blendMode;
};

struct vk_texture_t
{
	VkImage image;
	VkDeviceMemory memory;
	VkImageView view;
	VkDescriptorSet descriptorSet;
	VkDescriptorSet repeatDescriptorSet;
	uint32_t width;
	uint32_t height;
};

struct vk_texture_name_t
{
	std::string name;
	qhandle_t handle;
};

struct vk_material_stage_t
{
	qhandle_t texture;
	vk_blend_mode_t blendMode;
	vk_alpha_test_t alphaTest;
	float alpha;
	float scroll[2];
	vk_waveform_t stretchType;
	float stretch[4];
	float turbulence[3];
	float color[4];
	bool lightmap;
	bool vertexColor;
	bool depthWrite;
	bool clampMap;
	vk_surface_sprite_config_t surfaceSprite;
};

struct vk_material_t
{
	std::vector<vk_material_stage_t> stages;
};

struct vk_shader_stage_definition_t
{
	std::string imageName;
	vk_blend_mode_t blendMode;
	vk_alpha_test_t alphaTest;
	float alpha;
	float scroll[2];
	vk_waveform_t stretchType;
	float stretch[4];
	float turbulence[3];
	float color[4];
	bool vertexColor;
	bool depthWrite;
	bool clampMap;
	vk_surface_sprite_config_t surfaceSprite;
};

struct vk_shader_definition_t
{
	std::string name;
	std::string skyOuterbox;
	float skyCloudHeight;
	float fogColor[3];
	float fogDepth;
	bool hasFog;
	std::vector<vk_shader_stage_definition_t> stages;
};

struct vk_world_vertex_t
{
	float position[3];
	float color[4];
	float uv[2];
	float lightmapUv[2];
	float normal[3];
};

struct vk_world_plane_t
{
	float normal[3];
	float dist;
};

struct vk_world_node_t
{
	int plane;
	int children[2];
};

struct vk_world_leaf_t
{
	int cluster;
	int area;
	int firstLeafSurface;
	int numLeafSurfaces;
};

struct vk_world_batch_t
{
	uint32_t firstIndex;
	uint32_t indexCount;
	qhandle_t shader;
	qhandle_t lightmap;
	bool vertexLit;
	uint32_t surfaceIndex;
};

struct vk_surface_sprite_instance_t
{
	float position[3];
	float normal[3];
	float color[4];
	float width;
	float height;
	float phase;
	uint32_t orientation;
};

struct vk_surface_sprite_batch_t
{
	vk_material_stage_t stage;
	uint32_t surfaceIndex;
	std::vector<vk_surface_sprite_instance_t> instances;
};

struct vk_surface_sprite_build_stats_t
{
	size_t candidateStages;
	size_t supportedStages;
	size_t unsupportedStages;
	size_t invalidStages;
	size_t unavailableTextures;
	size_t triangles;
	size_t invalidTriangles;
	size_t facingRejectedTriangles;
	size_t areaRejectedTriangles;
	size_t anchors;
	size_t stagesByType[5];
	size_t anchorsByType[5];
};

struct vk_weather_zone_t
{
	float mins[3];
	float maxs[3];
};

struct vk_world_inline_model_t
{
	uint32_t firstSurface;
	uint32_t surfaceCount;
};

struct vk_world_geometry_t
{
	VkBuffer vertexBuffer;
	VkDeviceMemory vertexMemory;
	VkBuffer indexBuffer;
	VkDeviceMemory indexMemory;
	uint32_t vertexCount;
	uint32_t indexCount;
	uint32_t surfaceCount;
	uint32_t texturedBatchCount;
	std::vector<vk_world_batch_t> batches;
	std::vector<vk_surface_sprite_batch_t> surfaceSpriteBatches;
	qhandle_t skyTextures[6];
	std::string skyName;
	bool hasSky;
	float globalFogColor[3];
	float globalFogDepth;
	bool hasGlobalFog;
	std::vector<uint32_t> surfaceBatchIndex;
	uint32_t bspSurfaceCount;
	std::vector<vk_world_inline_model_t> inlineModels;
	std::vector<vk_world_plane_t> planes;
	std::vector<vk_world_node_t> nodes;
	std::vector<vk_world_leaf_t> leafs;
	std::vector<uint32_t> leafSurfaces;
	std::vector<byte> visibility;
	uint32_t numClusters;
	uint32_t clusterBytes;
	std::vector<byte> visibleSurfaces;
	bool loggedVisibility;
};

struct vk_model_surface_t
{
	VkBuffer vertexBuffer;
	VkDeviceMemory vertexMemory;
	VkBuffer indexBuffer;
	VkDeviceMemory indexMemory;
	uint32_t vertexCount;
	uint32_t indexCount;
	qhandle_t shader;
	std::string name;
	int modelSurfaceIndex;
	int parentSurfaceIndex;
	unsigned int defaultFlags;
	std::vector<mdxmVertex_t> glmVertices;
	std::vector<int> glmBoneReferences;
	std::vector<vk_world_vertex_t> glmBaseVertices;
	std::vector<uint32_t> glmIndices;
};

struct vk_gla_bone_t
{
	std::string name;
	int parent;
	mdxaBone_t basePose;
	mdxaBone_t basePoseInverse;
};

struct vk_gla_t
{
	std::string name;
	std::vector<byte> data;
	std::vector<vk_gla_bone_t> bones;
	int numFrames;
	int ofsFrames;
	int ofsCompBonePool;
};

struct vk_world_stage_push_t
{
	float uvOffset[2];
	float alpha;
	float useLightmap;
	float color[4];
	float flags[4];
	float uvScale[2];
};

static_assert( sizeof( vk_world_stage_push_t ) == sizeof( float ) * 14,
	"world stage push constants must match the GLSL block at byte 64" );

enum vk_world_pass_t
{
	VK_WORLD_PASS_OPAQUE,
	VK_WORLD_PASS_TRANSLUCENT,
	VK_WORLD_PASS_FOG,
};

enum vk_model_type_t
{
	VK_MODEL_PLACEHOLDER,
	VK_MODEL_PENDING,
	VK_MODEL_INLINE_BSP,
	VK_MODEL_MD3,
	VK_MODEL_GLM,
	VK_MODEL_GLA,
	VK_MODEL_UNSUPPORTED,
};

struct vk_model_t
{
	std::string name;
	std::string animationName;
	vk_model_type_t type;
	int inlineModelIndex;
	int boneCount;
	qhandle_t animationHandle;
	std::vector<vk_model_surface_t> surfaces;
	std::shared_ptr<vk_gla_t> animation;
};

struct vk_skin_surface_t
{
	std::string name;
	qhandle_t shader;
	bool off;
};

struct vk_skin_t
{
	std::string name;
	std::vector<vk_skin_surface_t> surfaces;
};

struct vk_scene_submission_t
{
	refdef_t refdef;
	std::vector<refEntity_t> entities;
	size_t rectCountBefore;
};

struct vk_screen_scene_clip_t
{
	const CGhoul2Info_v *ghoul2;
	float x;
	float y;
	float width;
	float height;
};

struct vk_ghoul2_bone_cache_t
{
	const CGhoul2Info *ghoul;
	const vk_model_t *model;
	int time;
	mdxaBone_t rootMatrix;
	bool valid;
	std::vector<mdxaBone_t> bones;
};

struct vk_ghoul2_surface_cache_t
{
	const CGhoul2Info *ghoul;
	const vk_model_surface_t *surface;
	int time;
	VkDeviceSize vertexOffset;
};

struct vk_surface_sprite_stream_cache_t
{
	const vk_surface_sprite_batch_t *batch;
	VkDeviceSize vertexOffset;
	uint32_t vertexCount;
};

struct vk_ghoul2_skinned_audit_t
{
	const CGhoul2Info *ghoul;
	const vk_model_t *model;
	size_t surfaces;
	size_t vertices;
	size_t triangles;
	size_t nonFiniteVertices;
	size_t degenerateTriangles;
	size_t collapsedTriangles;
	size_t expandedTriangles;
};

struct vk_backend_state_t
{
	bool initialized;
	bool sessionRunning;
	bool exitRenderLoop;
	XrSessionState sessionState;

	XrInstance xrInstance;
	XrSystemId xrSystemId;
	XrSession xrSession;
	XrSpace viewSpace;
	XrSpace localSpace;
	XrSpace stageSpace;
	XrViewConfigurationView viewConfiguration[VK_BACKEND_EYE_COUNT];
	XrView views[VK_BACKEND_EYE_COUNT];
	XrFrameState frameState;
	bool frameBegun;
	bool viewsValid;
	XrSwapchain colorSwapchain[VK_BACKEND_EYE_COUNT];
	XrSwapchainImageVulkanKHR *colorImages[VK_BACKEND_EYE_COUNT];
	VkImageView *colorImageViews[VK_BACKEND_EYE_COUNT];
	VkFramebuffer *framebuffers[VK_BACKEND_EYE_COUNT];
	uint32_t colorImageCount[VK_BACKEND_EYE_COUNT];
	uint32_t colorImageIndex[VK_BACKEND_EYE_COUNT];
	int64_t colorFormat;
	VkFormat depthFormat;
	VkImage depthImages[VK_BACKEND_EYE_COUNT];
	VkDeviceMemory depthMemories[VK_BACKEND_EYE_COUNT];
	VkImageView depthImageViews[VK_BACKEND_EYE_COUNT];
	bool swapchainsCreated;
	bool renderResourcesCreated;

	PFN_xrCreateVulkanInstanceKHR xrCreateVulkanInstanceKHR;
	PFN_xrGetVulkanGraphicsDevice2KHR xrGetVulkanGraphicsDevice2KHR;
	PFN_xrCreateVulkanDeviceKHR xrCreateVulkanDeviceKHR;
	PFN_xrGetVulkanGraphicsRequirements2KHR xrGetVulkanGraphicsRequirements2KHR;

	VkInstance instance;
	VkPhysicalDevice physicalDevice;
	VkDevice device;
	VkQueue queue;
	VkCommandPool commandPool;
	VkCommandBuffer commandBuffer;
	VkBuffer skinnedVertexBuffer;
	VkDeviceMemory skinnedVertexMemory;
	byte *skinnedVertexMapped;
	VkDeviceSize skinnedVertexCapacity;
	VkDeviceSize skinnedVertexOffset;
	uint64_t ghoul2CacheFrameIndex;
	std::deque<vk_ghoul2_bone_cache_t> ghoul2BoneCache;
	std::vector<vk_ghoul2_surface_cache_t> ghoul2SurfaceCache;
	std::vector<vk_surface_sprite_stream_cache_t> surfaceSpriteStreamCache;
	std::vector<vk_ghoul2_skinned_audit_t> ghoul2SkinnedAudits;
	std::vector<const CGhoul2Info *> loggedCinematicGhouls;
	std::vector<const CGhoul2Info *> loggedGhoul2RenderAudits;
	std::vector<const CGhoul2Info *> loggedGhoul2SkinnedAudits;
	VkRenderPass renderPass;
	VkPipelineLayout pipelineLayout;
	VkPipeline pipeline;
	VkPipeline rectPipeline;
	VkPipeline texturedRectPipeline;
	VkPipeline texturedRectOpaquePipeline;
	VkPipeline texturedRectAdditivePipeline;
	VkPipeline texturedRectDestinationColorAdditivePipeline;
	VkPipeline texturedRectOneMinusDestinationAlphaAdditivePipeline;
	VkPipeline texturedRectModulatePipeline;
	VkPipeline diagnostic3dPipeline;
	VkPipeline worldPipeline;
	VkPipeline worldAlphaPipeline;
	VkPipeline worldAlphaDepthWritePipeline;
	VkPipeline worldAdditivePipeline;
	VkPipeline worldSourceAlphaAdditivePipeline;
	VkPipeline worldOneSourceAlphaPipeline;
	VkPipeline worldDestinationColorAdditivePipeline;
	VkPipeline worldOneMinusDestinationAlphaAdditivePipeline;
	VkPipeline worldModulatePipeline;
	VkPipeline worldDoubleModulatePipeline;
	VkDescriptorSetLayout textureSetLayout;
	VkDescriptorPool descriptorPool;
	VkSampler textureSampler;
	VkSampler worldTextureSampler;
	uint32_t queueFamilyIndex;
	uint32_t queueIndex;
	uint32_t apiVersion;

	float currentColor[4];
	std::vector<vk_rect_t> rects;
	std::vector<vk_texture_t> textures;
	std::vector<vk_texture_name_t> textureNames;
	std::vector<vk_texture_name_t> imageNames;
	std::vector<vk_texture_name_t> modelNames;
	std::vector<vk_texture_name_t> skinNames;
	std::vector<vk_skin_t> skins;
	std::vector<vk_model_t> models;
	std::vector<std::shared_ptr<vk_gla_t>> animations;
	std::vector<vk_material_t> materials;
	std::vector<vk_shader_definition_t> shaderDefinitions;
	bool shaderDefinitionsLoaded;
	uint64_t frameIndex;
	size_t maxRectCount;
	bool loggedNoRects;
	bool loggedFirstRects;
	bool loggedRawStretch;
	bool loggedProjectionViews;
	bool loggedHmdPose;
	bool loggedFov;
	bool loggedHudStereo;
	bool loggedGhoul2Skinning;
	bool loggedGhoul2StreamInvalid;
	bool loggedGhoul2StreamOverflow;
	bool loggedSurfaceSpriteStreamOverflow;
	bool loggedSurfaceSpriteDraw;
	bool weatherSnow;
	bool weatherGusting;
	float weatherWind[3];
	uint32_t weatherSnowCount;
	qhandle_t weatherSnowShader;
	std::vector<vk_weather_zone_t> weatherZones;
	std::unordered_map<uint64_t, bool> weatherOutsideCache;
	vk_surface_sprite_batch_t weatherSnowBatch;
	uint64_t weatherSnowBatchFrame;
	bool loggedWeatherDraw;
	bool loggedWeatherSuppressed;
	bool loggedWeatherResourceFailure;
	uint32_t loggedWeaponOnlyEntities;
	std::vector<std::string> loggedWeaponOnlyModels;
	bool screenLayerActive;
	bool screenLayerStateKnown;
	bool screenLayerPoseValid;
	XrPosef screenLayerPose;
	uint32_t missingTextureCount;
	uint32_t modelRegistrationCount;
	uint32_t skinRegistrationCount;
	uint32_t worldLoadCount;
	vk_world_geometry_t world;
	uint32_t sceneEntityCount;
	uint32_t scenePolyCount;
	uint32_t scenePolyVertexCount;
	uint32_t sceneLightCount;
	uint32_t sceneRenderCount;
	uint32_t sceneEntityTypes[RT_MAX_REF_ENTITY_TYPE];
	std::vector<refEntity_t> sceneEntities;
	std::vector<refEntity_t> worldEntities;
	std::vector<vk_scene_submission_t> screenScenes;
	std::vector<vk_screen_scene_clip_t> screenSceneClips;
	bool sceneRenderedThisFrame;
	bool sceneWorldRenderedThisFrame;
	bool loggedFirstScene;
	bool loggedGameplayViewMode;
	bool loggedFirstModelDraw;
	bool haveWorldRefdef;
	refdef_t worldRefdef;
	bool loggedDiagnosticDraw;
	bool loggedWorldDraw;
	bool loggedVisibleWorldMaterials;
	bool loggedShipInteriorMaterials;
	bool loggedShipInteriorModels;
	cvar_t *diagnosticWorldCvar;
};

static vk_backend_state_t vk = {};
static void VK_LoadPendingRegistrations();
static bool VK_ModelBufferRangeValid( size_t offset, size_t byteCount, size_t limit );
static bool VK_PrepareXrFrame();
static void VK_UpdateJkxrHmdPose( XrTime displayTime );

template<typename T>
static T VK_ClampValue( T value, T minimum, T maximum )
{
	return std::max( minimum, std::min( value, maximum ) );
}

static void VK_Backend_Clear()
{
	vk.initialized = false;
	vk.sessionRunning = false;
	vk.exitRenderLoop = false;
	vk.sessionState = XR_SESSION_STATE_UNKNOWN;
	vk.xrInstance = XR_NULL_HANDLE;
	vk.xrSystemId = XR_NULL_SYSTEM_ID;
	vk.xrSession = XR_NULL_HANDLE;
	vk.viewSpace = XR_NULL_HANDLE;
	vk.localSpace = XR_NULL_HANDLE;
	vk.stageSpace = XR_NULL_HANDLE;
	vk.frameState = {};
	vk.frameState.type = XR_TYPE_FRAME_STATE;
	vk.frameBegun = false;
	vk.viewsValid = false;
	for ( int eye = 0; eye < VK_BACKEND_EYE_COUNT; ++eye )
	{
		vk.colorSwapchain[eye] = XR_NULL_HANDLE;
		vk.colorImages[eye] = nullptr;
		vk.colorImageViews[eye] = nullptr;
		vk.framebuffers[eye] = nullptr;
		vk.colorImageCount[eye] = 0;
		vk.colorImageIndex[eye] = 0;
		vk.depthImages[eye] = VK_NULL_HANDLE;
		vk.depthMemories[eye] = VK_NULL_HANDLE;
		vk.depthImageViews[eye] = VK_NULL_HANDLE;
		vk.viewConfiguration[eye].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
		vk.views[eye].type = XR_TYPE_VIEW;
	}
	vk.colorFormat = VK_FORMAT_R8G8B8A8_SRGB;
	vk.depthFormat = VK_FORMAT_D32_SFLOAT;
	vk.swapchainsCreated = false;
	vk.renderResourcesCreated = false;
	vk.xrCreateVulkanInstanceKHR = nullptr;
	vk.xrGetVulkanGraphicsDevice2KHR = nullptr;
	vk.xrCreateVulkanDeviceKHR = nullptr;
	vk.xrGetVulkanGraphicsRequirements2KHR = nullptr;
	vk.instance = VK_NULL_HANDLE;
	vk.physicalDevice = VK_NULL_HANDLE;
	vk.device = VK_NULL_HANDLE;
	vk.queue = VK_NULL_HANDLE;
	vk.commandPool = VK_NULL_HANDLE;
	vk.commandBuffer = VK_NULL_HANDLE;
	vk.skinnedVertexBuffer = VK_NULL_HANDLE;
	vk.skinnedVertexMemory = VK_NULL_HANDLE;
	vk.skinnedVertexMapped = nullptr;
	vk.skinnedVertexCapacity = 0;
	vk.skinnedVertexOffset = 0;
	vk.ghoul2CacheFrameIndex = ~uint64_t{ 0 };
	vk.ghoul2BoneCache.clear();
	vk.ghoul2SurfaceCache.clear();
	vk.surfaceSpriteStreamCache.clear();
	vk.ghoul2SkinnedAudits.clear();
	vk.loggedCinematicGhouls.clear();
	vk.loggedGhoul2RenderAudits.clear();
	vk.loggedGhoul2SkinnedAudits.clear();
	vk.renderPass = VK_NULL_HANDLE;
	vk.pipelineLayout = VK_NULL_HANDLE;
	vk.pipeline = VK_NULL_HANDLE;
	vk.rectPipeline = VK_NULL_HANDLE;
	vk.texturedRectPipeline = VK_NULL_HANDLE;
	vk.texturedRectOpaquePipeline = VK_NULL_HANDLE;
	vk.texturedRectAdditivePipeline = VK_NULL_HANDLE;
	vk.texturedRectDestinationColorAdditivePipeline = VK_NULL_HANDLE;
	vk.texturedRectOneMinusDestinationAlphaAdditivePipeline = VK_NULL_HANDLE;
	vk.texturedRectModulatePipeline = VK_NULL_HANDLE;
	vk.diagnostic3dPipeline = VK_NULL_HANDLE;
	vk.worldPipeline = VK_NULL_HANDLE;
	vk.worldAlphaPipeline = VK_NULL_HANDLE;
	vk.worldAlphaDepthWritePipeline = VK_NULL_HANDLE;
	vk.worldAdditivePipeline = VK_NULL_HANDLE;
	vk.worldSourceAlphaAdditivePipeline = VK_NULL_HANDLE;
	vk.worldOneSourceAlphaPipeline = VK_NULL_HANDLE;
	vk.worldDestinationColorAdditivePipeline = VK_NULL_HANDLE;
	vk.worldOneMinusDestinationAlphaAdditivePipeline = VK_NULL_HANDLE;
	vk.worldModulatePipeline = VK_NULL_HANDLE;
	vk.worldDoubleModulatePipeline = VK_NULL_HANDLE;
	vk.textureSetLayout = VK_NULL_HANDLE;
	vk.descriptorPool = VK_NULL_HANDLE;
	vk.textureSampler = VK_NULL_HANDLE;
	vk.worldTextureSampler = VK_NULL_HANDLE;
	vk.queueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	vk.queueIndex = 0;
	vk.currentColor[0] = 1.0f;
	vk.currentColor[1] = 1.0f;
	vk.currentColor[2] = 1.0f;
	vk.currentColor[3] = 1.0f;
	vk.rects.clear();
	vk.textures.clear();
	vk.textureNames.clear();
	vk.imageNames.clear();
	vk.modelNames.clear();
	vk.skinNames.clear();
	vk.skins.clear();
	vk.models.clear();
	vk.animations.clear();
	vk.materials.clear();
	vk.shaderDefinitions.clear();
	vk.shaderDefinitionsLoaded = false;
	vk.frameIndex = 0;
	vk.maxRectCount = 0;
	vk.loggedNoRects = false;
	vk.loggedFirstRects = false;
	vk.loggedRawStretch = false;
	vk.loggedProjectionViews = false;
	vk.loggedHmdPose = false;
	vk.loggedFov = false;
	vk.loggedHudStereo = false;
	vk.loggedGhoul2Skinning = false;
	vk.loggedGhoul2StreamInvalid = false;
	vk.loggedGhoul2StreamOverflow = false;
	vk.loggedSurfaceSpriteStreamOverflow = false;
	vk.loggedSurfaceSpriteDraw = false;
	vk.weatherSnow = false;
	vk.weatherGusting = false;
	std::memset( vk.weatherWind, 0, sizeof( vk.weatherWind ) );
	vk.weatherSnowCount = 0;
	vk.weatherSnowShader = 0;
	vk.weatherZones.clear();
	vk.weatherOutsideCache.clear();
	vk.weatherSnowBatch = {};
	vk.weatherSnowBatchFrame = ~uint64_t{ 0 };
	vk.loggedWeatherDraw = false;
	vk.loggedWeatherSuppressed = false;
	vk.loggedWeatherResourceFailure = false;
	vk.loggedWeaponOnlyEntities = 0;
	vk.loggedWeaponOnlyModels.clear();
	vk.screenLayerActive = false;
	vk.screenLayerStateKnown = false;
	vk.screenLayerPoseValid = false;
	vk.screenLayerPose.orientation.w = 1.0f;
	vk.missingTextureCount = 0;
	vk.modelRegistrationCount = 0;
	vk.skinRegistrationCount = 0;
	vk.worldLoadCount = 0;
	vk.world = {};
	vk.sceneEntityCount = 0;
	vk.scenePolyCount = 0;
	vk.scenePolyVertexCount = 0;
	vk.sceneLightCount = 0;
	vk.sceneRenderCount = 0;
	std::memset( vk.sceneEntityTypes, 0, sizeof( vk.sceneEntityTypes ) );
	vk.sceneEntities.clear();
	vk.worldEntities.clear();
	vk.screenScenes.clear();
	vk.screenSceneClips.clear();
	vk.sceneRenderedThisFrame = false;
	vk.sceneWorldRenderedThisFrame = false;
	vk.loggedFirstScene = false;
	vk.loggedGameplayViewMode = false;
	vk.loggedFirstModelDraw = false;
	vk.haveWorldRefdef = false;
	std::memset( &vk.worldRefdef, 0, sizeof( vk.worldRefdef ) );
	vk.loggedDiagnosticDraw = false;
	vk.loggedWorldDraw = false;
	vk.loggedVisibleWorldMaterials = false;
	vk.loggedShipInteriorMaterials = false;
	vk.loggedShipInteriorModels = false;
	vk.diagnosticWorldCvar = nullptr;
}

static bool VK_Backend_AppendScreenRect(
	float x,
	float y,
	float w,
	float h,
	const float color[4],
	float s1 = 0.0f,
	float t1 = 0.0f,
	float s2 = 1.0f,
	float t2 = 1.0f,
	qhandle_t texture = 0,
	vk_blend_mode_t blendMode = VK_BLEND_ALPHA )
{
	if ( w == 0.0f || h == 0.0f )
	{
		return false;
	}

	if ( vk.rects.size() >= 8192 )
	{
		return false;
	}

	const float x0 = ( x / 640.0f ) * 2.0f - 1.0f;
	const float x1 = ( ( x + w ) / 640.0f ) * 2.0f - 1.0f;
	const float y0 = ( y / 480.0f ) * 2.0f - 1.0f;
	const float y1 = ( ( y + h ) / 480.0f ) * 2.0f - 1.0f;

	vk_rect_t rect = {};
	rect.rect[0] = x0;
	rect.rect[1] = y0;
	rect.rect[2] = x1;
	rect.rect[3] = y1;
	rect.uv[0] = s1;
	rect.uv[1] = t1;
	rect.uv[2] = s2;
	rect.uv[3] = t2;
	rect.color[0] = color[0];
	rect.color[1] = color[1];
	rect.color[2] = color[2];
	rect.color[3] = color[3];
	rect.texture = texture;
	rect.blendMode = blendMode;
	vk.rects.push_back( rect );
	return true;
}

static void VK_LogXrFailure( const char *what, XrResult result )
{
	char resultString[XR_MAX_RESULT_STRING_SIZE] = {};
	if ( vk.xrInstance != XR_NULL_HANDLE )
	{
		xrResultToString( vk.xrInstance, result, resultString );
	}
	else
	{
		std::snprintf( resultString, sizeof( resultString ), "%d", result );
	}

	ri.Printf( PRINT_WARNING, "rd-vulkan: %s failed: %s\n", what, resultString );
}

static bool VK_CheckXr( XrResult result, const char *what )
{
	if ( XR_SUCCEEDED( result ) )
	{
		return true;
	}

	VK_LogXrFailure( what, result );
	return false;
}

static bool VK_CheckVk( VkResult result, const char *what )
{
	if ( result == VK_SUCCESS )
	{
		return true;
	}

	ri.Printf( PRINT_WARNING, "rd-vulkan: %s failed: VkResult %d\n", what, result );
	return false;
}

static bool VK_LoadXrProc( const char *name, PFN_xrVoidFunction *function )
{
	if ( !VK_CheckXr( xrGetInstanceProcAddr( vk.xrInstance, name, function ), name ) )
	{
		return false;
	}

	if ( *function == nullptr )
	{
		ri.Printf( PRINT_WARNING, "rd-vulkan: OpenXR runtime did not expose %s\n", name );
		return false;
	}

	return true;
}

static bool VK_HasXrExtension( const char *extensionName )
{
	uint32_t extensionCount = 0;
	XrResult result = xrEnumerateInstanceExtensionProperties( nullptr, 0, &extensionCount, nullptr );
	if ( XR_FAILED( result ) )
	{
		VK_LogXrFailure( "xrEnumerateInstanceExtensionProperties", result );
		return false;
	}

	std::vector<XrExtensionProperties> extensions( extensionCount );
	for ( XrExtensionProperties &extension : extensions )
	{
		extension.type = XR_TYPE_EXTENSION_PROPERTIES;
		extension.next = nullptr;
	}

	result = xrEnumerateInstanceExtensionProperties( nullptr, extensionCount, &extensionCount, extensions.data() );
	if ( XR_FAILED( result ) )
	{
		VK_LogXrFailure( "xrEnumerateInstanceExtensionProperties", result );
		return false;
	}

	return std::any_of( extensions.begin(), extensions.end(), [extensionName]( const XrExtensionProperties &extension ) {
		return std::strcmp( extension.extensionName, extensionName ) == 0;
	} );
}

static XrVersion VK_SelectApiVersion( const XrGraphicsRequirementsVulkanKHR &requirements, uint32_t loaderVersion )
{
	const XrVersion desired13 = XR_MAKE_VERSION( 1, 3, 0 );
	const XrVersion desired14 = XR_MAKE_VERSION( 1, 4, 0 );
	XrVersion selected = desired13;

	if ( loaderVersion >= VK_API_VERSION_1_4 && requirements.maxApiVersionSupported >= desired14 )
	{
		selected = desired14;
	}
	else if ( loaderVersion < VK_API_VERSION_1_3 || requirements.maxApiVersionSupported < desired13 )
	{
		selected = requirements.maxApiVersionSupported;
	}

	if ( selected < requirements.minApiVersionSupported )
	{
		selected = requirements.minApiVersionSupported;
	}

	return selected;
}

static uint32_t VK_XrVersionToVkVersion( XrVersion version )
{
	return VK_MAKE_API_VERSION( 0,
		static_cast<uint32_t>( XR_VERSION_MAJOR( version ) ),
		static_cast<uint32_t>( XR_VERSION_MINOR( version ) ),
		static_cast<uint32_t>( XR_VERSION_PATCH( version ) ) );
}

static bool VK_CreateXrInstance()
{
	if ( !VK_HasXrExtension( XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME ) )
	{
		ri.Printf( PRINT_WARNING, "rd-vulkan: OpenXR runtime does not support %s\n",
			XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME );
		return false;
	}

	const char *requiredExtensions[] = {
		XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME,
	};

	XrInstanceCreateInfo createInfo = {};
	createInfo.type = XR_TYPE_INSTANCE_CREATE_INFO;
	std::strncpy( createInfo.applicationInfo.applicationName, "JKXRL", XR_MAX_APPLICATION_NAME_SIZE - 1 );
	createInfo.applicationInfo.applicationVersion = 1;
	std::strncpy( createInfo.applicationInfo.engineName, "OpenJK rd-vulkan", XR_MAX_ENGINE_NAME_SIZE - 1 );
	createInfo.applicationInfo.engineVersion = 1;
	createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
	createInfo.enabledExtensionCount = ARRAY_LEN( requiredExtensions );
	createInfo.enabledExtensionNames = requiredExtensions;

	return VK_CheckXr( xrCreateInstance( &createInfo, &vk.xrInstance ), "xrCreateInstance" );
}

static bool VK_GetXrSystem()
{
	XrSystemGetInfo getInfo = {};
	getInfo.type = XR_TYPE_SYSTEM_GET_INFO;
	getInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

	return VK_CheckXr( xrGetSystem( vk.xrInstance, &getInfo, &vk.xrSystemId ), "xrGetSystem" );
}

static bool VK_LoadXrVulkanEntryPoints()
{
	return
		VK_LoadXrProc( "xrCreateVulkanInstanceKHR", reinterpret_cast<PFN_xrVoidFunction *>( &vk.xrCreateVulkanInstanceKHR ) ) &&
		VK_LoadXrProc( "xrGetVulkanGraphicsDevice2KHR", reinterpret_cast<PFN_xrVoidFunction *>( &vk.xrGetVulkanGraphicsDevice2KHR ) ) &&
		VK_LoadXrProc( "xrCreateVulkanDeviceKHR", reinterpret_cast<PFN_xrVoidFunction *>( &vk.xrCreateVulkanDeviceKHR ) ) &&
		VK_LoadXrProc( "xrGetVulkanGraphicsRequirements2KHR", reinterpret_cast<PFN_xrVoidFunction *>( &vk.xrGetVulkanGraphicsRequirements2KHR ) );
}

static bool VK_CreateVulkanInstance()
{
	uint32_t loaderVersion = VK_API_VERSION_1_0;
	PFN_vkEnumerateInstanceVersion enumerateInstanceVersion =
		reinterpret_cast<PFN_vkEnumerateInstanceVersion>( vkGetInstanceProcAddr( VK_NULL_HANDLE, "vkEnumerateInstanceVersion" ) );
	if ( enumerateInstanceVersion != nullptr )
	{
		enumerateInstanceVersion( &loaderVersion );
	}

	XrGraphicsRequirementsVulkanKHR requirements = {};
	requirements.type = XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR;
	if ( !VK_CheckXr( vk.xrGetVulkanGraphicsRequirements2KHR( vk.xrInstance, vk.xrSystemId, &requirements ),
			"xrGetVulkanGraphicsRequirements2KHR" ) )
	{
		return false;
	}

	const XrVersion selectedXrVersion = VK_SelectApiVersion( requirements, loaderVersion );
	vk.apiVersion = VK_XrVersionToVkVersion( selectedXrVersion );

	VkApplicationInfo appInfo = {};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "JKXRL";
	appInfo.applicationVersion = VK_MAKE_API_VERSION( 0, 0, 1, 0 );
	appInfo.pEngineName = "OpenJK rd-vulkan";
	appInfo.engineVersion = VK_MAKE_API_VERSION( 0, 0, 1, 0 );
	appInfo.apiVersion = vk.apiVersion;

	VkInstanceCreateInfo instanceInfo = {};
	instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instanceInfo.pApplicationInfo = &appInfo;

	XrVulkanInstanceCreateInfoKHR xrInstanceInfo = {};
	xrInstanceInfo.type = XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR;
	xrInstanceInfo.systemId = vk.xrSystemId;
	xrInstanceInfo.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
	xrInstanceInfo.vulkanCreateInfo = &instanceInfo;

	VkResult vkResult = VK_SUCCESS;
	if ( !VK_CheckXr( vk.xrCreateVulkanInstanceKHR( vk.xrInstance, &xrInstanceInfo, &vk.instance, &vkResult ),
			"xrCreateVulkanInstanceKHR" ) )
	{
		return false;
	}

	return VK_CheckVk( vkResult, "vkCreateInstance via OpenXR" );
}

static bool VK_SelectPhysicalDevice()
{
	XrVulkanGraphicsDeviceGetInfoKHR getInfo = {};
	getInfo.type = XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR;
	getInfo.systemId = vk.xrSystemId;
	getInfo.vulkanInstance = vk.instance;

	return VK_CheckXr( vk.xrGetVulkanGraphicsDevice2KHR( vk.xrInstance, &getInfo, &vk.physicalDevice ),
		"xrGetVulkanGraphicsDevice2KHR" );
}

static bool VK_SelectQueueFamily()
{
	uint32_t familyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties( vk.physicalDevice, &familyCount, nullptr );
	if ( familyCount == 0 )
	{
		ri.Printf( PRINT_WARNING, "rd-vulkan: selected physical device has no queue families\n" );
		return false;
	}

	std::vector<VkQueueFamilyProperties> families( familyCount );
	vkGetPhysicalDeviceQueueFamilyProperties( vk.physicalDevice, &familyCount, families.data() );

	for ( uint32_t i = 0; i < familyCount; ++i )
	{
		if ( families[i].queueCount > 0 && ( families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT ) )
		{
			vk.queueFamilyIndex = i;
			vk.queueIndex = 0;
			return true;
		}
	}

	ri.Printf( PRINT_WARNING, "rd-vulkan: selected physical device has no graphics queue family\n" );
	return false;
}

static bool VK_CreateVulkanDevice()
{
	const float queuePriority = 1.0f;
	VkDeviceQueueCreateInfo queueInfo = {};
	queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queueInfo.queueFamilyIndex = vk.queueFamilyIndex;
	queueInfo.queueCount = 1;
	queueInfo.pQueuePriorities = &queuePriority;

	VkDeviceCreateInfo deviceInfo = {};
	deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	deviceInfo.queueCreateInfoCount = 1;
	deviceInfo.pQueueCreateInfos = &queueInfo;

	XrVulkanDeviceCreateInfoKHR xrDeviceInfo = {};
	xrDeviceInfo.type = XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR;
	xrDeviceInfo.systemId = vk.xrSystemId;
	xrDeviceInfo.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
	xrDeviceInfo.vulkanPhysicalDevice = vk.physicalDevice;
	xrDeviceInfo.vulkanCreateInfo = &deviceInfo;

	VkResult vkResult = VK_SUCCESS;
	if ( !VK_CheckXr( vk.xrCreateVulkanDeviceKHR( vk.xrInstance, &xrDeviceInfo, &vk.device, &vkResult ),
			"xrCreateVulkanDeviceKHR" ) )
	{
		return false;
	}

	if ( !VK_CheckVk( vkResult, "vkCreateDevice via OpenXR" ) )
	{
		return false;
	}

	vkGetDeviceQueue( vk.device, vk.queueFamilyIndex, vk.queueIndex, &vk.queue );
	return true;
}

static bool VK_CreateCommandResources()
{
	VkCommandPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	poolInfo.queueFamilyIndex = vk.queueFamilyIndex;

	if ( !VK_CheckVk( vkCreateCommandPool( vk.device, &poolInfo, nullptr, &vk.commandPool ), "vkCreateCommandPool" ) )
	{
		return false;
	}

	VkCommandBufferAllocateInfo allocateInfo = {};
	allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocateInfo.commandPool = vk.commandPool;
	allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocateInfo.commandBufferCount = 1;

	return VK_CheckVk( vkAllocateCommandBuffers( vk.device, &allocateInfo, &vk.commandBuffer ), "vkAllocateCommandBuffers" );
}

static bool VK_FindMemoryType( uint32_t typeBits, VkMemoryPropertyFlags properties, uint32_t *typeIndex )
{
	VkPhysicalDeviceMemoryProperties memoryProperties = {};
	vkGetPhysicalDeviceMemoryProperties( vk.physicalDevice, &memoryProperties );

	for ( uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i )
	{
		if ( ( typeBits & ( 1u << i ) ) != 0 &&
			 ( memoryProperties.memoryTypes[i].propertyFlags & properties ) == properties )
		{
			*typeIndex = i;
			return true;
		}
	}

	ri.Printf( PRINT_WARNING, "rd-vulkan: no compatible Vulkan memory type found\n" );
	return false;
}

static bool VK_CreateTextureDescriptors()
{
	VkDescriptorSetLayoutBinding binding = {};
	binding.binding = 0;
	binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	binding.descriptorCount = 1;
	binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = 1;
	layoutInfo.pBindings = &binding;
	if ( !VK_CheckVk( vkCreateDescriptorSetLayout( vk.device, &layoutInfo, nullptr, &vk.textureSetLayout ),
			"vkCreateDescriptorSetLayout" ) )
	{
		return false;
	}

	VkDescriptorPoolSize poolSize = {};
	poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSize.descriptorCount = 8192;

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.maxSets = poolSize.descriptorCount;
	poolInfo.poolSizeCount = 1;
	poolInfo.pPoolSizes = &poolSize;
	if ( !VK_CheckVk( vkCreateDescriptorPool( vk.device, &poolInfo, nullptr, &vk.descriptorPool ),
			"vkCreateDescriptorPool" ) )
	{
		return false;
	}

	VkSamplerCreateInfo samplerInfo = {};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.maxLod = 0.0f;
	if ( !VK_CheckVk( vkCreateSampler( vk.device, &samplerInfo, nullptr, &vk.textureSampler ),
			"vkCreateSampler" ) )
	{
		return false;
	}

	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	return VK_CheckVk( vkCreateSampler( vk.device, &samplerInfo, nullptr, &vk.worldTextureSampler ),
		"vkCreateSampler(world)" );
}

static void VK_DestroyTexture( vk_texture_t &texture )
{
	if ( texture.view != VK_NULL_HANDLE )
	{
		vkDestroyImageView( vk.device, texture.view, nullptr );
	}
	if ( texture.image != VK_NULL_HANDLE )
	{
		vkDestroyImage( vk.device, texture.image, nullptr );
	}
	if ( texture.memory != VK_NULL_HANDLE )
	{
		vkFreeMemory( vk.device, texture.memory, nullptr );
	}
	texture = {};
}

static void VK_DestroyBuffer( VkBuffer *buffer, VkDeviceMemory *memory )
{
	if ( vk.device != VK_NULL_HANDLE )
	{
		if ( *buffer != VK_NULL_HANDLE )
		{
			vkDestroyBuffer( vk.device, *buffer, nullptr );
		}
		if ( *memory != VK_NULL_HANDLE )
		{
			vkFreeMemory( vk.device, *memory, nullptr );
		}
	}
	*buffer = VK_NULL_HANDLE;
	*memory = VK_NULL_HANDLE;
}

static void VK_DestroyModelSurface( vk_model_surface_t &surface )
{
	VK_DestroyBuffer( &surface.vertexBuffer, &surface.vertexMemory );
	VK_DestroyBuffer( &surface.indexBuffer, &surface.indexMemory );
	surface = {};
}

static void VK_DestroyModelRegistry()
{
	for ( vk_model_t &model : vk.models )
	{
		for ( vk_model_surface_t &surface : model.surfaces )
		{
			VK_DestroyModelSurface( surface );
		}
		model.surfaces.clear();
	}
	vk.models.clear();
}

static void VK_DestroyWorldGeometry()
{
	VK_DestroyBuffer( &vk.world.vertexBuffer, &vk.world.vertexMemory );
	VK_DestroyBuffer( &vk.world.indexBuffer, &vk.world.indexMemory );
	vk.world.vertexCount = 0;
	vk.world.indexCount = 0;
	vk.world.surfaceCount = 0;
	vk.world.texturedBatchCount = 0;
	vk.world.batches.clear();
	vk.world.surfaceSpriteBatches.clear();
	std::memset( vk.world.skyTextures, 0, sizeof( vk.world.skyTextures ) );
	vk.world.skyName.clear();
	vk.world.hasSky = false;
	std::memset( vk.world.globalFogColor, 0, sizeof( vk.world.globalFogColor ) );
	vk.world.globalFogDepth = 0.0f;
	vk.world.hasGlobalFog = false;
	vk.world.surfaceBatchIndex.clear();
	vk.world.bspSurfaceCount = 0;
	vk.world.inlineModels.clear();
	vk.world.planes.clear();
	vk.world.nodes.clear();
	vk.world.leafs.clear();
	vk.world.leafSurfaces.clear();
	vk.world.visibility.clear();
	vk.world.numClusters = 0;
	vk.world.clusterBytes = 0;
	vk.world.visibleSurfaces.clear();
	vk.world.loggedVisibility = false;
}

static bool VK_CreateBuffer(
	VkDeviceSize size,
	VkBufferUsageFlags usage,
	VkMemoryPropertyFlags properties,
	VkBuffer *buffer,
	VkDeviceMemory *memory,
	const char *label )
{
	*buffer = VK_NULL_HANDLE;
	*memory = VK_NULL_HANDLE;

	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = size;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	char action[128];
	Com_sprintf( action, sizeof( action ), "vkCreateBuffer(%s)", label );
	if ( !VK_CheckVk( vkCreateBuffer( vk.device, &bufferInfo, nullptr, buffer ), action ) )
	{
		return false;
	}

	VkMemoryRequirements requirements = {};
	vkGetBufferMemoryRequirements( vk.device, *buffer, &requirements );

	uint32_t memoryType = 0;
	if ( !VK_FindMemoryType( requirements.memoryTypeBits, properties, &memoryType ) )
	{
		VK_DestroyBuffer( buffer, memory );
		return false;
	}

	VkMemoryAllocateInfo allocateInfo = {};
	allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocateInfo.allocationSize = requirements.size;
	allocateInfo.memoryTypeIndex = memoryType;

	Com_sprintf( action, sizeof( action ), "vkAllocateMemory(%s)", label );
	if ( !VK_CheckVk( vkAllocateMemory( vk.device, &allocateInfo, nullptr, memory ), action ) )
	{
		VK_DestroyBuffer( buffer, memory );
		return false;
	}

	Com_sprintf( action, sizeof( action ), "vkBindBufferMemory(%s)", label );
	if ( !VK_CheckVk( vkBindBufferMemory( vk.device, *buffer, *memory, 0 ), action ) )
	{
		VK_DestroyBuffer( buffer, memory );
		return false;
	}

	return true;
}

static bool VK_UploadBuffer(
	const void *data,
	VkDeviceSize size,
	VkBufferUsageFlags usage,
	VkBuffer *buffer,
	VkDeviceMemory *memory,
	const char *label )
{
	if ( data == nullptr || size == 0 || vk.device == VK_NULL_HANDLE || vk.commandPool == VK_NULL_HANDLE )
	{
		return false;
	}

	VkBuffer stagingBuffer = VK_NULL_HANDLE;
	VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
	if ( !VK_CreateBuffer(
			size,
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&stagingBuffer,
			&stagingMemory,
			"world staging" ) )
	{
		return false;
	}

	void *mapped = nullptr;
	if ( !VK_CheckVk( vkMapMemory( vk.device, stagingMemory, 0, size, 0, &mapped ), "vkMapMemory(world staging)" ) )
	{
		VK_DestroyBuffer( &stagingBuffer, &stagingMemory );
		return false;
	}
	std::memcpy( mapped, data, static_cast<size_t>( size ) );
	vkUnmapMemory( vk.device, stagingMemory );

	if ( !VK_CreateBuffer(
			size,
			usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
			buffer,
			memory,
			label ) )
	{
		VK_DestroyBuffer( &stagingBuffer, &stagingMemory );
		return false;
	}

	bool uploaded = VK_CheckVk( vkResetCommandPool( vk.device, vk.commandPool, 0 ), "vkResetCommandPool(world upload)" );
	if ( uploaded )
	{
		VkCommandBufferBeginInfo commandBeginInfo = {};
		commandBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		commandBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		uploaded = VK_CheckVk( vkBeginCommandBuffer( vk.commandBuffer, &commandBeginInfo ),
			"vkBeginCommandBuffer(world upload)" );
	}
	if ( uploaded )
	{
		VkBufferCopy copy = {};
		copy.size = size;
		vkCmdCopyBuffer( vk.commandBuffer, stagingBuffer, *buffer, 1, &copy );
		uploaded = VK_CheckVk( vkEndCommandBuffer( vk.commandBuffer ), "vkEndCommandBuffer(world upload)" );
	}
	if ( uploaded )
	{
		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &vk.commandBuffer;
		uploaded = VK_CheckVk( vkQueueSubmit( vk.queue, 1, &submitInfo, VK_NULL_HANDLE ),
			"vkQueueSubmit(world upload)" ) &&
			VK_CheckVk( vkQueueWaitIdle( vk.queue ), "vkQueueWaitIdle(world upload)" );
	}

	VK_DestroyBuffer( &stagingBuffer, &stagingMemory );
	if ( !uploaded )
	{
		VK_DestroyBuffer( buffer, memory );
	}
	return uploaded;
}

static bool VK_CreateSkinnedVertexStream()
{
	if ( vk.skinnedVertexBuffer != VK_NULL_HANDLE )
	{
		return true;
	}

	constexpr VkDeviceSize capacity = 64ull * 1024ull * 1024ull;
	if ( !VK_CreateBuffer(
			capacity,
			VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&vk.skinnedVertexBuffer,
			&vk.skinnedVertexMemory,
			"Ghoul2 skinned vertex stream" ) )
	{
		return false;
	}

	void *mapped = nullptr;
	if ( !VK_CheckVk(
			vkMapMemory( vk.device, vk.skinnedVertexMemory, 0, capacity, 0, &mapped ),
			"vkMapMemory(Ghoul2 skinned vertex stream)" ) )
	{
		VK_DestroyBuffer( &vk.skinnedVertexBuffer, &vk.skinnedVertexMemory );
		return false;
	}

	vk.skinnedVertexMapped = static_cast<byte *>( mapped );
	vk.skinnedVertexCapacity = capacity;
	vk.skinnedVertexOffset = 0;
	return true;
}

static bool VK_CreateTextureFromPixels(
	const byte *pixels,
	uint32_t width,
	uint32_t height,
	vk_texture_t *texture,
	VkFormat format = VK_FORMAT_R8G8B8A8_SRGB )
{
	const VkDeviceSize uploadSize = static_cast<VkDeviceSize>( width ) * height * 4;
	VkBuffer stagingBuffer = VK_NULL_HANDLE;
	VkDeviceMemory stagingMemory = VK_NULL_HANDLE;

	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = uploadSize;
	bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	if ( !VK_CheckVk( vkCreateBuffer( vk.device, &bufferInfo, nullptr, &stagingBuffer ), "vkCreateBuffer(texture staging)" ) )
	{
		return false;
	}

	VkMemoryRequirements stagingRequirements = {};
	vkGetBufferMemoryRequirements( vk.device, stagingBuffer, &stagingRequirements );
	uint32_t stagingMemoryType = 0;
	if ( !VK_FindMemoryType( stagingRequirements.memoryTypeBits,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&stagingMemoryType ) )
	{
		vkDestroyBuffer( vk.device, stagingBuffer, nullptr );
		return false;
	}

	VkMemoryAllocateInfo stagingAllocateInfo = {};
	stagingAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	stagingAllocateInfo.allocationSize = stagingRequirements.size;
	stagingAllocateInfo.memoryTypeIndex = stagingMemoryType;
	if ( !VK_CheckVk( vkAllocateMemory( vk.device, &stagingAllocateInfo, nullptr, &stagingMemory ),
			"vkAllocateMemory(texture staging)" ) ||
		 !VK_CheckVk( vkBindBufferMemory( vk.device, stagingBuffer, stagingMemory, 0 ),
			"vkBindBufferMemory(texture staging)" ) )
	{
		vkDestroyBuffer( vk.device, stagingBuffer, nullptr );
		if ( stagingMemory != VK_NULL_HANDLE )
		{
			vkFreeMemory( vk.device, stagingMemory, nullptr );
		}
		return false;
	}

	void *mapped = nullptr;
	if ( !VK_CheckVk( vkMapMemory( vk.device, stagingMemory, 0, uploadSize, 0, &mapped ),
			"vkMapMemory(texture staging)" ) )
	{
		vkDestroyBuffer( vk.device, stagingBuffer, nullptr );
		vkFreeMemory( vk.device, stagingMemory, nullptr );
		return false;
	}
	std::memcpy( mapped, pixels, static_cast<size_t>( uploadSize ) );
	vkUnmapMemory( vk.device, stagingMemory );

	VkImageCreateInfo imageInfo = {};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.format = format;
	imageInfo.extent.width = width;
	imageInfo.extent.height = height;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	if ( !VK_CheckVk( vkCreateImage( vk.device, &imageInfo, nullptr, &texture->image ), "vkCreateImage(texture)" ) )
	{
		vkDestroyBuffer( vk.device, stagingBuffer, nullptr );
		vkFreeMemory( vk.device, stagingMemory, nullptr );
		return false;
	}

	VkMemoryRequirements imageRequirements = {};
	vkGetImageMemoryRequirements( vk.device, texture->image, &imageRequirements );
	uint32_t imageMemoryType = 0;
	if ( !VK_FindMemoryType( imageRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &imageMemoryType ) )
	{
		VK_DestroyTexture( *texture );
		vkDestroyBuffer( vk.device, stagingBuffer, nullptr );
		vkFreeMemory( vk.device, stagingMemory, nullptr );
		return false;
	}

	VkMemoryAllocateInfo imageAllocateInfo = {};
	imageAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	imageAllocateInfo.allocationSize = imageRequirements.size;
	imageAllocateInfo.memoryTypeIndex = imageMemoryType;
	if ( !VK_CheckVk( vkAllocateMemory( vk.device, &imageAllocateInfo, nullptr, &texture->memory ),
			"vkAllocateMemory(texture)" ) ||
		 !VK_CheckVk( vkBindImageMemory( vk.device, texture->image, texture->memory, 0 ),
			"vkBindImageMemory(texture)" ) )
	{
		VK_DestroyTexture( *texture );
		vkDestroyBuffer( vk.device, stagingBuffer, nullptr );
		vkFreeMemory( vk.device, stagingMemory, nullptr );
		return false;
	}

	if ( !VK_CheckVk( vkResetCommandPool( vk.device, vk.commandPool, 0 ), "vkResetCommandPool(texture upload)" ) )
	{
		VK_DestroyTexture( *texture );
		vkDestroyBuffer( vk.device, stagingBuffer, nullptr );
		vkFreeMemory( vk.device, stagingMemory, nullptr );
		return false;
	}

	VkCommandBufferBeginInfo commandBeginInfo = {};
	commandBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	commandBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	bool uploaded = VK_CheckVk( vkBeginCommandBuffer( vk.commandBuffer, &commandBeginInfo ),
		"vkBeginCommandBuffer(texture upload)" );
	if ( uploaded )
	{
		VkImageMemoryBarrier toTransfer = {};
		toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		toTransfer.srcAccessMask = 0;
		toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toTransfer.image = texture->image;
		toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		toTransfer.subresourceRange.levelCount = 1;
		toTransfer.subresourceRange.layerCount = 1;
		vkCmdPipelineBarrier( vk.commandBuffer,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
			0, nullptr, 0, nullptr, 1, &toTransfer );

		VkBufferImageCopy copy = {};
		copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copy.imageSubresource.layerCount = 1;
		copy.imageExtent.width = width;
		copy.imageExtent.height = height;
		copy.imageExtent.depth = 1;
		vkCmdCopyBufferToImage( vk.commandBuffer, stagingBuffer, texture->image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy );

		VkImageMemoryBarrier toShaderRead = toTransfer;
		toShaderRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		toShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		toShaderRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		toShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		vkCmdPipelineBarrier( vk.commandBuffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
			0, nullptr, 0, nullptr, 1, &toShaderRead );

		uploaded = VK_CheckVk( vkEndCommandBuffer( vk.commandBuffer ), "vkEndCommandBuffer(texture upload)" );
	}

	if ( uploaded )
	{
		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &vk.commandBuffer;
		uploaded = VK_CheckVk( vkQueueSubmit( vk.queue, 1, &submitInfo, VK_NULL_HANDLE ),
			"vkQueueSubmit(texture upload)" ) &&
			VK_CheckVk( vkQueueWaitIdle( vk.queue ), "vkQueueWaitIdle(texture upload)" );
	}

	vkDestroyBuffer( vk.device, stagingBuffer, nullptr );
	vkFreeMemory( vk.device, stagingMemory, nullptr );
	if ( !uploaded )
	{
		VK_DestroyTexture( *texture );
		return false;
	}

	VkImageViewCreateInfo viewInfo = {};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = texture->image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = format;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.layerCount = 1;
	if ( !VK_CheckVk( vkCreateImageView( vk.device, &viewInfo, nullptr, &texture->view ),
			"vkCreateImageView(texture)" ) )
	{
		VK_DestroyTexture( *texture );
		return false;
	}

	VkDescriptorSet descriptorSets[2] = {};
	VkDescriptorSetLayout descriptorLayouts[2] = {
		vk.textureSetLayout,
		vk.textureSetLayout,
	};
	VkDescriptorSetAllocateInfo descriptorAllocateInfo = {};
	descriptorAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	descriptorAllocateInfo.descriptorPool = vk.descriptorPool;
	descriptorAllocateInfo.descriptorSetCount = ARRAY_LEN( descriptorSets );
	descriptorAllocateInfo.pSetLayouts = descriptorLayouts;
	if ( !VK_CheckVk( vkAllocateDescriptorSets( vk.device, &descriptorAllocateInfo, descriptorSets ),
			"vkAllocateDescriptorSets(texture)" ) )
	{
		VK_DestroyTexture( *texture );
		return false;
	}
	texture->descriptorSet = descriptorSets[0];
	texture->repeatDescriptorSet = descriptorSets[1];

	VkDescriptorImageInfo descriptorImageInfo[2] = {};
	descriptorImageInfo[0].sampler = vk.textureSampler;
	descriptorImageInfo[0].imageView = texture->view;
	descriptorImageInfo[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	descriptorImageInfo[1] = descriptorImageInfo[0];
	descriptorImageInfo[1].sampler = vk.worldTextureSampler;

	VkWriteDescriptorSet descriptorWrites[2] = {};
	for ( size_t i = 0; i < ARRAY_LEN( descriptorWrites ); ++i )
	{
		descriptorWrites[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[i].dstSet = descriptorSets[i];
		descriptorWrites[i].dstBinding = 0;
		descriptorWrites[i].descriptorCount = 1;
		descriptorWrites[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descriptorWrites[i].pImageInfo = &descriptorImageInfo[i];
	}
	vkUpdateDescriptorSets( vk.device, ARRAY_LEN( descriptorWrites ), descriptorWrites, 0, nullptr );

	texture->width = width;
	texture->height = height;
	return true;
}

static bool VK_CreateFallbackTexture()
{
	const byte whitePixel[4] = { 255, 255, 255, 255 };
	const byte transparentPixel[4] = { 0, 0, 0, 0 };
	vk.textures.resize( 3 );
	vk.materials.resize( 3 );
	if ( !VK_CreateTextureFromPixels( whitePixel, 1, 1, &vk.textures[1] ) )
	{
		return false;
	}
	if ( !VK_CreateTextureFromPixels( transparentPixel, 1, 1, &vk.textures[2] ) )
	{
		return false;
	}
	vk.textureNames.push_back( { "white", 1 } );
	vk.textureNames.push_back( { "transparent", 2 } );
	vk.imageNames.push_back( { "white", 1 } );
	vk.imageNames.push_back( { "transparent", 2 } );
	return true;
}

static vk_waveform_t VK_ParseWaveform( const char *name )
{
	if ( Q_stricmp( name, "sin" ) == 0 )
	{
		return VK_WAVE_SIN;
	}
	if ( Q_stricmp( name, "triangle" ) == 0 )
	{
		return VK_WAVE_TRIANGLE;
	}
	if ( Q_stricmp( name, "square" ) == 0 )
	{
		return VK_WAVE_SQUARE;
	}
	if ( Q_stricmp( name, "sawtooth" ) == 0 )
	{
		return VK_WAVE_SAWTOOTH;
	}
	if ( Q_stricmp( name, "inversesawtooth" ) == 0 )
	{
		return VK_WAVE_INVERSE_SAWTOOTH;
	}
	return VK_WAVE_NONE;
}

static float VK_EvaluateWaveform( vk_waveform_t type, float value )
{
	const float cycle = value - std::floor( value );
	switch ( type )
	{
	case VK_WAVE_SIN:
		return std::sin( cycle * 6.28318530717958647692f );
	case VK_WAVE_TRIANGLE:
		if ( cycle < 0.25f )
		{
			return cycle * 4.0f;
		}
		if ( cycle < 0.75f )
		{
			return 2.0f - cycle * 4.0f;
		}
		return cycle * 4.0f - 4.0f;
	case VK_WAVE_SQUARE:
		return cycle < 0.5f ? 1.0f : -1.0f;
	case VK_WAVE_SAWTOOTH:
		return cycle;
	case VK_WAVE_INVERSE_SAWTOOTH:
		return 1.0f - cycle;
	case VK_WAVE_NONE:
	default:
		return 0.0f;
	}
}

static void VK_ResetShaderStageDefinition( vk_shader_stage_definition_t *stage )
{
	*stage = {};
	stage->blendMode = VK_BLEND_OPAQUE;
	stage->alphaTest = VK_ALPHA_TEST_NONE;
	stage->surfaceSprite.type = VK_SURFACE_SPRITE_NONE;
	stage->surfaceSprite.facing = VK_SURFACE_SPRITE_FACING_NORMAL;
	stage->surfaceSprite.fxDuration = 1000.0f;
	stage->surfaceSprite.fxAlphaStart = 1.0f;
	stage->surfaceSprite.fxAlphaEnd = 0.0f;
	stage->alpha = 1.0f;
	stage->color[0] = 1.0f;
	stage->color[1] = 1.0f;
	stage->color[2] = 1.0f;
	stage->color[3] = 1.0f;
}

static void VK_ParseShaderFile( const char *filename )
{
	char *buffer = nullptr;
	if ( ri.FS_ReadFile( filename, reinterpret_cast<void **>( &buffer ) ) <= 0 || buffer == nullptr )
	{
		return;
	}

	COM_BeginParseSession( filename );
	const char *text = buffer;
	while ( true )
	{
		const char *token = COM_ParseExt( &text, qtrue );
		if ( token[0] == '\0' )
		{
			break;
		}

		vk_shader_definition_t definition = {};
		definition.name = token;
		token = COM_ParseExt( &text, qtrue );
		if ( Q_stricmp( token, "{" ) != 0 )
		{
			continue;
		}

		int depth = 1;
		vk_shader_stage_definition_t stage;
		VK_ResetShaderStageDefinition( &stage );
		while ( depth > 0 )
		{
			token = COM_ParseExt( &text, qtrue );
			if ( token[0] == '\0' )
			{
				break;
			}
			if ( Q_stricmp( token, "{" ) == 0 )
			{
				++depth;
				if ( depth == 2 )
				{
					VK_ResetShaderStageDefinition( &stage );
				}
				continue;
			}
			if ( Q_stricmp( token, "}" ) == 0 )
			{
				if ( depth == 2 && !stage.imageName.empty() )
				{
					definition.stages.push_back( stage );
				}
				--depth;
				continue;
			}
			if ( depth == 1 && Q_stricmp( token, "skyParms" ) == 0 )
			{
				definition.skyOuterbox = COM_ParseExt( &text, qtrue );
				definition.skyCloudHeight = static_cast<float>( std::atof( COM_ParseExt( &text, qtrue ) ) );
				COM_ParseExt( &text, qtrue );
				continue;
			}
			if ( depth == 1 && Q_stricmp( token, "fogParms" ) == 0 )
			{
				const char *red = COM_ParseExt( &text, qtrue );
				if ( Q_stricmp( red, "(" ) == 0 )
				{
					red = COM_ParseExt( &text, qtrue );
				}
				definition.fogColor[0] = static_cast<float>( std::atof( red ) );
				definition.fogColor[1] = static_cast<float>( std::atof( COM_ParseExt( &text, qtrue ) ) );
				definition.fogColor[2] = static_cast<float>( std::atof( COM_ParseExt( &text, qtrue ) ) );
				const char *depthToken = COM_ParseExt( &text, qtrue );
				if ( Q_stricmp( depthToken, ")" ) == 0 )
				{
					depthToken = COM_ParseExt( &text, qtrue );
				}
				definition.fogDepth = static_cast<float>( std::atof( depthToken ) );
				definition.hasFog = definition.fogDepth > 0.0f;
				continue;
			}
			if ( depth != 2 )
			{
				continue;
			}

			if ( Q_stricmp( token, "map" ) == 0 || Q_stricmp( token, "clampmap" ) == 0 )
			{
				stage.clampMap = Q_stricmp( token, "clampmap" ) == 0;
				stage.imageName = COM_ParseExt( &text, qtrue );
			}
			else if ( Q_stricmp( token, "videoMap" ) == 0 )
			{
				COM_ParseExt( &text, qtrue );
			}
			else if ( Q_stricmp( token, "blendFunc" ) == 0 )
			{
				const std::string source = COM_ParseExt( &text, qtrue );
				if ( Q_stricmp( source.c_str(), "add" ) == 0 )
				{
					stage.blendMode = VK_BLEND_ADDITIVE;
				}
				else if ( Q_stricmp( source.c_str(), "blend" ) == 0 )
				{
					stage.blendMode = VK_BLEND_ALPHA;
				}
				else if ( Q_stricmp( source.c_str(), "filter" ) == 0 )
				{
					stage.blendMode = VK_BLEND_MODULATE;
				}
				else
				{
					const std::string destination = COM_ParseExt( &text, qtrue );
					if ( Q_stricmp( source.c_str(), "GL_SRC_ALPHA" ) == 0 &&
						 Q_stricmp( destination.c_str(), "GL_ONE_MINUS_SRC_ALPHA" ) == 0 )
					{
						stage.blendMode = VK_BLEND_ALPHA;
					}
					else if ( Q_stricmp( source.c_str(), "GL_ONE" ) == 0 &&
						 Q_stricmp( destination.c_str(), "GL_ONE" ) == 0 )
					{
						stage.blendMode = VK_BLEND_ADDITIVE;
					}
					else if ( Q_stricmp( source.c_str(), "GL_SRC_ALPHA" ) == 0 &&
						 Q_stricmp( destination.c_str(), "GL_ONE" ) == 0 )
					{
						stage.blendMode = VK_BLEND_SOURCE_ALPHA_ADDITIVE;
					}
					else if ( Q_stricmp( source.c_str(), "GL_ONE" ) == 0 &&
						 Q_stricmp( destination.c_str(), "GL_ZERO" ) == 0 )
					{
						stage.blendMode = VK_BLEND_OPAQUE;
					}
					else if ( Q_stricmp( source.c_str(), "GL_ONE" ) == 0 &&
						 Q_stricmp( destination.c_str(), "GL_SRC_ALPHA" ) == 0 )
					{
						stage.blendMode = VK_BLEND_ONE_SOURCE_ALPHA;
					}
					else if ( Q_stricmp( source.c_str(), "GL_DST_COLOR" ) == 0 &&
						 Q_stricmp( destination.c_str(), "GL_ONE" ) == 0 )
					{
						stage.blendMode = VK_BLEND_DESTINATION_COLOR_ADDITIVE;
					}
					else if ( Q_stricmp( source.c_str(), "GL_ONE_MINUS_DST_ALPHA" ) == 0 &&
						 Q_stricmp( destination.c_str(), "GL_ONE" ) == 0 )
					{
						stage.blendMode = VK_BLEND_ONE_MINUS_DESTINATION_ALPHA_ADDITIVE;
					}
					else if ( Q_stricmp( source.c_str(), "GL_DST_COLOR" ) == 0 &&
						 Q_stricmp( destination.c_str(), "GL_ZERO" ) == 0 )
					{
						stage.blendMode = VK_BLEND_MODULATE;
					}
					else if ( Q_stricmp( source.c_str(), "GL_DST_COLOR" ) == 0 &&
						 Q_stricmp( destination.c_str(), "GL_SRC_COLOR" ) == 0 )
					{
						stage.blendMode = VK_BLEND_DOUBLE_MODULATE;
					}
					else
					{
						stage.blendMode = VK_BLEND_ALPHA;
					}
				}
			}
			else if ( Q_stricmp( token, "alphaGen" ) == 0 )
			{
				const char *generator = COM_ParseExt( &text, qtrue );
				if ( Q_stricmp( generator, "const" ) == 0 )
				{
					stage.alpha = static_cast<float>( std::atof( COM_ParseExt( &text, qtrue ) ) );
				}
			}
			else if ( Q_stricmp( token, "alphaFunc" ) == 0 )
			{
				const char *function = COM_ParseExt( &text, qtrue );
				if ( Q_stricmp( function, "GT0" ) == 0 )
				{
					stage.alphaTest = VK_ALPHA_TEST_GREATER_ZERO;
				}
				else if ( Q_stricmp( function, "LT128" ) == 0 )
				{
					stage.alphaTest = VK_ALPHA_TEST_LESS_HALF;
				}
				else if ( Q_stricmp( function, "GE128" ) == 0 )
				{
					stage.alphaTest = VK_ALPHA_TEST_GREATER_EQUAL_HALF;
				}
				else if ( Q_stricmp( function, "GE192" ) == 0 )
				{
					stage.alphaTest = VK_ALPHA_TEST_GREATER_EQUAL_THREE_QUARTER;
				}
			}
			else if ( Q_stricmp( token, "surfaceSprites" ) == 0 )
			{
				const char *type = COM_ParseExt( &text, qtrue );
				if ( Q_stricmp( type, "vertical" ) == 0 )
				{
					stage.surfaceSprite.type = VK_SURFACE_SPRITE_VERTICAL;
				}
				else if ( Q_stricmp( type, "oriented" ) == 0 )
				{
					stage.surfaceSprite.type = VK_SURFACE_SPRITE_ORIENTED;
				}
				else if ( Q_stricmp( type, "effect" ) == 0 )
				{
					stage.surfaceSprite.type = VK_SURFACE_SPRITE_EFFECT;
				}
				else if ( Q_stricmp( type, "flattened" ) == 0 )
				{
					stage.surfaceSprite.type = VK_SURFACE_SPRITE_FLATTENED;
				}
				stage.surfaceSprite.width = static_cast<float>( std::atof( COM_ParseExt( &text, qtrue ) ) );
				stage.surfaceSprite.height = static_cast<float>( std::atof( COM_ParseExt( &text, qtrue ) ) );
				stage.surfaceSprite.density = static_cast<float>( std::atof( COM_ParseExt( &text, qtrue ) ) );
				stage.surfaceSprite.fadeDist = static_cast<float>( std::atof( COM_ParseExt( &text, qtrue ) ) );
				stage.surfaceSprite.fadeMax = stage.surfaceSprite.fadeDist * 1.33f;
			}
			else if ( Q_stricmp( token, "ssFademax" ) == 0 )
			{
				stage.surfaceSprite.fadeMax = static_cast<float>( std::atof( COM_ParseExt( &text, qtrue ) ) );
			}
			else if ( Q_stricmp( token, "ssFadescale" ) == 0 )
			{
				stage.surfaceSprite.fadeScale = static_cast<float>( std::atof( COM_ParseExt( &text, qtrue ) ) );
			}
			else if ( Q_stricmp( token, "ssVariance" ) == 0 )
			{
				stage.surfaceSprite.variance[0] = static_cast<float>( std::atof( COM_ParseExt( &text, qtrue ) ) );
				stage.surfaceSprite.variance[1] = static_cast<float>( std::atof( COM_ParseExt( &text, qtrue ) ) );
			}
			else if ( Q_stricmp( token, "ssWind" ) == 0 )
			{
				stage.surfaceSprite.wind = static_cast<float>( std::atof( COM_ParseExt( &text, qtrue ) ) );
				if ( stage.surfaceSprite.windIdle <= 0.0f )
				{
					stage.surfaceSprite.windIdle = stage.surfaceSprite.wind;
				}
			}
			else if ( Q_stricmp( token, "ssWindIdle" ) == 0 )
			{
				stage.surfaceSprite.windIdle = static_cast<float>( std::atof( COM_ParseExt( &text, qtrue ) ) );
			}
			else if ( Q_stricmp( token, "ssVertSkew" ) == 0 )
			{
				stage.surfaceSprite.vertSkew = static_cast<float>( std::atof( COM_ParseExt( &text, qtrue ) ) );
			}
			else if ( Q_stricmp( token, "ssFXDuration" ) == 0 )
			{
				stage.surfaceSprite.fxDuration = static_cast<float>( std::atof( COM_ParseExt( &text, qtrue ) ) );
			}
			else if ( Q_stricmp( token, "ssFXGrow" ) == 0 )
			{
				stage.surfaceSprite.fxGrow[0] = static_cast<float>( std::atof( COM_ParseExt( &text, qtrue ) ) );
				stage.surfaceSprite.fxGrow[1] = static_cast<float>( std::atof( COM_ParseExt( &text, qtrue ) ) );
			}
			else if ( Q_stricmp( token, "ssFXAlphaRange" ) == 0 )
			{
				stage.surfaceSprite.fxAlphaStart = static_cast<float>( std::atof( COM_ParseExt( &text, qtrue ) ) );
				stage.surfaceSprite.fxAlphaEnd = static_cast<float>( std::atof( COM_ParseExt( &text, qtrue ) ) );
			}
			else if ( Q_stricmp( token, "ssFXWeather" ) == 0 )
			{
				stage.surfaceSprite.weatherAffected = true;
			}
			else if ( Q_stricmp( token, "ssHangdown" ) == 0 )
			{
				stage.surfaceSprite.facing = VK_SURFACE_SPRITE_FACING_DOWN;
			}
			else if ( Q_stricmp( token, "ssAnyangle" ) == 0 )
			{
				stage.surfaceSprite.facing = VK_SURFACE_SPRITE_FACING_ANY;
			}
			else if ( Q_stricmp( token, "ssFaceup" ) == 0 )
			{
				stage.surfaceSprite.facing = VK_SURFACE_SPRITE_FACING_UP;
			}
			else if ( Q_stricmp( token, "depthWrite" ) == 0 )
			{
				stage.depthWrite = true;
			}
			else if ( Q_stricmp( token, "rgbGen" ) == 0 )
			{
				const char *generator = COM_ParseExt( &text, qtrue );
				if ( Q_stricmp( generator, "vertex" ) == 0 ||
					 Q_stricmp( generator, "exactVertex" ) == 0 )
				{
					stage.vertexColor = true;
				}
				else if ( Q_stricmp( generator, "const" ) == 0 )
				{
					const char *red = COM_ParseExt( &text, qtrue );
					if ( Q_stricmp( red, "(" ) == 0 )
					{
						red = COM_ParseExt( &text, qtrue );
					}
					stage.color[0] = static_cast<float>( std::atof( red ) );
					stage.color[1] = static_cast<float>( std::atof( COM_ParseExt( &text, qtrue ) ) );
					stage.color[2] = static_cast<float>( std::atof( COM_ParseExt( &text, qtrue ) ) );
				}
			}
			else if ( Q_stricmp( token, "tcMod" ) == 0 )
			{
				const char *operation = COM_ParseExt( &text, qtrue );
				if ( Q_stricmp( operation, "scroll" ) == 0 )
				{
					stage.scroll[0] = static_cast<float>( std::atof( COM_ParseExt( &text, qtrue ) ) );
					stage.scroll[1] = static_cast<float>( std::atof( COM_ParseExt( &text, qtrue ) ) );
				}
				else if ( Q_stricmp( operation, "stretch" ) == 0 )
				{
					stage.stretchType = VK_ParseWaveform( COM_ParseExt( &text, qtrue ) );
					for ( float &parameter : stage.stretch )
					{
						parameter = static_cast<float>( std::atof( COM_ParseExt( &text, qtrue ) ) );
					}
				}
				else if ( Q_stricmp( operation, "turb" ) == 0 )
				{
					COM_ParseExt( &text, qtrue );
					stage.turbulence[0] = static_cast<float>( std::atof( COM_ParseExt( &text, qtrue ) ) );
					stage.turbulence[1] = static_cast<float>( std::atof( COM_ParseExt( &text, qtrue ) ) );
					stage.turbulence[2] = static_cast<float>( std::atof( COM_ParseExt( &text, qtrue ) ) );
				}
			}
		}

		if ( !definition.stages.empty() || !definition.skyOuterbox.empty() || definition.hasFog )
		{
			vk.shaderDefinitions.push_back( definition );
		}
	}
	COM_EndParseSession();
	ri.FS_FreeFile( buffer );
}

static void VK_LoadShaderDefinitions()
{
	if ( vk.shaderDefinitionsLoaded )
	{
		return;
	}
	vk.shaderDefinitionsLoaded = true;

	int fileCount = 0;
	char **files = ri.FS_ListFiles( "shaders", ".shader", &fileCount );
	for ( int i = 0; files != nullptr && i < fileCount; ++i )
	{
		char filename[MAX_QPATH];
		Com_sprintf( filename, sizeof( filename ), "shaders/%s", files[i] );
		VK_ParseShaderFile( filename );
	}
	if ( files != nullptr )
	{
		ri.FS_FreeFileList( files );
	}
	ri.Printf( PRINT_ALL, "rd-vulkan: indexed %zu shader materials\n", vk.shaderDefinitions.size() );
}

static const vk_shader_definition_t *VK_FindShaderDefinition( const char *name )
{
	VK_LoadShaderDefinitions();
	char strippedName[MAX_QPATH];
	COM_StripExtension( name, strippedName, sizeof( strippedName ) );
	for ( auto it = vk.shaderDefinitions.rbegin(); it != vk.shaderDefinitions.rend(); ++it )
	{
		if ( Q_stricmp( it->name.c_str(), strippedName ) == 0 )
		{
			return &*it;
		}
	}
	return nullptr;
}

static qhandle_t VK_FindOrLoadImage( const char *name )
{
	if ( Q_stricmp( name, "$whiteimage" ) == 0 || Q_stricmp( name, "white" ) == 0 )
	{
		return 1;
	}
	for ( const vk_texture_name_t &loaded : vk.imageNames )
	{
		if ( Q_stricmp( loaded.name.c_str(), name ) == 0 )
		{
			return loaded.handle;
		}
	}
	if ( vk.textures.size() >= 4096 )
	{
		return 2;
	}

	byte *pixels = nullptr;
	int width = 0;
	int height = 0;
	R_LoadImage( name, &pixels, &width, &height );
	if ( pixels == nullptr || width <= 0 || height <= 0 )
	{
		return 2;
	}

	vk_texture_t texture = {};
	const bool created = VK_CreateTextureFromPixels(
		pixels,
		static_cast<uint32_t>( width ),
		static_cast<uint32_t>( height ),
		&texture );
	R_Free( pixels );
	if ( !created )
	{
		return 2;
	}

	const qhandle_t handle = static_cast<qhandle_t>( vk.textures.size() );
	vk.textures.push_back( texture );
	vk.materials.emplace_back();
	vk.imageNames.push_back( { name, handle } );
	return handle;
}

static bool VK_CreateXrSession()
{
	XrGraphicsBindingVulkan2KHR graphicsBinding = {};
	graphicsBinding.type = XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR;
	graphicsBinding.instance = vk.instance;
	graphicsBinding.physicalDevice = vk.physicalDevice;
	graphicsBinding.device = vk.device;
	graphicsBinding.queueFamilyIndex = vk.queueFamilyIndex;
	graphicsBinding.queueIndex = vk.queueIndex;

	XrSessionCreateInfo sessionInfo = {};
	sessionInfo.type = XR_TYPE_SESSION_CREATE_INFO;
	sessionInfo.next = &graphicsBinding;
	sessionInfo.systemId = vk.xrSystemId;

	return VK_CheckXr( xrCreateSession( vk.xrInstance, &sessionInfo, &vk.xrSession ), "xrCreateSession" );
}

static bool VK_CreateReferenceSpace( XrReferenceSpaceType type, XrSpace *space, const char *name )
{
	XrReferenceSpaceCreateInfo createInfo = {};
	createInfo.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
	createInfo.referenceSpaceType = type;
	createInfo.poseInReferenceSpace.orientation.w = 1.0f;

	if ( !VK_CheckXr( xrCreateReferenceSpace( vk.xrSession, &createInfo, space ), name ) )
	{
		*space = XR_NULL_HANDLE;
		return false;
	}

	return true;
}

static bool VK_QueryViewConfiguration()
{
	uint32_t viewCount = 0;
	if ( !VK_CheckXr( xrEnumerateViewConfigurationViews(
			vk.xrInstance,
			vk.xrSystemId,
			XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
			0,
			&viewCount,
			nullptr ), "xrEnumerateViewConfigurationViews" ) )
	{
		return false;
	}

	if ( viewCount < 2 )
	{
		ri.Printf( PRINT_WARNING, "rd-vulkan: OpenXR runtime returned %u stereo views\n", viewCount );
		return false;
	}

	for ( XrViewConfigurationView &view : vk.viewConfiguration )
	{
		view.type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
		view.next = nullptr;
	}

	return VK_CheckXr( xrEnumerateViewConfigurationViews(
		vk.xrInstance,
		vk.xrSystemId,
		XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
		ARRAY_LEN( vk.viewConfiguration ),
		&viewCount,
		vk.viewConfiguration ), "xrEnumerateViewConfigurationViews" );
}

static bool VK_SelectSwapchainFormat()
{
	uint32_t formatCount = 0;
	if ( !VK_CheckXr( xrEnumerateSwapchainFormats( vk.xrSession, 0, &formatCount, nullptr ),
			"xrEnumerateSwapchainFormats" ) )
	{
		return false;
	}

	if ( formatCount == 0 )
	{
		ri.Printf( PRINT_WARNING, "rd-vulkan: OpenXR runtime returned no swapchain formats\n" );
		return false;
	}

	std::vector<int64_t> formats( formatCount );
	if ( !VK_CheckXr( xrEnumerateSwapchainFormats( vk.xrSession, formatCount, &formatCount, formats.data() ),
			"xrEnumerateSwapchainFormats" ) )
	{
		return false;
	}

	const int64_t preferredFormats[] = {
		VK_FORMAT_R8G8B8A8_SRGB,
		VK_FORMAT_B8G8R8A8_SRGB,
		VK_FORMAT_R8G8B8A8_UNORM,
		VK_FORMAT_B8G8R8A8_UNORM,
	};

	for ( int64_t preferredFormat : preferredFormats )
	{
		if ( std::find( formats.begin(), formats.end(), preferredFormat ) != formats.end() )
		{
			vk.colorFormat = preferredFormat;
			ri.Printf( PRINT_ALL, "rd-vulkan: color mode=srgb-linear textureFormat=%d swapchainFormat=%lld\n",
				static_cast<int>( VK_FORMAT_R8G8B8A8_SRGB ), static_cast<long long>( vk.colorFormat ) );
			return true;
		}
	}

	vk.colorFormat = formats[0];
	ri.Printf( PRINT_WARNING, "rd-vulkan: using first runtime swapchain format %lld\n",
		static_cast<long long>( vk.colorFormat ) );
	return true;
}

static bool VK_CreateEyeSwapchain( int eye )
{
	XrSwapchainCreateInfo createInfo = {};
	createInfo.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
	createInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
	createInfo.format = vk.colorFormat;
	createInfo.sampleCount = 1;
	createInfo.width = vk.viewConfiguration[eye].recommendedImageRectWidth;
	createInfo.height = vk.viewConfiguration[eye].recommendedImageRectHeight;
	createInfo.faceCount = 1;
	createInfo.arraySize = 1;
	createInfo.mipCount = 1;

	if ( !VK_CheckXr( xrCreateSwapchain( vk.xrSession, &createInfo, &vk.colorSwapchain[eye] ),
			"xrCreateSwapchain" ) )
	{
		return false;
	}

	if ( !VK_CheckXr( xrEnumerateSwapchainImages( vk.colorSwapchain[eye], 0, &vk.colorImageCount[eye], nullptr ),
			"xrEnumerateSwapchainImages" ) )
	{
		return false;
	}

	vk.colorImages[eye] = static_cast<XrSwapchainImageVulkanKHR *>(
		std::calloc( vk.colorImageCount[eye], sizeof( XrSwapchainImageVulkanKHR ) ) );
	if ( vk.colorImages[eye] == nullptr )
	{
		ri.Printf( PRINT_WARNING, "rd-vulkan: failed to allocate swapchain image headers\n" );
		return false;
	}

	for ( uint32_t i = 0; i < vk.colorImageCount[eye]; ++i )
	{
		vk.colorImages[eye][i].type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR;
		vk.colorImages[eye][i].next = nullptr;
	}

	return VK_CheckXr( xrEnumerateSwapchainImages(
		vk.colorSwapchain[eye],
		vk.colorImageCount[eye],
		&vk.colorImageCount[eye],
		reinterpret_cast<XrSwapchainImageBaseHeader *>( vk.colorImages[eye] ) ), "xrEnumerateSwapchainImages" );
}

static bool VK_CreateShaderModule( const uint32_t *words, size_t wordCount, VkShaderModule *shaderModule )
{
	VkShaderModuleCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = wordCount * sizeof( uint32_t );
	createInfo.pCode = words;
	return VK_CheckVk( vkCreateShaderModule( vk.device, &createInfo, nullptr, shaderModule ), "vkCreateShaderModule" );
}

static bool VK_SelectDepthFormat()
{
	const VkFormat candidates[] = {
		VK_FORMAT_D32_SFLOAT,
		VK_FORMAT_X8_D24_UNORM_PACK32,
		VK_FORMAT_D16_UNORM,
	};

	for ( VkFormat format : candidates )
	{
		VkFormatProperties properties = {};
		vkGetPhysicalDeviceFormatProperties( vk.physicalDevice, format, &properties );
		if ( ( properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT ) != 0 )
		{
			vk.depthFormat = format;
			return true;
		}
	}

	ri.Printf( PRINT_WARNING, "rd-vulkan: selected physical device has no supported depth format\n" );
	return false;
}

static bool VK_CreateRenderPass()
{
	VkAttachmentDescription attachments[2] = {};
	attachments[0].format = static_cast<VkFormat>( vk.colorFormat );
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	attachments[1].format = vk.depthFormat;
	attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference colorReference = {};
	colorReference.attachment = 0;
	colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depthReference = {};
	depthReference.attachment = 1;
	depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorReference;
	subpass.pDepthStencilAttachment = &depthReference;

	VkSubpassDependency dependency = {};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask =
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
		VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
		VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	dependency.dstStageMask =
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
		VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
		VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	dependency.dstAccessMask =
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
		VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

	VkRenderPassCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	createInfo.attachmentCount = ARRAY_LEN( attachments );
	createInfo.pAttachments = attachments;
	createInfo.subpassCount = 1;
	createInfo.pSubpasses = &subpass;
	createInfo.dependencyCount = 1;
	createInfo.pDependencies = &dependency;

	return VK_CheckVk( vkCreateRenderPass( vk.device, &createInfo, nullptr, &vk.renderPass ), "vkCreateRenderPass" );
}

static bool VK_CreatePipeline(
	const uint32_t *vertexWords,
	size_t vertexWordCount,
	const uint32_t *fragmentWords,
	size_t fragmentWordCount,
	VkPipeline *pipeline,
	const char *label,
	vk_blend_mode_t blendMode = VK_BLEND_ALPHA,
	VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	bool depthTest = false,
	bool depthWrite = false,
	bool worldVertexInput = false )
{
	VkShaderModule vertexShader = VK_NULL_HANDLE;
	VkShaderModule fragmentShader = VK_NULL_HANDLE;

	if ( !VK_CreateShaderModule( vertexWords, vertexWordCount, &vertexShader ) ||
		 !VK_CreateShaderModule( fragmentWords, fragmentWordCount, &fragmentShader ) )
	{
		if ( vertexShader != VK_NULL_HANDLE )
		{
			vkDestroyShaderModule( vk.device, vertexShader, nullptr );
		}
		if ( fragmentShader != VK_NULL_HANDLE )
		{
			vkDestroyShaderModule( vk.device, fragmentShader, nullptr );
		}
		return false;
	}

	VkPipelineShaderStageCreateInfo shaderStages[2] = {};
	shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
	shaderStages[0].module = vertexShader;
	shaderStages[0].pName = "main";
	shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	shaderStages[1].module = fragmentShader;
	shaderStages[1].pName = "main";

	VkPipelineVertexInputStateCreateInfo vertexInput = {};
	vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	VkVertexInputBindingDescription vertexBinding = {};
	VkVertexInputAttributeDescription vertexAttributes[4] = {};
	if ( worldVertexInput )
	{
		vertexBinding.binding = 0;
		vertexBinding.stride = sizeof( vk_world_vertex_t );
		vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		vertexAttributes[0].location = 0;
		vertexAttributes[0].binding = 0;
		vertexAttributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		vertexAttributes[0].offset = offsetof( vk_world_vertex_t, position );

		vertexAttributes[1].location = 1;
		vertexAttributes[1].binding = 0;
		vertexAttributes[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
		vertexAttributes[1].offset = offsetof( vk_world_vertex_t, color );

		vertexAttributes[2].location = 2;
		vertexAttributes[2].binding = 0;
		vertexAttributes[2].format = VK_FORMAT_R32G32_SFLOAT;
		vertexAttributes[2].offset = offsetof( vk_world_vertex_t, uv );

		vertexAttributes[3].location = 3;
		vertexAttributes[3].binding = 0;
		vertexAttributes[3].format = VK_FORMAT_R32G32_SFLOAT;
		vertexAttributes[3].offset = offsetof( vk_world_vertex_t, lightmapUv );

		vertexInput.vertexBindingDescriptionCount = 1;
		vertexInput.pVertexBindingDescriptions = &vertexBinding;
		vertexInput.vertexAttributeDescriptionCount = ARRAY_LEN( vertexAttributes );
		vertexInput.pVertexAttributeDescriptions = vertexAttributes;
	}

	VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = topology;

	VkPipelineViewportStateCreateInfo viewportState = {};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo rasterization = {};
	rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterization.polygonMode = VK_POLYGON_MODE_FILL;
	rasterization.cullMode = VK_CULL_MODE_NONE;
	rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterization.lineWidth = 1.0f;

	VkPipelineMultisampleStateCreateInfo multisample = {};
	multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

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
	if ( blendMode == VK_BLEND_ADDITIVE )
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
	else if ( blendMode == VK_BLEND_DESTINATION_COLOR_ADDITIVE )
	{
		colorAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
		colorAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
		colorAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
		colorAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
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

	VkPipelineColorBlendStateCreateInfo colorBlend = {};
	colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlend.attachmentCount = 1;
	colorBlend.pAttachments = &colorAttachment;

	VkPipelineDepthStencilStateCreateInfo depthStencil = {};
	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = depthTest ? VK_TRUE : VK_FALSE;
	depthStencil.depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
	depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	depthStencil.depthBoundsTestEnable = VK_FALSE;
	depthStencil.stencilTestEnable = VK_FALSE;

	VkDynamicState dynamicStates[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
	};
	VkPipelineDynamicStateCreateInfo dynamicState = {};
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount = ARRAY_LEN( dynamicStates );
	dynamicState.pDynamicStates = dynamicStates;

	VkPushConstantRange pushConstant = {};
	pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	pushConstant.offset = 0;
	pushConstant.size = sizeof( float ) * 30;

	if ( vk.pipelineLayout == VK_NULL_HANDLE )
	{
		VkPipelineLayoutCreateInfo layoutInfo = {};
		layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.setLayoutCount = 1;
		layoutInfo.pSetLayouts = &vk.textureSetLayout;
		layoutInfo.pushConstantRangeCount = 1;
		layoutInfo.pPushConstantRanges = &pushConstant;
		if ( !VK_CheckVk( vkCreatePipelineLayout( vk.device, &layoutInfo, nullptr, &vk.pipelineLayout ), "vkCreatePipelineLayout" ) )
		{
			vkDestroyShaderModule( vk.device, vertexShader, nullptr );
			vkDestroyShaderModule( vk.device, fragmentShader, nullptr );
			return false;
		}
	}

	VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.stageCount = ARRAY_LEN( shaderStages );
	pipelineInfo.pStages = shaderStages;
	pipelineInfo.pVertexInputState = &vertexInput;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterization;
	pipelineInfo.pMultisampleState = &multisample;
	pipelineInfo.pDepthStencilState = &depthStencil;
	pipelineInfo.pColorBlendState = &colorBlend;
	pipelineInfo.pDynamicState = &dynamicState;
	pipelineInfo.layout = vk.pipelineLayout;
	pipelineInfo.renderPass = vk.renderPass;
	pipelineInfo.subpass = 0;

	const bool created = VK_CheckVk( vkCreateGraphicsPipelines( vk.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, pipeline ),
		label );

	vkDestroyShaderModule( vk.device, vertexShader, nullptr );
	vkDestroyShaderModule( vk.device, fragmentShader, nullptr );
	return created;
}

static bool VK_CreatePipelines()
{
	return
		VK_CreatePipeline( testPatternVertSpv, ARRAY_LEN( testPatternVertSpv ),
			testPatternFragSpv, ARRAY_LEN( testPatternFragSpv ),
			&vk.pipeline, "vkCreateGraphicsPipelines(test-pattern)" ) &&
		VK_CreatePipeline( rectVertSpv, ARRAY_LEN( rectVertSpv ),
			rectFragSpv, ARRAY_LEN( rectFragSpv ),
			&vk.rectPipeline, "vkCreateGraphicsPipelines(rect)" ) &&
		VK_CreatePipeline( texturedRectVertSpv, ARRAY_LEN( texturedRectVertSpv ),
			texturedRectFragSpv, ARRAY_LEN( texturedRectFragSpv ),
			&vk.texturedRectPipeline, "vkCreateGraphicsPipelines(textured-rect)" ) &&
		VK_CreatePipeline( texturedRectVertSpv, ARRAY_LEN( texturedRectVertSpv ),
			texturedRectFragSpv, ARRAY_LEN( texturedRectFragSpv ),
			&vk.texturedRectOpaquePipeline, "vkCreateGraphicsPipelines(textured-rect-opaque)", VK_BLEND_OPAQUE ) &&
		VK_CreatePipeline( texturedRectVertSpv, ARRAY_LEN( texturedRectVertSpv ),
			texturedRectFragSpv, ARRAY_LEN( texturedRectFragSpv ),
			&vk.texturedRectAdditivePipeline, "vkCreateGraphicsPipelines(textured-rect-additive)", VK_BLEND_ADDITIVE ) &&
		VK_CreatePipeline( texturedRectVertSpv, ARRAY_LEN( texturedRectVertSpv ),
			texturedRectFragSpv, ARRAY_LEN( texturedRectFragSpv ),
			&vk.texturedRectDestinationColorAdditivePipeline,
			"vkCreateGraphicsPipelines(textured-rect-destination-color-additive)", VK_BLEND_DESTINATION_COLOR_ADDITIVE ) &&
		VK_CreatePipeline( texturedRectVertSpv, ARRAY_LEN( texturedRectVertSpv ),
			texturedRectFragSpv, ARRAY_LEN( texturedRectFragSpv ),
			&vk.texturedRectOneMinusDestinationAlphaAdditivePipeline,
			"vkCreateGraphicsPipelines(textured-rect-one-minus-destination-alpha-additive)",
			VK_BLEND_ONE_MINUS_DESTINATION_ALPHA_ADDITIVE ) &&
		VK_CreatePipeline( texturedRectVertSpv, ARRAY_LEN( texturedRectVertSpv ),
			texturedRectFragSpv, ARRAY_LEN( texturedRectFragSpv ),
			&vk.texturedRectModulatePipeline, "vkCreateGraphicsPipelines(textured-rect-modulate)", VK_BLEND_MODULATE ) &&
		VK_CreatePipeline( diagnostic3dVertSpv, ARRAY_LEN( diagnostic3dVertSpv ),
			diagnostic3dFragSpv, ARRAY_LEN( diagnostic3dFragSpv ),
			&vk.diagnostic3dPipeline, "vkCreateGraphicsPipelines(diagnostic-3d)",
			VK_BLEND_OPAQUE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true, true ) &&
		VK_CreatePipeline( worldVertSpv, ARRAY_LEN( worldVertSpv ),
			worldFragSpv, ARRAY_LEN( worldFragSpv ),
			&vk.worldPipeline, "vkCreateGraphicsPipelines(world)",
			VK_BLEND_OPAQUE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true, true, true ) &&
		VK_CreatePipeline( worldVertSpv, ARRAY_LEN( worldVertSpv ),
			worldFragSpv, ARRAY_LEN( worldFragSpv ),
			&vk.worldAlphaPipeline, "vkCreateGraphicsPipelines(world-alpha)",
			VK_BLEND_ALPHA, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true, false, true ) &&
		VK_CreatePipeline( worldVertSpv, ARRAY_LEN( worldVertSpv ),
			worldFragSpv, ARRAY_LEN( worldFragSpv ),
			&vk.worldAlphaDepthWritePipeline, "vkCreateGraphicsPipelines(world-alpha-depth-write)",
			VK_BLEND_ALPHA, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true, true, true ) &&
		VK_CreatePipeline( worldVertSpv, ARRAY_LEN( worldVertSpv ),
			worldFragSpv, ARRAY_LEN( worldFragSpv ),
			&vk.worldAdditivePipeline, "vkCreateGraphicsPipelines(world-additive)",
			VK_BLEND_ADDITIVE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true, false, true ) &&
		VK_CreatePipeline( worldVertSpv, ARRAY_LEN( worldVertSpv ),
			worldFragSpv, ARRAY_LEN( worldFragSpv ),
			&vk.worldSourceAlphaAdditivePipeline,
			"vkCreateGraphicsPipelines(world-source-alpha-additive)",
			VK_BLEND_SOURCE_ALPHA_ADDITIVE,
			VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true, false, true ) &&
		VK_CreatePipeline( worldVertSpv, ARRAY_LEN( worldVertSpv ),
			worldFragSpv, ARRAY_LEN( worldFragSpv ),
			&vk.worldOneSourceAlphaPipeline, "vkCreateGraphicsPipelines(world-one-source-alpha)",
			VK_BLEND_ONE_SOURCE_ALPHA, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true, false, true ) &&
		VK_CreatePipeline( worldVertSpv, ARRAY_LEN( worldVertSpv ),
			worldFragSpv, ARRAY_LEN( worldFragSpv ),
			&vk.worldDestinationColorAdditivePipeline,
			"vkCreateGraphicsPipelines(world-destination-color-additive)",
			VK_BLEND_DESTINATION_COLOR_ADDITIVE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true, false, true ) &&
		VK_CreatePipeline( worldVertSpv, ARRAY_LEN( worldVertSpv ),
			worldFragSpv, ARRAY_LEN( worldFragSpv ),
			&vk.worldOneMinusDestinationAlphaAdditivePipeline,
			"vkCreateGraphicsPipelines(world-one-minus-destination-alpha-additive)",
			VK_BLEND_ONE_MINUS_DESTINATION_ALPHA_ADDITIVE,
			VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true, false, true ) &&
		VK_CreatePipeline( worldVertSpv, ARRAY_LEN( worldVertSpv ),
			worldFragSpv, ARRAY_LEN( worldFragSpv ),
			&vk.worldModulatePipeline, "vkCreateGraphicsPipelines(world-modulate)",
			VK_BLEND_MODULATE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true, false, true ) &&
		VK_CreatePipeline( worldVertSpv, ARRAY_LEN( worldVertSpv ),
			worldFragSpv, ARRAY_LEN( worldFragSpv ),
			&vk.worldDoubleModulatePipeline, "vkCreateGraphicsPipelines(world-double-modulate)",
			VK_BLEND_DOUBLE_MODULATE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true, false, true );
}

static void VK_DestroyEyeDepthResources( int eye )
{
	if ( vk.depthImageViews[eye] != VK_NULL_HANDLE )
	{
		vkDestroyImageView( vk.device, vk.depthImageViews[eye], nullptr );
		vk.depthImageViews[eye] = VK_NULL_HANDLE;
	}
	if ( vk.depthImages[eye] != VK_NULL_HANDLE )
	{
		vkDestroyImage( vk.device, vk.depthImages[eye], nullptr );
		vk.depthImages[eye] = VK_NULL_HANDLE;
	}
	if ( vk.depthMemories[eye] != VK_NULL_HANDLE )
	{
		vkFreeMemory( vk.device, vk.depthMemories[eye], nullptr );
		vk.depthMemories[eye] = VK_NULL_HANDLE;
	}
}

static bool VK_CreateEyeDepthResources( int eye )
{
	VK_DestroyEyeDepthResources( eye );

	VkImageCreateInfo imageInfo = {};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.format = vk.depthFormat;
	imageInfo.extent.width = vk.viewConfiguration[eye].recommendedImageRectWidth;
	imageInfo.extent.height = vk.viewConfiguration[eye].recommendedImageRectHeight;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	if ( !VK_CheckVk( vkCreateImage( vk.device, &imageInfo, nullptr, &vk.depthImages[eye] ),
			"vkCreateImage(depth)" ) )
	{
		return false;
	}

	VkMemoryRequirements memoryRequirements = {};
	vkGetImageMemoryRequirements( vk.device, vk.depthImages[eye], &memoryRequirements );

	uint32_t memoryType = 0;
	if ( !VK_FindMemoryType( memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &memoryType ) )
	{
		VK_DestroyEyeDepthResources( eye );
		return false;
	}

	VkMemoryAllocateInfo allocateInfo = {};
	allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	allocateInfo.allocationSize = memoryRequirements.size;
	allocateInfo.memoryTypeIndex = memoryType;

	if ( !VK_CheckVk( vkAllocateMemory( vk.device, &allocateInfo, nullptr, &vk.depthMemories[eye] ),
			"vkAllocateMemory(depth)" ) ||
		 !VK_CheckVk( vkBindImageMemory( vk.device, vk.depthImages[eye], vk.depthMemories[eye], 0 ),
			"vkBindImageMemory(depth)" ) )
	{
		VK_DestroyEyeDepthResources( eye );
		return false;
	}

	VkImageViewCreateInfo viewInfo = {};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = vk.depthImages[eye];
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = vk.depthFormat;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.layerCount = 1;

	if ( !VK_CheckVk( vkCreateImageView( vk.device, &viewInfo, nullptr, &vk.depthImageViews[eye] ),
			"vkCreateImageView(depth)" ) )
	{
		VK_DestroyEyeDepthResources( eye );
		return false;
	}

	return true;
}

static bool VK_CreateEyeFramebuffers( int eye )
{
	if ( !VK_CreateEyeDepthResources( eye ) )
	{
		return false;
	}

	vk.colorImageViews[eye] = static_cast<VkImageView *>(
		std::calloc( vk.colorImageCount[eye], sizeof( VkImageView ) ) );
	vk.framebuffers[eye] = static_cast<VkFramebuffer *>(
		std::calloc( vk.colorImageCount[eye], sizeof( VkFramebuffer ) ) );

	if ( vk.colorImageViews[eye] == nullptr || vk.framebuffers[eye] == nullptr )
	{
		ri.Printf( PRINT_WARNING, "rd-vulkan: failed to allocate framebuffer metadata\n" );
		return false;
	}

	for ( uint32_t i = 0; i < vk.colorImageCount[eye]; ++i )
	{
		VkImageViewCreateInfo viewInfo = {};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = vk.colorImages[eye][i].image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = static_cast<VkFormat>( vk.colorFormat );
		viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.layerCount = 1;

		if ( !VK_CheckVk( vkCreateImageView( vk.device, &viewInfo, nullptr, &vk.colorImageViews[eye][i] ), "vkCreateImageView" ) )
		{
			return false;
		}

		VkImageView attachments[] = {
			vk.colorImageViews[eye][i],
			vk.depthImageViews[eye],
		};

		VkFramebufferCreateInfo framebufferInfo = {};
		framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferInfo.renderPass = vk.renderPass;
		framebufferInfo.attachmentCount = ARRAY_LEN( attachments );
		framebufferInfo.pAttachments = attachments;
		framebufferInfo.width = vk.viewConfiguration[eye].recommendedImageRectWidth;
		framebufferInfo.height = vk.viewConfiguration[eye].recommendedImageRectHeight;
		framebufferInfo.layers = 1;

		if ( !VK_CheckVk( vkCreateFramebuffer( vk.device, &framebufferInfo, nullptr, &vk.framebuffers[eye][i] ), "vkCreateFramebuffer" ) )
		{
			return false;
		}
	}

	return true;
}

static bool VK_CreateRenderResources()
{
	if ( vk.renderResourcesCreated )
	{
		return true;
	}

	if ( !VK_SelectDepthFormat() ||
		 !VK_CreateRenderPass() ||
		 !VK_CreatePipelines() ||
		 !VK_CreateSkinnedVertexStream() )
	{
		return false;
	}

	for ( int eye = 0; eye < VK_BACKEND_EYE_COUNT; ++eye )
	{
		if ( !VK_CreateEyeFramebuffers( eye ) )
		{
			return false;
		}
	}

	vk.renderResourcesCreated = true;
	return true;
}

static bool VK_CreateSwapchains()
{
	if ( vk.swapchainsCreated )
	{
		return true;
	}

	if ( !VK_SelectSwapchainFormat() )
	{
		return false;
	}

	for ( int eye = 0; eye < VK_BACKEND_EYE_COUNT; ++eye )
	{
		if ( !VK_CreateEyeSwapchain( eye ) )
		{
			return false;
		}
	}

	vk.swapchainsCreated = true;
	return VK_CreateRenderResources();
}

static void VK_HandleSessionStateChanged( const XrEventDataSessionStateChanged &event )
{
	vk.sessionState = event.state;

	switch ( event.state )
	{
	case XR_SESSION_STATE_READY:
	{
		XrSessionBeginInfo beginInfo = {};
		beginInfo.type = XR_TYPE_SESSION_BEGIN_INFO;
		beginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
		if ( VK_CheckXr( xrBeginSession( vk.xrSession, &beginInfo ), "xrBeginSession" ) )
		{
			vk.sessionRunning = true;
		}
		break;
	}
	case XR_SESSION_STATE_STOPPING:
		if ( vk.sessionRunning )
		{
			VK_CheckXr( xrEndSession( vk.xrSession ), "xrEndSession" );
		}
		vk.sessionRunning = false;
		break;
	case XR_SESSION_STATE_EXITING:
	case XR_SESSION_STATE_LOSS_PENDING:
		vk.exitRenderLoop = true;
		vk.sessionRunning = false;
		break;
	default:
		break;
	}
}

static void VK_PollXrEvents()
{
	XrEventDataBuffer event = {};
	event.type = XR_TYPE_EVENT_DATA_BUFFER;

	for (;;)
	{
		const XrResult result = xrPollEvent( vk.xrInstance, &event );
		if ( result == XR_EVENT_UNAVAILABLE )
		{
			return;
		}
		if ( !VK_CheckXr( result, "xrPollEvent" ) )
		{
			return;
		}

		if ( event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED )
		{
			VK_HandleSessionStateChanged( *reinterpret_cast<XrEventDataSessionStateChanged *>( &event ) );
		}

		event = {};
		event.type = XR_TYPE_EVENT_DATA_BUFFER;
	}
}

static void VK_MatrixMultiply( const float a[16], const float b[16], float out[16] )
{
	float result[16] = {};
	for ( int column = 0; column < 4; ++column )
	{
		for ( int row = 0; row < 4; ++row )
		{
			result[column * 4 + row] =
				a[0 * 4 + row] * b[column * 4 + 0] +
				a[1 * 4 + row] * b[column * 4 + 1] +
				a[2 * 4 + row] * b[column * 4 + 2] +
				a[3 * 4 + row] * b[column * 4 + 3];
		}
	}
	std::memcpy( out, result, sizeof( result ) );
}

static void VK_BuildDiagnosticModelMatrix( float matrix[16] )
{
	vec3_t origin;
	VectorMA( vk.worldRefdef.vieworg, 180.0f, vk.worldRefdef.viewaxis[0], origin );
	VectorMA( origin, -18.0f, vk.worldRefdef.viewaxis[2], origin );

	std::memset( matrix, 0, sizeof( float ) * 16 );
	matrix[0] = vk.worldRefdef.viewaxis[0][0];
	matrix[1] = vk.worldRefdef.viewaxis[0][1];
	matrix[2] = vk.worldRefdef.viewaxis[0][2];
	matrix[4] = vk.worldRefdef.viewaxis[1][0];
	matrix[5] = vk.worldRefdef.viewaxis[1][1];
	matrix[6] = vk.worldRefdef.viewaxis[1][2];
	matrix[8] = vk.worldRefdef.viewaxis[2][0];
	matrix[9] = vk.worldRefdef.viewaxis[2][1];
	matrix[10] = vk.worldRefdef.viewaxis[2][2];
	matrix[12] = origin[0];
	matrix[13] = origin[1];
	matrix[14] = origin[2];
	matrix[15] = 1.0f;
}

static void VK_BuildViewMatrix(
	const refdef_t &refdef,
	int eye,
	float matrix[16],
	bool applyStereoSeparation = true )
{
	vec3_t eyeOrigin;
	VectorCopy( refdef.vieworg, eyeOrigin );

	float worldScale = refdef.worldscale > 0.0f ? refdef.worldscale : 33.5f;
	if ( refdef.worldscale <= 0.0f )
	{
		cvar_t *worldScaleCvar = ri.Cvar_Get( "cg_worldScale", "33.5", 0 );
		if ( worldScaleCvar != nullptr && worldScaleCvar->value > 0.0f )
		{
			worldScale = worldScaleCvar->value;
		}
	}

	float separation = 0.0f;
	if ( applyStereoSeparation && ri.TBXR_GetEyeStereoSeparation != nullptr )
	{
		separation = ri.TBXR_GetEyeStereoSeparation( eye );
	}

	if ( applyStereoSeparation && separation == 0.0f )
	{
		const float dx = vk.views[1].pose.position.x - vk.views[0].pose.position.x;
		const float dy = vk.views[1].pose.position.y - vk.views[0].pose.position.y;
		const float dz = vk.views[1].pose.position.z - vk.views[0].pose.position.z;
		const float ipd = std::sqrt( dx * dx + dy * dy + dz * dz );
		separation = ( eye == 0 ? 0.5f : -0.5f ) * ipd * worldScale;
	}

	VectorMA( eyeOrigin, separation, refdef.viewaxis[1], eyeOrigin );

	std::memset( matrix, 0, sizeof( float ) * 16 );
	matrix[0] = -refdef.viewaxis[1][0];
	matrix[4] = -refdef.viewaxis[1][1];
	matrix[8] = -refdef.viewaxis[1][2];
	matrix[12] = DotProduct( eyeOrigin, refdef.viewaxis[1] );

	matrix[1] = refdef.viewaxis[2][0];
	matrix[5] = refdef.viewaxis[2][1];
	matrix[9] = refdef.viewaxis[2][2];
	matrix[13] = -DotProduct( eyeOrigin, refdef.viewaxis[2] );

	matrix[2] = -refdef.viewaxis[0][0];
	matrix[6] = -refdef.viewaxis[0][1];
	matrix[10] = -refdef.viewaxis[0][2];
	matrix[14] = DotProduct( eyeOrigin, refdef.viewaxis[0] );

	matrix[15] = 1.0f;
}

static void VK_BuildProjectionMatrix( const XrFovf &fov, float zNear, float zFar, float matrix[16] )
{
	const float tanLeft = std::tan( fov.angleLeft );
	const float tanRight = std::tan( fov.angleRight );
	const float tanDown = std::tan( fov.angleDown );
	const float tanUp = std::tan( fov.angleUp );
	const float tanWidth = tanRight - tanLeft;
	const float tanHeight = tanUp - tanDown;
	const float depth = zNear - zFar;

	std::memset( matrix, 0, sizeof( float ) * 16 );
	matrix[0] = 2.0f / tanWidth;
	matrix[8] = ( tanRight + tanLeft ) / tanWidth;
	matrix[5] = -2.0f / tanHeight;
	matrix[9] = -( tanUp + tanDown ) / tanHeight;
	matrix[10] = zFar / depth;
	matrix[14] = ( zFar * zNear ) / depth;
	matrix[11] = -1.0f;
}

static void VK_BuildRefdefProjectionMatrix(
	const refdef_t &refdef,
	float zNear,
	float zFar,
	float matrix[16] )
{
	const float tanHalfX = std::tan( DEG2RAD( refdef.fov_x * 0.5f ) );
	const float tanHalfY = std::tan( DEG2RAD( refdef.fov_y * 0.5f ) );
	const float depth = zNear - zFar;
	std::memset( matrix, 0, sizeof( float ) * 16 );
	matrix[0] = tanHalfX > 0.0f ? 1.0f / tanHalfX : 1.0f;
	matrix[5] = tanHalfY > 0.0f ? -1.0f / tanHalfY : -1.0f;
	matrix[10] = zFar / depth;
	matrix[14] = ( zFar * zNear ) / depth;
	matrix[11] = -1.0f;
}

static bool VK_WorldTextureUsable( qhandle_t texture );

static bool VK_WorldHasVisibilityData()
{
	return !vk.world.planes.empty() &&
		!vk.world.nodes.empty() &&
		!vk.world.leafs.empty() &&
		!vk.world.leafSurfaces.empty() &&
		!vk.world.visibility.empty() &&
		vk.world.numClusters > 0 &&
		vk.world.clusterBytes > 0;
}

static int VK_WorldPointInLeaf( const vec3_t point )
{
	if ( vk.world.nodes.empty() || vk.world.planes.empty() || vk.world.leafs.empty() )
	{
		return -1;
	}

	int nodeIndex = 0;
	const size_t maxSteps = vk.world.nodes.size() + vk.world.leafs.size() + 1;
	for ( size_t step = 0; step < maxSteps; ++step )
	{
		if ( nodeIndex < 0 )
		{
			const int leafIndex = -1 - nodeIndex;
			return leafIndex >= 0 && static_cast<size_t>( leafIndex ) < vk.world.leafs.size() ? leafIndex : -1;
		}
		if ( static_cast<size_t>( nodeIndex ) >= vk.world.nodes.size() )
		{
			return -1;
		}

		const vk_world_node_t &node = vk.world.nodes[nodeIndex];
		if ( node.plane < 0 || static_cast<size_t>( node.plane ) >= vk.world.planes.size() )
		{
			return -1;
		}

		const vk_world_plane_t &plane = vk.world.planes[node.plane];
		const float distance =
			point[0] * plane.normal[0] +
			point[1] * plane.normal[1] +
			point[2] * plane.normal[2] -
			plane.dist;
		nodeIndex = node.children[distance > 0.0f ? 0 : 1];
	}

	return -1;
}

static bool VK_WorldClusterVisible( int sourceCluster, int targetCluster )
{
	if ( sourceCluster < 0 || targetCluster < 0 ||
		 static_cast<uint32_t>( sourceCluster ) >= vk.world.numClusters ||
		 static_cast<uint32_t>( targetCluster ) >= vk.world.numClusters )
	{
		return false;
	}

	const size_t byteIndex =
		static_cast<size_t>( sourceCluster ) * vk.world.clusterBytes +
		static_cast<size_t>( targetCluster >> 3 );
	if ( byteIndex >= vk.world.visibility.size() )
	{
		return false;
	}

	return ( vk.world.visibility[byteIndex] & ( 1 << ( targetCluster & 7 ) ) ) != 0;
}

static bool VK_WorldAreaMasked( const refdef_t &refdef, int area )
{
	if ( area < 0 || area >= MAX_MAP_AREAS )
	{
		return false;
	}
	return ( refdef.areamask[area >> 3] & ( 1 << ( area & 7 ) ) ) != 0;
}

static uint32_t VK_WorldMarkLeafSurfaces( const vk_world_leaf_t &leaf )
{
	uint32_t visibleCount = 0;
	for ( int i = 0; i < leaf.numLeafSurfaces; ++i )
	{
		const int leafSurfaceIndex = leaf.firstLeafSurface + i;
		if ( leafSurfaceIndex < 0 || static_cast<size_t>( leafSurfaceIndex ) >= vk.world.leafSurfaces.size() )
		{
			continue;
		}
		const uint32_t surfaceIndex = vk.world.leafSurfaces[leafSurfaceIndex];
		if ( surfaceIndex >= vk.world.visibleSurfaces.size() )
		{
			continue;
		}
		if ( vk.world.visibleSurfaces[surfaceIndex] == 0 )
		{
			vk.world.visibleSurfaces[surfaceIndex] = 1;
			++visibleCount;
		}
	}
	return visibleCount;
}

static const std::vector<byte> *VK_WorldVisibleSurfaceMask( const refdef_t &refdef )
{
	if ( !VK_WorldHasVisibilityData() || vk.world.visibleSurfaces.empty() )
	{
		return nullptr;
	}

	std::fill( vk.world.visibleSurfaces.begin(), vk.world.visibleSurfaces.end(), 0 );

	const int viewLeafIndex = VK_WorldPointInLeaf( refdef.vieworg );
	if ( viewLeafIndex < 0 || static_cast<size_t>( viewLeafIndex ) >= vk.world.leafs.size() )
	{
		return nullptr;
	}

	const vk_world_leaf_t &viewLeaf = vk.world.leafs[viewLeafIndex];
	const int viewCluster = viewLeaf.cluster;
	uint32_t visibleLeafCount = 0;
	uint32_t visibleSurfaceCount = 0;

	if ( viewCluster < 0 || static_cast<uint32_t>( viewCluster ) >= vk.world.numClusters )
	{
		visibleLeafCount = 1;
		visibleSurfaceCount = VK_WorldMarkLeafSurfaces( viewLeaf );
	}
	else
	{
		for ( const vk_world_leaf_t &leaf : vk.world.leafs )
		{
			if ( leaf.cluster < 0 ||
				 !VK_WorldClusterVisible( viewCluster, leaf.cluster ) ||
				 VK_WorldAreaMasked( refdef, leaf.area ) )
			{
				continue;
			}
			++visibleLeafCount;
			visibleSurfaceCount += VK_WorldMarkLeafSurfaces( leaf );
		}
	}

	if ( visibleSurfaceCount == 0 )
	{
		return nullptr;
	}

	if ( !vk.world.loggedVisibility )
	{
		ri.Printf( PRINT_ALL,
			"rd-vulkan-world: PVS active leaf=%d cluster=%d visibleLeaves=%u visibleSurfaces=%u/%u\n",
			viewLeafIndex, viewCluster, visibleLeafCount, visibleSurfaceCount, vk.world.bspSurfaceCount );
		vk.world.loggedVisibility = true;
	}

	return &vk.world.visibleSurfaces;
}

static qhandle_t VK_WorldResolveTexture( qhandle_t shader );

static bool VK_BindWorldTexture(
	qhandle_t texture,
	VkDescriptorSet *boundTexture,
	bool repeat = true )
{
	if ( !VK_WorldTextureUsable( texture ) )
	{
		texture = 1;
	}
	if ( !VK_WorldTextureUsable( texture ) )
	{
		return false;
	}

	VkDescriptorSet descriptorSet = repeat ?
		vk.textures[texture].repeatDescriptorSet : vk.textures[texture].descriptorSet;
	if ( *boundTexture != descriptorSet )
	{
		*boundTexture = descriptorSet;
		vkCmdBindDescriptorSets(
			vk.commandBuffer,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			vk.pipelineLayout,
			0,
			1,
			boundTexture,
			0,
			nullptr );
	}
	return true;
}

static VkPipeline VK_WorldPipelineForBlend( vk_blend_mode_t blendMode, bool depthWrite )
{
	switch ( blendMode )
	{
	case VK_BLEND_ALPHA:
		return depthWrite ? vk.worldAlphaDepthWritePipeline : vk.worldAlphaPipeline;
	case VK_BLEND_ADDITIVE:
		return vk.worldAdditivePipeline;
	case VK_BLEND_SOURCE_ALPHA_ADDITIVE:
		return vk.worldSourceAlphaAdditivePipeline;
	case VK_BLEND_ONE_SOURCE_ALPHA:
		return vk.worldOneSourceAlphaPipeline;
	case VK_BLEND_DESTINATION_COLOR_ADDITIVE:
		return vk.worldDestinationColorAdditivePipeline;
	case VK_BLEND_ONE_MINUS_DESTINATION_ALPHA_ADDITIVE:
		return vk.worldOneMinusDestinationAlphaAdditivePipeline;
	case VK_BLEND_MODULATE:
		return vk.worldModulatePipeline;
	case VK_BLEND_DOUBLE_MODULATE:
		return vk.worldDoubleModulatePipeline;
	case VK_BLEND_OPAQUE:
	default:
		return vk.worldPipeline;
	}
}

static void VK_BindWorldPipeline(
	vk_blend_mode_t blendMode,
	VkPipeline *boundPipeline,
	bool depthWrite = false )
{
	const VkPipeline pipeline = VK_WorldPipelineForBlend( blendMode, depthWrite );
	if ( pipeline != VK_NULL_HANDLE && *boundPipeline != pipeline )
	{
		*boundPipeline = pipeline;
		vkCmdBindPipeline( vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
	}
}

static void VK_PushWorldStage( const vk_material_stage_t *stage, bool useLightmap )
{
	vk_world_stage_push_t push = {};
	push.alpha = 1.0f;
	push.useLightmap = useLightmap ? 1.0f : 0.0f;
	push.uvScale[0] = 1.0f;
	push.uvScale[1] = 1.0f;
	push.color[0] = 1.0f;
	push.color[1] = 1.0f;
	push.color[2] = 1.0f;
	push.color[3] = 1.0f;
	push.flags[0] = stage == nullptr ? 1.0f : 0.0f;
	if ( stage != nullptr )
	{
		const float seconds = static_cast<float>( vk.worldRefdef.time ) * 0.001f;
		if ( stage->stretchType != VK_WAVE_NONE )
		{
			const float wave = stage->stretch[0] + stage->stretch[1] *
				VK_EvaluateWaveform( stage->stretchType, stage->stretch[2] + seconds * stage->stretch[3] );
			if ( std::fabs( wave ) > 0.0001f )
			{
				push.uvScale[0] = 1.0f / wave;
				push.uvScale[1] = 1.0f / wave;
			}
		}
		push.uvOffset[0] = 0.5f - 0.5f * push.uvScale[0] + stage->scroll[0] * seconds;
		push.uvOffset[1] = 0.5f - 0.5f * push.uvScale[1] + stage->scroll[1] * seconds;
		push.alpha = stage->alpha;
		std::memcpy( push.color, stage->color, sizeof( push.color ) );
		push.flags[0] = stage->vertexColor && !useLightmap ? 1.0f : 0.0f;
		push.flags[1] = stage->turbulence[0];
		push.flags[2] = stage->turbulence[1] + seconds * stage->turbulence[2];
		push.flags[3] = static_cast<float>( stage->alphaTest );
	}
	vkCmdPushConstants(
		vk.commandBuffer,
		vk.pipelineLayout,
		VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		sizeof( float ) * 16,
		sizeof( push ),
		&push );
}

static void VK_PushWorldFogStage( vk_alpha_test_t alphaTest )
{
	vk_world_stage_push_t push = {};
	push.alpha = vk.world.globalFogDepth;
	push.uvScale[0] = 1.0f;
	push.uvScale[1] = 1.0f;
	push.color[0] = vk.world.globalFogColor[0];
	push.color[1] = vk.world.globalFogColor[1];
	push.color[2] = vk.world.globalFogColor[2];
	push.color[3] = 1.0f;
	push.flags[3] = 10.0f + static_cast<float>( alphaTest );
	vkCmdPushConstants(
		vk.commandBuffer,
		vk.pipelineLayout,
		VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		sizeof( float ) * 16,
		sizeof( push ),
		&push );
}

static uint32_t VK_RecordBoundIndexedFog(
	qhandle_t shader,
	uint32_t indexCount,
	uint32_t firstIndex,
	VkPipeline *boundPipeline,
	VkDescriptorSet *boundTexture )
{
	if ( !vk.world.hasGlobalFog )
	{
		return 0;
	}

	qhandle_t texture = 0;
	vk_alpha_test_t alphaTest = VK_ALPHA_TEST_NONE;
	if ( shader > 0 && static_cast<size_t>( shader ) < vk.materials.size() )
	{
		const vk_material_t &material = vk.materials[shader];
		const vk_material_stage_t *maskStage = nullptr;
		for ( const vk_material_stage_t &stage : material.stages )
		{
			if ( stage.surfaceSprite.type == VK_SURFACE_SPRITE_NONE && !stage.lightmap )
			{
				maskStage = &stage;
				break;
			}
		}
		if ( maskStage != nullptr )
		{
			texture = maskStage->texture;
			alphaTest = maskStage->alphaTest;
		}
	}
	if ( !VK_WorldTextureUsable( texture ) )
	{
		texture = VK_WorldResolveTexture( shader );
	}
	if ( !VK_WorldTextureUsable( texture ) )
	{
		texture = 1;
	}

	VK_BindWorldPipeline( VK_BLEND_ALPHA, boundPipeline );
	if ( !VK_BindWorldTexture( texture, boundTexture ) )
	{
		return 0;
	}
	VK_PushWorldFogStage( alphaTest );
	vkCmdDrawIndexed( vk.commandBuffer, indexCount, 1, firstIndex, 0, 0 );
	return 1;
}

static uint32_t VK_RecordBoundIndexedShader(
	qhandle_t shader,
	qhandle_t lightmap,
	bool vertexLit,
	uint32_t indexCount,
	uint32_t firstIndex,
	vk_world_pass_t pass,
	VkPipeline *boundPipeline,
	VkDescriptorSet *boundTexture )
{
	if ( shader > 0 && static_cast<size_t>( shader ) < vk.materials.size() &&
		 !vk.materials[shader].stages.empty() )
	{
		const vk_material_t &material = vk.materials[shader];
		uint32_t drawCount = 0;
		bool hasLightmapStage = false;
		for ( const vk_material_stage_t &stage : material.stages )
		{
			hasLightmapStage = hasLightmapStage ||
				( stage.surfaceSprite.type == VK_SURFACE_SPRITE_NONE && stage.lightmap );
		}
		const bool promoteVertexStage = vertexLit && hasLightmapStage;
		bool promoted = false;
		for ( size_t stageIndex = 0; stageIndex < material.stages.size(); ++stageIndex )
		{
			const vk_material_stage_t &stage = material.stages[stageIndex];
			if ( stage.surfaceSprite.type != VK_SURFACE_SPRITE_NONE )
			{
				continue;
			}
			if ( promoteVertexStage && stage.lightmap )
			{
				continue;
			}
			vk_material_stage_t effectiveStage = stage;
			if ( promoteVertexStage && !promoted )
			{
				effectiveStage.blendMode = VK_BLEND_OPAQUE;
				effectiveStage.vertexColor = true;
				promoted = true;
			}
			const bool opaque = effectiveStage.blendMode == VK_BLEND_OPAQUE;
			if ( ( pass == VK_WORLD_PASS_OPAQUE ) != opaque )
			{
				continue;
			}
			const qhandle_t texture = effectiveStage.lightmap ? lightmap : effectiveStage.texture;
			if ( texture == 2 || !VK_WorldTextureUsable( texture ) )
			{
				continue;
			}
			VK_BindWorldPipeline(
				effectiveStage.blendMode, boundPipeline, effectiveStage.depthWrite );
			if ( !VK_BindWorldTexture(
					texture, boundTexture,
					!effectiveStage.lightmap && !effectiveStage.clampMap ) )
			{
				continue;
			}
			VK_PushWorldStage( &effectiveStage, effectiveStage.lightmap );
			vkCmdDrawIndexed( vk.commandBuffer, indexCount, 1, firstIndex, 0, 0 );
			++drawCount;
		}
		return drawCount;
	}

	if ( pass != VK_WORLD_PASS_OPAQUE )
	{
		return 0;
	}
	const qhandle_t texture = VK_WorldResolveTexture( shader );
	if ( texture == 2 || !VK_WorldTextureUsable( texture ) )
	{
		return 0;
	}
	VK_BindWorldPipeline( VK_BLEND_OPAQUE, boundPipeline );
	if ( !VK_BindWorldTexture( texture, boundTexture ) )
	{
		return 0;
	}
	const bool hasLightmap = !vertexLit && lightmap != 2 && VK_WorldTextureUsable( lightmap );
	vk_material_stage_t baseStage = {};
	baseStage.alpha = 1.0f;
	baseStage.color[0] = 1.0f;
	baseStage.color[1] = 1.0f;
	baseStage.color[2] = 1.0f;
	baseStage.color[3] = 1.0f;
	baseStage.vertexColor = !hasLightmap;
	VK_PushWorldStage( &baseStage, false );
	vkCmdDrawIndexed( vk.commandBuffer, indexCount, 1, firstIndex, 0, 0 );

	if ( !hasLightmap )
	{
		return 1;
	}

	VK_BindWorldPipeline( VK_BLEND_MODULATE, boundPipeline );
	if ( !VK_BindWorldTexture( lightmap, boundTexture, false ) )
	{
		return 1;
	}
	vk_material_stage_t lightmapStage = {};
	lightmapStage.alpha = 1.0f;
	lightmapStage.color[0] = 1.0f;
	lightmapStage.color[1] = 1.0f;
	lightmapStage.color[2] = 1.0f;
	lightmapStage.color[3] = 1.0f;
	VK_PushWorldStage( &lightmapStage, true );
	vkCmdDrawIndexed( vk.commandBuffer, indexCount, 1, firstIndex, 0, 0 );
	return 2;
}

static bool VK_ShaderUsesPass(
	qhandle_t shader,
	bool vertexLit,
	vk_world_pass_t pass )
{
	if ( pass == VK_WORLD_PASS_FOG )
	{
		return vk.world.hasGlobalFog;
	}
	if ( shader > 0 && static_cast<size_t>( shader ) < vk.materials.size() &&
		 !vk.materials[shader].stages.empty() )
	{
		const vk_material_t &material = vk.materials[shader];
		bool hasLightmapStage = false;
		for ( const vk_material_stage_t &stage : material.stages )
		{
			hasLightmapStage = hasLightmapStage ||
				( stage.surfaceSprite.type == VK_SURFACE_SPRITE_NONE && stage.lightmap );
		}

		const bool promoteVertexStage = vertexLit && hasLightmapStage;
		bool promoted = false;
		for ( const vk_material_stage_t &stage : material.stages )
		{
			if ( stage.surfaceSprite.type != VK_SURFACE_SPRITE_NONE )
			{
				continue;
			}
			if ( promoteVertexStage && stage.lightmap )
			{
				continue;
			}

			vk_blend_mode_t blendMode = stage.blendMode;
			if ( promoteVertexStage && !promoted )
			{
				blendMode = VK_BLEND_OPAQUE;
				promoted = true;
			}
			if ( ( pass == VK_WORLD_PASS_OPAQUE ) == ( blendMode == VK_BLEND_OPAQUE ) )
			{
				return true;
			}
		}
		return false;
	}

	return pass == VK_WORLD_PASS_OPAQUE;
}

static const vk_model_t *VK_ModelForHandle( qhandle_t handle )
{
	if ( handle <= 0 || static_cast<size_t>( handle ) >= vk.models.size() )
	{
		return nullptr;
	}
	return &vk.models[handle];
}

static void VK_BuildEntityModelMatrix( const refEntity_t &entity, float matrix[16] )
{
	std::memset( matrix, 0, sizeof( float ) * 16 );

	const vec_t ( *axis )[3] = entity.axis;
	bool hasAxis =
		VectorLength( axis[0] ) > 0.0001f &&
		VectorLength( axis[1] ) > 0.0001f &&
		VectorLength( axis[2] ) > 0.0001f;
	vec3_t fallbackAxis[3];
	if ( !hasAxis && entity.ghoul2 != nullptr )
	{
		AnglesToAxis( entity.angles, fallbackAxis );
		axis = fallbackAxis;
		hasAxis = true;
	}
	if ( hasAxis )
	{
		const float scaleX =
			entity.ghoul2 != nullptr && entity.modelScale[0] != 0.0f ? entity.modelScale[0] : 1.0f;
		const float scaleY =
			entity.ghoul2 != nullptr && entity.modelScale[1] != 0.0f ? entity.modelScale[1] : 1.0f;
		const float scaleZ =
			entity.ghoul2 != nullptr && entity.modelScale[2] != 0.0f ? entity.modelScale[2] : 1.0f;
		matrix[0] = axis[0][0] * scaleX;
		matrix[1] = axis[0][1] * scaleX;
		matrix[2] = axis[0][2] * scaleX;
		matrix[4] = axis[1][0] * scaleY;
		matrix[5] = axis[1][1] * scaleY;
		matrix[6] = axis[1][2] * scaleY;
		matrix[8] = axis[2][0] * scaleZ;
		matrix[9] = axis[2][1] * scaleZ;
		matrix[10] = axis[2][2] * scaleZ;
	}
	else
	{
		matrix[0] = 1.0f;
		matrix[5] = 1.0f;
		matrix[10] = 1.0f;
	}

	matrix[12] = entity.origin[0];
	matrix[13] = entity.origin[1];
	matrix[14] = entity.origin[2];
	matrix[15] = 1.0f;
}

static void VK_PushModelMvp( const float view[16], const float projection[16], const refEntity_t &entity )
{
	float model[16] = {};
	float modelView[16] = {};
	float mvp[16] = {};
	VK_BuildEntityModelMatrix( entity, model );
	VK_MatrixMultiply( view, model, modelView );
	VK_MatrixMultiply( projection, modelView, mvp );
	vkCmdPushConstants(
		vk.commandBuffer,
		vk.pipelineLayout,
		VK_SHADER_STAGE_VERTEX_BIT,
		0,
		sizeof( mvp ),
		mvp );
}

static uint32_t VK_RecordInlineModelSurfaces(
	const vk_model_t &model,
	vk_world_pass_t pass,
	VkPipeline *boundPipeline,
	VkDescriptorSet *boundTexture )
{
	if ( model.inlineModelIndex < 0 ||
		 static_cast<size_t>( model.inlineModelIndex ) >= vk.world.inlineModels.size() ||
		 vk.world.vertexBuffer == VK_NULL_HANDLE ||
		 vk.world.indexBuffer == VK_NULL_HANDLE )
	{
		return 0;
	}

	const vk_world_inline_model_t &inlineModel = vk.world.inlineModels[model.inlineModelIndex];
	if ( inlineModel.surfaceCount == 0 )
	{
		return 0;
	}

	const VkDeviceSize offsets[] = { 0 };
	vkCmdBindVertexBuffers( vk.commandBuffer, 0, 1, &vk.world.vertexBuffer, offsets );
	vkCmdBindIndexBuffer( vk.commandBuffer, vk.world.indexBuffer, 0, VK_INDEX_TYPE_UINT32 );

	uint32_t drawCount = 0;
	for ( uint32_t i = 0; i < inlineModel.surfaceCount; ++i )
	{
		const uint32_t surfaceIndex = inlineModel.firstSurface + i;
		if ( surfaceIndex >= vk.world.surfaceBatchIndex.size() )
		{
			continue;
		}
		const uint32_t batchIndex = vk.world.surfaceBatchIndex[surfaceIndex];
		if ( batchIndex >= vk.world.batches.size() )
		{
			continue;
		}
		const vk_world_batch_t &batch = vk.world.batches[batchIndex];
		if ( pass == VK_WORLD_PASS_FOG )
		{
			drawCount += VK_RecordBoundIndexedFog(
				batch.shader, batch.indexCount, batch.firstIndex, boundPipeline, boundTexture );
		}
		else
		{
			drawCount += VK_RecordBoundIndexedShader(
				batch.shader, batch.lightmap, batch.vertexLit, batch.indexCount, batch.firstIndex,
				pass, boundPipeline, boundTexture );
		}
	}
	return drawCount;
}

static const vk_model_surface_t *VK_GLMSurfaceForIndex(
	const vk_model_t &model,
	int surfaceIndex )
{
	for ( const vk_model_surface_t &surface : model.surfaces )
	{
		if ( surface.modelSurfaceIndex == surfaceIndex )
		{
			return &surface;
		}
	}
	return nullptr;
}

static unsigned int VK_GLMEffectiveFlags(
	const vk_model_surface_t &surface,
	const CGhoul2Info *ghoul )
{
	if ( ghoul != nullptr )
	{
		for ( const surfaceInfo_t &surfaceOverride : ghoul->mSlist )
		{
			if ( surfaceOverride.surface == surface.modelSurfaceIndex )
			{
				return static_cast<unsigned int>( surfaceOverride.offFlags );
			}
		}
	}
	return surface.defaultFlags;
}

static bool VK_GLMShouldDrawSurface(
	const vk_model_t &model,
	const vk_model_surface_t &surface,
	const CGhoul2Info *ghoul )
{
	if ( VK_GLMEffectiveFlags( surface, ghoul ) != 0 )
	{
		return false;
	}

	int parentIndex = surface.parentSurfaceIndex;
	for ( size_t depth = 0; depth < model.surfaces.size() && parentIndex >= 0; ++depth )
	{
		const vk_model_surface_t *parent = VK_GLMSurfaceForIndex( model, parentIndex );
		if ( parent == nullptr )
		{
			return true;
		}
		const unsigned int parentFlags = VK_GLMEffectiveFlags( *parent, ghoul );
		if ( ( parentFlags & G2SURFACEFLAG_NODESCENDANTS ) != 0 )
		{
			return false;
		}
		parentIndex = parent->parentSurfaceIndex;
	}
	return true;
}

static const vk_skin_surface_t *VK_FindSkinSurface(
	qhandle_t skinHandle,
	const std::string &surfaceName )
{
	if ( skinHandle <= 0 || static_cast<size_t>( skinHandle ) >= vk.skins.size() )
	{
		return nullptr;
	}
	for ( const vk_skin_surface_t &skinSurface : vk.skins[skinHandle].surfaces )
	{
		if ( Q_stricmp( skinSurface.name.c_str(), surfaceName.c_str() ) == 0 )
		{
			return &skinSurface;
		}
	}
	return nullptr;
}

static void VK_LogGhoul2RenderAudit(
	const vk_model_t &model,
	const CGhoul2Info &ghoul,
	qhandle_t skinHandle )
{
	if ( vk.loggedGhoul2RenderAudits.size() >= 64 ||
		 std::find(
			vk.loggedGhoul2RenderAudits.begin(),
			vk.loggedGhoul2RenderAudits.end(),
			&ghoul ) != vk.loggedGhoul2RenderAudits.end() )
	{
		return;
	}

	size_t visible = 0;
	size_t hidden = 0;
	size_t bolts = 0;
	size_t skinMapped = 0;
	size_t skinOff = 0;
	size_t unresolvedVisible = 0;
	for ( const vk_model_surface_t &surface : model.surfaces )
	{
		if ( ( surface.defaultFlags & G2SURFACEFLAG_ISBOLT ) != 0 )
		{
			++bolts;
			continue;
		}
		if ( !VK_GLMShouldDrawSurface( model, surface, &ghoul ) )
		{
			++hidden;
			continue;
		}
		++visible;
		const vk_skin_surface_t *skinSurface =
			VK_FindSkinSurface( skinHandle, surface.name );
		if ( skinSurface != nullptr )
		{
			if ( skinSurface->off )
			{
				++skinOff;
				continue;
			}
			++skinMapped;
		}
		const qhandle_t shader =
			skinSurface != nullptr && skinSurface->shader > 0
				? skinSurface->shader
				: surface.shader;
		if ( shader <= 1 || VK_WorldResolveTexture( shader ) == 2 )
		{
			++unresolvedVisible;
		}
	}

	const char *skinName =
		skinHandle > 0 && static_cast<size_t>( skinHandle ) < vk.skins.size()
			? vk.skins[skinHandle].name.c_str()
			: "<none>";
	ri.Printf(
		unresolvedVisible > 0 ? PRINT_WARNING : PRINT_ALL,
		"rd-vulkan-ghoul2-render-audit: model=%s skin=%s surfaces=%zu visible=%zu "
		"hidden=%zu bolts=%zu skinMapped=%zu skinOff=%zu unresolvedVisible=%zu\n",
		model.name.c_str(),
		skinName,
		model.surfaces.size(),
		visible,
		hidden,
		bolts,
		skinMapped,
		skinOff,
		unresolvedVisible );
	vk.loggedGhoul2RenderAudits.push_back( &ghoul );
}

struct vk_g2_frame_t
{
	int current;
	int next;
	float lerp;
};

static const mdxaBone_t vkGhoul2DefaultRootMatrix = {
	{
		{ 0.0f, -1.0f, 0.0f, 0.0f },
		{ 1.0f, 0.0f, 0.0f, 0.0f },
		{ 0.0f, 0.0f, 1.0f, 0.0f },
	},
};

static void VK_MultiplyBoneMatrices(
	const mdxaBone_t &parent,
	const mdxaBone_t &local,
	mdxaBone_t *result )
{
	for ( int row = 0; row < 3; ++row )
	{
		for ( int column = 0; column < 3; ++column )
		{
			result->matrix[row][column] =
				parent.matrix[row][0] * local.matrix[0][column] +
				parent.matrix[row][1] * local.matrix[1][column] +
				parent.matrix[row][2] * local.matrix[2][column];
		}
		result->matrix[row][3] =
			parent.matrix[row][0] * local.matrix[0][3] +
			parent.matrix[row][1] * local.matrix[1][3] +
			parent.matrix[row][2] * local.matrix[2][3] +
			parent.matrix[row][3];
	}
}

static bool VK_DecompressGLABone(
	const vk_gla_t &animation,
	int frame,
	int bone,
	mdxaBone_t *matrix )
{
	if ( frame < 0 || frame >= animation.numFrames ||
		 bone < 0 || static_cast<size_t>( bone ) >= animation.bones.size() )
	{
		return false;
	}

	const size_t indexOffset =
		static_cast<size_t>( animation.ofsFrames ) +
		( static_cast<size_t>( frame ) * animation.bones.size() +
		  static_cast<size_t>( bone ) ) * sizeof( mdxaIndex_t );
	if ( !VK_ModelBufferRangeValid(
			indexOffset,
			sizeof( mdxaIndex_t ),
			animation.data.size() ) )
	{
		return false;
	}

	const mdxaIndex_t *index =
		reinterpret_cast<const mdxaIndex_t *>( animation.data.data() + indexOffset );
	const uint32_t poolIndex =
		static_cast<uint32_t>( index->iIndex[0] ) |
		( static_cast<uint32_t>( index->iIndex[1] ) << 8 ) |
		( static_cast<uint32_t>( index->iIndex[2] ) << 16 );
	const size_t compressedOffset =
		static_cast<size_t>( animation.ofsCompBonePool ) +
		static_cast<size_t>( poolIndex ) * sizeof( mdxaCompQuatBone_t );
	if ( !VK_ModelBufferRangeValid(
			compressedOffset,
			sizeof( mdxaCompQuatBone_t ),
			animation.data.size() ) )
	{
		return false;
	}

	const mdxaCompQuatBone_t *compressed =
		reinterpret_cast<const mdxaCompQuatBone_t *>( animation.data.data() + compressedOffset );
	MC_UnCompressQuat( matrix->matrix, compressed->Comp );
	return true;
}

static const boneInfo_t *VK_FindBoneOverride( const CGhoul2Info &ghoul, int boneNumber )
{
	for ( const boneInfo_t &bone : ghoul.mBlist )
	{
		if ( bone.boneNumber == boneNumber )
		{
			return &bone;
		}
	}
	return nullptr;
}

static qhandle_t VK_FindRegisteredModelHandle( const char *name )
{
	if ( name == nullptr || name[0] == '\0' )
	{
		return 0;
	}
	for ( const vk_texture_name_t &registered : vk.modelNames )
	{
		if ( Q_stricmp( registered.name.c_str(), name ) == 0 )
		{
			return registered.handle;
		}
	}
	return 0;
}

static std::shared_ptr<vk_gla_t> VK_ModelAnimation(
	const vk_model_t &model,
	int animationIndex )
{
	if ( animationIndex == 0 )
	{
		return model.animation;
	}

	// Ghoul2 animation indices are offsets within a registration-time GLA bank,
	// not durable renderer model handles. Resolve against the explicitly loaded
	// animation sequence first so later model registrations cannot break the bank.
	for ( size_t i = 0; i < vk.animations.size(); ++i )
	{
		const std::shared_ptr<vk_gla_t> &baseAnimation = vk.animations[i];
		if ( baseAnimation == nullptr ||
			 ( baseAnimation != model.animation &&
			   Q_stricmp( baseAnimation->name.c_str(), model.animationName.c_str() ) != 0 ) )
		{
			continue;
		}
		const size_t selectedIndex = i + static_cast<size_t>( animationIndex );
		if ( selectedIndex < vk.animations.size() &&
			 vk.animations[selectedIndex] != nullptr &&
			 static_cast<int>( vk.animations[selectedIndex]->bones.size() ) == model.boneCount )
		{
			return vk.animations[selectedIndex];
		}
		break;
	}

	qhandle_t animationHandle = model.animationHandle;
	if ( animationHandle <= 0 )
	{
		animationHandle = VK_FindRegisteredModelHandle( model.animationName.c_str() );
	}
	const qhandle_t selectedHandle = animationHandle + animationIndex;
	if ( animationHandle > 0 &&
		 selectedHandle > 0 &&
		 static_cast<size_t>( selectedHandle ) < vk.models.size() )
	{
		const vk_model_t &animationModel = vk.models[selectedHandle];
		if ( animationModel.type == VK_MODEL_GLA &&
			 animationModel.animation != nullptr &&
			 static_cast<int>( animationModel.animation->bones.size() ) == model.boneCount )
		{
			return animationModel.animation;
		}
	}
	return nullptr;
}

static vk_g2_frame_t VK_Ghoul2AnimationFrame(
	const boneInfo_t &bone,
	int time,
	int numFrames )
{
	vk_g2_frame_t result = {};
	if ( numFrames <= 0 )
	{
		return result;
	}

	const int start = VK_ClampValue( bone.startFrame, 0, numFrames - 1 );
	const int end = VK_ClampValue( bone.endFrame, start + 1, numFrames );
	const int sampleTime = bone.pauseTime > 0 ? bone.pauseTime : time;
	float frame =
		static_cast<float>( start ) +
		static_cast<float>( sampleTime - bone.startTime ) * 0.02f * bone.animSpeed;
	const float range = static_cast<float>( end - start );

	if ( ( bone.flags & BONE_ANIM_OVERRIDE_LOOP ) != 0 && range > 0.0f )
	{
		frame = static_cast<float>( start ) +
			std::fmod( std::fmod( frame - static_cast<float>( start ), range ) + range, range );
	}
	else
	{
		frame = VK_ClampValue( frame, static_cast<float>( start ), static_cast<float>( end - 1 ) );
	}

	if ( bone.animSpeed < 0.0f )
	{
		result.current = static_cast<int>( std::ceil( frame ) );
		result.next = result.current - 1;
		result.lerp = static_cast<float>( result.current ) - frame;
		if ( result.next < start )
		{
			result.next = ( bone.flags & BONE_ANIM_OVERRIDE_LOOP ) != 0 ? end - 1 : start;
		}
	}
	else
	{
		result.current = static_cast<int>( std::floor( frame ) );
		result.next = result.current + 1;
		result.lerp = frame - static_cast<float>( result.current );
		if ( result.next >= end )
		{
			result.next = ( bone.flags & BONE_ANIM_OVERRIDE_LOOP ) != 0 ? start : end - 1;
		}
	}
	result.current = VK_ClampValue( result.current, 0, numFrames - 1 );
	result.next = VK_ClampValue( result.next, 0, numFrames - 1 );
	result.lerp = VK_ClampValue( result.lerp, 0.0f, 1.0f );
	return result;
}

static bool VK_EvaluateGhoul2Bones(
	const vk_model_t &model,
	const CGhoul2Info &ghoul,
	int time,
	const mdxaBone_t &rootMatrix,
	std::vector<mdxaBone_t> *finalBones )
{
	const std::shared_ptr<vk_gla_t> selectedAnimation =
		VK_ModelAnimation( model, ghoul.animModelIndexOffset );
	if ( selectedAnimation == nullptr || selectedAnimation->bones.empty() )
	{
		if ( Q_stricmp( model.animationName.c_str(), sDEFAULT_GLA_NAME ) == 0 &&
			 model.boneCount > 0 )
		{
			finalBones->assign( static_cast<size_t>( model.boneCount ), rootMatrix );
			return true;
		}
		return false;
	}

	const vk_gla_t &animation = *selectedAnimation;
	const int defaultFrame = VK_ClampValue( ghoul.mAnimFrameDefault, 0, animation.numFrames - 1 );
	std::vector<vk_g2_frame_t> frames( animation.bones.size() );
	std::vector<byte> evaluationState( animation.bones.size(), 0 );
	finalBones->resize( animation.bones.size() );
	std::function<bool( size_t )> evaluateBone = [&]( size_t boneIndex ) -> bool
	{
		if ( evaluationState[boneIndex] == 2 )
		{
			return true;
		}
		if ( evaluationState[boneIndex] == 1 )
		{
			return false;
		}
		evaluationState[boneIndex] = 1;

		const vk_gla_bone_t &bone = animation.bones[boneIndex];
		vk_g2_frame_t frame = { defaultFrame, defaultFrame, 0.0f };
		if ( bone.parent >= 0 )
		{
			if ( static_cast<size_t>( bone.parent ) >= animation.bones.size() ||
				 !evaluateBone( static_cast<size_t>( bone.parent ) ) )
			{
				return false;
			}
			frame = frames[bone.parent];
		}

		const boneInfo_t *override = VK_FindBoneOverride( ghoul, static_cast<int>( boneIndex ) );
		if ( override != nullptr &&
			 ( override->flags & ( BONE_ANIM_OVERRIDE | BONE_ANIM_OVERRIDE_LOOP ) ) != 0 )
		{
			frame = VK_Ghoul2AnimationFrame( *override, time, animation.numFrames );
		}
		frames[boneIndex] = frame;

		mdxaBone_t current = {};
		mdxaBone_t next = {};
		if ( !VK_DecompressGLABone(
				animation, frame.current, static_cast<int>( boneIndex ), &current ) ||
			 !VK_DecompressGLABone(
				animation, frame.next, static_cast<int>( boneIndex ), &next ) )
		{
			return false;
		}

		mdxaBone_t local = {};
		const float currentWeight = 1.0f - frame.lerp;
		for ( int element = 0; element < 12; ++element )
		{
			reinterpret_cast<float *>( &local )[element] =
				reinterpret_cast<const float *>( &current )[element] * currentWeight +
				reinterpret_cast<const float *>( &next )[element] * frame.lerp;
		}

		if ( bone.parent < 0 )
		{
			VK_MultiplyBoneMatrices(
				rootMatrix,
				local,
				&( *finalBones )[boneIndex] );
		}
		else
		{
			VK_MultiplyBoneMatrices(
				( *finalBones )[bone.parent],
				local,
				&( *finalBones )[boneIndex] );
		}
		evaluationState[boneIndex] = 2;
		return true;
	};

	for ( size_t boneIndex = 0; boneIndex < animation.bones.size(); ++boneIndex )
	{
		if ( !evaluateBone( boneIndex ) )
		{
			return false;
		}
	}
	return true;
}

static void VK_LogCinematicGhoul2State(
	const vk_model_t &model,
	const CGhoul2Info &ghoul )
{
	if ( ghoul.animModelIndexOffset == 0 ||
		 vk.loggedCinematicGhouls.size() >= 48 ||
		 std::find(
			vk.loggedCinematicGhouls.begin(),
			vk.loggedCinematicGhouls.end(),
			&ghoul ) != vk.loggedCinematicGhouls.end() )
	{
		return;
	}

	const std::shared_ptr<vk_gla_t> animation =
		VK_ModelAnimation( model, ghoul.animModelIndexOffset );
	ri.Printf(
		PRINT_ALL,
		"rd-vulkan-ghoul2-state: model=%s anim=%s offset=%d default=%d overrides=%zu\n",
		model.name.c_str(),
		animation != nullptr ? animation->name.c_str() : "<missing>",
		ghoul.animModelIndexOffset,
		ghoul.mAnimFrameDefault,
		ghoul.mBlist.size() );
	if ( animation != nullptr )
	{
		for ( const boneInfo_t &override : ghoul.mBlist )
		{
			if ( override.boneNumber < 0 ||
				 ( override.flags & BONE_ANIM_TOTAL ) == 0 )
			{
				continue;
			}
			const char *boneName =
				static_cast<size_t>( override.boneNumber ) < animation->bones.size()
					? animation->bones[override.boneNumber].name.c_str()
					: "<invalid>";
			ri.Printf(
				PRINT_ALL,
				"rd-vulkan-ghoul2-state:   bone=%s(%d) frames=%d..%d flags=0x%x speed=%.3f start=%d pause=%d\n",
				boneName,
				override.boneNumber,
				override.startFrame,
				override.endFrame,
				override.flags,
				override.animSpeed,
				override.startTime,
				override.pauseTime );
		}
	}
	vk.loggedCinematicGhouls.push_back( &ghoul );
}

static const std::vector<mdxaBone_t> *VK_GetCachedGhoul2Bones(
	const vk_model_t &model,
	const CGhoul2Info &ghoul,
	int time,
	const mdxaBone_t &rootMatrix = vkGhoul2DefaultRootMatrix )
{
	time = VK_G2API_GetTime( time );
	VK_LogCinematicGhoul2State( model, ghoul );
	for ( vk_ghoul2_bone_cache_t &entry : vk.ghoul2BoneCache )
	{
		if ( entry.ghoul == &ghoul && entry.model == &model && entry.time == time &&
			 std::memcmp( &entry.rootMatrix, &rootMatrix, sizeof( rootMatrix ) ) == 0 )
		{
			return entry.valid ? &entry.bones : nullptr;
		}
	}

	vk_ghoul2_bone_cache_t entry = {};
	entry.ghoul = &ghoul;
	entry.model = &model;
	entry.time = time;
	entry.rootMatrix = rootMatrix;
	entry.valid = VK_EvaluateGhoul2Bones(
		model, ghoul, time, rootMatrix, &entry.bones );
	vk.ghoul2BoneCache.push_back( std::move( entry ) );
	const vk_ghoul2_bone_cache_t &stored = vk.ghoul2BoneCache.back();
	return stored.valid ? &stored.bones : nullptr;
}

static vk_ghoul2_skinned_audit_t *VK_FindGhoul2SkinnedAudit(
	const vk_model_t &model,
	const CGhoul2Info &ghoul )
{
	if ( std::find(
			vk.loggedGhoul2SkinnedAudits.begin(),
			vk.loggedGhoul2SkinnedAudits.end(),
			&ghoul ) != vk.loggedGhoul2SkinnedAudits.end() )
	{
		return nullptr;
	}
	for ( vk_ghoul2_skinned_audit_t &audit : vk.ghoul2SkinnedAudits )
	{
		if ( audit.ghoul == &ghoul && audit.model == &model )
		{
			return &audit;
		}
	}

	vk_ghoul2_skinned_audit_t audit = {};
	audit.ghoul = &ghoul;
	audit.model = &model;
	vk.ghoul2SkinnedAudits.push_back( audit );
	return &vk.ghoul2SkinnedAudits.back();
}

static float VK_TriangleAreaSquared(
	const float a[3],
	const float b[3],
	const float c[3] )
{
	vec3_t edge0;
	vec3_t edge1;
	vec3_t cross;
	VectorSubtract( b, a, edge0 );
	VectorSubtract( c, a, edge1 );
	CrossProduct( edge0, edge1, cross );
	return VectorLengthSquared( cross );
}

static void VK_AuditSkinnedGLMSurface(
	const vk_model_t &model,
	const vk_model_surface_t &surface,
	const CGhoul2Info &ghoul,
	const vk_world_vertex_t *vertices )
{
	vk_ghoul2_skinned_audit_t *audit = VK_FindGhoul2SkinnedAudit( model, ghoul );
	if ( audit == nullptr )
	{
		return;
	}

	++audit->surfaces;
	audit->vertices += surface.glmVertices.size();
	audit->triangles += surface.glmIndices.size() / 3;
	for ( size_t vertexIndex = 0; vertexIndex < surface.glmVertices.size(); ++vertexIndex )
	{
		const float *position = vertices[vertexIndex].position;
		if ( !std::isfinite( position[0] ) ||
			 !std::isfinite( position[1] ) ||
			 !std::isfinite( position[2] ) )
		{
			++audit->nonFiniteVertices;
		}
	}

	for ( size_t index = 0; index + 2 < surface.glmIndices.size(); index += 3 )
	{
		const uint32_t i0 = surface.glmIndices[index + 0];
		const uint32_t i1 = surface.glmIndices[index + 1];
		const uint32_t i2 = surface.glmIndices[index + 2];
		if ( i0 >= surface.glmVertices.size() ||
			 i1 >= surface.glmVertices.size() ||
			 i2 >= surface.glmVertices.size() )
		{
			++audit->degenerateTriangles;
			continue;
		}

		const float bindAreaSquared = VK_TriangleAreaSquared(
			surface.glmVertices[i0].vertCoords,
			surface.glmVertices[i1].vertCoords,
			surface.glmVertices[i2].vertCoords );
		const float skinnedAreaSquared = VK_TriangleAreaSquared(
			vertices[i0].position,
			vertices[i1].position,
			vertices[i2].position );
		if ( !std::isfinite( skinnedAreaSquared ) || skinnedAreaSquared < 1.0e-10f )
		{
			++audit->degenerateTriangles;
		}
		if ( bindAreaSquared > 1.0e-8f && std::isfinite( skinnedAreaSquared ) )
		{
			const float areaRatio = skinnedAreaSquared / bindAreaSquared;
			audit->collapsedTriangles += areaRatio < 1.0e-4f ? 1 : 0;
			audit->expandedTriangles += areaRatio > 1.0e4f ? 1 : 0;
		}
	}
}

static void VK_LogGhoul2SkinnedAudit(
	const vk_model_t &model,
	const CGhoul2Info &ghoul )
{
	for ( size_t index = 0; index < vk.ghoul2SkinnedAudits.size(); ++index )
	{
		const vk_ghoul2_skinned_audit_t &audit = vk.ghoul2SkinnedAudits[index];
		if ( audit.ghoul != &ghoul || audit.model != &model )
		{
			continue;
		}
		const bool malformed =
			audit.nonFiniteVertices != 0 || audit.collapsedTriangles != 0 ||
			audit.expandedTriangles != 0;
		ri.Printf(
			malformed ? PRINT_WARNING : PRINT_ALL,
			"rd-vulkan-ghoul2-skinned-audit: model=%s surfaces=%zu vertices=%zu "
			"triangles=%zu nonFinite=%zu degenerate=%zu collapsed=%zu expanded=%zu status=%s\n",
			model.name.c_str(),
			audit.surfaces,
			audit.vertices,
			audit.triangles,
			audit.nonFiniteVertices,
			audit.degenerateTriangles,
			audit.collapsedTriangles,
			audit.expandedTriangles,
			malformed ? "suspect" : "ok" );
		vk.loggedGhoul2SkinnedAudits.push_back( &ghoul );
		vk.ghoul2SkinnedAudits.erase( vk.ghoul2SkinnedAudits.begin() + index );
		return;
	}
}

static bool VK_StreamSkinnedGLMSurface(
	const vk_model_t &model,
	const vk_model_surface_t &surface,
	const CGhoul2Info &ghoul,
	int time,
	const std::vector<mdxaBone_t> &bones,
	VkDeviceSize *vertexOffset )
{
	time = VK_G2API_GetTime( time );
	for ( const vk_ghoul2_surface_cache_t &entry : vk.ghoul2SurfaceCache )
	{
		if ( entry.ghoul == &ghoul && entry.surface == &surface && entry.time == time )
		{
			*vertexOffset = entry.vertexOffset;
			return true;
		}
	}

	if ( surface.glmVertices.size() != surface.glmBaseVertices.size() ||
		 surface.glmVertices.empty() ||
		 vk.skinnedVertexMapped == nullptr )
	{
		if ( !vk.loggedGhoul2StreamInvalid )
		{
			ri.Printf(
				PRINT_WARNING,
				"rd-vulkan-ghoul2: surface %s cannot skin "
				"(packed=%zu base=%zu mapped=%s)\n",
				surface.name.c_str(),
				surface.glmVertices.size(),
				surface.glmBaseVertices.size(),
				vk.skinnedVertexMapped != nullptr ? "yes" : "no" );
			vk.loggedGhoul2StreamInvalid = true;
		}
		return false;
	}

	const VkDeviceSize alignment = 16;
	const VkDeviceSize offset =
		( vk.skinnedVertexOffset + alignment - 1 ) & ~( alignment - 1 );
	const VkDeviceSize byteCount =
		static_cast<VkDeviceSize>( surface.glmBaseVertices.size() * sizeof( vk_world_vertex_t ) );
	if ( offset > vk.skinnedVertexCapacity ||
		 byteCount > vk.skinnedVertexCapacity - offset )
	{
		if ( !vk.loggedGhoul2StreamOverflow )
		{
			ri.Printf( PRINT_WARNING,
				"rd-vulkan-ghoul2: skinned vertex stream exhausted; using static mesh fallback\n" );
			vk.loggedGhoul2StreamOverflow = true;
		}
		return false;
	}

	vk_world_vertex_t *destination =
		reinterpret_cast<vk_world_vertex_t *>( vk.skinnedVertexMapped + offset );
	for ( size_t vertexIndex = 0; vertexIndex < surface.glmVertices.size(); ++vertexIndex )
	{
		const mdxmVertex_t &source = surface.glmVertices[vertexIndex];
		destination[vertexIndex] = surface.glmBaseVertices[vertexIndex];
		destination[vertexIndex].position[0] = 0.0f;
		destination[vertexIndex].position[1] = 0.0f;
		destination[vertexIndex].position[2] = 0.0f;

		const int weightCount = G2_GetVertWeights( &source );
		float totalWeight = 0.0f;
		for ( int weightIndex = 0; weightIndex < weightCount; ++weightIndex )
		{
			const int localBoneIndex = G2_GetVertBoneIndex( &source, weightIndex );
			if ( localBoneIndex < 0 ||
				 static_cast<size_t>( localBoneIndex ) >= surface.glmBoneReferences.size() )
			{
				if ( !vk.loggedGhoul2StreamInvalid )
				{
					ri.Printf(
						PRINT_WARNING,
						"rd-vulkan-ghoul2: surface %s vertex %zu has invalid local bone %d/%zu\n",
						surface.name.c_str(),
						vertexIndex,
						localBoneIndex,
						surface.glmBoneReferences.size() );
					vk.loggedGhoul2StreamInvalid = true;
				}
				return false;
			}
			const int boneIndex = surface.glmBoneReferences[localBoneIndex];
			if ( boneIndex < 0 || static_cast<size_t>( boneIndex ) >= bones.size() )
			{
				if ( !vk.loggedGhoul2StreamInvalid )
				{
					ri.Printf(
						PRINT_WARNING,
						"rd-vulkan-ghoul2: surface %s vertex %zu has invalid skeleton bone %d/%zu\n",
						surface.name.c_str(),
						vertexIndex,
						boneIndex,
						bones.size() );
					vk.loggedGhoul2StreamInvalid = true;
				}
				return false;
			}

			const float weight =
				G2_GetVertBoneWeight( &source, weightIndex, totalWeight, weightCount );
			const mdxaBone_t &bone = bones[boneIndex];
			for ( int component = 0; component < 3; ++component )
			{
				destination[vertexIndex].position[component] += weight * (
					bone.matrix[component][0] * source.vertCoords[0] +
					bone.matrix[component][1] * source.vertCoords[1] +
					bone.matrix[component][2] * source.vertCoords[2] +
					bone.matrix[component][3] );
			}
		}
	}
	VK_AuditSkinnedGLMSurface( model, surface, ghoul, destination );

	vk.skinnedVertexOffset = offset + byteCount;
	*vertexOffset = offset;
	vk_ghoul2_surface_cache_t cacheEntry = {};
	cacheEntry.ghoul = &ghoul;
	cacheEntry.surface = &surface;
	cacheEntry.time = time;
	cacheEntry.vertexOffset = offset;
	vk.ghoul2SurfaceCache.push_back( cacheEntry );
	if ( !vk.loggedGhoul2Skinning )
	{
		ri.Printf( PRINT_ALL,
			"rd-vulkan-ghoul2: CPU skeleton evaluation and Vulkan vertex streaming active\n" );
		vk.loggedGhoul2Skinning = true;
	}
	return true;
}

static bool VK_SkinGLMVertex(
	const vk_model_surface_t &surface,
	const std::vector<mdxaBone_t> &bones,
	uint32_t vertexIndex,
	vec3_t position )
{
	if ( vertexIndex >= surface.glmVertices.size() )
	{
		return false;
	}
	const mdxmVertex_t &source = surface.glmVertices[vertexIndex];
	VectorClear( position );
	const int weightCount = G2_GetVertWeights( &source );
	float totalWeight = 0.0f;
	for ( int weightIndex = 0; weightIndex < weightCount; ++weightIndex )
	{
		const int localBoneIndex = G2_GetVertBoneIndex( &source, weightIndex );
		if ( localBoneIndex < 0 ||
			 static_cast<size_t>( localBoneIndex ) >= surface.glmBoneReferences.size() )
		{
			return false;
		}
		const int boneIndex = surface.glmBoneReferences[localBoneIndex];
		if ( boneIndex < 0 || static_cast<size_t>( boneIndex ) >= bones.size() )
		{
			return false;
		}
		const float weight =
			G2_GetVertBoneWeight( &source, weightIndex, totalWeight, weightCount );
		const mdxaBone_t &bone = bones[boneIndex];
		for ( int component = 0; component < 3; ++component )
		{
			position[component] += weight * (
				bone.matrix[component][0] * source.vertCoords[0] +
				bone.matrix[component][1] * source.vertCoords[1] +
				bone.matrix[component][2] * source.vertCoords[2] +
				bone.matrix[component][3] );
		}
	}
	return true;
}

static bool VK_GetGhoul2BoltMatrix(
	const vk_model_t &model,
	const CGhoul2Info &ghoul,
	const std::vector<mdxaBone_t> &bones,
	int boltIndex,
	const vec3_t scale,
	mdxaBone_t *bolt )
{
	if ( bolt == nullptr || boltIndex < 0 ||
		 static_cast<size_t>( boltIndex ) >= ghoul.mBltlist.size() )
	{
		return false;
	}

	const boltInfo_t &boltInfo = ghoul.mBltlist[boltIndex];
	*bolt = {};
	if ( boltInfo.boneNumber >= 0 &&
		 static_cast<size_t>( boltInfo.boneNumber ) < bones.size() )
	{
		const std::shared_ptr<vk_gla_t> animation =
			VK_ModelAnimation( model, ghoul.animModelIndexOffset );
		if ( animation == nullptr ||
			 static_cast<size_t>( boltInfo.boneNumber ) >= animation->bones.size() )
		{
			return false;
		}
		VK_MultiplyBoneMatrices(
			bones[boltInfo.boneNumber],
			animation->bones[boltInfo.boneNumber].basePose,
			bolt );
	}
	else if ( boltInfo.surfaceNumber >= 0 )
	{
		const vk_model_surface_t *surface =
			VK_GLMSurfaceForIndex( model, boltInfo.surfaceNumber );
		if ( surface == nullptr || surface->glmVertices.size() < 3 )
		{
			return false;
		}
		vec3_t triangle[3];
		for ( int i = 0; i < 3; ++i )
		{
			if ( !VK_SkinGLMVertex( *surface, bones, i, triangle[i] ) )
			{
				return false;
			}
		}
		vec3_t sides[3];
		vec3_t axes[3];
		for ( int i = 0; i < 3; ++i )
		{
			VectorSubtract( triangle[( i + 1 ) % 3], triangle[i], sides[i] );
		}
		VectorNormalize2( sides[iG2_TRISIDE_LONGEST], axes[0] );
		VectorNormalize2( sides[iG2_TRISIDE_SHORTEST], axes[1] );
		const float projection = DotProduct( axes[0], axes[1] );
		VectorMA( axes[0], -projection, axes[1], axes[0] );
		VectorNormalize( axes[0] );
		CrossProduct(
			sides[iG2_TRISIDE_LONGEST], sides[iG2_TRISIDE_SHORTEST], axes[2] );
		VectorNormalize( axes[2] );

		for ( int row = 0; row < 3; ++row )
		{
			bolt->matrix[row][0] = axes[1][row];
			bolt->matrix[row][1] = axes[0][row];
			bolt->matrix[row][2] = -axes[2][row];
			bolt->matrix[row][3] = triangle[2][row];
		}
	}
	else
	{
		return false;
	}

	for ( int component = 0; component < 3; ++component )
	{
		if ( scale[component] != 0.0f )
		{
			bolt->matrix[component][3] *= scale[component];
		}
		VectorNormalize( bolt->matrix[component] );
	}
	return true;
}

static void VK_CreateBoneMatrix(
	const vec3_t angles,
	const vec3_t position,
	mdxaBone_t *matrix )
{
	vec3_t axis[3];
	AnglesToAxis( angles, axis );
	for ( int row = 0; row < 3; ++row )
	{
		matrix->matrix[row][0] = axis[0][row];
		matrix->matrix[row][1] = axis[1][row];
		matrix->matrix[row][2] = axis[2][row];
		matrix->matrix[row][3] = position[row];
	}
}

qboolean VK_Backend_GetBoltMatrix(
	CGhoul2Info_v &ghoul2,
	int modelIndex,
	int boltIndex,
	mdxaBone_t *matrix,
	const vec3_t angles,
	const vec3_t position,
	int frameNumber,
	const vec3_t scale )
{
	if ( matrix == nullptr || !ghoul2.IsValid() ||
		 modelIndex < 0 || modelIndex >= ghoul2.size() )
	{
		return qfalse;
	}
	CGhoul2Info &ghoul = ghoul2[modelIndex];
	if ( boltIndex < 0 || static_cast<size_t>( boltIndex ) >= ghoul.mBltlist.size() )
	{
		return qfalse;
	}
	const vk_model_t *model = VK_ModelForHandle( ghoul.mModel );
	if ( model == nullptr || model->type != VK_MODEL_GLM )
	{
		return qfalse;
	}

	std::vector<mdxaBone_t> bones;
	frameNumber = VK_G2API_GetTime( frameNumber );
	if ( !VK_EvaluateGhoul2Bones(
			*model, ghoul, frameNumber, vkGhoul2DefaultRootMatrix, &bones ) )
	{
		return qfalse;
	}

	mdxaBone_t bolt = {};
	if ( !VK_GetGhoul2BoltMatrix( *model, ghoul, bones, boltIndex, scale, &bolt ) )
	{
		return qfalse;
	}

	mdxaBone_t world = {};
	VK_CreateBoneMatrix( angles, position, &world );
	VK_MultiplyBoneMatrices( world, bolt, matrix );
	return qtrue;
}

static uint32_t VK_RecordMD3ModelSurfaces(
	const vk_model_t &model,
	vk_world_pass_t pass,
	VkPipeline *boundPipeline,
	VkDescriptorSet *boundTexture,
	qhandle_t shaderOverride,
	const CGhoul2Info *ghoul,
	qhandle_t skinHandle,
	int sceneTime,
	const std::vector<mdxaBone_t> *bones )
{
	uint32_t drawCount = 0;
	for ( const vk_model_surface_t &surface : model.surfaces )
	{
		if ( surface.vertexBuffer == VK_NULL_HANDLE ||
			 surface.indexBuffer == VK_NULL_HANDLE ||
			 surface.indexCount == 0 )
		{
			continue;
		}
		if ( model.type == VK_MODEL_GLM && !VK_GLMShouldDrawSurface( model, surface, ghoul ) )
		{
			continue;
		}

		const vk_skin_surface_t *skinSurface =
			model.type == VK_MODEL_GLM ? VK_FindSkinSurface( skinHandle, surface.name ) : nullptr;
		if ( skinSurface != nullptr && skinSurface->off )
		{
			continue;
		}
		qhandle_t shader = shaderOverride > 0 ? shaderOverride : surface.shader;
		if ( shaderOverride <= 0 && skinSurface != nullptr && skinSurface->shader > 0 )
		{
			shader = skinSurface->shader;
		}
		if ( !VK_ShaderUsesPass( shader, true, pass ) )
		{
			continue;
		}
		VkBuffer vertexBuffer = surface.vertexBuffer;
		VkDeviceSize vertexOffset = 0;
		if ( ghoul != nullptr && bones != nullptr &&
			 VK_StreamSkinnedGLMSurface(
				model, surface, *ghoul, sceneTime, *bones, &vertexOffset ) )
		{
			vertexBuffer = vk.skinnedVertexBuffer;
		}
		vkCmdBindVertexBuffers( vk.commandBuffer, 0, 1, &vertexBuffer, &vertexOffset );
		vkCmdBindIndexBuffer( vk.commandBuffer, surface.indexBuffer, 0, VK_INDEX_TYPE_UINT32 );
		if ( pass == VK_WORLD_PASS_FOG )
		{
			drawCount += VK_RecordBoundIndexedFog(
				shader, surface.indexCount, 0, boundPipeline, boundTexture );
		}
		else
		{
			drawCount += VK_RecordBoundIndexedShader(
				shader, 2, true, surface.indexCount, 0, pass, boundPipeline, boundTexture );
		}
	}
	if ( pass == VK_WORLD_PASS_TRANSLUCENT && ghoul != nullptr )
	{
		VK_LogGhoul2SkinnedAudit( model, *ghoul );
	}
	return drawCount;
}

static void VK_LogStandaloneWeaponTransform( const refEntity_t &entity )
{
	if ( vk.loggedWeaponOnlyEntities >= 12 || entity.ghoul2 == nullptr ||
		 !entity.ghoul2->IsValid() )
	{
		return;
	}

	bool foundModel = false;
	std::string diagnosticName;
	for ( int i = 0; i < entity.ghoul2->size(); ++i )
	{
		const CGhoul2Info &ghoul = ( *entity.ghoul2 )[i];
		if ( ghoul.mModelindex < 0 ||
			 ( ghoul.mFlags & ( GHOUL2_NOMODEL | GHOUL2_NORENDER ) ) != 0 )
		{
			continue;
		}
		const vk_model_t *model = VK_ModelForHandle( ghoul.mModel );
		if ( model == nullptr || Q_stricmpn( model->name.c_str(), "models/weapons2/", 16 ) != 0 )
		{
			return;
		}
		foundModel = true;
		if ( diagnosticName.empty() )
		{
			diagnosticName = model->name;
		}
	}
	if ( !foundModel )
	{
		return;
	}
	if ( std::find(
			vk.loggedWeaponOnlyModels.begin(),
			vk.loggedWeaponOnlyModels.end(),
			diagnosticName ) != vk.loggedWeaponOnlyModels.end() )
	{
		return;
	}

	ri.Printf( PRINT_ALL,
		"rd-vulkan-weapon-transform: origin=(%.2f %.2f %.2f) angles=(%.2f %.2f %.2f) "
		"scale=(%.2f %.2f %.2f) axis0=(%.3f %.3f %.3f) axis1=(%.3f %.3f %.3f) "
		"axis2=(%.3f %.3f %.3f)\n",
		entity.origin[0], entity.origin[1], entity.origin[2],
		entity.angles[0], entity.angles[1], entity.angles[2],
		entity.modelScale[0], entity.modelScale[1], entity.modelScale[2],
		entity.axis[0][0], entity.axis[0][1], entity.axis[0][2],
		entity.axis[1][0], entity.axis[1][1], entity.axis[1][2],
		entity.axis[2][0], entity.axis[2][1], entity.axis[2][2] );
	for ( int i = 0; i < entity.ghoul2->size(); ++i )
	{
		const CGhoul2Info &ghoul = ( *entity.ghoul2 )[i];
		if ( ghoul.mModelindex < 0 )
		{
			continue;
		}
		const vk_model_t *model = VK_ModelForHandle( ghoul.mModel );
		ri.Printf( PRINT_ALL,
			"rd-vulkan-weapon-transform: model[%d]=%s modelIndex=%d boltLink=%d newOrigin=%d flags=0x%x\n",
			i, model != nullptr ? model->name.c_str() : ghoul.mFileName,
			ghoul.mModelindex, ghoul.mModelBoltLink, ghoul.mNewOrigin, ghoul.mFlags );
	}
	vk.loggedWeaponOnlyModels.push_back( diagnosticName );
	++vk.loggedWeaponOnlyEntities;
}

static const std::vector<mdxaBone_t> *VK_ResolveGhoul2HierarchyBones(
	const CGhoul2Info_v &ghoul2,
	int modelIndex,
	int sceneTime,
	std::vector<byte> *states,
	std::vector<const std::vector<mdxaBone_t> *> *resolvedBones )
{
	if ( states == nullptr || resolvedBones == nullptr ||
		 modelIndex < 0 || modelIndex >= ghoul2.size() )
	{
		return nullptr;
	}
	if ( ( *states )[modelIndex] == 2 )
	{
		return ( *resolvedBones )[modelIndex];
	}
	if ( ( *states )[modelIndex] == 1 )
	{
		return nullptr;
	}
	( *states )[modelIndex] = 1;

	const CGhoul2Info &ghoul = ghoul2[modelIndex];
	const vk_model_t *model = VK_ModelForHandle( ghoul.mModel );
	if ( ghoul.mModelindex < 0 || model == nullptr || model->type != VK_MODEL_GLM )
	{
		( *states )[modelIndex] = 2;
		return nullptr;
	}

	mdxaBone_t rootMatrix = vkGhoul2DefaultRootMatrix;
	if ( ghoul.mModelBoltLink != -1 )
	{
		const int parentModelIndex =
			( ghoul.mModelBoltLink >> MODEL_SHIFT ) & MODEL_AND;
		const int parentBoltIndex =
			( ghoul.mModelBoltLink >> BOLT_SHIFT ) & BOLT_AND;
		const std::vector<mdxaBone_t> *parentBones =
			VK_ResolveGhoul2HierarchyBones(
				ghoul2, parentModelIndex, sceneTime, states, resolvedBones );
		if ( parentBones == nullptr ||
			 parentModelIndex < 0 || parentModelIndex >= ghoul2.size() )
		{
			( *states )[modelIndex] = 2;
			return nullptr;
		}
		const CGhoul2Info &parentGhoul = ghoul2[parentModelIndex];
		const vk_model_t *parentModel = VK_ModelForHandle( parentGhoul.mModel );
		const vec3_t unitScale = { 1.0f, 1.0f, 1.0f };
		if ( parentModel == nullptr ||
			 !VK_GetGhoul2BoltMatrix(
				*parentModel,
				parentGhoul,
				*parentBones,
				parentBoltIndex,
				unitScale,
				&rootMatrix ) )
		{
			( *states )[modelIndex] = 2;
			return nullptr;
		}
	}

	const std::vector<mdxaBone_t> *bones =
		VK_GetCachedGhoul2Bones( *model, ghoul, sceneTime, rootMatrix );
	( *resolvedBones )[modelIndex] = bones;
	( *states )[modelIndex] = 2;
	return bones;
}

static void VK_RecordSceneModels(
	const float view[16],
	const float projection[16],
	vk_world_pass_t pass,
	const std::vector<refEntity_t> &entities,
	bool suppressThirdPerson,
	int sceneTime )
{
	if ( entities.empty() || vk.worldPipeline == VK_NULL_HANDLE )
	{
		return;
	}

	VkPipeline boundPipeline = VK_NULL_HANDLE;
	VkDescriptorSet boundTexture = VK_NULL_HANDLE;
	uint32_t modelEntities = 0;
	uint32_t md3Entities = 0;
	uint32_t glmEntities = 0;
	uint32_t inlineEntities = 0;
	uint32_t unsupportedEntities = 0;
	uint32_t surfaceDraws = 0;
	for ( const refEntity_t &entity : entities )
	{
		if ( entity.reType != RT_MODEL )
		{
			continue;
		}
		if ( suppressThirdPerson && ( entity.renderfx & RF_THIRD_PERSON ) != 0 )
		{
			continue;
		}
		++modelEntities;

		if ( entity.ghoul2 != nullptr && entity.ghoul2->IsValid() )
		{
			if ( suppressThirdPerson && pass == VK_WORLD_PASS_OPAQUE )
			{
				VK_LogStandaloneWeaponTransform( entity );
			}
			VK_PushModelMvp( view, projection, entity );
			bool drewGhoul2 = false;
			std::vector<byte> hierarchyStates(
				static_cast<size_t>( entity.ghoul2->size() ), 0 );
			std::vector<const std::vector<mdxaBone_t> *> hierarchyBones(
				static_cast<size_t>( entity.ghoul2->size() ), nullptr );
			for ( int i = 0; i < entity.ghoul2->size(); ++i )
			{
				const CGhoul2Info &ghoul = ( *entity.ghoul2 )[i];
				if ( ghoul.mModelindex < 0 ||
					 ( ghoul.mFlags & ( GHOUL2_NOMODEL | GHOUL2_NORENDER ) ) != 0 )
				{
					continue;
				}
				const vk_model_t *ghoulModel = VK_ModelForHandle( ghoul.mModel );
				if ( ghoulModel == nullptr || ghoulModel->type != VK_MODEL_GLM )
				{
					continue;
				}
				qhandle_t shaderOverride = entity.customShader;
				if ( shaderOverride <= 0 )
				{
					shaderOverride = ghoul.mCustomShader;
				}
				qhandle_t skinHandle = entity.customSkin;
				if ( skinHandle <= 0 )
				{
					skinHandle = ghoul.mSkin;
				}
				if ( skinHandle <= 0 )
				{
					skinHandle = ghoul.mCustomSkin;
				}
				VK_LogGhoul2RenderAudit( *ghoulModel, ghoul, skinHandle );
				const std::vector<mdxaBone_t> *bonePointer =
					VK_ResolveGhoul2HierarchyBones(
						*entity.ghoul2,
						i,
						sceneTime,
						&hierarchyStates,
						&hierarchyBones );
				const uint32_t draws = VK_RecordMD3ModelSurfaces(
					*ghoulModel,
					pass,
					&boundPipeline,
					&boundTexture,
					shaderOverride,
					&ghoul,
					skinHandle,
					sceneTime,
					bonePointer );
				if ( draws > 0 )
				{
					drewGhoul2 = true;
					surfaceDraws += draws;
				}
			}
			if ( drewGhoul2 )
			{
				++glmEntities;
				continue;
			}
		}

		const vk_model_t *model = VK_ModelForHandle( entity.hModel );
		if ( model == nullptr )
		{
			++unsupportedEntities;
			continue;
		}

		VK_PushModelMvp( view, projection, entity );
		if ( model->type == VK_MODEL_INLINE_BSP )
		{
			const uint32_t draws = VK_RecordInlineModelSurfaces(
				*model, pass, &boundPipeline, &boundTexture );
			if ( draws > 0 )
			{
				++inlineEntities;
				surfaceDraws += draws;
			}
		}
		else if ( model->type == VK_MODEL_MD3 )
		{
			const uint32_t draws = VK_RecordMD3ModelSurfaces(
				*model,
				pass,
				&boundPipeline,
				&boundTexture,
				entity.customShader,
				nullptr,
				entity.customSkin,
				sceneTime,
				nullptr );
			if ( draws > 0 )
			{
				++md3Entities;
				surfaceDraws += draws;
			}
		}
		else if ( model->type == VK_MODEL_GLM )
		{
			const uint32_t draws = VK_RecordMD3ModelSurfaces(
				*model,
				pass,
				&boundPipeline,
				&boundTexture,
				entity.customShader,
				nullptr,
				entity.customSkin,
				sceneTime,
				nullptr );
			if ( draws > 0 )
			{
				++glmEntities;
				surfaceDraws += draws;
			}
		}
		else
		{
			++unsupportedEntities;
		}
	}

	if ( !vk.loggedFirstModelDraw && modelEntities > 0 && pass == VK_WORLD_PASS_TRANSLUCENT )
	{
		ri.Printf( PRINT_ALL,
			"rd-vulkan-scene: translucent model pass: entities=%u md3=%u glm=%u inline=%u unsupported=%u stageDraws=%u\n",
			modelEntities, md3Entities, glmEntities, inlineEntities, unsupportedEntities, surfaceDraws );
		vk.loggedFirstModelDraw = true;
	}
}

static bool VK_DiagnosticWorldEnabled()
{
	return vk.diagnosticWorldCvar == nullptr || vk.diagnosticWorldCvar->integer != 0;
}

static void VK_WriteSurfaceSpriteVertex(
	vk_world_vertex_t *vertex,
	const float position[3],
	const float color[4],
	float u,
	float v )
{
	*vertex = {};
	std::memcpy( vertex->position, position, sizeof( vertex->position ) );
	std::memcpy( vertex->color, color, sizeof( vertex->color ) );
	vertex->uv[0] = u;
	vertex->uv[1] = v;
}

static bool VK_StreamSurfaceSpriteBatch(
	const vk_surface_sprite_batch_t &batch,
	VkDeviceSize *vertexOffset,
	uint32_t *vertexCount )
{
	for ( const vk_surface_sprite_stream_cache_t &cached : vk.surfaceSpriteStreamCache )
	{
		if ( cached.batch == &batch )
		{
			*vertexOffset = cached.vertexOffset;
			*vertexCount = cached.vertexCount;
			return true;
		}
	}

	const VkDeviceSize alignment = 16;
	const VkDeviceSize offset =
		( vk.skinnedVertexOffset + alignment - 1 ) & ~( alignment - 1 );
	if ( offset >= vk.skinnedVertexCapacity || vk.skinnedVertexMapped == nullptr )
	{
		return false;
	}
	const size_t availableVertices = static_cast<size_t>(
		( vk.skinnedVertexCapacity - offset ) / sizeof( vk_world_vertex_t ) );
	vk_world_vertex_t *destination = reinterpret_cast<vk_world_vertex_t *>(
		vk.skinnedVertexMapped + offset );
	size_t writtenVertices = 0;

	const vk_surface_sprite_config_t &config = batch.stage.surfaceSprite;
	const float fadeMax = config.fadeMax > config.fadeDist
		? config.fadeMax
		: config.fadeDist * 1.33f;
	const float fadeRange = std::max( 1.0f, fadeMax - config.fadeDist );
	const float seconds = static_cast<float>( vk.worldRefdef.time ) * 0.001f;
	const float orientationAngles[4] = { 10.0f, -30.0f, 30.0f, 0.0f };

	for ( const vk_surface_sprite_instance_t &instance : batch.instances )
	{
		vec3_t toView;
		VectorSubtract( vk.worldRefdef.vieworg, instance.position, toView );
		const float distance = VectorLength( toView );
		if ( distance >= fadeMax )
		{
			continue;
		}
		float alpha = distance <= config.fadeDist
			? 1.0f
			: ( fadeMax - distance ) / fadeRange;
		const float randomFadeStart = 0.55f + 0.45f *
			( 0.5f + 0.5f * std::sin( instance.phase * 7.0f ) );
		if ( alpha < randomFadeStart )
		{
			alpha /= randomFadeStart;
		}
		alpha = std::max( 0.0f, std::min( 1.0f, alpha ) );
		if ( alpha <= 0.0f )
		{
			continue;
		}
		if ( writtenVertices + 6 > availableVertices )
		{
			if ( !vk.loggedSurfaceSpriteStreamOverflow )
			{
				ri.Printf( PRINT_WARNING,
					"rd-vulkan-surfacesprites: dynamic vertex stream exhausted; some sprites were skipped\n" );
				vk.loggedSurfaceSpriteStreamOverflow = true;
			}
			break;
		}

		float width = instance.width;
		float height = instance.height;
		vec3_t spritePosition;
		VectorCopy( instance.position, spritePosition );
		if ( config.type == VK_SURFACE_SPRITE_EFFECT )
		{
			const float duration = std::max( 1.0f, config.fxDuration );
			float effectPosition = seconds * 1000.0f / duration +
				instance.phase / 6.28318530718f;
			effectPosition -= std::floor( effectPosition );
			width *= 1.0f + effectPosition * config.fxGrow[0];
			height *= 1.0f + effectPosition * config.fxGrow[1];

			const float alphaDelta = config.fxAlphaEnd - config.fxAlphaStart;
			float effectAlpha;
			if ( config.fxAlphaEnd < 0.05f &&
				 std::fabs( instance.width ) >= 0.1f && instance.height >= 0.1f )
			{
				const float envelope = effectPosition > 0.5f
					? ( effectPosition - 0.5f ) * 2.0f
					: ( 0.5f - effectPosition ) * 2.0f;
				effectAlpha = config.fxAlphaStart + alphaDelta * envelope;
			}
			else
			{
				effectAlpha = config.fxAlphaStart + alphaDelta * effectPosition;
			}
			alpha *= std::max( 0.0f, std::min( 1.0f, effectAlpha ) );
			spritePosition[0] += effectPosition * config.wind * 0.8f;
			spritePosition[1] += effectPosition * config.wind * 0.6f;
			if ( alpha <= 0.0f )
			{
				continue;
			}
		}
		if ( config.fadeScale != 0.0f && alpha < 1.0f )
		{
			width *= 1.0f + config.fadeScale * ( 1.0f - alpha );
		}

		vec3_t bottomLeft;
		vec3_t bottomRight;
		vec3_t topLeft;
		vec3_t topRight;
		const bool cameraOriented = config.type == VK_SURFACE_SPRITE_ORIENTED ||
			config.type == VK_SURFACE_SPRITE_EFFECT;
		if ( cameraOriented && config.facing != VK_SURFACE_SPRITE_FACING_NORMAL )
		{
			const float halfWidth = width * 0.5f;
			VectorSet( bottomLeft,
				spritePosition[0] - halfWidth, spritePosition[1] - halfWidth, spritePosition[2] + 1.0f );
			VectorSet( bottomRight,
				spritePosition[0] + halfWidth, spritePosition[1] - halfWidth, spritePosition[2] + 1.0f );
			VectorSet( topRight,
				spritePosition[0] + halfWidth, spritePosition[1] + halfWidth, spritePosition[2] + 1.0f );
			VectorSet( topLeft,
				spritePosition[0] - halfWidth, spritePosition[1] + halfWidth, spritePosition[2] + 1.0f );
		}
		else
		{
			vec3_t right = {};
			vec3_t bottom;
			vec3_t top;
			VectorCopy( spritePosition, bottom );
			VectorCopy( spritePosition, top );
			if ( cameraOriented )
			{
				VectorScale( vk.worldRefdef.viewaxis[1], width * 0.5f, right );
				VectorMA( top, height, vk.worldRefdef.viewaxis[2], top );
			}
			else
			{
				if ( config.type == VK_SURFACE_SPRITE_FLATTENED )
				{
					right[0] = std::sin( instance.phase ) * width * 0.5f;
					right[1] = std::cos( instance.phase ) * width * 0.5f;
				}
				else
				{
					const float angle = DEG2RAD( orientationAngles[instance.orientation & 3u] );
					for ( int component = 0; component < 3; ++component )
					{
						right[component] = width * 0.5f * (
							vk.worldRefdef.viewaxis[1][component] * std::cos( angle ) +
							vk.worldRefdef.viewaxis[0][component] * std::sin( angle ) );
					}
					right[2] = 0.0f;
				}
				top[2] += config.facing == VK_SURFACE_SPRITE_FACING_DOWN ? -height : height;
				const float swayAngle =
					( instance.position[0] + instance.position[1] ) * 0.02f + seconds * 1.5f + instance.phase;
				const float idleSway = height * config.windIdle * 0.075f;
				top[0] += std::cos( swayAngle ) * idleSway;
				top[1] += std::sin( swayAngle ) * idleSway;
				const float windSway = height * config.wind * 0.035f;
				top[0] += std::sin( seconds * 0.8f + instance.phase ) * windSway;
				top[1] += std::cos( seconds * 0.65f + instance.phase ) * windSway;
				if ( config.vertSkew != 0.0f )
				{
					top[0] += std::sin( instance.phase * 3.0f ) * height * config.vertSkew;
					top[1] += std::cos( instance.phase * 3.0f ) * height * config.vertSkew;
				}
			}
			VectorSubtract( bottom, right, bottomLeft );
			VectorAdd( bottom, right, bottomRight );
			VectorSubtract( top, right, topLeft );
			VectorAdd( top, right, topRight );
		}
		float color[4];
		if ( batch.stage.blendMode == VK_BLEND_ADDITIVE )
		{
			for ( int component = 0; component < 3; ++component )
			{
				color[component] = ( 0.5f + instance.color[component] * 0.5f ) * alpha;
			}
			color[3] = 1.0f;
		}
		else
		{
			color[0] = instance.color[0];
			color[1] = instance.color[1];
			color[2] = instance.color[2];
			color[3] = alpha * instance.color[3];
		}
		VK_WriteSurfaceSpriteVertex( &destination[writtenVertices + 0], bottomLeft, color, 0.0f, 1.0f );
		VK_WriteSurfaceSpriteVertex( &destination[writtenVertices + 1], bottomRight, color, 1.0f, 1.0f );
		VK_WriteSurfaceSpriteVertex( &destination[writtenVertices + 2], topRight, color, 1.0f, 0.0f );
		VK_WriteSurfaceSpriteVertex( &destination[writtenVertices + 3], bottomLeft, color, 0.0f, 1.0f );
		VK_WriteSurfaceSpriteVertex( &destination[writtenVertices + 4], topRight, color, 1.0f, 0.0f );
		VK_WriteSurfaceSpriteVertex( &destination[writtenVertices + 5], topLeft, color, 0.0f, 0.0f );
		writtenVertices += 6;
	}

	const VkDeviceSize byteCount =
		static_cast<VkDeviceSize>( writtenVertices * sizeof( vk_world_vertex_t ) );
	vk.skinnedVertexOffset = offset + byteCount;
	*vertexOffset = offset;
	*vertexCount = static_cast<uint32_t>( writtenVertices );
	vk.surfaceSpriteStreamCache.push_back( {
		&batch,
		offset,
		static_cast<uint32_t>( writtenVertices ),
	} );
	return true;
}

static void VK_RecordWorldSurfaceSprites(
	const float mvp[16],
	const std::vector<byte> *visibleSurfaces )
{
	if ( vk.world.surfaceSpriteBatches.empty() )
	{
		return;
	}

	vkCmdPushConstants(
		vk.commandBuffer,
		vk.pipelineLayout,
		VK_SHADER_STAGE_VERTEX_BIT,
		0,
		sizeof( float ) * 16,
		mvp );
	VkPipeline boundPipeline = VK_NULL_HANDLE;
	VkDescriptorSet boundTexture = VK_NULL_HANDLE;
	uint32_t batchesDrawn = 0;
	uint32_t spritesDrawn = 0;
	uint32_t spritesDrawnByType[5] = {};
	for ( const vk_surface_sprite_batch_t &batch : vk.world.surfaceSpriteBatches )
	{
		if ( visibleSurfaces != nullptr &&
			 ( batch.surfaceIndex >= visibleSurfaces->size() ||
			   ( *visibleSurfaces )[batch.surfaceIndex] == 0 ) )
		{
			continue;
		}
		VkDeviceSize vertexOffset = 0;
		uint32_t vertexCount = 0;
		if ( !VK_StreamSurfaceSpriteBatch( batch, &vertexOffset, &vertexCount ) || vertexCount == 0 )
		{
			continue;
		}
		VK_BindWorldPipeline(
			batch.stage.blendMode, &boundPipeline, batch.stage.depthWrite );
		if ( !VK_BindWorldTexture(
				batch.stage.texture, &boundTexture, !batch.stage.clampMap ) )
		{
			continue;
		}
		VK_PushWorldStage( &batch.stage, false );
		vkCmdBindVertexBuffers(
			vk.commandBuffer, 0, 1, &vk.skinnedVertexBuffer, &vertexOffset );
		vkCmdDraw( vk.commandBuffer, vertexCount, 1, 0, 0 );
		++batchesDrawn;
		const uint32_t batchSprites = vertexCount / 6;
		spritesDrawn += batchSprites;
		const size_t typeIndex = static_cast<size_t>( batch.stage.surfaceSprite.type );
		if ( typeIndex < ARRAY_LEN( spritesDrawnByType ) )
		{
			spritesDrawnByType[typeIndex] += batchSprites;
		}
	}

	if ( !vk.loggedSurfaceSpriteDraw && batchesDrawn > 0 )
	{
		ri.Printf( PRINT_ALL,
			"rd-vulkan-surfacesprites: rendering %u visible sprites from %u BSP batches "
			"(vertical=%u oriented=%u effect=%u flattened=%u)\n",
			spritesDrawn, batchesDrawn,
			spritesDrawnByType[VK_SURFACE_SPRITE_VERTICAL],
			spritesDrawnByType[VK_SURFACE_SPRITE_ORIENTED],
			spritesDrawnByType[VK_SURFACE_SPRITE_EFFECT],
			spritesDrawnByType[VK_SURFACE_SPRITE_FLATTENED] );
		vk.loggedSurfaceSpriteDraw = true;
	}
}

static uint32_t VK_WeatherHash( uint32_t value )
{
	value ^= value >> 16;
	value *= 0x7feb352du;
	value ^= value >> 15;
	value *= 0x846ca68bu;
	return value ^ ( value >> 16 );
}

static float VK_WeatherRandom01( uint32_t value )
{
	return static_cast<float>( VK_WeatherHash( value ) & 0x00ffffffu ) /
		static_cast<float>( 0x01000000u );
}

static uint64_t VK_WeatherPointKey( const float position[3] )
{
	constexpr float cellSize = 32.0f;
	constexpr uint64_t coordinateMask = ( uint64_t{ 1 } << 21 ) - 1;
	const int x = static_cast<int>( std::floor( position[0] / cellSize ) );
	const int y = static_cast<int>( std::floor( position[1] / cellSize ) );
	const int z = static_cast<int>( std::floor( position[2] / cellSize ) );
	return ( static_cast<uint64_t>( x ) & coordinateMask ) << 42 |
		( static_cast<uint64_t>( y ) & coordinateMask ) << 21 |
		( static_cast<uint64_t>( z ) & coordinateMask );
}

static bool VK_WeatherPointOutside( const float position[3] )
{
	const uint64_t key = VK_WeatherPointKey( position );
	const auto cached = vk.weatherOutsideCache.find( key );
	if ( cached != vk.weatherOutsideCache.end() )
	{
		return cached->second;
	}

	int contents = 0;
	if ( ri.CM_PointContents != nullptr )
	{
		contents = ri.CM_PointContents( position, 0 );
	}
	const bool outside = ( contents & ( CONTENTS_SOLID | CONTENTS_WATER ) ) == 0 &&
		( ( contents & CONTENTS_OUTSIDE ) != 0 || ( contents & CONTENTS_INSIDE ) == 0 );
	if ( vk.weatherOutsideCache.size() >= 131072 )
	{
		vk.weatherOutsideCache.clear();
	}
	vk.weatherOutsideCache.emplace( key, outside );
	return outside;
}

static void VK_BuildWeatherSnowBatch()
{
	if ( vk.weatherSnowBatchFrame == vk.frameIndex )
	{
		return;
	}
	vk.weatherSnowBatchFrame = vk.frameIndex;
	vk.weatherSnowBatch.instances.clear();
	if ( !vk.weatherSnow || vk.weatherSnowCount == 0 )
	{
		return;
	}
	if ( !VK_WeatherPointOutside( vk.worldRefdef.vieworg ) )
	{
		if ( !vk.loggedWeatherSuppressed )
		{
			ri.Printf( PRINT_ALL,
				"rd-vulkan-worldfx: snow suppressed at indoor camera (%.1f %.1f %.1f)\n",
				vk.worldRefdef.vieworg[0], vk.worldRefdef.vieworg[1], vk.worldRefdef.vieworg[2] );
			vk.loggedWeatherSuppressed = true;
		}
		return;
	}

	if ( vk.weatherSnowShader <= 2 ||
		 static_cast<size_t>( vk.weatherSnowShader ) >= vk.textures.size() )
	{
		vk.weatherSnowShader = VK_Backend_RegisterTexture( "gfx/effects/snowflake1.tga" );
	}
	if ( vk.weatherSnowShader <= 2 ||
		 static_cast<size_t>( vk.weatherSnowShader ) >= vk.textures.size() )
	{
		if ( !vk.loggedWeatherResourceFailure )
		{
			ri.Printf( PRINT_WARNING,
				"rd-vulkan-worldfx: snowflake texture unavailable (handle=%d textures=%zu)\n",
				vk.weatherSnowShader, vk.textures.size() );
			vk.loggedWeatherResourceFailure = true;
		}
		return;
	}
	if ( static_cast<size_t>( vk.weatherSnowShader ) < vk.materials.size() &&
		 !vk.materials[vk.weatherSnowShader].stages.empty() )
	{
		vk.weatherSnowBatch.stage = vk.materials[vk.weatherSnowShader].stages.front();
	}
	else
	{
		vk.weatherSnowBatch.stage = {};
		vk.weatherSnowBatch.stage.texture = vk.weatherSnowShader;
		vk.weatherSnowBatch.stage.clampMap = true;
	}
	vk.weatherSnowBatch.stage.blendMode = VK_BLEND_ADDITIVE;
	vk.weatherSnowBatch.stage.alpha = 1.0f;
	vk.weatherSnowBatch.stage.vertexColor = true;
	vk.weatherSnowBatch.stage.depthWrite = false;
	vk.weatherSnowBatch.stage.color[0] = 1.0f;
	vk.weatherSnowBatch.stage.color[1] = 1.0f;
	vk.weatherSnowBatch.stage.color[2] = 1.0f;
	vk.weatherSnowBatch.stage.color[3] = 1.0f;
	vk_surface_sprite_config_t &config = vk.weatherSnowBatch.stage.surfaceSprite;
	config = {};
	config.type = VK_SURFACE_SPRITE_ORIENTED;
	config.facing = VK_SURFACE_SPRITE_FACING_NORMAL;
	config.fadeDist = 500.0f;
	config.fadeMax = 700.0f;

	constexpr int cellRadius = 5;
	constexpr int cellSpan = cellRadius * 2 + 1;
	constexpr float cellSize = 125.0f;
	constexpr float verticalSpan = 1250.0f;
	constexpr float fallSpeed = 300.0f;
	const uint32_t cellCount = cellSpan * cellSpan;
	const uint32_t particlesPerCell =
		std::max( 1u, ( vk.weatherSnowCount + cellCount - 1 ) / cellCount );
	const int centerCellX = static_cast<int>( std::floor( vk.worldRefdef.vieworg[0] / cellSize ) );
	const int centerCellY = static_cast<int>( std::floor( vk.worldRefdef.vieworg[1] / cellSize ) );
	const float seconds = static_cast<float>( vk.worldRefdef.time ) * 0.001f;
	vec3_t windDirection;
	VectorCopy( vk.weatherWind, windDirection );
	if ( VectorNormalize( windDirection ) == 0.0f )
	{
		VectorClear( windDirection );
	}
	const float gustOffset = vk.weatherGusting ? std::sin( seconds * 0.7f ) * 45.0f : 0.0f;
	vk.weatherSnowBatch.instances.reserve( cellCount * particlesPerCell );

	for ( int cellY = centerCellY - cellRadius; cellY <= centerCellY + cellRadius; ++cellY )
	{
		for ( int cellX = centerCellX - cellRadius; cellX <= centerCellX + cellRadius; ++cellX )
		{
			const uint32_t cellSeed =
				static_cast<uint32_t>( cellX ) * 0x9e3779b9u ^
				static_cast<uint32_t>( cellY ) * 0x85ebca6bu;
			for ( uint32_t particle = 0; particle < particlesPerCell; ++particle )
			{
				const uint32_t seed = cellSeed ^ particle * 0xc2b2ae35u;
				const float randomX = VK_WeatherRandom01( seed + 0u );
				const float randomY = VK_WeatherRandom01( seed + 1u );
				const float randomZ = VK_WeatherRandom01( seed + 2u );
				const float fallDistance = seconds * fallSpeed + randomZ * verticalSpan;
				const float fallCycle = fallDistance / verticalSpan -
					std::floor( fallDistance / verticalSpan );
				float z = vk.worldRefdef.vieworg[2] + verticalSpan * 0.5f -
					fallCycle * verticalSpan;
				const float drift = fallCycle * 220.0f + gustOffset;

				vk_surface_sprite_instance_t instance = {};
				instance.position[0] = ( static_cast<float>( cellX ) + randomX ) * cellSize +
					windDirection[0] * drift;
				instance.position[1] = ( static_cast<float>( cellY ) + randomY ) * cellSize +
					windDirection[1] * drift;
				instance.position[2] = z;
				instance.color[0] = 0.75f;
				instance.color[1] = 0.75f;
				instance.color[2] = 0.75f;
				instance.color[3] = 1.0f;
				instance.width = 1.5f;
				instance.height = 1.5f;
				instance.phase = VK_WeatherRandom01( seed + 3u ) * 6.28318530718f;
				if ( VK_WeatherPointOutside( instance.position ) )
				{
					vk.weatherSnowBatch.instances.push_back( instance );
				}
			}
		}
	}
}

static void VK_RecordWeather( const float mvp[16] )
{
	VK_BuildWeatherSnowBatch();
	if ( vk.weatherSnowBatch.instances.empty() )
	{
		return;
	}

	VkDeviceSize vertexOffset = 0;
	uint32_t vertexCount = 0;
	if ( !VK_StreamSurfaceSpriteBatch(
			vk.weatherSnowBatch, &vertexOffset, &vertexCount ) || vertexCount == 0 )
	{
		return;
	}
	vkCmdPushConstants(
		vk.commandBuffer, vk.pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT,
		0, sizeof( float ) * 16, mvp );
	VkPipeline boundPipeline = VK_NULL_HANDLE;
	VkDescriptorSet boundTexture = VK_NULL_HANDLE;
	VK_BindWorldPipeline( VK_BLEND_ADDITIVE, &boundPipeline );
	if ( !VK_BindWorldTexture(
			vk.weatherSnowBatch.stage.texture, &boundTexture, false ) )
	{
		return;
	}
	VK_PushWorldStage( &vk.weatherSnowBatch.stage, false );
	vkCmdBindVertexBuffers(
		vk.commandBuffer, 0, 1, &vk.skinnedVertexBuffer, &vertexOffset );
	vkCmdDraw( vk.commandBuffer, vertexCount, 1, 0, 0 );
	if ( !vk.loggedWeatherDraw )
	{
		ri.Printf( PRINT_ALL,
			"rd-vulkan-worldfx: rendering %u snow particles in %zu weather zones wind=(%.1f %.1f %.1f) gusting=%s\n",
			vertexCount / 6, vk.weatherZones.size(),
			vk.weatherWind[0], vk.weatherWind[1], vk.weatherWind[2],
			vk.weatherGusting ? "yes" : "no" );
		vk.loggedWeatherDraw = true;
	}
}

static const char *VK_TextureNameForHandle( qhandle_t handle )
{
	for ( const vk_texture_name_t &registered : vk.textureNames )
	{
		if ( registered.handle == handle )
		{
			return registered.name.c_str();
		}
	}
	return "<unnamed>";
}

static void VK_LogVisibleWorldMaterials(
	const std::vector<byte> *visibleSurfaces,
	const char *context,
	bool *logged )
{
	if ( *logged )
	{
		return;
	}

	struct visible_material_t
	{
		qhandle_t shader;
		uint32_t surfaces;
	};
	std::vector<visible_material_t> visibleMaterials;
	for ( const vk_world_batch_t &batch : vk.world.batches )
	{
		if ( visibleSurfaces != nullptr &&
			 ( batch.surfaceIndex >= visibleSurfaces->size() ||
			   ( *visibleSurfaces )[batch.surfaceIndex] == 0 ) )
		{
			continue;
		}

		auto material = std::find_if(
			visibleMaterials.begin(), visibleMaterials.end(),
			[&batch]( const visible_material_t &entry ) { return entry.shader == batch.shader; } );
		if ( material == visibleMaterials.end() )
		{
			visibleMaterials.push_back( { batch.shader, 1 } );
		}
		else
		{
			++material->surfaces;
		}
	}

	ri.Printf( PRINT_ALL,
		"rd-vulkan-world-materials: %s origin=(%.1f %.1f %.1f), unique=%zu\n",
		context,
		vk.worldRefdef.vieworg[0], vk.worldRefdef.vieworg[1], vk.worldRefdef.vieworg[2],
		visibleMaterials.size() );
	const size_t logLimit = std::min<size_t>( visibleMaterials.size(), 80 );
	for ( size_t i = 0; i < logLimit; ++i )
	{
		const visible_material_t &material = visibleMaterials[i];
		const size_t stageCount = material.shader > 0 &&
			static_cast<size_t>( material.shader ) < vk.materials.size()
			? vk.materials[material.shader].stages.size() : 0;
		ri.Printf( PRINT_ALL, "rd-vulkan-world-materials: %s surfaces=%u stages=%zu\n",
			VK_TextureNameForHandle( material.shader ), material.surfaces, stageCount );
	}
	if ( logLimit < visibleMaterials.size() )
	{
		ri.Printf( PRINT_ALL, "rd-vulkan-world-materials: omitted %zu additional materials\n",
			visibleMaterials.size() - logLimit );
	}
	*logged = true;
}

static void VK_LogShipInteriorModels()
{
	if ( vk.loggedShipInteriorModels )
	{
		return;
	}

	std::vector<qhandle_t> modelHandles;
	for ( const refEntity_t &entity : vk.worldEntities )
	{
		if ( entity.reType != RT_MODEL || entity.ghoul2 != nullptr || entity.hModel <= 0 ||
			 std::find( modelHandles.begin(), modelHandles.end(), entity.hModel ) != modelHandles.end() )
		{
			continue;
		}
		modelHandles.push_back( entity.hModel );
	}

	ri.Printf( PRINT_ALL, "rd-vulkan-ship-interior-models: unique=%zu\n", modelHandles.size() );
	for ( qhandle_t handle : modelHandles )
	{
		const vk_model_t *model = VK_ModelForHandle( handle );
		if ( model == nullptr )
		{
			continue;
		}
		ri.Printf( PRINT_ALL,
			"rd-vulkan-ship-interior-models: %s type=%d inline=%d surfaces=%zu\n",
			model->name.c_str(), static_cast<int>( model->type ), model->inlineModelIndex,
			model->surfaces.size() );
		for ( const vk_model_surface_t &surface : model->surfaces )
		{
			ri.Printf( PRINT_ALL,
				"rd-vulkan-ship-interior-models:   surface=%s material=%s\n",
				surface.name.c_str(), VK_TextureNameForHandle( surface.shader ) );
		}
	}
	vk.loggedShipInteriorModels = true;
}

static void VK_RecordSky( const float view[16], const float projection[16] )
{
	if ( !vk.world.hasSky || vk.skinnedVertexMapped == nullptr )
	{
		return;
	}

	constexpr size_t faceCount = 6;
	constexpr size_t verticesPerFace = 6;
	constexpr size_t vertexCount = faceCount * verticesPerFace;
	constexpr float boxSize = 32768.0f;
	constexpr float uvMin = 1.0f / 256.0f;
	constexpr float uvMax = 255.0f / 256.0f;
	static const int stToVec[6][3] = {
		{ 3, -1, 2 }, { -3, 1, 2 }, { 1, 3, 2 },
		{ -1, -3, 2 }, { -2, -1, 3 }, { 2, -1, -3 },
	};
	static const float corners[6][2] = {
		{ -1.0f, -1.0f }, { 1.0f, -1.0f }, { 1.0f, 1.0f },
		{ -1.0f, -1.0f }, { 1.0f, 1.0f }, { -1.0f, 1.0f },
	};

	const VkDeviceSize alignment = 16;
	const VkDeviceSize offset =
		( vk.skinnedVertexOffset + alignment - 1 ) & ~( alignment - 1 );
	const VkDeviceSize byteCount = vertexCount * sizeof( vk_world_vertex_t );
	if ( offset > vk.skinnedVertexCapacity ||
		 byteCount > vk.skinnedVertexCapacity - offset )
	{
		return;
	}

	vk_world_vertex_t *vertices = reinterpret_cast<vk_world_vertex_t *>(
		vk.skinnedVertexMapped + offset );
	for ( size_t face = 0; face < faceCount; ++face )
	{
		for ( size_t corner = 0; corner < verticesPerFace; ++corner )
		{
			const float s = corners[corner][0];
			const float t = corners[corner][1];
			const float components[3] = { s * boxSize, t * boxSize, boxSize };
			vk_world_vertex_t &vertex = vertices[face * verticesPerFace + corner];
			vertex = {};
			for ( int axis = 0; axis < 3; ++axis )
			{
				const int source = stToVec[face][axis];
				vertex.position[axis] = source < 0
					? -components[-source - 1]
					: components[source - 1];
			}
			vertex.color[0] = 1.0f;
			vertex.color[1] = 1.0f;
			vertex.color[2] = 1.0f;
			vertex.color[3] = 1.0f;
			vertex.uv[0] = uvMin + ( s + 1.0f ) * 0.5f * ( uvMax - uvMin );
			vertex.uv[1] = uvMin + ( 1.0f - ( t + 1.0f ) * 0.5f ) * ( uvMax - uvMin );
		}
	}
	vk.skinnedVertexOffset = offset + byteCount;

	float skyView[16];
	float skyMvp[16];
	std::memcpy( skyView, view, sizeof( skyView ) );
	skyView[12] = 0.0f;
	skyView[13] = 0.0f;
	skyView[14] = 0.0f;
	VK_MatrixMultiply( projection, skyView, skyMvp );
	vkCmdPushConstants(
		vk.commandBuffer,
		vk.pipelineLayout,
		VK_SHADER_STAGE_VERTEX_BIT,
		0,
		sizeof( skyMvp ),
		skyMvp );

	VkPipeline boundPipeline = VK_NULL_HANDLE;
	VkDescriptorSet boundTexture = VK_NULL_HANDLE;
	VK_BindWorldPipeline( VK_BLEND_OPAQUE, &boundPipeline );
	VK_PushWorldStage( nullptr, false );
	vkCmdBindVertexBuffers(
		vk.commandBuffer, 0, 1, &vk.skinnedVertexBuffer, &offset );
	for ( size_t face = 0; face < faceCount; ++face )
	{
		if ( !VK_BindWorldTexture(
			vk.world.skyTextures[face], &boundTexture, false ) )
		{
			continue;
		}
		vkCmdDraw(
			vk.commandBuffer,
			verticesPerFace,
			1,
			static_cast<uint32_t>( face * verticesPerFace ),
			0 );
	}
}

static void VK_RecordWorld( int eye )
{
	if ( !vk.haveWorldRefdef ||
		 vk.worldPipeline == VK_NULL_HANDLE ||
		 vk.world.vertexBuffer == VK_NULL_HANDLE ||
		 vk.world.indexBuffer == VK_NULL_HANDLE ||
		 vk.world.indexCount == 0 ||
		 vk.world.batches.empty() )
	{
		return;
	}

	float view[16] = {};
	float projection[16] = {};
	float mvp[16] = {};
	VK_BuildViewMatrix( vk.worldRefdef, eye, view );
	VK_BuildProjectionMatrix( vk.views[eye].fov, 1.0f, 65536.0f, projection );
	VK_MatrixMultiply( projection, view, mvp );
	VK_RecordSky( view, projection );

	const std::vector<byte> *visibleSurfaces = VK_WorldVisibleSurfaceMask( vk.worldRefdef );
	VK_LogVisibleWorldMaterials(
		visibleSurfaces, "first-view", &vk.loggedVisibleWorldMaterials );
	const bool shipInterior = vk.worldRefdef.vieworg[0] > 3500.0f &&
		vk.worldRefdef.vieworg[0] < 4500.0f &&
		vk.worldRefdef.vieworg[1] > -800.0f && vk.worldRefdef.vieworg[1] < 800.0f &&
		vk.worldRefdef.vieworg[2] > -500.0f && vk.worldRefdef.vieworg[2] < 500.0f;
	if ( shipInterior )
	{
		VK_LogVisibleWorldMaterials(
			visibleSurfaces, "ship-interior", &vk.loggedShipInteriorMaterials );
		VK_LogShipInteriorModels();
	}
	const auto recordWorldPass = [&]( vk_world_pass_t pass )
	{
		const VkDeviceSize offsets[] = { 0 };
		vkCmdBindVertexBuffers( vk.commandBuffer, 0, 1, &vk.world.vertexBuffer, offsets );
		vkCmdBindIndexBuffer( vk.commandBuffer, vk.world.indexBuffer, 0, VK_INDEX_TYPE_UINT32 );
		vkCmdPushConstants(
			vk.commandBuffer,
			vk.pipelineLayout,
			VK_SHADER_STAGE_VERTEX_BIT,
			0,
			sizeof( mvp ),
			mvp );

		VkPipeline boundPipeline = VK_NULL_HANDLE;
		VkDescriptorSet boundTexture = VK_NULL_HANDLE;
		for ( const vk_world_batch_t &batch : vk.world.batches )
		{
			if ( visibleSurfaces != nullptr &&
				 ( batch.surfaceIndex >= visibleSurfaces->size() || ( *visibleSurfaces )[batch.surfaceIndex] == 0 ) )
			{
				continue;
			}

			if ( pass == VK_WORLD_PASS_FOG )
			{
				VK_RecordBoundIndexedFog(
					batch.shader, batch.indexCount, batch.firstIndex,
					&boundPipeline, &boundTexture );
			}
			else
			{
				VK_RecordBoundIndexedShader(
					batch.shader, batch.lightmap, batch.vertexLit, batch.indexCount, batch.firstIndex,
					pass, &boundPipeline, &boundTexture );
			}
		}
	};

	recordWorldPass( VK_WORLD_PASS_OPAQUE );
	VK_RecordWorldSurfaceSprites( mvp, visibleSurfaces );
	VK_RecordSceneModels(
		view, projection, VK_WORLD_PASS_OPAQUE, vk.worldEntities, true, vk.worldRefdef.time );
	recordWorldPass( VK_WORLD_PASS_TRANSLUCENT );
	VK_RecordSceneModels(
		view, projection, VK_WORLD_PASS_TRANSLUCENT, vk.worldEntities, true, vk.worldRefdef.time );
	if ( vk.world.hasGlobalFog )
	{
		recordWorldPass( VK_WORLD_PASS_FOG );
		VK_RecordSceneModels(
			view, projection, VK_WORLD_PASS_FOG, vk.worldEntities, true, vk.worldRefdef.time );
	}
	VK_RecordWeather( mvp );

	if ( !vk.loggedWorldDraw )
	{
		ri.Printf( PRINT_ALL, "rd-vulkan-world: drawing static BSP geometry: surfaces=%u vertices=%u indices=%u batches=%zu textured=%u pvs=%s\n",
			vk.world.surfaceCount, vk.world.vertexCount, vk.world.indexCount,
			vk.world.batches.size(), vk.world.texturedBatchCount,
			visibleSurfaces != nullptr ? "yes" : "no" );
		vk.loggedWorldDraw = true;
	}
}

static void VK_RecordDiagnosticWorld( int eye )
{
	if ( !VK_DiagnosticWorldEnabled() || !vk.haveWorldRefdef )
	{
		return;
	}

	float model[16] = {};
	float view[16] = {};
	float projection[16] = {};
	float modelView[16] = {};
	float mvp[16] = {};

	VK_BuildDiagnosticModelMatrix( model );
	VK_BuildViewMatrix( vk.worldRefdef, eye, view );
	VK_BuildProjectionMatrix( vk.views[eye].fov, 1.0f, 8192.0f, projection );
	VK_MatrixMultiply( view, model, modelView );
	VK_MatrixMultiply( projection, modelView, mvp );

	vkCmdBindPipeline( vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.diagnostic3dPipeline );
	vkCmdPushConstants(
		vk.commandBuffer,
		vk.pipelineLayout,
		VK_SHADER_STAGE_VERTEX_BIT,
		0,
		sizeof( mvp ),
		mvp );
	vkCmdDraw( vk.commandBuffer, 108, 1, 0, 0 );

	if ( !vk.loggedDiagnosticDraw )
	{
		ri.Printf( PRINT_ALL, "rd-vulkan-scene: drawing camera-relative 3D diagnostic triad\n" );
		vk.loggedDiagnosticDraw = true;
	}
}

static void VK_RecordScreenScenes( int eye, size_t firstScene, size_t endScene )
{
	if ( firstScene >= vk.screenScenes.size() || firstScene >= endScene )
	{
		return;
	}
	endScene = std::min( endScene, vk.screenScenes.size() );

	const int targetWidth =
		static_cast<int>( vk.viewConfiguration[eye].recommendedImageRectWidth );
	const float targetHeight =
		static_cast<float>( vk.viewConfiguration[eye].recommendedImageRectHeight );
	for ( size_t sceneIndex = firstScene; sceneIndex < endScene; ++sceneIndex )
	{
		const vk_scene_submission_t &scene = vk.screenScenes[sceneIndex];
		if ( scene.refdef.width <= 0 || scene.refdef.height <= 0 )
		{
			continue;
		}

		const CGhoul2Info_v *previewGhoul2 = nullptr;
		for ( const refEntity_t &entity : scene.entities )
		{
			if ( entity.reType == RT_MODEL && entity.ghoul2 != nullptr )
			{
				previewGhoul2 = entity.ghoul2;
				break;
			}
		}

		vk_screen_scene_clip_t *savedClip = nullptr;
		if ( previewGhoul2 != nullptr )
		{
			for ( vk_screen_scene_clip_t &clip : vk.screenSceneClips )
			{
				if ( clip.ghoul2 == previewGhoul2 )
				{
					savedClip = &clip;
					break;
				}
			}
			if ( static_cast<float>( scene.refdef.width ) <= static_cast<float>( targetWidth ) &&
				 static_cast<float>( scene.refdef.height ) <= targetHeight )
			{
				const float area =
					static_cast<float>( scene.refdef.width ) * scene.refdef.height;
				const float savedArea =
					savedClip != nullptr ? savedClip->width * savedClip->height : 0.0f;
				if ( savedClip == nullptr )
				{
					vk.screenSceneClips.push_back( {
						previewGhoul2,
						static_cast<float>( scene.refdef.x ),
						targetHeight - static_cast<float>( scene.refdef.y + scene.refdef.height ),
						static_cast<float>( scene.refdef.width ),
						static_cast<float>( scene.refdef.height ),
					} );
					savedClip = &vk.screenSceneClips.back();
				}
				else if ( area < savedArea )
				{
					savedClip->x = static_cast<float>( scene.refdef.x );
					savedClip->y =
						targetHeight - static_cast<float>( scene.refdef.y + scene.refdef.height );
					savedClip->width = static_cast<float>( scene.refdef.width );
					savedClip->height = static_cast<float>( scene.refdef.height );
				}
			}
		}

		VkViewport viewport = {};
		viewport.x = static_cast<float>( scene.refdef.x );
		const bool previewZoom =
			savedClip != nullptr &&
			static_cast<float>( scene.refdef.height ) > savedClip->height + 0.5f;
		if ( previewZoom )
		{
			// Anchor the zoom to its original top edge. Switching coordinate
			// formulas only after overflow caused a visible mid-transition jolt.
			viewport.y = savedClip->y;
		}
		else
		{
			viewport.y =
				targetHeight - static_cast<float>( scene.refdef.y + scene.refdef.height );
		}
		viewport.width = static_cast<float>( scene.refdef.width );
		viewport.height = static_cast<float>( scene.refdef.height );
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport( vk.commandBuffer, 0, 1, &viewport );

		VkRect2D scissor = {};
		const int scissorLeft = std::max( 0, static_cast<int>( std::floor( viewport.x ) ) );
		const int scissorTop = std::max( 0, static_cast<int>( std::floor( viewport.y ) ) );
		const int scissorRight = std::min(
			targetWidth,
			static_cast<int>( std::ceil( viewport.x + viewport.width ) ) );
		const int scissorBottom = std::min(
			static_cast<int>( targetHeight ),
			static_cast<int>( std::ceil( viewport.y + viewport.height ) ) );
		int clippedLeft = scissorLeft;
		int clippedTop = scissorTop;
		int clippedRight = scissorRight;
		int clippedBottom = scissorBottom;
		if ( previewZoom )
		{
			clippedLeft = std::max(
				clippedLeft, static_cast<int>( std::floor( savedClip->x ) ) );
			clippedTop = std::max(
				clippedTop, static_cast<int>( std::floor( savedClip->y ) ) );
			clippedRight = std::min(
				clippedRight,
				static_cast<int>( std::ceil( savedClip->x + savedClip->width ) ) );
			clippedBottom = std::min(
				clippedBottom,
				static_cast<int>( std::ceil( savedClip->y + savedClip->height ) ) );
		}
		if ( clippedRight <= clippedLeft || clippedBottom <= clippedTop )
		{
			continue;
		}
		scissor.offset.x = clippedLeft;
		scissor.offset.y = clippedTop;
		scissor.extent.width = static_cast<uint32_t>( clippedRight - clippedLeft );
		scissor.extent.height = static_cast<uint32_t>( clippedBottom - clippedTop );
		vkCmdSetScissor( vk.commandBuffer, 0, 1, &scissor );

		float view[16] = {};
		float projection[16] = {};
		VK_BuildViewMatrix( scene.refdef, 0, view, false );
		VK_BuildRefdefProjectionMatrix( scene.refdef, 1.0f, 8192.0f, projection );
		VK_RecordSceneModels(
			view,
			projection,
				VK_WORLD_PASS_OPAQUE,
				scene.entities,
				false,
				scene.refdef.time );
		VK_RecordSceneModels(
			view,
			projection,
				VK_WORLD_PASS_TRANSLUCENT,
				scene.entities,
				false,
				scene.refdef.time );
	}
}

static void VK_UpdateJkxrHmdPose( XrTime displayTime )
{
	if ( ri.TBXR_UpdateHMDPose == nullptr )
	{
		return;
	}

	const XrSpace baseSpace =
		vk.stageSpace != XR_NULL_HANDLE ? vk.stageSpace : vk.localSpace;
	XrSpaceLocation location = {};
	location.type = XR_TYPE_SPACE_LOCATION;
	if ( !VK_CheckXr(
			xrLocateSpace( vk.viewSpace, baseSpace, displayTime, &location ),
			"xrLocateSpace(HMD pose)" ) )
	{
		return;
	}

	const XrSpaceLocationFlags required =
		XR_SPACE_LOCATION_POSITION_VALID_BIT |
		XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
	if ( ( location.locationFlags & required ) != required )
	{
		return;
	}

	const XrPosef &pose = location.pose;
	ri.TBXR_UpdateHMDPose(
		pose.position.x,
		pose.position.y,
		pose.position.z,
		pose.orientation.x,
		pose.orientation.y,
		pose.orientation.z,
		pose.orientation.w );
	if ( !vk.loggedHmdPose )
	{
		ri.Printf(
			PRINT_ALL,
			"rd-vulkan: forwarding OpenXR HMD pose from %s space; height=%.3fm\n",
			vk.stageSpace != XR_NULL_HANDLE ? "stage" : "local",
			pose.position.y );
		vk.loggedHmdPose = true;
	}
}

static bool VK_LocateXrViews( XrTime displayTime )
{
	XrViewState viewState = {};
	viewState.type = XR_TYPE_VIEW_STATE;

	XrViewLocateInfo locateInfo = {};
	locateInfo.type = XR_TYPE_VIEW_LOCATE_INFO;
	locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	locateInfo.displayTime = displayTime;
	locateInfo.space = vk.localSpace;

	uint32_t viewCount = 0;
	if ( !VK_CheckXr( xrLocateViews(
			vk.xrSession,
			&locateInfo,
			&viewState,
			ARRAY_LEN( vk.views ),
			&viewCount,
			vk.views ), "xrLocateViews" ) || viewCount != VK_BACKEND_EYE_COUNT )
	{
		return false;
	}

	const XrViewStateFlags required =
		XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT;
	if ( ( viewState.viewStateFlags & required ) != required )
	{
		return false;
	}

	if ( ri.TBXR_UpdateFov != nullptr )
	{
		float angleLeft = vk.views[0].fov.angleLeft;
		float angleRight = vk.views[0].fov.angleRight;
		float angleDown = vk.views[0].fov.angleDown;
		float angleUp = vk.views[0].fov.angleUp;
		for ( int eye = 1; eye < VK_BACKEND_EYE_COUNT; ++eye )
		{
			angleLeft = std::min( angleLeft, vk.views[eye].fov.angleLeft );
			angleRight = std::max( angleRight, vk.views[eye].fov.angleRight );
			angleDown = std::min( angleDown, vk.views[eye].fov.angleDown );
			angleUp = std::max( angleUp, vk.views[eye].fov.angleUp );
		}
		const float fovX = RAD2DEG( angleRight - angleLeft );
		const float fovY = RAD2DEG( angleUp - angleDown );
		ri.TBXR_UpdateFov( fovX, fovY );
		if ( !vk.loggedFov )
		{
			ri.Printf( PRINT_ALL,
				"rd-vulkan: forwarding OpenXR view FOV %.2fx%.2f degrees before scene construction\n",
				fovX, fovY );
			vk.loggedFov = true;
		}
	}

	if ( !vk.loggedProjectionViews )
	{
		ri.Printf( PRINT_ALL,
			"rd-vulkan: OpenXR stereo views: left x=%.4f, right x=%.4f\n",
			vk.views[0].pose.position.x,
			vk.views[1].pose.position.x );
		vk.loggedProjectionViews = true;
	}
	return true;
}

static bool VK_PrepareXrFrame()
{
	if ( !vk.initialized || vk.exitRenderLoop )
	{
		return false;
	}

	VK_PollXrEvents();
	vk.frameBegun = false;
	vk.viewsValid = false;
	if ( !vk.sessionRunning )
	{
		return false;
	}

	vk.frameState = {};
	vk.frameState.type = XR_TYPE_FRAME_STATE;
	if ( !VK_CheckXr( xrWaitFrame( vk.xrSession, nullptr, &vk.frameState ), "xrWaitFrame" ) )
	{
		return false;
	}

	XrFrameBeginInfo beginInfo = {};
	beginInfo.type = XR_TYPE_FRAME_BEGIN_INFO;
	if ( !VK_CheckXr( xrBeginFrame( vk.xrSession, &beginInfo ), "xrBeginFrame" ) )
	{
		return false;
	}
	vk.frameBegun = true;

	VK_UpdateJkxrHmdPose( vk.frameState.predictedDisplayTime );
	if ( vk.frameState.shouldRender )
	{
		vk.viewsValid = VK_LocateXrViews( vk.frameState.predictedDisplayTime );
	}
	return true;
}

static void VK_GetHudNdcOffset( int eye, float *xOffset, float *yOffset )
{
	*xOffset = 0.0f;
	*yOffset = 0.0f;
	if ( eye < 0 || eye >= VK_BACKEND_EYE_COUNT || !vk.viewsValid )
	{
		return;
	}

	const cvar_t *hudStereo = ri.Cvar_Get( "cg_hudStereo", "20", 0 );
	const float stereoPixels = hudStereo != nullptr ? hudStereo->value : 20.0f;
	float xPixels = eye == 0 ? stereoPixels : -stereoPixels;

	// Match the legacy JKXR HUD correction for asymmetric per-eye FOVs.
	const XrFovf &fov = vk.views[eye].fov;
	const float offCenterX = -( fov.angleLeft + fov.angleRight ) * 0.5f;
	const float offCenterY = -( fov.angleUp + fov.angleDown ) * 0.5f;
	xPixels += offCenterX * 640.0f;
	const float yPixels = -offCenterY * 480.0f;
	*xOffset = xPixels * ( 2.0f / 640.0f );
	*yOffset = yPixels * ( 2.0f / 480.0f );

	if ( !vk.loggedHudStereo && eye == 1 )
	{
		ri.Printf( PRINT_ALL,
			"rd-vulkan-hud: per-eye virtual-pixel offsets left=(%.2f %.2f) right=(%.2f %.2f)\n",
			( stereoPixels + -( vk.views[0].fov.angleLeft + vk.views[0].fov.angleRight ) * 320.0f ),
			( vk.views[0].fov.angleUp + vk.views[0].fov.angleDown ) * 240.0f,
			xPixels, yPixels );
		vk.loggedHudStereo = true;
	}
}

static void VK_RecordScreenRects( int eye, size_t firstRect, size_t endRect )
{
	if ( firstRect >= vk.rects.size() || firstRect >= endRect )
	{
		return;
	}
	endRect = std::min( endRect, vk.rects.size() );
	VkViewport viewport = {};
	viewport.width = static_cast<float>( vk.viewConfiguration[eye].recommendedImageRectWidth );
	viewport.height = static_cast<float>( vk.viewConfiguration[eye].recommendedImageRectHeight );
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport( vk.commandBuffer, 0, 1, &viewport );
	VkRect2D scissor = {};
	scissor.extent.width = vk.viewConfiguration[eye].recommendedImageRectWidth;
	scissor.extent.height = vk.viewConfiguration[eye].recommendedImageRectHeight;
	vkCmdSetScissor( vk.commandBuffer, 0, 1, &scissor );

	float hudXOffset = 0.0f;
	float hudYOffset = 0.0f;
	if ( vk.sceneWorldRenderedThisFrame )
	{
		VK_GetHudNdcOffset( eye, &hudXOffset, &hudYOffset );
	}

	VkPipeline boundPipeline = VK_NULL_HANDLE;
	for ( size_t rectIndex = firstRect; rectIndex < endRect; ++rectIndex )
	{
		const vk_rect_t &rect = vk.rects[rectIndex];
		const bool textured = rect.texture > 0 &&
			static_cast<size_t>( rect.texture ) < vk.textures.size() &&
			vk.textures[rect.texture].descriptorSet != VK_NULL_HANDLE;
		VkPipeline desiredPipeline = vk.rectPipeline;
		if ( textured )
		{
			switch ( rect.blendMode )
			{
			case VK_BLEND_OPAQUE:
				desiredPipeline = vk.texturedRectOpaquePipeline;
				break;
			case VK_BLEND_ADDITIVE:
				desiredPipeline = vk.texturedRectAdditivePipeline;
				break;
			case VK_BLEND_DESTINATION_COLOR_ADDITIVE:
				desiredPipeline = vk.texturedRectDestinationColorAdditivePipeline;
				break;
			case VK_BLEND_ONE_MINUS_DESTINATION_ALPHA_ADDITIVE:
				desiredPipeline = vk.texturedRectOneMinusDestinationAlphaAdditivePipeline;
				break;
			case VK_BLEND_MODULATE:
				desiredPipeline = vk.texturedRectModulatePipeline;
				break;
			case VK_BLEND_ALPHA:
			default:
				desiredPipeline = vk.texturedRectPipeline;
				break;
			}
		}
		if ( boundPipeline != desiredPipeline )
		{
			vkCmdBindPipeline( vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, desiredPipeline );
			boundPipeline = desiredPipeline;
		}
		if ( textured )
		{
			vkCmdBindDescriptorSets(
				vk.commandBuffer,
				VK_PIPELINE_BIND_POINT_GRAPHICS,
				vk.pipelineLayout,
				0,
				1,
				&vk.textures[rect.texture].descriptorSet,
				0,
				nullptr );
		}

		const bool fullScreen = rect.rect[0] <= -0.996f && rect.rect[1] <= -0.996f &&
			rect.rect[2] >= 0.996f && rect.rect[3] >= 0.996f;
		const float rectXOffset = fullScreen ? 0.0f : hudXOffset;
		const float rectYOffset = fullScreen ? 0.0f : hudYOffset;
		float pushConstants[12] = {
			rect.rect[0] + rectXOffset, rect.rect[1] + rectYOffset,
			rect.rect[2] + rectXOffset, rect.rect[3] + rectYOffset,
			rect.uv[0], rect.uv[1], rect.uv[2], rect.uv[3],
			rect.color[0], rect.color[1], rect.color[2], rect.color[3],
		};
		vkCmdPushConstants(
			vk.commandBuffer,
			vk.pipelineLayout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0,
			sizeof( pushConstants ),
			pushConstants );
		vkCmdDraw( vk.commandBuffer, 6, 1, 0, 0 );
	}
}

static bool VK_RecordTestPattern( int eye, uint32_t imageIndex, const float tint[4] )
{
	if ( vk.ghoul2CacheFrameIndex != vk.frameIndex )
	{
		vk.skinnedVertexOffset = 0;
		vk.ghoul2BoneCache.clear();
		vk.ghoul2SurfaceCache.clear();
		vk.surfaceSpriteStreamCache.clear();
		vk.ghoul2CacheFrameIndex = vk.frameIndex;
	}
	if ( !VK_CheckVk( vkResetCommandPool( vk.device, vk.commandPool, 0 ), "vkResetCommandPool" ) )
	{
		return false;
	}

	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	if ( !VK_CheckVk( vkBeginCommandBuffer( vk.commandBuffer, &beginInfo ), "vkBeginCommandBuffer" ) )
	{
		return false;
	}

	VkClearValue clearValues[2] = {};
	clearValues[0].color.float32[0] =
		vk.sceneWorldRenderedThisFrame && vk.world.hasGlobalFog ? vk.world.globalFogColor[0] : 0.0f;
	clearValues[0].color.float32[1] =
		vk.sceneWorldRenderedThisFrame && vk.world.hasGlobalFog ? vk.world.globalFogColor[1] : 0.0f;
	clearValues[0].color.float32[2] =
		vk.sceneWorldRenderedThisFrame && vk.world.hasGlobalFog ? vk.world.globalFogColor[2] : 0.0f;
	clearValues[0].color.float32[3] = 1.0f;
	clearValues[1].depthStencil.depth = 1.0f;
	clearValues[1].depthStencil.stencil = 0;

	VkRenderPassBeginInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassInfo.renderPass = vk.renderPass;
	renderPassInfo.framebuffer = vk.framebuffers[eye][imageIndex];
	renderPassInfo.renderArea.offset.x = 0;
	renderPassInfo.renderArea.offset.y = 0;
	renderPassInfo.renderArea.extent.width = vk.viewConfiguration[eye].recommendedImageRectWidth;
	renderPassInfo.renderArea.extent.height = vk.viewConfiguration[eye].recommendedImageRectHeight;
	renderPassInfo.clearValueCount = ARRAY_LEN( clearValues );
	renderPassInfo.pClearValues = clearValues;

	vkCmdBeginRenderPass( vk.commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE );

	VkViewport viewport = {};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = static_cast<float>( vk.viewConfiguration[eye].recommendedImageRectWidth );
	viewport.height = static_cast<float>( vk.viewConfiguration[eye].recommendedImageRectHeight );
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport( vk.commandBuffer, 0, 1, &viewport );

	VkRect2D scissor = {};
	scissor.extent.width = vk.viewConfiguration[eye].recommendedImageRectWidth;
	scissor.extent.height = vk.viewConfiguration[eye].recommendedImageRectHeight;
	vkCmdSetScissor( vk.commandBuffer, 0, 1, &scissor );

	if ( !vk.sceneWorldRenderedThisFrame )
	{
		vkCmdBindPipeline( vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vk.pipeline );
		vkCmdPushConstants(
			vk.commandBuffer,
			vk.pipelineLayout,
			VK_SHADER_STAGE_FRAGMENT_BIT,
			0,
			sizeof( float ) * 4,
			tint );
		vkCmdDraw( vk.commandBuffer, 3, 1, 0, 0 );
	}

	if ( vk.sceneWorldRenderedThisFrame )
	{
		VK_RecordWorld( eye );
	}

	if ( vk.sceneWorldRenderedThisFrame && vk.diagnostic3dPipeline != VK_NULL_HANDLE )
	{
		VK_RecordDiagnosticWorld( eye );
	}

	if ( !vk.sceneWorldRenderedThisFrame && !vk.screenScenes.empty() )
	{
		size_t firstRect = 0;
		for ( size_t sceneIndex = 0; sceneIndex < vk.screenScenes.size(); ++sceneIndex )
		{
			const size_t endRect = std::max(
				firstRect,
				std::min( vk.screenScenes[sceneIndex].rectCountBefore, vk.rects.size() ) );
			VK_RecordScreenRects( eye, firstRect, endRect );
			VK_RecordScreenScenes( eye, sceneIndex, sceneIndex + 1 );
			firstRect = endRect;
		}
		VK_RecordScreenRects( eye, firstRect, vk.rects.size() );
	}
	else
	{
		VK_RecordScreenRects( eye, 0, vk.rects.size() );
	}

	vkCmdEndRenderPass( vk.commandBuffer );

	if ( !VK_CheckVk( vkEndCommandBuffer( vk.commandBuffer ), "vkEndCommandBuffer" ) )
	{
		return false;
	}

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &vk.commandBuffer;

	if ( !VK_CheckVk( vkQueueSubmit( vk.queue, 1, &submitInfo, VK_NULL_HANDLE ), "vkQueueSubmit" ) )
	{
		return false;
	}

	return VK_CheckVk( vkQueueWaitIdle( vk.queue ), "vkQueueWaitIdle" );
}

static bool VK_RenderEye( int eye, const float tint[4] )
{
	XrSwapchainImageAcquireInfo acquireInfo = {};
	acquireInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;
	if ( !VK_CheckXr( xrAcquireSwapchainImage( vk.colorSwapchain[eye], &acquireInfo, &vk.colorImageIndex[eye] ),
			"xrAcquireSwapchainImage" ) )
	{
		return false;
	}

	XrSwapchainImageWaitInfo waitInfo = {};
	waitInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
	waitInfo.timeout = XR_INFINITE_DURATION;
	if ( !VK_CheckXr( xrWaitSwapchainImage( vk.colorSwapchain[eye], &waitInfo ), "xrWaitSwapchainImage" ) )
	{
		return false;
	}

	const bool recorded = VK_RecordTestPattern( eye, vk.colorImageIndex[eye], tint );

	XrSwapchainImageReleaseInfo releaseInfo = {};
	releaseInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
	const bool released = VK_CheckXr( xrReleaseSwapchainImage( vk.colorSwapchain[eye], &releaseInfo ),
		"xrReleaseSwapchainImage" );

	return recorded && released;
}

bool VK_Backend_Init()
{
	if ( vk.initialized )
	{
		return true;
	}

	std::vector<vk_texture_name_t> pendingModelNames = std::move( vk.modelNames );
	std::vector<vk_texture_name_t> pendingSkinNames = std::move( vk.skinNames );
	std::vector<vk_model_t> pendingModels = std::move( vk.models );
	std::vector<vk_skin_t> pendingSkins = std::move( vk.skins );
	const uint32_t pendingModelRegistrationCount = vk.modelRegistrationCount;
	const uint32_t pendingSkinRegistrationCount = vk.skinRegistrationCount;
	VK_Backend_Clear();
	vk.modelNames = std::move( pendingModelNames );
	vk.skinNames = std::move( pendingSkinNames );
	vk.models = std::move( pendingModels );
	vk.skins = std::move( pendingSkins );
	vk.modelRegistrationCount = pendingModelRegistrationCount;
	vk.skinRegistrationCount = pendingSkinRegistrationCount;
	vk.diagnosticWorldCvar = ri.Cvar_Get( "rd_vulkanDiagnosticWorld", "0", 0 );

	if ( !VK_CreateXrInstance() ||
		 !VK_GetXrSystem() ||
		 !VK_LoadXrVulkanEntryPoints() ||
		 !VK_CreateVulkanInstance() ||
		 !VK_SelectPhysicalDevice() ||
		 !VK_SelectQueueFamily() ||
		 !VK_CreateVulkanDevice() ||
		 !VK_CreateCommandResources() ||
		 !VK_CreateTextureDescriptors() ||
		 !VK_CreateXrSession() ||
		 !VK_CreateReferenceSpace( XR_REFERENCE_SPACE_TYPE_VIEW, &vk.viewSpace, "xrCreateReferenceSpace(VIEW)" ) ||
		 !VK_CreateReferenceSpace( XR_REFERENCE_SPACE_TYPE_LOCAL, &vk.localSpace, "xrCreateReferenceSpace(LOCAL)" ) ||
		 !VK_QueryViewConfiguration() ||
		 !VK_CreateSwapchains() )
	{
		VK_Backend_Shutdown();
		return false;
	}
	if ( !VK_CreateFallbackTexture() )
	{
		VK_Backend_Shutdown();
		return false;
	}

	VK_CreateReferenceSpace( XR_REFERENCE_SPACE_TYPE_STAGE, &vk.stageSpace, "xrCreateReferenceSpace(STAGE)" );

	vk.initialized = true;
	VK_LoadPendingRegistrations();
	ri.Printf( PRINT_ALL, "rd-vulkan: initialized Vulkan %u.%u.%u through OpenXR %s\n",
		VK_API_VERSION_MAJOR( vk.apiVersion ),
		VK_API_VERSION_MINOR( vk.apiVersion ),
		VK_API_VERSION_PATCH( vk.apiVersion ),
		XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME );
	return true;
}

void VK_Backend_Shutdown()
{
	if ( vk.device != VK_NULL_HANDLE )
	{
		vkDeviceWaitIdle( vk.device );
	}

	VK_DestroyWorldGeometry();
	VK_DestroyModelRegistry();
	if ( vk.skinnedVertexMapped != nullptr && vk.device != VK_NULL_HANDLE )
	{
		vkUnmapMemory( vk.device, vk.skinnedVertexMemory );
		vk.skinnedVertexMapped = nullptr;
	}
	VK_DestroyBuffer( &vk.skinnedVertexBuffer, &vk.skinnedVertexMemory );

	for ( vk_texture_t &texture : vk.textures )
	{
		VK_DestroyTexture( texture );
	}
	vk.textures.clear();
	vk.textureNames.clear();

	for ( int eye = 0; eye < VK_BACKEND_EYE_COUNT; ++eye )
	{
		for ( uint32_t i = 0; i < vk.colorImageCount[eye]; ++i )
		{
			if ( vk.framebuffers[eye] != nullptr && vk.framebuffers[eye][i] != VK_NULL_HANDLE )
			{
				vkDestroyFramebuffer( vk.device, vk.framebuffers[eye][i], nullptr );
			}
			if ( vk.colorImageViews[eye] != nullptr && vk.colorImageViews[eye][i] != VK_NULL_HANDLE )
			{
				vkDestroyImageView( vk.device, vk.colorImageViews[eye][i], nullptr );
			}
		}
		std::free( vk.framebuffers[eye] );
		std::free( vk.colorImageViews[eye] );
		vk.framebuffers[eye] = nullptr;
		vk.colorImageViews[eye] = nullptr;

		VK_DestroyEyeDepthResources( eye );

		if ( vk.colorSwapchain[eye] != XR_NULL_HANDLE )
		{
			xrDestroySwapchain( vk.colorSwapchain[eye] );
		}
		std::free( vk.colorImages[eye] );
		vk.colorImages[eye] = nullptr;
	}

	if ( vk.pipeline != VK_NULL_HANDLE )
	{
		vkDestroyPipeline( vk.device, vk.pipeline, nullptr );
	}
	if ( vk.rectPipeline != VK_NULL_HANDLE )
	{
		vkDestroyPipeline( vk.device, vk.rectPipeline, nullptr );
	}
	if ( vk.texturedRectPipeline != VK_NULL_HANDLE )
	{
		vkDestroyPipeline( vk.device, vk.texturedRectPipeline, nullptr );
	}
	if ( vk.texturedRectOpaquePipeline != VK_NULL_HANDLE )
	{
		vkDestroyPipeline( vk.device, vk.texturedRectOpaquePipeline, nullptr );
	}
	if ( vk.texturedRectAdditivePipeline != VK_NULL_HANDLE )
	{
		vkDestroyPipeline( vk.device, vk.texturedRectAdditivePipeline, nullptr );
	}
	if ( vk.texturedRectDestinationColorAdditivePipeline != VK_NULL_HANDLE )
	{
		vkDestroyPipeline( vk.device, vk.texturedRectDestinationColorAdditivePipeline, nullptr );
	}
	if ( vk.texturedRectOneMinusDestinationAlphaAdditivePipeline != VK_NULL_HANDLE )
	{
		vkDestroyPipeline( vk.device, vk.texturedRectOneMinusDestinationAlphaAdditivePipeline, nullptr );
	}
	if ( vk.texturedRectModulatePipeline != VK_NULL_HANDLE )
	{
		vkDestroyPipeline( vk.device, vk.texturedRectModulatePipeline, nullptr );
	}
	if ( vk.diagnostic3dPipeline != VK_NULL_HANDLE )
	{
		vkDestroyPipeline( vk.device, vk.diagnostic3dPipeline, nullptr );
	}
	if ( vk.worldPipeline != VK_NULL_HANDLE )
	{
		vkDestroyPipeline( vk.device, vk.worldPipeline, nullptr );
	}
	if ( vk.worldAlphaPipeline != VK_NULL_HANDLE )
	{
		vkDestroyPipeline( vk.device, vk.worldAlphaPipeline, nullptr );
	}
	if ( vk.worldAlphaDepthWritePipeline != VK_NULL_HANDLE )
	{
		vkDestroyPipeline( vk.device, vk.worldAlphaDepthWritePipeline, nullptr );
	}
	if ( vk.worldAdditivePipeline != VK_NULL_HANDLE )
	{
		vkDestroyPipeline( vk.device, vk.worldAdditivePipeline, nullptr );
	}
	if ( vk.worldSourceAlphaAdditivePipeline != VK_NULL_HANDLE )
	{
		vkDestroyPipeline( vk.device, vk.worldSourceAlphaAdditivePipeline, nullptr );
	}
	if ( vk.worldOneSourceAlphaPipeline != VK_NULL_HANDLE )
	{
		vkDestroyPipeline( vk.device, vk.worldOneSourceAlphaPipeline, nullptr );
	}
	if ( vk.worldDestinationColorAdditivePipeline != VK_NULL_HANDLE )
	{
		vkDestroyPipeline( vk.device, vk.worldDestinationColorAdditivePipeline, nullptr );
	}
	if ( vk.worldOneMinusDestinationAlphaAdditivePipeline != VK_NULL_HANDLE )
	{
		vkDestroyPipeline( vk.device, vk.worldOneMinusDestinationAlphaAdditivePipeline, nullptr );
	}
	if ( vk.worldModulatePipeline != VK_NULL_HANDLE )
	{
		vkDestroyPipeline( vk.device, vk.worldModulatePipeline, nullptr );
	}
	if ( vk.worldDoubleModulatePipeline != VK_NULL_HANDLE )
	{
		vkDestroyPipeline( vk.device, vk.worldDoubleModulatePipeline, nullptr );
	}
	if ( vk.pipelineLayout != VK_NULL_HANDLE )
	{
		vkDestroyPipelineLayout( vk.device, vk.pipelineLayout, nullptr );
	}
	if ( vk.renderPass != VK_NULL_HANDLE )
	{
		vkDestroyRenderPass( vk.device, vk.renderPass, nullptr );
	}
	if ( vk.textureSampler != VK_NULL_HANDLE )
	{
		vkDestroySampler( vk.device, vk.textureSampler, nullptr );
	}
	if ( vk.worldTextureSampler != VK_NULL_HANDLE )
	{
		vkDestroySampler( vk.device, vk.worldTextureSampler, nullptr );
	}
	if ( vk.descriptorPool != VK_NULL_HANDLE )
	{
		vkDestroyDescriptorPool( vk.device, vk.descriptorPool, nullptr );
	}
	if ( vk.textureSetLayout != VK_NULL_HANDLE )
	{
		vkDestroyDescriptorSetLayout( vk.device, vk.textureSetLayout, nullptr );
	}
	if ( vk.viewSpace != XR_NULL_HANDLE )
	{
		xrDestroySpace( vk.viewSpace );
	}
	if ( vk.localSpace != XR_NULL_HANDLE )
	{
		xrDestroySpace( vk.localSpace );
	}
	if ( vk.stageSpace != XR_NULL_HANDLE )
	{
		xrDestroySpace( vk.stageSpace );
	}
	if ( vk.xrSession != XR_NULL_HANDLE )
	{
		xrDestroySession( vk.xrSession );
	}
	if ( vk.commandPool != VK_NULL_HANDLE )
	{
		vkDestroyCommandPool( vk.device, vk.commandPool, nullptr );
	}
	if ( vk.device != VK_NULL_HANDLE )
	{
		vkDestroyDevice( vk.device, nullptr );
	}
	if ( vk.instance != VK_NULL_HANDLE )
	{
		vkDestroyInstance( vk.instance, nullptr );
	}
	if ( vk.xrInstance != XR_NULL_HANDLE )
	{
		xrDestroyInstance( vk.xrInstance );
	}

	VK_Backend_Clear();
}

bool VK_Backend_IsInitialized()
{
	return vk.initialized;
}

int VK_Backend_GetRecommendedWidth()
{
	return vk.viewConfiguration[0].recommendedImageRectWidth > 0
		? static_cast<int>( vk.viewConfiguration[0].recommendedImageRectWidth )
		: 1280;
}

int VK_Backend_GetRecommendedHeight()
{
	return vk.viewConfiguration[0].recommendedImageRectHeight > 0
		? static_cast<int>( vk.viewConfiguration[0].recommendedImageRectHeight )
		: 720;
}

static bool VK_WorldTextureUsable( qhandle_t texture );
static qhandle_t VK_WorldResolveTexture( qhandle_t shader );

static qhandle_t VK_FindPlaceholderHandle( const std::vector<vk_texture_name_t> &names, const char *name )
{
	for ( const vk_texture_name_t &registered : names )
	{
		if ( Q_stricmp( registered.name.c_str(), name ) == 0 )
		{
			return registered.handle;
		}
	}
	return 0;
}

static qhandle_t VK_RegisterPlaceholderHandle( std::vector<vk_texture_name_t> &names, const char *name )
{
	const qhandle_t existing = VK_FindPlaceholderHandle( names, name );
	if ( existing != 0 )
	{
		return existing;
	}

	const qhandle_t handle = static_cast<qhandle_t>( names.size() + 1 );
	names.push_back( { name, handle } );
	return handle;
}

static bool VK_ModelFileVisible( const char *name )
{
	if ( name == nullptr || name[0] == '\0' )
	{
		return false;
	}
	if ( name[0] == '*' || name[0] == '#' )
	{
		return true;
	}
	if ( ri.FS_ReadFile( name, nullptr ) > 0 )
	{
		return true;
	}

	const char *slash = std::strrchr( name, '/' );
	const char *dot = std::strrchr( name, '.' );
	if ( dot != nullptr && ( slash == nullptr || dot > slash ) )
	{
		return false;
	}

	const char *extensions[] = { ".md3", ".glm", ".mdr" };
	for ( const char *extension : extensions )
	{
		char filename[MAX_QPATH];
		Com_sprintf( filename, sizeof( filename ), "%s%s", name, extension );
		if ( ri.FS_ReadFile( filename, nullptr ) > 0 )
		{
			return true;
		}
	}
	return false;
}

static bool VK_ModelBufferRangeValid( size_t offset, size_t byteCount, size_t limit )
{
	return offset <= limit && byteCount <= limit - offset;
}

static bool VK_ModelSurfaceRangeValid( int offset, size_t byteCount, size_t surfaceSize )
{
	if ( offset < 0 )
	{
		return false;
	}
	return VK_ModelBufferRangeValid( static_cast<size_t>( offset ), byteCount, surfaceSize );
}

static const char *VK_ModelExtension( const char *name )
{
	const char *slash = std::strrchr( name, '/' );
	const char *dot = std::strrchr( name, '.' );
	return dot != nullptr && ( slash == nullptr || dot > slash ) ? dot : nullptr;
}

static bool VK_ReadModelFile( const char *name, char **buffer, std::string *resolvedName )
{
	*buffer = nullptr;
	if ( name == nullptr || name[0] == '\0' )
	{
		return false;
	}

	long size = ri.FS_ReadFile( name, reinterpret_cast<void **>( buffer ) );
	if ( size > 0 && *buffer != nullptr )
	{
		*resolvedName = name;
		return true;
	}
	if ( *buffer != nullptr )
	{
		ri.FS_FreeFile( *buffer );
		*buffer = nullptr;
	}

	if ( VK_ModelExtension( name ) == nullptr )
	{
		char md3Name[MAX_QPATH];
		Com_sprintf( md3Name, sizeof( md3Name ), "%s.md3", name );
		size = ri.FS_ReadFile( md3Name, reinterpret_cast<void **>( buffer ) );
		if ( size > 0 && *buffer != nullptr )
		{
			*resolvedName = md3Name;
			return true;
		}
		if ( *buffer != nullptr )
		{
			ri.FS_FreeFile( *buffer );
			*buffer = nullptr;
		}
	}

	return false;
}

static void VK_EnsureModelSlot( qhandle_t handle )
{
	if ( handle > 0 && static_cast<size_t>( handle ) >= vk.models.size() )
	{
		vk.models.resize( static_cast<size_t>( handle ) + 1 );
	}
}

static bool VK_LoadInlineModel( const char *name, qhandle_t handle )
{
	VK_EnsureModelSlot( handle );
	vk_model_t &model = vk.models[handle];
	model.name = name != nullptr ? name : "";
	model.type = VK_MODEL_INLINE_BSP;
	model.inlineModelIndex = std::atoi( name + 1 );
	return model.inlineModelIndex >= 0;
}

static bool VK_LoadMD3Surface(
	const byte *surfaceBase,
	size_t surfaceSize,
	const md3Surface_t *surface,
	vk_model_surface_t *loadedSurface )
{
	const int numFrames = LittleLong( surface->numFrames );
	const int numVerts = LittleLong( surface->numVerts );
	const int numTriangles = LittleLong( surface->numTriangles );
	const int numShaders = LittleLong( surface->numShaders );
	const int ofsTriangles = LittleLong( surface->ofsTriangles );
	const int ofsShaders = LittleLong( surface->ofsShaders );
	const int ofsSt = LittleLong( surface->ofsSt );
	const int ofsXyzNormals = LittleLong( surface->ofsXyzNormals );
	if ( numFrames <= 0 ||
		 numVerts <= 0 || numVerts > MD3_MAX_VERTS ||
		 numTriangles <= 0 || numTriangles > MD3_MAX_TRIANGLES )
	{
		return false;
	}

	const size_t triangleBytes = static_cast<size_t>( numTriangles ) * sizeof( md3Triangle_t );
	const size_t stBytes = static_cast<size_t>( numVerts ) * sizeof( md3St_t );
	const size_t xyzBytes =
		static_cast<size_t>( numFrames ) * static_cast<size_t>( numVerts ) * sizeof( md3XyzNormal_t );
	if ( !VK_ModelSurfaceRangeValid( ofsTriangles, triangleBytes, surfaceSize ) ||
		 !VK_ModelSurfaceRangeValid( ofsSt, stBytes, surfaceSize ) ||
		 !VK_ModelSurfaceRangeValid( ofsXyzNormals, xyzBytes, surfaceSize ) )
	{
		return false;
	}

	const md3Triangle_t *triangles = reinterpret_cast<const md3Triangle_t *>( surfaceBase + ofsTriangles );
	const md3St_t *texCoords = reinterpret_cast<const md3St_t *>( surfaceBase + ofsSt );
	const md3XyzNormal_t *positions = reinterpret_cast<const md3XyzNormal_t *>( surfaceBase + ofsXyzNormals );

	std::vector<vk_world_vertex_t> vertices;
	std::vector<uint32_t> indices;
	vertices.reserve( static_cast<size_t>( numVerts ) );
	indices.reserve( static_cast<size_t>( numTriangles ) * 3 );

	for ( int i = 0; i < numVerts; ++i )
	{
		vk_world_vertex_t vertex = {};
		vertex.position[0] = LittleShort( positions[i].xyz[0] ) * static_cast<float>( MD3_XYZ_SCALE );
		vertex.position[1] = LittleShort( positions[i].xyz[1] ) * static_cast<float>( MD3_XYZ_SCALE );
		vertex.position[2] = LittleShort( positions[i].xyz[2] ) * static_cast<float>( MD3_XYZ_SCALE );
		vertex.color[0] = 1.0f;
		vertex.color[1] = 1.0f;
		vertex.color[2] = 1.0f;
		vertex.color[3] = 1.0f;
		vertex.uv[0] = LittleFloat( texCoords[i].st[0] );
		vertex.uv[1] = LittleFloat( texCoords[i].st[1] );
		vertices.push_back( vertex );
	}

	for ( int i = 0; i < numTriangles; ++i )
	{
		for ( int j = 0; j < 3; ++j )
		{
			const int index = LittleLong( triangles[i].indexes[j] );
			if ( index < 0 || index >= numVerts )
			{
				return false;
			}
			indices.push_back( static_cast<uint32_t>( index ) );
		}
	}
	loadedSurface->glmIndices = indices;

	qhandle_t shader = 1;
	if ( numShaders > 0 && VK_ModelSurfaceRangeValid(
			ofsShaders,
			static_cast<size_t>( numShaders ) * sizeof( md3Shader_t ),
			surfaceSize ) )
	{
		const md3Shader_t *shaders = reinterpret_cast<const md3Shader_t *>( surfaceBase + ofsShaders );
		char shaderName[MAX_QPATH];
		Q_strncpyz( shaderName, shaders[0].name, sizeof( shaderName ) );
		if ( shaderName[0] != '\0' )
		{
			shader = VK_Backend_RegisterTexture( shaderName );
			if ( VK_WorldResolveTexture( shader ) == 2 )
			{
				char extensionlessName[MAX_QPATH];
				Q_strncpyz( extensionlessName, shaderName, sizeof( extensionlessName ) );
				COM_StripExtension( extensionlessName, extensionlessName, sizeof( extensionlessName ) );
				if ( Q_stricmp( extensionlessName, shaderName ) != 0 )
				{
					const qhandle_t extensionlessShader = VK_Backend_RegisterTexture( extensionlessName );
					if ( VK_WorldResolveTexture( extensionlessShader ) != 2 )
					{
						shader = extensionlessShader;
					}
				}
				if ( VK_WorldResolveTexture( shader ) == 2 )
				{
					shader = 1;
				}
			}
		}
	}

	const VkDeviceSize vertexBytes = static_cast<VkDeviceSize>( vertices.size() * sizeof( vertices[0] ) );
	const VkDeviceSize indexBytes = static_cast<VkDeviceSize>( indices.size() * sizeof( indices[0] ) );
	if ( !VK_UploadBuffer( vertices.data(), vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			&loadedSurface->vertexBuffer, &loadedSurface->vertexMemory, "model vertex" ) ||
		 !VK_UploadBuffer( indices.data(), indexBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			&loadedSurface->indexBuffer, &loadedSurface->indexMemory, "model index" ) )
	{
		VK_DestroyModelSurface( *loadedSurface );
		return false;
	}

	loadedSurface->vertexCount = static_cast<uint32_t>( vertices.size() );
	loadedSurface->indexCount = static_cast<uint32_t>( indices.size() );
	loadedSurface->glmBaseVertices = vertices;
	loadedSurface->shader = shader;
	return true;
}

static bool VK_LoadMD3Model( const char *name, qhandle_t handle )
{
	char *buffer = nullptr;
	std::string resolvedName;
	if ( !VK_ReadModelFile( name, &buffer, &resolvedName ) )
	{
		return false;
	}

	const long fileSizeLong = ri.FS_ReadFile( resolvedName.c_str(), nullptr );
	const size_t fileSize = fileSizeLong > 0 ? static_cast<size_t>( fileSizeLong ) : 0;
	if ( buffer == nullptr || fileSize < sizeof( md3Header_t ) )
	{
		if ( buffer != nullptr )
		{
			ri.FS_FreeFile( buffer );
		}
		return false;
	}

	const byte *fileBase = reinterpret_cast<const byte *>( buffer );
	const md3Header_t *header = reinterpret_cast<const md3Header_t *>( fileBase );
	const int ident = LittleLong( header->ident );
	const int version = LittleLong( header->version );
	const int numSurfaces = LittleLong( header->numSurfaces );
	const int ofsSurfaces = LittleLong( header->ofsSurfaces );
	const int ofsEnd = LittleLong( header->ofsEnd );
	if ( ident != MD3_IDENT || version != MD3_VERSION ||
		 numSurfaces <= 0 || numSurfaces > MD3_MAX_SURFACES ||
		 ofsEnd <= 0 || static_cast<size_t>( ofsEnd ) > fileSize ||
		 !VK_ModelBufferRangeValid( static_cast<size_t>( ofsSurfaces ), sizeof( md3Surface_t ), static_cast<size_t>( ofsEnd ) ) )
	{
		ri.FS_FreeFile( buffer );
		return false;
	}

	VK_EnsureModelSlot( handle );
	vk_model_t model = {};
	model.name = resolvedName;
	model.type = VK_MODEL_MD3;
	model.inlineModelIndex = -1;
	model.boneCount = 0;
	model.animationHandle = 0;

	size_t surfaceOffset = static_cast<size_t>( ofsSurfaces );
	for ( int i = 0; i < numSurfaces; ++i )
	{
		if ( !VK_ModelBufferRangeValid( surfaceOffset, sizeof( md3Surface_t ), static_cast<size_t>( ofsEnd ) ) )
		{
			break;
		}
		const md3Surface_t *surface = reinterpret_cast<const md3Surface_t *>( fileBase + surfaceOffset );
		const int surfaceEnd = LittleLong( surface->ofsEnd );
		if ( surfaceEnd <= 0 ||
			 !VK_ModelBufferRangeValid( surfaceOffset, static_cast<size_t>( surfaceEnd ), static_cast<size_t>( ofsEnd ) ) )
		{
			break;
		}

		vk_model_surface_t loadedSurface = {};
		if ( VK_LoadMD3Surface( fileBase + surfaceOffset, static_cast<size_t>( surfaceEnd ), surface, &loadedSurface ) )
		{
			model.surfaces.push_back( loadedSurface );
		}
		surfaceOffset += static_cast<size_t>( surfaceEnd );
	}

	ri.FS_FreeFile( buffer );
	if ( model.surfaces.empty() )
	{
		return false;
	}

	vk.models[handle] = std::move( model );
	return true;
}

static std::shared_ptr<vk_gla_t> VK_LoadGLA( const char *animationName )
{
	if ( animationName == nullptr || animationName[0] == '\0' ||
		 Q_stricmp( animationName, sDEFAULT_GLA_NAME ) == 0 )
	{
		return nullptr;
	}

	std::string fileName = animationName;
	if ( fileName.size() < 4 ||
		 Q_stricmp( fileName.c_str() + fileName.size() - 4, ".gla" ) != 0 )
	{
		fileName += ".gla";
	}
	for ( const std::shared_ptr<vk_gla_t> &animation : vk.animations )
	{
		if ( animation != nullptr && Q_stricmp( animation->name.c_str(), fileName.c_str() ) == 0 )
		{
			return animation;
		}
	}

	char *buffer = nullptr;
	const long length = ri.FS_ReadFile( fileName.c_str(), reinterpret_cast<void **>( &buffer ) );
	if ( length < static_cast<long>( sizeof( mdxaHeader_t ) ) || buffer == nullptr )
	{
		if ( buffer != nullptr )
		{
			ri.FS_FreeFile( buffer );
		}
		ri.Printf( PRINT_WARNING, "rd-vulkan-ghoul2: could not load animation %s\n", fileName.c_str() );
		return nullptr;
	}

	const mdxaHeader_t *sourceHeader = reinterpret_cast<const mdxaHeader_t *>( buffer );
	const int ident = LittleLong( sourceHeader->ident );
	const int version = LittleLong( sourceHeader->version );
	const int numFrames = LittleLong( sourceHeader->numFrames );
	const int numBones = LittleLong( sourceHeader->numBones );
	const int ofsFrames = LittleLong( sourceHeader->ofsFrames );
	const int ofsCompBonePool = LittleLong( sourceHeader->ofsCompBonePool );
	const int ofsEnd = LittleLong( sourceHeader->ofsEnd );
	const size_t frameIndexBytes =
		numFrames > 0 && numBones > 0 ?
			static_cast<size_t>( numFrames ) * static_cast<size_t>( numBones ) * sizeof( mdxaIndex_t ) :
			0;
	if ( ident != MDXA_IDENT || version != MDXA_VERSION ||
		 numFrames <= 0 || numBones <= 0 || ofsEnd <= 0 ||
		 ofsEnd > length || ofsFrames < 0 || ofsCompBonePool < 0 ||
		 !VK_ModelBufferRangeValid(
			sizeof( mdxaHeader_t ),
			static_cast<size_t>( numBones ) * sizeof( int ),
			static_cast<size_t>( ofsEnd ) ) ||
		 !VK_ModelBufferRangeValid(
			static_cast<size_t>( ofsFrames ),
			frameIndexBytes,
			static_cast<size_t>( ofsEnd ) ) ||
		 static_cast<size_t>( ofsCompBonePool ) >= static_cast<size_t>( ofsEnd ) )
	{
		ri.Printf( PRINT_WARNING, "rd-vulkan-ghoul2: invalid animation file %s\n", fileName.c_str() );
		ri.FS_FreeFile( buffer );
		return nullptr;
	}

	std::shared_ptr<vk_gla_t> animation = std::make_shared<vk_gla_t>();
	animation->name = fileName;
	animation->data.assign(
		reinterpret_cast<const byte *>( buffer ),
		reinterpret_cast<const byte *>( buffer ) + ofsEnd );
	animation->numFrames = numFrames;
	animation->ofsFrames = ofsFrames;
	animation->ofsCompBonePool = ofsCompBonePool;
	ri.FS_FreeFile( buffer );

	const byte *base = animation->data.data();
	const mdxaSkelOffsets_t *offsets =
		reinterpret_cast<const mdxaSkelOffsets_t *>( base + sizeof( mdxaHeader_t ) );
	animation->bones.reserve( static_cast<size_t>( numBones ) );
	for ( int boneIndex = 0; boneIndex < numBones; ++boneIndex )
	{
		const int offset = LittleLong( offsets->offsets[boneIndex] );
		const size_t skeletonOffset = sizeof( mdxaHeader_t ) + static_cast<size_t>( std::max( offset, 0 ) );
		if ( offset < 0 ||
			 !VK_ModelBufferRangeValid(
				skeletonOffset,
				offsetof( mdxaSkel_t, children ),
				animation->data.size() ) )
		{
			ri.Printf( PRINT_WARNING, "rd-vulkan-ghoul2: invalid bone table in %s\n", fileName.c_str() );
			return nullptr;
		}

		const mdxaSkel_t *source =
			reinterpret_cast<const mdxaSkel_t *>( base + skeletonOffset );
		char boneName[MAX_QPATH];
		std::memcpy( boneName, source->name, sizeof( boneName ) );
		boneName[sizeof( boneName ) - 1] = '\0';

		vk_gla_bone_t bone = {};
		bone.name = boneName;
		bone.parent = LittleLong( source->parent );
		for ( int row = 0; row < 3; ++row )
		{
			for ( int column = 0; column < 4; ++column )
			{
				bone.basePose.matrix[row][column] =
					LittleFloat( source->BasePoseMat.matrix[row][column] );
				bone.basePoseInverse.matrix[row][column] =
					LittleFloat( source->BasePoseMatInv.matrix[row][column] );
			}
		}
		if ( bone.parent >= numBones ||
			 bone.parent == boneIndex ||
			 bone.parent < -1 )
		{
			ri.Printf( PRINT_WARNING, "rd-vulkan-ghoul2: invalid parent for bone %s in %s\n",
				bone.name.c_str(), fileName.c_str() );
			return nullptr;
		}
		animation->bones.push_back( std::move( bone ) );
	}

	vk.animations.push_back( animation );
	ri.Printf(
		PRINT_ALL,
		"rd-vulkan-ghoul2: loaded %s (%d frames, %d bones)\n",
		fileName.c_str(),
		numFrames,
		numBones );
	return animation;
}

struct vk_glm_hierarchy_t
{
	std::string name;
	std::string shader;
	int parentIndex;
	unsigned int flags;
};

static bool VK_ParseGLMHierarchy(
	const byte *fileBase,
	size_t fileSize,
	int numSurfaces,
	std::vector<vk_glm_hierarchy_t> *hierarchy )
{
	if ( fileBase == nullptr || hierarchy == nullptr || numSurfaces <= 0 ||
		 !VK_ModelBufferRangeValid(
			sizeof( mdxmHeader_t ),
			static_cast<size_t>( numSurfaces ) * sizeof( int ),
			fileSize ) )
	{
		return false;
	}

	const mdxmHierarchyOffsets_t *hierarchyOffsets =
		reinterpret_cast<const mdxmHierarchyOffsets_t *>( fileBase + sizeof( mdxmHeader_t ) );
	hierarchy->clear();
	hierarchy->reserve( static_cast<size_t>( numSurfaces ) );
	for ( int i = 0; i < numSurfaces; ++i )
	{
		const int offset = LittleLong( hierarchyOffsets->offsets[i] );
		const size_t tableOffset = sizeof( mdxmHeader_t );
		if ( offset < 0 ||
			 !VK_ModelBufferRangeValid(
				tableOffset + static_cast<size_t>( offset ),
				offsetof( mdxmSurfHierarchy_t, childIndexes ),
				fileSize ) )
		{
			return false;
		}
		const mdxmSurfHierarchy_t *surfaceHierarchy =
			reinterpret_cast<const mdxmSurfHierarchy_t *>(
				reinterpret_cast<const byte *>( hierarchyOffsets ) + offset );
		char shaderName[MAX_QPATH];
		char surfaceName[MAX_QPATH];
		std::memcpy( shaderName, surfaceHierarchy->shader, sizeof( shaderName ) );
		shaderName[sizeof( shaderName ) - 1] = '\0';
		std::memcpy( surfaceName, surfaceHierarchy->name, sizeof( surfaceName ) );
		surfaceName[sizeof( surfaceName ) - 1] = '\0';
		Q_strlwr( surfaceName );
#ifndef JK2_MODE
		const size_t surfaceNameLength = std::strlen( surfaceName );
		if ( surfaceNameLength > 4 &&
			 Q_stricmp( surfaceName + surfaceNameLength - 4, "_off" ) == 0 )
		{
			surfaceName[surfaceNameLength - 4] = '\0';
		}
#endif
		hierarchy->push_back( {
			surfaceName,
			shaderName,
			LittleLong( surfaceHierarchy->parentIndex ),
			static_cast<unsigned int>(
				LittleLong( static_cast<int>( surfaceHierarchy->flags ) ) ),
		} );
	}
	return true;
}

static bool VK_LoadGLMSurface(
	const byte *surfaceBase,
	size_t surfaceSize,
	const mdxmSurface_t *surface,
	const vk_glm_hierarchy_t &hierarchy,
	int surfaceIndex,
	vk_model_surface_t *loadedSurface )
{
	const int numVerts = LittleLong( surface->numVerts );
	const int numTriangles = LittleLong( surface->numTriangles );
	const int numBoneReferences = LittleLong( surface->numBoneReferences );
	const int ofsVerts = LittleLong( surface->ofsVerts );
	const int ofsTriangles = LittleLong( surface->ofsTriangles );
	const int ofsBoneReferences = LittleLong( surface->ofsBoneReferences );
	if ( numVerts <= 0 || numTriangles <= 0 || numBoneReferences <= 0 )
	{
		return false;
	}

	const size_t vertexBytes =
		static_cast<size_t>( numVerts ) *
		( sizeof( mdxmVertex_t ) + sizeof( mdxmVertexTexCoord_t ) );
	const size_t triangleBytes =
		static_cast<size_t>( numTriangles ) * sizeof( mdxmTriangle_t );
	if ( !VK_ModelSurfaceRangeValid( ofsVerts, vertexBytes, surfaceSize ) ||
		 !VK_ModelSurfaceRangeValid( ofsTriangles, triangleBytes, surfaceSize ) ||
		 !VK_ModelSurfaceRangeValid(
			ofsBoneReferences,
			static_cast<size_t>( numBoneReferences ) * sizeof( int ),
			surfaceSize ) )
	{
		return false;
	}

	const mdxmVertex_t *sourceVertices =
		reinterpret_cast<const mdxmVertex_t *>( surfaceBase + ofsVerts );
	const mdxmVertexTexCoord_t *sourceTexCoords =
		reinterpret_cast<const mdxmVertexTexCoord_t *>( sourceVertices + numVerts );
	const mdxmTriangle_t *sourceTriangles =
		reinterpret_cast<const mdxmTriangle_t *>( surfaceBase + ofsTriangles );
	const int *sourceBoneReferences =
		reinterpret_cast<const int *>( surfaceBase + ofsBoneReferences );

	std::vector<vk_world_vertex_t> vertices;
	std::vector<uint32_t> indices;
	vertices.reserve( static_cast<size_t>( numVerts ) );
	indices.reserve( static_cast<size_t>( numTriangles ) * 3 );
	for ( int i = 0; i < numVerts; ++i )
	{
		vk_world_vertex_t vertex = {};
		vertex.position[0] = LittleFloat( sourceVertices[i].vertCoords[0] );
		vertex.position[1] = LittleFloat( sourceVertices[i].vertCoords[1] );
		vertex.position[2] = LittleFloat( sourceVertices[i].vertCoords[2] );
		vertex.color[0] = 1.0f;
		vertex.color[1] = 1.0f;
		vertex.color[2] = 1.0f;
		vertex.color[3] = 1.0f;
		vertex.uv[0] = LittleFloat( sourceTexCoords[i].texCoords[0] );
		vertex.uv[1] = LittleFloat( sourceTexCoords[i].texCoords[1] );
		vertices.push_back( vertex );

		mdxmVertex_t sourceVertex = sourceVertices[i];
		for ( int component = 0; component < 3; ++component )
		{
			sourceVertex.normal[component] = LittleFloat( sourceVertex.normal[component] );
			sourceVertex.vertCoords[component] = LittleFloat( sourceVertex.vertCoords[component] );
		}
		sourceVertex.uiNmWeightsAndBoneIndexes =
			static_cast<unsigned int>(
				LittleLong( static_cast<int>( sourceVertex.uiNmWeightsAndBoneIndexes ) ) );
		loadedSurface->glmVertices.push_back( sourceVertex );
	}
	loadedSurface->glmBoneReferences.reserve( static_cast<size_t>( numBoneReferences ) );
	for ( int i = 0; i < numBoneReferences; ++i )
	{
		loadedSurface->glmBoneReferences.push_back( LittleLong( sourceBoneReferences[i] ) );
	}

	for ( int i = 0; i < numTriangles; ++i )
	{
		for ( int j = 0; j < 3; ++j )
		{
			const int index = LittleLong( sourceTriangles[i].indexes[j] );
			if ( index < 0 || index >= numVerts )
			{
				return false;
			}
			indices.push_back( static_cast<uint32_t>( index ) );
		}
	}
	loadedSurface->glmIndices = indices;

	qhandle_t shader = 1;
	if ( !hierarchy.shader.empty() && hierarchy.shader[0] != '[' )
	{
		shader = VK_Backend_RegisterTexture( hierarchy.shader.c_str() );
		if ( VK_WorldResolveTexture( shader ) == 2 )
		{
			shader = 1;
		}
	}

	const VkDeviceSize uploadVertexBytes =
		static_cast<VkDeviceSize>( vertices.size() * sizeof( vertices[0] ) );
	const VkDeviceSize uploadIndexBytes =
		static_cast<VkDeviceSize>( indices.size() * sizeof( indices[0] ) );
	if ( !VK_UploadBuffer( vertices.data(), uploadVertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			&loadedSurface->vertexBuffer, &loadedSurface->vertexMemory, "GLM vertex" ) ||
		 !VK_UploadBuffer( indices.data(), uploadIndexBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			&loadedSurface->indexBuffer, &loadedSurface->indexMemory, "GLM index" ) )
	{
		VK_DestroyModelSurface( *loadedSurface );
		return false;
	}

	loadedSurface->vertexCount = static_cast<uint32_t>( vertices.size() );
	loadedSurface->indexCount = static_cast<uint32_t>( indices.size() );
	loadedSurface->glmBaseVertices = vertices;
	loadedSurface->shader = shader;
	loadedSurface->name = hierarchy.name;
	loadedSurface->modelSurfaceIndex = surfaceIndex;
	loadedSurface->parentSurfaceIndex = hierarchy.parentIndex;
	loadedSurface->defaultFlags = hierarchy.flags;
	return true;
}

static void VK_AuditGLMModel( const vk_model_t &model, int declaredSurfaceCount )
{
	size_t vertexCount = 0;
	size_t triangleCount = 0;
	size_t invalidBoneReferences = 0;
	size_t invalidVertexBoneIndices = 0;
	size_t invalidWeights = 0;
	size_t nonFiniteValues = 0;
	size_t degenerateTriangles = 0;
	size_t fallbackShaders = 0;

	for ( const vk_model_surface_t &surface : model.surfaces )
	{
		vertexCount += surface.glmVertices.size();
		triangleCount += surface.glmIndices.size() / 3;
		fallbackShaders += surface.shader <= 1 ? 1 : 0;
		for ( int boneReference : surface.glmBoneReferences )
		{
			invalidBoneReferences +=
				boneReference < 0 || boneReference >= model.boneCount ? 1 : 0;
		}
		for ( const mdxmVertex_t &vertex : surface.glmVertices )
		{
			for ( int component = 0; component < 3; ++component )
			{
				nonFiniteValues +=
					!std::isfinite( vertex.vertCoords[component] ) ||
					!std::isfinite( vertex.normal[component] ) ? 1 : 0;
			}

			const int weightCount = G2_GetVertWeights( &vertex );
			float totalWeight = 0.0f;
			for ( int weightIndex = 0; weightIndex < weightCount; ++weightIndex )
			{
				const int localBoneIndex = G2_GetVertBoneIndex( &vertex, weightIndex );
				if ( localBoneIndex < 0 ||
					 static_cast<size_t>( localBoneIndex ) >= surface.glmBoneReferences.size() )
				{
					++invalidVertexBoneIndices;
				}
				const float weight =
					G2_GetVertBoneWeight( &vertex, weightIndex, totalWeight, weightCount );
				if ( !std::isfinite( weight ) || weight < -0.001f || weight > 1.001f )
				{
					++invalidWeights;
				}
			}
		}

		if ( ( surface.defaultFlags & G2SURFACEFLAG_ISBOLT ) == 0 )
		{
			for ( size_t i = 0; i + 2 < surface.glmIndices.size(); i += 3 )
			{
				const uint32_t i0 = surface.glmIndices[i + 0];
				const uint32_t i1 = surface.glmIndices[i + 1];
				const uint32_t i2 = surface.glmIndices[i + 2];
				if ( i0 >= surface.glmVertices.size() ||
					 i1 >= surface.glmVertices.size() ||
					 i2 >= surface.glmVertices.size() )
				{
					++degenerateTriangles;
					continue;
				}
				vec3_t edge0;
				vec3_t edge1;
				vec3_t cross;
				VectorSubtract(
					surface.glmVertices[i1].vertCoords,
					surface.glmVertices[i0].vertCoords,
					edge0 );
				VectorSubtract(
					surface.glmVertices[i2].vertCoords,
					surface.glmVertices[i0].vertCoords,
					edge1 );
				CrossProduct( edge0, edge1, cross );
				if ( !std::isfinite( VectorLengthSquared( cross ) ) ||
					 VectorLengthSquared( cross ) < 1.0e-10f )
				{
					++degenerateTriangles;
				}
			}
		}
	}

	const bool malformed =
		static_cast<int>( model.surfaces.size() ) != declaredSurfaceCount ||
		invalidBoneReferences != 0 ||
		invalidVertexBoneIndices != 0 ||
		invalidWeights != 0 ||
		nonFiniteValues != 0;
	const bool playerModel = model.name.compare( 0, 15, "models/players/" ) == 0;
	if ( malformed || playerModel )
	{
		ri.Printf(
			malformed ? PRINT_WARNING : PRINT_ALL,
			"rd-vulkan-ghoul2-audit: model=%s surfaces=%zu/%d vertices=%zu triangles=%zu "
			"badBoneRefs=%zu badVertexBones=%zu badWeights=%zu nonFinite=%zu "
			"degenerate=%zu fallbackShaders=%zu status=%s\n",
			model.name.c_str(),
			model.surfaces.size(),
			declaredSurfaceCount,
			vertexCount,
			triangleCount,
			invalidBoneReferences,
			invalidVertexBoneIndices,
			invalidWeights,
			nonFiniteValues,
			degenerateTriangles,
			fallbackShaders,
			malformed ? "FAILED" : "ok" );
	}
}

static bool VK_LoadGLMModel( const char *name, qhandle_t handle )
{
	char *buffer = nullptr;
	std::string resolvedName;
	if ( !VK_ReadModelFile( name, &buffer, &resolvedName ) )
	{
		return false;
	}

	const long fileSizeLong = ri.FS_ReadFile( resolvedName.c_str(), nullptr );
	const size_t fileSize = fileSizeLong > 0 ? static_cast<size_t>( fileSizeLong ) : 0;
	if ( buffer == nullptr || fileSize < sizeof( mdxmHeader_t ) )
	{
		if ( buffer != nullptr )
		{
			ri.FS_FreeFile( buffer );
		}
		return false;
	}

	const byte *fileBase = reinterpret_cast<const byte *>( buffer );
	const mdxmHeader_t *header = reinterpret_cast<const mdxmHeader_t *>( fileBase );
	const int ident = LittleLong( header->ident );
	const int version = LittleLong( header->version );
	const int numBones = LittleLong( header->numBones );
	const int numLODs = LittleLong( header->numLODs );
	const int numSurfaces = LittleLong( header->numSurfaces );
	const int ofsLODs = LittleLong( header->ofsLODs );
	const int ofsEnd = LittleLong( header->ofsEnd );
	if ( ident != MDXM_IDENT || version != MDXM_VERSION ||
		 numBones <= 0 || numLODs <= 0 || numSurfaces <= 0 ||
		 ofsEnd <= 0 || static_cast<size_t>( ofsEnd ) > fileSize ||
		 !VK_ModelBufferRangeValid(
			 sizeof( mdxmHeader_t ),
			 static_cast<size_t>( numSurfaces ) * sizeof( int ),
			 static_cast<size_t>( ofsEnd ) ) ||
		 ofsLODs < 0 ||
		 !VK_ModelBufferRangeValid(
			 static_cast<size_t>( ofsLODs ),
			 sizeof( mdxmLOD_t ) + static_cast<size_t>( numSurfaces ) * sizeof( int ),
			 static_cast<size_t>( ofsEnd ) ) )
	{
		ri.FS_FreeFile( buffer );
		return false;
	}

	std::vector<vk_glm_hierarchy_t> hierarchy;
	if ( !VK_ParseGLMHierarchy(
			fileBase, static_cast<size_t>( ofsEnd ), numSurfaces, &hierarchy ) )
	{
		ri.FS_FreeFile( buffer );
		return false;
	}

	const mdxmLOD_t *lod = reinterpret_cast<const mdxmLOD_t *>( fileBase + ofsLODs );
	const byte *lodOffsetsBase = reinterpret_cast<const byte *>( lod ) + sizeof( mdxmLOD_t );
	const mdxmLODSurfOffset_t *surfaceOffsets =
		reinterpret_cast<const mdxmLODSurfOffset_t *>( lodOffsetsBase );

	VK_EnsureModelSlot( handle );
	vk_model_t model = {};
	char animationName[MAX_QPATH];
	std::memcpy( animationName, header->animName, sizeof( animationName ) );
	animationName[sizeof( animationName ) - 1] = '\0';

	model.name = resolvedName;
	model.animationName = animationName;
	model.type = VK_MODEL_GLM;
	model.inlineModelIndex = -1;
	model.boneCount = numBones;
	model.animationHandle = VK_FindRegisteredModelHandle( animationName );
	model.animation = VK_LoadGLA( animationName );
	if ( model.animation != nullptr &&
		 static_cast<int>( model.animation->bones.size() ) != numBones )
	{
		ri.Printf(
			PRINT_WARNING,
			"rd-vulkan-ghoul2: %s references %d mesh bones but animation %s has %zu\n",
			resolvedName.c_str(),
			numBones,
			model.animation->name.c_str(),
			model.animation->bones.size() );
		model.animation.reset();
	}

	for ( int i = 0; i < numSurfaces; ++i )
	{
		const int surfaceOffset = LittleLong( surfaceOffsets->offsets[i] );
		const size_t lodOffsetsFileOffset =
			static_cast<size_t>( lodOffsetsBase - fileBase );
		if ( surfaceOffset < 0 ||
			 !VK_ModelBufferRangeValid(
				 lodOffsetsFileOffset + static_cast<size_t>( surfaceOffset ),
				 sizeof( mdxmSurface_t ),
				 static_cast<size_t>( ofsEnd ) ) )
		{
			continue;
		}
		const byte *surfaceBase = lodOffsetsBase + surfaceOffset;
		const mdxmSurface_t *surface = reinterpret_cast<const mdxmSurface_t *>( surfaceBase );
		const int surfaceEnd = LittleLong( surface->ofsEnd );
		if ( surfaceEnd <= 0 ||
			 !VK_ModelBufferRangeValid(
				 static_cast<size_t>( surfaceBase - fileBase ),
				 static_cast<size_t>( surfaceEnd ),
				 static_cast<size_t>( ofsEnd ) ) )
		{
			continue;
		}

		vk_model_surface_t loadedSurface = {};
		if ( VK_LoadGLMSurface(
				surfaceBase,
				static_cast<size_t>( surfaceEnd ),
				surface,
				hierarchy[i],
				i,
				&loadedSurface ) )
		{
			model.surfaces.push_back( loadedSurface );
		}
	}

	ri.FS_FreeFile( buffer );
	if ( model.surfaces.empty() )
	{
		return false;
	}

	VK_AuditGLMModel( model, numSurfaces );
	vk.models[handle] = std::move( model );
	return true;
}

static bool VK_LoadRegisteredModel( const char *name, qhandle_t handle )
{
	bool loaded = false;
	if ( name != nullptr && name[0] == '*' )
	{
		loaded = VK_LoadInlineModel( name, handle );
	}
	else if ( name != nullptr )
	{
		const char *extension = VK_ModelExtension( name );
		if ( extension == nullptr || Q_stricmp( extension, ".md3" ) == 0 )
		{
			loaded = VK_LoadMD3Model( name, handle );
		}
		else if ( Q_stricmp( extension, ".glm" ) == 0 )
		{
			loaded = VK_LoadGLMModel( name, handle );
		}
		else if ( Q_stricmp( extension, ".gla" ) == 0 )
		{
			VK_EnsureModelSlot( handle );
			vk_model_t model = {};
			model.name = name;
			model.animationName = name;
			model.type = VK_MODEL_GLA;
			model.inlineModelIndex = -1;
			model.animationHandle = handle;
			model.animation = VK_LoadGLA( name );
			model.boneCount =
				model.animation != nullptr ? static_cast<int>( model.animation->bones.size() ) : 0;
			loaded = model.animation != nullptr;
			if ( loaded )
			{
				vk.models[handle] = std::move( model );
			}
		}
	}

	if ( !loaded )
	{
		VK_EnsureModelSlot( handle );
		vk.models[handle].name = name != nullptr ? name : "";
		vk.models[handle].type = VK_MODEL_UNSUPPORTED;
		vk.models[handle].inlineModelIndex = -1;
		vk.models[handle].boneCount = 0;
		vk.models[handle].animationHandle = 0;
	}
	return loaded;
}

static void VK_LoadPendingGLMMetadata( const char *name, vk_model_t *model )
{
	if ( name == nullptr || model == nullptr )
	{
		return;
	}
	const char *extension = VK_ModelExtension( name );
	if ( extension == nullptr || Q_stricmp( extension, ".glm" ) != 0 )
	{
		return;
	}

	char *buffer = nullptr;
	const long length = ri.FS_ReadFile( name, reinterpret_cast<void **>( &buffer ) );
	if ( length >= static_cast<long>( sizeof( mdxmHeader_t ) ) && buffer != nullptr )
	{
		const mdxmHeader_t *header = reinterpret_cast<const mdxmHeader_t *>( buffer );
		if ( LittleLong( header->ident ) == MDXM_IDENT &&
			 LittleLong( header->version ) == MDXM_VERSION )
		{
			const int numSurfaces = LittleLong( header->numSurfaces );
			const int ofsEnd = LittleLong( header->ofsEnd );
			char animationName[MAX_QPATH];
			std::memcpy( animationName, header->animName, sizeof( animationName ) );
			animationName[sizeof( animationName ) - 1] = '\0';
			model->animationName = animationName;
			model->boneCount = LittleLong( header->numBones );
			model->animationHandle = VK_FindRegisteredModelHandle( animationName );
			model->animation = VK_LoadGLA( animationName );

			std::vector<vk_glm_hierarchy_t> hierarchy;
			if ( ofsEnd > 0 && ofsEnd <= length &&
				 VK_ParseGLMHierarchy(
					reinterpret_cast<const byte *>( buffer ),
					static_cast<size_t>( ofsEnd ),
					numSurfaces,
					&hierarchy ) )
			{
				model->surfaces.clear();
				for ( int i = 0; i < numSurfaces; ++i )
				{
					vk_model_surface_t surface = {};
					surface.name = hierarchy[i].name;
					surface.modelSurfaceIndex = i;
					surface.parentSurfaceIndex = hierarchy[i].parentIndex;
					surface.defaultFlags = hierarchy[i].flags;
					model->surfaces.push_back( std::move( surface ) );
				}
			}
		}
	}
	if ( buffer != nullptr )
	{
		ri.FS_FreeFile( buffer );
	}
}

qhandle_t VK_Backend_RegisterModel( const char *name )
{
	++vk.modelRegistrationCount;
	if ( !VK_ModelFileVisible( name ) )
	{
		if ( vk.modelRegistrationCount <= 20 || ( vk.modelRegistrationCount % 100 ) == 0 )
		{
			ri.Printf( PRINT_ALL, "rd-vulkan-scene: model registration %u missing: %s\n",
				vk.modelRegistrationCount, name != nullptr ? name : "<null>" );
		}
		return 0;
	}

	const qhandle_t handle = VK_RegisterPlaceholderHandle( vk.modelNames, name );
	VK_EnsureModelSlot( handle );
	if ( vk.models[handle].name.empty() || vk.models[handle].type == VK_MODEL_PENDING )
	{
		if ( !vk.initialized || vk.device == VK_NULL_HANDLE )
		{
			vk.models[handle].name = name != nullptr ? name : "";
			vk.models[handle].type = VK_MODEL_PENDING;
			vk.models[handle].inlineModelIndex = -1;
			vk.models[handle].boneCount = 0;
			vk.models[handle].animationHandle = 0;
			VK_LoadPendingGLMMetadata( name, &vk.models[handle] );
		}
		else
		{
			VK_LoadRegisteredModel( name, handle );
		}
	}

	if ( vk.modelRegistrationCount <= 20 || ( vk.modelRegistrationCount % 100 ) == 0 )
	{
		const char *type = "unsupported";
		if ( vk.models[handle].type == VK_MODEL_MD3 )
		{
			type = "md3";
		}
		else if ( vk.models[handle].type == VK_MODEL_GLM )
		{
			type = "glm";
		}
		else if ( vk.models[handle].type == VK_MODEL_GLA )
		{
			type = "gla";
		}
		else if ( vk.models[handle].type == VK_MODEL_INLINE_BSP )
		{
			type = "inline";
		}
		else if ( vk.models[handle].type == VK_MODEL_PENDING )
		{
			type = "pending";
		}
		ri.Printf( PRINT_ALL, "rd-vulkan-scene: model registration %u %s %d: %s\n",
			vk.modelRegistrationCount, type, handle, name != nullptr ? name : "<null>" );
	}
	return handle;
}

int VK_Backend_FindModelSurface(
	qhandle_t modelHandle,
	const char *surfaceName,
	unsigned int *defaultFlags )
{
	const vk_model_t *model = VK_ModelForHandle( modelHandle );
	if ( model == nullptr ||
		 ( model->type != VK_MODEL_GLM && model->type != VK_MODEL_PENDING ) ||
		 surfaceName == nullptr || surfaceName[0] == '\0' )
	{
		return -1;
	}

	for ( const vk_model_surface_t &surface : model->surfaces )
	{
		if ( Q_stricmp( surface.name.c_str(), surfaceName ) == 0 )
		{
			if ( defaultFlags != nullptr )
			{
				*defaultFlags = surface.defaultFlags;
			}
			return surface.modelSurfaceIndex;
		}
	}
	return -1;
}

int VK_Backend_FindModelBone( qhandle_t modelHandle, const char *boneName )
{
	const vk_model_t *model = VK_ModelForHandle( modelHandle );
	if ( model == nullptr || model->animation == nullptr ||
		 boneName == nullptr || boneName[0] == '\0' )
	{
		return -1;
	}

	for ( size_t i = 0; i < model->animation->bones.size(); ++i )
	{
		if ( Q_stricmp( model->animation->bones[i].name.c_str(), boneName ) == 0 )
		{
			return static_cast<int>( i );
		}
	}
	return -1;
}

int VK_Backend_GetModelAnimationFrameCount( qhandle_t modelHandle, int animationIndex )
{
	const vk_model_t *model = VK_ModelForHandle( modelHandle );
	if ( model == nullptr )
	{
		return 0;
	}
	const std::shared_ptr<vk_gla_t> animation =
		VK_ModelAnimation( *model, animationIndex );
	return animation != nullptr ? animation->numFrames : 0;
}

char *VK_Backend_GetModelAnimationName( qhandle_t modelHandle )
{
	const vk_model_t *model = VK_ModelForHandle( modelHandle );
	if ( model == nullptr || model->animationName.empty() )
	{
		return nullptr;
	}
	return const_cast<char *>( model->animationName.c_str() );
}

static void VK_SkipSkinWhitespaceAndComments( const char **cursor )
{
	for ( ;; )
	{
		while ( **cursor != '\0' &&
			 static_cast<unsigned char>( **cursor ) <= static_cast<unsigned char>( ' ' ) )
		{
			++( *cursor );
		}
		if ( ( *cursor )[0] == '/' && ( *cursor )[1] == '/' )
		{
			while ( **cursor != '\0' && **cursor != '\n' )
			{
				++( *cursor );
			}
			continue;
		}
		if ( ( *cursor )[0] == '/' && ( *cursor )[1] == '*' )
		{
			*cursor += 2;
			while ( **cursor != '\0' &&
				 !( ( *cursor )[0] == '*' && ( *cursor )[1] == '/' ) )
			{
				++( *cursor );
			}
			if ( **cursor != '\0' )
			{
				*cursor += 2;
			}
			continue;
		}
		return;
	}
}

static std::string VK_ParseSkinToken( const char **cursor )
{
	VK_SkipSkinWhitespaceAndComments( cursor );
	if ( **cursor == '\0' )
	{
		return {};
	}

	std::string token;
	if ( **cursor == '"' )
	{
		++( *cursor );
		while ( **cursor != '\0' && **cursor != '"' )
		{
			token.push_back( **cursor );
			++( *cursor );
		}
		if ( **cursor == '"' )
		{
			++( *cursor );
		}
		return token;
	}

	while ( **cursor != '\0' &&
		 static_cast<unsigned char>( **cursor ) > static_cast<unsigned char>( ' ' ) &&
		 **cursor != ',' )
	{
		token.push_back( **cursor );
		++( *cursor );
	}
	return token;
}

static bool VK_SplitSkinName( const char *name, std::vector<std::string> *filenames )
{
	const char *first = std::strchr( name, '|' );
	if ( first == nullptr )
	{
		filenames->push_back( name );
		return true;
	}

	const char *second = std::strchr( first + 1, '|' );
	const char *third = second != nullptr ? std::strchr( second + 1, '|' ) : nullptr;
	if ( second == nullptr || third == nullptr )
	{
		return false;
	}

	const std::string base( name, static_cast<size_t>( first - name ) );
	const std::string head( first + 1, static_cast<size_t>( second - first - 1 ) );
	const std::string torso( second + 1, static_cast<size_t>( third - second - 1 ) );
	const std::string lower( third + 1 );
	if ( head.empty() || torso.empty() || lower.empty() )
	{
		return false;
	}

	filenames->push_back( base + head + ".skin" );
	if ( torso != head )
	{
		filenames->push_back( base + torso + ".skin" );
	}
	if ( lower != head && lower != torso )
	{
		filenames->push_back( base + lower + ".skin" );
	}
	return true;
}

static bool VK_LoadSkinFile( const std::string &filename, vk_skin_t *skin )
{
	char *buffer = nullptr;
	const long length = ri.FS_ReadFile( filename.c_str(), reinterpret_cast<void **>( &buffer ) );
	if ( length <= 0 || buffer == nullptr )
	{
		if ( buffer != nullptr )
		{
			ri.FS_FreeFile( buffer );
		}
		ri.Printf( PRINT_WARNING, "rd-vulkan-scene: failed to load skin %s\n", filename.c_str() );
		return false;
	}

	std::string text( buffer, static_cast<size_t>( length ) );
	ri.FS_FreeFile( buffer );
	text.push_back( '\0' );
	const char *cursor = text.c_str();
	size_t loadedSurfaces = 0;
	for ( ;; )
	{
		std::string surfaceName = VK_ParseSkinToken( &cursor );
		if ( surfaceName.empty() )
		{
			break;
		}
		if ( *cursor == ',' )
		{
			++cursor;
		}
		std::string shaderName = VK_ParseSkinToken( &cursor );
		if ( shaderName.empty() )
		{
			break;
		}
		if ( Q_stricmpn( surfaceName.c_str(), "tag_", 4 ) == 0 )
		{
			continue;
		}

		std::transform(
			surfaceName.begin(),
			surfaceName.end(),
			surfaceName.begin(),
			[]( unsigned char character ) { return static_cast<char>( std::tolower( character ) ); } );
#ifndef JK2_MODE
		if ( surfaceName.size() > 4 &&
			 Q_stricmp( surfaceName.c_str() + surfaceName.size() - 4, "_off" ) == 0 )
		{
			if ( Q_stricmp( shaderName.c_str(), "*off" ) == 0 )
			{
				continue;
			}
			surfaceName.resize( surfaceName.size() - 4 );
		}
#endif

		const bool off = Q_stricmp( shaderName.c_str(), "*off" ) == 0;
		const qhandle_t shader = off ? 0 : VK_Backend_RegisterTexture( shaderName.c_str() );
		skin->surfaces.push_back( { surfaceName, shader, off } );
		++loadedSurfaces;
	}
	return loadedSurfaces > 0;
}

static void VK_LoadRegisteredSkin( vk_skin_t *skin )
{
	if ( skin == nullptr || skin->name.empty() || !skin->surfaces.empty() )
	{
		return;
	}

	std::vector<std::string> filenames;
	if ( !VK_SplitSkinName( skin->name.c_str(), &filenames ) )
	{
		return;
	}
	for ( const std::string &filename : filenames )
	{
		VK_LoadSkinFile( filename, skin );
	}
}

qhandle_t VK_Backend_RegisterSkin( const char *name )
{
	++vk.skinRegistrationCount;
	if ( name == nullptr || name[0] == '\0' )
	{
		return 0;
	}

	const qhandle_t handle = VK_RegisterPlaceholderHandle( vk.skinNames, name );
	if ( static_cast<size_t>( handle ) >= vk.skins.size() )
	{
		vk.skins.resize( static_cast<size_t>( handle ) + 1 );
	}
	vk_skin_t &skin = vk.skins[handle];
	if ( skin.name.empty() )
	{
		skin.name = name;
	}
	if ( vk.initialized && vk.device != VK_NULL_HANDLE )
	{
		VK_LoadRegisteredSkin( &skin );
	}
	if ( vk.skinRegistrationCount <= 10 || ( vk.skinRegistrationCount % 100 ) == 0 )
	{
		ri.Printf( PRINT_ALL, "rd-vulkan-scene: skin registration %u loaded %zu surfaces as %d: %s\n",
			vk.skinRegistrationCount, skin.surfaces.size(), handle, name );
	}
	return handle;
}

static void VK_LoadPendingRegistrations()
{
	uint32_t loadedModels = 0;
	for ( const vk_texture_name_t &registered : vk.modelNames )
	{
		if ( registered.handle <= 0 ||
			 static_cast<size_t>( registered.handle ) >= vk.models.size() ||
			 vk.models[registered.handle].type != VK_MODEL_PENDING )
		{
			continue;
		}
		VK_LoadRegisteredModel( registered.name.c_str(), registered.handle );
		++loadedModels;
	}

	uint32_t loadedSkins = 0;
	for ( size_t i = 1; i < vk.skins.size(); ++i )
	{
		if ( vk.skins[i].name.empty() || !vk.skins[i].surfaces.empty() )
		{
			continue;
		}
		VK_LoadRegisteredSkin( &vk.skins[i] );
		++loadedSkins;
	}

	if ( loadedModels > 0 || loadedSkins > 0 )
	{
		ri.Printf( PRINT_ALL,
			"rd-vulkan-scene: finalized pending registrations: models=%u skins=%u\n",
			loadedModels,
			loadedSkins );
	}
}

void VK_Backend_ModelBounds( qhandle_t, vec3_t mins, vec3_t maxs )
{
	if ( mins != nullptr )
	{
		VectorClear( mins );
	}
	if ( maxs != nullptr )
	{
		VectorClear( maxs );
	}
}

void VK_Backend_GetModelBounds( refEntity_t *, vec3_t mins, vec3_t maxs )
{
	if ( mins != nullptr )
	{
		VectorClear( mins );
	}
	if ( maxs != nullptr )
	{
		VectorClear( maxs );
	}
}

struct vk_bsp_lump_view_t
{
	const void *data;
	size_t count;
};

static bool VK_BspGetLump(
	const dheader_t *header,
	const byte *fileBase,
	size_t fileSize,
	int lumpIndex,
	size_t elementSize,
	const char *lumpName,
	vk_bsp_lump_view_t *view )
{
	const int offset = LittleLong( header->lumps[lumpIndex].fileofs );
	const int length = LittleLong( header->lumps[lumpIndex].filelen );
	if ( offset < 0 || length < 0 )
	{
		ri.Printf( PRINT_WARNING, "rd-vulkan-world: %s lump has negative range\n", lumpName );
		return false;
	}

	const size_t offsetBytes = static_cast<size_t>( offset );
	const size_t lengthBytes = static_cast<size_t>( length );
	if ( offsetBytes > fileSize || lengthBytes > fileSize - offsetBytes )
	{
		ri.Printf( PRINT_WARNING, "rd-vulkan-world: %s lump is outside the BSP file\n", lumpName );
		return false;
	}
	if ( elementSize == 0 || ( lengthBytes % elementSize ) != 0 )
	{
		ri.Printf( PRINT_WARNING, "rd-vulkan-world: %s lump has an unexpected size\n", lumpName );
		return false;
	}

	view->data = fileBase + offsetBytes;
	view->count = lengthBytes / elementSize;
	return true;
}

static bool VK_BspRangeValid( int first, int count, size_t limit )
{
	if ( first < 0 || count < 0 )
	{
		return false;
	}
	const size_t begin = static_cast<size_t>( first );
	const size_t length = static_cast<size_t>( count );
	return begin <= limit && length <= limit - begin;
}

static bool VK_WorldLoadLightmaps(
	const dheader_t *header,
	const byte *fileBase,
	size_t fileSize,
	std::vector<qhandle_t> *lightmaps )
{
	constexpr size_t lightmapPixelCount = LIGHTMAP_WIDTH * LIGHTMAP_HEIGHT;
	constexpr size_t lightmapBytes = lightmapPixelCount * 3;
	vk_bsp_lump_view_t lump = {};
	if ( !VK_BspGetLump(
			header, fileBase, fileSize, LUMP_LIGHTMAPS, lightmapBytes, "lightmap", &lump ) )
	{
		return false;
	}

	lightmaps->clear();
	lightmaps->reserve( lump.count );
	const byte *source = static_cast<const byte *>( lump.data );
	std::vector<byte> rgba( lightmapPixelCount * 4 );
	for ( size_t i = 0; i < lump.count; ++i )
	{
		if ( vk.textures.size() >= 4096 )
		{
			ri.Printf( PRINT_WARNING, "rd-vulkan-world: texture capacity reached while loading lightmaps\n" );
			return false;
		}
		const byte *input = source + i * lightmapBytes;
		for ( size_t pixel = 0; pixel < lightmapPixelCount; ++pixel )
		{
			rgba[pixel * 4 + 0] = input[pixel * 3 + 0];
			rgba[pixel * 4 + 1] = input[pixel * 3 + 1];
			rgba[pixel * 4 + 2] = input[pixel * 3 + 2];
			rgba[pixel * 4 + 3] = 255;
		}

		vk_texture_t texture = {};
		if ( !VK_CreateTextureFromPixels(
				rgba.data(),
				LIGHTMAP_WIDTH,
				LIGHTMAP_HEIGHT,
				&texture,
				VK_FORMAT_R8G8B8A8_UNORM ) )
		{
			ri.Printf( PRINT_WARNING, "rd-vulkan-world: failed to upload lightmap %zu\n", i );
			return false;
		}
		const qhandle_t handle = static_cast<qhandle_t>( vk.textures.size() );
		vk.textures.push_back( texture );
		vk.materials.emplace_back();
		lightmaps->push_back( handle );
	}

	ri.Printf( PRINT_ALL, "rd-vulkan-world: loaded %zu BSP lightmaps as linear UNORM\n", lightmaps->size() );
	return true;
}

static bool VK_BspSurfaceIsSkipped( const dsurface_t &surface, const dshader_t *shaders, size_t shaderCount )
{
	const int shaderNum = LittleLong( surface.shaderNum );
	if ( shaderNum < 0 || static_cast<size_t>( shaderNum ) >= shaderCount )
	{
		return false;
	}

	const uint32_t surfaceFlags = static_cast<uint32_t>( LittleLong( shaders[shaderNum].surfaceFlags ) );
	return ( surfaceFlags & ( SURF_NODRAW | SURF_SKY ) ) != 0;
}

static void VK_WorldConfigureSky(
	const dsurface_t *surfaces,
	size_t surfaceCount,
	const dshader_t *shaders,
	size_t shaderCount,
	vk_world_geometry_t *world )
{
	static const char *suffixes[6] = { "rt", "lf", "bk", "ft", "up", "dn" };
	for ( size_t surfaceIndex = 0; surfaceIndex < surfaceCount; ++surfaceIndex )
	{
		const int shaderIndex = LittleLong( surfaces[surfaceIndex].shaderNum );
		if ( shaderIndex < 0 || static_cast<size_t>( shaderIndex ) >= shaderCount )
		{
			continue;
		}
		const dshader_t &shader = shaders[shaderIndex];
		if ( ( LittleLong( shader.surfaceFlags ) & SURF_SKY ) == 0 )
		{
			continue;
		}

		char shaderName[MAX_QPATH];
		Q_strncpyz( shaderName, shader.shader, sizeof( shaderName ) );
		const vk_shader_definition_t *definition = VK_FindShaderDefinition( shaderName );
		if ( definition == nullptr || definition->skyOuterbox.empty() ||
			 definition->skyOuterbox == "-" )
		{
			continue;
		}

		qhandle_t loadedFaces[6] = {};
		bool loaded = true;
		for ( size_t face = 0; face < ARRAY_LEN( loadedFaces ); ++face )
		{
			char imageName[MAX_QPATH];
			Com_sprintf( imageName, sizeof( imageName ), "%s_%s",
				definition->skyOuterbox.c_str(), suffixes[face] );
			loadedFaces[face] = VK_FindOrLoadImage( imageName );
			if ( !VK_WorldTextureUsable( loadedFaces[face] ) )
			{
				loadedFaces[face] = face > 0 ? loadedFaces[face - 1] : 0;
			}
			loaded = loaded && VK_WorldTextureUsable( loadedFaces[face] );
		}
		if ( !loaded )
		{
			ri.Printf( PRINT_WARNING,
				"rd-vulkan-sky: failed to load outerbox %s for shader %s\n",
				definition->skyOuterbox.c_str(), shaderName );
			continue;
		}

		std::memcpy( world->skyTextures, loadedFaces, sizeof( loadedFaces ) );
		world->skyName = definition->skyOuterbox;
		world->hasSky = true;
		ri.Printf( PRINT_ALL,
			"rd-vulkan-sky: shader=%s outerbox=%s cloudHeight=%.1f faces=%d,%d,%d,%d,%d,%d\n",
			shaderName, world->skyName.c_str(), definition->skyCloudHeight,
			world->skyTextures[0], world->skyTextures[1], world->skyTextures[2],
			world->skyTextures[3], world->skyTextures[4], world->skyTextures[5] );
		return;
	}

	ri.Printf( PRINT_ALL, "rd-vulkan-sky: BSP contains no usable outer skybox\n" );
}

static void VK_WorldConfigureGlobalFog(
	const dheader_t *header,
	const byte *fileBase,
	size_t fileSize,
	vk_world_geometry_t *world )
{
	vk_bsp_lump_view_t fogLump = {};
	if ( !VK_BspGetLump( header, fileBase, fileSize, LUMP_FOGS, sizeof( dfog_t ), "fog", &fogLump ) )
	{
		return;
	}

	const dfog_t *fogs = static_cast<const dfog_t *>( fogLump.data );
	for ( size_t i = 0; i < fogLump.count; ++i )
	{
		if ( LittleLong( fogs[i].brushNum ) != -1 )
		{
			continue;
		}
		char shaderName[MAX_QPATH];
		Q_strncpyz( shaderName, fogs[i].shader, sizeof( shaderName ) );
		const vk_shader_definition_t *definition = VK_FindShaderDefinition( shaderName );
		if ( definition == nullptr || !definition->hasFog )
		{
			ri.Printf( PRINT_WARNING,
				"rd-vulkan-fog: global fog shader %s has no parsed fogParms\n", shaderName );
			return;
		}
		std::memcpy( world->globalFogColor, definition->fogColor, sizeof( world->globalFogColor ) );
		world->globalFogDepth = definition->fogDepth;
		world->hasGlobalFog = true;
		ri.Printf( PRINT_ALL,
			"rd-vulkan-fog: global shader=%s color=(%.3f %.3f %.3f) depth=%.1f\n",
			shaderName,
			world->globalFogColor[0], world->globalFogColor[1], world->globalFogColor[2],
			world->globalFogDepth );
		return;
	}

	ri.Printf( PRINT_ALL, "rd-vulkan-fog: BSP contains no global fog\n" );
}

static float VK_ClampWorldColor( float value )
{
	return std::max( 0.0f, std::min( 1.0f, value ) );
}

static bool VK_WorldCanAppend(
	const std::vector<vk_world_vertex_t> &vertices,
	const std::vector<uint32_t> &indices,
	size_t vertexCount,
	size_t indexCount )
{
	const size_t maxDrawCount = static_cast<size_t>( std::numeric_limits<uint32_t>::max() );
	if ( vertices.size() > maxDrawCount || indices.size() > maxDrawCount )
	{
		return false;
	}
	return vertexCount <= maxDrawCount - vertices.size() &&
		indexCount <= maxDrawCount - indices.size();
}

static vk_world_vertex_t VK_WorldConvertVertex( const mapVert_t &source )
{
	vk_world_vertex_t vertex = {};
	for ( int i = 0; i < 3; ++i )
	{
		vertex.position[i] = LittleFloat( source.xyz[i] );
		vertex.normal[i] = LittleFloat( source.normal[i] );
	}
	vertex.uv[0] = LittleFloat( source.st[0] );
	vertex.uv[1] = LittleFloat( source.st[1] );
	vertex.lightmapUv[0] = LittleFloat( source.lightmap[0][0] );
	vertex.lightmapUv[1] = LittleFloat( source.lightmap[0][1] );

	const float lightColor[3] = {
		source.color[0][0] / 255.0f,
		source.color[0][1] / 255.0f,
		source.color[0][2] / 255.0f,
	};
	const float maxLight = std::max( lightColor[0], std::max( lightColor[1], lightColor[2] ) );
	if ( maxLight > 0.04f )
	{
		vertex.color[0] = VK_ClampWorldColor( 0.16f + lightColor[0] * 1.05f );
		vertex.color[1] = VK_ClampWorldColor( 0.16f + lightColor[1] * 1.05f );
		vertex.color[2] = VK_ClampWorldColor( 0.16f + lightColor[2] * 1.05f );
	}
	else
	{
		const float nx = std::fabs( LittleFloat( source.normal[0] ) );
		const float ny = std::fabs( LittleFloat( source.normal[1] ) );
		const float nz = std::fabs( LittleFloat( source.normal[2] ) );
		vertex.color[0] = VK_ClampWorldColor( 0.36f + nx * 0.20f );
		vertex.color[1] = VK_ClampWorldColor( 0.36f + ny * 0.20f );
		vertex.color[2] = VK_ClampWorldColor( 0.40f + nz * 0.24f );
	}
	vertex.color[3] = 1.0f;
	return vertex;
}

static void VK_WorldAppendVertex( const mapVert_t &source, std::vector<vk_world_vertex_t> &vertices )
{
	vertices.push_back( VK_WorldConvertVertex( source ) );
}

static bool VK_WorldTextureUsable( qhandle_t texture )
{
	return texture > 0 &&
		static_cast<size_t>( texture ) < vk.textures.size() &&
		vk.textures[texture].repeatDescriptorSet != VK_NULL_HANDLE;
}

static qhandle_t VK_WorldResolveTexture( qhandle_t shader )
{
	if ( shader > 0 && static_cast<size_t>( shader ) < vk.materials.size() )
	{
		const bool hasMaterial = !vk.materials[shader].stages.empty();
		for ( const vk_material_stage_t &stage : vk.materials[shader].stages )
		{
			if ( stage.surfaceSprite.type != VK_SURFACE_SPRITE_NONE )
			{
				continue;
			}
			if ( stage.texture != 2 && VK_WorldTextureUsable( stage.texture ) )
			{
				return stage.texture;
			}
		}
		if ( hasMaterial )
		{
			return 2;
		}
	}

	if ( shader != 2 && VK_WorldTextureUsable( shader ) )
	{
		return shader;
	}

	return 2;
}

static qhandle_t VK_WorldRegisterSurfaceShader(
	const dsurface_t &surface,
	const dshader_t *shaders,
	size_t shaderCount )
{
	const int shaderNum = LittleLong( surface.shaderNum );
	if ( shaderNum < 0 || static_cast<size_t>( shaderNum ) >= shaderCount )
	{
		return 1;
	}

	char shaderName[MAX_QPATH];
	Q_strncpyz( shaderName, shaders[shaderNum].shader, sizeof( shaderName ) );
	if ( shaderName[0] == '\0' )
	{
		return 1;
	}

	const qhandle_t shader = VK_Backend_RegisterTexture( shaderName );
	if ( shader > 0 && static_cast<size_t>( shader ) < vk.materials.size() )
	{
		for ( const vk_material_stage_t &stage : vk.materials[shader].stages )
		{
			if ( stage.lightmap )
			{
				return shader;
			}
		}
	}
	return VK_WorldResolveTexture( shader ) == 2 ? 2 : shader;
}

static void VK_WorldAppendBatch(
	std::vector<vk_world_batch_t> &batches,
	uint32_t firstIndex,
	uint32_t indexCount,
	qhandle_t shader,
	qhandle_t lightmap,
	bool vertexLit,
	uint32_t surfaceIndex )
{
	if ( indexCount == 0 )
	{
		return;
	}
	if ( shader == 2 )
	{
		return;
	}
	if ( shader <= 0 )
	{
		shader = 1;
	}

	vk_world_batch_t batch = {};
	batch.firstIndex = firstIndex;
	batch.indexCount = indexCount;
	batch.shader = shader;
	batch.lightmap = lightmap;
	batch.vertexLit = vertexLit;
	batch.surfaceIndex = surfaceIndex;
	batches.push_back( batch );
}

static void VK_LogWaterfallGeometry(
	const std::vector<vk_world_vertex_t> &vertices,
	const std::vector<uint32_t> &indices,
	const std::vector<vk_world_batch_t> &batches )
{
	struct normal_cluster_t
	{
		float normal[3];
		uint32_t triangles;
	};

	for ( const vk_world_batch_t &batch : batches )
	{
		if ( Q_stricmp( VK_TextureNameForHandle( batch.shader ), "textures/h_evil/wfall" ) != 0 )
		{
			continue;
		}

		float mins[3] = {
			std::numeric_limits<float>::max(),
			std::numeric_limits<float>::max(),
			std::numeric_limits<float>::max() };
		float maxs[3] = {
			-std::numeric_limits<float>::max(),
			-std::numeric_limits<float>::max(),
			-std::numeric_limits<float>::max() };
		std::vector<normal_cluster_t> normalClusters;
		uint32_t validTriangles = 0;
		const uint32_t endIndex = std::min<uint32_t>(
			batch.firstIndex + batch.indexCount, static_cast<uint32_t>( indices.size() ) );
		for ( uint32_t index = batch.firstIndex; index + 2 < endIndex; index += 3 )
		{
			const uint32_t vertexIndices[3] = {
				indices[index], indices[index + 1], indices[index + 2] };
			if ( vertexIndices[0] >= vertices.size() || vertexIndices[1] >= vertices.size() ||
				 vertexIndices[2] >= vertices.size() )
			{
				continue;
			}

			for ( uint32_t vertexIndex : vertexIndices )
			{
				for ( int axis = 0; axis < 3; ++axis )
				{
					mins[axis] = std::min( mins[axis], vertices[vertexIndex].position[axis] );
					maxs[axis] = std::max( maxs[axis], vertices[vertexIndex].position[axis] );
				}
			}

			vec3_t edge0;
			vec3_t edge1;
			vec3_t normal;
			VectorSubtract( vertices[vertexIndices[1]].position, vertices[vertexIndices[0]].position, edge0 );
			VectorSubtract( vertices[vertexIndices[2]].position, vertices[vertexIndices[0]].position, edge1 );
			CrossProduct( edge0, edge1, normal );
			if ( VectorNormalize( normal ) == 0.0f )
			{
				continue;
			}
			++validTriangles;

			auto cluster = std::find_if(
				normalClusters.begin(), normalClusters.end(),
				[&normal]( const normal_cluster_t &entry )
				{
					return std::fabs( DotProduct( normal, entry.normal ) ) > 0.995f;
				} );
			if ( cluster == normalClusters.end() )
			{
				normal_cluster_t entry = {};
				VectorCopy( normal, entry.normal );
				entry.triangles = 1;
				normalClusters.push_back( entry );
			}
			else
			{
				++cluster->triangles;
			}
		}

		ri.Printf( PRINT_ALL,
			"rd-vulkan-waterfall-geometry: surface=%u triangles=%u bounds=(%.1f %.1f %.1f)-(%.1f %.1f %.1f) planes=%zu\n",
			batch.surfaceIndex, validTriangles,
			mins[0], mins[1], mins[2], maxs[0], maxs[1], maxs[2], normalClusters.size() );
		for ( const normal_cluster_t &cluster : normalClusters )
		{
			ri.Printf( PRINT_ALL,
				"rd-vulkan-waterfall-geometry: normal=(%.3f %.3f %.3f) triangles=%u\n",
				cluster.normal[0], cluster.normal[1], cluster.normal[2], cluster.triangles );
		}
	}
}

static float VK_SurfaceSpriteRandom( uint32_t *state )
{
	*state ^= *state << 13;
	*state ^= *state >> 17;
	*state ^= *state << 5;
	return static_cast<float>( *state & 0x00ffffffu ) / 16777216.0f;
}

static void VK_WorldAppendSurfaceSpriteBatches(
	std::vector<vk_surface_sprite_batch_t> *spriteBatches,
	vk_surface_sprite_build_stats_t *stats,
	const std::vector<vk_world_vertex_t> &vertices,
	const std::vector<uint32_t> &indices,
	uint32_t firstIndex,
	uint32_t indexCount,
	qhandle_t shader,
	uint32_t surfaceIndex )
{
	if ( shader <= 0 || static_cast<size_t>( shader ) >= vk.materials.size() )
	{
		return;
	}

	for ( const vk_material_stage_t &stage : vk.materials[shader].stages )
	{
		const vk_surface_sprite_config_t &config = stage.surfaceSprite;
		if ( config.type == VK_SURFACE_SPRITE_NONE )
		{
			continue;
		}
		++stats->candidateStages;
		if ( config.type != VK_SURFACE_SPRITE_VERTICAL &&
			 config.type != VK_SURFACE_SPRITE_FLATTENED &&
			 config.type != VK_SURFACE_SPRITE_ORIENTED &&
			 config.type != VK_SURFACE_SPRITE_EFFECT )
		{
			++stats->unsupportedStages;
			continue;
		}
		++stats->supportedStages;
		const size_t typeIndex = static_cast<size_t>( config.type );
		if ( typeIndex < ARRAY_LEN( stats->stagesByType ) )
		{
			++stats->stagesByType[typeIndex];
		}
		if ( config.width <= 0.0f || config.height <= 0.0f || config.density <= 0.0f )
		{
			++stats->invalidStages;
			continue;
		}
		if ( stage.texture == 2 || !VK_WorldTextureUsable( stage.texture ) )
		{
			++stats->unavailableTextures;
			continue;
		}

		vk_surface_sprite_batch_t batch = {};
		batch.stage = stage;
		batch.stage.vertexColor = true;
		batch.surfaceIndex = surfaceIndex;
		constexpr size_t maxInstancesPerSurface = 100000;
		const size_t endIndex = std::min<size_t>(
			indices.size(), static_cast<size_t>( firstIndex ) + indexCount );
		for ( size_t index = firstIndex;
			  index + 2 < endIndex && batch.instances.size() < maxInstancesPerSurface;
			  index += 3 )
		{
			++stats->triangles;
			const uint32_t i0 = indices[index + 0];
			const uint32_t i1 = indices[index + 1];
			const uint32_t i2 = indices[index + 2];
			if ( i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size() )
			{
				++stats->invalidTriangles;
				continue;
			}

			const vk_world_vertex_t &v0 = vertices[i0];
			const vk_world_vertex_t &v1 = vertices[i1];
			const vk_world_vertex_t &v2 = vertices[i2];
			vec3_t edge0;
			vec3_t edge1;
			vec3_t faceNormal;
			VectorSubtract( v1.position, v0.position, edge0 );
			VectorSubtract( v2.position, v0.position, edge1 );
			CrossProduct( edge0, edge1, faceNormal );
			if ( VectorNormalize( faceNormal ) <= 0.0f )
			{
				++stats->invalidTriangles;
				continue;
			}

			bool facingAccepted = true;
			if ( ( config.type == VK_SURFACE_SPRITE_ORIENTED ||
				   config.type == VK_SURFACE_SPRITE_EFFECT ) &&
				 config.facing != VK_SURFACE_SPRITE_FACING_NORMAL )
			{
				facingAccepted = v0.normal[2] >= 0.99f &&
					v1.normal[2] >= 0.99f && v2.normal[2] >= 0.99f;
			}
			else if ( config.facing == VK_SURFACE_SPRITE_FACING_DOWN )
			{
				facingAccepted = v0.normal[2] <= -0.5f &&
					v1.normal[2] <= -0.5f && v2.normal[2] <= -0.5f;
			}
			else if ( config.facing != VK_SURFACE_SPRITE_FACING_ANY )
			{
				facingAccepted = v0.normal[2] >= 0.5f &&
					v1.normal[2] >= 0.5f && v2.normal[2] >= 0.5f;
			}
			if ( !facingAccepted )
			{
				++stats->facingRejectedTriangles;
				continue;
			}

			const float projectedArea = std::fabs(
				( v2.position[0] - v0.position[0] ) * ( v1.position[1] - v0.position[1] ) -
				( v2.position[1] - v0.position[1] ) * ( v1.position[0] - v0.position[0] ) );
			if ( projectedArea <= 1.0f )
			{
				++stats->areaRejectedTriangles;
				continue;
			}
			const float step = config.density / std::sqrt( projectedArea );
			if ( !std::isfinite( step ) || step <= 0.0f )
			{
				continue;
			}

			uint32_t randomState =
				0x9e3779b9u ^ ( surfaceIndex * 0x85ebca6bu ) ^ static_cast<uint32_t>( index );
			for ( float positionI = 0.0f;
				  positionI < 1.0f && batch.instances.size() < maxInstancesPerSurface;
				  positionI += step )
			{
				for ( float positionJ = 0.0f;
					  positionJ < 1.0f - positionI && batch.instances.size() < maxInstancesPerSurface;
					  positionJ += step )
				{
					const float weight0 = positionI + VK_SurfaceSpriteRandom( &randomState ) * step;
					const float weight1 = positionJ + VK_SurfaceSpriteRandom( &randomState ) * step;
					if ( weight0 > 1.0f || weight1 > 1.0f - weight0 )
					{
						continue;
					}
					const float weight2 = 1.0f - weight0 - weight1;
					vk_surface_sprite_instance_t instance = {};
					for ( int component = 0; component < 3; ++component )
					{
						instance.position[component] =
							v0.position[component] * weight0 +
							v1.position[component] * weight1 +
							v2.position[component] * weight2;
						instance.normal[component] =
							v0.normal[component] * weight0 +
							v1.normal[component] * weight1 +
							v2.normal[component] * weight2;
					}
					if ( VectorNormalize( instance.normal ) <= 0.0f )
					{
						VectorCopy( faceNormal, instance.normal );
					}
					for ( int component = 0; component < 4; ++component )
					{
						instance.color[component] =
							v0.color[component] * weight0 +
							v1.color[component] * weight1 +
							v2.color[component] * weight2;
					}
					instance.width = config.width *
						( 1.0f + config.variance[0] * VK_SurfaceSpriteRandom( &randomState ) );
					instance.height = config.height *
						( 1.0f + config.variance[1] * VK_SurfaceSpriteRandom( &randomState ) );
					if ( VK_SurfaceSpriteRandom( &randomState ) > 0.5f )
					{
						instance.width = -instance.width;
					}
					instance.phase = VK_SurfaceSpriteRandom( &randomState ) * 6.28318530718f;
					instance.orientation = randomState & 3u;
					batch.instances.push_back( instance );
					++stats->anchors;
					if ( typeIndex < ARRAY_LEN( stats->anchorsByType ) )
					{
						++stats->anchorsByType[typeIndex];
					}
				}
			}
		}

		if ( !batch.instances.empty() )
		{
			spriteBatches->push_back( std::move( batch ) );
		}
	}
}

static bool VK_WorldAppendIndexedSurface(
	const dsurface_t &surface,
	const mapVert_t *drawVerts,
	size_t drawVertCount,
	const int *drawIndexes,
	size_t drawIndexCount,
	std::vector<vk_world_vertex_t> &vertices,
	std::vector<uint32_t> &indices )
{
	const int firstVert = LittleLong( surface.firstVert );
	const int numVerts = LittleLong( surface.numVerts );
	const int firstIndex = LittleLong( surface.firstIndex );
	const int numIndexes = LittleLong( surface.numIndexes );
	if ( numVerts <= 0 || numIndexes <= 0 ||
		 !VK_BspRangeValid( firstVert, numVerts, drawVertCount ) ||
		 !VK_BspRangeValid( firstIndex, numIndexes, drawIndexCount ) ||
		 !VK_WorldCanAppend( vertices, indices, static_cast<size_t>( numVerts ), static_cast<size_t>( numIndexes ) ) )
	{
		return false;
	}

	for ( int i = 0; i < numIndexes; ++i )
	{
		const int index = LittleLong( drawIndexes[firstIndex + i] );
		if ( index < 0 || index >= numVerts )
		{
			return false;
		}
	}

	const uint32_t baseVertex = static_cast<uint32_t>( vertices.size() );
	for ( int i = 0; i < numVerts; ++i )
	{
		VK_WorldAppendVertex( drawVerts[firstVert + i], vertices );
	}
	for ( int i = 0; i < numIndexes; ++i )
	{
		indices.push_back( baseVertex + static_cast<uint32_t>( LittleLong( drawIndexes[firstIndex + i] ) ) );
	}
	return true;
}

static bool VK_WorldAppendPatchSurface(
	const dsurface_t &surface,
	const mapVert_t *drawVerts,
	size_t drawVertCount,
	std::vector<vk_world_vertex_t> &vertices,
	std::vector<uint32_t> &indices )
{
	const int firstVert = LittleLong( surface.firstVert );
	const int numVerts = LittleLong( surface.numVerts );
	const int width = LittleLong( surface.patchWidth );
	const int height = LittleLong( surface.patchHeight );
	if ( firstVert < 0 || numVerts <= 0 || width < 3 || height < 3 ||
		 ( width & 1 ) == 0 || ( height & 1 ) == 0 )
	{
		return false;
	}

	const size_t pointCount = static_cast<size_t>( width ) * static_cast<size_t>( height );
	if ( pointCount > static_cast<size_t>( numVerts ) ||
		 static_cast<size_t>( firstVert ) > drawVertCount ||
		 pointCount > drawVertCount - static_cast<size_t>( firstVert ) )
	{
		return false;
	}

	// Quake 3 patches are adjoining quadratic Bezier spans. The GLES renderer's
	// default r_subdivisions value is four, which is a good fixed CPU baseline
	// here and avoids exposing the raw control cage as faceted geometry.
	constexpr int subdivisions = 4;
	const int segmentWidth = ( width - 1 ) / 2;
	const int segmentHeight = ( height - 1 ) / 2;
	const int outputWidth = segmentWidth * subdivisions + 1;
	const int outputHeight = segmentHeight * subdivisions + 1;
	const size_t generatedVertices =
		static_cast<size_t>( outputWidth ) * static_cast<size_t>( outputHeight );
	const size_t generatedIndexes = static_cast<size_t>( outputWidth - 1 ) *
		static_cast<size_t>( outputHeight - 1 ) * 6;
	if ( !VK_WorldCanAppend( vertices, indices, generatedVertices, generatedIndexes ) )
	{
		return false;
	}

	std::vector<vk_world_vertex_t> controlPoints;
	controlPoints.reserve( pointCount );
	for ( size_t i = 0; i < pointCount; ++i )
	{
		controlPoints.push_back( VK_WorldConvertVertex( drawVerts[firstVert + i] ) );
	}

	const uint32_t baseVertex = static_cast<uint32_t>( vertices.size() );
	for ( int outputY = 0; outputY < outputHeight; ++outputY )
	{
		const int segmentY = std::min( outputY / subdivisions, segmentHeight - 1 );
		const float v = static_cast<float>( outputY - segmentY * subdivisions ) /
			static_cast<float>( subdivisions );
		const float oneMinusV = 1.0f - v;
		const float basisV[3] = { oneMinusV * oneMinusV, 2.0f * v * oneMinusV, v * v };
		for ( int outputX = 0; outputX < outputWidth; ++outputX )
		{
			const int segmentX = std::min( outputX / subdivisions, segmentWidth - 1 );
			const float u = static_cast<float>( outputX - segmentX * subdivisions ) /
				static_cast<float>( subdivisions );
			const float oneMinusU = 1.0f - u;
			const float basisU[3] = { oneMinusU * oneMinusU, 2.0f * u * oneMinusU, u * u };

			vk_world_vertex_t vertex = {};
			for ( int controlY = 0; controlY < 3; ++controlY )
			{
				for ( int controlX = 0; controlX < 3; ++controlX )
				{
					const float weight = basisU[controlX] * basisV[controlY];
					const vk_world_vertex_t &control = controlPoints[
						static_cast<size_t>( segmentY * 2 + controlY ) * width +
						static_cast<size_t>( segmentX * 2 + controlX )];
					for ( int axis = 0; axis < 3; ++axis )
					{
						vertex.position[axis] += control.position[axis] * weight;
						vertex.normal[axis] += control.normal[axis] * weight;
					}
					for ( int channel = 0; channel < 4; ++channel )
					{
						vertex.color[channel] += control.color[channel] * weight;
					}
					for ( int coordinate = 0; coordinate < 2; ++coordinate )
					{
						vertex.uv[coordinate] += control.uv[coordinate] * weight;
						vertex.lightmapUv[coordinate] += control.lightmapUv[coordinate] * weight;
					}
				}
			}
			VectorNormalize( vertex.normal );
			vertices.push_back( vertex );
		}
	}
	for ( int y = 0; y < outputHeight - 1; ++y )
	{
		for ( int x = 0; x < outputWidth - 1; ++x )
		{
			const uint32_t a = baseVertex + static_cast<uint32_t>( y * outputWidth + x );
			const uint32_t b = a + 1;
			const uint32_t c = a + static_cast<uint32_t>( outputWidth );
			const uint32_t d = c + 1;
			indices.push_back( a );
			indices.push_back( b );
			indices.push_back( c );
			indices.push_back( b );
			indices.push_back( d );
			indices.push_back( c );
		}
	}
	return true;
}

static void VK_WorldLoadVisibilityData(
	const dheader_t *header,
	const byte *fileBase,
	size_t fileSize,
	size_t surfaceCount,
	vk_world_geometry_t *world )
{
	world->bspSurfaceCount = static_cast<uint32_t>( surfaceCount );
	world->visibleSurfaces.assign( surfaceCount, 1 );

	vk_bsp_lump_view_t planeLump = {};
	vk_bsp_lump_view_t nodeLump = {};
	vk_bsp_lump_view_t leafLump = {};
	vk_bsp_lump_view_t leafSurfaceLump = {};
	vk_bsp_lump_view_t visibilityLump = {};
	if ( !VK_BspGetLump( header, fileBase, fileSize, LUMP_PLANES, sizeof( dplane_t ), "plane", &planeLump ) ||
		 !VK_BspGetLump( header, fileBase, fileSize, LUMP_NODES, sizeof( dnode_t ), "node", &nodeLump ) ||
		 !VK_BspGetLump( header, fileBase, fileSize, LUMP_LEAFS, sizeof( dleaf_t ), "leaf", &leafLump ) ||
		 !VK_BspGetLump( header, fileBase, fileSize, LUMP_LEAFSURFACES, sizeof( int ), "leafsurface", &leafSurfaceLump ) ||
		 !VK_BspGetLump( header, fileBase, fileSize, LUMP_VISIBILITY, sizeof( byte ), "visibility", &visibilityLump ) )
	{
		ri.Printf( PRINT_WARNING, "rd-vulkan-world: BSP visibility lumps are incomplete; drawing all surfaces\n" );
		return;
	}

	const dplane_t *bspPlanes = static_cast<const dplane_t *>( planeLump.data );
	world->planes.reserve( planeLump.count );
	for ( size_t i = 0; i < planeLump.count; ++i )
	{
		vk_world_plane_t plane = {};
		plane.normal[0] = LittleFloat( bspPlanes[i].normal[0] );
		plane.normal[1] = LittleFloat( bspPlanes[i].normal[1] );
		plane.normal[2] = LittleFloat( bspPlanes[i].normal[2] );
		plane.dist = LittleFloat( bspPlanes[i].dist );
		world->planes.push_back( plane );
	}

	const dnode_t *bspNodes = static_cast<const dnode_t *>( nodeLump.data );
	world->nodes.reserve( nodeLump.count );
	for ( size_t i = 0; i < nodeLump.count; ++i )
	{
		vk_world_node_t node = {};
		node.plane = LittleLong( bspNodes[i].planeNum );
		node.children[0] = LittleLong( bspNodes[i].children[0] );
		node.children[1] = LittleLong( bspNodes[i].children[1] );
		world->nodes.push_back( node );
	}

	const int *bspLeafSurfaces = static_cast<const int *>( leafSurfaceLump.data );
	world->leafSurfaces.reserve( leafSurfaceLump.count );
	uint32_t badLeafSurfaceRefs = 0;
	for ( size_t i = 0; i < leafSurfaceLump.count; ++i )
	{
		const int surfaceIndex = LittleLong( bspLeafSurfaces[i] );
		if ( surfaceIndex < 0 || static_cast<size_t>( surfaceIndex ) >= surfaceCount )
		{
			world->leafSurfaces.push_back( std::numeric_limits<uint32_t>::max() );
			++badLeafSurfaceRefs;
		}
		else
		{
			world->leafSurfaces.push_back( static_cast<uint32_t>( surfaceIndex ) );
		}
	}

	const dleaf_t *bspLeafs = static_cast<const dleaf_t *>( leafLump.data );
	world->leafs.reserve( leafLump.count );
	uint32_t badLeafRanges = 0;
	for ( size_t i = 0; i < leafLump.count; ++i )
	{
		vk_world_leaf_t leaf = {};
		leaf.cluster = LittleLong( bspLeafs[i].cluster );
		leaf.area = LittleLong( bspLeafs[i].area );
		leaf.firstLeafSurface = LittleLong( bspLeafs[i].firstLeafSurface );
		leaf.numLeafSurfaces = LittleLong( bspLeafs[i].numLeafSurfaces );
		if ( !VK_BspRangeValid( leaf.firstLeafSurface, leaf.numLeafSurfaces, leafSurfaceLump.count ) )
		{
			leaf.firstLeafSurface = 0;
			leaf.numLeafSurfaces = 0;
			++badLeafRanges;
		}
		world->leafs.push_back( leaf );
	}

	if ( visibilityLump.count >= 8 )
	{
		const byte *visibilityBytes = static_cast<const byte *>( visibilityLump.data );
		int rawNumClusters = 0;
		int rawClusterBytes = 0;
		std::memcpy( &rawNumClusters, visibilityBytes, sizeof( rawNumClusters ) );
		std::memcpy( &rawClusterBytes, visibilityBytes + sizeof( rawNumClusters ), sizeof( rawClusterBytes ) );
		const int numClusters = LittleLong( rawNumClusters );
		const int clusterBytes = LittleLong( rawClusterBytes );
		if ( numClusters > 0 && clusterBytes > 0 )
		{
			const size_t requiredBytes = static_cast<size_t>( numClusters ) * static_cast<size_t>( clusterBytes );
			if ( requiredBytes <= visibilityLump.count - 8 )
			{
				world->numClusters = static_cast<uint32_t>( numClusters );
				world->clusterBytes = static_cast<uint32_t>( clusterBytes );
				world->visibility.assign( visibilityBytes + 8, visibilityBytes + 8 + requiredBytes );
			}
		}
	}

	if ( !world->visibility.empty() )
	{
		ri.Printf( PRINT_ALL,
			"rd-vulkan-world: loaded BSP visibility: planes=%zu nodes=%zu leafs=%zu leafsurfaces=%zu clusters=%u clusterBytes=%u badRefs=%u badLeafRanges=%u\n",
			world->planes.size(), world->nodes.size(), world->leafs.size(), world->leafSurfaces.size(),
			world->numClusters, world->clusterBytes, badLeafSurfaceRefs, badLeafRanges );
	}
	else
	{
		ri.Printf( PRINT_WARNING, "rd-vulkan-world: BSP has no usable PVS data; drawing all surfaces\n" );
	}
}

static void VK_WorldLoadInlineModels(
	const dheader_t *header,
	const byte *fileBase,
	size_t fileSize,
	size_t surfaceCount,
	vk_world_geometry_t *world )
{
	vk_bsp_lump_view_t modelLump = {};
	if ( !VK_BspGetLump( header, fileBase, fileSize, LUMP_MODELS, sizeof( dmodel_t ), "model", &modelLump ) )
	{
		return;
	}

	const dmodel_t *bspModels = static_cast<const dmodel_t *>( modelLump.data );
	world->inlineModels.reserve( modelLump.count );
	uint32_t badRanges = 0;
	for ( size_t i = 0; i < modelLump.count; ++i )
	{
		const int firstSurface = LittleLong( bspModels[i].firstSurface );
		const int numSurfaces = LittleLong( bspModels[i].numSurfaces );
		vk_world_inline_model_t model = {};
		if ( VK_BspRangeValid( firstSurface, numSurfaces, surfaceCount ) )
		{
			model.firstSurface = static_cast<uint32_t>( firstSurface );
			model.surfaceCount = static_cast<uint32_t>( numSurfaces );
		}
		else
		{
			++badRanges;
		}
		world->inlineModels.push_back( model );
	}

	ri.Printf( PRINT_ALL, "rd-vulkan-world: loaded %zu inline BSP models (badRanges=%u)\n",
		world->inlineModels.size(), badRanges );
}

void VK_Backend_LoadWorld( const char *name )
{
	++vk.worldLoadCount;
	vk.loggedDiagnosticDraw = false;
	vk.loggedWorldDraw = false;
	vk.loggedVisibleWorldMaterials = false;
	vk.loggedShipInteriorMaterials = false;
	vk.loggedShipInteriorModels = false;
	vk.loggedFirstModelDraw = false;
	VK_DestroyWorldGeometry();

	char *buffer = nullptr;
	const long fileSize = name != nullptr ? ri.FS_ReadFile( name, reinterpret_cast<void **>( &buffer ) ) : -1;
	ri.Printf( PRINT_ALL, "rd-vulkan-world: load %u: %s (%ld bytes visible through VFS)\n",
		vk.worldLoadCount, name != nullptr ? name : "<null>", fileSize );
	if ( fileSize <= 0 || buffer == nullptr )
	{
		if ( buffer != nullptr )
		{
			ri.FS_FreeFile( buffer );
		}
		return;
	}

	const size_t fileSizeBytes = static_cast<size_t>( fileSize );
	if ( fileSizeBytes < sizeof( dheader_t ) )
	{
		ri.Printf( PRINT_WARNING, "rd-vulkan-world: %s is too small to be a BSP\n", name );
		ri.FS_FreeFile( buffer );
		return;
	}

	const byte *fileBase = reinterpret_cast<const byte *>( buffer );
	const dheader_t *header = reinterpret_cast<const dheader_t *>( fileBase );
	const int ident = LittleLong( header->ident );
	const int version = LittleLong( header->version );
	if ( ident != BSP_IDENT || version != BSP_VERSION )
	{
		ri.Printf( PRINT_WARNING, "rd-vulkan-world: %s has unsupported BSP header ident=0x%x version=%d\n",
			name, ident, version );
		ri.FS_FreeFile( buffer );
		return;
	}

	vk_bsp_lump_view_t shaderLump = {};
	vk_bsp_lump_view_t surfaceLump = {};
	vk_bsp_lump_view_t vertexLump = {};
	vk_bsp_lump_view_t indexLump = {};
	if ( !VK_BspGetLump( header, fileBase, fileSizeBytes, LUMP_SHADERS, sizeof( dshader_t ), "shader", &shaderLump ) ||
		 !VK_BspGetLump( header, fileBase, fileSizeBytes, LUMP_SURFACES, sizeof( dsurface_t ), "surface", &surfaceLump ) ||
		 !VK_BspGetLump( header, fileBase, fileSizeBytes, LUMP_DRAWVERTS, sizeof( mapVert_t ), "drawvert", &vertexLump ) ||
		 !VK_BspGetLump( header, fileBase, fileSizeBytes, LUMP_DRAWINDEXES, sizeof( int ), "drawindex", &indexLump ) )
	{
		ri.FS_FreeFile( buffer );
		return;
	}

	const dshader_t *shaders = static_cast<const dshader_t *>( shaderLump.data );
	const dsurface_t *surfaces = static_cast<const dsurface_t *>( surfaceLump.data );
	const mapVert_t *drawVerts = static_cast<const mapVert_t *>( vertexLump.data );
	const int *drawIndexes = static_cast<const int *>( indexLump.data );

	std::vector<vk_world_vertex_t> vertices;
	std::vector<uint32_t> indices;
	std::vector<vk_world_batch_t> batches;
	std::vector<vk_surface_sprite_batch_t> surfaceSpriteBatches;
	vk_surface_sprite_build_stats_t surfaceSpriteBuildStats = {};
	std::vector<qhandle_t> lightmaps;
	VK_WorldLoadLightmaps( header, fileBase, fileSizeBytes, &lightmaps );
	vertices.reserve( std::min<size_t>( vertexLump.count, 262144 ) );
	indices.reserve( std::min<size_t>( indexLump.count, 524288 ) );

	uint32_t appendedSurfaces = 0;
	uint32_t planarSurfaces = 0;
	uint32_t patchSurfaces = 0;
	uint32_t triangleSoupSurfaces = 0;
	uint32_t flareSurfaces = 0;
	uint32_t skippedSurfaces = 0;
	for ( size_t i = 0; i < surfaceLump.count; ++i )
	{
		const dsurface_t &surface = surfaces[i];
		if ( VK_BspSurfaceIsSkipped( surface, shaders, shaderLump.count ) )
		{
			++skippedSurfaces;
			continue;
		}

		bool appended = false;
		const uint32_t firstBatchIndex = static_cast<uint32_t>( indices.size() );
		switch ( LittleLong( surface.surfaceType ) )
		{
		case MST_PLANAR:
			appended = VK_WorldAppendIndexedSurface(
				surface, drawVerts, vertexLump.count, drawIndexes, indexLump.count, vertices, indices );
			if ( appended )
			{
				++planarSurfaces;
			}
			break;
		case MST_PATCH:
			appended = VK_WorldAppendPatchSurface( surface, drawVerts, vertexLump.count, vertices, indices );
			if ( appended )
			{
				++patchSurfaces;
			}
			break;
		case MST_TRIANGLE_SOUP:
			appended = VK_WorldAppendIndexedSurface(
				surface, drawVerts, vertexLump.count, drawIndexes, indexLump.count, vertices, indices );
			if ( appended )
			{
				++triangleSoupSurfaces;
			}
			break;
		case MST_FLARE:
			++flareSurfaces;
			break;
		default:
			break;
		}

		if ( appended )
		{
			constexpr int lightmapByVertex = -3;
			++appendedSurfaces;
			const qhandle_t shader = VK_WorldRegisterSurfaceShader( surface, shaders, shaderLump.count );
			qhandle_t lightmap = 2;
			const int lightmapIndex = LittleLong( surface.lightmapNum[0] );
			const bool vertexLit = lightmapIndex == lightmapByVertex;
			if ( lightmapIndex >= 0 && static_cast<size_t>( lightmapIndex ) < lightmaps.size() )
			{
				lightmap = lightmaps[lightmapIndex];
			}
			VK_WorldAppendBatch(
				batches,
				firstBatchIndex,
				static_cast<uint32_t>( indices.size() ) - firstBatchIndex,
				shader,
				lightmap,
				vertexLit,
				static_cast<uint32_t>( i ) );
			VK_WorldAppendSurfaceSpriteBatches(
				&surfaceSpriteBatches,
				&surfaceSpriteBuildStats,
				vertices,
				indices,
				firstBatchIndex,
				static_cast<uint32_t>( indices.size() ) - firstBatchIndex,
				shader,
				static_cast<uint32_t>( i ) );
		}
		else
		{
			++skippedSurfaces;
		}
	}

	VK_LogWaterfallGeometry( vertices, indices, batches );

	if ( vertices.empty() || indices.empty() )
	{
		ri.Printf( PRINT_WARNING, "rd-vulkan-world: %s produced no drawable BSP geometry\n", name );
		ri.FS_FreeFile( buffer );
		return;
	}
	if ( vk.device == VK_NULL_HANDLE || vk.commandPool == VK_NULL_HANDLE )
	{
		ri.Printf( PRINT_WARNING, "rd-vulkan-world: backend is not initialized; skipping BSP buffer upload\n" );
		ri.FS_FreeFile( buffer );
		return;
	}

	vk_world_geometry_t world = {};
	VK_WorldConfigureSky(
		surfaces, surfaceLump.count, shaders, shaderLump.count, &world );
	VK_WorldConfigureGlobalFog( header, fileBase, fileSizeBytes, &world );
	const VkDeviceSize vertexBytes = static_cast<VkDeviceSize>( vertices.size() * sizeof( vertices[0] ) );
	const VkDeviceSize indexBytes = static_cast<VkDeviceSize>( indices.size() * sizeof( indices[0] ) );
	const bool uploaded =
		VK_UploadBuffer( vertices.data(), vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			&world.vertexBuffer, &world.vertexMemory, "world vertex" ) &&
		VK_UploadBuffer( indices.data(), indexBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			&world.indexBuffer, &world.indexMemory, "world index" );
	if ( !uploaded )
	{
		VK_DestroyBuffer( &world.vertexBuffer, &world.vertexMemory );
		VK_DestroyBuffer( &world.indexBuffer, &world.indexMemory );
		ri.FS_FreeFile( buffer );
		return;
	}

	world.vertexCount = static_cast<uint32_t>( vertices.size() );
	world.indexCount = static_cast<uint32_t>( indices.size() );
	world.surfaceCount = appendedSurfaces;
	world.batches = batches;
	world.surfaceSpriteBatches = std::move( surfaceSpriteBatches );
	world.surfaceBatchIndex.assign( surfaceLump.count, std::numeric_limits<uint32_t>::max() );
	for ( size_t i = 0; i < world.batches.size(); ++i )
	{
		const uint32_t surfaceIndex = world.batches[i].surfaceIndex;
		if ( surfaceIndex < world.surfaceBatchIndex.size() )
		{
			world.surfaceBatchIndex[surfaceIndex] = static_cast<uint32_t>( i );
		}
	}
	VK_WorldLoadInlineModels( header, fileBase, fileSizeBytes, surfaceLump.count, &world );
	VK_WorldLoadVisibilityData( header, fileBase, fileSizeBytes, surfaceLump.count, &world );
	for ( const vk_world_batch_t &batch : world.batches )
	{
		if ( batch.shader != 1 )
		{
			++world.texturedBatchCount;
		}
	}
	vk.world = world;

	ri.Printf( PRINT_ALL,
		"rd-vulkan-world: uploaded %s: surfaces=%u/%zu planar=%u patches=%u trisoup=%u flares=%u skipped=%u vertices=%u indices=%u batches=%zu textured=%u spriteBatches=%zu spriteAnchors=%zu\n",
		name, appendedSurfaces, surfaceLump.count, planarSurfaces, patchSurfaces, triangleSoupSurfaces,
		flareSurfaces, skippedSurfaces, vk.world.vertexCount, vk.world.indexCount,
		vk.world.batches.size(), vk.world.texturedBatchCount,
		vk.world.surfaceSpriteBatches.size(),
		std::accumulate(
			vk.world.surfaceSpriteBatches.begin(), vk.world.surfaceSpriteBatches.end(), size_t{ 0 },
			[]( size_t count, const vk_surface_sprite_batch_t &batch )
			{
				return count + batch.instances.size();
			} ) );
	if ( surfaceSpriteBuildStats.candidateStages > 0 )
	{
		ri.Printf( PRINT_ALL,
			"rd-vulkan-surfacesprites: build candidates=%zu supported=%zu unsupported=%zu invalid=%zu unavailableTextures=%zu triangles=%zu invalidTriangles=%zu facingRejected=%zu areaRejected=%zu anchors=%zu\n",
			surfaceSpriteBuildStats.candidateStages,
			surfaceSpriteBuildStats.supportedStages,
			surfaceSpriteBuildStats.unsupportedStages,
			surfaceSpriteBuildStats.invalidStages,
			surfaceSpriteBuildStats.unavailableTextures,
			surfaceSpriteBuildStats.triangles,
			surfaceSpriteBuildStats.invalidTriangles,
			surfaceSpriteBuildStats.facingRejectedTriangles,
			surfaceSpriteBuildStats.areaRejectedTriangles,
			surfaceSpriteBuildStats.anchors );
		ri.Printf( PRINT_ALL,
			"rd-vulkan-surfacesprites: types vertical=%zu/%zu oriented=%zu/%zu effect=%zu/%zu flattened=%zu/%zu (stages/anchors)\n",
			surfaceSpriteBuildStats.stagesByType[VK_SURFACE_SPRITE_VERTICAL],
			surfaceSpriteBuildStats.anchorsByType[VK_SURFACE_SPRITE_VERTICAL],
			surfaceSpriteBuildStats.stagesByType[VK_SURFACE_SPRITE_ORIENTED],
			surfaceSpriteBuildStats.anchorsByType[VK_SURFACE_SPRITE_ORIENTED],
			surfaceSpriteBuildStats.stagesByType[VK_SURFACE_SPRITE_EFFECT],
			surfaceSpriteBuildStats.anchorsByType[VK_SURFACE_SPRITE_EFFECT],
			surfaceSpriteBuildStats.stagesByType[VK_SURFACE_SPRITE_FLATTENED],
			surfaceSpriteBuildStats.anchorsByType[VK_SURFACE_SPRITE_FLATTENED] );
	}

	ri.FS_FreeFile( buffer );
}

void VK_Backend_ClearScene()
{
	vk.sceneEntityCount = 0;
	vk.scenePolyCount = 0;
	vk.scenePolyVertexCount = 0;
	vk.sceneLightCount = 0;
	std::memset( vk.sceneEntityTypes, 0, sizeof( vk.sceneEntityTypes ) );
	vk.sceneEntities.clear();
}

void VK_Backend_AddRefEntity( const refEntity_t *entity )
{
	if ( entity == nullptr )
	{
		return;
	}
	++vk.sceneEntityCount;
	if ( entity->reType >= 0 && entity->reType < RT_MAX_REF_ENTITY_TYPE )
	{
		++vk.sceneEntityTypes[entity->reType];
	}
	if ( vk.sceneEntities.size() < 4096 )
	{
		vk.sceneEntities.push_back( *entity );
	}
}

void VK_Backend_AddPoly( qhandle_t, int vertexCount, const polyVert_t *vertices )
{
	if ( vertexCount <= 0 || vertices == nullptr )
	{
		return;
	}
	++vk.scenePolyCount;
	vk.scenePolyVertexCount += static_cast<uint32_t>( vertexCount );
}

void VK_Backend_AddLight( const vec3_t, float intensity, float, float, float )
{
	if ( intensity > 0.0f )
	{
		++vk.sceneLightCount;
	}
}

static void VK_ResetWorldEffects( const char *levelName )
{
	vk.weatherSnow = false;
	vk.weatherGusting = false;
	std::memset( vk.weatherWind, 0, sizeof( vk.weatherWind ) );
	vk.weatherSnowCount = 0;
	vk.weatherSnowShader = 0;
	vk.weatherZones.clear();
	vk.weatherOutsideCache.clear();
	vk.weatherSnowBatch = {};
	vk.weatherSnowBatchFrame = ~uint64_t{ 0 };
	vk.loggedWeatherDraw = false;
	vk.loggedWeatherSuppressed = false;
	vk.loggedWeatherResourceFailure = false;
	ri.Printf( PRINT_ALL, "rd-vulkan-worldfx: reset for %s\n",
		levelName != nullptr && levelName[0] != '\0' ? levelName : "renderer initialization" );
}

void VK_Backend_BeginLevelLoad( const char *name )
{
	VK_ResetWorldEffects( name );
	vk.loggedWeaponOnlyEntities = 0;
	vk.loggedWeaponOnlyModels.clear();
}

void VK_Backend_InitWorldEffects()
{
	VK_ResetWorldEffects( nullptr );
}

void VK_Backend_WorldEffectCommand( const char *command )
{
	if ( command == nullptr || command[0] == '\0' )
	{
		return;
	}

	ri.Printf( PRINT_ALL, "rd-vulkan-worldfx: command %s\n", command );
	vk_weather_zone_t zone = {};
	if ( std::sscanf( command,
			"zone ( %f %f %f ) ( %f %f %f )",
			&zone.mins[0], &zone.mins[1], &zone.mins[2],
			&zone.maxs[0], &zone.maxs[1], &zone.maxs[2] ) == 6 )
	{
		const auto duplicate = std::find_if(
			vk.weatherZones.begin(), vk.weatherZones.end(),
			[&zone]( const vk_weather_zone_t &existing )
			{
				return std::memcmp( &existing, &zone, sizeof( zone ) ) == 0;
			} );
		if ( duplicate == vk.weatherZones.end() )
		{
			vk.weatherZones.push_back( zone );
		}
		return;
	}
	if ( std::sscanf( command, "constantwind ( %f %f %f )",
			&vk.weatherWind[0], &vk.weatherWind[1], &vk.weatherWind[2] ) == 3 )
	{
		return;
	}
	if ( Q_stricmpn( command, "gustingwind", 11 ) == 0 )
	{
		vk.weatherGusting = true;
		return;
	}
	if ( Q_stricmpn( command, "snow", 4 ) == 0 ||
		 Q_stricmpn( command, "lightsnow", 9 ) == 0 ||
		 Q_stricmpn( command, "heavysnow", 9 ) == 0 )
	{
		vk.weatherSnow = true;
		vk.weatherSnowCount = Q_stricmpn( command, "lightsnow", 9 ) == 0 ? 500u :
			( Q_stricmpn( command, "heavysnow", 9 ) == 0 ? 1500u : 1000u );
		unsigned int configuredCount = 0;
		if ( std::sscanf( command, "%*s %u", &configuredCount ) == 1 && configuredCount > 0 )
		{
			vk.weatherSnowCount = std::min( configuredCount, 4000u );
		}
		vk.weatherSnowShader = VK_Backend_RegisterTexture( "gfx/effects/snowflake1.tga" );
	}
}

void VK_Backend_AddWeatherZone( vec3_t mins, vec3_t maxs )
{
	ri.Printf( PRINT_ALL,
		"rd-vulkan-worldfx: zone (%.1f %.1f %.1f) (%.1f %.1f %.1f)\n",
		mins[0], mins[1], mins[2], maxs[0], maxs[1], maxs[2] );
	vk_weather_zone_t zone = {};
	VectorCopy( mins, zone.mins );
	VectorCopy( maxs, zone.maxs );
	vk.weatherZones.push_back( zone );
}

void VK_Backend_RenderScene( const refdef_t *refdef )
{
	if ( refdef == nullptr )
	{
		return;
	}
	vk.sceneRenderedThisFrame = true;
	if ( ( refdef->rdflags & RDF_NOWORLDMODEL ) == 0 )
	{
		vk.sceneWorldRenderedThisFrame = true;
		vk.worldRefdef = *refdef;
		vk.haveWorldRefdef = true;
		vk.worldEntities = vk.sceneEntities;
		if ( !vk.loggedGameplayViewMode )
		{
			const cvar_t *thirdPerson = ri.Cvar_Get( "cg_thirdPerson", "0", 0 );
			ri.Printf(
				PRINT_ALL,
				"rd-vulkan-scene: gameplay view mode cg_thirdPerson=%d worldscale=%.2f\n",
				thirdPerson != nullptr ? thirdPerson->integer : 0,
				refdef->worldscale );
			vk.loggedGameplayViewMode = true;
		}
	}
	else
	{
		vk_scene_submission_t scene = {};
		scene.refdef = *refdef;
		scene.entities = vk.sceneEntities;
		scene.rectCountBefore = vk.rects.size();
		vk.screenScenes.push_back( std::move( scene ) );
	}
	++vk.sceneRenderCount;
	if ( !vk.loggedFirstScene || vk.sceneRenderCount <= 5 || ( vk.sceneRenderCount % 300 ) == 0 )
	{
		ri.Printf(
			PRINT_ALL,
			"rd-vulkan-scene: render %u viewport=%d,%d %dx%d fov=%.2fx%.2f "
			"origin=(%.2f %.2f %.2f) flags=0x%x worldscale=%.2f entities=%u "
			"(models=%u sprites=%u) polys=%u/%u lights=%u\n",
			vk.sceneRenderCount,
			refdef->x, refdef->y, refdef->width, refdef->height,
			refdef->fov_x, refdef->fov_y,
			refdef->vieworg[0], refdef->vieworg[1], refdef->vieworg[2],
			refdef->rdflags, refdef->worldscale,
			vk.sceneEntityCount,
			vk.sceneEntityTypes[RT_MODEL], vk.sceneEntityTypes[RT_SPRITE],
			vk.scenePolyCount, vk.scenePolyVertexCount, vk.sceneLightCount );
		vk.loggedFirstScene = true;
	}
}

void VK_Backend_BeginFrame()
{
	vk.rects.clear();
	vk.sceneRenderedThisFrame = false;
	vk.sceneWorldRenderedThisFrame = false;
	vk.worldEntities.clear();
	vk.screenScenes.clear();
	VK_Backend_SetColor( nullptr );
	VK_PrepareXrFrame();
}

void VK_Backend_SetColor( const float *color )
{
	if ( color != nullptr )
	{
		vk.currentColor[0] = color[0];
		vk.currentColor[1] = color[1];
		vk.currentColor[2] = color[2];
		vk.currentColor[3] = color[3];
	}
	else
	{
		vk.currentColor[0] = 1.0f;
		vk.currentColor[1] = 1.0f;
		vk.currentColor[2] = 1.0f;
		vk.currentColor[3] = 1.0f;
	}
}

qhandle_t VK_Backend_RegisterTexture( const char *name )
{
	if ( !vk.initialized || name == nullptr || name[0] == '\0' )
	{
		return 0;
	}

	for ( const vk_texture_name_t &registered : vk.textureNames )
	{
		if ( Q_stricmp( registered.name.c_str(), name ) == 0 )
		{
			return registered.handle;
		}
	}

	if ( vk.textures.size() >= 4096 )
	{
		ri.Printf( PRINT_WARNING, "rd-vulkan: texture descriptor capacity reached while registering %s\n", name );
		return 2;
	}

	const vk_shader_definition_t *definition = VK_FindShaderDefinition( name );
	if ( definition != nullptr )
	{
		const qhandle_t handle = static_cast<qhandle_t>( vk.textures.size() );
		vk.textures.emplace_back();
		vk.materials.emplace_back();
		vk.textureNames.push_back( { name, handle } );
		for ( const vk_shader_stage_definition_t &stageDefinition : definition->stages )
		{
			vk_material_stage_t stage = {};
			stage.lightmap = Q_stricmp( stageDefinition.imageName.c_str(), "$lightmap" ) == 0;
			stage.texture = stage.lightmap ? 2 : VK_FindOrLoadImage( stageDefinition.imageName.c_str() );
			stage.blendMode = stageDefinition.blendMode;
			stage.alphaTest = stageDefinition.alphaTest;
			stage.depthWrite = stageDefinition.depthWrite;
			stage.clampMap = stageDefinition.clampMap;
			stage.surfaceSprite = stageDefinition.surfaceSprite;
			stage.alpha = stageDefinition.alpha;
			stage.scroll[0] = stageDefinition.scroll[0];
			stage.scroll[1] = stageDefinition.scroll[1];
			stage.stretchType = stageDefinition.stretchType;
			std::memcpy( stage.stretch, stageDefinition.stretch, sizeof( stage.stretch ) );
			std::memcpy( stage.turbulence, stageDefinition.turbulence, sizeof( stage.turbulence ) );
			std::memcpy( stage.color, stageDefinition.color, sizeof( stage.color ) );
			stage.vertexColor = stageDefinition.vertexColor;
			vk.materials[handle].stages.push_back( stage );
		}
		ri.Printf( PRINT_ALL, "rd-vulkan: material %d: %s (%zu stages)\n",
			handle, name, vk.materials[handle].stages.size() );
		return handle;
	}

	const qhandle_t handle = VK_FindOrLoadImage( name );
	vk.textureNames.push_back( { name, handle } );
	if ( handle == 2 )
	{
		++vk.missingTextureCount;
		if ( vk.missingTextureCount <= 20 || ( vk.missingTextureCount % 50 ) == 0 )
		{
			ri.Printf( PRINT_ALL, "rd-vulkan: texture or material not found, using transparent fallback: %s\n", name );
		}
		return handle;
	}

	if ( handle <= 12 || ( handle % 50 ) == 0 )
	{
		ri.Printf( PRINT_ALL, "rd-vulkan: texture %d: %s (%ux%u)\n", handle, name,
			vk.textures[handle].width, vk.textures[handle].height );
	}
	return handle;
}

void VK_Backend_DrawStretchPic(
	float x, float y, float w, float h,
	float s1, float t1, float s2, float t2,
	qhandle_t shader )
{
	if ( shader <= 0 || static_cast<size_t>( shader ) >= vk.textures.size() )
	{
		shader = 2;
	}

	if ( static_cast<size_t>( shader ) < vk.materials.size() && !vk.materials[shader].stages.empty() )
	{
		const float seconds = static_cast<float>( ri.Milliseconds() ) * 0.001f;
		for ( const vk_material_stage_t &stage : vk.materials[shader].stages )
		{
			if ( stage.surfaceSprite.type != VK_SURFACE_SPRITE_NONE )
			{
				continue;
			}
			float color[4] = {
				vk.currentColor[0],
				vk.currentColor[1],
				vk.currentColor[2],
				vk.currentColor[3] * stage.alpha,
			};
			const float scrollS = stage.scroll[0] * seconds;
			const float scrollT = stage.scroll[1] * seconds;
			VK_Backend_AppendScreenRect(
				x, y, w, h, color,
				s1 + scrollS, t1 + scrollT,
				s2 + scrollS, t2 + scrollT,
				stage.texture, stage.blendMode );
		}
		return;
	}
	VK_Backend_AppendScreenRect( x, y, w, h, vk.currentColor, s1, t1, s2, t2, shader, VK_BLEND_ALPHA );
}

void VK_Backend_DrawStretchRaw( int x, int y, int w, int h )
{
	if ( !vk.loggedRawStretch )
	{
		ri.Printf( PRINT_ALL, "rd-vulkan: DrawStretchRaw placeholder queued at %d,%d %dx%d\n", x, y, w, h );
		vk.loggedRawStretch = true;
	}

	const float rawColor[4] = { 0.00f, 0.55f, 0.62f, 0.92f };
	VK_Backend_AppendScreenRect(
		static_cast<float>( x ),
		static_cast<float>( y ),
		static_cast<float>( w ),
		static_cast<float>( h ),
		rawColor );
}

static XrVector3f VK_RotateVector( const XrQuaternionf &q, const XrVector3f &v )
{
	const XrVector3f cross = {
		q.y * v.z - q.z * v.y,
		q.z * v.x - q.x * v.z,
		q.x * v.y - q.y * v.x,
	};
	const XrVector3f nestedCross = {
		q.y * cross.z - q.z * cross.y,
		q.z * cross.x - q.x * cross.z,
		q.x * cross.y - q.y * cross.x,
	};
	return {
		v.x + 2.0f * ( q.w * cross.x + nestedCross.x ),
		v.y + 2.0f * ( q.w * cross.y + nestedCross.y ),
		v.z + 2.0f * ( q.w * cross.z + nestedCross.z ),
	};
}

static bool VK_CaptureScreenLayerPose( XrTime displayTime )
{
	XrSpaceLocation headLocation = {};
	headLocation.type = XR_TYPE_SPACE_LOCATION;
	if ( !VK_CheckXr( xrLocateSpace( vk.viewSpace, vk.localSpace, displayTime, &headLocation ),
			"xrLocateSpace(screen layer)" ) )
	{
		return false;
	}

	const XrSpaceLocationFlags requiredFlags =
		XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_POSITION_VALID_BIT;
	if ( ( headLocation.locationFlags & requiredFlags ) != requiredFlags )
	{
		return false;
	}

	vk.screenLayerPose = headLocation.pose;
	const XrVector3f forward = VK_RotateVector( headLocation.pose.orientation, { 0.0f, 0.0f, -2.5f } );
	vk.screenLayerPose.position.x += forward.x;
	vk.screenLayerPose.position.y += forward.y;
	vk.screenLayerPose.position.z += forward.z;
	vk.screenLayerPoseValid = true;
	ri.Printf( PRINT_ALL, "rd-vulkan: captured world-locked screen pose at (%.3f %.3f %.3f)\n",
		vk.screenLayerPose.position.x,
		vk.screenLayerPose.position.y,
		vk.screenLayerPose.position.z );
	return true;
}

void VK_Backend_SubmitClearFrame()
{
	if ( !vk.initialized || vk.exitRenderLoop )
	{
		return;
	}

	const size_t gameRectCount = vk.rects.size();
	++vk.frameIndex;
	if ( gameRectCount == 0 )
	{
		if ( vk.sceneWorldRenderedThisFrame )
		{
			// World frames can legitimately have no 2D overlay; keep debug markers out of gameplay.
		}
		else
		{
			if ( !vk.loggedNoRects )
			{
				ri.Printf( PRINT_ALL, "rd-vulkan: no 2D rectangles queued by the engine; drawing diagnostic marker\n" );
				vk.loggedNoRects = true;
			}

			const float markerColor[4] = { 1.0f, 0.0f, 0.85f, 1.0f };
			VK_Backend_AppendScreenRect( 48.0f, 48.0f, 160.0f, 96.0f, markerColor );
		}
	}
	else if ( gameRectCount > vk.maxRectCount || !vk.loggedFirstRects )
	{
		const vk_rect_t &first = vk.rects.front();
		ri.Printf(
			PRINT_ALL,
			"rd-vulkan: %zu 2D rectangles queued; first ndc=(%.3f %.3f %.3f %.3f) rgba=(%.2f %.2f %.2f %.2f)\n",
			gameRectCount,
			first.rect[0],
			first.rect[1],
			first.rect[2],
			first.rect[3],
			first.color[0],
			first.color[1],
			first.color[2],
			first.color[3] );
		vk.maxRectCount = std::max( vk.maxRectCount, gameRectCount );
		vk.loggedFirstRects = true;
	}
	else if ( vk.frameIndex % 300 == 0 )
	{
		ri.Printf( PRINT_ALL, "rd-vulkan: current 2D rectangle count: %zu\n", gameRectCount );
	}
	if ( !vk.frameBegun )
	{
		return;
	}
	const XrFrameState frameState = vk.frameState;

	XrCompositionLayerBaseHeader *layers[1] = {};
	XrCompositionLayerProjection projectionLayer = {};
	XrCompositionLayerProjectionView projectionViews[VK_BACKEND_EYE_COUNT] = {};
	XrCompositionLayerQuad screenLayer = {};
	uint32_t layerCount = 0;
	const bool useScreenLayer = !vk.sceneWorldRenderedThisFrame &&
		ri.TBXR_useScreenLayer != nullptr && ri.TBXR_useScreenLayer();

	if ( !vk.screenLayerStateKnown || useScreenLayer != vk.screenLayerActive )
	{
		ri.Printf( PRINT_ALL, "rd-vulkan: OpenXR composition mode: %s\n",
			useScreenLayer ? "quad screen layer" : "stereo projection layer" );
		vk.screenLayerActive = useScreenLayer;
		vk.screenLayerStateKnown = true;
		vk.screenLayerPoseValid = false;
	}

	if ( frameState.shouldRender && vk.viewsValid && VK_CreateSwapchains() )
	{
		if ( useScreenLayer )
		{
			const float neutralTint[4] = { 0.08f, 0.10f, 0.12f, 1.0f };
			if ( VK_RenderEye( 0, neutralTint ) )
			{
				if ( !vk.screenLayerPoseValid )
				{
					VK_CaptureScreenLayerPose( frameState.predictedDisplayTime );
				}

				screenLayer.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
				screenLayer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
				screenLayer.space = vk.screenLayerPoseValid ? vk.localSpace : vk.viewSpace;
				screenLayer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
				screenLayer.subImage.swapchain = vk.colorSwapchain[0];
				screenLayer.subImage.imageRect.extent.width =
					static_cast<int32_t>( vk.viewConfiguration[0].recommendedImageRectWidth );
				screenLayer.subImage.imageRect.extent.height =
					static_cast<int32_t>( vk.viewConfiguration[0].recommendedImageRectHeight );
				if ( vk.screenLayerPoseValid )
				{
					screenLayer.pose = vk.screenLayerPose;
				}
				else
				{
					screenLayer.pose.orientation.w = 1.0f;
					screenLayer.pose.position.z = -2.5f;
				}
				screenLayer.size.width = 3.2f;
				screenLayer.size.height = 2.4f;

				layers[0] = reinterpret_cast<XrCompositionLayerBaseHeader *>( &screenLayer );
				layerCount = 1;
			}
		}
		else
		{
			const float eyeTints[VK_BACKEND_EYE_COUNT][4] = {
				{ 0.02f, 0.20f, 1.00f, 1.0f },
				{ 1.00f, 0.22f, 0.04f, 1.0f },
			};

			bool rendered = true;
			for ( int eye = 0; eye < VK_BACKEND_EYE_COUNT; ++eye )
			{
				rendered = VK_RenderEye( eye, eyeTints[eye] ) && rendered;

				projectionViews[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
				projectionViews[eye].pose = vk.views[eye].pose;
				projectionViews[eye].fov = vk.views[eye].fov;
				projectionViews[eye].subImage.swapchain = vk.colorSwapchain[eye];
				projectionViews[eye].subImage.imageRect.extent.width =
					static_cast<int32_t>( vk.viewConfiguration[eye].recommendedImageRectWidth );
				projectionViews[eye].subImage.imageRect.extent.height =
					static_cast<int32_t>( vk.viewConfiguration[eye].recommendedImageRectHeight );
			}

			if ( rendered )
			{
				projectionLayer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
				projectionLayer.space = vk.localSpace;
				projectionLayer.viewCount = ARRAY_LEN( projectionViews );
				projectionLayer.views = projectionViews;

				layers[0] = reinterpret_cast<XrCompositionLayerBaseHeader *>( &projectionLayer );
				layerCount = 1;
			}
		}
	}

	XrFrameEndInfo endInfo = {};
	endInfo.type = XR_TYPE_FRAME_END_INFO;
	endInfo.displayTime = frameState.predictedDisplayTime;
	endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	endInfo.layerCount = layerCount;
	endInfo.layers = layers;
	VK_CheckXr( xrEndFrame( vk.xrSession, &endInfo ), "xrEndFrame" );
	vk.frameBegun = false;
}
