/*
===========================================================================
Copyright (C) 2026 JKXRL contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.
===========================================================================
*/

#include "vk_ghoul2.h"

#include "vk_backend.h"
#include "qcommon/ojk_saved_game_helper.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <deque>

namespace
{
constexpr int VK_G2_MAX_INFO_ARRAYS = 512;
constexpr int VK_G2_INFO_INDEX_MASK = VK_G2_MAX_INFO_ARRAYS - 1;
std::array<int, NUM_G2T_TIME> vkG2TimeBases = {};

template<typename T>
T VK_G2Clamp( T value, T minimum, T maximum )
{
	return std::max( minimum, std::min( value, maximum ) );
}

class VulkanGhoul2InfoArray final : public IGhoul2InfoArray
{
public:
	VulkanGhoul2InfoArray()
	{
		for ( int i = 0; i < VK_G2_MAX_INFO_ARRAYS; ++i )
		{
			ids_[i] = VK_G2_MAX_INFO_ARRAYS + i;
			freeIndices_.push_back( i );
		}
	}

	int New() override
	{
		if ( freeIndices_.empty() )
		{
			Com_Error( ERR_FATAL, "Out of Ghoul2 info slots" );
		}

		const int index = freeIndices_.front();
		freeIndices_.pop_front();
		return ids_[index];
	}

	void Delete( int handle ) override
	{
		if ( !IsValid( handle ) )
		{
			return;
		}

		const int index = handle & VK_G2_INFO_INDEX_MASK;
		for ( CGhoul2Info &info : infos_[index] )
		{
			info.mBoneCache = nullptr;
			info.mTransformedVertsArray = nullptr;
		}
		infos_[index].clear();
		ids_[index] += VK_G2_MAX_INFO_ARRAYS;
		if ( ids_[index] <= 0 )
		{
			ids_[index] = VK_G2_MAX_INFO_ARRAYS + index;
		}
		freeIndices_.push_front( index );
	}

	bool IsValid( int handle ) const override
	{
		if ( handle <= 0 )
		{
			return false;
		}
		const int index = handle & VK_G2_INFO_INDEX_MASK;
		return ids_[index] == handle;
	}

	std::vector<CGhoul2Info> &Get( int handle ) override
	{
		assert( IsValid( handle ) );
		return infos_[handle & VK_G2_INFO_INDEX_MASK];
	}

	const std::vector<CGhoul2Info> &Get( int handle ) const override
	{
		assert( IsValid( handle ) );
		return infos_[handle & VK_G2_INFO_INDEX_MASK];
	}

private:
	std::array<std::vector<CGhoul2Info>, VK_G2_MAX_INFO_ARRAYS> infos_;
	std::array<int, VK_G2_MAX_INFO_ARRAYS> ids_;
	std::deque<int> freeIndices_;
};

VulkanGhoul2InfoArray vkGhoul2InfoArray;

bool VK_G2ModelUsable( const CGhoul2Info *ghoul2 )
{
	return ghoul2 != nullptr &&
		ghoul2->mModelindex >= 0 &&
		ghoul2->mModel > 0 &&
		ghoul2->mFileName[0] != '\0';
}

void VK_G2AnimationDefaults(
	float *currentFrame,
	int *startFrame,
	int *endFrame,
	int *flags,
	float *animSpeed )
{
	if ( currentFrame != nullptr )
	{
		*currentFrame = 0.0f;
	}
	if ( startFrame != nullptr )
	{
		*startFrame = 0;
	}
	if ( endFrame != nullptr )
	{
		*endFrame = 1;
	}
	if ( flags != nullptr )
	{
		*flags = 0;
	}
	if ( animSpeed != nullptr )
	{
		*animSpeed = 1.0f;
	}
}

float VK_G2CurrentAnimationFrame( const boneInfo_t &bone, int currentTime, int numFrames )
{
	if ( numFrames <= 0 )
	{
		return 0.0f;
	}

	const int start = VK_G2Clamp( bone.startFrame, 0, numFrames - 1 );
	const int end = VK_G2Clamp( bone.endFrame, start + 1, numFrames );
	const int sampleTime = bone.pauseTime > 0 ? bone.pauseTime : currentTime;
	const float elapsed =
		std::max( 0.0f, static_cast<float>( sampleTime - bone.startTime ) / 50.0f );
	float frame = static_cast<float>( start ) + elapsed * bone.animSpeed;
	const float range = static_cast<float>( end - start );

	if ( ( bone.flags & BONE_ANIM_OVERRIDE_LOOP ) != 0 && range > 0.0f )
	{
		frame = static_cast<float>( start ) +
			std::fmod( std::fmod( frame - static_cast<float>( start ), range ) + range, range );
	}
	else
	{
		frame = VK_G2Clamp(
			frame, static_cast<float>( start ), static_cast<float>( end - 1 ) );
	}
	return VK_G2Clamp( frame, 0.0f, static_cast<float>( numFrames - 1 ) );
}
}

IGhoul2InfoArray &TheGhoul2InfoArray()
{
	return vkGhoul2InfoArray;
}

int VK_G2API_InitGhoul2Model(
	CGhoul2Info_v &ghoul2,
	const char *fileName,
	int,
	qhandle_t customSkin,
	qhandle_t customShader,
	int modelFlags,
	int lodBias )
{
	if ( fileName == nullptr || fileName[0] == '\0' )
	{
		return -1;
	}

	const qhandle_t modelHandle = VK_Backend_RegisterModel( fileName );
	if ( modelHandle <= 0 )
	{
		return -1;
	}

	int modelIndex = 0;
	for ( ; modelIndex < ghoul2.size(); ++modelIndex )
	{
		if ( ghoul2[modelIndex].mModelindex < 0 )
		{
			break;
		}
	}
	if ( modelIndex == ghoul2.size() )
	{
		ghoul2.push_back( CGhoul2Info() );
	}

	CGhoul2Info &info = ghoul2[modelIndex];
	info = CGhoul2Info();
	Q_strncpyz( info.mFileName, fileName, sizeof( info.mFileName ) );
	info.mModelindex = modelIndex;
	info.mModel = modelHandle;
	info.mCustomSkin = customSkin;
	info.mCustomShader = customShader;
	info.mLodBias = lodBias;
	info.mFlags = modelFlags;
	info.mModelBoltLink = -1;
	info.mValid = true;
	return modelIndex;
}

void VK_G2API_CleanGhoul2Models( CGhoul2Info_v &ghoul2 )
{
	ghoul2.clear();
}

void VK_G2API_CopyGhoul2Instance(
	CGhoul2Info_v &ghoul2From,
	CGhoul2Info_v &ghoul2To,
	int modelIndex )
{
	if ( !ghoul2From.IsValid() )
	{
		return;
	}

	if ( modelIndex < 0 )
	{
		ghoul2To.DeepCopy( ghoul2From );
		return;
	}
	if ( modelIndex >= ghoul2From.size() )
	{
		return;
	}

	ghoul2To.clear();
	ghoul2To.push_back( ghoul2From[modelIndex] );
	ghoul2To[0].mModelindex = 0;
	ghoul2To[0].mBoneCache = nullptr;
	ghoul2To[0].mTransformedVertsArray = nullptr;
}

void VK_G2API_SaveGhoul2Models( CGhoul2Info_v &ghoul2 )
{
	ojk::SavedGameHelper savedGame( ri.saved_game );
	savedGame.reset_buffer();

	const int modelCount = ghoul2.IsValid() ? ghoul2.size() : 0;
	savedGame.write<int32_t>( modelCount );
	for ( int i = 0; i < modelCount; ++i )
	{
		const CGhoul2Info &model = ghoul2[i];
		model.sg_export( savedGame );

		const int surfaceCount = static_cast<int>( model.mSlist.size() );
		savedGame.write<int32_t>( surfaceCount );
		for ( const surfaceInfo_t &surface : model.mSlist )
		{
			surface.sg_export( savedGame );
		}

		const int boneCount = static_cast<int>( model.mBlist.size() );
		savedGame.write<int32_t>( boneCount );
		for ( const boneInfo_t &bone : model.mBlist )
		{
			bone.sg_export( savedGame );
		}

		const int boltCount = static_cast<int>( model.mBltlist.size() );
		savedGame.write<int32_t>( boltCount );
		for ( const boltInfo_t &bolt : model.mBltlist )
		{
			bolt.sg_export( savedGame );
		}
	}

#ifdef JK2_MODE
	savedGame.write_chunk_and_size<int32_t>(
		INT_ID( 'G', 'L', '2', 'S' ), INT_ID( 'G', 'H', 'L', '2' ) );
#else
	savedGame.write_chunk( INT_ID( 'G', 'H', 'L', '2' ) );
#endif
}

void VK_G2API_LoadGhoul2Models( CGhoul2Info_v &ghoul2, char *buffer )
{
	static_cast<void>( buffer );
	ojk::SavedGameHelper savedGame( ri.saved_game );
	int modelCount = 0;

#ifdef JK2_MODE
	if ( savedGame.get_buffer_size() > 0 )
	{
#endif
		savedGame.read<int32_t>( modelCount );
#ifdef JK2_MODE
	}
#endif

	ghoul2.resize( modelCount );
	for ( int i = 0; i < modelCount; ++i )
	{
		CGhoul2Info &model = ghoul2[i];
		model = CGhoul2Info();
		model.sg_import( savedGame );
		model.mBoneCache = nullptr;
		model.mTransformedVertsArray = nullptr;

		int surfaceCount = 0;
		savedGame.read<int32_t>( surfaceCount );
		model.mSlist.resize( surfaceCount );
		for ( surfaceInfo_t &surface : model.mSlist )
		{
			surface.sg_import( savedGame );
		}

		int boneCount = 0;
		savedGame.read<int32_t>( boneCount );
		model.mBlist.resize( boneCount );
		for ( boneInfo_t &bone : model.mBlist )
		{
			bone.sg_import( savedGame );
		}

		int boltCount = 0;
		savedGame.read<int32_t>( boltCount );
		model.mBltlist.resize( boltCount );
		for ( boltInfo_t &bolt : model.mBltlist )
		{
			bolt.sg_import( savedGame );
		}

		if ( model.mModelindex != -1 && model.mFileName[0] != '\0' )
		{
			model.mModelindex = i;
			model.mModel = VK_Backend_RegisterModel( model.mFileName );
			model.mValid = model.mModel > 0;
		}
	}
	savedGame.ensure_all_data_read();
}

void VK_G2API_LoadSaveCodeDestructGhoul2Info( CGhoul2Info_v &ghoul2 )
{
	ghoul2.~CGhoul2Info_v();
}

qboolean VK_G2API_HaveWeGhoul2Models( CGhoul2Info_v &ghoul2 )
{
	return ghoul2.IsValid() ? qtrue : qfalse;
}

qboolean VK_G2API_GetAnimFileName( CGhoul2Info *ghoul2, char **filename )
{
	if ( !VK_G2ModelUsable( ghoul2 ) || filename == nullptr )
	{
		return qfalse;
	}
	*filename = VK_Backend_GetModelAnimationName( ghoul2->mModel );
	return *filename != nullptr ? qtrue : qfalse;
}

char *VK_G2API_GetGLAName( CGhoul2Info *ghoul2 )
{
	return VK_G2ModelUsable( ghoul2 )
		? VK_Backend_GetModelAnimationName( ghoul2->mModel )
		: nullptr;
}

qhandle_t VK_G2API_PrecacheGhoul2Model( const char *fileName )
{
	return VK_Backend_RegisterModel( fileName );
}

qboolean VK_G2API_RemoveGhoul2Model( CGhoul2Info_v &ghoul2, int modelIndex )
{
	if ( !ghoul2.IsValid() || modelIndex < 0 || modelIndex >= ghoul2.size() )
	{
		return qfalse;
	}
	ghoul2[modelIndex] = CGhoul2Info();
	return qtrue;
}

qboolean VK_G2API_SetGhoul2ModelFlags( CGhoul2Info *ghoul2, int flags )
{
	if ( !VK_G2ModelUsable( ghoul2 ) )
	{
		return qfalse;
	}
	ghoul2->mFlags = flags;
	return qtrue;
}

int VK_G2API_GetGhoul2ModelFlags( CGhoul2Info *ghoul2 )
{
	return VK_G2ModelUsable( ghoul2 ) ? ghoul2->mFlags : 0;
}

void VK_G2API_SetGhoul2ModelIndexes(
	CGhoul2Info_v &ghoul2,
	qhandle_t *,
	qhandle_t *skinList )
{
	if ( !ghoul2.IsValid() || skinList == nullptr )
	{
		return;
	}
	for ( int i = 0; i < ghoul2.size(); ++i )
	{
		if ( ghoul2[i].mModelindex >= 0 && ghoul2[i].mCustomSkin >= 0 )
		{
			ghoul2[i].mSkin = skinList[ghoul2[i].mCustomSkin];
		}
	}
}

qboolean VK_G2API_SetLodBias( CGhoul2Info *ghoul2, int lodBias )
{
	if ( !VK_G2ModelUsable( ghoul2 ) )
	{
		return qfalse;
	}
	ghoul2->mLodBias = lodBias;
	return qtrue;
}

qboolean VK_G2API_SetShader( CGhoul2Info *ghoul2, qhandle_t customShader )
{
	if ( !VK_G2ModelUsable( ghoul2 ) )
	{
		return qfalse;
	}
	ghoul2->mCustomShader = customShader;
	return qtrue;
}

qboolean VK_G2API_SetSkin(
	CGhoul2Info *ghoul2,
	qhandle_t customSkin,
	qhandle_t renderSkin )
{
	if ( !VK_G2ModelUsable( ghoul2 ) )
	{
		return qfalse;
	}
	ghoul2->mCustomSkin = customSkin;
	ghoul2->mSkin = renderSkin;
	return qtrue;
}

qboolean VK_G2API_SetSurfaceOnOff(
	CGhoul2Info *ghoul2,
	const char *surfaceName,
	int flags )
{
	if ( !VK_G2ModelUsable( ghoul2 ) ||
		 ( flags & ~( G2SURFACEFLAG_OFF | G2SURFACEFLAG_NODESCENDANTS ) ) != 0 )
	{
		return qfalse;
	}

	unsigned int defaultFlags = 0;
	const int surfaceIndex =
		VK_Backend_FindModelSurface( ghoul2->mModel, surfaceName, &defaultFlags );
	if ( surfaceIndex < 0 )
	{
		return qfalse;
	}

	const int overrideMask = G2SURFACEFLAG_OFF | G2SURFACEFLAG_NODESCENDANTS;
	const int effectiveFlags =
		( static_cast<int>( defaultFlags ) & ~overrideMask ) | ( flags & overrideMask );
	for ( surfaceInfo_t &surface : ghoul2->mSlist )
	{
		if ( surface.surface == surfaceIndex )
		{
			surface.offFlags = effectiveFlags;
			ghoul2->mMeshFrameNum = 0;
			return qtrue;
		}
	}

	if ( effectiveFlags != static_cast<int>( defaultFlags ) )
	{
		surfaceInfo_t surface;
		surface.surface = surfaceIndex;
		surface.offFlags = effectiveFlags;
		ghoul2->mSlist.push_back( surface );
	}
	ghoul2->mMeshFrameNum = 0;
	return qtrue;
}

int VK_G2API_AddBolt( CGhoul2Info *ghoul2, const char *name )
{
	if ( !VK_G2ModelUsable( ghoul2 ) || name == nullptr || name[0] == '\0' )
	{
		return -1;
	}
	int surfaceNumber = VK_Backend_FindModelSurface( ghoul2->mModel, name, nullptr );
	int boneNumber = -1;
	if ( surfaceNumber < 0 )
	{
		boneNumber = VK_Backend_FindModelBone( ghoul2->mModel, name );
		if ( boneNumber < 0 )
		{
			return -1;
		}
	}

	for ( size_t i = 0; i < ghoul2->mBltlist.size(); ++i )
	{
		boltInfo_t &bolt = ghoul2->mBltlist[i];
		if ( bolt.surfaceNumber == surfaceNumber && bolt.boneNumber == boneNumber )
		{
			++bolt.boltUsed;
			return static_cast<int>( i );
		}
	}
	for ( size_t i = 0; i < ghoul2->mBltlist.size(); ++i )
	{
		boltInfo_t &bolt = ghoul2->mBltlist[i];
		if ( bolt.surfaceNumber < 0 && bolt.boneNumber < 0 )
		{
			bolt.surfaceNumber = surfaceNumber;
			bolt.boneNumber = boneNumber;
			bolt.surfaceType = 0;
			bolt.boltUsed = 1;
			return static_cast<int>( i );
		}
	}

	boltInfo_t bolt;
	bolt.surfaceNumber = surfaceNumber;
	bolt.boneNumber = boneNumber;
	bolt.boltUsed = 1;
	ghoul2->mBltlist.push_back( bolt );
	return static_cast<int>( ghoul2->mBltlist.size() - 1 );
}

int VK_G2API_AddBoltSurfNum( CGhoul2Info *ghoul2, int surfaceListIndex )
{
	if ( !VK_G2ModelUsable( ghoul2 ) ||
		 surfaceListIndex < 0 ||
		 static_cast<size_t>( surfaceListIndex ) >= ghoul2->mSlist.size() )
	{
		return -1;
	}
	const int surfaceNumber = ghoul2->mSlist[surfaceListIndex].surface;
	for ( size_t i = 0; i < ghoul2->mBltlist.size(); ++i )
	{
		boltInfo_t &bolt = ghoul2->mBltlist[i];
		if ( bolt.surfaceNumber == surfaceNumber )
		{
			++bolt.boltUsed;
			return static_cast<int>( i );
		}
	}
	boltInfo_t bolt;
	bolt.surfaceNumber = surfaceNumber;
	bolt.surfaceType = G2SURFACEFLAG_GENERATED;
	bolt.boltUsed = 1;
	ghoul2->mBltlist.push_back( bolt );
	return static_cast<int>( ghoul2->mBltlist.size() - 1 );
}

qboolean VK_G2API_RemoveBolt( CGhoul2Info *ghoul2, int boltIndex )
{
	if ( !VK_G2ModelUsable( ghoul2 ) ||
		 boltIndex < 0 ||
		 static_cast<size_t>( boltIndex ) >= ghoul2->mBltlist.size() )
	{
		return qfalse;
	}
	boltInfo_t &bolt = ghoul2->mBltlist[boltIndex];
	if ( bolt.boltUsed > 0 )
	{
		--bolt.boltUsed;
	}
	if ( bolt.boltUsed == 0 )
	{
		bolt = boltInfo_t();
	}
	return qtrue;
}

qboolean VK_G2API_AttachG2Model(
	CGhoul2Info *ghoul2,
	CGhoul2Info *parent,
	int parentBoltIndex,
	int parentModelIndex )
{
	if ( !VK_G2ModelUsable( ghoul2 ) || !VK_G2ModelUsable( parent ) ||
		 parentBoltIndex < 0 ||
		 static_cast<size_t>( parentBoltIndex ) >= parent->mBltlist.size() )
	{
		ri.Printf( PRINT_WARNING,
			"rd-vulkan-ghoul2-attach: rejected child=%s parent=%s bolt=%d parentBolts=%zu\n",
			ghoul2 != nullptr ? ghoul2->mFileName : "<null>",
			parent != nullptr ? parent->mFileName : "<null>",
			parentBoltIndex,
			parent != nullptr ? parent->mBltlist.size() : 0 );
		return qfalse;
	}
	ghoul2->mModelBoltLink =
		( ( parentModelIndex & MODEL_AND ) << MODEL_SHIFT ) |
		( ( parentBoltIndex & BOLT_AND ) << BOLT_SHIFT );
	const boltInfo_t &bolt = parent->mBltlist[parentBoltIndex];
	ri.Printf( PRINT_ALL,
		"rd-vulkan-ghoul2-attach: child=%s parent=%s parentModel=%d bolt=%d bone=%d surface=%d link=%d\n",
		ghoul2->mFileName,
		parent->mFileName,
		parentModelIndex,
		parentBoltIndex,
		bolt.boneNumber,
		bolt.surfaceNumber,
		ghoul2->mModelBoltLink );
	return qtrue;
}

qboolean VK_G2API_DetachG2Model( CGhoul2Info *ghoul2 )
{
	if ( !VK_G2ModelUsable( ghoul2 ) )
	{
		return qfalse;
	}
	ghoul2->mModelBoltLink = -1;
	return qtrue;
}

qboolean VK_G2API_GetBoltMatrix(
	CGhoul2Info_v &ghoul2,
	int modelIndex,
	int boltIndex,
	mdxaBone_t *matrix,
	const vec3_t angles,
	const vec3_t position,
	int frameNumber,
	qhandle_t *,
	const vec3_t scale )
{
	return VK_Backend_GetBoltMatrix(
		ghoul2,
		modelIndex,
		boltIndex,
		matrix,
		angles,
		position,
		frameNumber,
		scale );
}

void VK_G2API_GiveMeVectorFromMatrix(
	mdxaBone_t &matrix,
	Eorientations orientation,
	vec3_t &vector )
{
	if ( orientation == ORIGIN )
	{
		vector[0] = matrix.matrix[0][3];
		vector[1] = matrix.matrix[1][3];
		vector[2] = matrix.matrix[2][3];
		return;
	}
	int column = 0;
	float sign = 1.0f;
	switch ( orientation )
	{
	case POSITIVE_X: column = 0; break;
	case POSITIVE_Y: column = 1; break;
	case POSITIVE_Z: column = 2; break;
	case NEGATIVE_X: column = 0; sign = -1.0f; break;
	case NEGATIVE_Y: column = 1; sign = -1.0f; break;
	case NEGATIVE_Z: column = 2; sign = -1.0f; break;
	default: VectorClear( vector ); return;
	}
	for ( int component = 0; component < 3; ++component )
	{
		vector[component] = sign * matrix.matrix[component][column];
	}
}

void VK_G2API_SetTime( int currentTime, int clock )
{
	if ( clock < 0 || clock >= NUM_G2T_TIME )
	{
		return;
	}
	vkG2TimeBases[clock] = currentTime;
	if ( vkG2TimeBases[G2T_CG_TIME] > vkG2TimeBases[G2T_SV_TIME] + 200 )
	{
		vkG2TimeBases[G2T_CG_TIME] = 0;
	}
}

int VK_G2API_GetTime( int argumentTime )
{
	const int currentTime = vkG2TimeBases[G2T_CG_TIME] != 0 ?
		vkG2TimeBases[G2T_CG_TIME] :
		vkG2TimeBases[G2T_SV_TIME];
	return currentTime != 0 ? currentTime : argumentTime;
}

int VK_G2API_GetAnimIndex( CGhoul2Info *ghoul2 )
{
	return ghoul2 != nullptr ? ghoul2->animModelIndexOffset : 0;
}

qboolean VK_G2API_SetAnimIndex( CGhoul2Info *ghoul2, int index )
{
	if ( !VK_G2ModelUsable( ghoul2 ) )
	{
		return qfalse;
	}
	if ( ghoul2->animModelIndexOffset != index )
	{
		ghoul2->animModelIndexOffset = index;
		ghoul2->currentAnimModelSize = 0;
		for ( boneInfo_t &bone : ghoul2->mBlist )
		{
			bone.flags &= ~( BONE_ANIM_TOTAL | BONE_ANGLES_TOTAL );
		}
		ghoul2->mSkelFrameNum = -1;
	}
	return qtrue;
}

int VK_G2API_GetBoneIndex(
	CGhoul2Info *ghoul2,
	const char *boneName,
	qboolean addIfNotFound )
{
	if ( !VK_G2ModelUsable( ghoul2 ) || boneName == nullptr || boneName[0] == '\0' )
	{
		return -1;
	}

	const int boneNumber = VK_Backend_FindModelBone( ghoul2->mModel, boneName );
	if ( boneNumber < 0 )
	{
		return -1;
	}
	for ( size_t i = 0; i < ghoul2->mBlist.size(); ++i )
	{
		if ( ghoul2->mBlist[i].boneNumber == boneNumber )
		{
			return static_cast<int>( i );
		}
	}
	if ( !addIfNotFound )
	{
		return -1;
	}

	for ( size_t i = 0; i < ghoul2->mBlist.size(); ++i )
	{
		if ( ghoul2->mBlist[i].boneNumber < 0 )
		{
			ghoul2->mBlist[i] = boneInfo_t();
			ghoul2->mBlist[i].boneNumber = boneNumber;
			return static_cast<int>( i );
		}
	}

	boneInfo_t bone;
	bone.boneNumber = boneNumber;
	ghoul2->mBlist.push_back( bone );
	return static_cast<int>( ghoul2->mBlist.size() - 1 );
}

qboolean VK_G2API_GetAnimRangeIndex(
	CGhoul2Info *ghoul2,
	int boneListIndex,
	int *startFrame,
	int *endFrame )
{
	if ( !VK_G2ModelUsable( ghoul2 ) ||
		 boneListIndex < 0 ||
		 static_cast<size_t>( boneListIndex ) >= ghoul2->mBlist.size() )
	{
		return qfalse;
	}
	const boneInfo_t &bone = ghoul2->mBlist[boneListIndex];
	if ( bone.boneNumber < 0 ||
		 ( bone.flags & ( BONE_ANIM_OVERRIDE | BONE_ANIM_OVERRIDE_LOOP ) ) == 0 )
	{
		return qfalse;
	}
	if ( startFrame != nullptr )
	{
		*startFrame = bone.startFrame;
	}
	if ( endFrame != nullptr )
	{
		*endFrame = bone.endFrame;
	}
	return qtrue;
}

qboolean VK_G2API_GetAnimRange(
	CGhoul2Info *ghoul2,
	const char *boneName,
	int *startFrame,
	int *endFrame )
{
	const int index = VK_G2API_GetBoneIndex( ghoul2, boneName, qfalse );
	return index >= 0 ?
		VK_G2API_GetAnimRangeIndex( ghoul2, index, startFrame, endFrame ) :
		qfalse;
}

qboolean VK_G2API_GetBoneAnimIndex(
	CGhoul2Info *ghoul2,
	int boneListIndex,
	int currentTime,
	float *currentFrame,
	int *startFrame,
	int *endFrame,
	int *flags,
	float *animSpeed,
	int * )
{
	VK_G2AnimationDefaults( currentFrame, startFrame, endFrame, flags, animSpeed );
	if ( !VK_G2ModelUsable( ghoul2 ) ||
		 boneListIndex < 0 ||
		 static_cast<size_t>( boneListIndex ) >= ghoul2->mBlist.size() )
	{
		return qfalse;
	}

	const boneInfo_t &bone = ghoul2->mBlist[boneListIndex];
	if ( bone.boneNumber < 0 ||
		 ( bone.flags & ( BONE_ANIM_OVERRIDE | BONE_ANIM_OVERRIDE_LOOP ) ) == 0 )
	{
		return qfalse;
	}
	const int numFrames = VK_Backend_GetModelAnimationFrameCount(
		ghoul2->mModel, ghoul2->animModelIndexOffset );
	if ( numFrames <= 0 )
	{
		return qfalse;
	}
	if ( currentFrame != nullptr )
	{
		*currentFrame =
			VK_G2CurrentAnimationFrame( bone, VK_G2API_GetTime( currentTime ), numFrames );
	}
	if ( startFrame != nullptr )
	{
		*startFrame = bone.startFrame;
	}
	if ( endFrame != nullptr )
	{
		*endFrame = bone.endFrame;
	}
	if ( flags != nullptr )
	{
		*flags = bone.flags;
	}
	if ( animSpeed != nullptr )
	{
		*animSpeed = bone.animSpeed;
	}
	return qtrue;
}

qboolean VK_G2API_GetBoneAnim(
	CGhoul2Info *ghoul2,
	const char *boneName,
	int currentTime,
	float *currentFrame,
	int *startFrame,
	int *endFrame,
	int *flags,
	float *animSpeed,
	int *modelList )
{
	const int index = VK_G2API_GetBoneIndex( ghoul2, boneName, qfalse );
	if ( index < 0 )
	{
		VK_G2AnimationDefaults( currentFrame, startFrame, endFrame, flags, animSpeed );
		return qfalse;
	}
	return VK_G2API_GetBoneAnimIndex(
		ghoul2,
		index,
		currentTime,
		currentFrame,
		startFrame,
		endFrame,
		flags,
		animSpeed,
		modelList );
}

qboolean VK_G2API_SetBoneAnimIndex(
	CGhoul2Info *ghoul2,
	int boneListIndex,
	int startFrame,
	int endFrame,
	int flags,
	float animSpeed,
	int currentTime,
	float setFrame,
	int blendTime )
{
	if ( !VK_G2ModelUsable( ghoul2 ) ||
		 boneListIndex < 0 ||
		 static_cast<size_t>( boneListIndex ) >= ghoul2->mBlist.size() ||
		 ghoul2->mBlist[boneListIndex].boneNumber < 0 )
	{
		return qfalse;
	}

	const int numFrames = VK_Backend_GetModelAnimationFrameCount(
		ghoul2->mModel, ghoul2->animModelIndexOffset );
	if ( numFrames <= 0 )
	{
		return qfalse;
	}
	startFrame = VK_G2Clamp( startFrame, 0, numFrames - 1 );
	endFrame = VK_G2Clamp( endFrame, startFrame + 1, numFrames );
	if ( setFrame >= 0.0f )
	{
		setFrame = VK_G2Clamp( setFrame, static_cast<float>( startFrame ),
			static_cast<float>( endFrame - 1 ) );
	}

	currentTime = VK_G2API_GetTime( currentTime );
	boneInfo_t &bone = ghoul2->mBlist[boneListIndex];
	bone.startFrame = startFrame;
	bone.endFrame = endFrame;
	bone.animSpeed = animSpeed;
	bone.pauseTime = 0;
	bone.blendTime = std::max( blendTime, 0 );
	bone.blendStart = currentTime;
	bone.flags &= ~BONE_ANIM_TOTAL;
	bone.flags |= flags;
	if ( setFrame >= 0.0f && std::fabs( animSpeed ) > 0.0001f )
	{
		bone.startTime = currentTime - static_cast<int>(
			( setFrame - static_cast<float>( startFrame ) ) * 50.0f / animSpeed );
	}
	else
	{
		bone.startTime = currentTime;
	}
	ghoul2->mSkelFrameNum = -1;
	return qtrue;
}

qboolean VK_G2API_SetBoneAnim(
	CGhoul2Info *ghoul2,
	const char *boneName,
	int startFrame,
	int endFrame,
	int flags,
	float animSpeed,
	int currentTime,
	float setFrame,
	int blendTime )
{
	const int index = VK_G2API_GetBoneIndex( ghoul2, boneName, qtrue );
	return index >= 0 ?
		VK_G2API_SetBoneAnimIndex(
			ghoul2,
			index,
			startFrame,
			endFrame,
			flags,
			animSpeed,
			currentTime,
			setFrame,
			blendTime ) :
		qfalse;
}

qboolean VK_G2API_PauseBoneAnimIndex(
	CGhoul2Info *ghoul2,
	int boneListIndex,
	int currentTime )
{
	if ( !VK_G2ModelUsable( ghoul2 ) ||
		 boneListIndex < 0 ||
		 static_cast<size_t>( boneListIndex ) >= ghoul2->mBlist.size() )
	{
		return qfalse;
	}
	boneInfo_t &bone = ghoul2->mBlist[boneListIndex];
	if ( bone.boneNumber < 0 ||
		 ( bone.flags & ( BONE_ANIM_OVERRIDE | BONE_ANIM_OVERRIDE_LOOP ) ) == 0 )
	{
		return qfalse;
	}

	currentTime = VK_G2API_GetTime( currentTime );
	if ( bone.pauseTime == 0 )
	{
		bone.pauseTime = currentTime;
		ghoul2->mSkelFrameNum = -1;
		return qtrue;
	}

	const int numFrames = VK_Backend_GetModelAnimationFrameCount(
		ghoul2->mModel, ghoul2->animModelIndexOffset );
	const float frame = VK_G2CurrentAnimationFrame( bone, bone.pauseTime, numFrames );
	const int startFrame = bone.startFrame;
	const int endFrame = bone.endFrame;
	const int flags = bone.flags;
	const float animSpeed = bone.animSpeed;
	if ( !VK_G2API_SetBoneAnimIndex(
			ghoul2,
			boneListIndex,
			startFrame,
			endFrame,
			flags,
			animSpeed,
			currentTime,
			frame,
			0 ) )
	{
		return qfalse;
	}
	ghoul2->mBlist[boneListIndex].pauseTime = 0;
	return qtrue;
}

qboolean VK_G2API_PauseBoneAnim(
	CGhoul2Info *ghoul2,
	const char *boneName,
	int currentTime )
{
	const int index = VK_G2API_GetBoneIndex( ghoul2, boneName, qfalse );
	return index >= 0 ?
		VK_G2API_PauseBoneAnimIndex( ghoul2, index, currentTime ) :
		qfalse;
}

qboolean VK_G2API_IsPaused( CGhoul2Info *ghoul2, const char *boneName )
{
	const int index = VK_G2API_GetBoneIndex( ghoul2, boneName, qfalse );
	return index >= 0 && ghoul2->mBlist[index].pauseTime != 0 ? qtrue : qfalse;
}

qboolean VK_G2API_StopBoneAnimIndex( CGhoul2Info *ghoul2, int boneListIndex )
{
	if ( !VK_G2ModelUsable( ghoul2 ) ||
		 boneListIndex < 0 ||
		 static_cast<size_t>( boneListIndex ) >= ghoul2->mBlist.size() ||
		 ghoul2->mBlist[boneListIndex].boneNumber < 0 )
	{
		return qfalse;
	}
	boneInfo_t &bone = ghoul2->mBlist[boneListIndex];
	bone.flags &= ~BONE_ANIM_TOTAL;
	bone.pauseTime = 0;
	if ( bone.flags == 0 )
	{
		bone.boneNumber = -1;
	}
	ghoul2->mSkelFrameNum = -1;
	return qtrue;
}

qboolean VK_G2API_StopBoneAnim( CGhoul2Info *ghoul2, const char *boneName )
{
	const int index = VK_G2API_GetBoneIndex( ghoul2, boneName, qfalse );
	return index >= 0 ? VK_G2API_StopBoneAnimIndex( ghoul2, index ) : qfalse;
}
