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

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

refimport_t ri;
cvar_t *se_language;
cvar_t *com_buildScript;

void QDECL Com_Printf( const char *format, ... )
{
	char message[4096];
	va_list args;
	va_start( args, format );
	std::vsnprintf( message, sizeof( message ), format, args );
	va_end( args );
	ri.Printf( PRINT_ALL, "%s", message );
}

void NORETURN QDECL Com_Error( int level, const char *format, ... )
{
	char message[4096];
	va_list args;
	va_start( args, format );
	std::vsnprintf( message, sizeof( message ), format, args );
	va_end( args );
	ri.Error( level, "%s", message );
	std::abort();
}

static window_t trWindow;
static bool trWindowInitialized;
static bool trFontsInitialized;
void *R_Malloc( int size, memtag_t tag, qboolean zeroIt )
{
	return ri.Malloc( size, tag, zeroIt, 4 );
}

void R_Free( void *ptr )
{
	ri.Z_Free( ptr );
}

int R_MemSize( memtag_t tag )
{
	return ri.Z_MemSize( tag );
}

void R_MorphMallocTag( void *ptr, memtag_t tag )
{
	ri.Z_MorphMallocTag( ptr, tag );
}

qhandle_t RE_RegisterShaderNoMip( const char *name )
{
	return VK_Backend_RegisterTextureNoMip( name );
}

void RE_SetColor( const float *color )
{
	VK_Backend_SetColor( color );
}

void RE_StretchPic(
	float x, float y, float w, float h,
	float s1, float t1, float s2, float t2,
	qhandle_t shader )
{
	VK_Backend_DrawStretchPic( x, y, w, h, s1, t1, s2, t2, shader );
}

static int RE_GetAnimationCFG( const char *filename, char *destination, int destinationSize )
{
	char *buffer = nullptr;
	const long length = ri.FS_ReadFile( filename, reinterpret_cast<void **>( &buffer ) );
	if ( length <= 0 || buffer == nullptr )
	{
		return 0;
	}

	if ( destination != nullptr && destinationSize > 0 )
	{
		const int copyLength = std::min<int>( static_cast<int>( length ), destinationSize - 1 );
		std::memcpy( destination, buffer, copyLength );
		destination[copyLength] = '\0';
	}

	ri.FS_FreeFile( buffer );
	return static_cast<int>( length );
}

static void RE_RegisterMedia_LevelLoadEnd()
{
	if ( ri.gbAlreadyDoingLoad != nullptr )
	{
		qboolean *loading = ri.gbAlreadyDoingLoad();
		if ( loading != nullptr )
		{
			*loading = qfalse;
			ri.Printf( PRINT_ALL, "rd-vulkan: completed level registration; future load commands enabled\n" );
		}
	}
}

#ifndef JK2_MODE
unsigned int AnyLanguage_ReadCharFromString( char **text, qboolean *trailingPunctuation )
{
	int advance = 0;
	const unsigned int character = AnyLanguage_ReadCharFromString( *text, &advance, trailingPunctuation );
	*text += advance;
	return character;
}
#endif

static void RE_BeginRegistration( glconfig_t *config, intptr_t )
{
	const bool backendReady = VK_Backend_Init();
	R_ImageLoader_Init();
	se_language = ri.Cvar_Get( "se_language", "english", CVAR_ARCHIVE | CVAR_NORESTART );
	com_buildScript = ri.Cvar_Get( "com_buildScript", "0", 0 );
	if ( !trFontsInitialized )
	{
		R_InitFonts();
		trFontsInitialized = true;
	}

	std::memset( config, 0, sizeof( *config ) );
	if ( !trWindowInitialized )
	{
		windowDesc_t windowDesc = {};
		windowDesc.api = GRAPHICS_API_VULKAN;
		trWindow = ri.WIN_Init( &windowDesc, config );
		trWindowInitialized = true;
	}

	config->vidWidth = VK_Backend_GetRecommendedWidth();
	config->vidHeight = VK_Backend_GetRecommendedHeight();
	config->colorBits = 32;
	config->depthBits = 24;
	config->stencilBits = 8;
	config->deviceSupportsGamma = qfalse;
	config->renderer_string = "JKXRL Vulkan renderer scaffold";
	config->vendor_string = "JKXRL";
	config->version_string = backendReady ? "Vulkan/OpenXR bootstrap" : "Vulkan scaffold";
	config->extensions_string = "";
}

extern "C" Q_EXPORT refexport_t* QDECL GetRefAPI( int apiVersion, refimport_t *refimp )
{
	static refexport_t re;

	if ( refimp == nullptr ) {
		return nullptr;
	}

	ri = *refimp;
	std::memset( &re, 0, sizeof( re ) );

	if ( apiVersion != REF_API_VERSION ) {
		ri.Printf( PRINT_ALL, "rd-vulkan: mismatched REF_API_VERSION: expected %i, got %i\n",
			REF_API_VERSION, apiVersion );
		return nullptr;
	}

	re.Shutdown = []( qboolean destroyWindow, qboolean ) {
		if ( trFontsInitialized )
		{
			R_ShutdownFonts();
			trFontsInitialized = false;
		}
		if ( !destroyWindow )
		{
			VK_Backend_SoftShutdown();
			return;
		}
		VK_Backend_Shutdown();
		if ( trWindowInitialized )
		{
			ri.WIN_Shutdown();
			trWindow = {};
			trWindowInitialized = false;
		}
	};
	re.BeginRegistration = RE_BeginRegistration;
	re.RegisterModel = VK_Backend_RegisterModel;
	re.RegisterSkin = VK_Backend_RegisterSkin;
	re.GetAnimationCFG = RE_GetAnimationCFG;
	re.RegisterShader = VK_Backend_RegisterTexture;
	re.RegisterShaderNoMip = RE_RegisterShaderNoMip;
	re.LoadWorld = VK_Backend_LoadWorld;
	re.R_LoadImage = R_LoadImage;

	re.RegisterMedia_LevelLoadBegin = []( const char *name, ForceReload_e, qboolean ) {
		VK_Backend_BeginLevelLoad( name );
	};
	re.RegisterMedia_LevelLoadEnd = RE_RegisterMedia_LevelLoadEnd;
	re.RegisterMedia_GetLevel = []() -> int { return 0; };
	re.RegisterModels_LevelLoadEnd = []( qboolean ) -> qboolean { return qfalse; };
	re.RegisterImages_LevelLoadEnd = []() -> qboolean { return qfalse; };

	re.SetWorldVisData = []( const byte * ) {};
	re.EndRegistration = []() {};

	re.ClearScene = VK_Backend_ClearScene;
	re.AddRefEntityToScene = VK_Backend_AddRefEntity;
	re.AddPolyToScene = VK_Backend_AddPoly;
	re.AddLightToScene = VK_Backend_AddLight;
	re.RenderScene = VK_Backend_RenderScene;
	re.GetLighting = []( const vec3_t, vec3_t, vec3_t, vec3_t ) -> qboolean { return qfalse; };

	re.SetColor = VK_Backend_SetColor;
	re.DrawStretchPic = []( float x, float y, float w, float h, float s1, float t1, float s2, float t2, qhandle_t shader ) {
		VK_Backend_DrawStretchPic( x, y, w, h, s1, t1, s2, t2, shader );
	};
	re.DrawRotatePic = []( float x, float y, float w, float h,
		float s1, float t1, float s2, float t2, float angle, qhandle_t shader ) {
		VK_Backend_DrawRotatePic(
			x, y, w, h, s1, t1, s2, t2, angle, shader, false );
	};
	re.DrawRotatePic2 = []( float x, float y, float w, float h,
		float s1, float t1, float s2, float t2, float angle, qhandle_t shader ) {
		VK_Backend_DrawRotatePic(
			x, y, w, h, s1, t1, s2, t2, angle, shader, true );
	};
	re.LAGoggles = []() {};
	re.Scissor = []( float, float, float, float ) {};

	re.DrawStretchRaw = []( int x, int y, int w, int h,
		int cols, int rows, const byte *data, int client, qboolean dirty ) {
		VK_Backend_DrawStretchRaw( x, y, w, h, cols, rows, data, client, dirty );
	};
	re.UploadCinematic = VK_Backend_UploadCinematic;

	re.BeginFrame = []( stereoFrame_t ) { VK_Backend_BeginFrame(); };
	re.EndFrame = []( int *, int * ) { VK_Backend_SubmitClearFrame(); };
	re.SubmitStereoFrame = []() {};
	re.VR_BeginStereoReplayCapture = []() -> qboolean { return qfalse; };
	re.VR_ReplayStereoFrame = []( stereoFrame_t, qboolean ) -> qboolean { return qfalse; };
	re.VR_CancelStereoReplayCapture = []() {};

	re.ProcessDissolve = []() -> qboolean { return qfalse; };
	re.InitDissolve = []( qboolean ) -> qboolean { return qfalse; };
	re.GetScreenShot = []( byte *, int, int ) {};

#ifdef JK2_MODE
	re.SaveJPGToBuffer = []( byte *, size_t, int, int, int, byte *, int, bool ) -> size_t { return 0; };
	re.LoadJPGFromBuffer = []( byte *, size_t, byte **, int *, int * ) {};
#endif

	re.TempRawImage_ReadFromFile = []( const char *, int *, int *, byte *, qboolean ) -> byte * { return nullptr; };
	re.TempRawImage_CleanUp = []() {};
	re.MarkFragments = []( int, const vec3_t *, const vec3_t, int, vec3_t, int, markFragment_t * ) -> int { return 0; };
	re.LerpTag = VK_Backend_LerpTag;
	re.ModelBounds = VK_Backend_ModelBounds;
	re.GetLightStyle = VK_Backend_GetLightStyle;
	re.SetLightStyle = VK_Backend_SetLightStyle;
	re.GetBModelVerts = []( int, vec3_t *, vec3_t ) {};
	re.WorldEffectCommand = VK_Backend_WorldEffectCommand;
	re.GetModelBounds = VK_Backend_GetModelBounds;

	re.RegisterFont = RE_RegisterFont;
	re.Font_HeightPixels = RE_Font_HeightPixels;
	re.Font_StrLenPixels = RE_Font_StrLenPixels;
	re.Font_DrawString = RE_Font_DrawString;
	re.Font_StrLenChars = RE_Font_StrLenChars;
	re.Language_IsAsian = Language_IsAsian;
	re.Language_UsesSpaces = Language_UsesSpaces;
	re.AnyLanguage_ReadCharFromString = []( char *text, int *advance, qboolean *trailingPunctuation ) -> unsigned int {
		return AnyLanguage_ReadCharFromString( text, advance, trailingPunctuation );
	};
	re.AnyLanguage_ReadCharFromString2 = []( char **text, qboolean *trailingPunctuation ) -> unsigned int {
		return AnyLanguage_ReadCharFromString( text, trailingPunctuation );
	};

	re.R_InitWorldEffects = VK_Backend_InitWorldEffects;
	re.R_ClearStuffToStopGhoul2CrashingThings = []() {};
	re.R_inPVS = []( vec3_t, vec3_t ) -> qboolean { return qfalse; };
	re.SVModelInit = []() {};

	re.tr_distortionAlpha = []() -> float * { return nullptr; };
	re.tr_distortionStretch = []() -> float * { return nullptr; };
	re.tr_distortionPrePost = []() -> qboolean * { return nullptr; };
	re.tr_distortionNegate = []() -> qboolean * { return nullptr; };

	re.GetWindVector = []( vec3_t, vec3_t ) -> bool { return false; };
	re.GetWindGusting = []( vec3_t ) -> bool { return false; };
	re.IsOutside = []( vec3_t ) -> bool { return false; };
	re.IsOutsideCausingPain = []( vec3_t ) -> float { return 0.0f; };
	re.GetChanceOfSaberFizz = []() -> float { return 0.0f; };
	re.IsShaking = []( vec3_t ) -> bool { return false; };
	re.AddWeatherZone = VK_Backend_AddWeatherZone;
	re.SetTempGlobalFogColor = []( vec3_t ) -> bool { return false; };
	re.SetRangedFog = []( float ) {};

	re.TheGhoul2InfoArray = TheGhoul2InfoArray;
	re.G2API_AddBolt = VK_G2API_AddBolt;
	re.G2API_AddBoltSurfNum = VK_G2API_AddBoltSurfNum;
	re.G2API_AddSurface = []( CGhoul2Info *, int, int, float, float, int ) -> int { return 0; };
	re.G2API_AnimateG2Models = []( CGhoul2Info_v &, int, CRagDollUpdateParams * ) {};
	re.G2API_AttachEnt = VK_G2API_AttachEnt;
	re.G2API_AttachG2Model = VK_G2API_AttachG2Model;
	re.G2API_CollisionDetect = VK_G2API_CollisionDetect;
	re.G2API_CleanGhoul2Models = VK_G2API_CleanGhoul2Models;
	re.G2API_CopyGhoul2Instance = VK_G2API_CopyGhoul2Instance;
	re.G2API_DetachEnt = []( int *boltInfo ) { if ( boltInfo != nullptr ) { *boltInfo = 0; } };
	re.G2API_DetachG2Model = VK_G2API_DetachG2Model;
	re.G2API_GetAnimFileName = VK_G2API_GetAnimFileName;
	re.G2API_GetAnimFileNameIndex = []( qhandle_t ) -> char * { return nullptr; };
	re.G2API_GetAnimFileInternalNameIndex = []( qhandle_t ) -> char * { return nullptr; };
	re.G2API_GetAnimIndex = VK_G2API_GetAnimIndex;
	re.G2API_GetAnimRange = VK_G2API_GetAnimRange;
	re.G2API_GetAnimRangeIndex = VK_G2API_GetAnimRangeIndex;
	re.G2API_GetBoneAnim = VK_G2API_GetBoneAnim;
	re.G2API_GetBoneAnimIndex = VK_G2API_GetBoneAnimIndex;
	re.G2API_GetBoneIndex = VK_G2API_GetBoneIndex;
	re.G2API_GetBoltMatrix = VK_G2API_GetBoltMatrix;
	re.G2API_GetGhoul2ModelFlags = VK_G2API_GetGhoul2ModelFlags;
	re.G2API_GetGLAName = VK_G2API_GetGLAName;
	re.G2API_GetParentSurface = VK_G2API_GetParentSurface;
	re.G2API_GetRagBonePos = []( CGhoul2Info_v &, const char *, vec3_t, vec3_t, vec3_t, vec3_t ) -> qboolean { return qfalse; };
	re.G2API_GetSurfaceIndex = VK_G2API_GetSurfaceIndex;
	re.G2API_GetSurfaceName = VK_G2API_GetSurfaceName;
	re.G2API_GetSurfaceRenderStatus = VK_G2API_GetSurfaceRenderStatus;
	re.G2API_GetTime = VK_G2API_GetTime;
	re.G2API_GiveMeVectorFromMatrix = VK_G2API_GiveMeVectorFromMatrix;
	re.G2API_HaveWeGhoul2Models = VK_G2API_HaveWeGhoul2Models;
	re.G2API_IKMove = []( CGhoul2Info_v &, int, sharedIKMoveParams_t * ) -> qboolean { return qfalse; };
	re.G2API_InitGhoul2Model = VK_G2API_InitGhoul2Model;
	re.G2API_IsPaused = VK_G2API_IsPaused;
	re.G2API_ListBones = []( CGhoul2Info *, int ) {};
	re.G2API_ListSurfaces = []( CGhoul2Info * ) {};
	re.G2API_LoadGhoul2Models = VK_G2API_LoadGhoul2Models;
	re.G2API_LoadSaveCodeDestructGhoul2Info = VK_G2API_LoadSaveCodeDestructGhoul2Info;
	re.G2API_PauseBoneAnim = VK_G2API_PauseBoneAnim;
	re.G2API_PauseBoneAnimIndex = VK_G2API_PauseBoneAnimIndex;
	re.G2API_PrecacheGhoul2Model = VK_G2API_PrecacheGhoul2Model;
	re.G2API_RagEffectorGoal = []( CGhoul2Info_v &, const char *, vec3_t ) -> qboolean { return qfalse; };
	re.G2API_RagEffectorKick = []( CGhoul2Info_v &, const char *, vec3_t ) -> qboolean { return qfalse; };
	re.G2API_RagForceSolve = []( CGhoul2Info_v &, qboolean ) -> qboolean { return qfalse; };
	re.G2API_RagPCJConstraint = []( CGhoul2Info_v &, const char *, vec3_t, vec3_t ) -> qboolean { return qfalse; };
	re.G2API_RagPCJGradientSpeed = []( CGhoul2Info_v &, const char *, const float ) -> qboolean { return qfalse; };
	re.G2API_RemoveBolt = VK_G2API_RemoveBolt;
	re.G2API_RemoveBone = []( CGhoul2Info *, const char * ) -> qboolean { return qfalse; };
	re.G2API_RemoveGhoul2Model = VK_G2API_RemoveGhoul2Model;
	re.G2API_RemoveSurface = []( CGhoul2Info *, const int ) -> qboolean { return qfalse; };
	re.G2API_SaveGhoul2Models = VK_G2API_SaveGhoul2Models;
	re.G2API_SetAnimIndex = VK_G2API_SetAnimIndex;
	re.G2API_SetBoneAnim = VK_G2API_SetBoneAnim;
	re.G2API_SetBoneAnimIndex = VK_G2API_SetBoneAnimIndex;
	re.G2API_SetBoneAngles = VK_G2API_SetBoneAngles;
	re.G2API_SetBoneAnglesIndex = VK_G2API_SetBoneAnglesIndex;
	re.G2API_SetBoneAnglesMatrix = VK_G2API_SetBoneAnglesMatrix;
	re.G2API_SetBoneAnglesMatrixIndex = VK_G2API_SetBoneAnglesMatrixIndex;
	re.G2API_SetBoneIKState = []( CGhoul2Info_v &, int, const char *, int, sharedSetBoneIKStateParams_t * ) -> qboolean { return qfalse; };
	re.G2API_SetGhoul2ModelFlags = VK_G2API_SetGhoul2ModelFlags;
	re.G2API_SetGhoul2ModelIndexes = VK_G2API_SetGhoul2ModelIndexes;
	re.G2API_SetLodBias = VK_G2API_SetLodBias;
	re.G2API_SetNewOrigin = []( CGhoul2Info *, const int ) -> qboolean { return qfalse; };
	re.G2API_SetRagDoll = []( CGhoul2Info_v &, CRagDollParams * ) {};
	re.G2API_SetRootSurface = VK_G2API_SetRootSurface;
	re.G2API_SetShader = VK_G2API_SetShader;
	re.G2API_SetSkin = VK_G2API_SetSkin;
	re.G2API_SetSurfaceOnOff = VK_G2API_SetSurfaceOnOff;
	re.G2API_SetTime = VK_G2API_SetTime;
	re.G2API_StopBoneAnim = VK_G2API_StopBoneAnim;
	re.G2API_StopBoneAnimIndex = VK_G2API_StopBoneAnimIndex;
	re.G2API_StopBoneAngles = VK_G2API_StopBoneAngles;
	re.G2API_StopBoneAnglesIndex = VK_G2API_StopBoneAnglesIndex;

#ifdef _G2_GORE
	re.G2API_AddSkinGore = []( CGhoul2Info_v &, SSkinGoreData & ) {};
	re.G2API_ClearSkinGore = []( CGhoul2Info_v & ) {};
#endif

	re.G2Time_ResetTimers = []() {};
	re.G2Time_ReportTimers = []() {};

	return &re;
}
