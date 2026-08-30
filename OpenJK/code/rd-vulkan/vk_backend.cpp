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
#include <array>
#include <cctype>
#include <chrono>
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
#include <unordered_set>
#include <utility>
#include <vector>

enum { VK_BACKEND_EYE_COUNT = 2 };

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
	float rotation[4];
	float uvRotation[2];
	qhandle_t texture;
	vk_blend_mode_t blendMode;
	bool forceHudStereo;
	bool repeatTexture;
	bool headLockedOverlay;
	bool forceSenseVignette;
	bool forceSenseRays;
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
	uint32_t mipLevels;
};

struct vk_texture_name_t
{
	std::string name;
	qhandle_t handle;
};

struct vk_material_stage_t
{
	qhandle_t texture;
	int videoHandle;
	bool videoMap;
	vk_blend_mode_t blendMode;
	vk_alpha_test_t alphaTest;
	float alpha;
	float scroll[2];
	float tcScale[2];
	float rotateSpeed;
	vk_waveform_t stretchType;
	float stretch[4];
	float turbulence[3];
	float color[4];
	bool lightmap;
	bool vertexColor;
	bool entityColor;
	bool lightingDiffuse;
	bool lightingDiffuseEntity;
	bool depthWrite;
	bool clampMap;
	bool environmentMap;
	bool glow;
	bool effectBoost;
	uint32_t yavinRiverStage;
	bool yavinWaterBase;
	bool yavinWaterDetail;
	bool waterWake;
	vk_surface_sprite_config_t surfaceSprite;
};

struct vk_material_t
{
	std::vector<vk_material_stage_t> stages;
	bool polygonOffset = false;
};

struct vk_shader_stage_definition_t
{
	std::string imageName;
	std::string videoName;
	vk_blend_mode_t blendMode;
	vk_alpha_test_t alphaTest;
	float alpha;
	float scroll[2];
	float tcScale[2];
	float rotateSpeed;
	vk_waveform_t stretchType;
	float stretch[4];
	float turbulence[3];
	float color[4];
	bool vertexColor;
	bool entityColor;
	bool lightingDiffuse;
	bool lightingDiffuseEntity;
	bool depthWrite;
	bool clampMap;
	bool environmentMap;
	bool glow;
	bool detail;
	vk_surface_sprite_config_t surfaceSprite;
};

struct vk_cinematic_texture_t
{
	int client;
	qhandle_t texture;
	uint32_t width;
	uint32_t height;
	bool loggedUpload;
	uint32_t runCount;
	uint32_t uploadCount;
	bool loggedNonzero;
};

struct vk_shader_definition_t
{
	std::string name;
	std::string skyOuterbox;
	float skyCloudHeight;
	float fogColor[3];
	float fogDepth;
	bool hasFog;
	bool polygonOffset = false;
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

static_assert( sizeof( vk_world_vertex_t ) == 56,
	"world vertex layout must remain compact for streamed animated models" );

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
	qhandle_t lightmaps[MAXLIGHTMAPS];
	byte lightmapStyles[MAXLIGHTMAPS];
	byte vertexStyles[MAXLIGHTMAPS];
	float mins[3];
	float maxs[3];
	uint32_t surfaceFlags;
	bool vertexLit;
	uint32_t surfaceIndex;
};

struct vk_world_indirect_group_t
{
	vk_world_batch_t representative;
	std::vector<uint32_t> batchIndices;
	uint32_t commandFirst;
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
	uint32_t surfaceFlags;
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
	vec3_t mins;
	vec3_t maxs;
	vec3_t facingNormal;
	bool hasFacingNormal;
};

struct vk_world_geometry_t
{
	VkBuffer vertexBuffer;
	VkDeviceMemory vertexMemory;
	VkBuffer indexBuffer;
	VkDeviceMemory indexMemory;
	VkBuffer indirectBuffer;
	VkDeviceMemory indirectMemory;
	VkDrawIndexedIndirectCommand *indirectMapped;
	uint32_t vertexCount;
	uint32_t indexCount;
	uint32_t indirectCommandCount;
	uint32_t surfaceCount;
	uint32_t texturedBatchCount;
	std::vector<vk_world_batch_t> batches;
	std::vector<vk_world_indirect_group_t> indirectGroups;
	std::vector<uint32_t> indirectVisibleGroupCounts[2];
	uint64_t indirectFrameIndex[2];
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
	float lightGridSize[3];
	float lightGridOrigin[3];
	int lightGridBounds[3];
	std::vector<dgrid_t> lightGridData;
	std::vector<uint16_t> lightGridArray;
};

struct vk_entity_lighting_t
{
	float ambient[3];
	float directed[3];
	float localDirection[3];
};

struct vk_glm_skin_vertex_t
{
	int boneIndices[iMAX_G2_BONEWEIGHTS_PER_VERT];
	float weights[iMAX_G2_BONEWEIGHTS_PER_VERT];
	int weightCount;
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
	std::vector<vk_glm_skin_vertex_t> glmSkinVertices;
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

struct vk_model_tag_t
{
	std::string name;
	vec3_t origin;
	vec3_t axis[3];
};

struct vk_world_stage_push_t
{
	float uvOffset[2];
	float alpha;
	float useLightmap;
	float color[4];
	float flags[4];
	float uvScale[2];
	float lightmapGamma;
	float padding;
};

static_assert( sizeof( vk_world_stage_push_t ) == sizeof( float ) * 16,
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
	int frameCount;
	int tagCount;
	float mins[3];
	float maxs[3];
	bool hasBounds;
	std::vector<vk_model_tag_t> tags;
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

struct vk_scene_poly_t
{
	qhandle_t shader;
	std::vector<polyVert_t> vertices;
};

struct vk_dynamic_light_t
{
	float origin[3];
	float radius;
	float color[3];
};

struct vk_scene_submission_t
{
	refdef_t refdef;
	std::vector<refEntity_t> entities;
	std::vector<vk_scene_poly_t> polys;
	std::vector<vk_dynamic_light_t> lights;
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

struct vk_ghoul2_surface_cache_key_t
{
	const CGhoul2Info *ghoul;
	const vk_model_surface_t *surface;
	int time;
	int disintegrationMode;

	bool operator==( const vk_ghoul2_surface_cache_key_t &other ) const
	{
		return ghoul == other.ghoul && surface == other.surface && time == other.time &&
			disintegrationMode == other.disintegrationMode;
	}
};

struct vk_ghoul2_surface_cache_hash_t
{
	size_t operator()( const vk_ghoul2_surface_cache_key_t &key ) const
	{
		size_t hash = std::hash<const void *>{}( key.ghoul );
		auto combine = [&hash]( size_t value )
		{
			hash ^= value + 0x9e3779b9u + ( hash << 6 ) + ( hash >> 2 );
		};
		combine( std::hash<const void *>{}( key.surface ) );
		combine( std::hash<int>{}( key.time ) );
		combine( std::hash<int>{}( key.disintegrationMode ) );
		return hash;
	}
};

struct vk_surface_sprite_stream_cache_t
{
	const vk_surface_sprite_batch_t *batch;
	VkDeviceSize vertexOffset;
	uint32_t vertexCount;
};

struct vk_skin_model_timing_t
{
	std::string name;
	uint64_t calls;
	uint64_t cacheHits;
	uint64_t misses;
	uint64_t vertices;
	double totalMs;
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
	XrActionSet controllerActionSet;
	XrPath handPaths[VK_BACKEND_EYE_COUNT];
	XrAction aimPoseAction;
	XrAction gripPoseAction;
	XrAction triggerAction;
	XrAction triggerClickAction;
	XrAction triggerTouchAction;
	XrAction squeezeAction;
	XrAction thumbstickAction;
	XrAction thumbstickClickAction;
	XrAction thumbstickTouchAction;
	XrAction primaryButtonAction;
	XrAction secondaryButtonAction;
	XrAction primaryTouchAction;
	XrAction secondaryTouchAction;
	XrAction thumbrestTouchAction;
	XrAction menuAction;
	XrSpace aimSpaces[VK_BACKEND_EYE_COUNT];
	XrSpace gripSpaces[VK_BACKEND_EYE_COUNT];
	vrControllerType_t controllerType;
	bool loggedControllerInput;
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
	VkFormat colorRenderFormat;
	bool legacyColorActive;
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
	bool samplerAnisotropy;
	float maxSamplerAnisotropy;
	bool multiDrawIndirect;
	uint32_t maxDrawIndirectCount;
	VkDevice device;
	VkQueue queue;
	VkCommandPool commandPool;
	VkCommandBuffer commandBuffer;
	VkCommandBuffer eyeCommandBuffers[VK_BACKEND_EYE_COUNT];
	VkQueryPool timingQueryPool;
	uint32_t queueTimestampValidBits;
	float timestampPeriodNanoseconds;
	VkBuffer skinnedVertexBuffer;
	VkDeviceMemory skinnedVertexMemory;
	byte *skinnedVertexMapped;
	VkDeviceSize skinnedVertexCapacity;
	VkDeviceSize skinnedVertexOffset;
	uint64_t ghoul2CacheFrameIndex;
	std::deque<vk_ghoul2_bone_cache_t> ghoul2BoneCache;
	std::unordered_map<vk_ghoul2_surface_cache_key_t, VkDeviceSize,
		vk_ghoul2_surface_cache_hash_t> ghoul2SurfaceCache;
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
	VkPipeline texturedRectSourceAlphaAdditivePipeline;
	VkPipeline texturedRectInverseSourceAlphaAdditivePipeline;
	VkPipeline texturedRectDestinationColorAdditivePipeline;
	VkPipeline texturedRectOneMinusDestinationAlphaAdditivePipeline;
	VkPipeline texturedRectModulatePipeline;
	VkPipeline texturedRectDoubleModulatePipeline;
	VkPipeline texturedRectInverseSourceColorModulatePipeline;
	VkPipeline texturedRectScreenPipeline;
	VkPipeline diagnostic3dPipeline;
	VkPipeline worldPipeline;
	VkPipeline worldBackCullPipeline;
	VkPipeline worldFrontCullPipeline;
	VkPipeline worldAlphaPipeline;
	VkPipeline worldAlphaDepthWritePipeline;
	VkPipeline worldAdditivePipeline;
	VkPipeline worldSourceAlphaAdditivePipeline;
	VkPipeline worldInverseSourceAlphaAdditivePipeline;
	VkPipeline worldOneSourceAlphaPipeline;
	VkPipeline worldDestinationColorAdditivePipeline;
	VkPipeline worldOneMinusDestinationAlphaAdditivePipeline;
	VkPipeline worldModulatePipeline;
	VkPipeline worldDoubleModulatePipeline;
	VkPipeline worldInverseSourceColorModulatePipeline;
	VkPipeline worldScreenPipeline;
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
	std::unordered_set<qhandle_t> clampTextureHandles;
	std::vector<vk_texture_name_t> imageNames;
	std::vector<vk_texture_name_t> modelNames;
	std::vector<vk_texture_name_t> skinNames;
	std::vector<vk_skin_t> skins;
	std::vector<vk_model_t> models;
	std::vector<std::shared_ptr<vk_gla_t>> animations;
	std::vector<vk_material_t> materials;
	std::vector<vk_cinematic_texture_t> videoMaps;
	std::vector<int> videoMapsUsed;
	std::vector<vk_cinematic_texture_t> rawCinematics;
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
	bool loggedDisruptorScope;
	bool loggedForcePushEffect;
	bool loggedScepterLine;
	int loggedVideoSelectionClient;
	int loggedVideoSelectionInlineModel;
	uint32_t loggedVideoSelectionChanges;
	std::unordered_map<int, std::array<float, 3>> loggedVideoNeighborOrigins;
	bool loggedGhoul2Skinning;
	bool loggedGhoul2StreamInvalid;
	bool loggedGhoul2StreamOverflow;
	uint32_t loggedDiffuseModels;
	uint32_t loggedImplicitModelShaders;
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
	bool screenLayerContentValid;
	bool screenLayerTransitionHeld;
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
	std::array<std::array<byte, 4>, MAX_LIGHT_STYLES> lightStyles;
	std::vector<refEntity_t> sceneEntities;
	std::vector<vk_scene_poly_t> scenePolys;
	std::vector<vk_dynamic_light_t> sceneLights;
	std::vector<refEntity_t> worldEntities;
	std::vector<vk_scene_poly_t> worldPolys;
	std::vector<vk_dynamic_light_t> worldLights;
	bool havePortalRefdef;
	refdef_t portalRefdef;
	std::vector<refEntity_t> portalEntities;
	std::vector<vk_scene_poly_t> portalPolys;
	std::vector<vk_dynamic_light_t> portalLights;
	std::vector<vk_scene_submission_t> screenScenes;
	std::vector<vk_screen_scene_clip_t> screenSceneClips;
	bool sceneRenderedThisFrame;
	bool sceneWorldRenderedThisFrame;
	bool loggedFirstScene;
	bool loggedGameplayViewMode;
	bool loggedFirstModelDraw;
	bool loggedDynamicEffects;
	bool loggedDynamicEffectOverflow;
	bool loggedDynamicLighting;
	uint32_t loggedLightStyleUpdates;
	bool haveWorldRefdef;
	refdef_t worldRefdef;
	bool loggedDiagnosticDraw;
	bool loggedWorldDraw;
	bool loggedVisibleWorldMaterials;
	bool loggedShipInteriorMaterials;
	bool loggedShipInteriorModels;
	bool loggedYavinRiverDraw;
	uint32_t loggedMedpacEntities;
	cvar_t *diagnosticWorldCvar;
	cvar_t *materialAuditCvar;
	cvar_t *legacyColorCvar;
	cvar_t *picmipCvar;
	cvar_t *detailTexturesCvar;
	cvar_t *offsetFactorCvar;
	cvar_t *offsetUnitsCvar;
	bool depthBiasStateKnown;
	bool depthBiasEnabled;
	cvar_t *worldDebugCvar;
	uint8_t materialAuditPasses[2];
	cvar_t *glowIntensityCvar;
	cvar_t *glowRadiusCvar;
	cvar_t *waterEffectIntensityCvar;
	cvar_t *yavinRiverOpacityCvar;
	cvar_t *yavinRiverExtinctionCvar;
	cvar_t *yavinRiverDiagnosticCvar;
	cvar_t *yavinRiverStageMaskCvar;
	cvar_t *yavinRiverLightmapGammaCvar;
	cvar_t *yavinWaterTransparencyCvar;
	cvar_t *yavinWaterDetailIntensityCvar;
	cvar_t *waterWakeIntensityCvar;
	cvar_t *lightmapGammaCvar;
	cvar_t *ewebCullCvar;
	cvar_t *fxModelAuditCvar;
	int fxModelAuditLastTime;
	cvar_t *modelCullCvar;
	cvar_t *timingCvar;
	bool timingWasEnabled;
	bool loggedTimingNoGpu;
	uint32_t timingSamples;
	uint32_t timingGpuSamples;
	double timingRecordTotalMs;
	double timingRecordMaxMs;
	double timingWaitTotalMs;
	double timingWaitMaxMs;
	double timingGpuTotalMs;
	double timingGpuMaxMs;
	uint64_t timingLightTotal;
	uint32_t timingLightMax;
	uint64_t timingModelCandidateTotal;
	uint64_t timingModelCulledTotal;
	uint64_t timingModelDrawTotal;
	double timingSkyTotalMs;
	double timingBspTotalMs;
	double timingWorldLightTotalMs;
	double timingSpriteTotalMs;
	double timingModelTotalMs;
	double timingModelCullTotalMs;
	double timingModelBoneTotalMs;
	double timingModelSkinTotalMs;
	double timingModelSubmitTotalMs;
	double timingEffectTotalMs;
	uint64_t timingBspDrawTotal;
	std::unordered_map<const vk_model_t *, vk_skin_model_timing_t> timingSkinModels;
};

static vk_backend_state_t vk = {};
static void VK_LoadPendingRegistrations();
static bool VK_ModelBufferRangeValid( size_t offset, size_t byteCount, size_t limit );
static bool VK_PrepareXrFrame();
static void VK_UpdateJkxrHmdPose( XrTime displayTime );
static void VK_UpdateJkxrControllers( XrTime displayTime );
static bool VK_TimingEnabled();

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
	vk.controllerActionSet = XR_NULL_HANDLE;
	vk.aimPoseAction = XR_NULL_HANDLE;
	vk.gripPoseAction = XR_NULL_HANDLE;
	vk.triggerAction = XR_NULL_HANDLE;
	vk.triggerClickAction = XR_NULL_HANDLE;
	vk.triggerTouchAction = XR_NULL_HANDLE;
	vk.squeezeAction = XR_NULL_HANDLE;
	vk.thumbstickAction = XR_NULL_HANDLE;
	vk.thumbstickClickAction = XR_NULL_HANDLE;
	vk.thumbstickTouchAction = XR_NULL_HANDLE;
	vk.primaryButtonAction = XR_NULL_HANDLE;
	vk.secondaryButtonAction = XR_NULL_HANDLE;
	vk.primaryTouchAction = XR_NULL_HANDLE;
	vk.secondaryTouchAction = XR_NULL_HANDLE;
	vk.thumbrestTouchAction = XR_NULL_HANDLE;
	vk.menuAction = XR_NULL_HANDLE;
	vk.controllerType = VR_CONTROLLER_TYPE_UNKNOWN;
	vk.loggedControllerInput = false;
	vk.frameState = {};
	vk.frameState.type = XR_TYPE_FRAME_STATE;
	vk.frameBegun = false;
	vk.viewsValid = false;
	for ( int eye = 0; eye < VK_BACKEND_EYE_COUNT; ++eye )
	{
		vk.handPaths[eye] = XR_NULL_PATH;
		vk.aimSpaces[eye] = XR_NULL_HANDLE;
		vk.gripSpaces[eye] = XR_NULL_HANDLE;
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
	vk.colorRenderFormat = VK_FORMAT_R8G8B8A8_SRGB;
	vk.legacyColorActive = false;
	vk.depthFormat = VK_FORMAT_D32_SFLOAT;
	vk.swapchainsCreated = false;
	vk.renderResourcesCreated = false;
	vk.xrCreateVulkanInstanceKHR = nullptr;
	vk.xrGetVulkanGraphicsDevice2KHR = nullptr;
	vk.xrCreateVulkanDeviceKHR = nullptr;
	vk.xrGetVulkanGraphicsRequirements2KHR = nullptr;
	vk.instance = VK_NULL_HANDLE;
	vk.physicalDevice = VK_NULL_HANDLE;
	vk.samplerAnisotropy = false;
	vk.maxSamplerAnisotropy = 1.0f;
	vk.multiDrawIndirect = false;
	vk.maxDrawIndirectCount = 1;
	vk.device = VK_NULL_HANDLE;
	vk.queue = VK_NULL_HANDLE;
	vk.commandPool = VK_NULL_HANDLE;
	vk.commandBuffer = VK_NULL_HANDLE;
	for ( VkCommandBuffer &commandBuffer : vk.eyeCommandBuffers )
	{
		commandBuffer = VK_NULL_HANDLE;
	}
	vk.timingQueryPool = VK_NULL_HANDLE;
	vk.queueTimestampValidBits = 0;
	vk.timestampPeriodNanoseconds = 0.0f;
	vk.skinnedVertexBuffer = VK_NULL_HANDLE;
	vk.skinnedVertexMemory = VK_NULL_HANDLE;
	vk.skinnedVertexMapped = nullptr;
	vk.skinnedVertexCapacity = 0;
	vk.skinnedVertexOffset = 0;
	vk.ghoul2CacheFrameIndex = ~uint64_t{ 0 };
	vk.ghoul2BoneCache.clear();
	vk.ghoul2SurfaceCache.clear();
	vk.ghoul2SurfaceCache.reserve( 4096 );
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
	vk.texturedRectSourceAlphaAdditivePipeline = VK_NULL_HANDLE;
	vk.texturedRectInverseSourceAlphaAdditivePipeline = VK_NULL_HANDLE;
	vk.texturedRectDestinationColorAdditivePipeline = VK_NULL_HANDLE;
	vk.texturedRectOneMinusDestinationAlphaAdditivePipeline = VK_NULL_HANDLE;
	vk.texturedRectModulatePipeline = VK_NULL_HANDLE;
	vk.texturedRectDoubleModulatePipeline = VK_NULL_HANDLE;
	vk.texturedRectInverseSourceColorModulatePipeline = VK_NULL_HANDLE;
	vk.texturedRectScreenPipeline = VK_NULL_HANDLE;
	vk.diagnostic3dPipeline = VK_NULL_HANDLE;
	vk.worldPipeline = VK_NULL_HANDLE;
	vk.worldBackCullPipeline = VK_NULL_HANDLE;
	vk.worldFrontCullPipeline = VK_NULL_HANDLE;
	vk.worldAlphaPipeline = VK_NULL_HANDLE;
	vk.worldAlphaDepthWritePipeline = VK_NULL_HANDLE;
	vk.worldAdditivePipeline = VK_NULL_HANDLE;
	vk.worldSourceAlphaAdditivePipeline = VK_NULL_HANDLE;
	vk.worldInverseSourceAlphaAdditivePipeline = VK_NULL_HANDLE;
	vk.worldOneSourceAlphaPipeline = VK_NULL_HANDLE;
	vk.worldDestinationColorAdditivePipeline = VK_NULL_HANDLE;
	vk.worldOneMinusDestinationAlphaAdditivePipeline = VK_NULL_HANDLE;
	vk.worldModulatePipeline = VK_NULL_HANDLE;
	vk.worldDoubleModulatePipeline = VK_NULL_HANDLE;
	vk.worldInverseSourceColorModulatePipeline = VK_NULL_HANDLE;
	vk.worldScreenPipeline = VK_NULL_HANDLE;
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
	vk.clampTextureHandles.clear();
	vk.imageNames.clear();
	vk.modelNames.clear();
	vk.skinNames.clear();
	vk.skins.clear();
	vk.models.clear();
	vk.animations.clear();
	vk.materials.clear();
	vk.videoMaps.clear();
	vk.videoMapsUsed.clear();
	vk.rawCinematics.clear();
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
	vk.loggedDisruptorScope = false;
	vk.loggedForcePushEffect = false;
	vk.loggedScepterLine = false;
	vk.loggedVideoSelectionClient = -2;
	vk.loggedVideoSelectionInlineModel = -2;
	vk.loggedVideoSelectionChanges = 0;
	vk.loggedVideoNeighborOrigins.clear();
	vk.loggedGhoul2Skinning = false;
	vk.loggedGhoul2StreamInvalid = false;
	vk.loggedGhoul2StreamOverflow = false;
	vk.loggedDiffuseModels = 0;
	vk.loggedImplicitModelShaders = 0;
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
	vk.screenLayerContentValid = false;
	vk.screenLayerTransitionHeld = false;
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
	for ( std::array<byte, 4> &style : vk.lightStyles )
	{
		style.fill( 255 );
	}
	vk.sceneEntities.clear();
	vk.scenePolys.clear();
	vk.sceneLights.clear();
	vk.worldEntities.clear();
	vk.worldPolys.clear();
	vk.worldLights.clear();
	vk.havePortalRefdef = false;
	std::memset( &vk.portalRefdef, 0, sizeof( vk.portalRefdef ) );
	vk.portalEntities.clear();
	vk.portalPolys.clear();
	vk.portalLights.clear();
	vk.screenScenes.clear();
	vk.screenSceneClips.clear();
	vk.sceneRenderedThisFrame = false;
	vk.sceneWorldRenderedThisFrame = false;
	vk.loggedFirstScene = false;
	vk.loggedGameplayViewMode = false;
	vk.loggedFirstModelDraw = false;
	vk.loggedDynamicEffects = false;
	vk.loggedDynamicEffectOverflow = false;
	vk.loggedDynamicLighting = false;
	vk.loggedLightStyleUpdates = 0;
	vk.haveWorldRefdef = false;
	std::memset( &vk.worldRefdef, 0, sizeof( vk.worldRefdef ) );
	vk.loggedDiagnosticDraw = false;
	vk.loggedWorldDraw = false;
	vk.loggedVisibleWorldMaterials = false;
	vk.loggedShipInteriorMaterials = false;
	vk.loggedShipInteriorModels = false;
	vk.loggedYavinRiverDraw = false;
	vk.loggedMedpacEntities = 0;
	vk.diagnosticWorldCvar = nullptr;
	vk.materialAuditCvar = nullptr;
	vk.legacyColorCvar = nullptr;
	vk.picmipCvar = nullptr;
	vk.detailTexturesCvar = nullptr;
	vk.offsetFactorCvar = nullptr;
	vk.offsetUnitsCvar = nullptr;
	vk.depthBiasStateKnown = false;
	vk.depthBiasEnabled = false;
	vk.worldDebugCvar = nullptr;
	std::memset( vk.materialAuditPasses, 0, sizeof( vk.materialAuditPasses ) );
	vk.glowIntensityCvar = nullptr;
	vk.glowRadiusCvar = nullptr;
	vk.waterEffectIntensityCvar = nullptr;
	vk.yavinRiverOpacityCvar = nullptr;
	vk.yavinRiverExtinctionCvar = nullptr;
	vk.yavinRiverDiagnosticCvar = nullptr;
	vk.yavinRiverStageMaskCvar = nullptr;
	vk.yavinRiverLightmapGammaCvar = nullptr;
	vk.yavinWaterTransparencyCvar = nullptr;
	vk.yavinWaterDetailIntensityCvar = nullptr;
	vk.waterWakeIntensityCvar = nullptr;
	vk.lightmapGammaCvar = nullptr;
	vk.ewebCullCvar = nullptr;
	vk.fxModelAuditCvar = nullptr;
	vk.fxModelAuditLastTime = std::numeric_limits<int>::min();
	vk.modelCullCvar = nullptr;
	vk.timingCvar = nullptr;
	vk.timingWasEnabled = false;
	vk.loggedTimingNoGpu = false;
	vk.timingSamples = 0;
	vk.timingGpuSamples = 0;
	vk.timingRecordTotalMs = 0.0;
	vk.timingRecordMaxMs = 0.0;
	vk.timingWaitTotalMs = 0.0;
	vk.timingWaitMaxMs = 0.0;
	vk.timingGpuTotalMs = 0.0;
	vk.timingGpuMaxMs = 0.0;
	vk.timingLightTotal = 0;
	vk.timingLightMax = 0;
	vk.timingModelCandidateTotal = 0;
	vk.timingModelCulledTotal = 0;
	vk.timingModelDrawTotal = 0;
	vk.timingSkyTotalMs = 0.0;
	vk.timingBspTotalMs = 0.0;
	vk.timingWorldLightTotalMs = 0.0;
	vk.timingSpriteTotalMs = 0.0;
	vk.timingModelTotalMs = 0.0;
	vk.timingModelCullTotalMs = 0.0;
	vk.timingModelBoneTotalMs = 0.0;
	vk.timingModelSkinTotalMs = 0.0;
	vk.timingModelSubmitTotalMs = 0.0;
	vk.timingEffectTotalMs = 0.0;
	vk.timingBspDrawTotal = 0;
	vk.timingSkinModels.clear();
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
	vk_blend_mode_t blendMode = VK_BLEND_ALPHA,
	float angle = 0.0f,
	float pivotX = 0.0f,
	float pivotY = 0.0f,
	bool forceHudStereo = false,
	bool repeatTexture = false,
	float textureAngle = 0.0f,
	bool headLockedOverlay = false,
	bool forceSenseVignette = false,
	bool forceSenseRays = false )
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
	const float radians = DEG2RAD( angle );
	rect.rotation[0] = std::sin( radians );
	rect.rotation[1] = std::cos( radians );
	rect.rotation[2] = ( pivotX / 640.0f ) * 2.0f - 1.0f;
	rect.rotation[3] = ( pivotY / 480.0f ) * 2.0f - 1.0f;
	const float textureRadians = DEG2RAD( textureAngle );
	rect.uvRotation[0] = std::sin( textureRadians );
	rect.uvRotation[1] = std::cos( textureRadians );
	rect.texture = texture;
	rect.blendMode = blendMode;
	rect.forceHudStereo = forceHudStereo;
	rect.repeatTexture = repeatTexture;
	rect.headLockedOverlay = headLockedOverlay;
	rect.forceSenseVignette = forceSenseVignette;
	rect.forceSenseRays = forceSenseRays;
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
			vk.queueTimestampValidBits = families[i].timestampValidBits;
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
	VkPhysicalDeviceFeatures availableFeatures = {};
	vkGetPhysicalDeviceFeatures( vk.physicalDevice, &availableFeatures );
	VkPhysicalDeviceFeatures enabledFeatures = {};
	enabledFeatures.multiDrawIndirect = availableFeatures.multiDrawIndirect;
	vk.multiDrawIndirect = availableFeatures.multiDrawIndirect == VK_TRUE;
	if ( availableFeatures.samplerAnisotropy )
	{
		enabledFeatures.samplerAnisotropy = VK_TRUE;
		VkPhysicalDeviceProperties properties = {};
		vkGetPhysicalDeviceProperties( vk.physicalDevice, &properties );
		vk.samplerAnisotropy = true;
		vk.maxSamplerAnisotropy = std::min( 16.0f, properties.limits.maxSamplerAnisotropy );
	}
	deviceInfo.pEnabledFeatures = &enabledFeatures;

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
	VkPhysicalDeviceProperties properties = {};
	vkGetPhysicalDeviceProperties( vk.physicalDevice, &properties );
	vk.timestampPeriodNanoseconds = properties.limits.timestampPeriod;
	vk.maxDrawIndirectCount = std::max( 1u, properties.limits.maxDrawIndirectCount );
	ri.Printf( PRINT_ALL,
		"rd-vulkan: multi-draw indirect %s (maxDrawIndirectCount=%u)\n",
		vk.multiDrawIndirect ? "enabled" : "unavailable",
		vk.maxDrawIndirectCount );
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
	allocateInfo.commandBufferCount = VK_BACKEND_EYE_COUNT;

	if ( !VK_CheckVk( vkAllocateCommandBuffers(
			vk.device, &allocateInfo, vk.eyeCommandBuffers ), "vkAllocateCommandBuffers" ) )
	{
		return false;
	}
	vk.commandBuffer = vk.eyeCommandBuffers[0];
	return true;
}

static void VK_CreateTimingResources()
{
	if ( vk.queueTimestampValidBits == 0 || vk.timestampPeriodNanoseconds <= 0.0f )
	{
		return;
	}

	VkQueryPoolCreateInfo queryInfo = {};
	queryInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
	queryInfo.queryCount = VK_BACKEND_EYE_COUNT * 2;
	const VkResult result = vkCreateQueryPool(
		vk.device, &queryInfo, nullptr, &vk.timingQueryPool );
	if ( result != VK_SUCCESS )
	{
		vk.timingQueryPool = VK_NULL_HANDLE;
		ri.Printf( PRINT_WARNING,
			"rd-vulkan: GPU timing unavailable (vkCreateQueryPool: VkResult %d)\n",
			result );
	}
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
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
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
	const int picmip = vk.picmipCvar != nullptr
		? VK_ClampValue( vk.picmipCvar->integer, 0, 16 ) : 1;
	// GLES physically discards the first r_picmip levels. Keeping the complete
	// image but clamping the minimum sampled LOD is equivalent while preserving
	// the same minification choice for more distant surfaces.
	samplerInfo.minLod = static_cast<float>( picmip );
	samplerInfo.maxLod = 16.0f;
	samplerInfo.anisotropyEnable = vk.samplerAnisotropy ? VK_TRUE : VK_FALSE;
	samplerInfo.maxAnisotropy = vk.maxSamplerAnisotropy;
	ri.Printf( PRINT_ALL, "rd-vulkan: world texture anisotropy %.1fx picmip=%d\n",
		vk.samplerAnisotropy ? vk.maxSamplerAnisotropy : 1.0f, picmip );
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
	if ( vk.world.indirectMapped != nullptr && vk.device != VK_NULL_HANDLE )
	{
		vkUnmapMemory( vk.device, vk.world.indirectMemory );
		vk.world.indirectMapped = nullptr;
	}
	VK_DestroyBuffer( &vk.world.vertexBuffer, &vk.world.vertexMemory );
	VK_DestroyBuffer( &vk.world.indexBuffer, &vk.world.indexMemory );
	VK_DestroyBuffer( &vk.world.indirectBuffer, &vk.world.indirectMemory );
	vk.world.vertexCount = 0;
	vk.world.indexCount = 0;
	vk.world.indirectCommandCount = 0;
	vk.world.surfaceCount = 0;
	vk.world.texturedBatchCount = 0;
	vk.world.batches.clear();
	vk.world.indirectGroups.clear();
	for ( std::vector<uint32_t> &visibleCounts : vk.world.indirectVisibleGroupCounts )
	{
		visibleCounts.clear();
	}
	vk.world.indirectFrameIndex[0] = 0;
	vk.world.indirectFrameIndex[1] = 0;
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
	std::memset( vk.world.lightGridSize, 0, sizeof( vk.world.lightGridSize ) );
	std::memset( vk.world.lightGridOrigin, 0, sizeof( vk.world.lightGridOrigin ) );
	std::memset( vk.world.lightGridBounds, 0, sizeof( vk.world.lightGridBounds ) );
	vk.world.lightGridData.clear();
	vk.world.lightGridArray.clear();
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
	VkFormat format = VK_FORMAT_UNDEFINED,
	bool mipmapped = false )
{
	if ( format == VK_FORMAT_UNDEFINED )
	{
		// JKXR's GL path uploads ordinary images as unsized RGBA and disables
		// framebuffer sRGB conversion. Preserve those authored byte-space
		// operations when the matching Vulkan color path is active.
		format = vk.legacyColorActive
			? VK_FORMAT_R8G8B8A8_UNORM
			: VK_FORMAT_R8G8B8A8_SRGB;
	}
	const uint32_t mipLevels = mipmapped
		? 1u + static_cast<uint32_t>( std::floor(
			std::log2( static_cast<double>( std::max( width, height ) ) ) ) )
		: 1u;
	std::vector<byte> uploadPixels(
		pixels,
		pixels + static_cast<size_t>( width ) * height * 4 );
	std::vector<VkBufferImageCopy> copyRegions;
	copyRegions.reserve( mipLevels );
	uint32_t mipWidth = width;
	uint32_t mipHeight = height;
	size_t mipOffset = 0;
	for ( uint32_t level = 0; level < mipLevels; ++level )
	{
		VkBufferImageCopy copy = {};
		copy.bufferOffset = mipOffset;
		copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copy.imageSubresource.mipLevel = level;
		copy.imageSubresource.layerCount = 1;
		copy.imageExtent = { mipWidth, mipHeight, 1 };
		copyRegions.push_back( copy );

		if ( level + 1 >= mipLevels )
		{
			break;
		}
		const uint32_t nextWidth = std::max( 1u, mipWidth / 2 );
		const uint32_t nextHeight = std::max( 1u, mipHeight / 2 );
		const size_t nextOffset = uploadPixels.size();
		uploadPixels.resize( nextOffset + static_cast<size_t>( nextWidth ) * nextHeight * 4 );
		for ( uint32_t y = 0; y < nextHeight; ++y )
		{
			for ( uint32_t x = 0; x < nextWidth; ++x )
			{
				for ( uint32_t component = 0; component < 4; ++component )
				{
					uint32_t total = 0;
					for ( uint32_t dy = 0; dy < 2; ++dy )
					{
						const uint32_t sourceY = std::min( mipHeight - 1, y * 2 + dy );
						for ( uint32_t dx = 0; dx < 2; ++dx )
						{
							const uint32_t sourceX = std::min( mipWidth - 1, x * 2 + dx );
							total += uploadPixels[mipOffset +
								( static_cast<size_t>( sourceY ) * mipWidth + sourceX ) * 4 + component];
						}
					}
					uploadPixels[nextOffset +
						( static_cast<size_t>( y ) * nextWidth + x ) * 4 + component] =
						static_cast<byte>( total / 4 );
				}
			}
		}
		mipOffset = nextOffset;
		mipWidth = nextWidth;
		mipHeight = nextHeight;
	}
	const VkDeviceSize uploadSize = static_cast<VkDeviceSize>( uploadPixels.size() );
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
	std::memcpy( mapped, uploadPixels.data(), static_cast<size_t>( uploadSize ) );
	vkUnmapMemory( vk.device, stagingMemory );

	VkImageCreateInfo imageInfo = {};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.format = format;
	imageInfo.extent.width = width;
	imageInfo.extent.height = height;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = mipLevels;
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
		toTransfer.subresourceRange.levelCount = mipLevels;
		toTransfer.subresourceRange.layerCount = 1;
		vkCmdPipelineBarrier( vk.commandBuffer,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
			0, nullptr, 0, nullptr, 1, &toTransfer );

		vkCmdCopyBufferToImage( vk.commandBuffer, stagingBuffer, texture->image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			static_cast<uint32_t>( copyRegions.size() ), copyRegions.data() );

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
	viewInfo.subresourceRange.levelCount = mipLevels;
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
	texture->mipLevels = mipLevels;
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
	stage->tcScale[0] = 1.0f;
	stage->tcScale[1] = 1.0f;
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
				if ( depth == 2 && ( !stage.imageName.empty() || !stage.videoName.empty() ) )
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
			if ( depth == 1 && Q_stricmp( token, "polygonOffset" ) == 0 )
			{
				definition.polygonOffset = true;
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
				stage.videoName = COM_ParseExt( &text, qtrue );
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
					else if ( Q_stricmp( source.c_str(), "GL_ONE_MINUS_SRC_ALPHA" ) == 0 &&
						 Q_stricmp( destination.c_str(), "GL_ONE" ) == 0 )
					{
						stage.blendMode = VK_BLEND_INVERSE_SOURCE_ALPHA_ADDITIVE;
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
					else if ( Q_stricmp( source.c_str(), "GL_ZERO" ) == 0 &&
						 Q_stricmp( destination.c_str(), "GL_SRC_COLOR" ) == 0 )
					{
						// Source * destination is commutative for both RGB and alpha.
						stage.blendMode = VK_BLEND_MODULATE;
					}
					else if ( Q_stricmp( source.c_str(), "GL_DST_COLOR" ) == 0 &&
						 Q_stricmp( destination.c_str(), "GL_SRC_COLOR" ) == 0 )
					{
						stage.blendMode = VK_BLEND_DOUBLE_MODULATE;
					}
					else if ( Q_stricmp( source.c_str(), "GL_ZERO" ) == 0 &&
						 Q_stricmp( destination.c_str(), "GL_ONE_MINUS_SRC_COLOR" ) == 0 )
					{
						stage.blendMode = VK_BLEND_INVERSE_SOURCE_COLOR_MODULATE;
					}
					else if ( Q_stricmp( source.c_str(), "GL_ONE" ) == 0 &&
							 Q_stricmp( destination.c_str(), "GL_ONE_MINUS_SRC_COLOR" ) == 0 )
					{
						stage.blendMode = VK_BLEND_SCREEN;
					}
					else if ( Q_stricmp( source.c_str(), "GL_ONE_MINUS_DST_COLOR" ) == 0 &&
							 Q_stricmp( destination.c_str(), "GL_ONE" ) == 0 )
					{
						// Mathematically equivalent to GL_ONE, GL_ONE_MINUS_SRC_COLOR.
						stage.blendMode = VK_BLEND_SCREEN;
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
			else if ( Q_stricmp( token, "glow" ) == 0 )
			{
				stage.glow = true;
			}
			else if ( Q_stricmp( token, "detail" ) == 0 )
			{
				stage.detail = true;
			}
			else if ( Q_stricmp( token, "tcGen" ) == 0 )
			{
				stage.environmentMap =
					Q_stricmp( COM_ParseExt( &text, qtrue ), "environment" ) == 0;
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
				else if ( Q_stricmp( generator, "entity" ) == 0 )
				{
					stage.entityColor = true;
				}
				else if ( Q_stricmp( generator, "lightingDiffuse" ) == 0 )
				{
					stage.lightingDiffuse = true;
				}
				else if ( Q_stricmp( generator, "lightingDiffuseEntity" ) == 0 )
				{
					stage.lightingDiffuse = true;
					stage.lightingDiffuseEntity = true;
					stage.entityColor = true;
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
				else if ( Q_stricmp( operation, "scale" ) == 0 )
				{
					stage.tcScale[0] = static_cast<float>( std::atof( COM_ParseExt( &text, qtrue ) ) );
					stage.tcScale[1] = static_cast<float>( std::atof( COM_ParseExt( &text, qtrue ) ) );
				}
				else if ( Q_stricmp( operation, "rotate" ) == 0 )
				{
					stage.rotateSpeed = static_cast<float>( std::atof( COM_ParseExt( &text, qtrue ) ) );
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
	if ( Q_stricmp( name, "$whiteimage" ) == 0 || Q_stricmp( name, "*white" ) == 0 ||
		 Q_stricmp( name, "white" ) == 0 )
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
		&texture,
		VK_FORMAT_UNDEFINED,
		true );
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

static qhandle_t VK_CreateCinematicTexture( uint32_t width, uint32_t height )
{
	if ( width == 0 || height == 0 || width > 4096 || height > 4096 || vk.textures.size() >= 4096 )
	{
		return 2;
	}

	std::vector<byte> pixels( static_cast<size_t>( width ) * height * 4, 0 );
	vk_texture_t texture = {};
	if ( !VK_CreateTextureFromPixels( pixels.data(), width, height, &texture ) )
	{
		return 2;
	}

	const qhandle_t handle = static_cast<qhandle_t>( vk.textures.size() );
	vk.textures.push_back( texture );
	vk.materials.emplace_back();
	return handle;
}

static bool VK_UpdateTexturePixels(
	qhandle_t handle,
	uint32_t width,
	uint32_t height,
	const byte *pixels )
{
	if ( pixels == nullptr || handle <= 2 || static_cast<size_t>( handle ) >= vk.textures.size() )
	{
		return false;
	}
	vk_texture_t &texture = vk.textures[handle];
	if ( texture.width != width || texture.height != height )
	{
		return false;
	}

	const VkDeviceSize uploadSize = static_cast<VkDeviceSize>( width ) * height * 4;
	VkBuffer stagingBuffer = VK_NULL_HANDLE;
	VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
	if ( !VK_CreateBuffer(
		uploadSize,
		VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		&stagingBuffer,
		&stagingMemory,
		"cinematic staging" ) )
	{
		return false;
	}

	void *mapped = nullptr;
	bool uploaded = VK_CheckVk(
		vkMapMemory( vk.device, stagingMemory, 0, uploadSize, 0, &mapped ),
		"vkMapMemory(cinematic staging)" );
	if ( uploaded )
	{
		std::memcpy( mapped, pixels, static_cast<size_t>( uploadSize ) );
		vkUnmapMemory( vk.device, stagingMemory );
		uploaded = VK_CheckVk( vkQueueWaitIdle( vk.queue ), "vkQueueWaitIdle(cinematic upload)" ) &&
			VK_CheckVk( vkResetCommandPool( vk.device, vk.commandPool, 0 ),
				"vkResetCommandPool(cinematic upload)" );
	}

	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	if ( uploaded )
	{
		uploaded = VK_CheckVk(
			vkBeginCommandBuffer( vk.commandBuffer, &beginInfo ),
			"vkBeginCommandBuffer(cinematic upload)" );
	}
	if ( uploaded )
	{
		VkImageMemoryBarrier toTransfer = {};
		toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		toTransfer.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		toTransfer.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		toTransfer.image = texture.image;
		toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		toTransfer.subresourceRange.levelCount = 1;
		toTransfer.subresourceRange.layerCount = 1;
		vkCmdPipelineBarrier(
			vk.commandBuffer,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &toTransfer );

		VkBufferImageCopy copy = {};
		copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copy.imageSubresource.layerCount = 1;
		copy.imageExtent.width = width;
		copy.imageExtent.height = height;
		copy.imageExtent.depth = 1;
		vkCmdCopyBufferToImage(
			vk.commandBuffer,
			stagingBuffer,
			texture.image,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			1,
			&copy );

		VkImageMemoryBarrier toShaderRead = toTransfer;
		toShaderRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		toShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		toShaderRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		toShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		vkCmdPipelineBarrier(
			vk.commandBuffer,
			VK_PIPELINE_STAGE_TRANSFER_BIT,
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			0, 0, nullptr, 0, nullptr, 1, &toShaderRead );
		uploaded = VK_CheckVk(
			vkEndCommandBuffer( vk.commandBuffer ),
			"vkEndCommandBuffer(cinematic upload)" );
	}
	if ( uploaded )
	{
		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &vk.commandBuffer;
		uploaded = VK_CheckVk(
			vkQueueSubmit( vk.queue, 1, &submitInfo, VK_NULL_HANDLE ),
			"vkQueueSubmit(cinematic upload)" ) &&
			VK_CheckVk( vkQueueWaitIdle( vk.queue ), "vkQueueWaitIdle(cinematic upload complete)" );
	}

	VK_DestroyBuffer( &stagingBuffer, &stagingMemory );
	return uploaded;
}

static vk_cinematic_texture_t *VK_FindCinematicTexture(
	std::vector<vk_cinematic_texture_t> &cinematics,
	int client )
{
	auto found = std::find_if(
		cinematics.begin(), cinematics.end(),
		[client]( const vk_cinematic_texture_t &cinematic ) {
			return cinematic.client == client;
		} );
	return found == cinematics.end() ? nullptr : &*found;
}

static void VK_UpdateVideoMaps()
{
	std::vector<int> usedClients;
	usedClients.swap( vk.videoMapsUsed );
	if ( usedClients.empty() )
	{
		return;
	}

	// The legacy RoQ decoder owns one shared codebook and frame buffer. The
	// frontmost submitted video wins when a scene contains parked alternatives.
	const int client = usedClients.back();
	vk_cinematic_texture_t *cinematic = VK_FindCinematicTexture( vk.videoMaps, client );
	if ( cinematic == nullptr )
	{
		return;
	}
	const e_status status = ri.CIN_RunCinematic( client );
	++cinematic->runCount;
	ri.CIN_UploadCinematic( client );
	if ( cinematic->runCount <= 3 || ( cinematic->runCount % 60 ) == 0 )
	{
		const cvar_t *inGameVideo = ri.Cvar_Get( "cl_inGameVideo", "1", 0 );
		ri.Printf( PRINT_ALL,
			"rd-vulkan: videoMap client=%d run=%u uploads=%u status=%d enabled=%d candidates=%zu\n",
			client, cinematic->runCount, cinematic->uploadCount,
			static_cast<int>( status ), inGameVideo != nullptr ? inGameVideo->integer : -1,
			usedClients.size() );
	}
}

static void VK_MarkVideoMapUsed( int client )
{
	if ( client < 0 || std::find( vk.videoMapsUsed.begin(), vk.videoMapsUsed.end(), client ) !=
		 vk.videoMapsUsed.end() )
	{
		return;
	}
	vk.videoMapsUsed.push_back( client );
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

static bool VK_CreateControllerAction(
	XrActionType type, const char *name, const char *localizedName, XrAction *action )
{
	XrActionCreateInfo createInfo = {};
	createInfo.type = XR_TYPE_ACTION_CREATE_INFO;
	createInfo.actionType = type;
	createInfo.countSubactionPaths = VK_BACKEND_EYE_COUNT;
	createInfo.subactionPaths = vk.handPaths;
	std::snprintf( createInfo.actionName, sizeof( createInfo.actionName ), "%s", name );
	std::snprintf( createInfo.localizedActionName, sizeof( createInfo.localizedActionName ), "%s", localizedName );
	return VK_CheckXr(
		xrCreateAction( vk.controllerActionSet, &createInfo, action ), name );
}

static bool VK_AddControllerBinding(
	std::vector<XrActionSuggestedBinding> *bindings, XrAction action, const char *pathName )
{
	XrPath path = XR_NULL_PATH;
	if ( !VK_CheckXr( xrStringToPath( vk.xrInstance, pathName, &path ), pathName ) )
	{
		return false;
	}
	bindings->push_back( { action, path } );
	return true;
}

static void VK_SuggestControllerBindings(
	const char *profileName, const std::vector<XrActionSuggestedBinding> &bindings )
{
	XrPath profile = XR_NULL_PATH;
	if ( !VK_CheckXr( xrStringToPath( vk.xrInstance, profileName, &profile ), profileName ) )
	{
		return;
	}

	XrInteractionProfileSuggestedBinding suggestion = {};
	suggestion.type = XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING;
	suggestion.interactionProfile = profile;
	suggestion.countSuggestedBindings = static_cast<uint32_t>( bindings.size() );
	suggestion.suggestedBindings = bindings.data();
	const XrResult result = xrSuggestInteractionProfileBindings( vk.xrInstance, &suggestion );
	if ( XR_FAILED( result ) )
	{
		VK_LogXrFailure( profileName, result );
	}
}

static bool VK_CreateControllerActions()
{
	XrActionSetCreateInfo setInfo = {};
	setInfo.type = XR_TYPE_ACTION_SET_CREATE_INFO;
	std::snprintf( setInfo.actionSetName, sizeof( setInfo.actionSetName ), "jkxr_gameplay" );
	std::snprintf( setInfo.localizedActionSetName, sizeof( setInfo.localizedActionSetName ), "JKXR Gameplay" );
	if ( !VK_CheckXr(
			xrCreateActionSet( vk.xrInstance, &setInfo, &vk.controllerActionSet ),
			"xrCreateActionSet(JKXR gameplay)" ) ||
		 !VK_CheckXr( xrStringToPath( vk.xrInstance, "/user/hand/left", &vk.handPaths[0] ),
			"xrStringToPath(left hand)" ) ||
		 !VK_CheckXr( xrStringToPath( vk.xrInstance, "/user/hand/right", &vk.handPaths[1] ),
			"xrStringToPath(right hand)" ) )
	{
		return false;
	}

	if ( !VK_CreateControllerAction( XR_ACTION_TYPE_POSE_INPUT, "aim_pose", "Aim Pose", &vk.aimPoseAction ) ||
		 !VK_CreateControllerAction( XR_ACTION_TYPE_POSE_INPUT, "grip_pose", "Grip Pose", &vk.gripPoseAction ) ||
		 !VK_CreateControllerAction( XR_ACTION_TYPE_FLOAT_INPUT, "trigger", "Trigger", &vk.triggerAction ) ||
		 !VK_CreateControllerAction( XR_ACTION_TYPE_BOOLEAN_INPUT, "trigger_click", "Trigger Click", &vk.triggerClickAction ) ||
		 !VK_CreateControllerAction( XR_ACTION_TYPE_BOOLEAN_INPUT, "trigger_touch", "Trigger Touch", &vk.triggerTouchAction ) ||
		 !VK_CreateControllerAction( XR_ACTION_TYPE_FLOAT_INPUT, "squeeze", "Squeeze", &vk.squeezeAction ) ||
		 !VK_CreateControllerAction( XR_ACTION_TYPE_VECTOR2F_INPUT, "thumbstick", "Thumbstick", &vk.thumbstickAction ) ||
		 !VK_CreateControllerAction( XR_ACTION_TYPE_BOOLEAN_INPUT, "thumbstick_click", "Thumbstick Click", &vk.thumbstickClickAction ) ||
		 !VK_CreateControllerAction( XR_ACTION_TYPE_BOOLEAN_INPUT, "thumbstick_touch", "Thumbstick Touch", &vk.thumbstickTouchAction ) ||
		 !VK_CreateControllerAction( XR_ACTION_TYPE_BOOLEAN_INPUT, "primary_button", "Primary Button", &vk.primaryButtonAction ) ||
		 !VK_CreateControllerAction( XR_ACTION_TYPE_BOOLEAN_INPUT, "secondary_button", "Secondary Button", &vk.secondaryButtonAction ) ||
		 !VK_CreateControllerAction( XR_ACTION_TYPE_BOOLEAN_INPUT, "primary_touch", "Primary Touch", &vk.primaryTouchAction ) ||
		 !VK_CreateControllerAction( XR_ACTION_TYPE_BOOLEAN_INPUT, "secondary_touch", "Secondary Touch", &vk.secondaryTouchAction ) ||
		 !VK_CreateControllerAction( XR_ACTION_TYPE_BOOLEAN_INPUT, "thumbrest_touch", "Thumbrest Touch", &vk.thumbrestTouchAction ) ||
		 !VK_CreateControllerAction( XR_ACTION_TYPE_BOOLEAN_INPUT, "menu", "Menu", &vk.menuAction ) )
	{
		return false;
	}

	std::vector<XrActionSuggestedBinding> touch;
	const struct {
		XrAction action;
		const char *path;
	} touchBindings[] = {
		{ vk.aimPoseAction, "/user/hand/left/input/aim/pose" },
		{ vk.aimPoseAction, "/user/hand/right/input/aim/pose" },
		{ vk.gripPoseAction, "/user/hand/left/input/grip/pose" },
		{ vk.gripPoseAction, "/user/hand/right/input/grip/pose" },
		{ vk.triggerAction, "/user/hand/left/input/trigger/value" },
		{ vk.triggerAction, "/user/hand/right/input/trigger/value" },
		{ vk.triggerTouchAction, "/user/hand/left/input/trigger/touch" },
		{ vk.triggerTouchAction, "/user/hand/right/input/trigger/touch" },
		{ vk.squeezeAction, "/user/hand/left/input/squeeze/value" },
		{ vk.squeezeAction, "/user/hand/right/input/squeeze/value" },
		{ vk.thumbstickAction, "/user/hand/left/input/thumbstick" },
		{ vk.thumbstickAction, "/user/hand/right/input/thumbstick" },
		{ vk.thumbstickClickAction, "/user/hand/left/input/thumbstick/click" },
		{ vk.thumbstickClickAction, "/user/hand/right/input/thumbstick/click" },
		{ vk.thumbstickTouchAction, "/user/hand/left/input/thumbstick/touch" },
		{ vk.thumbstickTouchAction, "/user/hand/right/input/thumbstick/touch" },
		{ vk.primaryButtonAction, "/user/hand/left/input/x/click" },
		{ vk.primaryButtonAction, "/user/hand/right/input/a/click" },
		{ vk.secondaryButtonAction, "/user/hand/left/input/y/click" },
		{ vk.secondaryButtonAction, "/user/hand/right/input/b/click" },
		{ vk.primaryTouchAction, "/user/hand/left/input/x/touch" },
		{ vk.primaryTouchAction, "/user/hand/right/input/a/touch" },
		{ vk.secondaryTouchAction, "/user/hand/left/input/y/touch" },
		{ vk.secondaryTouchAction, "/user/hand/right/input/b/touch" },
		{ vk.thumbrestTouchAction, "/user/hand/left/input/thumbrest/touch" },
		{ vk.thumbrestTouchAction, "/user/hand/right/input/thumbrest/touch" },
		{ vk.menuAction, "/user/hand/left/input/menu/click" },
	};
	for ( const auto &binding : touchBindings )
	{
		if ( !VK_AddControllerBinding( &touch, binding.action, binding.path ) )
		{
			return false;
		}
	}
	VK_SuggestControllerBindings( "/interaction_profiles/oculus/touch_controller", touch );

	std::vector<XrActionSuggestedBinding> simple;
	const struct {
		XrAction action;
		const char *path;
	} simpleBindings[] = {
		{ vk.aimPoseAction, "/user/hand/left/input/aim/pose" },
		{ vk.aimPoseAction, "/user/hand/right/input/aim/pose" },
		{ vk.gripPoseAction, "/user/hand/left/input/grip/pose" },
		{ vk.gripPoseAction, "/user/hand/right/input/grip/pose" },
		{ vk.triggerClickAction, "/user/hand/left/input/select/click" },
		{ vk.triggerClickAction, "/user/hand/right/input/select/click" },
		{ vk.menuAction, "/user/hand/left/input/menu/click" },
		{ vk.menuAction, "/user/hand/right/input/menu/click" },
	};
	for ( const auto &binding : simpleBindings )
	{
		if ( !VK_AddControllerBinding( &simple, binding.action, binding.path ) )
		{
			return false;
		}
	}
	VK_SuggestControllerBindings( "/interaction_profiles/khr/simple_controller", simple );

	for ( int hand = 0; hand < VK_BACKEND_EYE_COUNT; ++hand )
	{
		XrActionSpaceCreateInfo spaceInfo = {};
		spaceInfo.type = XR_TYPE_ACTION_SPACE_CREATE_INFO;
		spaceInfo.poseInActionSpace.orientation.w = 1.0f;
		spaceInfo.subactionPath = vk.handPaths[hand];
		spaceInfo.action = vk.aimPoseAction;
		if ( !VK_CheckXr(
				xrCreateActionSpace( vk.xrSession, &spaceInfo, &vk.aimSpaces[hand] ),
				"xrCreateActionSpace(aim)" ) )
		{
			return false;
		}
		spaceInfo.action = vk.gripPoseAction;
		if ( !VK_CheckXr(
				xrCreateActionSpace( vk.xrSession, &spaceInfo, &vk.gripSpaces[hand] ),
				"xrCreateActionSpace(grip)" ) )
		{
			return false;
		}
	}

	XrSessionActionSetsAttachInfo attachInfo = {};
	attachInfo.type = XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO;
	attachInfo.countActionSets = 1;
	attachInfo.actionSets = &vk.controllerActionSet;
	if ( !VK_CheckXr(
			xrAttachSessionActionSets( vk.xrSession, &attachInfo ),
			"xrAttachSessionActionSets" ) )
	{
		return false;
	}

	ri.Printf( PRINT_ALL, "rd-vulkan: OpenXR tracked-controller actions attached\n" );
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
			vk.legacyColorActive = vk.legacyColorCvar != nullptr &&
				vk.legacyColorCvar->integer != 0;
			vk.colorRenderFormat = static_cast<VkFormat>( vk.colorFormat );
			if ( vk.legacyColorActive )
			{
				if ( vk.colorFormat == VK_FORMAT_R8G8B8A8_SRGB )
				{
					vk.colorRenderFormat = VK_FORMAT_R8G8B8A8_UNORM;
				}
				else if ( vk.colorFormat == VK_FORMAT_B8G8R8A8_SRGB )
				{
					vk.colorRenderFormat = VK_FORMAT_B8G8R8A8_UNORM;
				}
				else
				{
					vk.legacyColorActive = false;
				}
			}
			ri.Printf( PRINT_ALL,
				"rd-vulkan: color mode=%s textureFormat=%d swapchainFormat=%lld renderViewFormat=%d\n",
				vk.legacyColorActive ? "jkxr-legacy-byte-space" : "srgb-linear",
				static_cast<int>( vk.legacyColorActive
					? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_SRGB ),
				static_cast<long long>( vk.colorFormat ),
				static_cast<int>( vk.colorRenderFormat ) );
			return true;
		}
	}

	vk.colorFormat = formats[0];
	vk.colorRenderFormat = static_cast<VkFormat>( vk.colorFormat );
	vk.legacyColorActive = false;
	ri.Printf( PRINT_WARNING, "rd-vulkan: using first runtime swapchain format %lld\n",
		static_cast<long long>( vk.colorFormat ) );
	return true;
}

static bool VK_CreateEyeSwapchain( int eye )
{
	XrSwapchainCreateInfo createInfo = {};
	createInfo.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
	createInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
	if ( vk.legacyColorActive )
	{
		createInfo.usageFlags |= XR_SWAPCHAIN_USAGE_MUTABLE_FORMAT_BIT;
	}
	createInfo.format = vk.colorFormat;
	createInfo.sampleCount = 1;
	createInfo.width = vk.viewConfiguration[eye].recommendedImageRectWidth;
	createInfo.height = vk.viewConfiguration[eye].recommendedImageRectHeight;
	createInfo.faceCount = 1;
	createInfo.arraySize = 1;
	createInfo.mipCount = 1;

	XrResult createResult = xrCreateSwapchain( vk.xrSession, &createInfo, &vk.colorSwapchain[eye] );
	if ( XR_FAILED( createResult ) && vk.legacyColorActive && eye == 0 )
	{
		ri.Printf( PRINT_WARNING,
			"rd-vulkan: runtime rejected mutable legacy-color swapchain; reverting to sRGB-linear rendering\n" );
		vk.legacyColorActive = false;
		vk.colorRenderFormat = static_cast<VkFormat>( vk.colorFormat );
		createInfo.usageFlags &= ~XR_SWAPCHAIN_USAGE_MUTABLE_FORMAT_BIT;
		createResult = xrCreateSwapchain( vk.xrSession, &createInfo, &vk.colorSwapchain[eye] );
	}
	if ( !VK_CheckXr( createResult, "xrCreateSwapchain" ) )
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
	attachments[0].format = vk.colorRenderFormat;
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
	bool worldVertexInput = false,
	VkCullModeFlags cullMode = VK_CULL_MODE_NONE )
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
	VkVertexInputAttributeDescription vertexAttributes[5] = {};
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

		vertexAttributes[4].location = 4;
		vertexAttributes[4].binding = 0;
		vertexAttributes[4].format = VK_FORMAT_R32G32B32_SFLOAT;
		vertexAttributes[4].offset = offsetof( vk_world_vertex_t, normal );

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
	rasterization.cullMode = cullMode;
	rasterization.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterization.lineWidth = 1.0f;
	rasterization.depthBiasEnable = worldVertexInput ? VK_TRUE : VK_FALSE;

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
		VK_DYNAMIC_STATE_DEPTH_BIAS,
	};
	VkPipelineDynamicStateCreateInfo dynamicState = {};
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount = ARRAY_LEN( dynamicStates );
	dynamicState.pDynamicStates = dynamicStates;

	VkPushConstantRange pushConstant = {};
	pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	pushConstant.offset = 0;
	pushConstant.size = sizeof( float ) * 32;

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
			&vk.texturedRectSourceAlphaAdditivePipeline,
			"vkCreateGraphicsPipelines(textured-rect-source-alpha-additive)",
			VK_BLEND_SOURCE_ALPHA_ADDITIVE ) &&
		VK_CreatePipeline( texturedRectVertSpv, ARRAY_LEN( texturedRectVertSpv ),
			texturedRectFragSpv, ARRAY_LEN( texturedRectFragSpv ),
			&vk.texturedRectInverseSourceAlphaAdditivePipeline,
			"vkCreateGraphicsPipelines(textured-rect-inverse-source-alpha-additive)",
			VK_BLEND_INVERSE_SOURCE_ALPHA_ADDITIVE ) &&
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
		VK_CreatePipeline( texturedRectVertSpv, ARRAY_LEN( texturedRectVertSpv ),
			texturedRectFragSpv, ARRAY_LEN( texturedRectFragSpv ),
			&vk.texturedRectDoubleModulatePipeline,
			"vkCreateGraphicsPipelines(textured-rect-double-modulate)", VK_BLEND_DOUBLE_MODULATE ) &&
		VK_CreatePipeline( texturedRectVertSpv, ARRAY_LEN( texturedRectVertSpv ),
			texturedRectFragSpv, ARRAY_LEN( texturedRectFragSpv ),
			&vk.texturedRectInverseSourceColorModulatePipeline,
			"vkCreateGraphicsPipelines(textured-rect-inverse-source-color-modulate)",
			VK_BLEND_INVERSE_SOURCE_COLOR_MODULATE ) &&
		VK_CreatePipeline( texturedRectVertSpv, ARRAY_LEN( texturedRectVertSpv ),
			texturedRectFragSpv, ARRAY_LEN( texturedRectFragSpv ),
			&vk.texturedRectScreenPipeline,
			"vkCreateGraphicsPipelines(textured-rect-screen)", VK_BLEND_SCREEN ) &&
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
			&vk.worldBackCullPipeline, "vkCreateGraphicsPipelines(world-back-cull)",
			VK_BLEND_OPAQUE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true, true, true,
			VK_CULL_MODE_BACK_BIT ) &&
		VK_CreatePipeline( worldVertSpv, ARRAY_LEN( worldVertSpv ),
			worldFragSpv, ARRAY_LEN( worldFragSpv ),
			&vk.worldFrontCullPipeline, "vkCreateGraphicsPipelines(world-front-cull)",
			VK_BLEND_OPAQUE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true, true, true,
			VK_CULL_MODE_FRONT_BIT ) &&
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
			&vk.worldInverseSourceAlphaAdditivePipeline,
			"vkCreateGraphicsPipelines(world-inverse-source-alpha-additive)",
			VK_BLEND_INVERSE_SOURCE_ALPHA_ADDITIVE,
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
			VK_BLEND_DOUBLE_MODULATE, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true, false, true ) &&
		VK_CreatePipeline( worldVertSpv, ARRAY_LEN( worldVertSpv ),
			worldFragSpv, ARRAY_LEN( worldFragSpv ),
			&vk.worldInverseSourceColorModulatePipeline,
			"vkCreateGraphicsPipelines(world-inverse-source-color-modulate)",
			VK_BLEND_INVERSE_SOURCE_COLOR_MODULATE,
			VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true, false, true ) &&
		VK_CreatePipeline( worldVertSpv, ARRAY_LEN( worldVertSpv ),
			worldFragSpv, ARRAY_LEN( worldFragSpv ),
			&vk.worldScreenPipeline,
			"vkCreateGraphicsPipelines(world-screen)", VK_BLEND_SCREEN,
			VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, true, false, true );
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
		viewInfo.format = vk.colorRenderFormat;
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

static void VK_BuildProjectionMatrix(
	const XrFovf &fov,
	float zNear,
	float zFar,
	float matrix[16],
	float tangentScale = 1.0f )
{
	const float tanLeft = std::tan( fov.angleLeft ) * tangentScale;
	const float tanRight = std::tan( fov.angleRight ) * tangentScale;
	const float tanDown = std::tan( fov.angleDown ) * tangentScale;
	const float tanUp = std::tan( fov.angleUp ) * tangentScale;
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
static bool VK_BatchBelongsToStaticWorld( const vk_world_batch_t &batch )
{
	if ( vk.world.inlineModels.empty() )
	{
		return true;
	}
	const vk_world_inline_model_t &worldModel = vk.world.inlineModels.front();
	return batch.surfaceIndex >= worldModel.firstSurface &&
		batch.surfaceIndex - worldModel.firstSurface < worldModel.surfaceCount;
}

static bool VK_DynamicLightIntersectsBatch(
	const vk_dynamic_light_t &light,
	const vk_world_batch_t &batch );
static qhandle_t VK_DynamicLightSurfaceTexture(
	qhandle_t shader,
	vk_alpha_test_t *alphaTest );
static void VK_PushWorldDynamicLight(
	const vk_dynamic_light_t &light,
	vk_alpha_test_t alphaTest );

static bool VK_TextureUsesClamp( qhandle_t texture )
{
	return vk.clampTextureHandles.find( texture ) != vk.clampTextureHandles.end();
}

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
	case VK_BLEND_INVERSE_SOURCE_ALPHA_ADDITIVE:
		return vk.worldInverseSourceAlphaAdditivePipeline;
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
	case VK_BLEND_INVERSE_SOURCE_COLOR_MODULATE:
		return vk.worldInverseSourceColorModulatePipeline;
	case VK_BLEND_SCREEN:
		return vk.worldScreenPipeline;
	case VK_BLEND_OPAQUE:
	default:
		return vk.worldPipeline;
	}
}

static const char *VK_BlendModeName( vk_blend_mode_t blendMode )
{
	switch ( blendMode )
	{
	case VK_BLEND_ALPHA: return "alpha";
	case VK_BLEND_OPAQUE: return "opaque";
	case VK_BLEND_ADDITIVE: return "add";
	case VK_BLEND_SOURCE_ALPHA_ADDITIVE: return "src-alpha-add";
	case VK_BLEND_INVERSE_SOURCE_ALPHA_ADDITIVE: return "inv-src-alpha-add";
	case VK_BLEND_ONE_SOURCE_ALPHA: return "one-src-alpha";
	case VK_BLEND_DESTINATION_COLOR_ADDITIVE: return "dst-color-add";
	case VK_BLEND_ONE_MINUS_DESTINATION_ALPHA_ADDITIVE: return "inv-dst-alpha-add";
	case VK_BLEND_MODULATE: return "modulate";
	case VK_BLEND_DOUBLE_MODULATE: return "double-modulate";
	case VK_BLEND_INVERSE_SOURCE_COLOR_MODULATE: return "inv-src-color";
	case VK_BLEND_SCREEN: return "screen";
	default: return "unknown";
	}
}

static int VK_MaterialAuditTarget( qhandle_t shader, const char **name )
{
	for ( const vk_texture_name_t &registered : vk.textureNames )
	{
		if ( registered.handle != shader )
		{
			continue;
		}
		if ( Q_stricmp( registered.name.c_str(), "textures/h_evil/lakewater" ) == 0 )
		{
			*name = registered.name.c_str();
			return 0;
		}
		if ( Q_stricmp( registered.name.c_str(), "textures/common/Water_Yavin2" ) == 0 )
		{
			*name = registered.name.c_str();
			return 1;
		}
	}
	return -1;
}

static bool VK_ShaderIsYavinRiver( qhandle_t shader )
{
	return shader > 0 && static_cast<size_t>( shader ) < vk.materials.size() &&
		std::any_of(
			vk.materials[shader].stages.begin(), vk.materials[shader].stages.end(),
			[]( const vk_material_stage_t &stage ) { return stage.yavinRiverStage != 0; } );
}

static bool VK_WorldIndirectBatchesMatch(
	const vk_world_batch_t &left,
	const vk_world_batch_t &right )
{
	return left.shader == right.shader &&
		left.surfaceFlags == right.surfaceFlags &&
		left.vertexLit == right.vertexLit &&
		std::equal(
			std::begin( left.lightmaps ), std::end( left.lightmaps ),
			std::begin( right.lightmaps ) ) &&
		std::equal(
			std::begin( left.lightmapStyles ), std::end( left.lightmapStyles ),
			std::begin( right.lightmapStyles ) ) &&
		std::equal(
			std::begin( left.vertexStyles ), std::end( left.vertexStyles ),
			std::begin( right.vertexStyles ) );
}

static bool VK_WorldBatchBelongsToRoot(
	const vk_world_geometry_t &world,
	const vk_world_batch_t &batch )
{
	if ( world.inlineModels.empty() )
	{
		return true;
	}
	const vk_world_inline_model_t &root = world.inlineModels.front();
	return batch.surfaceIndex >= root.firstSurface &&
		batch.surfaceIndex - root.firstSurface < root.surfaceCount;
}

static bool VK_CreateWorldIndirectBatches( vk_world_geometry_t *world )
{
	if ( world == nullptr || !vk.multiDrawIndirect || world->batches.empty() )
	{
		return false;
	}

	for ( uint32_t batchIndex = 0; batchIndex < world->batches.size(); ++batchIndex )
	{
		const vk_world_batch_t &batch = world->batches[batchIndex];
		if ( !VK_WorldBatchBelongsToRoot( *world, batch ) ||
			 VK_ShaderIsYavinRiver( batch.shader ) )
		{
			continue;
		}

		auto group = std::find_if(
			world->indirectGroups.begin(), world->indirectGroups.end(),
			[&]( const vk_world_indirect_group_t &candidate )
			{
				return VK_WorldIndirectBatchesMatch( candidate.representative, batch );
			} );
		if ( group == world->indirectGroups.end() )
		{
			vk_world_indirect_group_t newGroup = {};
			newGroup.representative = batch;
			world->indirectGroups.push_back( std::move( newGroup ) );
			group = std::prev( world->indirectGroups.end() );
		}
		group->batchIndices.push_back( batchIndex );
	}

	uint32_t commandCount = 0;
	uint32_t largestGroup = 0;
	for ( vk_world_indirect_group_t &group : world->indirectGroups )
	{
		group.commandFirst = commandCount;
		commandCount += static_cast<uint32_t>( group.batchIndices.size() );
		largestGroup = std::max(
			largestGroup, static_cast<uint32_t>( group.batchIndices.size() ) );
	}
	if ( commandCount == 0 )
	{
		world->indirectGroups.clear();
		return false;
	}

	const VkDeviceSize commandBytes =
		static_cast<VkDeviceSize>( commandCount ) *
		2 * sizeof( VkDrawIndexedIndirectCommand );
	if ( !VK_CreateBuffer(
			commandBytes,
			VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&world->indirectBuffer,
			&world->indirectMemory,
			"world indirect" ) )
	{
		world->indirectGroups.clear();
		return false;
	}

	void *mapped = nullptr;
	if ( !VK_CheckVk(
			vkMapMemory( vk.device, world->indirectMemory, 0, commandBytes, 0, &mapped ),
			"vkMapMemory(world indirect)" ) )
	{
		VK_DestroyBuffer( &world->indirectBuffer, &world->indirectMemory );
		world->indirectGroups.clear();
		return false;
	}
	world->indirectMapped = static_cast<VkDrawIndexedIndirectCommand *>( mapped );
	world->indirectCommandCount = commandCount;
	world->indirectFrameIndex[0] = std::numeric_limits<uint64_t>::max();
	world->indirectFrameIndex[1] = std::numeric_limits<uint64_t>::max();
	for ( int slot = 0; slot < 2; ++slot )
	{
		world->indirectVisibleGroupCounts[slot].assign( world->indirectGroups.size(), 0 );
		VkDrawIndexedIndirectCommand *commands =
			world->indirectMapped + static_cast<size_t>( slot ) * commandCount;
		for ( const vk_world_indirect_group_t &group : world->indirectGroups )
		{
			for ( size_t i = 0; i < group.batchIndices.size(); ++i )
			{
				const vk_world_batch_t &batch = world->batches[group.batchIndices[i]];
				VkDrawIndexedIndirectCommand &command = commands[group.commandFirst + i];
				command.indexCount = batch.indexCount;
				command.instanceCount = 0;
				command.firstIndex = batch.firstIndex;
				command.vertexOffset = 0;
				command.firstInstance = 0;
			}
		}
	}

	ri.Printf( PRINT_ALL,
		"rd-vulkan-world: indirect BSP submission groups=%zu commands=%u largest=%u slots=2\n",
		world->indirectGroups.size(), commandCount, largestGroup );
	return true;
}

static void VK_UpdateWorldIndirectVisibility(
	int slot,
	const std::vector<byte> *visibleSurfaces )
{
	if ( slot < 0 || slot >= 2 || vk.world.indirectMapped == nullptr ||
		 vk.world.indirectCommandCount == 0 ||
		 vk.world.indirectFrameIndex[slot] == vk.frameIndex )
	{
		return;
	}

	VkDrawIndexedIndirectCommand *commands =
		vk.world.indirectMapped +
		static_cast<size_t>( slot ) * vk.world.indirectCommandCount;
	std::vector<uint32_t> &visibleCounts = vk.world.indirectVisibleGroupCounts[slot];
	for ( size_t groupIndex = 0; groupIndex < vk.world.indirectGroups.size(); ++groupIndex )
	{
		const vk_world_indirect_group_t &group = vk.world.indirectGroups[groupIndex];
		uint32_t visibleCount = 0;
		for ( size_t i = 0; i < group.batchIndices.size(); ++i )
		{
			const vk_world_batch_t &batch = vk.world.batches[group.batchIndices[i]];
			const bool forceSightVisible =
				( batch.surfaceFlags & SURF_FORCESIGHT ) == 0 ||
				( vk.worldRefdef.rdflags & RDF_ForceSightOn ) != 0;
			const bool pvsVisible = visibleSurfaces == nullptr ||
				( batch.surfaceIndex < visibleSurfaces->size() &&
				  ( *visibleSurfaces )[batch.surfaceIndex] != 0 );
			const bool visible = forceSightVisible && pvsVisible;
			commands[group.commandFirst + i].instanceCount = visible ? 1 : 0;
			visibleCount += visible ? 1u : 0u;
		}
		visibleCounts[groupIndex] = visibleCount;
	}
	vk.world.indirectFrameIndex[slot] = vk.frameIndex;
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

static void VK_BindWorldPipelineOverride(
	vk_blend_mode_t blendMode,
	VkPipeline opaquePipelineOverride,
	VkPipeline *boundPipeline,
	bool depthWrite = false )
{
	const VkPipeline pipeline = blendMode == VK_BLEND_OPAQUE &&
		opaquePipelineOverride != VK_NULL_HANDLE
		? opaquePipelineOverride
		: VK_WorldPipelineForBlend( blendMode, depthWrite );
	if ( pipeline != VK_NULL_HANDLE && *boundPipeline != pipeline )
	{
		*boundPipeline = pipeline;
		vkCmdBindPipeline( vk.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
	}
}

static void VK_SetWorldDepthBias( bool polygonOffset )
{
	if ( vk.depthBiasStateKnown && vk.depthBiasEnabled == polygonOffset )
	{
		return;
	}

	vk.depthBiasStateKnown = true;
	vk.depthBiasEnabled = polygonOffset;
	const float factor = polygonOffset && vk.offsetFactorCvar != nullptr
		? vk.offsetFactorCvar->value : 0.0f;
	const float units = polygonOffset && vk.offsetUnitsCvar != nullptr
		? vk.offsetUnitsCvar->value : 0.0f;
	vkCmdSetDepthBias( vk.commandBuffer, units, 0.0f, factor );
	if ( polygonOffset )
	{
		static bool loggedPolygonOffset = false;
		if ( !loggedPolygonOffset )
		{
			ri.Printf( PRINT_ALL,
				"rd-vulkan-material: polygon offset active factor=%.2f units=%.2f\n",
				factor, units );
			loggedPolygonOffset = true;
		}
	}
}

static void VK_PushWorldStage(
	const vk_material_stage_t *stage,
	bool useLightmap,
	const byte *entityColor = nullptr,
	int lightmapSlot = 0,
	int vertexColorSlot = 0,
	const byte *styleColor = nullptr,
	const float *diffuseColor = nullptr )
{
	vk_world_stage_push_t push = {};
	push.alpha = 1.0f;
	push.useLightmap = stage != nullptr && stage->environmentMap
		? 2.0f
		: ( useLightmap ? 1.0f : 0.0f );
	push.uvScale[0] = 1.0f;
	push.uvScale[1] = 1.0f;
	push.lightmapGamma = useLightmap && vk.lightmapGammaCvar != nullptr
		? VK_ClampValue( vk.lightmapGammaCvar->value, 0.5f, 3.0f )
		: 1.0f;
	if ( useLightmap && stage != nullptr && stage->yavinRiverStage == 4 )
	{
		push.lightmapGamma = vk.yavinRiverLightmapGammaCvar != nullptr
			? VK_ClampValue( vk.yavinRiverLightmapGammaCvar->value, 0.5f, 3.0f )
			: 1.0f;
	}
	push.color[0] = 1.0f;
	push.color[1] = 1.0f;
	push.color[2] = 1.0f;
	push.color[3] = 1.0f;
	push.flags[0] = stage == nullptr ? 1.0f : 0.0f;
	if ( stage != nullptr )
	{
		if ( stage->videoMap )
		{
			VK_MarkVideoMapUsed( stage->videoHandle );
		}
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
		if ( stage->yavinRiverStage >= 1 && stage->yavinRiverStage <= 3 )
		{
			const float opacityScale = vk.yavinRiverOpacityCvar != nullptr
				? VK_ClampValue( vk.yavinRiverOpacityCvar->value, 0.0f, 2.0f )
				: 1.35f;
			// Keep values above one: the fragment shader applies this to the
			// sampled PNG alpha, where it strengthens the authored pale water
			// layers instead of saturating the stage constant prematurely.
			push.alpha *= opacityScale;
		}
		if ( stage->yavinWaterBase )
		{
			// GL_ONE, GL_SRC_ALPHA preserves more of the background as this value rises.
			const float templeTransparency = vk.yavinWaterTransparencyCvar != nullptr
				? VK_ClampValue( vk.yavinWaterTransparencyCvar->value, 0.0f, 1.0f )
				: 0.35f;
			// The river approach occupies negative Y; the temple pool is reached
			// after crossing into the positive-Y half of yavin1/yavin1b.
			const float templeRegion = VK_ClampValue(
				( vk.worldRefdef.vieworg[1] - 512.0f ) / 1536.0f, 0.0f, 1.0f );
			push.alpha = stage->alpha +
				( templeTransparency - stage->alpha ) * templeRegion;
		}
		std::memcpy( push.color, stage->color, sizeof( push.color ) );
		if ( stage->glow )
		{
			const float glowIntensity = vk.glowIntensityCvar != nullptr
				? std::max( 0.0f, std::min( 3.0f, vk.glowIntensityCvar->value ) )
				: 1.45f;
			for ( int component = 0; component < 3; ++component )
			{
				push.color[component] *= glowIntensity;
			}
		}
		if ( stage->effectBoost )
		{
			const float effectIntensity = vk.waterEffectIntensityCvar != nullptr
				? std::max( 0.0f, std::min( 3.0f,
					vk.waterEffectIntensityCvar->value ) )
				: 1.35f;
			for ( int component = 0; component < 3; ++component )
			{
				push.color[component] *= effectIntensity;
			}
		}
		if ( stage->yavinWaterDetail )
		{
			const float detailIntensity = vk.yavinWaterDetailIntensityCvar != nullptr
				? VK_ClampValue( vk.yavinWaterDetailIntensityCvar->value, 0.0f, 4.0f )
				: 1.0f;
			for ( int component = 0; component < 3; ++component )
			{
				push.color[component] *= detailIntensity;
			}
		}
		if ( stage->waterWake )
		{
			const float wakeIntensity = vk.waterWakeIntensityCvar != nullptr
				? VK_ClampValue( vk.waterWakeIntensityCvar->value, 0.0f, 4.0f )
				: 1.0f;
			for ( int component = 0; component < 3; ++component )
			{
				push.color[component] *= wakeIntensity;
			}
		}
		if ( stage->entityColor && entityColor != nullptr )
		{
			for ( int component = 0; component < 4; ++component )
			{
				push.color[component] *= entityColor[component] / 255.0f;
			}
		}
		push.flags[0] = stage->vertexColor && !useLightmap ? 1.0f : 0.0f;
		push.flags[1] = stage->turbulence[0];
		push.flags[2] = stage->turbulence[1] + seconds * stage->turbulence[2];
		push.flags[3] = static_cast<float>( stage->alphaTest );
	}
	if ( styleColor != nullptr )
	{
		for ( int component = 0; component < 4; ++component )
		{
			push.color[component] *= styleColor[component] / 255.0f;
		}
	}
	if ( stage != nullptr && stage->lightingDiffuse && diffuseColor != nullptr )
	{
		for ( int component = 0; component < 3; ++component )
		{
			push.color[component] *= diffuseColor[component];
		}
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

static uint32_t VK_RecordIndexedCommands(
	uint32_t indexCount,
	uint32_t firstIndex,
	VkBuffer indirectBuffer,
	VkDeviceSize indirectOffset,
	uint32_t indirectDrawCount )
{
	if ( indirectBuffer == VK_NULL_HANDLE || indirectDrawCount == 0 )
	{
		vkCmdDrawIndexed( vk.commandBuffer, indexCount, 1, firstIndex, 0, 0 );
		return 1;
	}

	uint32_t remaining = indirectDrawCount;
	VkDeviceSize offset = indirectOffset;
	while ( remaining > 0 )
	{
		const uint32_t chunk = std::min( remaining, vk.maxDrawIndirectCount );
		vkCmdDrawIndexedIndirect(
			vk.commandBuffer,
			indirectBuffer,
			offset,
			chunk,
			sizeof( VkDrawIndexedIndirectCommand ) );
		remaining -= chunk;
		offset += static_cast<VkDeviceSize>( chunk ) * sizeof( VkDrawIndexedIndirectCommand );
	}
	return indirectDrawCount;
}

static uint32_t VK_RecordBoundIndexedFog(
	qhandle_t shader,
	uint32_t indexCount,
	uint32_t firstIndex,
	VkPipeline *boundPipeline,
	VkDescriptorSet *boundTexture,
	VkBuffer indirectBuffer = VK_NULL_HANDLE,
	VkDeviceSize indirectOffset = 0,
	uint32_t indirectDrawCount = 0 )
{
	if ( !vk.world.hasGlobalFog )
	{
		return 0;
	}
	const bool polygonOffset = shader > 0 &&
		static_cast<size_t>( shader ) < vk.materials.size() &&
		vk.materials[shader].polygonOffset;
	VK_SetWorldDepthBias( polygonOffset );

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
	return VK_RecordIndexedCommands(
		indexCount, firstIndex, indirectBuffer, indirectOffset, indirectDrawCount );
}

static uint32_t VK_RecordBoundIndexedShader(
	qhandle_t shader,
	qhandle_t lightmap,
	bool vertexLit,
	uint32_t indexCount,
	uint32_t firstIndex,
	vk_world_pass_t pass,
	VkPipeline *boundPipeline,
	VkDescriptorSet *boundTexture,
	const byte *entityColor = nullptr,
	bool forceVertexColor = false,
	int materialStageFilter = -1,
	bool drawRiverFinalOverlay = true,
	const vk_world_batch_t *worldBatch = nullptr,
	const float *diffuseColor = nullptr,
	VkPipeline opaquePipelineOverride = VK_NULL_HANDLE,
	VkBuffer indirectBuffer = VK_NULL_HANDLE,
	VkDeviceSize indirectOffset = 0,
	uint32_t indirectDrawCount = 0 )
{
	const auto recordDraw = [&]()
	{
		return VK_RecordIndexedCommands(
			indexCount, firstIndex, indirectBuffer, indirectOffset, indirectDrawCount );
	};
	const bool polygonOffset = shader > 0 &&
		static_cast<size_t>( shader ) < vk.materials.size() &&
		vk.materials[shader].polygonOffset;
	VK_SetWorldDepthBias( polygonOffset );
	if ( shader > 0 && static_cast<size_t>( shader ) < vk.materials.size() &&
		 !vk.materials[shader].stages.empty() )
	{
		const vk_material_t &material = vk.materials[shader];
		const bool yavinRiverMaterial = VK_ShaderIsYavinRiver( shader );
		const char *auditName = nullptr;
		const int auditTarget = VK_MaterialAuditTarget( shader, &auditName );
		const uint8_t auditPass = static_cast<uint8_t>( 1u << static_cast<unsigned>( pass ) );
		const bool audit = auditTarget >= 0 && vk.materialAuditCvar != nullptr &&
			vk.materialAuditCvar->integer != 0 &&
			( vk.materialAuditPasses[auditTarget] & auditPass ) == 0;
		if ( audit )
		{
			vk.materialAuditPasses[auditTarget] |= auditPass;
			ri.Printf( PRINT_ALL,
				"rd-vulkan-material-audit: draw material=%s shader=%d pass=%d "
				"surfaceFirstIndex=%u indexCount=%u lightmap=%d stages=%zu\n",
				auditName, shader, static_cast<int>( pass ), firstIndex, indexCount,
				lightmap, material.stages.size() );
		}
		uint32_t drawCount = 0;
		const float riverExtinction = vk.yavinRiverExtinctionCvar != nullptr
			? VK_ClampValue( vk.yavinRiverExtinctionCvar->value, 0.0f, 1.0f )
			: 0.22f;
		const bool riverDiagnostic = vk.yavinRiverDiagnosticCvar != nullptr &&
			vk.yavinRiverDiagnosticCvar->integer != 0;
		const auto drawRiverOverlay = [&]( float alpha, bool diagnostic )
		{
			vk_material_stage_t overlayStage = {};
			overlayStage.alpha = alpha;
			overlayStage.blendMode = VK_BLEND_ALPHA;
			overlayStage.color[0] = diagnostic ? 1.0f : 0.78f;
			overlayStage.color[1] = diagnostic ? 0.0f : 0.86f;
			overlayStage.color[2] = diagnostic ? 1.0f : 0.90f;
			overlayStage.color[3] = 1.0f;
			VK_BindWorldPipeline( overlayStage.blendMode, boundPipeline );
			if ( !VK_BindWorldTexture( 1, boundTexture, false ) )
			{
				return false;
			}
			VK_PushWorldStage( &overlayStage, false, entityColor );
			vkCmdDrawIndexed( vk.commandBuffer, indexCount, 1, firstIndex, 0, 0 );
			return true;
		};
		if ( pass == VK_WORLD_PASS_TRANSLUCENT && yavinRiverMaterial )
		{
			if ( !vk.loggedYavinRiverDraw )
			{
				const float opacity = vk.yavinRiverOpacityCvar != nullptr
					? vk.yavinRiverOpacityCvar->value : 1.0f;
				const int stageMask = vk.yavinRiverStageMaskCvar != nullptr
					? vk.yavinRiverStageMaskCvar->integer : 15;
				ri.Printf( PRINT_ALL,
					"rd-vulkan-river-audit: shader=%d firstIndex=%u indices=%u "
					"opacity=%.3f extinction=%.3f stageMask=%d diagnostic=%d batching=stage-major\n",
					shader, firstIndex, indexCount, opacity, riverExtinction, stageMask,
					riverDiagnostic ? 1 : 0 );
				vk.loggedYavinRiverDraw = true;
			}
			if ( riverDiagnostic )
			{
				if ( drawRiverOverlay( 1.0f, true ) )
				{
					++drawCount;
				}
				return drawCount;
			}
		}
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
			if ( materialStageFilter >= 0 &&
				static_cast<int>( stageIndex ) != materialStageFilter )
			{
				continue;
			}
			const vk_material_stage_t &stage = material.stages[stageIndex];
			const uint32_t riverStageMask = vk.yavinRiverStageMaskCvar != nullptr
				? static_cast<uint32_t>( vk.yavinRiverStageMaskCvar->integer )
				: 15u;
			const bool riverStageEnabled = stage.yavinRiverStage == 0 ||
				( riverStageMask & ( 1u << ( stage.yavinRiverStage - 1 ) ) ) != 0;
			if ( audit )
			{
				const bool selected = stage.surfaceSprite.type == VK_SURFACE_SPRITE_NONE &&
					( ( pass == VK_WORLD_PASS_OPAQUE ) ==
					  ( stage.blendMode == VK_BLEND_OPAQUE ) ) && riverStageEnabled;
				ri.Printf( PRINT_ALL,
					"rd-vulkan-material-audit: stage=%zu texture=%d blend=%s alpha=%.4f "
					"lightmap=%d depthWrite=%d sprite=%d riverStage=%u enabled=%d selected=%d\n",
					stageIndex, stage.texture, VK_BlendModeName( stage.blendMode ),
					stage.alpha, stage.lightmap, stage.depthWrite,
					static_cast<int>( stage.surfaceSprite.type ), stage.yavinRiverStage,
					riverStageEnabled, selected );
			}
			if ( !riverStageEnabled )
			{
				continue;
			}
			if ( stage.surfaceSprite.type != VK_SURFACE_SPRITE_NONE )
			{
				continue;
			}
			if ( promoteVertexStage && stage.lightmap )
			{
				continue;
			}
			vk_material_stage_t effectiveStage = stage;
			bool vertexStyleStage = false;
			if ( promoteVertexStage && !promoted )
			{
				effectiveStage.blendMode = VK_BLEND_OPAQUE;
				effectiveStage.vertexColor = true;
				promoted = true;
				vertexStyleStage = true;
			}
			if ( forceVertexColor )
			{
				effectiveStage.vertexColor = true;
				effectiveStage.alphaTest = VK_ALPHA_TEST_GREATER_ZERO;
			}
			const bool opaque = effectiveStage.blendMode == VK_BLEND_OPAQUE;
			if ( ( pass == VK_WORLD_PASS_OPAQUE ) != opaque )
			{
				continue;
			}
			const bool styledStage = worldBatch != nullptr &&
				( effectiveStage.lightmap || vertexStyleStage );
			for ( int styleSlot = 0; styleSlot < 1; ++styleSlot )
			{
				const byte style = styledStage
					? ( vertexStyleStage
						? worldBatch->vertexStyles[styleSlot]
						: worldBatch->lightmapStyles[styleSlot] )
					: LS_NORMAL;
				if ( style >= LS_UNUSED || style >= MAX_LIGHT_STYLES )
				{
					continue;
				}
				const qhandle_t styleLightmap = worldBatch != nullptr
					? worldBatch->lightmaps[styleSlot]
					: lightmap;
				const qhandle_t texture = effectiveStage.lightmap
					? styleLightmap
					: effectiveStage.texture;
				if ( texture == 2 || !VK_WorldTextureUsable( texture ) )
				{
					continue;
				}
				const vk_blend_mode_t blendMode = styledStage && styleSlot > 0
					? VK_BLEND_ADDITIVE
					: effectiveStage.blendMode;
				VK_BindWorldPipelineOverride(
					blendMode, opaquePipelineOverride,
					boundPipeline,
					styleSlot == 0 && effectiveStage.depthWrite );
				if ( !VK_BindWorldTexture(
						texture, boundTexture,
						!effectiveStage.lightmap && !effectiveStage.clampMap ) )
				{
					continue;
				}
				const byte *styleColor = styledStage
					? vk.lightStyles[style].data()
					: nullptr;
				VK_PushWorldStage(
					&effectiveStage,
					effectiveStage.lightmap,
					entityColor,
					styleSlot,
					vertexStyleStage ? styleSlot : 0,
					styleColor,
					diffuseColor );
				drawCount += recordDraw();
			}
		}
		if ( drawRiverFinalOverlay && pass == VK_WORLD_PASS_TRANSLUCENT && yavinRiverMaterial &&
			 riverExtinction > 0.0f && drawRiverOverlay( riverExtinction, false ) )
		{
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
	const int worldDebug = vk.worldDebugCvar != nullptr
		? VK_ClampValue( vk.worldDebugCvar->integer, 0, 2 ) : 0;
	if ( worldDebug == 2 )
	{
		if ( !hasLightmap )
		{
			return 0;
		}
		VK_BindWorldPipeline( VK_BLEND_OPAQUE, boundPipeline );
		if ( !VK_BindWorldTexture( lightmap, boundTexture, false ) )
		{
			return 0;
		}
		vk_material_stage_t lightmapOnlyStage = {};
		lightmapOnlyStage.alpha = 1.0f;
		lightmapOnlyStage.color[0] = 1.0f;
		lightmapOnlyStage.color[1] = 1.0f;
		lightmapOnlyStage.color[2] = 1.0f;
		lightmapOnlyStage.color[3] = 1.0f;
		const byte *styleColor = worldBatch != nullptr &&
			worldBatch->lightmapStyles[0] < MAX_LIGHT_STYLES
			? vk.lightStyles[worldBatch->lightmapStyles[0]].data()
			: nullptr;
		VK_PushWorldStage( &lightmapOnlyStage, true, nullptr, 0, 0, styleColor );
		return recordDraw();
	}
	vk_material_stage_t baseStage = {};
	baseStage.alpha = 1.0f;
	baseStage.color[0] = 1.0f;
	baseStage.color[1] = 1.0f;
	baseStage.color[2] = 1.0f;
	baseStage.color[3] = 1.0f;
	baseStage.vertexColor = forceVertexColor || !hasLightmap;
	baseStage.alphaTest = forceVertexColor
		? VK_ALPHA_TEST_GREATER_ZERO
		: VK_ALPHA_TEST_NONE;
	uint32_t drawCount = 0;
	const bool styledVertices = vertexLit && worldBatch != nullptr;
	for ( int styleSlot = 0; styleSlot < 1; ++styleSlot )
	{
		const byte style = styledVertices ? worldBatch->vertexStyles[styleSlot] : LS_NORMAL;
		if ( style >= LS_UNUSED || style >= MAX_LIGHT_STYLES )
		{
			continue;
		}
		VK_BindWorldPipeline(
			styleSlot > 0 ? VK_BLEND_ADDITIVE : VK_BLEND_OPAQUE, boundPipeline );
		if ( !VK_BindWorldTexture( texture, boundTexture ) )
		{
			continue;
		}
		VK_PushWorldStage(
			&baseStage,
			false,
			nullptr,
			0,
			styleSlot,
			styledVertices ? vk.lightStyles[style].data() : nullptr );
		drawCount += recordDraw();
	}

	if ( !hasLightmap || worldDebug == 1 )
	{
		return drawCount;
	}
	vk_material_stage_t lightmapStage = {};
	lightmapStage.alpha = 1.0f;
	lightmapStage.color[0] = 1.0f;
	lightmapStage.color[1] = 1.0f;
	lightmapStage.color[2] = 1.0f;
	lightmapStage.color[3] = 1.0f;
	for ( int styleSlot = 0; styleSlot < 1; ++styleSlot )
	{
		const byte style = worldBatch != nullptr
			? worldBatch->lightmapStyles[styleSlot]
			: LS_NORMAL;
		if ( style >= LS_UNUSED || style >= MAX_LIGHT_STYLES )
		{
			continue;
		}
		const qhandle_t styleLightmap = worldBatch != nullptr
			? worldBatch->lightmaps[styleSlot]
			: lightmap;
		if ( !VK_WorldTextureUsable( styleLightmap ) )
		{
			continue;
		}
		VK_BindWorldPipeline(
			styleSlot == 0 ? VK_BLEND_MODULATE : VK_BLEND_ADDITIVE, boundPipeline );
		if ( !VK_BindWorldTexture( styleLightmap, boundTexture, false ) )
		{
			continue;
		}
		VK_PushWorldStage(
			&lightmapStage,
			true,
			nullptr,
			styleSlot,
			0,
			vk.lightStyles[style].data() );
		drawCount += recordDraw();
	}
	return drawCount;
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

static const vk_model_tag_t *VK_ModelTagForFrame(
	const vk_model_t &model,
	int frame,
	const char *tagName )
{
	if ( model.type != VK_MODEL_MD3 || model.frameCount <= 0 || model.tagCount <= 0 ||
		 tagName == nullptr || tagName[0] == '\0' )
	{
		return nullptr;
	}
	frame = VK_ClampValue( frame, 0, model.frameCount - 1 );
	const size_t first = static_cast<size_t>( frame ) * model.tagCount;
	if ( first > model.tags.size() ||
		 static_cast<size_t>( model.tagCount ) > model.tags.size() - first )
	{
		return nullptr;
	}
	for ( int tagIndex = 0; tagIndex < model.tagCount; ++tagIndex )
	{
		const vk_model_tag_t &tag = model.tags[first + tagIndex];
		if ( Q_stricmp( tag.name.c_str(), tagName ) == 0 )
		{
			return &tag;
		}
	}
	return nullptr;
}

void VK_Backend_LerpTag(
	orientation_t *tag,
	qhandle_t modelHandle,
	int startFrame,
	int endFrame,
	float fraction,
	const char *tagName )
{
	if ( tag == nullptr )
	{
		return;
	}
	VectorClear( tag->origin );
	AxisClear( tag->axis );

	const vk_model_t *model = VK_ModelForHandle( modelHandle );
	if ( model == nullptr )
	{
		return;
	}
	const vk_model_tag_t *start =
		VK_ModelTagForFrame( *model, startFrame, tagName );
	const vk_model_tag_t *finish =
		VK_ModelTagForFrame( *model, endFrame, tagName );
	if ( start == nullptr || finish == nullptr )
	{
		return;
	}
	static bool loggedTagInterpolation = false;
	if ( !loggedTagInterpolation )
	{
		ri.Printf(
			PRINT_ALL,
			"rd-vulkan-model: MD3 tag interpolation active: model=%s tag=%s frames=%d..%d\n",
			model->name.c_str(), tagName, startFrame, endFrame );
		loggedTagInterpolation = true;
	}

	fraction = VK_ClampValue( fraction, 0.0f, 1.0f );
	const float backFraction = 1.0f - fraction;
	for ( int component = 0; component < 3; ++component )
	{
		tag->origin[component] =
			start->origin[component] * backFraction + finish->origin[component] * fraction;
		for ( int axisIndex = 0; axisIndex < 3; ++axisIndex )
		{
			tag->axis[axisIndex][component] =
				start->axis[axisIndex][component] * backFraction +
				finish->axis[axisIndex][component] * fraction;
		}
	}
	for ( int axisIndex = 0; axisIndex < 3; ++axisIndex )
	{
		VectorNormalize( tag->axis[axisIndex] );
	}
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

static bool VK_LocalBoundsIntersectView(
	const float mins[3],
	const float maxs[3],
	const refEntity_t &entity,
	const float view[16],
	const float projection[16] )
{
	float model[16] = {};
	float modelView[16] = {};
	float mvp[16] = {};
	VK_BuildEntityModelMatrix( entity, model );
	VK_MatrixMultiply( view, model, modelView );
	VK_MatrixMultiply( projection, modelView, mvp );

	bool outsideLeft = true;
	bool outsideRight = true;
	bool outsideBottom = true;
	bool outsideTop = true;
	for ( int corner = 0; corner < 8; ++corner )
	{
		const float x = ( corner & 1 ) != 0 ? maxs[0] : mins[0];
		const float y = ( corner & 2 ) != 0 ? maxs[1] : mins[1];
		const float z = ( corner & 4 ) != 0 ? maxs[2] : mins[2];
		const float clipX = mvp[0] * x + mvp[4] * y + mvp[8] * z + mvp[12];
		const float clipY = mvp[1] * x + mvp[5] * y + mvp[9] * z + mvp[13];
		const float clipW = mvp[3] * x + mvp[7] * y + mvp[11] * z + mvp[15];
		outsideLeft &= clipX < -clipW;
		outsideRight &= clipX > clipW;
		outsideBottom &= clipY < -clipW;
		outsideTop &= clipY > clipW;
	}

	return !( outsideLeft || outsideRight || outsideBottom || outsideTop );
}

static bool VK_InlineModelIntersectsView(
	const vk_world_inline_model_t &inlineModel,
	const refEntity_t &entity,
	const float view[16],
	const float projection[16] )
{
	return VK_LocalBoundsIntersectView(
		inlineModel.mins, inlineModel.maxs, entity, view, projection );
}

static bool VK_ModelEntityIntersectsView(
	const refEntity_t &entity,
	const vk_model_t *model,
	const float view[16],
	const float projection[16] )
{
	if ( ( entity.renderfx & ( RF_FIRST_PERSON | RF_DEPTHHACK ) ) != 0 )
	{
		return true;
	}

	float mins[3];
	float maxs[3];
	if ( entity.ghoul2 != nullptr && entity.ghoul2->IsValid() &&
		 std::isfinite( entity.radius ) && entity.radius > 0.0f )
	{
		// Match the legacy Ghoul2 sphere cull, with a small edge margin for
		// animated limbs and the difference between the two eye frusta.
		const float radius = entity.radius * 1.05f;
		for ( int axis = 0; axis < 3; ++axis )
		{
			mins[axis] = -radius;
			maxs[axis] = radius;
		}
	}
	else if ( model != nullptr && model->hasBounds )
	{
		for ( int axis = 0; axis < 3; ++axis )
		{
			const float center = ( model->mins[axis] + model->maxs[axis] ) * 0.5f;
			const float extent = ( model->maxs[axis] - model->mins[axis] ) * 0.525f + 1.0f;
			mins[axis] = center - extent;
			maxs[axis] = center + extent;
		}
	}
	else
	{
		return true;
	}

	return VK_LocalBoundsIntersectView( mins, maxs, entity, view, projection );
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
	VkDescriptorSet *boundTexture,
	int selectedVideoInlineModel,
	const refEntity_t &entity,
	const std::vector<vk_dynamic_light_t> &dynamicLights )
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
		if ( ( batch.surfaceFlags & SURF_FORCESIGHT ) != 0 &&
			 ( vk.worldRefdef.rdflags & RDF_ForceSightOn ) == 0 )
		{
			continue;
		}
		if ( selectedVideoInlineModel != -1 &&
			 model.inlineModelIndex != selectedVideoInlineModel && batch.shader > 0 &&
			 static_cast<size_t>( batch.shader ) < vk.materials.size() )
		{
			const vk_material_t &material = vk.materials[batch.shader];
			const auto videoStage = std::find_if(
				material.stages.begin(), material.stages.end(),
				[]( const vk_material_stage_t &stage ) { return stage.videoMap; } );
			if ( videoStage != material.stages.end() )
			{
				continue;
			}
		}
		if ( pass == VK_WORLD_PASS_FOG )
		{
			drawCount += VK_RecordBoundIndexedFog(
				batch.shader, batch.indexCount, batch.firstIndex, boundPipeline, boundTexture );
		}
		else
		{
			drawCount += VK_RecordBoundIndexedShader(
				batch.shader, batch.lightmaps[0], batch.vertexLit,
				batch.indexCount, batch.firstIndex,
				pass, boundPipeline, boundTexture, nullptr, false, -1, true, &batch );
		}
	}
	if ( pass == VK_WORLD_PASS_OPAQUE )
	{
		VK_SetWorldDepthBias( false );
		for ( const vk_dynamic_light_t &worldLight : dynamicLights )
		{
			vk_dynamic_light_t localLight = worldLight;
			vec3_t offset;
			VectorSubtract( worldLight.origin, entity.origin, offset );
			if ( VectorLengthSquared( entity.axis[0] ) > 0.5f &&
				 VectorLengthSquared( entity.axis[1] ) > 0.5f &&
				 VectorLengthSquared( entity.axis[2] ) > 0.5f )
			{
				for ( int axis = 0; axis < 3; ++axis )
				{
					localLight.origin[axis] = DotProduct( offset, entity.axis[axis] );
				}
			}
			else
			{
				VectorCopy( offset, localLight.origin );
			}

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
				if ( ( batch.surfaceFlags & ( SURF_NODLIGHT | SURF_SKY ) ) != 0 ||
					 !VK_DynamicLightIntersectsBatch( localLight, batch ) )
				{
					continue;
				}
				vk_alpha_test_t alphaTest = VK_ALPHA_TEST_NONE;
				const qhandle_t texture =
					VK_DynamicLightSurfaceTexture( batch.shader, &alphaTest );
				VK_BindWorldPipeline( VK_BLEND_ADDITIVE, boundPipeline );
				if ( !VK_BindWorldTexture( texture, boundTexture ) )
				{
					continue;
				}
				VK_PushWorldDynamicLight( localLight, alphaTest );
				vkCmdDrawIndexed(
					vk.commandBuffer, batch.indexCount, 1, batch.firstIndex, 0, 0 );
				++drawCount;
			}
		}
	}
	return drawCount;
}

static int VK_InlineModelVideoClient( const vk_model_t &model )
{
	if ( model.inlineModelIndex < 0 ||
		 static_cast<size_t>( model.inlineModelIndex ) >= vk.world.inlineModels.size() )
	{
		return -1;
	}

	const vk_world_inline_model_t &inlineModel = vk.world.inlineModels[model.inlineModelIndex];
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
		const qhandle_t shader = vk.world.batches[batchIndex].shader;
		if ( shader <= 0 || static_cast<size_t>( shader ) >= vk.materials.size() )
		{
			continue;
		}
		for ( const vk_material_stage_t &stage : vk.materials[shader].stages )
		{
			if ( stage.videoMap )
			{
				return stage.videoHandle;
			}
		}
	}
	return -1;
}

static float VK_InlineModelViewDistanceSquared(
	const vk_world_inline_model_t &inlineModel,
	const refEntity_t &entity,
	const float view[16] )
{
	float model[16] = {};
	float modelView[16] = {};
	VK_BuildEntityModelMatrix( entity, model );
	VK_MatrixMultiply( view, model, modelView );
	const float center[3] = {
		( inlineModel.mins[0] + inlineModel.maxs[0] ) * 0.5f,
		( inlineModel.mins[1] + inlineModel.maxs[1] ) * 0.5f,
		( inlineModel.mins[2] + inlineModel.maxs[2] ) * 0.5f,
	};
	const float viewCenter[3] = {
		modelView[0] * center[0] + modelView[4] * center[1] +
			modelView[8] * center[2] + modelView[12],
		modelView[1] * center[0] + modelView[5] * center[1] +
			modelView[9] * center[2] + modelView[13],
		modelView[2] * center[0] + modelView[6] * center[1] +
			modelView[10] * center[2] + modelView[14],
	};
	return viewCenter[0] * viewCenter[0] + viewCenter[1] * viewCenter[1] +
		viewCenter[2] * viewCenter[2];
}

static bool VK_InlineModelFacesView(
	const vk_world_inline_model_t &inlineModel,
	const refEntity_t &entity,
	const float view[16] )
{
	if ( !inlineModel.hasFacingNormal )
	{
		return true;
	}

	float model[16] = {};
	float modelView[16] = {};
	VK_BuildEntityModelMatrix( entity, model );
	VK_MatrixMultiply( view, model, modelView );
	const float center[3] = {
		( inlineModel.mins[0] + inlineModel.maxs[0] ) * 0.5f,
		( inlineModel.mins[1] + inlineModel.maxs[1] ) * 0.5f,
		( inlineModel.mins[2] + inlineModel.maxs[2] ) * 0.5f,
	};
	const float viewCenter[3] = {
		modelView[0] * center[0] + modelView[4] * center[1] +
			modelView[8] * center[2] + modelView[12],
		modelView[1] * center[0] + modelView[5] * center[1] +
			modelView[9] * center[2] + modelView[13],
		modelView[2] * center[0] + modelView[6] * center[1] +
			modelView[10] * center[2] + modelView[14],
	};
	const float viewNormal[3] = {
		modelView[0] * inlineModel.facingNormal[0] +
			modelView[4] * inlineModel.facingNormal[1] +
			modelView[8] * inlineModel.facingNormal[2],
		modelView[1] * inlineModel.facingNormal[0] +
			modelView[5] * inlineModel.facingNormal[1] +
			modelView[9] * inlineModel.facingNormal[2],
		modelView[2] * inlineModel.facingNormal[0] +
			modelView[6] * inlineModel.facingNormal[1] +
			modelView[10] * inlineModel.facingNormal[2],
	};

	// In view space the camera is at the origin, so negate the center vector.
	return viewCenter[2] < -0.001f && DotProduct( viewNormal, viewCenter ) < -0.001f;
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
	if ( ghoul != nullptr && surface.modelSurfaceIndex != ghoul->mSurfaceRoot )
	{
		int ancestorIndex = surface.parentSurfaceIndex;
		bool descendsFromRoot = false;
		for ( size_t depth = 0;
			  depth < model.surfaces.size() && ancestorIndex >= 0;
			  ++depth )
		{
			if ( ancestorIndex == ghoul->mSurfaceRoot )
			{
				descendsFromRoot = true;
				break;
			}
			const vk_model_surface_t *ancestor =
				VK_GLMSurfaceForIndex( model, ancestorIndex );
			if ( ancestor == nullptr )
			{
				break;
			}
			ancestorIndex = ancestor->parentSurfaceIndex;
		}
		if ( !descendsFromRoot )
		{
			return false;
		}
	}

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

char *VK_Backend_GetModelSurfaceName( qhandle_t modelHandle, int surfaceIndex )
{
	const vk_model_t *model = VK_ModelForHandle( modelHandle );
	const vk_model_surface_t *surface = model != nullptr
		? VK_GLMSurfaceForIndex( *model, surfaceIndex )
		: nullptr;
	return surface != nullptr
		? const_cast<char *>( surface->name.c_str() )
		: nullptr;
}

int VK_Backend_GetModelParentSurface( qhandle_t modelHandle, int surfaceIndex )
{
	const vk_model_t *model = VK_ModelForHandle( modelHandle );
	const vk_model_surface_t *surface = model != nullptr
		? VK_GLMSurfaceForIndex( *model, surfaceIndex )
		: nullptr;
	return surface != nullptr ? surface->parentSurfaceIndex : -1;
}

int VK_Backend_GetModelSurfaceRenderStatus(
	qhandle_t modelHandle,
	const CGhoul2Info *ghoul,
	const char *surfaceName )
{
	const vk_model_t *model = VK_ModelForHandle( modelHandle );
	if ( model == nullptr || surfaceName == nullptr )
	{
		return -1;
	}
	const int surfaceIndex =
		VK_Backend_FindModelSurface( modelHandle, surfaceName, nullptr );
	const vk_model_surface_t *surface =
		VK_GLMSurfaceForIndex( *model, surfaceIndex );
	if ( surface == nullptr )
	{
		return -1;
	}

	unsigned int flags = VK_GLMEffectiveFlags( *surface, ghoul );
	int parentIndex = surface->parentSurfaceIndex;
	for ( size_t depth = 0; depth < model->surfaces.size() && parentIndex >= 0; ++depth )
	{
		const vk_model_surface_t *parent =
			VK_GLMSurfaceForIndex( *model, parentIndex );
		if ( parent == nullptr )
		{
			break;
		}
		if ( ( VK_GLMEffectiveFlags( *parent, ghoul ) &
			   G2SURFACEFLAG_NODESCENDANTS ) != 0 )
		{
			flags |= G2SURFACEFLAG_OFF;
			break;
		}
		parentIndex = parent->parentSurfaceIndex;
	}
	return static_cast<int>( flags );
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
	const vk_model_surface_t *rootSurface =
		VK_GLMSurfaceForIndex( model, ghoul.mSurfaceRoot );
	ri.Printf(
		unresolvedVisible > 0 ? PRINT_WARNING : PRINT_ALL,
		"rd-vulkan-ghoul2-render-audit: model=%s skin=%s root=%d(%s) surfaces=%zu visible=%zu "
		"hidden=%zu bolts=%zu skinMapped=%zu skinOff=%zu unresolvedVisible=%zu\n",
		model.name.c_str(),
		skinName,
		ghoul.mSurfaceRoot,
		rootSurface != nullptr ? rootSurface->name.c_str() : "<invalid>",
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

static void VK_CreateGhoul2AngleMatrix( const vec3_t angles, mdxaBone_t *matrix )
{
	vec3_t axis[3];
	AnglesToAxis( angles, axis );
	for ( int row = 0; row < 3; ++row )
	{
		matrix->matrix[row][0] = axis[0][row];
		matrix->matrix[row][1] = axis[1][row];
		matrix->matrix[row][2] = axis[2][row];
		matrix->matrix[row][3] = 0.0f;
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

		mdxaBone_t animated = {};
		if ( bone.parent < 0 )
		{
			VK_MultiplyBoneMatrices(
				rootMatrix,
				local,
				&animated );
		}
		else
		{
			VK_MultiplyBoneMatrices(
				( *finalBones )[bone.parent],
				local,
				&animated );
		}

		mdxaBone_t overridden = animated;
		const int angleFlags = override != nullptr ? override->flags & BONE_ANGLES_TOTAL : 0;
		if ( angleFlags == BONE_ANGLES_PREMULT )
		{
			VK_MultiplyBoneMatrices(
				bone.parent < 0 ? rootMatrix : ( *finalBones )[bone.parent],
				override->newMatrix,
				&overridden );
		}
		else if ( angleFlags == BONE_ANGLES_POSTMULT )
		{
			VK_MultiplyBoneMatrices( animated, override->newMatrix, &overridden );
		}
		else if ( angleFlags == BONE_ANGLES_REPLACE )
		{
			mdxaBone_t posed = {};
			VK_MultiplyBoneMatrices( animated, bone.basePose, &posed );
			const float matrixScale = std::sqrt(
				posed.matrix[0][0] * posed.matrix[0][0] +
				posed.matrix[0][1] * posed.matrix[0][1] +
				posed.matrix[0][2] * posed.matrix[0][2] );
			mdxaBone_t replacement = override->newMatrix;
			for ( int row = 0; row < 3; ++row )
			{
				for ( int column = 0; column < 3; ++column )
				{
					replacement.matrix[row][column] *= matrixScale;
				}
				replacement.matrix[row][3] = posed.matrix[row][3];
			}
			VK_MultiplyBoneMatrices( replacement, bone.basePoseInverse, &overridden );
		}

		if ( override != nullptr && angleFlags != 0 && override->boneBlendTime > 0 )
		{
			const float blend = VK_ClampValue(
				static_cast<float>( time - override->boneBlendStart ) /
					static_cast<float>( override->boneBlendTime ),
				0.0f,
				1.0f );
			for ( int element = 0; element < 12; ++element )
			{
				reinterpret_cast<float *>( &overridden )[element] =
					reinterpret_cast<const float *>( &animated )[element] * ( 1.0f - blend ) +
					reinterpret_cast<const float *>( &overridden )[element] * blend;
			}
		}
		( *finalBones )[boneIndex] = overridden;
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

static int VK_DisintegrationMode( const refEntity_t *entity )
{
	if ( entity == nullptr )
	{
		return 0;
	}
	if ( ( entity->renderfx & RF_DISINTEGRATE2 ) != 0 )
	{
		return 2;
	}
	return ( entity->renderfx & RF_DISINTEGRATE1 ) != 0 ? 1 : 0;
}

static void VK_ApplyDisintegration(
	vk_world_vertex_t *vertex,
	const refEntity_t &entity,
	int sceneTime )
{
	const int mode = VK_DisintegrationMode( &entity );
	if ( mode == 0 )
	{
		return;
	}

	const float threshold = std::max( 0.0f,
		static_cast<float>( sceneTime - entity.endTime ) * 0.045f );
	const float thresholdSquared = threshold * threshold;
	const float dx = entity.oldorigin[0] - vertex->position[0];
	const float dy = entity.oldorigin[1] - vertex->position[1];
	const float dz = entity.oldorigin[2] - vertex->position[2];
	const float distanceSquared = dx * dx + dy * dy + dz * dz;

	if ( distanceSquared < thresholdSquared )
	{
		vertex->color[0] = 0.0f;
		vertex->color[1] = 0.0f;
		vertex->color[2] = 0.0f;
		vertex->color[3] = 0.0f;
		if ( mode == 2 )
		{
			vertex->position[0] += vertex->normal[0] * 2.0f;
			vertex->position[1] += vertex->normal[1] * 2.0f;
			vertex->position[2] += vertex->normal[2] * 0.5f;
		}
		return;
	}

	vertex->color[3] = 1.0f;
	if ( mode == 2 )
	{
		vertex->color[0] = 1.0f;
		vertex->color[1] = 1.0f;
		vertex->color[2] = 1.0f;
		if ( distanceSquared < thresholdSquared + 50.0f )
		{
			vertex->position[0] += vertex->normal[0];
			vertex->position[1] += vertex->normal[1];
		}
		return;
	}

	float colorScale = 1.0f;
	if ( distanceSquared < thresholdSquared + 60.0f )
	{
		colorScale = 0.0f;
	}
	else if ( distanceSquared < thresholdSquared + 150.0f )
	{
		colorScale = 0x6f / 255.0f;
	}
	else if ( distanceSquared < thresholdSquared + 180.0f )
	{
		colorScale = 0xaf / 255.0f;
	}
	for ( int component = 0; component < 3; ++component )
	{
		vertex->color[component] *= colorScale;
	}
}

static void VK_RecordSkinModelTiming(
	const vk_model_t &model,
	bool cacheHit,
	size_t vertexCount,
	std::chrono::steady_clock::time_point begin )
{
	if ( !VK_TimingEnabled() )
	{
		return;
	}
	vk_skin_model_timing_t &timing = vk.timingSkinModels[&model];
	if ( timing.name.empty() )
	{
		timing.name = model.name;
	}
	++timing.calls;
	timing.cacheHits += cacheHit ? 1 : 0;
	timing.misses += cacheHit ? 0 : 1;
	timing.vertices += vertexCount;
	timing.totalMs += std::chrono::duration<double, std::milli>(
		std::chrono::steady_clock::now() - begin ).count();
}

static bool VK_StreamSkinnedGLMSurface(
	const vk_model_t &model,
	const vk_model_surface_t &surface,
	const CGhoul2Info &ghoul,
	int time,
	const std::vector<mdxaBone_t> &bones,
	const refEntity_t *entity,
	VkDeviceSize *vertexOffset )
{
	const std::chrono::steady_clock::time_point timingBegin = VK_TimingEnabled()
		? std::chrono::steady_clock::now()
		: std::chrono::steady_clock::time_point{};
	time = VK_G2API_GetTime( time );
	const int disintegrationMode = VK_DisintegrationMode( entity );
	const vk_ghoul2_surface_cache_key_t cacheKey = {
		&ghoul,
		&surface,
		time,
		disintegrationMode,
	};
	const auto cachedSurface = vk.ghoul2SurfaceCache.find( cacheKey );
	if ( cachedSurface != vk.ghoul2SurfaceCache.end() )
	{
		*vertexOffset = cachedSurface->second;
		VK_RecordSkinModelTiming( model, true, 0, timingBegin );
		return true;
	}

	if ( surface.glmVertices.size() != surface.glmBaseVertices.size() ||
		 surface.glmSkinVertices.size() != surface.glmBaseVertices.size() ||
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
		const vk_glm_skin_vertex_t &skin = surface.glmSkinVertices[vertexIndex];
		const float *sourcePosition = surface.glmBaseVertices[vertexIndex].position;
		const float *sourceNormal = surface.glmBaseVertices[vertexIndex].normal;
		destination[vertexIndex] = surface.glmBaseVertices[vertexIndex];
		destination[vertexIndex].position[0] = 0.0f;
		destination[vertexIndex].position[1] = 0.0f;
		destination[vertexIndex].position[2] = 0.0f;
		destination[vertexIndex].normal[0] = 0.0f;
		destination[vertexIndex].normal[1] = 0.0f;
		destination[vertexIndex].normal[2] = 0.0f;

		for ( int weightIndex = 0; weightIndex < skin.weightCount; ++weightIndex )
		{
			const int boneIndex = skin.boneIndices[weightIndex];
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

			const float weight = skin.weights[weightIndex];
			const mdxaBone_t &bone = bones[boneIndex];
			for ( int component = 0; component < 3; ++component )
			{
				destination[vertexIndex].position[component] += weight * (
					bone.matrix[component][0] * sourcePosition[0] +
					bone.matrix[component][1] * sourcePosition[1] +
					bone.matrix[component][2] * sourcePosition[2] +
					bone.matrix[component][3] );
				destination[vertexIndex].normal[component] += weight * (
					bone.matrix[component][0] * sourceNormal[0] +
					bone.matrix[component][1] * sourceNormal[1] +
					bone.matrix[component][2] * sourceNormal[2] );
			}
		}
		if ( VectorNormalize( destination[vertexIndex].normal ) == 0.0f )
		{
			VectorCopy( sourceNormal, destination[vertexIndex].normal );
		}
		if ( entity != nullptr )
		{
			VK_ApplyDisintegration( &destination[vertexIndex], *entity, time );
		}
	}
	VK_AuditSkinnedGLMSurface( model, surface, ghoul, destination );

	vk.skinnedVertexOffset = offset + byteCount;
	*vertexOffset = offset;
	vk.ghoul2SurfaceCache.emplace( cacheKey, offset );
	VK_RecordSkinModelTiming(
		model, false, surface.glmBaseVertices.size(), timingBegin );
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
	if ( vertexIndex >= surface.glmSkinVertices.size() )
	{
		return false;
	}
	const vk_glm_skin_vertex_t &skin = surface.glmSkinVertices[vertexIndex];
	const float *sourcePosition = surface.glmBaseVertices[vertexIndex].position;
	VectorClear( position );
	for ( int weightIndex = 0; weightIndex < skin.weightCount; ++weightIndex )
	{
		const int boneIndex = skin.boneIndices[weightIndex];
		if ( boneIndex < 0 || static_cast<size_t>( boneIndex ) >= bones.size() )
		{
			return false;
		}
		const float weight = skin.weights[weightIndex];
		const mdxaBone_t &bone = bones[boneIndex];
		for ( int component = 0; component < 3; ++component )
		{
			position[component] += weight * (
				bone.matrix[component][0] * sourcePosition[0] +
				bone.matrix[component][1] * sourcePosition[1] +
				bone.matrix[component][2] * sourcePosition[2] +
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

static const std::vector<mdxaBone_t> *VK_ResolveGhoul2HierarchyBones(
	const CGhoul2Info_v &ghoul2,
	int modelIndex,
	int sceneTime,
	std::vector<byte> *states,
	std::vector<const std::vector<mdxaBone_t> *> *resolvedBones );

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

	frameNumber = VK_G2API_GetTime( frameNumber );
	std::vector<byte> hierarchyStates( static_cast<size_t>( ghoul2.size() ), 0 );
	std::vector<const std::vector<mdxaBone_t> *> hierarchyBones(
		static_cast<size_t>( ghoul2.size() ), nullptr );
	const std::vector<mdxaBone_t> *bones =
		VK_ResolveGhoul2HierarchyBones(
			ghoul2,
			modelIndex,
			frameNumber,
			&hierarchyStates,
			&hierarchyBones );
	if ( bones == nullptr )
	{
		return qfalse;
	}

	mdxaBone_t bolt = {};
	if ( !VK_GetGhoul2BoltMatrix( *model, ghoul, *bones, boltIndex, scale, &bolt ) )
	{
		return qfalse;
	}

	mdxaBone_t world = {};
	VK_CreateBoneMatrix( angles, position, &world );
	VK_MultiplyBoneMatrices( world, bolt, matrix );
	return qtrue;
}

static bool VK_MaterialUsesLightingDiffuse( qhandle_t shader )
{
	if ( shader <= 0 || static_cast<size_t>( shader ) >= vk.materials.size() )
	{
		return false;
	}
	return std::any_of(
		vk.materials[shader].stages.begin(), vk.materials[shader].stages.end(),
		[]( const vk_material_stage_t &stage ) { return stage.lightingDiffuse; } );
}

static void VK_SetupEntityLighting(
	const refEntity_t &entity,
	const refdef_t &refdef,
	const std::vector<vk_dynamic_light_t> &dynamicLights,
	vk_entity_lighting_t *lighting )
{
	const float fallbackDirection[3] = { 0.428571f, 0.285714f, 0.857143f };
	lighting->ambient[0] = 150.0f;
	lighting->ambient[1] = 150.0f;
	lighting->ambient[2] = 150.0f;
	lighting->directed[0] = 150.0f;
	lighting->directed[1] = 150.0f;
	lighting->directed[2] = 150.0f;
	vec3_t worldDirection;
	VectorCopy( fallbackDirection, worldDirection );
	vec3_t lightOrigin;
	if ( ( entity.renderfx & RF_LIGHTING_ORIGIN ) != 0 )
	{
		VectorCopy( entity.lightingOrigin, lightOrigin );
	}
	else
	{
		VectorCopy( entity.origin, lightOrigin );
	}

	const vk_world_geometry_t &world = vk.world;
	if ( ( refdef.rdflags & RDF_NOWORLDMODEL ) == 0 &&
		 !world.lightGridData.empty() && !world.lightGridArray.empty() )
	{
		int position[3] = {};
		float fraction[3] = {};
		for ( int axis = 0; axis < 3; ++axis )
		{
			const float gridPosition =
				( lightOrigin[axis] - world.lightGridOrigin[axis] ) / world.lightGridSize[axis];
			position[axis] = static_cast<int>( std::floor( gridPosition ) );
			fraction[axis] = gridPosition - static_cast<float>( position[axis] );
			position[axis] = VK_ClampValue( position[axis], 0, world.lightGridBounds[axis] - 1 );
		}

		VectorClear( lighting->ambient );
		VectorClear( lighting->directed );
		VectorClear( worldDirection );
		const int gridStep[3] = {
			1,
			world.lightGridBounds[0],
			world.lightGridBounds[0] * world.lightGridBounds[1],
		};
		const size_t startIndex =
			static_cast<size_t>( position[0] ) * gridStep[0] +
			static_cast<size_t>( position[1] ) * gridStep[1] +
			static_cast<size_t>( position[2] ) * gridStep[2];
		float totalFactor = 0.0f;
		for ( int corner = 0; corner < 8; ++corner )
		{
			float factor = 1.0f;
			size_t arrayIndex = startIndex;
			for ( int axis = 0; axis < 3; ++axis )
			{
				if ( ( corner & ( 1 << axis ) ) != 0 )
				{
					factor *= fraction[axis];
					arrayIndex += static_cast<size_t>( gridStep[axis] );
				}
				else
				{
					factor *= 1.0f - fraction[axis];
				}
			}
			if ( factor <= 0.0f || arrayIndex >= world.lightGridArray.size() )
			{
				continue;
			}
			const dgrid_t &sample = world.lightGridData[world.lightGridArray[arrayIndex]];
			if ( sample.styles[0] == LS_NONE )
			{
				continue;
			}

			totalFactor += factor;
			for ( int style = 0; style < MAXLIGHTMAPS && sample.styles[style] != LS_NONE; ++style )
			{
				const byte lightStyle = sample.styles[style];
				if ( lightStyle >= MAX_LIGHT_STYLES )
				{
					continue;
				}
				for ( int channel = 0; channel < 3; ++channel )
				{
					const float styleScale = vk.lightStyles[lightStyle][channel] / 255.0f;
					lighting->ambient[channel] +=
						factor * sample.ambientLight[style][channel] * styleScale;
					lighting->directed[channel] +=
						factor * sample.directLight[style][channel] * styleScale;
				}
			}

			constexpr float byteToRadians = 2.0f * static_cast<float>( M_PI ) / 256.0f;
			const float latitude = sample.latLong[1] * byteToRadians;
			const float longitude = sample.latLong[0] * byteToRadians;
			const float sinLongitude = std::sin( longitude );
			const vec3_t sampleDirection = {
				std::cos( latitude ) * sinLongitude,
				std::sin( latitude ) * sinLongitude,
				std::cos( longitude ),
			};
			VectorMA( worldDirection, factor, sampleDirection, worldDirection );
		}

		if ( totalFactor > 0.0f && totalFactor < 0.99f )
		{
			const float inverseFactor = 1.0f / totalFactor;
			VectorScale( lighting->ambient, inverseFactor, lighting->ambient );
			VectorScale( lighting->directed, inverseFactor, lighting->directed );
		}
		VectorScale( lighting->ambient, 0.5f, lighting->ambient );
		if ( totalFactor <= 0.0f || VectorNormalize( worldDirection ) == 0.0f )
		{
			VectorCopy( fallbackDirection, worldDirection );
		}
	}

	const float minimumAmbient = ( entity.renderfx & RF_MORELIGHT ) != 0 ? 96.0f : 32.0f;
	for ( int channel = 0; channel < 3; ++channel )
	{
		lighting->ambient[channel] =
			std::min( 255.0f, lighting->ambient[channel] + minimumAmbient );
	}
	if ( VectorNormalize( worldDirection ) == 0.0f )
	{
		VectorCopy( fallbackDirection, worldDirection );
	}
	vec3_t weightedDirection;
	VectorScale( worldDirection, VectorLength( lighting->directed ), weightedDirection );
	constexpr float dynamicLightAtRadius = 16.0f;
	constexpr float dynamicLightMinimumRadius = 16.0f;
	for ( const vk_dynamic_light_t &light : dynamicLights )
	{
		vec3_t direction;
		VectorSubtract( light.origin, lightOrigin, direction );
		float distance = VectorNormalize( direction );
		distance = std::max( distance, dynamicLightMinimumRadius );
		const float contribution =
			dynamicLightAtRadius * light.radius * light.radius / ( distance * distance );
		for ( int channel = 0; channel < 3; ++channel )
		{
			lighting->directed[channel] += contribution * light.color[channel];
		}
		VectorMA( weightedDirection, contribution, direction, weightedDirection );
	}
	if ( VectorNormalize( weightedDirection ) == 0.0f )
	{
		VectorCopy( fallbackDirection, weightedDirection );
	}
	for ( int axis = 0; axis < 3; ++axis )
	{
		lighting->localDirection[axis] = DotProduct( weightedDirection, entity.axis[axis] );
	}
	VectorNormalize( lighting->localDirection );
}

static bool VK_StreamLitModelSurface(
	const vk_model_surface_t &surface,
	const vk_entity_lighting_t &lighting,
	VkDeviceSize *vertexOffset )
{
	if ( surface.glmBaseVertices.empty() || vk.skinnedVertexMapped == nullptr )
	{
		return false;
	}
	const VkDeviceSize alignment = 16;
	const VkDeviceSize offset =
		( vk.skinnedVertexOffset + alignment - 1 ) & ~( alignment - 1 );
	const VkDeviceSize byteCount = static_cast<VkDeviceSize>(
		surface.glmBaseVertices.size() * sizeof( vk_world_vertex_t ) );
	if ( offset > vk.skinnedVertexCapacity ||
		 byteCount > vk.skinnedVertexCapacity - offset )
	{
		return false;
	}

	vk_world_vertex_t *destination = reinterpret_cast<vk_world_vertex_t *>(
		vk.skinnedVertexMapped + offset );
	std::memcpy(
		destination, surface.glmBaseVertices.data(), static_cast<size_t>( byteCount ) );
	for ( size_t vertexIndex = 0; vertexIndex < surface.glmBaseVertices.size(); ++vertexIndex )
	{
		const float incoming = std::max(
			0.0f, DotProduct( destination[vertexIndex].normal, lighting.localDirection ) );
		for ( int channel = 0; channel < 3; ++channel )
		{
			destination[vertexIndex].color[channel] = std::min(
				255.0f, lighting.ambient[channel] + incoming * lighting.directed[channel] ) / 255.0f;
		}
		destination[vertexIndex].color[3] = 1.0f;
	}

	vk.skinnedVertexOffset = offset + byteCount;
	*vertexOffset = offset;
	return true;
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
	const std::vector<mdxaBone_t> *bones,
	const byte *entityColor,
	const refdef_t &refdef,
	const std::vector<vk_dynamic_light_t> &dynamicLights,
	const refEntity_t *entity = nullptr )
{
	const bool disintegrating = VK_DisintegrationMode( entity ) != 0;
	const bool timingEnabled = VK_TimingEnabled();
	const bool ewebModel =
		Q_stristr( model.name.c_str(), "eweb_model.glm" ) != nullptr;
	vk_entity_lighting_t entityLighting = {};
	float entityDiffuseColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	bool entityLightingCalculated = false;
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
		const bool usesLightingDiffuse = entity != nullptr &&
			VK_MaterialUsesLightingDiffuse( shader );
		if ( usesLightingDiffuse && !entityLightingCalculated )
		{
			VK_SetupEntityLighting( *entity, refdef, dynamicLights, &entityLighting );
			for ( int channel = 0; channel < 3; ++channel )
			{
				// Ghoul2's legacy fast path transforms normals on the CPU. A
				// representative hemispherical response retains scene and dlight
				// tint without repeating that work for every animated vertex.
				entityDiffuseColor[channel] = VK_ClampValue(
					( entityLighting.ambient[channel] +
					  entityLighting.directed[channel] * 0.25f ) / 255.0f,
					0.0f,
					1.0f );
			}
			entityLightingCalculated = true;
		}
		VkBuffer vertexBuffer = surface.vertexBuffer;
		VkDeviceSize vertexOffset = 0;
		const std::chrono::steady_clock::time_point skinBegin = timingEnabled
			? std::chrono::steady_clock::now()
			: std::chrono::steady_clock::time_point{};
		if ( ghoul != nullptr && bones != nullptr &&
			 VK_StreamSkinnedGLMSurface(
				model,
				surface,
				*ghoul,
				sceneTime,
				*bones,
				entity,
				&vertexOffset ) )
		{
			vertexBuffer = vk.skinnedVertexBuffer;
		}
		else if ( model.type == VK_MODEL_MD3 &&
			 usesLightingDiffuse )
		{
			if ( VK_StreamLitModelSurface( surface, entityLighting, &vertexOffset ) )
			{
				vertexBuffer = vk.skinnedVertexBuffer;
				if ( vk.loggedDiffuseModels < 8 )
				{
					ri.Printf( PRINT_ALL,
						"rd-vulkan-model-lighting: model=%s shader=%d "
						"origin=(%.1f %.1f %.1f) ambient=(%.1f %.1f %.1f) "
						"directed=(%.1f %.1f %.1f) localDir=(%.3f %.3f %.3f)\n",
						model.name.c_str(), shader,
						entity->origin[0], entity->origin[1], entity->origin[2],
						entityLighting.ambient[0], entityLighting.ambient[1], entityLighting.ambient[2],
						entityLighting.directed[0], entityLighting.directed[1], entityLighting.directed[2],
						entityLighting.localDirection[0], entityLighting.localDirection[1],
						entityLighting.localDirection[2] );
					++vk.loggedDiffuseModels;
				}
			}
		}
		if ( timingEnabled )
		{
			vk.timingModelSkinTotalMs += std::chrono::duration<double, std::milli>(
				std::chrono::steady_clock::now() - skinBegin ).count();
		}
		VkBuffer indexBuffer = surface.indexBuffer;
		uint32_t indexCount = surface.indexCount;
		const std::chrono::steady_clock::time_point submitBegin = timingEnabled
			? std::chrono::steady_clock::now()
			: std::chrono::steady_clock::time_point{};
		vkCmdBindVertexBuffers( vk.commandBuffer, 0, 1, &vertexBuffer, &vertexOffset );
		vkCmdBindIndexBuffer( vk.commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32 );
		if ( pass == VK_WORLD_PASS_FOG )
		{
			drawCount += VK_RecordBoundIndexedFog(
				shader, indexCount, 0, boundPipeline, boundTexture );
		}
		else
		{
			const float *diffuseColor = usesLightingDiffuse && model.type != VK_MODEL_MD3
				? entityDiffuseColor
				: nullptr;
			VkPipeline opaquePipelineOverride = VK_NULL_HANDLE;
			if ( ewebModel && vk.ewebCullCvar != nullptr )
			{
				const int requestedMode = vk.ewebCullCvar->integer;
				if ( requestedMode == 1 )
				{
					opaquePipelineOverride = vk.worldBackCullPipeline;
				}
				else if ( requestedMode == 2 )
				{
					opaquePipelineOverride = vk.worldFrontCullPipeline;
				}
				static int loggedMode = std::numeric_limits<int>::min();
				if ( loggedMode != requestedMode )
				{
					loggedMode = requestedMode;
					ri.Printf( PRINT_ALL,
						"rd-vulkan-eweb-cull: mode=%d "
						"(0=two-sided 1=back 2=front)\n",
						requestedMode );
				}
			}
			drawCount += VK_RecordBoundIndexedShader(
				shader, 2, true, indexCount, 0, pass, boundPipeline, boundTexture,
				entityColor, disintegrating, -1, true, nullptr, diffuseColor,
				opaquePipelineOverride );
		}
		if ( timingEnabled )
		{
			vk.timingModelSubmitTotalMs += std::chrono::duration<double, std::milli>(
				std::chrono::steady_clock::now() - submitBegin ).count();
		}
	}
	if ( pass == VK_WORLD_PASS_TRANSLUCENT && ghoul != nullptr && model.type == VK_MODEL_GLM )
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

static bool VK_Ghoul2SegmentTriangle(
	const vec3_t start,
	const vec3_t end,
	const vec3_t a,
	const vec3_t b,
	const vec3_t c,
	vec3_t hitPoint,
	vec3_t hitNormal,
	float *face,
	float *barycentricI,
	float *barycentricJ )
{
	constexpr float parallelEpsilon = 1.0e-10f;
	vec3_t edgeAB;
	vec3_t edgeAC;
	vec3_t ray;
	VectorSubtract( b, a, edgeAB );
	VectorSubtract( c, a, edgeAC );
	CrossProduct( edgeAB, edgeAC, hitNormal );
	VectorSubtract( end, start, ray );
	*face = DotProduct( ray, hitNormal );
	if ( std::fabs( *face ) < parallelEpsilon )
	{
		return false;
	}

	vec3_t toPlane;
	VectorSubtract( a, start, toPlane );
	const float distance = DotProduct( toPlane, hitNormal ) / *face;
	if ( distance < 0.0f || distance > 1.0f )
	{
		return false;
	}
	VectorMA( start, distance, ray, hitPoint );

	vec3_t v0;
	vec3_t v1;
	vec3_t v2;
	VectorSubtract( b, a, v0 );
	VectorSubtract( c, a, v1 );
	VectorSubtract( hitPoint, a, v2 );
	const float d00 = DotProduct( v0, v0 );
	const float d01 = DotProduct( v0, v1 );
	const float d11 = DotProduct( v1, v1 );
	const float d20 = DotProduct( v2, v0 );
	const float d21 = DotProduct( v2, v1 );
	const float denominator = d00 * d11 - d01 * d01;
	if ( std::fabs( denominator ) < parallelEpsilon )
	{
		return false;
	}
	const float weightB = ( d11 * d20 - d01 * d21 ) / denominator;
	const float weightC = ( d00 * d21 - d01 * d20 ) / denominator;
	const float weightA = 1.0f - weightB - weightC;
	constexpr float edgeEpsilon = -1.0e-5f;
	if ( weightA < edgeEpsilon || weightB < edgeEpsilon || weightC < edgeEpsilon )
	{
		return false;
	}

	*barycentricI = weightA;
	*barycentricJ = weightB;
	VectorNormalize( hitNormal );
	return true;
}

static void VK_Ghoul2WorldPoint(
	const vec3_t local,
	const vec3_t axis[3],
	const vec3_t position,
	const vec3_t scale,
	vec3_t world )
{
	const float scaleX = scale[0] != 0.0f ? scale[0] : 1.0f;
	const float scaleY = scale[1] != 0.0f ? scale[1] : 1.0f;
	const float scaleZ = scale[2] != 0.0f ? scale[2] : 1.0f;
	for ( int component = 0; component < 3; ++component )
	{
		world[component] = position[component] +
			axis[0][component] * local[0] * scaleX +
			axis[1][component] * local[1] * scaleY +
			axis[2][component] * local[2] * scaleZ;
	}
}

void VK_Backend_Ghoul2CollisionDetect(
	CCollisionRecord *collisionRecords,
	CGhoul2Info_v &ghoul2,
	const vec3_t angles,
	const vec3_t position,
	int frameNumber,
	int entityNumber,
	const vec3_t rayStart,
	const vec3_t rayEnd,
	const vec3_t scale,
	EG2_Collision collisionType,
	float radius )
{
	if ( collisionRecords == nullptr || !ghoul2.IsValid() || collisionType == G2_NOCOLLIDE )
	{
		return;
	}

	vec3_t axis[3];
	AnglesToAxis( angles, axis );
	std::array<std::array<float, 2>, 9> traceOffsets = {};
	int traceOffsetCount = 1;
	vec3_t traceBasis[2] = {};
	if ( radius > 0.1f )
	{
		vec3_t rayDirection;
		vec3_t helper = { 0.0f, 0.0f, 1.0f };
		VectorSubtract( rayEnd, rayStart, rayDirection );
		VectorNormalize( rayDirection );
		if ( std::fabs( rayDirection[2] ) > 0.9f )
		{
			VectorSet( helper, 0.0f, 1.0f, 0.0f );
		}
		CrossProduct( rayDirection, helper, traceBasis[0] );
		VectorNormalize( traceBasis[0] );
		CrossProduct( rayDirection, traceBasis[0], traceBasis[1] );
		VectorNormalize( traceBasis[1] );
		constexpr float diagonal = 0.70710678f;
		traceOffsets = { {
			{ 0.0f, 0.0f }, { 1.0f, 0.0f }, { -1.0f, 0.0f },
			{ 0.0f, 1.0f }, { 0.0f, -1.0f },
			{ diagonal, diagonal }, { diagonal, -diagonal },
			{ -diagonal, diagonal }, { -diagonal, -diagonal },
		} };
		traceOffsetCount = static_cast<int>( traceOffsets.size() );
	}

	std::vector<byte> hierarchyStates( static_cast<size_t>( ghoul2.size() ), 0 );
	std::vector<const std::vector<mdxaBone_t> *> hierarchyBones(
		static_cast<size_t>( ghoul2.size() ), nullptr );
	bool stopAfterHit = false;
	for ( int modelIndex = 0;
		  modelIndex < ghoul2.size() && !stopAfterHit;
		  ++modelIndex )
	{
		const CGhoul2Info &ghoul = ghoul2[modelIndex];
		if ( ghoul.mModelindex < 0 || !ghoul.mValid ||
			 ( ghoul.mFlags & GHOUL2_NOCOLLIDE ) != 0 )
		{
			continue;
		}
		const vk_model_t *model = VK_ModelForHandle( ghoul.mModel );
		const std::vector<mdxaBone_t> *bones =
			VK_ResolveGhoul2HierarchyBones(
				ghoul2,
				modelIndex,
				frameNumber,
				&hierarchyStates,
				&hierarchyBones );
		if ( model == nullptr || model->type != VK_MODEL_GLM || bones == nullptr )
		{
			continue;
		}

		for ( const vk_model_surface_t &surface : model->surfaces )
		{
			if ( !VK_GLMShouldDrawSurface( *model, surface, &ghoul ) ||
				 surface.glmVertices.empty() || surface.glmIndices.size() < 3 )
			{
				continue;
			}
			std::vector<std::array<float, 3>> vertices( surface.glmVertices.size() );
			bool validSurface = true;
			for ( size_t vertexIndex = 0; vertexIndex < vertices.size(); ++vertexIndex )
			{
				vec3_t local;
				if ( !VK_SkinGLMVertex(
						surface, *bones, static_cast<uint32_t>( vertexIndex ), local ) )
				{
					validSurface = false;
					break;
				}
				VK_Ghoul2WorldPoint(
					local, axis, position, scale, vertices[vertexIndex].data() );
			}
			if ( !validSurface )
			{
				continue;
			}

			for ( size_t triangleIndex = 0;
				  triangleIndex + 2 < surface.glmIndices.size();
				  triangleIndex += 3 )
			{
				const uint32_t indices[3] = {
					surface.glmIndices[triangleIndex],
					surface.glmIndices[triangleIndex + 1],
					surface.glmIndices[triangleIndex + 2],
				};
				if ( indices[0] >= vertices.size() ||
					 indices[1] >= vertices.size() || indices[2] >= vertices.size() )
				{
					continue;
				}

				bool triangleHit = false;
				vec3_t closestPoint = {};
				vec3_t closestNormal = {};
				float closestDistance = 100000.0f;
				float closestFace = 0.0f;
				float closestBarycentricI = 0.0f;
				float closestBarycentricJ = 0.0f;
				for ( int offsetIndex = 0; offsetIndex < traceOffsetCount; ++offsetIndex )
				{
					vec3_t offset;
					for ( int component = 0; component < 3; ++component )
					{
						offset[component] = radius * (
							traceBasis[0][component] * traceOffsets[offsetIndex][0] +
							traceBasis[1][component] * traceOffsets[offsetIndex][1] );
					}
					vec3_t offsetStart;
					vec3_t offsetEnd;
					VectorAdd( rayStart, offset, offsetStart );
					VectorAdd( rayEnd, offset, offsetEnd );
					vec3_t hitPoint;
					vec3_t hitNormal;
					float face = 0.0f;
					float barycentricI = 0.0f;
					float barycentricJ = 0.0f;
					if ( !VK_Ghoul2SegmentTriangle(
							offsetStart,
							offsetEnd,
							vertices[indices[0]].data(),
							vertices[indices[1]].data(),
							vertices[indices[2]].data(),
							hitPoint,
							hitNormal,
							&face,
							&barycentricI,
							&barycentricJ ) )
					{
						continue;
					}
					vec3_t distanceVector;
					VectorSubtract( hitPoint, rayStart, distanceVector );
					const float distance = VectorLength( distanceVector );
					if ( distance < closestDistance )
					{
						triangleHit = true;
						closestDistance = distance;
						closestFace = face;
						closestBarycentricI = barycentricI;
						closestBarycentricJ = barycentricJ;
						VectorCopy( hitPoint, closestPoint );
						VectorCopy( hitNormal, closestNormal );
					}
				}
				if ( !triangleHit )
				{
					continue;
				}

				int recordIndex = 0;
				while ( recordIndex < MAX_G2_COLLISIONS &&
						collisionRecords[recordIndex].mEntityNum != -1 )
				{
					++recordIndex;
				}
				if ( recordIndex >= MAX_G2_COLLISIONS )
				{
					stopAfterHit = true;
					break;
				}
				CCollisionRecord &record = collisionRecords[recordIndex];
				record.mDistance = closestDistance;
				record.mEntityNum = entityNumber;
				record.mModelIndex = modelIndex;
				record.mPolyIndex = static_cast<int>( triangleIndex / 3 );
				record.mSurfaceIndex = surface.modelSurfaceIndex;
				VectorCopy( closestPoint, record.mCollisionPosition );
				VectorCopy( closestNormal, record.mCollisionNormal );
				record.mFlags = closestFace > 0.0f ? G2_FRONTFACE : G2_BACKFACE;
				record.mMaterial = 0;
				record.mLocation = 0;
				record.mBarycentricI = closestBarycentricI;
				record.mBarycentricJ = closestBarycentricJ;
				if ( collisionType == G2_RETURNONHIT )
				{
					stopAfterHit = true;
					break;
				}
			}
			if ( stopAfterHit )
			{
				break;
			}
		}
	}

	std::sort(
		collisionRecords,
		collisionRecords + MAX_G2_COLLISIONS,
		[]( const CCollisionRecord &left, const CCollisionRecord &right ) {
			return left.mDistance < right.mDistance;
		} );
	static bool loggedCollision = false;
	if ( !loggedCollision )
	{
		ri.Printf( PRINT_ALL,
			"rd-vulkan-ghoul2: animated triangle collision active\n" );
		loggedCollision = true;
	}
}

static const char *VK_TextureNameForHandle( qhandle_t handle );

static bool VK_InlineModelUsesMaterial( const vk_model_t &model, const char *name )
{
	if ( model.inlineModelIndex < 0 ||
		 static_cast<size_t>( model.inlineModelIndex ) >= vk.world.inlineModels.size() )
	{
		return false;
	}

	const vk_world_inline_model_t &inlineModel = vk.world.inlineModels[model.inlineModelIndex];
	for ( uint32_t i = 0; i < inlineModel.surfaceCount; ++i )
	{
		const uint32_t surfaceIndex = inlineModel.firstSurface + i;
		if ( surfaceIndex >= vk.world.surfaceBatchIndex.size() )
		{
			continue;
		}
		const uint32_t batchIndex = vk.world.surfaceBatchIndex[surfaceIndex];
		if ( batchIndex < vk.world.batches.size() &&
			 Q_stricmp( VK_TextureNameForHandle( vk.world.batches[batchIndex].shader ), name ) == 0 )
		{
			return true;
		}
	}
	return false;
}

static refEntity_t VK_AdjustInlineVideoEntity(
	const vk_model_t &model,
	const refEntity_t &entity )
{
	refEntity_t adjusted = entity;
	if ( !VK_InlineModelUsesMaterial( model, "textures/video/mon_mothma_back" ) )
	{
		return adjusted;
	}

	const vk_world_inline_model_t &inlineModel = vk.world.inlineModels[model.inlineModelIndex];
	vec3_t worldNormal;
	if ( VectorLength( entity.axis[0] ) > 0.0001f &&
		 VectorLength( entity.axis[1] ) > 0.0001f &&
		 VectorLength( entity.axis[2] ) > 0.0001f )
	{
		for ( int component = 0; component < 3; ++component )
		{
			worldNormal[component] =
				entity.axis[0][component] * inlineModel.facingNormal[0] +
				entity.axis[1][component] * inlineModel.facingNormal[1] +
				entity.axis[2][component] * inlineModel.facingNormal[2];
		}
	}
	else
	{
		VectorCopy( inlineModel.facingNormal, worldNormal );
	}
	if ( VectorNormalize( worldNormal ) > 0.0f )
	{
		// The reverse-angle hologram brush sits against the cockpit panel.
		// Pull it into the cabin while preserving its authored orientation.
		VectorMA( adjusted.origin, -5.0f, worldNormal, adjusted.origin );
		static bool loggedAdjustment = false;
		if ( !loggedAdjustment )
		{
			ri.Printf( PRINT_ALL,
				"rd-vulkan-video: shifted rear Mon Mothma hologram inline=%d "
				"origin=(%.1f %.1f %.1f)->(%.1f %.1f %.1f)\n",
				model.inlineModelIndex,
				entity.origin[0], entity.origin[1], entity.origin[2],
				adjusted.origin[0], adjusted.origin[1], adjusted.origin[2] );
			loggedAdjustment = true;
		}
	}
	return adjusted;
}

static void VK_RecordSceneModels(
	const float view[16],
	const float projection[16],
	vk_world_pass_t pass,
	const std::vector<refEntity_t> &entities,
	bool suppressThirdPerson,
	int sceneTime,
	const refdef_t &refdef,
	const std::vector<vk_dynamic_light_t> &dynamicLights )
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
	uint32_t culledModels = 0;
	uint32_t surfaceDraws = 0;
	int selectedVideoClient = -1;
	int selectedVideoInlineModel = -1;
	bool hasVideoInlineModels = false;
	int firstVideoInlineModel = std::numeric_limits<int>::max();
	int lastVideoInlineModel = -1;
	float selectedVideoDistanceSquared = std::numeric_limits<float>::max();
	for ( const refEntity_t &entity : entities )
	{
		if ( entity.reType != RT_MODEL )
		{
			continue;
		}
		const vk_model_t *model = VK_ModelForHandle( entity.hModel );
		if ( model == nullptr || model->type != VK_MODEL_INLINE_BSP ||
			 model->inlineModelIndex < 0 ||
			 static_cast<size_t>( model->inlineModelIndex ) >= vk.world.inlineModels.size() )
		{
			continue;
		}
		const refEntity_t adjustedEntity = VK_AdjustInlineVideoEntity( *model, entity );
		const vk_world_inline_model_t &inlineModel = vk.world.inlineModels[model->inlineModelIndex];
		const int videoClient = VK_InlineModelVideoClient( *model );
		if ( videoClient < 0 )
		{
			continue;
		}
		hasVideoInlineModels = true;
		firstVideoInlineModel = std::min( firstVideoInlineModel, model->inlineModelIndex );
		lastVideoInlineModel = std::max( lastVideoInlineModel, model->inlineModelIndex );
		if ( !VK_InlineModelIntersectsView( inlineModel, adjustedEntity, view, projection ) ||
			 !VK_InlineModelFacesView( inlineModel, adjustedEntity, view ) )
		{
			continue;
		}
		const float distanceSquared =
			VK_InlineModelViewDistanceSquared( inlineModel, adjustedEntity, view );
		if ( distanceSquared < selectedVideoDistanceSquared )
		{
			selectedVideoClient = videoClient;
			selectedVideoInlineModel = model->inlineModelIndex;
			selectedVideoDistanceSquared = distanceSquared;
		}
	}
	if ( hasVideoInlineModels && selectedVideoInlineModel < 0 )
	{
		// Suppress all video panels when every submitted candidate is culled or
		// back-facing. -1 remains the sentinel for scenes without video panels.
		selectedVideoInlineModel = -2;
	}
	if ( pass == VK_WORLD_PASS_OPAQUE && suppressThirdPerson && lastVideoInlineModel >= 0 &&
		 vk.loggedVideoSelectionChanges < 64 )
	{
		if ( selectedVideoClient != vk.loggedVideoSelectionClient ||
			 selectedVideoInlineModel != vk.loggedVideoSelectionInlineModel )
		{
			ri.Printf( PRINT_ALL,
				"rd-vulkan-video-select: client=%d inline=%d distance=%.2f time=%d\n",
				selectedVideoClient, selectedVideoInlineModel,
				std::sqrt( selectedVideoDistanceSquared ), sceneTime );
			vk.loggedVideoSelectionClient = selectedVideoClient;
			vk.loggedVideoSelectionInlineModel = selectedVideoInlineModel;
			++vk.loggedVideoSelectionChanges;
		}

		for ( const refEntity_t &entity : entities )
		{
			if ( entity.reType != RT_MODEL )
			{
				continue;
			}
			const vk_model_t *model = VK_ModelForHandle( entity.hModel );
			if ( model == nullptr || model->type != VK_MODEL_INLINE_BSP ||
				 model->inlineModelIndex < firstVideoInlineModel - 2 ||
				 model->inlineModelIndex > lastVideoInlineModel + 2 )
			{
				continue;
			}
			const std::array<float, 3> origin = {
				entity.origin[0], entity.origin[1], entity.origin[2],
			};
			const auto previous = vk.loggedVideoNeighborOrigins.find( model->inlineModelIndex );
			if ( previous != vk.loggedVideoNeighborOrigins.end() &&
				 std::fabs( previous->second[0] - origin[0] ) < 0.01f &&
				 std::fabs( previous->second[1] - origin[1] ) < 0.01f &&
				 std::fabs( previous->second[2] - origin[2] ) < 0.01f )
			{
				continue;
			}
			vk.loggedVideoNeighborOrigins[model->inlineModelIndex] = origin;
			ri.Printf( PRINT_ALL,
				"rd-vulkan-video-entity: model=%s inline=%d video=%d origin=(%.1f %.1f %.1f)\n",
				model->name.c_str(), model->inlineModelIndex,
				VK_InlineModelVideoClient( *model ), origin[0], origin[1], origin[2] );
			const vk_world_inline_model_t &inlineModel =
				vk.world.inlineModels[model->inlineModelIndex];
			std::vector<qhandle_t> shaders;
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
				const qhandle_t shader = vk.world.batches[batchIndex].shader;
				if ( std::find( shaders.begin(), shaders.end(), shader ) == shaders.end() )
				{
					shaders.push_back( shader );
				}
			}
			for ( const qhandle_t shader : shaders )
			{
				ri.Printf( PRINT_ALL, "rd-vulkan-video-entity:   material=%s\n",
					VK_TextureNameForHandle( shader ) );
			}
		}
	}
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
		if ( pass == VK_WORLD_PASS_OPAQUE && VK_TimingEnabled() )
		{
			++vk.timingModelCandidateTotal;
		}

		const vk_model_t *entityModel = VK_ModelForHandle( entity.hModel );
		if ( pass == VK_WORLD_PASS_OPAQUE && entityModel != nullptr &&
			 vk.fxModelAuditCvar != nullptr && vk.fxModelAuditCvar->integer != 0 &&
			 ( Q_stricmp( entityModel->name.c_str(), "models/chunks/generic/chunks_1.md3" ) == 0 ||
			   Q_stricmp( entityModel->name.c_str(), "models/chunks/generic/chunks_2.md3" ) == 0 ) &&
			 ( vk.fxModelAuditLastTime == std::numeric_limits<int>::min() ||
			   sceneTime - vk.fxModelAuditLastTime >= 100 ) )
		{
			ri.Printf(
				PRINT_ALL,
				"rd-vulkan-fx-model-audit: time=%d model=%s origin=(%.2f %.2f %.2f) "
				"axisLength=(%.4f %.4f %.4f) radius=%.4f rgba=(%u %u %u %u) "
				"renderfx=0x%x nonNormalized=%d\n",
				sceneTime, entityModel->name.c_str(),
				entity.origin[0], entity.origin[1], entity.origin[2],
				VectorLength( entity.axis[0] ), VectorLength( entity.axis[1] ),
				VectorLength( entity.axis[2] ), entity.radius,
				entity.shaderRGBA[0], entity.shaderRGBA[1],
				entity.shaderRGBA[2], entity.shaderRGBA[3],
				entity.renderfx, entity.nonNormalizedAxes );
			vk.fxModelAuditLastTime = sceneTime;
		}
		const bool timingEnabled = VK_TimingEnabled();
		const std::chrono::steady_clock::time_point cullBegin = timingEnabled
			? std::chrono::steady_clock::now()
			: std::chrono::steady_clock::time_point{};
		const bool modelCulled =
			( vk.modelCullCvar == nullptr || vk.modelCullCvar->integer != 0 ) &&
			 ( entityModel == nullptr || entityModel->type != VK_MODEL_INLINE_BSP ) &&
			 !VK_ModelEntityIntersectsView( entity, entityModel, view, projection );
		if ( timingEnabled )
		{
			vk.timingModelCullTotalMs += std::chrono::duration<double, std::milli>(
				std::chrono::steady_clock::now() - cullBegin ).count();
		}
		if ( modelCulled )
		{
			++culledModels;
			if ( pass == VK_WORLD_PASS_OPAQUE && VK_TimingEnabled() )
			{
				++vk.timingModelCulledTotal;
			}
			continue;
		}

		if ( entity.ghoul2 != nullptr && entity.ghoul2->IsValid() )
		{
			if ( suppressThirdPerson && pass == VK_WORLD_PASS_OPAQUE )
			{
				VK_LogStandaloneWeaponTransform( entity );
			}
			VK_PushModelMvp( view, projection, entity );
			bool drewGhoul2 = false;
			bool handledGhoul2 = false;
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
				if ( ghoulModel == nullptr ||
					 ( ghoulModel->type != VK_MODEL_GLM && ghoulModel->type != VK_MODEL_MD3 ) )
				{
					continue;
				}
				handledGhoul2 = true;
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
				if ( ghoulModel->type == VK_MODEL_GLM )
				{
					VK_LogGhoul2RenderAudit( *ghoulModel, ghoul, skinHandle );
				}
				const std::vector<mdxaBone_t> *bonePointer = nullptr;
				if ( ghoulModel->type == VK_MODEL_GLM )
				{
					const std::chrono::steady_clock::time_point boneBegin = timingEnabled
						? std::chrono::steady_clock::now()
						: std::chrono::steady_clock::time_point{};
					bonePointer = VK_ResolveGhoul2HierarchyBones(
						*entity.ghoul2,
						i,
						sceneTime,
						&hierarchyStates,
						&hierarchyBones );
					if ( timingEnabled )
					{
						vk.timingModelBoneTotalMs += std::chrono::duration<double, std::milli>(
							std::chrono::steady_clock::now() - boneBegin ).count();
					}
				}
				if ( pass == VK_WORLD_PASS_OPAQUE && vk.loggedMedpacEntities < 12 &&
					 Q_stricmp( ghoulModel->name.c_str(), "models/items/medpac.md3" ) == 0 )
				{
					ri.Printf( PRINT_ALL,
						"rd-vulkan-item-audit: ghoul2-md3 medpac origin=(%.1f %.1f %.1f) "
						"surfaces=%zu flags=0x%x\n",
						entity.origin[0], entity.origin[1], entity.origin[2],
						ghoulModel->surfaces.size(), ghoul.mFlags );
					++vk.loggedMedpacEntities;
				}
				const uint32_t draws = VK_RecordMD3ModelSurfaces(
					*ghoulModel,
					pass,
					&boundPipeline,
					&boundTexture,
					shaderOverride,
					&ghoul,
					skinHandle,
					sceneTime,
					bonePointer,
					entity.shaderRGBA,
					refdef,
					dynamicLights,
					&entity );
				if ( draws > 0 )
				{
					drewGhoul2 = true;
					surfaceDraws += draws;
				}
			}
			if ( handledGhoul2 )
			{
				// Hidden surfaces are still an authoritative Ghoul2 result for this pass.
				// Falling through would redraw the static hModel in its bind pose.
				if ( drewGhoul2 )
				{
					++glmEntities;
				}
				continue;
			}
		}

		const vk_model_t *model = entityModel;
		if ( model == nullptr )
		{
			++unsupportedEntities;
			continue;
		}
		if ( pass == VK_WORLD_PASS_OPAQUE && vk.loggedMedpacEntities < 12 &&
			Q_stricmp( model->name.c_str(), "models/items/medpac.md3" ) == 0 )
		{
			ri.Printf( PRINT_ALL,
				"rd-vulkan-item-audit: medpac origin=(%.1f %.1f %.1f) type=%d "
				"surfaces=%zu customShader=%d renderfx=0x%x\n",
				entity.origin[0], entity.origin[1], entity.origin[2],
				static_cast<int>( model->type ), model->surfaces.size(),
				entity.customShader, entity.renderfx );
			++vk.loggedMedpacEntities;
		}

		if ( model->type == VK_MODEL_INLINE_BSP )
		{
			if ( model->inlineModelIndex < 0 ||
				 static_cast<size_t>( model->inlineModelIndex ) >= vk.world.inlineModels.size() )
			{
				++unsupportedEntities;
				continue;
			}
			const refEntity_t adjustedEntity = VK_AdjustInlineVideoEntity( *model, entity );
			if ( !VK_InlineModelIntersectsView(
					 vk.world.inlineModels[model->inlineModelIndex], adjustedEntity, view, projection ) )
			{
				continue;
			}
			VK_PushModelMvp( view, projection, adjustedEntity );
			const uint32_t draws = VK_RecordInlineModelSurfaces(
				*model,
				pass,
				&boundPipeline,
				&boundTexture,
				selectedVideoInlineModel,
				adjustedEntity,
				dynamicLights );
			if ( draws > 0 )
			{
				++inlineEntities;
				surfaceDraws += draws;
			}
		}
		else if ( model->type == VK_MODEL_MD3 )
		{
			VK_PushModelMvp( view, projection, entity );
			const uint32_t draws = VK_RecordMD3ModelSurfaces(
				*model,
				pass,
				&boundPipeline,
				&boundTexture,
				entity.customShader,
				nullptr,
				entity.customSkin,
				sceneTime,
				nullptr,
				entity.shaderRGBA,
				refdef,
				dynamicLights,
				&entity );
			if ( draws > 0 )
			{
				++md3Entities;
				surfaceDraws += draws;
			}
		}
		else if ( model->type == VK_MODEL_GLM )
		{
			VK_PushModelMvp( view, projection, entity );
			const uint32_t draws = VK_RecordMD3ModelSurfaces(
				*model,
				pass,
				&boundPipeline,
				&boundTexture,
				entity.customShader,
				nullptr,
				entity.customSkin,
				sceneTime,
				nullptr,
				entity.shaderRGBA,
				refdef,
				dynamicLights,
				&entity );
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
	if ( pass == VK_WORLD_PASS_OPAQUE && VK_TimingEnabled() )
	{
		vk.timingModelDrawTotal += surfaceDraws;
	}

	if ( !vk.loggedFirstModelDraw && modelEntities > 0 && pass == VK_WORLD_PASS_TRANSLUCENT )
	{
		ri.Printf( PRINT_ALL,
			"rd-vulkan-scene: translucent model pass: entities=%u culled=%u md3=%u glm=%u inline=%u unsupported=%u stageDraws=%u\n",
			modelEntities, culledModels, md3Entities, glmEntities, inlineEntities,
			unsupportedEntities, surfaceDraws );
		vk.loggedFirstModelDraw = true;
	}
}

struct vk_dynamic_effect_batch_t
{
	qhandle_t shader;
	std::vector<vk_world_vertex_t> vertices;
};

static vk_dynamic_effect_batch_t *VK_DynamicEffectBatchForShader(
	std::vector<vk_dynamic_effect_batch_t> *batches,
	qhandle_t shader )
{
	for ( vk_dynamic_effect_batch_t &batch : *batches )
	{
		if ( batch.shader == shader )
		{
			return &batch;
		}
	}
	batches->push_back( { shader, {} } );
	return &batches->back();
}

static void VK_WriteDynamicEffectVertex(
	vk_world_vertex_t *vertex,
	const vec3_t position,
	const byte color[4],
	float u,
	float v )
{
	*vertex = {};
	VectorCopy( position, vertex->position );
	for ( int component = 0; component < 4; ++component )
	{
		vertex->color[component] = color[component] / 255.0f;
	}
	vertex->uv[0] = u;
	vertex->uv[1] = v;
}

static void VK_AppendDynamicEffectTriangle(
	vk_dynamic_effect_batch_t *batch,
	const vec3_t p0,
	const vec3_t p1,
	const vec3_t p2,
	const byte c0[4],
	const byte c1[4],
	const byte c2[4],
	const float uv0[2],
	const float uv1[2],
	const float uv2[2] )
{
	const size_t first = batch->vertices.size();
	batch->vertices.resize( first + 3 );
	VK_WriteDynamicEffectVertex( &batch->vertices[first + 0], p0, c0, uv0[0], uv0[1] );
	VK_WriteDynamicEffectVertex( &batch->vertices[first + 1], p1, c1, uv1[0], uv1[1] );
	VK_WriteDynamicEffectVertex( &batch->vertices[first + 2], p2, c2, uv2[0], uv2[1] );
}

static void VK_AppendDynamicEffectQuad(
	vk_dynamic_effect_batch_t *batch,
	const vec3_t origin,
	const vec3_t left,
	const vec3_t up,
	const byte color[4] )
{
	vec3_t corners[4];
	VectorAdd( origin, left, corners[0] );
	VectorAdd( corners[0], up, corners[0] );
	VectorSubtract( origin, left, corners[1] );
	VectorAdd( corners[1], up, corners[1] );
	VectorSubtract( origin, left, corners[2] );
	VectorSubtract( corners[2], up, corners[2] );
	VectorAdd( origin, left, corners[3] );
	VectorSubtract( corners[3], up, corners[3] );
	const float uvs[4][2] = {
		{ 0.0f, 0.0f }, { 1.0f, 0.0f },
		{ 1.0f, 1.0f }, { 0.0f, 1.0f },
	};
	VK_AppendDynamicEffectTriangle(
		batch, corners[0], corners[1], corners[3],
		color, color, color, uvs[0], uvs[1], uvs[3] );
	VK_AppendDynamicEffectTriangle(
		batch, corners[3], corners[1], corners[2],
		color, color, color, uvs[3], uvs[1], uvs[2] );
}

static void VK_AppendDynamicEffectLine(
	vk_dynamic_effect_batch_t *batch,
	const vec3_t start,
	const vec3_t end,
	const vec3_t viewOrigin,
	float startRadius,
	float endRadius,
	const byte color[4],
	float textureStart = 0.0f,
	float textureEnd = 1.0f )
{
	vec3_t startToView;
	vec3_t endToView;
	vec3_t right;
	VectorSubtract( start, viewOrigin, startToView );
	VectorSubtract( end, viewOrigin, endToView );
	CrossProduct( startToView, endToView, right );
	if ( VectorNormalize( right ) < 0.0001f )
	{
		vec3_t direction;
		vec3_t fallbackUp;
		VectorSubtract( end, start, direction );
		if ( VectorNormalize( direction ) < 0.0001f )
		{
			return;
		}
		MakeNormalVectors( direction, right, fallbackUp );
	}

	vec3_t corners[4];
	VectorMA( start, startRadius, right, corners[0] );
	VectorMA( start, -startRadius, right, corners[1] );
	VectorMA( end, endRadius, right, corners[2] );
	VectorMA( end, -endRadius, right, corners[3] );
	const float uvs[4][2] = {
		{ 0.0f, textureStart }, { 1.0f, textureStart },
		{ 0.0f, textureEnd }, { 1.0f, textureEnd },
	};
	VK_AppendDynamicEffectTriangle(
		batch, corners[0], corners[1], corners[2],
		color, color, color, uvs[0], uvs[1], uvs[2] );
	VK_AppendDynamicEffectTriangle(
		batch, corners[2], corners[1], corners[3],
		color, color, color, uvs[2], uvs[1], uvs[3] );
}

static bool VK_DynamicShaderUsesPass( qhandle_t shader, vk_world_pass_t pass )
{
	if ( shader > 0 && static_cast<size_t>( shader ) < vk.materials.size() &&
		 !vk.materials[shader].stages.empty() )
	{
		for ( const vk_material_stage_t &stage : vk.materials[shader].stages )
		{
			if ( stage.surfaceSprite.type == VK_SURFACE_SPRITE_NONE && !stage.lightmap &&
				 ( ( pass == VK_WORLD_PASS_OPAQUE ) == ( stage.blendMode == VK_BLEND_OPAQUE ) ) )
			{
				return true;
			}
		}
		return false;
	}
	return pass == VK_WORLD_PASS_TRANSLUCENT;
}

static bool VK_IsNamedTexture( qhandle_t handle, const char *name )
{
	for ( const vk_texture_name_t &registered : vk.textureNames )
	{
		if ( registered.handle == handle && Q_stricmp( registered.name.c_str(), name ) == 0 )
		{
			return true;
		}
	}
	return false;
}

static bool VK_MaterialHasGlow( qhandle_t shader )
{
	if ( shader <= 0 || static_cast<size_t>( shader ) >= vk.materials.size() )
	{
		return false;
	}
	for ( const vk_material_stage_t &stage : vk.materials[shader].stages )
	{
		if ( stage.glow )
		{
			return true;
		}
	}
	return false;
}

static void VK_BuildDynamicEffectBatches(
	const refdef_t &refdef,
	const std::vector<refEntity_t> &entities,
	const std::vector<vk_scene_poly_t> &polys,
	vk_world_pass_t pass,
	bool suppressThirdPerson,
	std::vector<vk_dynamic_effect_batch_t> *batches,
	uint32_t typeCounts[RT_MAX_REF_ENTITY_TYPE] )
{
	for ( const refEntity_t &entity : entities )
	{
		if ( suppressThirdPerson && ( entity.renderfx & RF_THIRD_PERSON ) != 0 )
		{
			continue;
		}
		if ( entity.reType != RT_SPRITE && entity.reType != RT_SABER_GLOW &&
			 entity.reType != RT_ORIENTED_QUAD && entity.reType != RT_LINE &&
			 entity.reType != RT_ELECTRICITY && entity.reType != RT_BEAM )
		{
			continue;
		}

		const qhandle_t shader = entity.customShader > 0 ? entity.customShader : 1;
		if ( !VK_DynamicShaderUsesPass( shader, pass ) )
		{
			continue;
		}
		vec3_t lineDelta = {};
		if ( entity.reType == RT_LINE )
		{
			VectorSubtract( entity.oldorigin, entity.origin, lineDelta );
		}
		if ( !vk.loggedScepterLine && entity.reType == RT_LINE &&
			 VectorLengthSquared( lineDelta ) > 400.0f * 400.0f &&
			 VK_IsNamedTexture( shader, "gfx/effects/sabers/orange_line" ) )
		{
			ri.Printf( PRINT_ALL,
				"rd-vulkan-scepter: line submitted pass=%d start=(%.1f %.1f %.1f) "
				"end=(%.1f %.1f %.1f) radius=%.2f rgba=(%u %u %u %u)\n",
				static_cast<int>( pass ),
				entity.origin[0], entity.origin[1], entity.origin[2],
				entity.oldorigin[0], entity.oldorigin[1], entity.oldorigin[2],
				entity.radius,
				entity.shaderRGBA[0], entity.shaderRGBA[1],
				entity.shaderRGBA[2], entity.shaderRGBA[3] );
			vk.loggedScepterLine = true;
		}
		if ( !vk.loggedForcePushEffect &&
			 VK_IsNamedTexture( shader, "gfx/effects/forcePush" ) )
		{
			const size_t stageCount = static_cast<size_t>( shader ) < vk.materials.size()
				? vk.materials[shader].stages.size() : 0;
			ri.Printf( PRINT_ALL,
				"rd-vulkan-fx: force push sprite submitted pass=%d radius=%.2f "
				"rgba=(%u %u %u %u) origin=(%.1f %.1f %.1f) stages=%zu\n",
				static_cast<int>( pass ), entity.radius,
				entity.shaderRGBA[0], entity.shaderRGBA[1],
				entity.shaderRGBA[2], entity.shaderRGBA[3],
				entity.origin[0], entity.origin[1], entity.origin[2], stageCount );
			vk.loggedForcePushEffect = true;
		}
		vk_dynamic_effect_batch_t *batch = VK_DynamicEffectBatchForShader( batches, shader );
		++typeCounts[entity.reType];
		const float authoredGlowRadiusScale = VK_MaterialHasGlow( shader )
			? ( vk.glowRadiusCvar != nullptr
				? std::max( 1.0f, std::min( 2.0f, vk.glowRadiusCvar->value ) )
				: 1.12f )
			: 1.0f;

		if ( entity.reType == RT_SPRITE || entity.reType == RT_SABER_GLOW )
		{
			if ( !std::isfinite( entity.radius ) || entity.radius <= 0.0f )
			{
				continue;
			}
			auto appendBillboard = [&]( const vec3_t origin, float radius, float rotation )
			{
				vec3_t left;
				vec3_t up;
				const float angle = DEG2RAD( rotation );
				const float sine = std::sin( angle );
				const float cosine = std::cos( angle );
				VectorScale( refdef.viewaxis[1], cosine * radius, left );
				VectorMA( left, -sine * radius, refdef.viewaxis[2], left );
				VectorScale( refdef.viewaxis[2], cosine * radius, up );
				VectorMA( up, sine * radius, refdef.viewaxis[1], up );
				VK_AppendDynamicEffectQuad(
					batch, origin, left, up, entity.shaderRGBA );
			};

			if ( entity.reType == RT_SABER_GLOW &&
				 std::isfinite( entity.saberLength ) && entity.saberLength > 0.0f &&
				 VectorLength( entity.axis[0] ) > 0.0001f )
			{
				float glowRadius = entity.radius * authoredGlowRadiusScale;
				const float spacing = std::max( entity.radius * 0.65f, 0.05f );
				for ( float distance = entity.saberLength; distance > 0.0f; distance -= spacing )
				{
					vec3_t glowOrigin;
					VectorMA( entity.origin, distance, entity.axis[0], glowOrigin );
					appendBillboard( glowOrigin, glowRadius, 0.0f );
					glowRadius += 0.017f;
				}

				const float pulse = 0.125f *
					( 1.0f + std::sin( refdef.time * 0.013f + entity.origin[0] * 0.17f ) );
				appendBillboard(
					entity.origin, ( 5.5f + pulse ) * authoredGlowRadiusScale, 0.0f );
			}
			else
			{
				appendBillboard(
					entity.origin, entity.radius * authoredGlowRadiusScale, entity.rotation );
			}
		}
		else if ( entity.reType == RT_ORIENTED_QUAD )
		{
			if ( !std::isfinite( entity.radius ) || entity.radius <= 0.0f ||
				 VectorLength( entity.axis[0] ) < 0.0001f )
			{
				continue;
			}
			vec3_t left;
			vec3_t up;
			MakeNormalVectors( entity.axis[0], left, up );
			if ( entity.rotation != 0.0f )
			{
				vec3_t rotatedLeft;
				vec3_t rotatedUp;
				const float angle = DEG2RAD( entity.rotation );
				const float sine = std::sin( angle );
				const float cosine = std::cos( angle );
				VectorScale( left, cosine, rotatedLeft );
				VectorMA( rotatedLeft, -sine, up, rotatedLeft );
				VectorScale( up, cosine, rotatedUp );
				VectorMA( rotatedUp, sine, left, rotatedUp );
				VectorCopy( rotatedLeft, left );
				VectorCopy( rotatedUp, up );
			}
			VectorScale( left, entity.radius, left );
			VectorScale( up, entity.radius, up );
			VK_AppendDynamicEffectQuad(
				batch, entity.origin, left, up, entity.shaderRGBA );
		}
		else if ( entity.reType == RT_ELECTRICITY )
		{
			vec3_t end;
			VectorCopy( entity.oldorigin, end );
			vec3_t direction;
			VectorSubtract( end, entity.origin, direction );
			float distance = VectorNormalize( direction );
			if ( ( entity.renderfx & RF_GROW ) != 0 && entity.angles[1] > 0.0f )
			{
				const float growth = VK_ClampValue(
					1.0f - ( entity.endTime - refdef.time ) / entity.angles[1], 0.0f, 1.0f );
				VectorMA( entity.origin, distance * growth, direction, end );
				distance *= growth;
			}
			if ( distance <= 0.001f )
			{
				continue;
			}
			vec3_t side;
			vec3_t vertical;
			MakeNormalVectors( direction, side, vertical );
			const int segments = VK_ClampValue( static_cast<int>( distance / 16.0f ), 1, 64 );
			vec3_t previous;
			VectorCopy( entity.origin, previous );
			for ( int segment = 1; segment <= segments; ++segment )
			{
				const float fraction = static_cast<float>( segment ) / segments;
				vec3_t point;
				VectorMA( entity.origin, distance * fraction, direction, point );
				if ( segment < segments )
				{
					const float envelope = std::sin( fraction * 3.14159265359f );
					const float phase = entity.frame * 0.00031f + segment * 2.39996323f;
					VectorMA( point,
						std::sin( phase ) * entity.angles[0] * 7.0f * envelope,
						side, point );
					VectorMA( point,
						std::cos( phase * 1.37f ) * entity.angles[0] * 7.0f * envelope,
						vertical, point );
				}
				const float startFraction = static_cast<float>( segment - 1 ) / segments;
				const float startRadius = ( entity.renderfx & RF_TAPERED ) != 0
					? entity.radius * ( 1.0f - startFraction * startFraction )
					: entity.radius;
				const float endRadius = ( entity.renderfx & RF_TAPERED ) != 0
					? entity.radius * ( 1.0f - fraction * fraction )
					: entity.radius;
				VK_AppendDynamicEffectLine(
					batch, previous, point, refdef.vieworg,
					startRadius, endRadius, entity.shaderRGBA,
					startFraction, fraction );
				VectorCopy( point, previous );
			}
		}
		else
		{
			const float radius = ( entity.reType == RT_BEAM && entity.frame > 0
				? entity.frame * 0.5f
				: entity.radius ) * authoredGlowRadiusScale;
			if ( std::isfinite( radius ) && radius > 0.0f )
			{
				VK_AppendDynamicEffectLine(
					batch, entity.origin, entity.oldorigin, refdef.vieworg,
					radius, radius, entity.shaderRGBA );
			}
		}
	}

	for ( const vk_scene_poly_t &poly : polys )
	{
		const qhandle_t shader = poly.shader > 0 ? poly.shader : 1;
		if ( poly.vertices.size() < 3 || !VK_DynamicShaderUsesPass( shader, pass ) )
		{
			continue;
		}
		vk_dynamic_effect_batch_t *batch = VK_DynamicEffectBatchForShader( batches, shader );
		for ( size_t vertex = 1; vertex + 1 < poly.vertices.size(); ++vertex )
		{
			const polyVert_t &p0 = poly.vertices[0];
			const polyVert_t &p1 = poly.vertices[vertex];
			const polyVert_t &p2 = poly.vertices[vertex + 1];
			VK_AppendDynamicEffectTriangle(
				batch, p0.xyz, p1.xyz, p2.xyz,
				p0.modulate, p1.modulate, p2.modulate,
				p0.st, p1.st, p2.st );
		}
	}
}

static bool VK_StreamDynamicEffectBatch(
	const vk_dynamic_effect_batch_t &batch,
	VkDeviceSize *vertexOffset )
{
	const VkDeviceSize alignment = 16;
	const VkDeviceSize offset =
		( vk.skinnedVertexOffset + alignment - 1 ) & ~( alignment - 1 );
	const VkDeviceSize byteCount = static_cast<VkDeviceSize>(
		batch.vertices.size() * sizeof( vk_world_vertex_t ) );
	if ( batch.vertices.empty() || vk.skinnedVertexMapped == nullptr ||
		 offset > vk.skinnedVertexCapacity || byteCount > vk.skinnedVertexCapacity - offset )
	{
		if ( !batch.vertices.empty() && !vk.loggedDynamicEffectOverflow )
		{
			ri.Printf( PRINT_WARNING,
				"rd-vulkan-fx: dynamic vertex stream exhausted; some effects were skipped\n" );
			vk.loggedDynamicEffectOverflow = true;
		}
		return false;
	}
	std::memcpy( vk.skinnedVertexMapped + offset, batch.vertices.data(),
		static_cast<size_t>( byteCount ) );
	vk.skinnedVertexOffset = offset + byteCount;
	*vertexOffset = offset;
	return true;
}

static uint32_t VK_RecordDynamicEffectBatch(
	const vk_dynamic_effect_batch_t &batch,
	vk_world_pass_t pass,
	VkPipeline *boundPipeline,
	VkDescriptorSet *boundTexture )
{
	VkDeviceSize vertexOffset = 0;
	if ( !VK_StreamDynamicEffectBatch( batch, &vertexOffset ) )
	{
		return 0;
	}
	vkCmdBindVertexBuffers(
		vk.commandBuffer, 0, 1, &vk.skinnedVertexBuffer, &vertexOffset );

	uint32_t draws = 0;
	if ( batch.shader > 0 && static_cast<size_t>( batch.shader ) < vk.materials.size() &&
		 !vk.materials[batch.shader].stages.empty() )
	{
		for ( const vk_material_stage_t &stage : vk.materials[batch.shader].stages )
		{
			if ( stage.surfaceSprite.type != VK_SURFACE_SPRITE_NONE || stage.lightmap ||
				 ( ( pass == VK_WORLD_PASS_OPAQUE ) != ( stage.blendMode == VK_BLEND_OPAQUE ) ) )
			{
				continue;
			}
			if ( !VK_WorldTextureUsable( stage.texture ) )
			{
				continue;
			}
			VK_BindWorldPipeline( stage.blendMode, boundPipeline, stage.depthWrite );
			if ( !VK_BindWorldTexture(
					stage.texture, boundTexture, !stage.clampMap ) )
			{
				continue;
			}
			VK_PushWorldStage( &stage, false );
			vkCmdDraw( vk.commandBuffer,
				static_cast<uint32_t>( batch.vertices.size() ), 1, 0, 0 );
			++draws;
		}
		return draws;
	}

	if ( pass != VK_WORLD_PASS_TRANSLUCENT || !VK_WorldTextureUsable( batch.shader ) )
	{
		return 0;
	}
	vk_material_stage_t fallback = {};
	fallback.texture = batch.shader;
	fallback.blendMode = VK_BLEND_ALPHA;
	fallback.alpha = 1.0f;
	fallback.color[0] = 1.0f;
	fallback.color[1] = 1.0f;
	fallback.color[2] = 1.0f;
	fallback.color[3] = 1.0f;
	fallback.vertexColor = true;
	VK_BindWorldPipeline( fallback.blendMode, boundPipeline );
	if ( !VK_BindWorldTexture(
			fallback.texture, boundTexture, !VK_TextureUsesClamp( fallback.texture ) ) )
	{
		return 0;
	}
	VK_PushWorldStage( &fallback, false );
	vkCmdDraw( vk.commandBuffer,
		static_cast<uint32_t>( batch.vertices.size() ), 1, 0, 0 );
	return 1;
}

static void VK_RecordDynamicEffects(
	const float mvp[16],
	const refdef_t &refdef,
	const std::vector<refEntity_t> &entities,
	const std::vector<vk_scene_poly_t> &polys,
	vk_world_pass_t pass,
	bool suppressThirdPerson )
{
	VK_SetWorldDepthBias( false );
	std::vector<vk_dynamic_effect_batch_t> batches;
	uint32_t typeCounts[RT_MAX_REF_ENTITY_TYPE] = {};
	VK_BuildDynamicEffectBatches(
		refdef, entities, polys, pass, suppressThirdPerson, &batches, typeCounts );
	if ( batches.empty() )
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
	uint32_t draws = 0;
	uint32_t triangles = 0;
	for ( const vk_dynamic_effect_batch_t &batch : batches )
	{
		draws += VK_RecordDynamicEffectBatch(
			batch, pass, &boundPipeline, &boundTexture );
		triangles += static_cast<uint32_t>( batch.vertices.size() / 3 );
	}

	if ( !vk.loggedDynamicEffects && pass == VK_WORLD_PASS_TRANSLUCENT && draws > 0 )
	{
		ri.Printf( PRINT_ALL,
			"rd-vulkan-fx: rendering dynamic effects: batches=%zu draws=%u triangles=%u "
			"sprites=%u oriented=%u lines=%u electricity=%u beams=%u polys=%zu\n",
			batches.size(), draws, triangles,
			typeCounts[RT_SPRITE] + typeCounts[RT_SABER_GLOW],
			typeCounts[RT_ORIENTED_QUAD], typeCounts[RT_LINE],
			typeCounts[RT_ELECTRICITY], typeCounts[RT_BEAM], polys.size() );
		vk.loggedDynamicEffects = true;
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
	VK_SetWorldDepthBias( false );
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
		if ( ( batch.surfaceFlags & SURF_FORCESIGHT ) != 0 &&
			 ( vk.worldRefdef.rdflags & RDF_ForceSightOn ) == 0 )
		{
			continue;
		}
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
	VK_SetWorldDepthBias( false );
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

static bool VK_TextureHandleHasName( qhandle_t handle, const char *name )
{
	return std::any_of(
		vk.textureNames.begin(), vk.textureNames.end(),
		[handle, name]( const vk_texture_name_t &registered )
		{
			return registered.handle == handle &&
				Q_stricmp( registered.name.c_str(), name ) == 0;
		} );
}

static bool VK_IsDisruptorScopeShader( qhandle_t handle )
{
	for ( const vk_texture_name_t &registered : vk.textureNames )
	{
		if ( registered.handle != handle )
		{
			continue;
		}
		const char *name = registered.name.c_str();
		if ( Q_stricmp( name, "gfx/2d/cropCircle2" ) == 0 ||
			 Q_stricmp( name, "gfx/2d/cropCircle" ) == 0 ||
			 Q_stricmp( name, "gfx/2d/cropCircleGlow" ) == 0 ||
			 Q_stricmp( name, "gfx/2d/insertTick" ) == 0 ||
			 Q_stricmp( name, "gfx/2d/crop_charge" ) == 0 )
		{
			return true;
		}
	}
	return false;
}

static bool VK_DisruptorScopeActive()
{
	return std::any_of(
		vk.rects.begin(), vk.rects.end(),
		[]( const vk_rect_t &rect ) { return rect.forceHudStereo; } );
}

static float VK_DisruptorZoomTangentScale()
{
	if ( !VK_DisruptorScopeActive() || vk.worldRefdef.fov_x <= 0.0f )
	{
		return 1.0f;
	}
	const float headsetFov =
		( std::fabs( vk.views[0].fov.angleLeft ) +
		  std::fabs( vk.views[1].fov.angleRight ) ) * 180.0f / M_PI;
	if ( headsetFov <= 0.0f || vk.worldRefdef.fov_x >= headsetFov )
	{
		return 1.0f;
	}
	const float headsetTangent = std::tan( DEG2RAD( headsetFov * 0.5f ) );
	const float zoomTangent = std::tan( DEG2RAD( vk.worldRefdef.fov_x * 0.5f ) );
	return headsetTangent > 0.0f
		? VK_ClampValue( zoomTangent / headsetTangent, 0.01f, 1.0f )
		: 1.0f;
}

static float VK_DisruptorScopeAspectScale()
{
	float tanWidth = 0.0f;
	float tanHeight = 0.0f;
	for ( int eye = 0; eye < VK_BACKEND_EYE_COUNT; ++eye )
	{
		tanWidth += std::tan( vk.views[eye].fov.angleRight ) -
			std::tan( vk.views[eye].fov.angleLeft );
		tanHeight += std::tan( vk.views[eye].fov.angleUp ) -
			std::tan( vk.views[eye].fov.angleDown );
	}
	if ( tanWidth <= 0.0f || tanHeight <= 0.0f )
	{
		return 1.0f;
	}
	return ( 4.0f / 3.0f ) / ( tanWidth / tanHeight );
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
	VK_SetWorldDepthBias( false );
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

static bool VK_DynamicLightIntersectsBatch(
	const vk_dynamic_light_t &light,
	const vk_world_batch_t &batch )
{
	float distanceSquared = 0.0f;
	for ( int axis = 0; axis < 3; ++axis )
	{
		const float nearest = VK_ClampValue(
			light.origin[axis], batch.mins[axis], batch.maxs[axis] );
		const float delta = light.origin[axis] - nearest;
		distanceSquared += delta * delta;
	}
	return distanceSquared < light.radius * light.radius;
}

static qhandle_t VK_DynamicLightSurfaceTexture(
	qhandle_t shader,
	vk_alpha_test_t *alphaTest )
{
	*alphaTest = VK_ALPHA_TEST_NONE;
	if ( shader > 0 && static_cast<size_t>( shader ) < vk.materials.size() )
	{
		for ( const vk_material_stage_t &stage : vk.materials[shader].stages )
		{
			if ( stage.surfaceSprite.type != VK_SURFACE_SPRITE_NONE || stage.lightmap ||
				 stage.blendMode != VK_BLEND_OPAQUE )
			{
				continue;
			}
			if ( VK_WorldTextureUsable( stage.texture ) )
			{
				*alphaTest = stage.alphaTest;
				return stage.texture;
			}
		}
	}
	const qhandle_t texture = VK_WorldResolveTexture( shader );
	return VK_WorldTextureUsable( texture ) ? texture : 1;
}

static void VK_PushWorldDynamicLight(
	const vk_dynamic_light_t &light,
	vk_alpha_test_t alphaTest )
{
	vk_world_stage_push_t push = {};
	push.uvOffset[0] = light.origin[0];
	push.uvOffset[1] = light.origin[1];
	push.alpha = light.origin[2];
	push.useLightmap = light.radius;
	push.color[0] = light.color[0];
	push.color[1] = light.color[1];
	push.color[2] = light.color[2];
	push.color[3] = 1.0f;
	push.flags[3] = 20.0f;
	push.uvScale[0] = 1.0f;
	push.uvScale[1] = 1.0f;
	push.lightmapGamma = static_cast<float>( alphaTest );
	vkCmdPushConstants(
		vk.commandBuffer,
		vk.pipelineLayout,
		VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
		sizeof( float ) * 16,
		sizeof( push ),
		&push );
}

static uint32_t VK_RecordWorldDynamicLights(
	const std::vector<byte> *visibleSurfaces )
{
	if ( vk.worldLights.empty() )
	{
		return 0;
	}
	VK_SetWorldDepthBias( false );

	const VkDeviceSize offsets[] = { 0 };
	vkCmdBindVertexBuffers( vk.commandBuffer, 0, 1, &vk.world.vertexBuffer, offsets );
	vkCmdBindIndexBuffer( vk.commandBuffer, vk.world.indexBuffer, 0, VK_INDEX_TYPE_UINT32 );
	VkPipeline boundPipeline = VK_NULL_HANDLE;
	VkDescriptorSet boundTexture = VK_NULL_HANDLE;
	uint32_t drawCount = 0;
	for ( const vk_dynamic_light_t &light : vk.worldLights )
	{
		for ( const vk_world_batch_t &batch : vk.world.batches )
		{
			if ( !VK_BatchBelongsToStaticWorld( batch ) ||
				 ( batch.surfaceFlags & ( SURF_NODLIGHT | SURF_SKY ) ) != 0 ||
				 !VK_ShaderUsesPass( batch.shader, batch.vertexLit, VK_WORLD_PASS_OPAQUE ) ||
				 ( visibleSurfaces != nullptr &&
				   ( batch.surfaceIndex >= visibleSurfaces->size() ||
					 ( *visibleSurfaces )[batch.surfaceIndex] == 0 ) ) ||
				 !VK_DynamicLightIntersectsBatch( light, batch ) )
			{
				continue;
			}

			vk_alpha_test_t alphaTest = VK_ALPHA_TEST_NONE;
			const qhandle_t texture = VK_DynamicLightSurfaceTexture( batch.shader, &alphaTest );
			VK_BindWorldPipeline( VK_BLEND_ADDITIVE, &boundPipeline );
			if ( !VK_BindWorldTexture( texture, &boundTexture ) )
			{
				continue;
			}
			VK_PushWorldDynamicLight( light, alphaTest );
			vkCmdDrawIndexed(
				vk.commandBuffer, batch.indexCount, 1, batch.firstIndex, 0, 0 );
			++drawCount;
		}
	}
	if ( !vk.loggedDynamicLighting )
	{
		ri.Printf( PRINT_ALL,
			"rd-vulkan-lighting: dynamic world pass active lights=%zu draws=%u\n",
			vk.worldLights.size(), drawCount );
		vk.loggedDynamicLighting = true;
	}
	return drawCount;
}

static void VK_RecordWorld( int eye, bool drawSky, bool drawWeather )
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
	const bool timingEnabled = VK_TimingEnabled();
	std::chrono::steady_clock::time_point phaseBegin = {};
	const auto beginPhase = [&]()
	{
		if ( timingEnabled )
		{
			phaseBegin = std::chrono::steady_clock::now();
		}
	};
	const auto endPhase = [&]( double *total )
	{
		if ( timingEnabled )
		{
			*total += std::chrono::duration<double, std::milli>(
				std::chrono::steady_clock::now() - phaseBegin ).count();
		}
	};

	float view[16] = {};
	float projection[16] = {};
	float mvp[16] = {};
	const bool applyStereoSeparation =
		( vk.worldRefdef.rdflags & RDF_SKYBOXPORTAL ) == 0;
	VK_BuildViewMatrix( vk.worldRefdef, eye, view, applyStereoSeparation );
	VK_BuildProjectionMatrix(
		vk.views[eye].fov, 1.0f, 65536.0f, projection,
		VK_DisruptorZoomTangentScale() );
	VK_MatrixMultiply( projection, view, mvp );
	if ( drawSky )
	{
		beginPhase();
		VK_RecordSky( view, projection );
		endPhase( &vk.timingSkyTotalMs );
	}

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
		uint32_t drawCount = 0;
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
		std::unordered_set<qhandle_t> stageMajorShaders;
		const auto batchVisible = [&]( const vk_world_batch_t &batch )
		{
			if ( !VK_BatchBelongsToStaticWorld( batch ) )
			{
				return false;
			}
			if ( ( batch.surfaceFlags & SURF_FORCESIGHT ) != 0 &&
				 ( vk.worldRefdef.rdflags & RDF_ForceSightOn ) == 0 )
			{
				return false;
			}
			return visibleSurfaces == nullptr ||
				( batch.surfaceIndex < visibleSurfaces->size() &&
				  ( *visibleSurfaces )[batch.surfaceIndex] != 0 );
		};
		const bool indirectPass =
			( pass == VK_WORLD_PASS_OPAQUE || pass == VK_WORLD_PASS_FOG ) &&
			vk.world.indirectBuffer != VK_NULL_HANDLE &&
			vk.world.indirectMapped != nullptr &&
			!vk.world.indirectGroups.empty();
		if ( indirectPass )
		{
			const int slot = ( vk.worldRefdef.rdflags & RDF_SKYBOXPORTAL ) != 0 ? 0 : 1;
			VK_UpdateWorldIndirectVisibility( slot, visibleSurfaces );
			const VkDeviceSize slotOffset =
				static_cast<VkDeviceSize>( slot ) *
				vk.world.indirectCommandCount *
				sizeof( VkDrawIndexedIndirectCommand );
			for ( size_t groupIndex = 0;
				  groupIndex < vk.world.indirectGroups.size(); ++groupIndex )
			{
				const vk_world_indirect_group_t &group = vk.world.indirectGroups[groupIndex];
				if ( vk.world.indirectVisibleGroupCounts[slot][groupIndex] == 0 )
				{
					continue;
				}
				const vk_world_batch_t &batch = group.representative;
				const VkDeviceSize groupOffset = slotOffset +
					static_cast<VkDeviceSize>( group.commandFirst ) *
					sizeof( VkDrawIndexedIndirectCommand );
				const uint32_t groupDrawCount =
					static_cast<uint32_t>( group.batchIndices.size() );
				if ( pass == VK_WORLD_PASS_FOG )
				{
					drawCount += VK_RecordBoundIndexedFog(
						batch.shader, batch.indexCount, batch.firstIndex,
						&boundPipeline, &boundTexture,
						vk.world.indirectBuffer, groupOffset, groupDrawCount );
				}
				else
				{
					drawCount += VK_RecordBoundIndexedShader(
						batch.shader, batch.lightmaps[0], batch.vertexLit,
						batch.indexCount, batch.firstIndex, pass,
						&boundPipeline, &boundTexture, nullptr, false, -1, true,
						&batch, nullptr, VK_NULL_HANDLE,
						vk.world.indirectBuffer, groupOffset, groupDrawCount );
				}
			}

			// River materials retain their stage-major direct path so the ordering
			// that hides BSP patch boundaries remains unchanged.
			for ( const vk_world_batch_t &batch : vk.world.batches )
			{
				if ( !VK_ShaderIsYavinRiver( batch.shader ) || !batchVisible( batch ) )
				{
					continue;
				}
				if ( pass == VK_WORLD_PASS_FOG )
				{
					drawCount += VK_RecordBoundIndexedFog(
						batch.shader, batch.indexCount, batch.firstIndex,
						&boundPipeline, &boundTexture );
				}
				else
				{
					drawCount += VK_RecordBoundIndexedShader(
						batch.shader, batch.lightmaps[0], batch.vertexLit,
						batch.indexCount, batch.firstIndex, pass,
						&boundPipeline, &boundTexture, nullptr, false, -1, true, &batch );
				}
			}
			return drawCount;
		}
		for ( const vk_world_batch_t &batch : vk.world.batches )
		{
			if ( !batchVisible( batch ) )
			{
				continue;
			}

			if ( pass == VK_WORLD_PASS_TRANSLUCENT &&
				 VK_ShaderIsYavinRiver( batch.shader ) )
			{
				if ( !stageMajorShaders.insert( batch.shader ).second )
				{
					continue;
				}

				const bool diagnostic = vk.yavinRiverDiagnosticCvar != nullptr &&
					vk.yavinRiverDiagnosticCvar->integer != 0;
				const vk_material_t &material = vk.materials[batch.shader];
				if ( diagnostic )
				{
					for ( const vk_world_batch_t &riverBatch : vk.world.batches )
					{
						if ( riverBatch.shader == batch.shader && batchVisible( riverBatch ) )
						{
							drawCount += VK_RecordBoundIndexedShader(
								riverBatch.shader, riverBatch.lightmaps[0], riverBatch.vertexLit,
								riverBatch.indexCount, riverBatch.firstIndex, pass,
								&boundPipeline, &boundTexture, nullptr, false, -1, true, &riverBatch );
						}
					}
					continue;
				}

				// OpenJK batches world surfaces sharing a shader before iterating
				// material stages. Preserve that order: completing every translucent
				// stage per BSP patch exposes overlap and triangulation boundaries.
				for ( size_t stageIndex = 0; stageIndex < material.stages.size(); ++stageIndex )
				{
					for ( const vk_world_batch_t &riverBatch : vk.world.batches )
					{
						if ( riverBatch.shader != batch.shader || !batchVisible( riverBatch ) )
						{
							continue;
						}
						drawCount += VK_RecordBoundIndexedShader(
							riverBatch.shader, riverBatch.lightmaps[0], riverBatch.vertexLit,
							riverBatch.indexCount, riverBatch.firstIndex, pass,
							&boundPipeline, &boundTexture, nullptr, false,
							static_cast<int>( stageIndex ), false, &riverBatch );
					}
				}
				for ( const vk_world_batch_t &riverBatch : vk.world.batches )
				{
					if ( riverBatch.shader != batch.shader || !batchVisible( riverBatch ) )
					{
						continue;
					}
					drawCount += VK_RecordBoundIndexedShader(
						riverBatch.shader, riverBatch.lightmaps[0], riverBatch.vertexLit,
						riverBatch.indexCount, riverBatch.firstIndex, pass,
						&boundPipeline, &boundTexture, nullptr, false,
						static_cast<int>( material.stages.size() ), true, &riverBatch );
				}
				continue;
			}

			if ( pass == VK_WORLD_PASS_FOG )
			{
				drawCount += VK_RecordBoundIndexedFog(
					batch.shader, batch.indexCount, batch.firstIndex,
					&boundPipeline, &boundTexture );
			}
			else
			{
				drawCount += VK_RecordBoundIndexedShader(
					batch.shader, batch.lightmaps[0], batch.vertexLit,
					batch.indexCount, batch.firstIndex,
					pass, &boundPipeline, &boundTexture, nullptr, false, -1, true, &batch );
			}
		}
		return drawCount;
	};

	beginPhase();
	vk.timingBspDrawTotal += recordWorldPass( VK_WORLD_PASS_OPAQUE );
	endPhase( &vk.timingBspTotalMs );
	beginPhase();
	VK_RecordWorldDynamicLights( visibleSurfaces );
	endPhase( &vk.timingWorldLightTotalMs );
	beginPhase();
	VK_RecordWorldSurfaceSprites( mvp, visibleSurfaces );
	endPhase( &vk.timingSpriteTotalMs );
	beginPhase();
	VK_RecordSceneModels(
		view,
		projection,
		VK_WORLD_PASS_OPAQUE,
		vk.worldEntities,
		true,
		vk.worldRefdef.time,
		vk.worldRefdef,
		vk.worldLights );
	endPhase( &vk.timingModelTotalMs );
	beginPhase();
	VK_RecordDynamicEffects(
		mvp, vk.worldRefdef, vk.worldEntities, vk.worldPolys,
		VK_WORLD_PASS_OPAQUE, true );
	endPhase( &vk.timingEffectTotalMs );
	// Complete model materials before translucent world surfaces. Opaque model
	// depth then lets water and glass composite only where they are in front,
	// without later model lighting stages painting back over them.
	beginPhase();
	VK_RecordSceneModels(
		view,
		projection,
		VK_WORLD_PASS_TRANSLUCENT,
		vk.worldEntities,
		true,
		vk.worldRefdef.time,
		vk.worldRefdef,
		vk.worldLights );
	endPhase( &vk.timingModelTotalMs );
	beginPhase();
	vk.timingBspDrawTotal += recordWorldPass( VK_WORLD_PASS_TRANSLUCENT );
	endPhase( &vk.timingBspTotalMs );
	if ( vk.world.hasGlobalFog )
	{
		beginPhase();
		vk.timingBspDrawTotal += recordWorldPass( VK_WORLD_PASS_FOG );
		endPhase( &vk.timingBspTotalMs );
		beginPhase();
		VK_RecordSceneModels(
			view,
			projection,
			VK_WORLD_PASS_FOG,
			vk.worldEntities,
			true,
			vk.worldRefdef.time,
			vk.worldRefdef,
			vk.worldLights );
		endPhase( &vk.timingModelTotalMs );
	}
	// Global fog is a geometry pass. Draw non-depth-writing effects after it so
	// distant fogged surfaces cannot composite over nearby saber glow and beams.
	beginPhase();
	VK_RecordDynamicEffects(
		mvp, vk.worldRefdef, vk.worldEntities, vk.worldPolys,
		VK_WORLD_PASS_TRANSLUCENT, true );
	endPhase( &vk.timingEffectTotalMs );
	if ( drawWeather )
	{
		beginPhase();
		VK_RecordWeather( mvp );
		endPhase( &vk.timingSpriteTotalMs );
	}
	VK_SetWorldDepthBias( false );

	if ( !vk.loggedWorldDraw )
	{
		ri.Printf( PRINT_ALL, "rd-vulkan-world: drawing static BSP geometry: surfaces=%u vertices=%u indices=%u batches=%zu textured=%u pvs=%s\n",
			vk.world.surfaceCount, vk.world.vertexCount, vk.world.indexCount,
			vk.world.batches.size(), vk.world.texturedBatchCount,
			visibleSurfaces != nullptr ? "yes" : "no" );
		vk.loggedWorldDraw = true;
	}
}

static void VK_ClearWorldDepth( int eye )
{
	VkClearAttachment attachment = {};
	attachment.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	attachment.clearValue.depthStencil.depth = 1.0f;
	VkClearRect rect = {};
	rect.rect.extent.width = vk.viewConfiguration[eye].recommendedImageRectWidth;
	rect.rect.extent.height = vk.viewConfiguration[eye].recommendedImageRectHeight;
	rect.layerCount = 1;
	vkCmdClearAttachments( vk.commandBuffer, 1, &attachment, 1, &rect );
}

static void VK_RecordSubmittedWorld( int eye )
{
	if ( vk.havePortalRefdef )
	{
		std::swap( vk.worldRefdef, vk.portalRefdef );
		vk.worldEntities.swap( vk.portalEntities );
		vk.worldPolys.swap( vk.portalPolys );
		vk.worldLights.swap( vk.portalLights );

		VK_RecordWorld( eye, true, false );

		vk.worldLights.swap( vk.portalLights );
		vk.worldPolys.swap( vk.portalPolys );
		vk.worldEntities.swap( vk.portalEntities );
		std::swap( vk.worldRefdef, vk.portalRefdef );

		// Keep the portal color, then let foreground geometry own depth.
		VK_ClearWorldDepth( eye );
		VK_RecordWorld( eye, false, true );

		static bool loggedPortalComposition = false;
		if ( !loggedPortalComposition )
		{
			ri.Printf( PRINT_ALL,
				"rd-vulkan-sky: composing authored portal sky behind the foreground world\n" );
			loggedPortalComposition = true;
		}
		return;
	}

	VK_RecordWorld( eye, true, true );
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
						static_cast<float>( scene.refdef.y ),
						static_cast<float>( scene.refdef.width ),
						static_cast<float>( scene.refdef.height ),
					} );
					savedClip = &vk.screenSceneClips.back();
				}
				else if ( area < savedArea )
				{
					savedClip->x = static_cast<float>( scene.refdef.x );
					savedClip->y = static_cast<float>( scene.refdef.y );
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
			viewport.y = static_cast<float>( scene.refdef.y );
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
		float mvp[16] = {};
		VK_BuildViewMatrix( scene.refdef, 0, view, false );
		VK_BuildRefdefProjectionMatrix( scene.refdef, 1.0f, 8192.0f, projection );
		VK_MatrixMultiply( projection, view, mvp );
		VK_RecordSceneModels(
			view,
			projection,
			VK_WORLD_PASS_OPAQUE,
			scene.entities,
			false,
			scene.refdef.time,
			scene.refdef,
			scene.lights );
		VK_RecordDynamicEffects(
			mvp, scene.refdef, scene.entities, scene.polys,
			VK_WORLD_PASS_OPAQUE, false );
		VK_RecordSceneModels(
			view,
			projection,
			VK_WORLD_PASS_TRANSLUCENT,
			scene.entities,
			false,
			scene.refdef.time,
			scene.refdef,
			scene.lights );
		VK_RecordDynamicEffects(
			mvp, scene.refdef, scene.entities, scene.polys,
			VK_WORLD_PASS_TRANSLUCENT, false );
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

static bool VK_GetControllerBoolean( XrAction action, int hand )
{
	XrActionStateGetInfo getInfo = {};
	getInfo.type = XR_TYPE_ACTION_STATE_GET_INFO;
	getInfo.action = action;
	getInfo.subactionPath = vk.handPaths[hand];
	XrActionStateBoolean state = {};
	state.type = XR_TYPE_ACTION_STATE_BOOLEAN;
	return VK_CheckXr(
		xrGetActionStateBoolean( vk.xrSession, &getInfo, &state ),
		"xrGetActionStateBoolean" ) && state.isActive && state.currentState;
}

static float VK_GetControllerFloat( XrAction action, int hand )
{
	XrActionStateGetInfo getInfo = {};
	getInfo.type = XR_TYPE_ACTION_STATE_GET_INFO;
	getInfo.action = action;
	getInfo.subactionPath = vk.handPaths[hand];
	XrActionStateFloat state = {};
	state.type = XR_TYPE_ACTION_STATE_FLOAT;
	if ( !VK_CheckXr(
			xrGetActionStateFloat( vk.xrSession, &getInfo, &state ),
			"xrGetActionStateFloat" ) || !state.isActive )
	{
		return 0.0f;
	}
	return state.currentState;
}

static XrVector2f VK_GetControllerVector2(
	XrAction action, int hand, qboolean *active,
	XrBool32 *changed, XrTime *lastChangeTime )
{
	XrActionStateGetInfo getInfo = {};
	getInfo.type = XR_TYPE_ACTION_STATE_GET_INFO;
	getInfo.action = action;
	getInfo.subactionPath = vk.handPaths[hand];
	XrActionStateVector2f state = {};
	state.type = XR_TYPE_ACTION_STATE_VECTOR2F;
	if ( !VK_CheckXr(
			xrGetActionStateVector2f( vk.xrSession, &getInfo, &state ),
			"xrGetActionStateVector2f" ) || !state.isActive )
	{
		if ( active != nullptr )
		{
			*active = qfalse;
		}
		if ( changed != nullptr )
		{
			*changed = XR_FALSE;
		}
		if ( lastChangeTime != nullptr )
		{
			*lastChangeTime = 0;
		}
		return {};
	}
	if ( active != nullptr )
	{
		*active = qtrue;
	}
	if ( changed != nullptr )
	{
		*changed = state.changedSinceLastSync;
	}
	if ( lastChangeTime != nullptr )
	{
		*lastChangeTime = state.lastChangeTime;
	}
	return state.currentState;
}

static void VK_CopyControllerPose(
	const XrPosef &pose, float position[3], float orientation[4] )
{
	position[0] = pose.position.x;
	position[1] = pose.position.y;
	position[2] = pose.position.z;
	orientation[0] = pose.orientation.x;
	orientation[1] = pose.orientation.y;
	orientation[2] = pose.orientation.z;
	orientation[3] = pose.orientation.w;
}

static void VK_UpdateControllerType()
{
	XrInteractionProfileState profileState = {};
	profileState.type = XR_TYPE_INTERACTION_PROFILE_STATE;
	if ( !VK_CheckXr(
			xrGetCurrentInteractionProfile( vk.xrSession, vk.handPaths[1], &profileState ),
			"xrGetCurrentInteractionProfile" ) ||
		 profileState.interactionProfile == XR_NULL_PATH )
	{
		return;
	}

	char profileName[XR_MAX_PATH_LENGTH] = {};
	uint32_t profileLength = 0;
	if ( !VK_CheckXr(
			xrPathToString(
				vk.xrInstance, profileState.interactionProfile,
				sizeof( profileName ), &profileLength, profileName ),
			"xrPathToString(controller profile)" ) )
	{
		return;
	}

	vrControllerType_t type = VR_CONTROLLER_TYPE_UNKNOWN;
	if ( std::strstr( profileName, "oculus/touch_controller" ) != nullptr ||
		 std::strstr( profileName, "meta/touch_controller" ) != nullptr )
	{
		type = VR_CONTROLLER_TYPE_TOUCH;
	}
	else if ( std::strstr( profileName, "valve/index_controller" ) != nullptr )
	{
		type = VR_CONTROLLER_TYPE_INDEX;
	}
	else if ( std::strstr( profileName, "htc/vive_controller" ) != nullptr )
	{
		type = VR_CONTROLLER_TYPE_VIVE;
	}
	else if ( std::strstr( profileName, "pico" ) != nullptr )
	{
		type = VR_CONTROLLER_TYPE_PICO;
	}

	if ( type != vk.controllerType )
	{
		vk.controllerType = type;
		ri.Printf( PRINT_ALL, "rd-vulkan: OpenXR controller profile %s\n", profileName );
	}
}

static void VK_LocateController(
	int hand, XrTime displayTime, vrControllerState_t *state )
{
	state->aimOrientation[3] = 1.0f;
	state->gripOrientation[3] = 1.0f;
	const XrSpace baseSpace =
		vk.stageSpace != XR_NULL_HANDLE ? vk.stageSpace : vk.localSpace;

	XrSpaceVelocity velocity = {};
	velocity.type = XR_TYPE_SPACE_VELOCITY;
	XrSpaceLocation aim = {};
	aim.type = XR_TYPE_SPACE_LOCATION;
	aim.next = &velocity;
	if ( !VK_CheckXr(
			xrLocateSpace( vk.aimSpaces[hand], baseSpace, displayTime, &aim ),
			"xrLocateSpace(controller aim)" ) )
	{
		return;
	}

	const XrSpaceLocationFlags required =
		XR_SPACE_LOCATION_POSITION_VALID_BIT |
		XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
	if ( ( aim.locationFlags & required ) != required )
	{
		return;
	}
	state->active = qtrue;
	VK_CopyControllerPose( aim.pose, state->aimPosition, state->aimOrientation );
	state->velocityFlags = velocity.velocityFlags;
	state->linearVelocity[0] = velocity.linearVelocity.x;
	state->linearVelocity[1] = velocity.linearVelocity.y;
	state->linearVelocity[2] = velocity.linearVelocity.z;
	state->angularVelocity[0] = velocity.angularVelocity.x;
	state->angularVelocity[1] = velocity.angularVelocity.y;
	state->angularVelocity[2] = velocity.angularVelocity.z;

	XrSpaceLocation grip = {};
	grip.type = XR_TYPE_SPACE_LOCATION;
	if ( VK_CheckXr(
			xrLocateSpace( vk.gripSpaces[hand], baseSpace, displayTime, &grip ),
			"xrLocateSpace(controller grip)" ) &&
		 ( grip.locationFlags & required ) == required )
	{
		VK_CopyControllerPose( grip.pose, state->gripPosition, state->gripOrientation );
	}
	else
	{
		std::memcpy( state->gripPosition, state->aimPosition, sizeof( state->gripPosition ) );
		std::memcpy( state->gripOrientation, state->aimOrientation, sizeof( state->gripOrientation ) );
	}
}

static void VK_UpdateJkxrControllers( XrTime displayTime )
{
	if ( ri.TBXR_UpdateControllers == nullptr || vk.controllerActionSet == XR_NULL_HANDLE )
	{
		return;
	}

	XrActiveActionSet activeSet = {};
	activeSet.actionSet = vk.controllerActionSet;
	XrActionsSyncInfo syncInfo = {};
	syncInfo.type = XR_TYPE_ACTIONS_SYNC_INFO;
	syncInfo.countActiveActionSets = 1;
	syncInfo.activeActionSets = &activeSet;
	if ( !VK_CheckXr( xrSyncActions( vk.xrSession, &syncInfo ), "xrSyncActions" ) )
	{
		return;
	}

	VK_UpdateControllerType();
	vrControllerState_t controllers[VK_BACKEND_EYE_COUNT] = {};
	XrBool32 joystickChanged[VK_BACKEND_EYE_COUNT] = {};
	XrTime joystickLastChangeTime[VK_BACKEND_EYE_COUNT] = {};
	for ( int hand = 0; hand < VK_BACKEND_EYE_COUNT; ++hand )
	{
		vrControllerState_t &state = controllers[hand];
		VK_LocateController( hand, displayTime, &state );
		state.indexTrigger = VK_GetControllerFloat( vk.triggerAction, hand );
		state.gripTrigger = VK_GetControllerFloat( vk.squeezeAction, hand );
		const XrVector2f joystick = VK_GetControllerVector2(
			vk.thumbstickAction, hand, &state.joystickActive,
			&joystickChanged[hand], &joystickLastChangeTime[hand] );
		state.joystick[0] = joystick.x;
		state.joystick[1] = joystick.y;

		const uint32_t primary = hand == 0
			? VR_CONTROLLER_BUTTON_X : VR_CONTROLLER_BUTTON_A;
		const uint32_t secondary = hand == 0
			? VR_CONTROLLER_BUTTON_Y : VR_CONTROLLER_BUTTON_B;
		const uint32_t thumb = hand == 0
			? VR_CONTROLLER_BUTTON_LEFT_THUMB : VR_CONTROLLER_BUTTON_RIGHT_THUMB;
		if ( VK_GetControllerBoolean( vk.primaryButtonAction, hand ) ) state.buttons |= primary;
		if ( VK_GetControllerBoolean( vk.secondaryButtonAction, hand ) ) state.buttons |= secondary;
		if ( VK_GetControllerBoolean( vk.primaryTouchAction, hand ) ) state.touches |= primary;
		if ( VK_GetControllerBoolean( vk.secondaryTouchAction, hand ) ) state.touches |= secondary;
		if ( VK_GetControllerBoolean( vk.thumbstickClickAction, hand ) )
		{
			state.buttons |= thumb | VR_CONTROLLER_BUTTON_JOYSTICK;
		}
		if ( VK_GetControllerBoolean( vk.thumbstickTouchAction, hand ) )
		{
			state.touches |= thumb | VR_CONTROLLER_BUTTON_JOYSTICK;
		}
		if ( VK_GetControllerBoolean( vk.menuAction, hand ) )
		{
			state.buttons |= VR_CONTROLLER_BUTTON_MENU;
		}
		if ( state.indexTrigger > 0.5f ||
			 VK_GetControllerBoolean( vk.triggerClickAction, hand ) )
		{
			state.buttons |= VR_CONTROLLER_BUTTON_TRIGGER;
		}
		if ( VK_GetControllerBoolean( vk.triggerTouchAction, hand ) )
		{
			state.touches |= VR_CONTROLLER_BUTTON_TRIGGER;
		}
		if ( VK_GetControllerBoolean( vk.thumbrestTouchAction, hand ) )
		{
			state.touches |= VR_CONTROLLER_TOUCH_THUMBREST;
		}
	}

	static uint32_t controllerDebugFrame = 0;
	if ( ri.Cvar_VariableIntegerValue( "vr_controller_debug" ) &&
		 ++controllerDebugFrame % 22 == 0 )
	{
		ri.Printf(
			PRINT_ALL,
			"rd-vulkan-controller-debug: sticks=L%d(%.3f %.3f chg=%d t=%lld touch=0x%x) "
			"R%d(%.3f %.3f chg=%d t=%lld touch=0x%x)\n",
			controllers[0].joystickActive,
			controllers[0].joystick[0], controllers[0].joystick[1],
			joystickChanged[0], static_cast<long long>( joystickLastChangeTime[0] ),
			controllers[0].touches,
			controllers[1].joystickActive,
			controllers[1].joystick[0], controllers[1].joystick[1],
			joystickChanged[1], static_cast<long long>( joystickLastChangeTime[1] ),
			controllers[1].touches );
	}

	ri.TBXR_UpdateControllers( &controllers[0], &controllers[1], vk.controllerType );
	if ( !vk.loggedControllerInput && ( controllers[0].active || controllers[1].active ) )
	{
		ri.Printf(
			PRINT_ALL,
			"rd-vulkan: forwarding tracked controllers (left=%d right=%d)\n",
			controllers[0].active, controllers[1].active );
		vk.loggedControllerInput = true;
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
	VK_UpdateJkxrControllers( vk.frameState.predictedDisplayTime );
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

static void VK_GetScopeAimNdcOffset( int eye, float *xOffset, float *yOffset )
{
	*xOffset = 0.0f;
	*yOffset = 0.0f;
	if ( eye < 0 || eye >= VK_BACKEND_EYE_COUNT || !vk.viewsValid )
	{
		return;
	}

	const XrFovf &fov = vk.views[eye].fov;
	const float tanLeft = std::tan( fov.angleLeft );
	const float tanRight = std::tan( fov.angleRight );
	const float tanDown = std::tan( fov.angleDown );
	const float tanUp = std::tan( fov.angleUp );
	const float tanWidth = tanRight - tanLeft;
	const float tanHeight = tanUp - tanDown;
	if ( tanWidth > 0.0001f )
	{
		*xOffset = -( tanRight + tanLeft ) / tanWidth;
	}
	if ( tanHeight > 0.0001f )
	{
		*yOffset = ( tanUp + tanDown ) / tanHeight;
	}

	static bool logged[VK_BACKEND_EYE_COUNT] = {};
	if ( !logged[eye] )
	{
		ri.Printf( PRINT_ALL,
			"rd-vulkan-scope: eye=%d optical aim offset=(%.5f %.5f)\n",
			eye, *xOffset, *yOffset );
		logged[eye] = true;
	}
}

static bool VK_ScopeHudActive( size_t firstRect, size_t endRect )
{
	endRect = std::min( endRect, vk.rects.size() );
	for ( size_t rectIndex = firstRect; rectIndex < endRect; ++rectIndex )
	{
		const vk_rect_t &rect = vk.rects[rectIndex];
		if ( rect.forceHudStereo ||
			VK_TextureHandleHasName( rect.texture, "gfx/weapon/scope" ) )
		{
			return true;
		}
	}
	return false;
}

static void VK_GetHeadLockedUvTransform( int eye, float transform[4] )
{
	transform[0] = 1.0f;
	transform[1] = 1.0f;
	transform[2] = 0.0f;
	transform[3] = 0.0f;
	if ( eye < 0 || eye >= VK_BACKEND_EYE_COUNT || !vk.viewsValid )
	{
		return;
	}

	const float commonLeft = std::min(
		std::tan( vk.views[0].fov.angleLeft ), std::tan( vk.views[1].fov.angleLeft ) );
	const float commonRight = std::max(
		std::tan( vk.views[0].fov.angleRight ), std::tan( vk.views[1].fov.angleRight ) );
	const float commonDown = std::min(
		std::tan( vk.views[0].fov.angleDown ), std::tan( vk.views[1].fov.angleDown ) );
	const float commonUp = std::max(
		std::tan( vk.views[0].fov.angleUp ), std::tan( vk.views[1].fov.angleUp ) );
	const float eyeLeft = std::tan( vk.views[eye].fov.angleLeft );
	const float eyeRight = std::tan( vk.views[eye].fov.angleRight );
	const float eyeDown = std::tan( vk.views[eye].fov.angleDown );
	const float eyeUp = std::tan( vk.views[eye].fov.angleUp );
	const float commonWidth = commonRight - commonLeft;
	const float commonHeight = commonUp - commonDown;
	if ( commonWidth > 0.0001f )
	{
		transform[0] = ( eyeRight - eyeLeft ) / commonWidth;
		transform[2] = ( eyeLeft - commonLeft ) / commonWidth;
	}
	if ( commonHeight > 0.0001f )
	{
		transform[1] = ( eyeUp - eyeDown ) / commonHeight;
		transform[3] = ( commonUp - eyeUp ) / commonHeight;
	}

	static bool logged[VK_BACKEND_EYE_COUNT] = {};
	if ( !logged[eye] )
	{
		ri.Printf( PRINT_ALL,
			"rd-vulkan-overlay: eye=%d angular UV scale=(%.4f %.4f) offset=(%.4f %.4f)\n",
			eye, transform[0], transform[1], transform[2], transform[3] );
		logged[eye] = true;
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
		if ( VK_ScopeHudActive( firstRect, endRect ) )
		{
			VK_GetScopeAimNdcOffset( eye, &hudXOffset, &hudYOffset );
		}
		else
		{
			VK_GetHudNdcOffset( eye, &hudXOffset, &hudYOffset );
		}
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
			case VK_BLEND_SOURCE_ALPHA_ADDITIVE:
				desiredPipeline = vk.texturedRectSourceAlphaAdditivePipeline;
				break;
			case VK_BLEND_INVERSE_SOURCE_ALPHA_ADDITIVE:
				desiredPipeline = vk.texturedRectInverseSourceAlphaAdditivePipeline;
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
			case VK_BLEND_DOUBLE_MODULATE:
				desiredPipeline = vk.texturedRectDoubleModulatePipeline;
				break;
			case VK_BLEND_INVERSE_SOURCE_COLOR_MODULATE:
				desiredPipeline = vk.texturedRectInverseSourceColorModulatePipeline;
				break;
			case VK_BLEND_SCREEN:
				desiredPipeline = vk.texturedRectScreenPipeline;
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
				rect.repeatTexture
					? &vk.textures[rect.texture].repeatDescriptorSet
					: &vk.textures[rect.texture].descriptorSet,
				0,
				nullptr );
		}

		const bool fullScreen = rect.rect[0] <= -0.996f && rect.rect[1] <= -0.996f &&
			rect.rect[2] >= 0.996f && rect.rect[3] >= 0.996f;
		const bool stereoOffset = !fullScreen || rect.forceHudStereo;
		const float rectXOffset = stereoOffset ? hudXOffset : 0.0f;
		const float rectYOffset = stereoOffset ? hudYOffset : 0.0f;
		float scopeAspectScale = 1.0f;
		if ( rect.forceHudStereo )
		{
			scopeAspectScale = VK_DisruptorScopeAspectScale();
		}
		float overlayUvTransform[4] = { 1.0f, 1.0f, 0.0f, 0.0f };
		if ( fullScreen && rect.headLockedOverlay )
		{
			VK_GetHeadLockedUvTransform( eye, overlayUvTransform );
		}
		float forceSenseRayScale = 1.0f;
		if ( rect.forceSenseRays )
		{
			const cvar_t *scaleCvar =
				ri.Cvar_Get( "r_vulkanForceSenseRayScale", "1.22", CVAR_ARCHIVE );
			forceSenseRayScale = scaleCvar != nullptr
				? std::max( 1.0f, std::min( 1.6f, scaleCvar->value ) )
				: 1.22f;
		}
		float pushConstants[28] = {
			rect.rect[0], rect.rect[1], rect.rect[2], rect.rect[3],
			rect.uv[0], rect.uv[1], rect.uv[2], rect.uv[3],
			rect.color[0], rect.color[1], rect.color[2], rect.color[3],
			rect.rotation[0], rect.rotation[1],
			rect.rotation[2], rect.rotation[3],
			rect.uvRotation[0], rect.uvRotation[1], forceSenseRayScale, 0.0f,
			overlayUvTransform[0], overlayUvTransform[1],
			overlayUvTransform[2], overlayUvTransform[3],
			scopeAspectScale, rectXOffset, rectYOffset,
			rect.forceSenseVignette ? 1.0f : ( rect.forceSenseRays ? 2.0f : 0.0f ),
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

using vk_timing_clock_t = std::chrono::steady_clock;

static bool VK_TimingEnabled()
{
	return vk.timingCvar != nullptr && vk.timingCvar->integer != 0;
}

static double VK_TimingMilliseconds(
	vk_timing_clock_t::time_point begin,
	vk_timing_clock_t::time_point end )
{
	return std::chrono::duration<double, std::milli>( end - begin ).count();
}

static void VK_ResetTimingSamples()
{
	vk.timingSamples = 0;
	vk.timingGpuSamples = 0;
	vk.timingRecordTotalMs = 0.0;
	vk.timingRecordMaxMs = 0.0;
	vk.timingWaitTotalMs = 0.0;
	vk.timingWaitMaxMs = 0.0;
	vk.timingGpuTotalMs = 0.0;
	vk.timingGpuMaxMs = 0.0;
	vk.timingLightTotal = 0;
	vk.timingLightMax = 0;
	vk.timingModelCandidateTotal = 0;
	vk.timingModelCulledTotal = 0;
	vk.timingModelDrawTotal = 0;
	vk.timingSkyTotalMs = 0.0;
	vk.timingBspTotalMs = 0.0;
	vk.timingWorldLightTotalMs = 0.0;
	vk.timingSpriteTotalMs = 0.0;
	vk.timingModelTotalMs = 0.0;
	vk.timingModelCullTotalMs = 0.0;
	vk.timingModelBoneTotalMs = 0.0;
	vk.timingModelSkinTotalMs = 0.0;
	vk.timingModelSubmitTotalMs = 0.0;
	vk.timingEffectTotalMs = 0.0;
	vk.timingBspDrawTotal = 0;
	vk.timingSkinModels.clear();
}

static uint64_t VK_TimestampDelta( uint64_t begin, uint64_t end )
{
	if ( vk.queueTimestampValidBits >= 64 )
	{
		return end - begin;
	}
	const uint64_t mask = ( uint64_t{ 1 } << vk.queueTimestampValidBits ) - 1;
	return ( end - begin ) & mask;
}

static uint32_t VK_TimingActiveLightCount()
{
	uint64_t count = vk.worldLights.size() + vk.portalLights.size();
	for ( const vk_scene_submission_t &scene : vk.screenScenes )
	{
		count += scene.lights.size();
	}
	return static_cast<uint32_t>( std::min<uint64_t>( count, UINT32_MAX ) );
}

static void VK_RecordTimingSample(
	double recordMs,
	double waitMs,
	bool haveGpuSample,
	double gpuMs )
{
	++vk.timingSamples;
	vk.timingRecordTotalMs += recordMs;
	vk.timingRecordMaxMs = std::max( vk.timingRecordMaxMs, recordMs );
	vk.timingWaitTotalMs += waitMs;
	vk.timingWaitMaxMs = std::max( vk.timingWaitMaxMs, waitMs );
	if ( haveGpuSample )
	{
		++vk.timingGpuSamples;
		vk.timingGpuTotalMs += gpuMs;
		vk.timingGpuMaxMs = std::max( vk.timingGpuMaxMs, gpuMs );
	}
	const uint32_t lightCount = VK_TimingActiveLightCount();
	vk.timingLightTotal += lightCount;
	vk.timingLightMax = std::max( vk.timingLightMax, lightCount );

	constexpr uint32_t reportInterval = 120;
	if ( vk.timingSamples < reportInterval )
	{
		return;
	}

	const double sampleScale = 1.0 / static_cast<double>( vk.timingSamples );
	if ( vk.timingGpuSamples > 0 )
	{
		ri.Printf( PRINT_ALL,
			"rd-vulkan-timing: frames=%u record=%.3f/%.3fms wait=%.3f/%.3fms "
			"gpu-stereo=%.3f/%.3fms lights=%.1f/%u "
			"models-opaque-stereo=%.1f/%.1f/%.1f (candidates/culled/draws avg)\n",
			vk.timingSamples,
			vk.timingRecordTotalMs * sampleScale,
			vk.timingRecordMaxMs,
			vk.timingWaitTotalMs * sampleScale,
			vk.timingWaitMaxMs,
			vk.timingGpuTotalMs / static_cast<double>( vk.timingGpuSamples ),
			vk.timingGpuMaxMs,
			static_cast<double>( vk.timingLightTotal ) * sampleScale,
			vk.timingLightMax,
			static_cast<double>( vk.timingModelCandidateTotal ) * sampleScale,
			static_cast<double>( vk.timingModelCulledTotal ) * sampleScale,
			static_cast<double>( vk.timingModelDrawTotal ) * sampleScale );
	}
	else
	{
		ri.Printf( PRINT_ALL,
			"rd-vulkan-timing: frames=%u record=%.3f/%.3fms wait=%.3f/%.3fms "
			"gpu-stereo=n/a lights=%.1f/%u "
			"models-opaque-stereo=%.1f/%.1f/%.1f (candidates/culled/draws avg)\n",
			vk.timingSamples,
			vk.timingRecordTotalMs * sampleScale,
			vk.timingRecordMaxMs,
			vk.timingWaitTotalMs * sampleScale,
			vk.timingWaitMaxMs,
			static_cast<double>( vk.timingLightTotal ) * sampleScale,
			vk.timingLightMax,
			static_cast<double>( vk.timingModelCandidateTotal ) * sampleScale,
			static_cast<double>( vk.timingModelCulledTotal ) * sampleScale,
			static_cast<double>( vk.timingModelDrawTotal ) * sampleScale );
	}
	ri.Printf( PRINT_ALL,
		"rd-vulkan-phases: cpu-stereo sky=%.3fms bsp=%.3fms light=%.3fms "
		"sprites=%.3fms models=%.3fms effects=%.3fms bsp-draws=%.1f\n",
		vk.timingSkyTotalMs * sampleScale,
		vk.timingBspTotalMs * sampleScale,
		vk.timingWorldLightTotalMs * sampleScale,
		vk.timingSpriteTotalMs * sampleScale,
		vk.timingModelTotalMs * sampleScale,
		vk.timingEffectTotalMs * sampleScale,
		static_cast<double>( vk.timingBspDrawTotal ) * sampleScale );
	const double classifiedModelMs =
		vk.timingModelCullTotalMs + vk.timingModelBoneTotalMs +
		vk.timingModelSkinTotalMs + vk.timingModelSubmitTotalMs;
	ri.Printf( PRINT_ALL,
		"rd-vulkan-model-phases: cpu-stereo cull=%.3fms bones=%.3fms "
		"skin=%.3fms submit=%.3fms other=%.3fms\n",
		vk.timingModelCullTotalMs * sampleScale,
		vk.timingModelBoneTotalMs * sampleScale,
		vk.timingModelSkinTotalMs * sampleScale,
		vk.timingModelSubmitTotalMs * sampleScale,
		std::max( 0.0, vk.timingModelTotalMs - classifiedModelMs ) * sampleScale );
	std::vector<const vk_skin_model_timing_t *> skinModels;
	skinModels.reserve( vk.timingSkinModels.size() );
	for ( const auto &entry : vk.timingSkinModels )
	{
		skinModels.push_back( &entry.second );
	}
	std::sort(
		skinModels.begin(),
		skinModels.end(),
		[]( const vk_skin_model_timing_t *left, const vk_skin_model_timing_t *right )
		{
			return left->totalMs > right->totalMs;
		} );
	const size_t reportedSkinModels = std::min<size_t>( skinModels.size(), 8 );
	for ( size_t index = 0; index < reportedSkinModels; ++index )
	{
		const vk_skin_model_timing_t &timing = *skinModels[index];
		ri.Printf( PRINT_ALL,
			"rd-vulkan-skin-model: rank=%zu model=%s ms=%.3f "
			"calls=%.1f hits=%.1f misses=%.1f vertices=%.1f\n",
			index + 1,
			timing.name.c_str(),
			timing.totalMs * sampleScale,
			static_cast<double>( timing.calls ) * sampleScale,
			static_cast<double>( timing.cacheHits ) * sampleScale,
			static_cast<double>( timing.misses ) * sampleScale,
			static_cast<double>( timing.vertices ) * sampleScale );
	}
	VK_ResetTimingSamples();
}

static bool VK_RecordTestPattern(
	int eye,
	uint32_t imageIndex,
	const float tint[4],
	bool clearOnly )
{
	vk.commandBuffer = vk.eyeCommandBuffers[eye];
	if ( vk.ghoul2CacheFrameIndex != vk.frameIndex )
	{
		vk.skinnedVertexOffset = 0;
		vk.ghoul2BoneCache.clear();
		vk.ghoul2SurfaceCache.clear();
		vk.surfaceSpriteStreamCache.clear();
		vk.ghoul2CacheFrameIndex = vk.frameIndex;
	}

	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	if ( !VK_CheckVk( vkBeginCommandBuffer( vk.commandBuffer, &beginInfo ), "vkBeginCommandBuffer" ) )
	{
		return false;
	}
	const bool gpuTiming = VK_TimingEnabled() && vk.timingQueryPool != VK_NULL_HANDLE;
	if ( gpuTiming )
	{
		vkCmdResetQueryPool(
			vk.commandBuffer, vk.timingQueryPool, eye * 2, 2 );
		vkCmdWriteTimestamp(
			vk.commandBuffer,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			vk.timingQueryPool,
			eye * 2 );
	}
	vk.depthBiasStateKnown = false;
	vk.depthBiasEnabled = false;

	VkClearValue clearValues[2] = {};
	clearValues[0].color.float32[0] =
		!clearOnly && vk.sceneWorldRenderedThisFrame && vk.world.hasGlobalFog
			? vk.world.globalFogColor[0] : 0.0f;
	clearValues[0].color.float32[1] =
		!clearOnly && vk.sceneWorldRenderedThisFrame && vk.world.hasGlobalFog
			? vk.world.globalFogColor[1] : 0.0f;
	clearValues[0].color.float32[2] =
		!clearOnly && vk.sceneWorldRenderedThisFrame && vk.world.hasGlobalFog
			? vk.world.globalFogColor[2] : 0.0f;
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

	if ( !clearOnly && !vk.sceneWorldRenderedThisFrame )
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

	if ( !clearOnly && vk.sceneWorldRenderedThisFrame )
	{
		VK_RecordSubmittedWorld( eye );
	}

	if ( !clearOnly && vk.sceneWorldRenderedThisFrame &&
		 vk.diagnostic3dPipeline != VK_NULL_HANDLE )
	{
		VK_RecordDiagnosticWorld( eye );
	}

	if ( !clearOnly && !vk.sceneWorldRenderedThisFrame && !vk.screenScenes.empty() )
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
	else if ( !clearOnly )
	{
		VK_RecordScreenRects( eye, 0, vk.rects.size() );
	}

	vkCmdEndRenderPass( vk.commandBuffer );
	if ( gpuTiming )
	{
		vkCmdWriteTimestamp(
			vk.commandBuffer,
			VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			vk.timingQueryPool,
			eye * 2 + 1 );
	}

	if ( !VK_CheckVk( vkEndCommandBuffer( vk.commandBuffer ), "vkEndCommandBuffer" ) )
	{
		return false;
	}

	return true;
}

static bool VK_RenderEyes(
	const float tints[VK_BACKEND_EYE_COUNT][4],
	const bool clearOnly[VK_BACKEND_EYE_COUNT] )
{
	const bool timingEnabled = VK_TimingEnabled();
	if ( timingEnabled && !vk.timingWasEnabled )
	{
		VK_ResetTimingSamples();
		vk.timingWasEnabled = true;
		if ( vk.timingQueryPool == VK_NULL_HANDLE && !vk.loggedTimingNoGpu )
		{
			ri.Printf( PRINT_ALL,
				"rd-vulkan-timing: GPU timestamps unavailable; reporting CPU timing only\n" );
			vk.loggedTimingNoGpu = true;
		}
	}
	else if ( !timingEnabled && vk.timingWasEnabled )
	{
		VK_ResetTimingSamples();
		vk.timingWasEnabled = false;
	}

	bool acquired[VK_BACKEND_EYE_COUNT] = {};
	bool ready = true;
	for ( int eye = 0; eye < VK_BACKEND_EYE_COUNT; ++eye )
	{
		XrSwapchainImageAcquireInfo acquireInfo = {};
		acquireInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;
		if ( !VK_CheckXr( xrAcquireSwapchainImage(
				vk.colorSwapchain[eye], &acquireInfo, &vk.colorImageIndex[eye] ),
				"xrAcquireSwapchainImage" ) )
		{
			ready = false;
			break;
		}
		acquired[eye] = true;

		XrSwapchainImageWaitInfo waitInfo = {};
		waitInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
		waitInfo.timeout = XR_INFINITE_DURATION;
		if ( !VK_CheckXr( xrWaitSwapchainImage(
				vk.colorSwapchain[eye], &waitInfo ), "xrWaitSwapchainImage" ) )
		{
			ready = false;
			break;
		}
	}

	vk_timing_clock_t::time_point recordBegin = {};
	vk_timing_clock_t::time_point recordEnd = {};
	if ( ready )
	{
		if ( timingEnabled )
		{
			recordBegin = vk_timing_clock_t::now();
		}
		ready = VK_CheckVk(
			vkResetCommandPool( vk.device, vk.commandPool, 0 ),
			"vkResetCommandPool(stereo frame)" );
	}
	for ( int eye = 0; ready && eye < VK_BACKEND_EYE_COUNT; ++eye )
	{
		ready = VK_RecordTestPattern(
			eye, vk.colorImageIndex[eye], tints[eye], clearOnly[eye] );
	}
	if ( ready && timingEnabled )
	{
		recordEnd = vk_timing_clock_t::now();
	}

	double waitMs = 0.0;
	bool haveGpuSample = false;
	double gpuMs = 0.0;
	if ( ready )
	{
		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = VK_BACKEND_EYE_COUNT;
		submitInfo.pCommandBuffers = vk.eyeCommandBuffers;
		const vk_timing_clock_t::time_point waitBegin = timingEnabled
			? vk_timing_clock_t::now()
			: vk_timing_clock_t::time_point{};
		ready = VK_CheckVk(
			vkQueueSubmit( vk.queue, 1, &submitInfo, VK_NULL_HANDLE ),
			"vkQueueSubmit(stereo frame)" ) &&
			VK_CheckVk( vkQueueWaitIdle( vk.queue ), "vkQueueWaitIdle(stereo frame)" );
		if ( timingEnabled )
		{
			waitMs = VK_TimingMilliseconds( waitBegin, vk_timing_clock_t::now() );
		}

		if ( ready && timingEnabled && vk.timingQueryPool != VK_NULL_HANDLE )
		{
			uint64_t timestamps[VK_BACKEND_EYE_COUNT * 2] = {};
			const VkResult queryResult = vkGetQueryPoolResults(
				vk.device,
				vk.timingQueryPool,
				0,
				ARRAY_LEN( timestamps ),
				sizeof( timestamps ),
				timestamps,
				sizeof( timestamps[0] ),
				VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT );
			if ( queryResult == VK_SUCCESS )
			{
				const uint64_t ticks = VK_TimestampDelta(
					timestamps[0], timestamps[ARRAY_LEN( timestamps ) - 1] );
				gpuMs = static_cast<double>( ticks ) *
					static_cast<double>( vk.timestampPeriodNanoseconds ) / 1000000.0;
				haveGpuSample = true;
			}
			else if ( !vk.loggedTimingNoGpu )
			{
				ri.Printf( PRINT_WARNING,
					"rd-vulkan-timing: GPU query read failed (VkResult %d); "
					"continuing with CPU timing\n",
					queryResult );
				vk.loggedTimingNoGpu = true;
			}
		}

		if ( ready && timingEnabled )
		{
			VK_RecordTimingSample(
				VK_TimingMilliseconds( recordBegin, recordEnd ),
				waitMs,
				haveGpuSample,
				gpuMs );
		}
	}

	bool released = true;
	for ( int eye = 0; eye < VK_BACKEND_EYE_COUNT; ++eye )
	{
		if ( !acquired[eye] )
		{
			continue;
		}
		XrSwapchainImageReleaseInfo releaseInfo = {};
		releaseInfo.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
		released = VK_CheckXr(
			xrReleaseSwapchainImage( vk.colorSwapchain[eye], &releaseInfo ),
			"xrReleaseSwapchainImage" ) && released;
	}
	return ready && released;
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
	vk.materialAuditCvar = ri.Cvar_Get( "r_vulkanMaterialAudit", "0", 0 );
	vk.legacyColorCvar =
		ri.Cvar_Get( "r_vulkanLegacyColorPipeline", "1", CVAR_ARCHIVE | CVAR_LATCH );
	vk.picmipCvar = ri.Cvar_Get( "r_picmip", "1", CVAR_ARCHIVE | CVAR_LATCH );
#ifndef JK2_MODE
	vk.detailTexturesCvar =
		ri.Cvar_Get( "r_detailtextures", "0", CVAR_ARCHIVE_ND | CVAR_LATCH );
#else
	vk.detailTexturesCvar =
		ri.Cvar_Get( "r_detailtextures", "1", CVAR_ARCHIVE_ND | CVAR_LATCH );
#endif
	vk.offsetFactorCvar = ri.Cvar_Get( "r_offsetfactor", "-1", CVAR_CHEAT );
	vk.offsetUnitsCvar = ri.Cvar_Get( "r_offsetunits", "-2", CVAR_CHEAT );
	vk.worldDebugCvar = ri.Cvar_Get( "r_vulkanWorldDebug", "0", 0 );
	vk.glowIntensityCvar =
		ri.Cvar_Get( "r_vulkanGlowIntensity", "1.45", CVAR_ARCHIVE );
	vk.glowRadiusCvar =
		ri.Cvar_Get( "r_vulkanGlowRadius", "1.12", CVAR_ARCHIVE );
	vk.waterEffectIntensityCvar =
		ri.Cvar_Get( "r_vulkanWaterEffectIntensity", "1.35", CVAR_ARCHIVE );
	vk.yavinRiverOpacityCvar =
		ri.Cvar_Get( "r_vulkanYavinRiverOpacityScale", "1.0", CVAR_ARCHIVE );
	vk.yavinRiverExtinctionCvar =
		ri.Cvar_Get( "r_vulkanYavinRiverExtinction", "0.22", CVAR_ARCHIVE );
	vk.yavinRiverDiagnosticCvar =
		ri.Cvar_Get( "r_vulkanYavinRiverDiagnostic", "0", 0 );
	vk.yavinRiverStageMaskCvar =
		ri.Cvar_Get( "r_vulkanYavinRiverStageMask", "15", 0 );
	vk.yavinRiverLightmapGammaCvar =
		ri.Cvar_Get( "r_vulkanYavinRiverLightmapGamma", "1.0", CVAR_ARCHIVE );
	vk.yavinWaterTransparencyCvar =
		ri.Cvar_Get( "r_vulkanYavinWaterTransparency", "0.35", CVAR_ARCHIVE );
	vk.yavinWaterDetailIntensityCvar =
		ri.Cvar_Get( "r_vulkanYavinWaterDetailIntensity", "1.0", CVAR_ARCHIVE );
	vk.waterWakeIntensityCvar =
		ri.Cvar_Get( "r_vulkanWaterWakeIntensity", "1.0", CVAR_ARCHIVE );
	vk.lightmapGammaCvar =
		ri.Cvar_Get( "r_vulkanLightmapGamma", "1.0", CVAR_ARCHIVE );
	vk.ewebCullCvar = ri.Cvar_Get( "r_vulkanEwebCull", "2", 0 );
	vk.fxModelAuditCvar = ri.Cvar_Get( "r_vulkanFxModelAudit", "0", 0 );
	vk.modelCullCvar = ri.Cvar_Get( "r_vulkanModelCull", "1", 0 );
	vk.timingCvar = ri.Cvar_Get( "r_vulkanTiming", "0", 0 );

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
	VK_CreateTimingResources();
	VK_CreateReferenceSpace( XR_REFERENCE_SPACE_TYPE_STAGE, &vk.stageSpace, "xrCreateReferenceSpace(STAGE)" );
	if ( !VK_CreateControllerActions() )
	{
		VK_Backend_Shutdown();
		return false;
	}
	if ( !VK_CreateFallbackTexture() )
	{
		VK_Backend_Shutdown();
		return false;
	}

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
	vk.clampTextureHandles.clear();

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
	if ( vk.texturedRectSourceAlphaAdditivePipeline != VK_NULL_HANDLE )
	{
		vkDestroyPipeline( vk.device, vk.texturedRectSourceAlphaAdditivePipeline, nullptr );
	}
	if ( vk.texturedRectInverseSourceAlphaAdditivePipeline != VK_NULL_HANDLE )
	{
		vkDestroyPipeline( vk.device, vk.texturedRectInverseSourceAlphaAdditivePipeline, nullptr );
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
	if ( vk.texturedRectDoubleModulatePipeline != VK_NULL_HANDLE )
	{
		vkDestroyPipeline( vk.device, vk.texturedRectDoubleModulatePipeline, nullptr );
	}
	if ( vk.texturedRectInverseSourceColorModulatePipeline != VK_NULL_HANDLE )
	{
		vkDestroyPipeline( vk.device, vk.texturedRectInverseSourceColorModulatePipeline, nullptr );
	}
	if ( vk.texturedRectScreenPipeline != VK_NULL_HANDLE )
	{
		vkDestroyPipeline( vk.device, vk.texturedRectScreenPipeline, nullptr );
	}
	if ( vk.diagnostic3dPipeline != VK_NULL_HANDLE )
	{
		vkDestroyPipeline( vk.device, vk.diagnostic3dPipeline, nullptr );
	}
	if ( vk.worldPipeline != VK_NULL_HANDLE )
	{
		vkDestroyPipeline( vk.device, vk.worldPipeline, nullptr );
	}
	if ( vk.worldBackCullPipeline != VK_NULL_HANDLE )
	{
		vkDestroyPipeline( vk.device, vk.worldBackCullPipeline, nullptr );
	}
	if ( vk.worldFrontCullPipeline != VK_NULL_HANDLE )
	{
		vkDestroyPipeline( vk.device, vk.worldFrontCullPipeline, nullptr );
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
	if ( vk.worldInverseSourceAlphaAdditivePipeline != VK_NULL_HANDLE )
	{
		vkDestroyPipeline( vk.device, vk.worldInverseSourceAlphaAdditivePipeline, nullptr );
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
	if ( vk.worldInverseSourceColorModulatePipeline != VK_NULL_HANDLE )
	{
		vkDestroyPipeline( vk.device, vk.worldInverseSourceColorModulatePipeline, nullptr );
	}
	if ( vk.worldScreenPipeline != VK_NULL_HANDLE )
	{
		vkDestroyPipeline( vk.device, vk.worldScreenPipeline, nullptr );
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
	for ( int hand = 0; hand < VK_BACKEND_EYE_COUNT; ++hand )
	{
		if ( vk.aimSpaces[hand] != XR_NULL_HANDLE )
		{
			xrDestroySpace( vk.aimSpaces[hand] );
		}
		if ( vk.gripSpaces[hand] != XR_NULL_HANDLE )
		{
			xrDestroySpace( vk.gripSpaces[hand] );
		}
	}
	if ( vk.controllerActionSet != XR_NULL_HANDLE )
	{
		xrDestroyActionSet( vk.controllerActionSet );
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
	if ( vk.timingQueryPool != VK_NULL_HANDLE )
	{
		vkDestroyQueryPool( vk.device, vk.timingQueryPool, nullptr );
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

static void VK_CalculateModelBounds( vk_model_t *model )
{
	if ( model == nullptr )
	{
		return;
	}

	model->hasBounds = false;
	for ( const vk_model_surface_t &surface : model->surfaces )
	{
		for ( const vk_world_vertex_t &vertex : surface.glmBaseVertices )
		{
			if ( !std::isfinite( vertex.position[0] ) ||
				 !std::isfinite( vertex.position[1] ) ||
				 !std::isfinite( vertex.position[2] ) )
			{
				continue;
			}
			if ( !model->hasBounds )
			{
				for ( int axis = 0; axis < 3; ++axis )
				{
					model->mins[axis] = vertex.position[axis];
					model->maxs[axis] = vertex.position[axis];
				}
				model->hasBounds = true;
				continue;
			}
			for ( int axis = 0; axis < 3; ++axis )
			{
				model->mins[axis] = std::min( model->mins[axis], vertex.position[axis] );
				model->maxs[axis] = std::max( model->maxs[axis], vertex.position[axis] );
			}
		}
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

static void VK_DecodeMD3Normal( short packedValue, float normal[3] )
{
	const uint16_t packed = static_cast<uint16_t>( LittleShort( packedValue ) );
	constexpr float byteToRadians = 2.0f * static_cast<float>( M_PI ) / 256.0f;
	const float latitude = static_cast<float>( ( packed >> 8 ) & 0xff ) * byteToRadians;
	const float longitude = static_cast<float>( packed & 0xff ) * byteToRadians;
	const float sinLongitude = std::sin( longitude );
	normal[0] = std::cos( latitude ) * sinLongitude;
	normal[1] = std::sin( latitude ) * sinLongitude;
	normal[2] = std::cos( longitude );
}

static qhandle_t VK_RegisterModelShader( const char *name )
{
	const qhandle_t shader = VK_Backend_RegisterTexture( name );
	if ( shader <= 0 || static_cast<size_t>( shader ) >= vk.materials.size() ||
		 !vk.materials[shader].stages.empty() )
	{
		return shader;
	}

	const qhandle_t texture = VK_WorldResolveTexture( shader );
	if ( texture == 2 )
	{
		return shader;
	}

	// R_FindShader assigns CGEN_LIGHTING_DIFFUSE to an image registered with
	// LIGHTMAP_NONE. Model files use that path even when their shader has no
	// explicit script definition.
	vk_material_stage_t stage = {};
	stage.texture = texture;
	stage.videoHandle = -1;
	stage.blendMode = VK_BLEND_OPAQUE;
	stage.alphaTest = VK_ALPHA_TEST_NONE;
	stage.alpha = 1.0f;
	stage.tcScale[0] = 1.0f;
	stage.tcScale[1] = 1.0f;
	stage.color[0] = 1.0f;
	stage.color[1] = 1.0f;
	stage.color[2] = 1.0f;
	stage.color[3] = 1.0f;
	stage.vertexColor = true;
	stage.lightingDiffuse = true;
	stage.depthWrite = true;
	vk.materials[shader].stages.push_back( stage );

	if ( vk.loggedImplicitModelShaders < 16 )
	{
		ri.Printf( PRINT_ALL,
			"rd-vulkan-model-material: implicit lightingDiffuse shader=%s handle=%d texture=%d\n",
			name, shader, texture );
		++vk.loggedImplicitModelShaders;
	}
	return shader;
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
		VK_DecodeMD3Normal( positions[i].normal, vertex.normal );
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
			shader = VK_RegisterModelShader( shaderName );
			if ( VK_WorldResolveTexture( shader ) == 2 )
			{
				char extensionlessName[MAX_QPATH];
				Q_strncpyz( extensionlessName, shaderName, sizeof( extensionlessName ) );
				COM_StripExtension( extensionlessName, extensionlessName, sizeof( extensionlessName ) );
				if ( Q_stricmp( extensionlessName, shaderName ) != 0 )
				{
					const qhandle_t extensionlessShader = VK_RegisterModelShader( extensionlessName );
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
	const int numFrames = LittleLong( header->numFrames );
	const int numTags = LittleLong( header->numTags );
	const int numSurfaces = LittleLong( header->numSurfaces );
	const int ofsTags = LittleLong( header->ofsTags );
	const int ofsSurfaces = LittleLong( header->ofsSurfaces );
	const int ofsEnd = LittleLong( header->ofsEnd );
	const size_t tagBytes = numFrames > 0 && numTags > 0
		? static_cast<size_t>( numFrames ) * static_cast<size_t>( numTags ) * sizeof( md3Tag_t )
		: 0;
	if ( ident != MD3_IDENT || version != MD3_VERSION ||
		 numFrames <= 0 || numFrames > MD3_MAX_FRAMES ||
		 numTags < 0 || numTags > MD3_MAX_TAGS ||
		 numSurfaces < 0 || numSurfaces > MD3_MAX_SURFACES ||
		 ofsEnd <= 0 || static_cast<size_t>( ofsEnd ) > fileSize ||
		 ( tagBytes > 0 && ( ofsTags < 0 || !VK_ModelBufferRangeValid(
			 static_cast<size_t>( ofsTags ), tagBytes, static_cast<size_t>( ofsEnd ) ) ) ) ||
		 ( numSurfaces > 0 && !VK_ModelBufferRangeValid(
			 static_cast<size_t>( ofsSurfaces ), sizeof( md3Surface_t ), static_cast<size_t>( ofsEnd ) ) ) )
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
	model.frameCount = numFrames;
	model.tagCount = numTags;
	model.tags.reserve( static_cast<size_t>( numFrames ) * static_cast<size_t>( numTags ) );
	if ( numTags > 0 )
	{
		const md3Tag_t *sourceTags =
			reinterpret_cast<const md3Tag_t *>( fileBase + ofsTags );
		for ( int tagIndex = 0; tagIndex < numFrames * numTags; ++tagIndex )
		{
			char tagName[MAX_QPATH];
			std::memcpy( tagName, sourceTags[tagIndex].name, sizeof( tagName ) );
			tagName[sizeof( tagName ) - 1] = '\0';
			vk_model_tag_t loadedTag = {};
			loadedTag.name = tagName;
			for ( int component = 0; component < 3; ++component )
			{
				loadedTag.origin[component] =
					LittleFloat( sourceTags[tagIndex].origin[component] );
				for ( int axisIndex = 0; axisIndex < 3; ++axisIndex )
				{
					loadedTag.axis[axisIndex][component] =
						LittleFloat( sourceTags[tagIndex].axis[axisIndex][component] );
				}
			}
			model.tags.push_back( std::move( loadedTag ) );
		}
	}

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
	if ( model.surfaces.empty() && model.tags.empty() )
	{
		return false;
	}

	VK_CalculateModelBounds( &model );
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
		vertex.normal[0] = LittleFloat( sourceVertices[i].normal[0] );
		vertex.normal[1] = LittleFloat( sourceVertices[i].normal[1] );
		vertex.normal[2] = LittleFloat( sourceVertices[i].normal[2] );
		VectorNormalize( vertex.normal );
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
	loadedSurface->glmSkinVertices.reserve( loadedSurface->glmVertices.size() );
	for ( const mdxmVertex_t &sourceVertex : loadedSurface->glmVertices )
	{
		vk_glm_skin_vertex_t skin = {};
		skin.weightCount = G2_GetVertWeights( &sourceVertex );
		float totalWeight = 0.0f;
		for ( int weightIndex = 0; weightIndex < skin.weightCount; ++weightIndex )
		{
			const int localBoneIndex =
				G2_GetVertBoneIndex( &sourceVertex, weightIndex );
			if ( localBoneIndex < 0 ||
				 static_cast<size_t>( localBoneIndex ) >=
					loadedSurface->glmBoneReferences.size() )
			{
				return false;
			}
			skin.boneIndices[weightIndex] =
				loadedSurface->glmBoneReferences[localBoneIndex];
			skin.weights[weightIndex] = G2_GetVertBoneWeight(
				&sourceVertex, weightIndex, totalWeight, skin.weightCount );
		}
		loadedSurface->glmSkinVertices.push_back( skin );
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
		shader = VK_RegisterModelShader( hierarchy.shader.c_str() );
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

	VK_CalculateModelBounds( &model );
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

qboolean VK_Backend_GenerateBoneOverrideMatrix(
	qhandle_t modelHandle,
	int boneNumber,
	const vec3_t angles,
	int flags,
	Eorientations up,
	Eorientations left,
	Eorientations forward,
	mdxaBone_t *matrix )
{
	const vk_model_t *model = VK_ModelForHandle( modelHandle );
	if ( model == nullptr || model->animation == nullptr || matrix == nullptr ||
		 angles == nullptr || boneNumber < 0 ||
		 static_cast<size_t>( boneNumber ) >= model->animation->bones.size() )
	{
		return qfalse;
	}

	if ( ( flags & ( BONE_ANGLES_PREMULT | BONE_ANGLES_POSTMULT ) ) != 0 )
	{
		vec3_t remapped = {};
		switch ( up )
		{
		case NEGATIVE_X: remapped[YAW] = angles[ROLL] + 180.0f; break;
		case POSITIVE_X: remapped[YAW] = angles[ROLL]; break;
		case NEGATIVE_Y:
		case POSITIVE_Y: remapped[YAW] = angles[PITCH]; break;
		case NEGATIVE_Z: remapped[YAW] = angles[YAW] + 180.0f; break;
		case POSITIVE_Z: remapped[YAW] = angles[YAW]; break;
		default: return qfalse;
		}
		switch ( left )
		{
		case NEGATIVE_X: remapped[PITCH] = angles[ROLL]; break;
		case POSITIVE_X: remapped[PITCH] = angles[ROLL] + 180.0f; break;
		case NEGATIVE_Y: remapped[PITCH] = angles[PITCH]; break;
		case POSITIVE_Y: remapped[PITCH] = angles[PITCH] + 180.0f; break;
		case NEGATIVE_Z:
		case POSITIVE_Z: remapped[PITCH] = angles[YAW]; break;
		default: return qfalse;
		}
		switch ( forward )
		{
		case NEGATIVE_X:
		case POSITIVE_X: remapped[ROLL] = angles[ROLL]; break;
		case NEGATIVE_Y: remapped[ROLL] = angles[PITCH]; break;
		case POSITIVE_Y: remapped[ROLL] = angles[PITCH] + 180.0f; break;
		case NEGATIVE_Z: remapped[ROLL] = angles[YAW]; break;
		case POSITIVE_Z: remapped[ROLL] = angles[YAW] + 180.0f; break;
		default: return qfalse;
		}

		mdxaBone_t rotation = {};
		mdxaBone_t localRotation = {};
		VK_CreateGhoul2AngleMatrix( remapped, &rotation );
		const vk_gla_bone_t &bone = model->animation->bones[boneNumber];
		VK_MultiplyBoneMatrices( rotation, bone.basePoseInverse, &localRotation );
		VK_MultiplyBoneMatrices( bone.basePose, localRotation, matrix );
		return qtrue;
	}

	vec3_t remapped;
	VectorCopy( angles, remapped );
	if ( left == POSITIVE_Y )
	{
		remapped[PITCH] += 180.0f;
	}
	mdxaBone_t rotation = {};
	mdxaBone_t permutation = {};
	VK_CreateGhoul2AngleMatrix( remapped, &rotation );
	auto setPermutation = [&]( Eorientations orientation, int column ) -> bool
	{
		switch ( orientation )
		{
		case NEGATIVE_X: permutation.matrix[0][column] = -1.0f; return true;
		case POSITIVE_X: permutation.matrix[0][column] = 1.0f; return true;
		case NEGATIVE_Y: permutation.matrix[1][column] = -1.0f; return true;
		case POSITIVE_Y: permutation.matrix[1][column] = 1.0f; return true;
		case NEGATIVE_Z: permutation.matrix[2][column] = -1.0f; return true;
		case POSITIVE_Z: permutation.matrix[2][column] = 1.0f; return true;
		default: return false;
		}
	};
	if ( !setPermutation( forward, 0 ) ||
		 !setPermutation( left, 1 ) ||
		 !setPermutation( up, 2 ) )
	{
		return qfalse;
	}
	VK_MultiplyBoneMatrices( rotation, permutation, matrix );
	return qtrue;
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
		const qhandle_t shader = off ? 0 : VK_RegisterModelShader( shaderName.c_str() );
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

void VK_Backend_ModelBounds( qhandle_t handle, vec3_t mins, vec3_t maxs )
{
	if ( mins == nullptr || maxs == nullptr )
	{
		return;
	}

	VectorClear( mins );
	VectorClear( maxs );
	const vk_model_t *model = VK_ModelForHandle( handle );
	if ( model == nullptr )
	{
		return;
	}

	if ( model->type == VK_MODEL_INLINE_BSP && model->inlineModelIndex >= 0 &&
		 static_cast<size_t>( model->inlineModelIndex ) < vk.world.inlineModels.size() )
	{
		VectorCopy( vk.world.inlineModels[model->inlineModelIndex].mins, mins );
		VectorCopy( vk.world.inlineModels[model->inlineModelIndex].maxs, maxs );
		return;
	}

	bool haveBounds = false;
	for ( const vk_model_surface_t &surface : model->surfaces )
	{
		for ( const vk_world_vertex_t &vertex : surface.glmBaseVertices )
		{
			for ( int axis = 0; axis < 3; ++axis )
			{
				if ( !haveBounds || vertex.position[axis] < mins[axis] )
				{
					mins[axis] = vertex.position[axis];
				}
				if ( !haveBounds || vertex.position[axis] > maxs[axis] )
				{
					maxs[axis] = vertex.position[axis];
				}
			}
			haveBounds = true;
		}
	}
	static bool loggedLogoBounds = false;
	if ( haveBounds && !loggedLogoBounds &&
		 Q_stricmp( model->name.c_str(), "models/map_objects/bespin/jk2logo.md3" ) == 0 )
	{
		ri.Printf( PRINT_ALL,
			"rd-vulkan-model: JKII logo bounds mins=(%.3f %.3f %.3f) "
			"maxs=(%.3f %.3f %.3f)\n",
			mins[0], mins[1], mins[2], maxs[0], maxs[1], maxs[2] );
		loggedLogoBounds = true;
	}
}

void VK_Backend_GetModelBounds( refEntity_t *entity, vec3_t mins, vec3_t maxs )
{
	VK_Backend_ModelBounds( entity != nullptr ? entity->hModel : 0, mins, maxs );
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
	for ( int coordinate = 0; coordinate < 2; ++coordinate )
	{
		vertex.lightmapUv[coordinate] = LittleFloat( source.lightmap[0][coordinate] );
	}

	// R_ColorShiftLightingBytes preserves these bytes with the legacy defaults
	// (r_mapOverBrightBits=0). In particular, do not replace dark authored
	// vertices with normal-derived colors: that exposes BSP triangle facets.
	for ( int channel = 0; channel < 4; ++channel )
	{
		vertex.color[channel] = source.color[0][channel] / 255.0f;
	}
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
	const std::vector<vk_world_vertex_t> &vertices,
	const std::vector<uint32_t> &indices,
	uint32_t firstIndex,
	uint32_t indexCount,
	qhandle_t shader,
	const qhandle_t lightmaps[MAXLIGHTMAPS],
	const byte lightmapStyles[MAXLIGHTMAPS],
	const byte vertexStyles[MAXLIGHTMAPS],
	uint32_t surfaceFlags,
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
	std::memcpy( batch.lightmaps, lightmaps, sizeof( batch.lightmaps ) );
	std::memcpy( batch.lightmapStyles, lightmapStyles, sizeof( batch.lightmapStyles ) );
	std::memcpy( batch.vertexStyles, vertexStyles, sizeof( batch.vertexStyles ) );
	for ( int axis = 0; axis < 3; ++axis )
	{
		batch.mins[axis] = std::numeric_limits<float>::max();
		batch.maxs[axis] = -std::numeric_limits<float>::max();
	}
	const uint32_t endIndex = std::min<uint32_t>(
		firstIndex + indexCount, static_cast<uint32_t>( indices.size() ) );
	for ( uint32_t index = firstIndex; index < endIndex; ++index )
	{
		if ( indices[index] >= vertices.size() )
		{
			continue;
		}
		const vk_world_vertex_t &vertex = vertices[indices[index]];
		for ( int axis = 0; axis < 3; ++axis )
		{
			batch.mins[axis] = std::min( batch.mins[axis], vertex.position[axis] );
			batch.maxs[axis] = std::max( batch.maxs[axis], vertex.position[axis] );
		}
	}
	batch.surfaceFlags = surfaceFlags;
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
	uint32_t surfaceFlags,
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
		batch.surfaceFlags = surfaceFlags;
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

	const int segmentWidth = ( width - 1 ) / 2;
	const int segmentHeight = ( height - 1 ) / 2;

	std::vector<vk_world_vertex_t> controlPoints;
	controlPoints.reserve( pointCount );
	for ( size_t i = 0; i < pointCount; ++i )
	{
		controlPoints.push_back( VK_WorldConvertVertex( drawVerts[firstVert + i] ) );
	}

	struct patch_sample_t
	{
		int segment;
		float fraction;
	};

	// Match the legacy meaning of r_subdivisions=4: four is a maximum curve
	// error in world units, not a fixed number of triangles. Large river bends
	// and terrain backdrops therefore need substantially more than four samples.
	auto buildSamples = [&]( bool horizontal )
	{
		const int segmentCount = horizontal ? segmentWidth : segmentHeight;
		const int crossCount = horizontal ? height : width;
		std::vector<int> segmentSubdivisions( static_cast<size_t>( segmentCount ), 1 );
		for ( int segment = 0; segment < segmentCount; ++segment )
		{
			float maximumError = 0.0f;
			for ( int cross = 0; cross < crossCount; ++cross )
			{
				const int x0 = horizontal ? segment * 2 : cross;
				const int y0 = horizontal ? cross : segment * 2;
				const int x1 = horizontal ? x0 + 1 : x0;
				const int y1 = horizontal ? y0 : y0 + 1;
				const int x2 = horizontal ? x0 + 2 : x0;
				const int y2 = horizontal ? y0 : y0 + 2;
				const float *p0 = controlPoints[static_cast<size_t>( y0 ) * width + x0].position;
				const float *p1 = controlPoints[static_cast<size_t>( y1 ) * width + x1].position;
				const float *p2 = controlPoints[static_cast<size_t>( y2 ) * width + x2].position;

				vec3_t midpoint = {
					( p0[0] + 2.0f * p1[0] + p2[0] ) * 0.25f,
					( p0[1] + 2.0f * p1[1] + p2[1] ) * 0.25f,
					( p0[2] + 2.0f * p1[2] + p2[2] ) * 0.25f };
				vec3_t chord;
				vec3_t offset;
				VectorSubtract( p2, p0, chord );
				VectorSubtract( midpoint, p0, offset );
				if ( VectorNormalize( chord ) != 0.0f )
				{
					VectorMA( offset, -DotProduct( offset, chord ), chord, offset );
				}
				maximumError = std::max( maximumError, VectorLength( offset ) );
			}

			if ( maximumError < 0.1f )
			{
				continue;
			}

			int subdivisions = 2;
			while ( maximumError > 4.0f && subdivisions < 64 )
			{
				maximumError *= 0.25f;
				subdivisions *= 2;
			}
			segmentSubdivisions[segment] = subdivisions;
		}

		// The original patch grid is capped at 129 samples per axis. Preserve that
		// guard while preferentially retaining detail in the curviest spans.
		auto sampleCount = [&]()
		{
			return 1 + std::accumulate(
				segmentSubdivisions.begin(), segmentSubdivisions.end(), 0 );
		};
		while ( sampleCount() > 129 )
		{
			auto largest = std::max_element(
				segmentSubdivisions.begin(), segmentSubdivisions.end() );
			if ( largest == segmentSubdivisions.end() || *largest <= 1 )
			{
				break;
			}
			*largest /= 2;
		}

		std::vector<patch_sample_t> samples;
		samples.reserve( static_cast<size_t>( sampleCount() ) );
		samples.push_back( { 0, 0.0f } );
		for ( int segment = 0; segment < segmentCount; ++segment )
		{
			const int subdivisions = segmentSubdivisions[segment];
			for ( int sample = 1; sample <= subdivisions; ++sample )
			{
				samples.push_back( {
					segment,
					static_cast<float>( sample ) / static_cast<float>( subdivisions ) } );
			}
		}
		return samples;
	};

	const std::vector<patch_sample_t> horizontalSamples = buildSamples( true );
	const std::vector<patch_sample_t> verticalSamples = buildSamples( false );
	const int outputWidth = static_cast<int>( horizontalSamples.size() );
	const int outputHeight = static_cast<int>( verticalSamples.size() );
	const size_t generatedVertices =
		static_cast<size_t>( outputWidth ) * static_cast<size_t>( outputHeight );
	const size_t generatedIndexes = static_cast<size_t>( outputWidth - 1 ) *
		static_cast<size_t>( outputHeight - 1 ) * 6;
	if ( !VK_WorldCanAppend( vertices, indices, generatedVertices, generatedIndexes ) )
	{
		return false;
	}

	const uint32_t baseVertex = static_cast<uint32_t>( vertices.size() );
	for ( int outputY = 0; outputY < outputHeight; ++outputY )
	{
		const int segmentY = verticalSamples[outputY].segment;
		const float v = verticalSamples[outputY].fraction;
		const float oneMinusV = 1.0f - v;
		const float basisV[3] = { oneMinusV * oneMinusV, 2.0f * v * oneMinusV, v * v };
		for ( int outputX = 0; outputX < outputWidth; ++outputX )
		{
			const int segmentX = horizontalSamples[outputX].segment;
			const float u = horizontalSamples[outputX].fraction;
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

static void VK_WorldLoadGridSize(
	const dheader_t *header,
	const byte *fileBase,
	size_t fileSize,
	float gridSize[3] )
{
	gridSize[0] = 64.0f;
	gridSize[1] = 64.0f;
	gridSize[2] = 128.0f;

	vk_bsp_lump_view_t entityLump = {};
	if ( !VK_BspGetLump(
		header, fileBase, fileSize, LUMP_ENTITIES, sizeof( byte ), "entity", &entityLump ) ||
		 entityLump.count == 0 )
	{
		return;
	}

	const char *entityBytes = static_cast<const char *>( entityLump.data );
	std::string entities( entityBytes, entityBytes + entityLump.count );
	entities.push_back( '\0' );
	const char *cursor = entities.c_str();
	COM_BeginParseSession( "Vulkan BSP worldspawn" );
	const char *token = COM_ParseExt( &cursor, qtrue );
	if ( Q_stricmp( token, "{" ) == 0 )
	{
		while ( true )
		{
			const char *key = COM_ParseExt( &cursor, qtrue );
			if ( key[0] == '\0' || Q_stricmp( key, "}" ) == 0 )
			{
				break;
			}
			const char *value = COM_ParseExt( &cursor, qtrue );
			if ( Q_stricmp( key, "gridsize" ) == 0 )
			{
				float parsed[3] = {};
				if ( std::sscanf( value, "%f %f %f", &parsed[0], &parsed[1], &parsed[2] ) == 3 &&
					 parsed[0] > 0.0f && parsed[1] > 0.0f && parsed[2] > 0.0f )
				{
					std::memcpy( gridSize, parsed, sizeof( parsed ) );
				}
			}
		}
	}
	COM_EndParseSession();
}

static void VK_WorldLoadLightGrid(
	const dheader_t *header,
	const byte *fileBase,
	size_t fileSize,
	vk_world_geometry_t *world )
{
	if ( world->inlineModels.empty() )
	{
		return;
	}

	vk_bsp_lump_view_t dataLump = {};
	vk_bsp_lump_view_t arrayLump = {};
	if ( !VK_BspGetLump(
			header, fileBase, fileSize, LUMP_LIGHTGRID, sizeof( dgrid_t ), "lightgrid", &dataLump ) ||
		 !VK_BspGetLump(
			header, fileBase, fileSize, LUMP_LIGHTARRAY, sizeof( uint16_t ), "lightarray", &arrayLump ) ||
		 dataLump.count == 0 || arrayLump.count == 0 )
	{
		return;
	}

	VK_WorldLoadGridSize( header, fileBase, fileSize, world->lightGridSize );
	const vk_world_inline_model_t &worldModel = world->inlineModels.front();
	for ( int axis = 0; axis < 3; ++axis )
	{
		const float minimum = worldModel.mins[axis] + 1.0f;
		const float maximum = worldModel.maxs[axis] - 1.0f;
		world->lightGridOrigin[axis] =
			world->lightGridSize[axis] * std::ceil( minimum / world->lightGridSize[axis] );
		const float gridMaximum =
			world->lightGridSize[axis] * std::floor( maximum / world->lightGridSize[axis] );
		world->lightGridBounds[axis] = static_cast<int>(
			( gridMaximum - world->lightGridOrigin[axis] ) / world->lightGridSize[axis] + 1.0f );
		if ( world->lightGridBounds[axis] <= 0 )
		{
			return;
		}
	}

	const size_t expectedArrayCount =
		static_cast<size_t>( world->lightGridBounds[0] ) *
		static_cast<size_t>( world->lightGridBounds[1] ) *
		static_cast<size_t>( world->lightGridBounds[2] );
	if ( arrayLump.count != expectedArrayCount )
	{
		ri.Printf( PRINT_WARNING,
			"rd-vulkan-lightgrid: array mismatch expected=%zu actual=%zu\n",
			expectedArrayCount, arrayLump.count );
		return;
	}

	const dgrid_t *gridData = static_cast<const dgrid_t *>( dataLump.data );
	world->lightGridData.assign( gridData, gridData + dataLump.count );
	const uint16_t *gridArray = static_cast<const uint16_t *>( arrayLump.data );
	world->lightGridArray.reserve( arrayLump.count );
	for ( size_t index = 0; index < arrayLump.count; ++index )
	{
		const uint16_t dataIndex = static_cast<uint16_t>( LittleShort( gridArray[index] ) );
		if ( static_cast<size_t>( dataIndex ) >= world->lightGridData.size() )
		{
			world->lightGridData.clear();
			world->lightGridArray.clear();
			ri.Printf( PRINT_WARNING,
				"rd-vulkan-lightgrid: invalid data reference %u/%zu at %zu\n",
				dataIndex, dataLump.count, index );
			return;
		}
		world->lightGridArray.push_back( dataIndex );
	}

	ri.Printf( PRINT_ALL,
		"rd-vulkan-lightgrid: loaded data=%zu array=%zu size=(%.0f %.0f %.0f) "
		"origin=(%.0f %.0f %.0f) bounds=(%d %d %d)\n",
		world->lightGridData.size(), world->lightGridArray.size(),
		world->lightGridSize[0], world->lightGridSize[1], world->lightGridSize[2],
		world->lightGridOrigin[0], world->lightGridOrigin[1], world->lightGridOrigin[2],
		world->lightGridBounds[0], world->lightGridBounds[1], world->lightGridBounds[2] );
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
	const dsurface_t *surfaces,
	const mapVert_t *drawVerts,
	size_t drawVertCount,
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
		for ( int axis = 0; axis < 3; ++axis )
		{
			model.mins[axis] = LittleFloat( bspModels[i].mins[axis] ) - 1.0f;
			model.maxs[axis] = LittleFloat( bspModels[i].maxs[axis] ) + 1.0f;
		}
		if ( VK_BspRangeValid( firstSurface, numSurfaces, surfaceCount ) )
		{
			model.firstSurface = static_cast<uint32_t>( firstSurface );
			model.surfaceCount = static_cast<uint32_t>( numSurfaces );
			for ( int surfaceOffset = 0; surfaceOffset < numSurfaces; ++surfaceOffset )
			{
				const dsurface_t &surface = surfaces[firstSurface + surfaceOffset];
				const int firstVert = LittleLong( surface.firstVert );
				const int numVerts = LittleLong( surface.numVerts );
				if ( numVerts <= 0 || !VK_BspRangeValid( firstVert, numVerts, drawVertCount ) )
				{
					continue;
				}
				for ( int axis = 0; axis < 3; ++axis )
				{
					model.facingNormal[axis] = LittleFloat( drawVerts[firstVert].normal[axis] );
				}
				model.hasFacingNormal = VectorNormalize( model.facingNormal ) > 0.0f;
				break;
			}
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
	vk.loggedYavinRiverDraw = false;
	vk.loggedFirstModelDraw = false;
	vk.loggedDynamicEffects = false;
	vk.loggedDynamicEffectOverflow = false;
	vk.loggedDiffuseModels = 0;
	vk.loggedScepterLine = false;
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
			const int shaderNum = LittleLong( surface.shaderNum );
			const uint32_t surfaceFlags = shaderNum >= 0 &&
				static_cast<size_t>( shaderNum ) < shaderLump.count
				? static_cast<uint32_t>( LittleLong( shaders[shaderNum].surfaceFlags ) )
				: 0u;
			const qhandle_t shader = VK_WorldRegisterSurfaceShader( surface, shaders, shaderLump.count );
			qhandle_t surfaceLightmaps[MAXLIGHTMAPS] = { 2, 2, 2, 2 };
			for ( int style = 0; style < MAXLIGHTMAPS; ++style )
			{
				const int lightmapIndex = LittleLong( surface.lightmapNum[style] );
				if ( lightmapIndex >= 0 && static_cast<size_t>( lightmapIndex ) < lightmaps.size() )
				{
					surfaceLightmaps[style] = lightmaps[lightmapIndex];
				}
			}
			const bool vertexLit = LittleLong( surface.lightmapNum[0] ) == lightmapByVertex;
			VK_WorldAppendBatch(
				batches,
				vertices,
				indices,
				firstBatchIndex,
				static_cast<uint32_t>( indices.size() ) - firstBatchIndex,
				shader,
				surfaceLightmaps,
				surface.lightmapStyles,
				surface.vertexStyles,
				surfaceFlags,
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
				surfaceFlags,
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
	VK_WorldLoadInlineModels(
		header, fileBase, fileSizeBytes, surfaceLump.count,
		surfaces, drawVerts, vertexLump.count, &world );
	VK_WorldLoadLightGrid( header, fileBase, fileSizeBytes, &world );
	VK_WorldLoadVisibilityData( header, fileBase, fileSizeBytes, surfaceLump.count, &world );
	VK_CreateWorldIndirectBatches( &world );
	for ( const vk_world_batch_t &batch : world.batches )
	{
		if ( batch.shader != 1 )
		{
			++world.texturedBatchCount;
		}
	}
	vk.world = std::move( world );

	std::array<uint32_t, MAX_LIGHT_STYLES> authoredStyleSurfaces = {};
	for ( const vk_world_batch_t &batch : vk.world.batches )
	{
		const byte *styles = batch.vertexLit ? batch.vertexStyles : batch.lightmapStyles;
		for ( int slot = 0; slot < MAXLIGHTMAPS; ++slot )
		{
			if ( styles[slot] >= LS_UNUSED || styles[slot] >= MAX_LIGHT_STYLES )
			{
				break;
			}
			++authoredStyleSurfaces[styles[slot]];
		}
	}
	std::string authoredStyleSummary;
	for ( size_t style = 0; style < authoredStyleSurfaces.size(); ++style )
	{
		if ( authoredStyleSurfaces[style] == 0 )
		{
			continue;
		}
		char entry[48];
		std::snprintf(
			entry, sizeof( entry ), "%s%zu:%u",
			authoredStyleSummary.empty() ? "" : " ",
			style,
			authoredStyleSurfaces[style] );
		authoredStyleSummary += entry;
	}
	ri.Printf( PRINT_ALL, "rd-vulkan-lightstyles: authored surface counts [%s]\n",
		authoredStyleSummary.empty() ? "none" : authoredStyleSummary.c_str() );

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
	vk.scenePolys.clear();
	vk.sceneLights.clear();
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

void VK_Backend_AddPoly( qhandle_t shader, int vertexCount, const polyVert_t *vertices )
{
	if ( vertexCount <= 0 || vertices == nullptr )
	{
		return;
	}
	const uint32_t retainedVertexCount = vk.scenePolyVertexCount;
	++vk.scenePolyCount;
	vk.scenePolyVertexCount += static_cast<uint32_t>( vertexCount );
	if ( vk.scenePolys.size() >= 8192 || vertexCount > 4096 ||
		 retainedVertexCount >= 262144u ||
		 static_cast<uint32_t>( vertexCount ) > 262144u - retainedVertexCount )
	{
		return;
	}
	vk_scene_poly_t poly = {};
	poly.shader = shader;
	poly.vertices.assign( vertices, vertices + vertexCount );
	vk.scenePolys.push_back( std::move( poly ) );
}

void VK_Backend_AddLight(
	const vec3_t origin,
	float intensity,
	float red,
	float green,
	float blue )
{
	if ( origin == nullptr || intensity <= 0.0f || vk.sceneLights.size() >= MAX_DLIGHTS )
	{
		return;
	}
	vk_dynamic_light_t light = {};
	VectorCopy( origin, light.origin );
	light.radius = intensity;
	light.color[0] = red;
	light.color[1] = green;
	light.color[2] = blue;
	vk.sceneLights.push_back( light );
	vk.sceneLightCount = static_cast<uint32_t>( vk.sceneLights.size() );
}

void VK_Backend_GetLightStyle( int style, color4ub_t color )
{
	if ( style < 0 || style >= MAX_LIGHT_STYLES || color == nullptr )
	{
		return;
	}
	std::memcpy( color, vk.lightStyles[style].data(), vk.lightStyles[style].size() );
}

void VK_Backend_SetLightStyle( int style, int color )
{
	if ( style < 0 || style >= MAX_LIGHT_STYLES )
	{
		ri.Error( ERR_DROP, "VK_Backend_SetLightStyle: %d is out of range", style );
		return;
	}
	if ( std::memcmp( vk.lightStyles[style].data(), &color, vk.lightStyles[style].size() ) != 0 )
	{
		std::memcpy( vk.lightStyles[style].data(), &color, vk.lightStyles[style].size() );
		if ( vk.loggedLightStyleUpdates < 12 )
		{
			ri.Printf( PRINT_ALL,
				"rd-vulkan-lighting: style=%d rgba=(%u %u %u %u)\n",
				style,
				static_cast<unsigned>( vk.lightStyles[style][0] ),
				static_cast<unsigned>( vk.lightStyles[style][1] ),
				static_cast<unsigned>( vk.lightStyles[style][2] ),
				static_cast<unsigned>( vk.lightStyles[style][3] ) );
			++vk.loggedLightStyleUpdates;
		}
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
		if ( ( refdef->rdflags & RDF_SKYBOXPORTAL ) != 0 )
		{
			vk.portalRefdef = *refdef;
			vk.havePortalRefdef = true;
			vk.portalEntities = vk.sceneEntities;
			vk.portalPolys = vk.scenePolys;
			vk.portalLights = vk.sceneLights;
		}
		else
		{
			vk.worldRefdef = *refdef;
			vk.haveWorldRefdef = true;
			vk.worldEntities = vk.sceneEntities;
			vk.worldPolys = vk.scenePolys;
			vk.worldLights = vk.sceneLights;
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
	}
	else
	{
		vk_scene_submission_t scene = {};
		scene.refdef = *refdef;
		scene.entities = vk.sceneEntities;
		scene.polys = vk.scenePolys;
		scene.lights = vk.sceneLights;
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
	VK_UpdateVideoMaps();
	vk.rects.clear();
	vk.sceneRenderedThisFrame = false;
	vk.sceneWorldRenderedThisFrame = false;
	vk.worldEntities.clear();
	vk.worldPolys.clear();
	vk.worldLights.clear();
	vk.haveWorldRefdef = false;
	vk.havePortalRefdef = false;
	vk.portalEntities.clear();
	vk.portalPolys.clear();
	vk.portalLights.clear();
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
		const bool yavinRiverMaterial = Q_stricmp( name, "textures/h_evil/lakewater" ) == 0;
		const bool yavinPoolMaterial = Q_stricmp( name, "textures/common/Water_Yavin2" ) == 0;
		const bool auditMaterial = yavinRiverMaterial || yavinPoolMaterial;
		const qhandle_t handle = static_cast<qhandle_t>( vk.textures.size() );
		vk.textures.emplace_back();
		vk.materials.emplace_back();
		vk.materials[handle].polygonOffset = definition->polygonOffset;
		vk.textureNames.push_back( { name, handle } );
		if ( definition->polygonOffset )
		{
			static uint32_t loggedPolygonOffsetMaterials = 0;
			if ( loggedPolygonOffsetMaterials++ < 32 )
			{
				ri.Printf( PRINT_ALL,
					"rd-vulkan-material: polygonOffset shader=%s handle=%d\n",
					name, handle );
			}
		}
		for ( const vk_shader_stage_definition_t &stageDefinition : definition->stages )
		{
			if ( stageDefinition.detail && vk.detailTexturesCvar != nullptr &&
				 vk.detailTexturesCvar->integer == 0 )
			{
				continue;
			}
			vk_material_stage_t stage = {};
			stage.videoHandle = -1;
			stage.lightmap = Q_stricmp( stageDefinition.imageName.c_str(), "$lightmap" ) == 0;
			if ( !stageDefinition.videoName.empty() )
			{
				stage.videoMap = true;
				stage.videoHandle = ri.CIN_PlayCinematic(
					stageDefinition.videoName.c_str(),
					0, 0, 256, 256,
					CIN_loop | CIN_silent | CIN_shader,
					nullptr );
				stage.texture = stage.videoHandle < 0 ? 2 : VK_CreateCinematicTexture( 256, 256 );
				if ( stage.videoHandle >= 0 && stage.texture > 2 )
				{
					vk.videoMaps.push_back( {
						stage.videoHandle, stage.texture, 256, 256, false, 0, 0, false } );
					ri.Printf( PRINT_ALL,
						"rd-vulkan: cinematic material %s -> client %d texture %d\n",
						stageDefinition.videoName.c_str(), stage.videoHandle, stage.texture );
				}
			}
			else
			{
				stage.texture = stage.lightmap ? 2 : VK_FindOrLoadImage( stageDefinition.imageName.c_str() );
			}
			stage.blendMode = stageDefinition.blendMode;
			stage.alphaTest = stageDefinition.alphaTest;
			stage.depthWrite = stageDefinition.depthWrite;
			stage.clampMap = stageDefinition.clampMap;
			stage.environmentMap = stageDefinition.environmentMap;
			stage.glow = stageDefinition.glow;
			stage.effectBoost =
				stageDefinition.surfaceSprite.type == VK_SURFACE_SPRITE_EFFECT ||
				Q_stricmp( name, "gfx/effects/water_splash" ) == 0;
			stage.yavinRiverStage = yavinRiverMaterial
				? static_cast<uint32_t>( vk.materials[handle].stages.size() + 1 )
				: 0;
			stage.yavinWaterBase = yavinPoolMaterial && vk.materials[handle].stages.empty();
			stage.yavinWaterDetail = yavinPoolMaterial &&
				Q_stricmp( stageDefinition.imageName.c_str(), "textures/common/stars" ) == 0;
			stage.waterWake = Q_stricmp( name, "wake" ) == 0;
			stage.surfaceSprite = stageDefinition.surfaceSprite;
			stage.alpha = stageDefinition.alpha;
			stage.scroll[0] = stageDefinition.scroll[0];
			stage.scroll[1] = stageDefinition.scroll[1];
			stage.tcScale[0] = stageDefinition.tcScale[0];
			stage.tcScale[1] = stageDefinition.tcScale[1];
			stage.rotateSpeed = stageDefinition.rotateSpeed;
			stage.stretchType = stageDefinition.stretchType;
			std::memcpy( stage.stretch, stageDefinition.stretch, sizeof( stage.stretch ) );
			std::memcpy( stage.turbulence, stageDefinition.turbulence, sizeof( stage.turbulence ) );
			std::memcpy( stage.color, stageDefinition.color, sizeof( stage.color ) );
			stage.vertexColor = stageDefinition.vertexColor || stageDefinition.lightingDiffuse;
			stage.entityColor = stageDefinition.entityColor;
			stage.lightingDiffuse = stageDefinition.lightingDiffuse;
			stage.lightingDiffuseEntity = stageDefinition.lightingDiffuseEntity;
			vk.materials[handle].stages.push_back( stage );
		}
		ri.Printf( PRINT_ALL, "rd-vulkan: material %d: %s (%zu stages)\n",
			handle, name, vk.materials[handle].stages.size() );
		if ( auditMaterial && vk.materialAuditCvar != nullptr &&
			 vk.materialAuditCvar->integer != 0 )
		{
			for ( size_t stageIndex = 0;
				  stageIndex < vk.materials[handle].stages.size(); ++stageIndex )
			{
				const vk_material_stage_t &stage = vk.materials[handle].stages[stageIndex];
				const vk_shader_stage_definition_t &source = definition->stages[stageIndex];
				ri.Printf( PRINT_ALL,
					"rd-vulkan-material-audit: register material=%s shader=%d stage=%zu "
					"image=%s texture=%d blend=%s alpha=%.4f color=(%.3f %.3f %.3f %.3f) "
					"scroll=(%.4f %.4f) lightmap=%d depthWrite=%d\n",
					name, handle, stageIndex, source.imageName.c_str(), stage.texture,
					VK_BlendModeName( stage.blendMode ), stage.alpha,
					stage.color[0], stage.color[1], stage.color[2], stage.color[3],
					stage.scroll[0], stage.scroll[1], stage.lightmap, stage.depthWrite );
			}
		}
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

qhandle_t VK_Backend_RegisterTextureNoMip( const char *name )
{
	const qhandle_t handle = VK_Backend_RegisterTexture( name );
	if ( handle > 0 )
	{
		vk.clampTextureHandles.insert( handle );
	}
	return handle;
}

static void VK_Backend_DrawPic(
	float x, float y, float w, float h,
	float s1, float t1, float s2, float t2,
	float angle, float pivotX, float pivotY,
	qhandle_t shader )
{
	if ( shader <= 0 || static_cast<size_t>( shader ) >= vk.textures.size() )
	{
		shader = 2;
	}

	const bool disruptorScope = VK_IsDisruptorScopeShader( shader );
	const bool forceSenseOverlay = VK_TextureHandleHasName( shader, "gfx/2d/jsense" );
	const bool vignetteOverlay = VK_TextureHandleHasName( shader, "gfx/vignette" );
	const bool headLockedOverlay = forceSenseOverlay || vignetteOverlay;
	const bool fullScreenHeadLockedOverlay = headLockedOverlay &&
		x <= 0.01f && y <= 0.01f && w >= 639.99f && h >= 479.99f;
	if ( disruptorScope && !vk.loggedDisruptorScope )
	{
		ri.Printf( PRINT_ALL,
			"rd-vulkan-scope: enabling Tenloss stereo artwork and material blending\n" );
		vk.loggedDisruptorScope = true;
	}
	float materialIntensity = 1.0f;
	if ( forceSenseOverlay )
	{
		const cvar_t *intensity =
			ri.Cvar_Get( "r_vulkanForceSenseIntensity", "6.0", CVAR_ARCHIVE );
		materialIntensity = intensity != nullptr
			? std::max( 0.0f, std::min( 8.0f, intensity->value ) )
			: 6.0f;
	}

	if ( static_cast<size_t>( shader ) < vk.materials.size() && !vk.materials[shader].stages.empty() )
	{
		const float seconds = static_cast<float>( ri.Milliseconds() ) * 0.001f;
		if ( forceSenseOverlay && fullScreenHeadLockedOverlay )
		{
			const cvar_t *strengthCvar =
				ri.Cvar_Get( "r_vulkanForceSenseVignetteStrength", "2.0", CVAR_ARCHIVE );
			const float strength = strengthCvar != nullptr
				? std::max( 0.0f, std::min( 4.0f, strengthCvar->value ) )
				: 2.0f;
			const qhandle_t vignetteTexture = vk.materials[shader].stages.front().texture;
			const auto appendVignette = [&]( float alpha )
			{
				const float black[4] = { 0.0f, 0.0f, 0.0f, alpha };
				VK_Backend_AppendScreenRect(
					x, y, w, h, black, 0.0f, 0.0f, 1.0f, 1.0f,
					vignetteTexture, VK_BLEND_ALPHA, angle, pivotX, pivotY,
					false, false, 0.0f, false, true );
			};
			const int fullPasses = static_cast<int>( std::floor( strength ) );
			for ( int pass = 0; pass < fullPasses; ++pass )
			{
				appendVignette( 1.0f );
			}
			const float partialPass = strength - static_cast<float>( fullPasses );
			if ( partialPass > 0.001f )
			{
				appendVignette( partialPass );
			}
			static bool loggedInjectedVignette = false;
			if ( !loggedInjectedVignette )
			{
				ri.Printf( PRINT_ALL,
					"rd-vulkan-overlay: injected lens-local Force Sense vignette "
					"texture=%d strength=%.2f\n",
					vignetteTexture, strength );
				loggedInjectedVignette = true;
			}
		}
		for ( const vk_material_stage_t &stage : vk.materials[shader].stages )
		{
			if ( stage.surfaceSprite.type != VK_SURFACE_SPRITE_NONE )
			{
				continue;
			}
			float color[4];
			for ( int component = 0; component < 3; ++component )
			{
				color[component] = stage.color[component] * materialIntensity;
				if ( stage.vertexColor )
				{
					color[component] *= vk.currentColor[component];
				}
			}
			color[3] = stage.color[3] * vk.currentColor[3] * stage.alpha;
			const float scrollS = stage.scroll[0] * seconds;
			const float scrollT = stage.scroll[1] * seconds;
			float stretchScale = 1.0f;
			if ( stage.stretchType != VK_WAVE_NONE )
			{
				const float wave = stage.stretch[0] + stage.stretch[1] *
					VK_EvaluateWaveform(
						stage.stretchType, stage.stretch[2] + seconds * stage.stretch[3] );
				if ( std::fabs( wave ) > 0.0001f )
				{
					stretchScale = 1.0f / wave;
				}
			}
			const auto transformCoordinate = [stretchScale]( float coordinate, float scale, float scroll )
			{
				return 0.5f + ( coordinate * scale - 0.5f ) * stretchScale + scroll;
			};
			VK_Backend_AppendScreenRect(
				x, y, w, h, color,
				transformCoordinate( s1, stage.tcScale[0], scrollS ),
				transformCoordinate( t1, stage.tcScale[1], scrollT ),
				transformCoordinate( s2, stage.tcScale[0], scrollS ),
				transformCoordinate( t2, stage.tcScale[1], scrollT ),
				stage.texture, stage.blendMode,
				angle, pivotX, pivotY, disruptorScope,
				forceSenseOverlay ? false : !stage.clampMap,
				-stage.rotateSpeed * seconds, headLockedOverlay, false,
				forceSenseOverlay && fullScreenHeadLockedOverlay );
		}
		return;
	}
	if ( static_cast<size_t>( shader ) < vk.materials.size() &&
		 vk.materials[shader].stages.empty() &&
		 static_cast<size_t>( shader ) < vk.textures.size() &&
		 vk.textures[shader].descriptorSet == VK_NULL_HANDLE )
	{
		// A registered shader can become empty when every stage is filtered,
		// notably the detail-only rotating Tenloss insert. Its material handle
		// is not an image handle; drawing it as an untextured rectangle produces
		// an opaque white screen.
		return;
	}
	if ( vignetteOverlay && fullScreenHeadLockedOverlay )
	{
		return;
	}
	VK_Backend_AppendScreenRect(
		x, y, w, h, vk.currentColor, s1, t1, s2, t2,
		shader, VK_BLEND_ALPHA, angle, pivotX, pivotY, disruptorScope,
		!VK_TextureUsesClamp( shader ), 0.0f, headLockedOverlay );
}

void VK_Backend_DrawStretchPic(
	float x, float y, float w, float h,
	float s1, float t1, float s2, float t2,
	qhandle_t shader )
{
	VK_Backend_DrawPic( x, y, w, h, s1, t1, s2, t2, 0.0f, x, y, shader );
}

void VK_Backend_DrawRotatePic(
	float x, float y, float w, float h,
	float s1, float t1, float s2, float t2,
	float angle, qhandle_t shader, bool centerPivot )
{
	if ( centerPivot )
	{
		VK_Backend_DrawPic(
			x - w * 0.5f, y - h * 0.5f, w, h,
			s1, t1, s2, t2, angle, x, y, shader );
	}
	else
	{
		VK_Backend_DrawPic(
			x, y, w, h, s1, t1, s2, t2,
			angle, x + w, y, shader );
	}
}

void VK_Backend_UploadCinematic(
	int cols,
	int rows,
	const byte *data,
	int client,
	qboolean dirty )
{
	vk_cinematic_texture_t *cinematic = VK_FindCinematicTexture( vk.videoMaps, client );
	if ( cinematic == nullptr )
	{
		return;
	}
	++cinematic->uploadCount;
	if ( !dirty || cols <= 0 || rows <= 0 || data == nullptr )
	{
		return;
	}
	if ( static_cast<uint32_t>( cols ) != cinematic->width ||
		 static_cast<uint32_t>( rows ) != cinematic->height )
	{
		ri.Printf( PRINT_WARNING,
			"rd-vulkan: cinematic client %d changed dimensions from %ux%u to %dx%d\n",
			client, cinematic->width, cinematic->height, cols, rows );
		return;
	}
	if ( !cinematic->loggedUpload )
	{
		byte minimum = 255;
		byte maximum = 0;
		const size_t byteCount = static_cast<size_t>( cols ) * rows * 4;
		for ( size_t i = 0; i < byteCount; ++i )
		{
			minimum = std::min( minimum, data[i] );
			maximum = std::max( maximum, data[i] );
		}
		ri.Printf( PRINT_ALL,
			"rd-vulkan: first videoMap frame client=%d size=%dx%d range=%u..%u\n",
			client, cols, rows, minimum, maximum );
		cinematic->loggedUpload = true;
	}
	if ( !cinematic->loggedNonzero )
	{
		byte maximum = 0;
		const size_t byteCount = static_cast<size_t>( cols ) * rows * 4;
		for ( size_t i = 0; i < byteCount; ++i )
		{
			maximum = std::max( maximum, data[i] );
		}
		if ( maximum != 0 )
		{
			ri.Printf( PRINT_ALL,
				"rd-vulkan: videoMap client=%d produced first non-black frame on upload=%u\n",
				client, cinematic->uploadCount );
			cinematic->loggedNonzero = true;
		}
	}
	VK_UpdateTexturePixels(
		cinematic->texture,
		cinematic->width,
		cinematic->height,
		data );
}

void VK_Backend_DrawStretchRaw(
	int x,
	int y,
	int w,
	int h,
	int cols,
	int rows,
	const byte *data,
	int client,
	qboolean dirty )
{
	if ( cols <= 0 || rows <= 0 || data == nullptr )
	{
		return;
	}
	vk_cinematic_texture_t *cinematic = VK_FindCinematicTexture( vk.rawCinematics, client );
	if ( cinematic == nullptr || cinematic->width != static_cast<uint32_t>( cols ) ||
		 cinematic->height != static_cast<uint32_t>( rows ) )
	{
		const qhandle_t texture = VK_CreateCinematicTexture(
			static_cast<uint32_t>( cols ), static_cast<uint32_t>( rows ) );
		if ( texture <= 2 )
		{
			return;
		}
		vk.rawCinematics.push_back( {
			client, texture, static_cast<uint32_t>( cols ), static_cast<uint32_t>( rows ),
			false, 0, 0, false } );
		cinematic = &vk.rawCinematics.back();
		dirty = qtrue;
	}
	if ( dirty )
	{
		if ( !cinematic->loggedUpload )
		{
			byte minimum = 255;
			byte maximum = 0;
			const size_t byteCount = static_cast<size_t>( cols ) * rows * 4;
			for ( size_t i = 0; i < byteCount; ++i )
			{
				minimum = std::min( minimum, data[i] );
				maximum = std::max( maximum, data[i] );
			}
			ri.Printf( PRINT_ALL,
				"rd-vulkan: first raw cinematic frame client=%d size=%dx%d range=%u..%u\n",
				client, cols, rows, minimum, maximum );
			cinematic->loggedUpload = true;
		}
		VK_UpdateTexturePixels(
			cinematic->texture,
			cinematic->width,
			cinematic->height,
			data );
	}
	if ( !vk.loggedRawStretch )
	{
		ri.Printf( PRINT_ALL,
			"rd-vulkan: DrawStretchRaw cinematic queued at %d,%d %dx%d (%dx%d client=%d)\n",
			x, y, w, h, cols, rows, client );
		vk.loggedRawStretch = true;
	}

	const float rawColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	VK_Backend_AppendScreenRect(
		static_cast<float>( x ),
		static_cast<float>( y ),
		static_cast<float>( w ),
		static_cast<float>( h ),
		rawColor,
		0.5f / static_cast<float>( cols ),
		0.5f / static_cast<float>( rows ),
		( static_cast<float>( cols ) - 0.5f ) / static_cast<float>( cols ),
		( static_cast<float>( rows ) - 0.5f ) / static_cast<float>( rows ),
		cinematic->texture,
		VK_BLEND_OPAQUE );
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

	XrCompositionLayerBaseHeader *layers[2] = {};
	XrCompositionLayerProjection projectionLayer = {};
	XrCompositionLayerProjectionView projectionViews[VK_BACKEND_EYE_COUNT] = {};
	XrCompositionLayerProjection screenBackgroundLayer = {};
	XrCompositionLayerProjectionView screenBackgroundViews[VK_BACKEND_EYE_COUNT] = {};
	XrCompositionLayerQuad screenLayer = {};
	uint32_t layerCount = 0;
	const bool requestedScreenLayer = !vk.sceneWorldRenderedThisFrame &&
		ri.TBXR_useScreenLayer != nullptr && ri.TBXR_useScreenLayer();
	bool holdScreenLayer = false;
	if ( requestedScreenLayer )
	{
		vk.screenLayerTransitionHeld = false;
	}
	else if ( vk.screenLayerStateKnown && vk.screenLayerActive &&
		vk.screenLayerContentValid && !vk.screenLayerTransitionHeld )
	{
		holdScreenLayer = true;
		vk.screenLayerTransitionHeld = true;
		ri.Printf( PRINT_ALL,
			"rd-vulkan: holding the last screen-layer image for projection handoff\n" );
	}
	const bool useScreenLayer = requestedScreenLayer || holdScreenLayer;

	if ( !vk.screenLayerStateKnown || useScreenLayer != vk.screenLayerActive )
	{
		ri.Printf( PRINT_ALL, "rd-vulkan: OpenXR composition mode: %s\n",
			useScreenLayer ? "quad screen layer" : "stereo projection layer" );
		vk.screenLayerActive = useScreenLayer;
		vk.screenLayerStateKnown = true;
		if ( !useScreenLayer )
		{
			vk.screenLayerPoseValid = false;
			vk.screenLayerContentValid = false;
			vk.screenLayerTransitionHeld = false;
		}
		else if ( !holdScreenLayer )
		{
			vk.screenLayerPoseValid = false;
		}
	}

	auto appendScreenComposition = [&]( bool includeBackground )
	{
		if ( includeBackground )
		{
			for ( int eye = 0; eye < VK_BACKEND_EYE_COUNT; ++eye )
			{
				screenBackgroundViews[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
				screenBackgroundViews[eye].pose = vk.views[eye].pose;
				screenBackgroundViews[eye].fov = vk.views[eye].fov;
				screenBackgroundViews[eye].subImage.swapchain = vk.colorSwapchain[1];
				screenBackgroundViews[eye].subImage.imageRect.extent.width =
					static_cast<int32_t>( vk.viewConfiguration[1].recommendedImageRectWidth );
				screenBackgroundViews[eye].subImage.imageRect.extent.height =
					static_cast<int32_t>( vk.viewConfiguration[1].recommendedImageRectHeight );
			}
			screenBackgroundLayer.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
			screenBackgroundLayer.space = vk.localSpace;
			screenBackgroundLayer.viewCount = ARRAY_LEN( screenBackgroundViews );
			screenBackgroundLayer.views = screenBackgroundViews;
			layers[layerCount++] = reinterpret_cast<XrCompositionLayerBaseHeader *>(
				&screenBackgroundLayer );
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
		layers[layerCount++] =
			reinterpret_cast<XrCompositionLayerBaseHeader *>( &screenLayer );
	};

	if ( frameState.shouldRender && vk.viewsValid && VK_CreateSwapchains() )
	{
		if ( useScreenLayer )
		{
			if ( holdScreenLayer )
			{
				appendScreenComposition( true );
			}
			else
			{
				const float screenTints[VK_BACKEND_EYE_COUNT][4] = {
					{ 0.08f, 0.10f, 0.12f, 1.0f },
					{ 0.0f, 0.0f, 0.0f, 1.0f },
				};
				const bool clearOnly[VK_BACKEND_EYE_COUNT] = { false, true };
				const bool screenRendered = VK_RenderEyes( screenTints, clearOnly );
				const bool backgroundRendered = screenRendered;
				if ( !vk.screenLayerPoseValid )
				{
					VK_CaptureScreenLayerPose( frameState.predictedDisplayTime );
				}
				if ( screenRendered )
				{
					appendScreenComposition( backgroundRendered );
				}
				vk.screenLayerContentValid = screenRendered && backgroundRendered;
			}
		}
		else
		{
			const float eyeTints[VK_BACKEND_EYE_COUNT][4] = {
				{ 0.02f, 0.20f, 1.00f, 1.0f },
				{ 1.00f, 0.22f, 0.04f, 1.0f },
			};

			const bool clearOnly[VK_BACKEND_EYE_COUNT] = { false, false };
			const bool rendered = VK_RenderEyes( eyeTints, clearOnly );
			for ( int eye = 0; eye < VK_BACKEND_EYE_COUNT; ++eye )
			{
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
