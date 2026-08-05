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

#include "tr_local.h"

IGhoul2InfoArray &TheGhoul2InfoArray();

int VK_G2API_InitGhoul2Model(
	CGhoul2Info_v &ghoul2,
	const char *fileName,
	int modelIndex,
	qhandle_t customSkin,
	qhandle_t customShader,
	int modelFlags,
	int lodBias );
void VK_G2API_CleanGhoul2Models( CGhoul2Info_v &ghoul2 );
void VK_G2API_CopyGhoul2Instance(
	CGhoul2Info_v &ghoul2From,
	CGhoul2Info_v &ghoul2To,
	int modelIndex );
void VK_G2API_SaveGhoul2Models( CGhoul2Info_v &ghoul2 );
void VK_G2API_LoadGhoul2Models( CGhoul2Info_v &ghoul2, char *buffer );
void VK_G2API_LoadSaveCodeDestructGhoul2Info( CGhoul2Info_v &ghoul2 );
qboolean VK_G2API_HaveWeGhoul2Models( CGhoul2Info_v &ghoul2 );
qboolean VK_G2API_GetAnimFileName( CGhoul2Info *ghoul2, char **filename );
char *VK_G2API_GetGLAName( CGhoul2Info *ghoul2 );
int VK_G2API_GetParentSurface( CGhoul2Info *ghoul2, int surfaceIndex );
int VK_G2API_GetSurfaceIndex( CGhoul2Info *ghoul2, const char *surfaceName );
char *VK_G2API_GetSurfaceName( CGhoul2Info *ghoul2, int surfaceIndex );
int VK_G2API_GetSurfaceRenderStatus( CGhoul2Info *ghoul2, const char *surfaceName );
void VK_G2API_CollisionDetect(
	CCollisionRecord *collisionRecords,
	CGhoul2Info_v &ghoul2,
	const vec3_t angles,
	const vec3_t position,
	int frameNumber,
	int entityNumber,
	vec3_t rayStart,
	vec3_t rayEnd,
	vec3_t scale,
	CMiniHeap *heap,
	EG2_Collision collisionType,
	int useLod,
	float radius );
qhandle_t VK_G2API_PrecacheGhoul2Model( const char *fileName );
qboolean VK_G2API_RemoveGhoul2Model( CGhoul2Info_v &ghoul2, int modelIndex );
qboolean VK_G2API_SetGhoul2ModelFlags( CGhoul2Info *ghoul2, int flags );
int VK_G2API_GetGhoul2ModelFlags( CGhoul2Info *ghoul2 );
void VK_G2API_SetGhoul2ModelIndexes(
	CGhoul2Info_v &ghoul2,
	qhandle_t *modelList,
	qhandle_t *skinList );
qboolean VK_G2API_SetLodBias( CGhoul2Info *ghoul2, int lodBias );
qboolean VK_G2API_SetRootSurface(
	CGhoul2Info_v &ghoul2,
	int modelIndex,
	const char *surfaceName );
qboolean VK_G2API_SetShader( CGhoul2Info *ghoul2, qhandle_t customShader );
qboolean VK_G2API_SetSkin(
	CGhoul2Info *ghoul2,
	qhandle_t customSkin,
	qhandle_t renderSkin );
qboolean VK_G2API_SetSurfaceOnOff(
	CGhoul2Info *ghoul2,
	const char *surfaceName,
	int flags );
int VK_G2API_AddBolt( CGhoul2Info *ghoul2, const char *name );
int VK_G2API_AddBoltSurfNum( CGhoul2Info *ghoul2, int surfaceListIndex );
qboolean VK_G2API_RemoveBolt( CGhoul2Info *ghoul2, int boltIndex );
qboolean VK_G2API_AttachEnt(
	int *boltInfo,
	CGhoul2Info *ghoul2,
	int boltIndex,
	int entityNumber,
	int modelIndex );
qboolean VK_G2API_AttachG2Model(
	CGhoul2Info *ghoul2,
	CGhoul2Info *parent,
	int parentBoltIndex,
	int parentModelIndex );
qboolean VK_G2API_DetachG2Model( CGhoul2Info *ghoul2 );
qboolean VK_G2API_GetBoltMatrix(
	CGhoul2Info_v &ghoul2,
	int modelIndex,
	int boltIndex,
	mdxaBone_t *matrix,
	const vec3_t angles,
	const vec3_t position,
	int frameNumber,
	qhandle_t *modelList,
	const vec3_t scale );
void VK_G2API_GiveMeVectorFromMatrix(
	mdxaBone_t &matrix,
	Eorientations orientation,
	vec3_t &vector );
void VK_G2API_SetTime( int currentTime, int clock );
int VK_G2API_GetTime( int argumentTime );
int VK_G2API_GetAnimIndex( CGhoul2Info *ghoul2 );
qboolean VK_G2API_SetAnimIndex( CGhoul2Info *ghoul2, int index );
int VK_G2API_GetBoneIndex(
	CGhoul2Info *ghoul2,
	const char *boneName,
	qboolean addIfNotFound );
qboolean VK_G2API_GetAnimRange(
	CGhoul2Info *ghoul2,
	const char *boneName,
	int *startFrame,
	int *endFrame );
qboolean VK_G2API_GetAnimRangeIndex(
	CGhoul2Info *ghoul2,
	int boneListIndex,
	int *startFrame,
	int *endFrame );
qboolean VK_G2API_GetBoneAnim(
	CGhoul2Info *ghoul2,
	const char *boneName,
	int currentTime,
	float *currentFrame,
	int *startFrame,
	int *endFrame,
	int *flags,
	float *animSpeed,
	int *modelList );
qboolean VK_G2API_GetBoneAnimIndex(
	CGhoul2Info *ghoul2,
	int boneListIndex,
	int currentTime,
	float *currentFrame,
	int *startFrame,
	int *endFrame,
	int *flags,
	float *animSpeed,
	int *modelList );
qboolean VK_G2API_SetBoneAnim(
	CGhoul2Info *ghoul2,
	const char *boneName,
	int startFrame,
	int endFrame,
	int flags,
	float animSpeed,
	int currentTime,
	float setFrame,
	int blendTime );
qboolean VK_G2API_PauseBoneAnim(
	CGhoul2Info *ghoul2,
	const char *boneName,
	int currentTime );
qboolean VK_G2API_PauseBoneAnimIndex(
	CGhoul2Info *ghoul2,
	int boneListIndex,
	int currentTime );
qboolean VK_G2API_IsPaused( CGhoul2Info *ghoul2, const char *boneName );
qboolean VK_G2API_StopBoneAnim( CGhoul2Info *ghoul2, const char *boneName );
qboolean VK_G2API_StopBoneAnimIndex( CGhoul2Info *ghoul2, int boneListIndex );
qboolean VK_G2API_SetBoneAnimIndex(
	CGhoul2Info *ghoul2,
	int boneListIndex,
	int startFrame,
	int endFrame,
	int flags,
	float animSpeed,
	int currentTime,
	float setFrame,
	int blendTime );
qboolean VK_G2API_SetBoneAngles(
	CGhoul2Info *ghoul2,
	const char *boneName,
	const vec3_t angles,
	int flags,
	Eorientations up,
	Eorientations left,
	Eorientations forward,
	qhandle_t *modelList,
	int blendTime,
	int currentTime );
qboolean VK_G2API_SetBoneAnglesIndex(
	CGhoul2Info *ghoul2,
	int boneListIndex,
	const vec3_t angles,
	int flags,
	Eorientations up,
	Eorientations left,
	Eorientations forward,
	qhandle_t *modelList,
	int blendTime,
	int currentTime );
qboolean VK_G2API_SetBoneAnglesMatrix(
	CGhoul2Info *ghoul2,
	const char *boneName,
	const mdxaBone_t &matrix,
	int flags,
	qhandle_t *modelList,
	int blendTime,
	int currentTime );
qboolean VK_G2API_SetBoneAnglesMatrixIndex(
	CGhoul2Info *ghoul2,
	int boneListIndex,
	const mdxaBone_t &matrix,
	int flags,
	qhandle_t *modelList,
	int blendTime,
	int currentTime );
qboolean VK_G2API_StopBoneAngles( CGhoul2Info *ghoul2, const char *boneName );
qboolean VK_G2API_StopBoneAnglesIndex( CGhoul2Info *ghoul2, int boneListIndex );
