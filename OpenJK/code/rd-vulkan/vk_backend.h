/*
===========================================================================
Copyright (C) 2026 JKXRL contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.
===========================================================================
*/

#pragma once

bool VK_Backend_Init();
void VK_Backend_Shutdown();
bool VK_Backend_IsInitialized();
int VK_Backend_GetRecommendedWidth();
int VK_Backend_GetRecommendedHeight();
qhandle_t VK_Backend_RegisterModel( const char *name );
qhandle_t VK_Backend_RegisterSkin( const char *name );
int VK_Backend_FindModelSurface(
	qhandle_t model,
	const char *surfaceName,
	unsigned int *defaultFlags );
int VK_Backend_FindModelBone( qhandle_t model, const char *boneName );
int VK_Backend_GetModelAnimationFrameCount( qhandle_t model, int animationIndex = 0 );
char *VK_Backend_GetModelAnimationName( qhandle_t model );
qboolean VK_Backend_GetBoltMatrix(
	CGhoul2Info_v &ghoul2,
	int modelIndex,
	int boltIndex,
	mdxaBone_t *matrix,
	const vec3_t angles,
	const vec3_t position,
	int frameNumber,
	const vec3_t scale );
void VK_Backend_ModelBounds( qhandle_t model, vec3_t mins, vec3_t maxs );
void VK_Backend_GetModelBounds( refEntity_t *entity, vec3_t mins, vec3_t maxs );
void VK_Backend_LoadWorld( const char *name );
void VK_Backend_BeginLevelLoad( const char *name );
void VK_Backend_BeginFrame();
void VK_Backend_ClearScene();
void VK_Backend_AddRefEntity( const refEntity_t *entity );
void VK_Backend_AddPoly( qhandle_t shader, int vertexCount, const polyVert_t *vertices );
void VK_Backend_AddLight( const vec3_t origin, float intensity, float red, float green, float blue );
void VK_Backend_RenderScene( const refdef_t *refdef );
void VK_Backend_InitWorldEffects();
void VK_Backend_WorldEffectCommand( const char *command );
void VK_Backend_AddWeatherZone( vec3_t mins, vec3_t maxs );
void VK_Backend_SetColor( const float *color );
qhandle_t VK_Backend_RegisterTexture( const char *name );
void VK_Backend_DrawStretchPic(
	float x, float y, float w, float h,
	float s1, float t1, float s2, float t2,
	qhandle_t shader );
void VK_Backend_DrawStretchRaw( int x, int y, int w, int h );
void VK_Backend_SubmitClearFrame();
