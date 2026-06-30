/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2005 - 2015, ioquake3 contributors
Copyright (C) 2013 - 2015, OpenJK contributors

This file is part of the OpenJK source code.

OpenJK is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

#include "../server/exe_headers.h"

#include "tr_local.h"
#include <VrClientInfo.h>

extern vr_client_info_t *vr;

typedef union {
	void	*align;
	byte	cmds[MAX_RENDER_COMMANDS + sizeof( int ) + sizeof( void * )];
} vrStereoReplayBuffer_t;

static vrStereoReplayBuffer_t	vrStereoReplayCommands;
static vrStereoReplayBuffer_t	vrStereoReplayScratch;
static int	vrStereoReplayCommandBytes;
static qboolean vrStereoReplayActive;
static qboolean vrStereoReplayLoggedInvalid;

typedef struct {
	int drawSurfs;
	int stretchPics;
	int swaps;
	int flushes;
	int unknownCommand;
} vrStereoReplayStats_t;

static qboolean R_VR_InspectStereoReplayCommands( const byte *cmds, vrStereoReplayStats_t *stats ) {
	const void *curCmd = cmds;

	memset( stats, 0, sizeof( *stats ) );

	while ( 1 ) {
		curCmd = PADP( curCmd, sizeof( void * ) );

		switch ( *(const int *)curCmd ) {
		case RC_SET_COLOR:
			curCmd = (const void *)( (const setColorCommand_t *)curCmd + 1 );
			break;
		case RC_STRETCH_PIC:
			stats->stretchPics++;
			curCmd = (const void *)( (const stretchPicCommand_t *)curCmd + 1 );
			break;
		case RC_SCISSOR:
			curCmd = (const void *)( (const scissorCommand_t *)curCmd + 1 );
			break;
		case RC_ROTATE_PIC:
		case RC_ROTATE_PIC2:
			curCmd = (const void *)( (const rotatePicCommand_t *)curCmd + 1 );
			break;
		case RC_DRAW_SURFS:
			stats->drawSurfs++;
			curCmd = (const void *)( (const drawSurfsCommand_t *)curCmd + 1 );
			break;
		case RC_DRAW_BUFFER:
			curCmd = (const void *)( (const drawBufferCommand_t *)curCmd + 1 );
			break;
		case RC_SWAP_BUFFERS:
			stats->swaps++;
			curCmd = (const void *)( (const swapBuffersCommand_t *)curCmd + 1 );
			break;
		case RC_WORLD_EFFECTS:
			curCmd = (const void *)( (const setModeCommand_t *)curCmd + 1 );
			break;
		case RC_FLUSH:
			stats->flushes++;
			curCmd = (const void *)( (const swapBuffersCommand_t *)curCmd + 1 );
			break;
		case RC_END_OF_LIST:
			return qtrue;
		default:
			stats->unknownCommand = *(const int *)curCmd;
			return qfalse;
		}
	}
}

static void R_VR_GetHudReplayOffset( stereoFrame_t stereoFrame, float *x, float *y ) {
	cvar_t *hudStereo;

	*x = 0.0f;
	*y = 0.0f;

	if ( stereoFrame != STEREO_LEFT && stereoFrame != STEREO_RIGHT ) {
		return;
	}

	hudStereo = ri.Cvar_Get( "cg_hudStereo", "20", 0 );
	if ( hudStereo ) {
		*x += ( stereoFrame == STEREO_LEFT ) ? hudStereo->value : -hudStereo->value;
	}

	if ( vr ) {
		*x += vr->off_center_fov_x * 640.0f;
		*y -= vr->off_center_fov_y * 480.0f;
	}
}

static void R_VR_PatchStretchPicCommand( stretchPicCommand_t *cmd, stereoFrame_t stereoFrame ) {
	float x, y;

	if ( cmd->x <= 1.0f && cmd->y <= 1.0f &&
	     cmd->w >= 638.0f && cmd->h >= 478.0f ) {
		return;
	}

	R_VR_GetHudReplayOffset( stereoFrame, &x, &y );
	cmd->x += x;
	cmd->y += y;
}

static void R_VR_PatchRotatePicCommand( rotatePicCommand_t *cmd, stereoFrame_t stereoFrame ) {
	float x, y;

	if ( cmd->x <= 1.0f && cmd->y <= 1.0f &&
	     cmd->w >= 638.0f && cmd->h >= 478.0f ) {
		return;
	}

	R_VR_GetHudReplayOffset( stereoFrame, &x, &y );
	cmd->x += x;
	cmd->y += y;
}

static void R_VR_PatchDrawSurfsCommand( drawSurfsCommand_t *cmd, stereoFrame_t stereoFrame ) {
	int eye;
	float worldScale;
	float stereoSeparation;
	float sep;
	trRefdef_t savedRefdef;
	viewParms_t savedViewParms;
	stereoFrame_t savedStereoFrame;
	qboolean savedReplayCapture;
	cvar_t *stereoSeparationCvar;
	cvar_t *worldScaleCvar;

	if ( stereoFrame != STEREO_LEFT && stereoFrame != STEREO_RIGHT ) {
		return;
	}

	eye = ( stereoFrame == STEREO_LEFT ) ? 0 : 1;
	cmd->refdef.stereoFrame = stereoFrame;
	cmd->viewParms.stereoFrame = stereoFrame;

	if ( !( cmd->refdef.rdflags & ( RDF_NOWORLDMODEL | RDF_SKYBOXPORTAL ) ) ) {
		if ( ri.TBXR_GetEyeStereoSeparation ) {
			sep = ri.TBXR_GetEyeStereoSeparation( eye );
		} else {
			worldScale = cmd->refdef.worldscale;
			if ( worldScale <= 0.0f ) {
				worldScaleCvar = ri.Cvar_Get( "cg_worldScale", "33.5", 0 );
				worldScale = worldScaleCvar ? worldScaleCvar->value : 33.5f;
			}

			stereoSeparationCvar = ri.Cvar_Get( "cg_stereoSeparation", "0.065", 0 );
			stereoSeparation = stereoSeparationCvar ? stereoSeparationCvar->value : 0.065f;
			sep = ( eye == 0 ? 0.5f : -0.5f ) * stereoSeparation * worldScale;
		}

		VectorMA( cmd->refdef.vieworg, sep, cmd->refdef.viewaxis[1], cmd->refdef.vieworg );
		VectorCopy( cmd->refdef.vieworg, cmd->viewParms.ori.origin );
		VectorCopy( cmd->refdef.vieworg, cmd->viewParms.pvsOrigin );
	}

	R_RebuildViewParmsWorld( &cmd->viewParms );

	savedRefdef = tr.refdef;
	savedViewParms = tr.viewParms;
	savedStereoFrame = tr.currentStereoFrame;
	savedReplayCapture = tr.vrStereoReplayCapture;

	tr.refdef = cmd->refdef;
	tr.viewParms = cmd->viewParms;
	tr.currentStereoFrame = stereoFrame;
	tr.vrStereoReplayCapture = qfalse;
	R_SetupProjection();
	cmd->viewParms = tr.viewParms;

	tr.refdef = savedRefdef;
	tr.viewParms = savedViewParms;
	tr.currentStereoFrame = savedStereoFrame;
	tr.vrStereoReplayCapture = savedReplayCapture;
}

static void R_VR_PatchStereoReplayCommands( byte *cmds, stereoFrame_t stereoFrame ) {
	void *curCmd = cmds;
	qboolean seenDrawSurfs = qfalse;

	while ( 1 ) {
		curCmd = PADP( curCmd, sizeof( void * ) );

		switch ( *(int *)curCmd ) {
		case RC_SET_COLOR:
			curCmd = (void *)( (setColorCommand_t *)curCmd + 1 );
			break;
		case RC_STRETCH_PIC:
			if ( seenDrawSurfs ) {
				R_VR_PatchStretchPicCommand( (stretchPicCommand_t *)curCmd, stereoFrame );
			}
			curCmd = (void *)( (stretchPicCommand_t *)curCmd + 1 );
			break;
		case RC_SCISSOR:
			curCmd = (void *)( (scissorCommand_t *)curCmd + 1 );
			break;
		case RC_ROTATE_PIC:
		case RC_ROTATE_PIC2:
			if ( seenDrawSurfs ) {
				R_VR_PatchRotatePicCommand( (rotatePicCommand_t *)curCmd, stereoFrame );
			}
			curCmd = (void *)( (rotatePicCommand_t *)curCmd + 1 );
			break;
		case RC_DRAW_SURFS:
			R_VR_PatchDrawSurfsCommand( (drawSurfsCommand_t *)curCmd, stereoFrame );
			seenDrawSurfs = qtrue;
			curCmd = (void *)( (drawSurfsCommand_t *)curCmd + 1 );
			break;
		case RC_DRAW_BUFFER:
			curCmd = (void *)( (drawBufferCommand_t *)curCmd + 1 );
			break;
		case RC_SWAP_BUFFERS:
			curCmd = (void *)( (swapBuffersCommand_t *)curCmd + 1 );
			break;
		case RC_WORLD_EFFECTS:
			curCmd = (void *)( (setModeCommand_t *)curCmd + 1 );
			break;
		case RC_FLUSH:
			curCmd = (void *)( (swapBuffersCommand_t *)curCmd + 1 );
			break;
		case RC_END_OF_LIST:
		default:
			return;
		}
	}
}


/*
=====================
R_PerformanceCounters
=====================
*/
void R_PerformanceCounters( void ) {
	if ( !r_speeds->integer ) {
		// clear the counters even if we aren't printing
		memset( &tr.pc, 0, sizeof( tr.pc ) );
		memset( &backEnd.pc, 0, sizeof( backEnd.pc ) );
		return;
	}

	if (r_speeds->integer == 1) {
		const float texSize = R_SumOfUsedImages( qfalse )/(8*1048576.0f)*(r_texturebits->integer?r_texturebits->integer:glConfig.colorBits);
		ri.Printf (PRINT_ALL, "%i/%i shdrs/srfs %i leafs %i vrts %i/%i tris %.2fMB tex %.2f dc\n",
			backEnd.pc.c_shaders, backEnd.pc.c_surfaces, tr.pc.c_leafs, backEnd.pc.c_vertexes,
			backEnd.pc.c_indexes/3, backEnd.pc.c_totalIndexes/3,
			texSize, backEnd.pc.c_overDraw / (float)(glConfig.vidWidth * glConfig.vidHeight) );
	} else if (r_speeds->integer == 2) {
		ri.Printf (PRINT_ALL, "(patch) %i sin %i sclip  %i sout %i bin %i bclip %i bout\n",
			tr.pc.c_sphere_cull_patch_in, tr.pc.c_sphere_cull_patch_clip, tr.pc.c_sphere_cull_patch_out,
			tr.pc.c_box_cull_patch_in, tr.pc.c_box_cull_patch_clip, tr.pc.c_box_cull_patch_out );
		ri.Printf (PRINT_ALL, "(md3) %i sin %i sclip  %i sout %i bin %i bclip %i bout\n",
			tr.pc.c_sphere_cull_md3_in, tr.pc.c_sphere_cull_md3_clip, tr.pc.c_sphere_cull_md3_out,
			tr.pc.c_box_cull_md3_in, tr.pc.c_box_cull_md3_clip, tr.pc.c_box_cull_md3_out );
	} else if (r_speeds->integer == 3) {
		ri.Printf (PRINT_ALL, "viewcluster: %i\n", tr.viewCluster );
	} else if (r_speeds->integer == 4) {
		if ( backEnd.pc.c_dlightVertexes ) {
			ri.Printf (PRINT_ALL, "dlight srf:%i  culled:%i  verts:%i  tris:%i\n",
				tr.pc.c_dlightSurfaces, tr.pc.c_dlightSurfacesCulled,
				backEnd.pc.c_dlightVertexes, backEnd.pc.c_dlightIndexes / 3 );
		}
	}
	else if (r_speeds->integer == 5 )
	{
		ri.Printf( PRINT_ALL, "zFar: %.0f\n", tr.viewParms.zFar );
	}
	else if (r_speeds->integer == 6 )
	{
		ri.Printf( PRINT_ALL, "flare adds:%i tests:%i renders:%i\n",
			backEnd.pc.c_flareAdds, backEnd.pc.c_flareTests, backEnd.pc.c_flareRenders );
	}
	else if (r_speeds->integer == 7) {
		const float texSize = R_SumOfUsedImages(qtrue) / (1048576.0f);
		const float backBuff= glConfig.vidWidth * glConfig.vidHeight * glConfig.colorBits / (8.0f * 1024*1024);
		const float depthBuff= glConfig.vidWidth * glConfig.vidHeight * glConfig.depthBits / (8.0f * 1024*1024);
		const float stencilBuff= glConfig.vidWidth * glConfig.vidHeight * glConfig.stencilBits / (8.0f * 1024*1024);
		ri.Printf (PRINT_ALL, "Tex MB %.2f + buffers %.2f MB = Total %.2fMB\n",
			texSize, backBuff*2+depthBuff+stencilBuff, texSize+backBuff*2+depthBuff+stencilBuff);
	}

	memset( &tr.pc, 0, sizeof( tr.pc ) );
	memset( &backEnd.pc, 0, sizeof( backEnd.pc ) );
}

/*
====================
R_IssueRenderCommands
====================
*/
void R_IssueRenderCommands( qboolean runPerformanceCounters ) {
	renderCommandList_t	*cmdList;

	cmdList = &backEndData->commands;

	// add an end-of-list command
	byteAlias_t *ba = (byteAlias_t *)&cmdList->cmds[cmdList->used];
	ba->ui = RC_END_OF_LIST;

	// clear it out, in case this is a sync and not a buffer flip
	cmdList->used = 0;

	// at this point, the back end thread is idle, so it is ok
	// to look at it's performance counters
	if ( runPerformanceCounters ) {
		R_PerformanceCounters();
	}

	// actually start the commands going
	if ( !r_skipBackEnd->integer ) {
		// let it start on the new batch
		RB_ExecuteRenderCommands( cmdList->cmds );
	}
}


/*
====================
R_IssuePendingRenderCommands

Issue any pending commands and wait for them to complete.
====================
*/
void R_IssuePendingRenderCommands( void ) {
	if ( !tr.registered ) {
		return;
	}
	R_IssueRenderCommands( qfalse );
}

/*
============
R_GetCommandBufferReserved

make sure there is enough command space
============
*/
static void *R_GetCommandBufferReserved( int bytes, int reservedBytes ) {
	renderCommandList_t	*cmdList;

	cmdList = &backEndData->commands;
	bytes = PAD(bytes, sizeof(void *));

	// always leave room for the end of list command
	if ( cmdList->used + bytes + sizeof( int ) + reservedBytes > MAX_RENDER_COMMANDS ) {
		if ( bytes > MAX_RENDER_COMMANDS - (int)sizeof( int ) ) {
			ri.Error( ERR_FATAL, "R_GetCommandBuffer: bad size %i", bytes );
		}
		// if we run out of room, just start dropping commands
		return NULL;
	}

	cmdList->used += bytes;

	return cmdList->cmds + cmdList->used - bytes;
}

/*
============
R_GetCommandBuffer

make sure there is enough command space
============
*/
static void *R_GetCommandBuffer( int bytes ) {
	return R_GetCommandBufferReserved( bytes, PAD( sizeof( swapBuffersCommand_t ), sizeof(void *) ) );
}


/*
=============
R_AddDrawSurfCmd

=============
*/
void	R_AddDrawSurfCmd( drawSurf_t *drawSurfs, int numDrawSurfs ) {
	drawSurfsCommand_t	*cmd;

	cmd = (drawSurfsCommand_t *) R_GetCommandBuffer( sizeof( *cmd ) );
	if ( !cmd ) {
		return;
	}
	cmd->commandId = RC_DRAW_SURFS;

	cmd->drawSurfs = drawSurfs;
	cmd->numDrawSurfs = numDrawSurfs;

	cmd->refdef = tr.refdef;
	cmd->viewParms = tr.viewParms;
}


/*
=============
RE_SetColor

Passing NULL will set the color to white
=============
*/
void	RE_SetColor( const float *rgba ) {
	setColorCommand_t	*cmd;

	if ( !tr.registered ) {
		return;
	}
	cmd = (setColorCommand_t *) R_GetCommandBuffer( sizeof( *cmd ) );
	if ( !cmd ) {
		return;
	}
	cmd->commandId = RC_SET_COLOR;
	if ( !rgba ) {
		rgba = colorWhite;
	}
	cmd->color[0] = rgba[0];
	cmd->color[1] = rgba[1];
	cmd->color[2] = rgba[2];
	cmd->color[3] = rgba[3];

}


/*
=============
RE_StretchPic
=============
*/
void RE_StretchPic ( float x, float y, float w, float h,
					  float s1, float t1, float s2, float t2, qhandle_t hShader ) {
	stretchPicCommand_t	*cmd;

	if ( !tr.registered ) {
		return;
	}
	cmd = (stretchPicCommand_t *) R_GetCommandBuffer( sizeof( *cmd ) );
	if ( !cmd ) {
		return;
	}
	cmd->commandId = RC_STRETCH_PIC;
	cmd->shader = R_GetShaderByHandle( hShader );
	cmd->x = x;
	cmd->y = y;
	cmd->w = w;
	cmd->h = h;
	cmd->s1 = s1;
	cmd->t1 = t1;
	cmd->s2 = s2;
	cmd->t2 = t2;
}

/*
=============
RE_RotatePic
=============
*/
void RE_RotatePic ( float x, float y, float w, float h,
					  float s1, float t1, float s2, float t2,float a, qhandle_t hShader ) {
	rotatePicCommand_t	*cmd;

	if (!tr.registered) {
		return;
	}
	cmd = (rotatePicCommand_t *) R_GetCommandBuffer( sizeof( *cmd ) );
	if ( !cmd ) {
		return;
	}
	cmd->commandId = RC_ROTATE_PIC;
	cmd->shader = R_GetShaderByHandle( hShader );
	cmd->x = x;
	cmd->y = y;
	cmd->w = w;
	cmd->h = h;
	cmd->s1 = s1;
	cmd->t1 = t1;
	cmd->s2 = s2;
	cmd->t2 = t2;
	cmd->a = a;
}

/*
=============
RE_RotatePic2
=============
*/
void RE_RotatePic2 ( float x, float y, float w, float h,
					  float s1, float t1, float s2, float t2,float a, qhandle_t hShader ) {
	rotatePicCommand_t	*cmd;

	if (!tr.registered) {
		return;
	}

	cmd = (rotatePicCommand_t *) R_GetCommandBuffer( sizeof( *cmd ) );
	if ( !cmd ) {
		return;
	}
	cmd->commandId = RC_ROTATE_PIC2;
	cmd->shader = R_GetShaderByHandle( hShader );
	cmd->x = x;
	cmd->y = y;
	cmd->w = w;
	cmd->h = h;
	cmd->s1 = s1;
	cmd->t1 = t1;
	cmd->s2 = s2;
	cmd->t2 = t2;
	cmd->a = a;
}

void RE_LAGoggles( void )
{
	tr.refdef.rdflags |= (RDF_doLAGoggles|RDF_doFullbright);
	tr.refdef.doLAGoggles = qtrue;

	jk_fog_t		*fog = &tr.world->fogs[tr.world->numfogs];

	fog->parms.color[0] = 0.75f;
	fog->parms.color[1] = 0.42f + Q_flrand(0.0f, 1.0f) * 0.025f;
	fog->parms.color[2] = 0.07f;
	fog->parms.depthForOpaque = 10000;
	fog->colorInt = ColorBytes4(fog->parms.color[0], fog->parms.color[1], fog->parms.color[2], 1.0f);
	fog->tcScale = 2.0f / ( fog->parms.depthForOpaque * (1.0f + cos( tr.refdef.floatTime) * 0.1f));
}

void RE_RenderWorldEffects(void)
{
	setModeCommand_t	*cmd;

	if (!tr.registered) {
		return;
	}

	cmd = (setModeCommand_t *)R_GetCommandBuffer( sizeof( *cmd ) );
	if ( !cmd ) {
		return;
	}
	cmd->commandId = RC_WORLD_EFFECTS;
}

/*
=============
RE_Scissor
=============
*/
void RE_Scissor ( float x, float y, float w, float h)
{
	scissorCommand_t	*cmd;

	if (!tr.registered) {
		return;
	}

	cmd = (scissorCommand_t *) R_GetCommandBuffer( sizeof( *cmd ) );
	if ( !cmd ) {
		return;
	}
	cmd->commandId = RC_SCISSOR;
	cmd->x = x;
	cmd->y = y;
	cmd->w = w;
	cmd->h = h;
}

/*
====================
RE_BeginFrame

If running in stereo, RE_BeginFrame will be called twice
for each RE_EndFrame
====================
*/
void RE_BeginFrame( stereoFrame_t stereoFrame ) {
	drawBufferCommand_t	*cmd = NULL;

	if ( !tr.registered ) {
		return;
	}
	glState.finishCalled = qfalse;
	tr.currentStereoFrame = stereoFrame;

	tr.frameCount++;
	tr.frameSceneNum = 0;

	//
	// do overdraw measurement
	//
#ifndef HAVE_GLES
	if ( r_measureOverdraw->integer )
	{
		if ( glConfig.stencilBits < 4 )
		{
			ri.Printf( PRINT_ALL, "Warning: not enough stencil bits to measure overdraw: %d\n", glConfig.stencilBits );
			ri.Cvar_Set( "r_measureOverdraw", "0" );
			r_measureOverdraw->modified = qfalse;
		}
		else if ( r_shadows->integer == 2 )
		{
			ri.Printf( PRINT_ALL, "Warning: stencil shadows and overdraw measurement are mutually exclusive\n" );
			ri.Cvar_Set( "r_measureOverdraw", "0" );
			r_measureOverdraw->modified = qfalse;
		}
		else
		{
			R_IssuePendingRenderCommands();
			qglEnable( GL_STENCIL_TEST );
			qglStencilMask( ~0U );
			qglClearStencil( 0U );
			qglStencilFunc( GL_ALWAYS, 0U, ~0U );
			qglStencilOp( GL_KEEP, GL_INCR, GL_INCR );
		}
		r_measureOverdraw->modified = qfalse;
	}
	else
	{
		// this is only reached if it was on and is now off
		if ( r_measureOverdraw->modified ) {
			R_IssuePendingRenderCommands();
			qglDisable( GL_STENCIL_TEST );
			r_measureOverdraw->modified = qfalse;
		}
	}
#endif

	//
	// texturemode stuff
	//
	if ( r_textureMode->modified || r_ext_texture_filter_anisotropic->modified) {
		R_IssuePendingRenderCommands();
		GL_TextureMode( r_textureMode->string );
		r_textureMode->modified = qfalse;
		r_ext_texture_filter_anisotropic->modified = qfalse;
	}

	//
	// gamma stuff
	//
	if ( r_gamma->modified ) {
		r_gamma->modified = qfalse;

		R_IssuePendingRenderCommands();
		R_SetColorMappings();
	}

    // check for errors
    if ( !r_ignoreGLErrors->integer ) {
        int	err;

		R_IssuePendingRenderCommands();
        if ( ( err = qglGetError() ) != GL_NO_ERROR ) {
            Com_Error( ERR_FATAL, "RE_BeginFrame() - glGetError() failed (0x%x)!\n", err );
        }
    }

	//
	// draw buffer stuff
	//
	cmd = (drawBufferCommand_t *) R_GetCommandBuffer( sizeof( *cmd ) );
	if ( !cmd ) {
		return;
	}
	cmd->commandId = RC_DRAW_BUFFER;

	{
		if ( stereoFrame == STEREO_LEFT ) {
			cmd->buffer = (int)0;
		} else if ( stereoFrame == STEREO_RIGHT ) {
			cmd->buffer = (int)1;
		} else if ( stereoFrame == STEREO_CENTER ) {
			cmd->buffer = (int)0;
		} else {
			ri.Error( ERR_FATAL, "RE_BeginFrame: Stereo is enabled, but stereoFrame was %i", stereoFrame );
		}
	}
}


/*
=============
RE_EndFrame

Returns the number of msec spent in the back end
=============
*/
void RE_EndFrame( int *frontEndMsec, int *backEndMsec ) {
	swapBuffersCommand_t    *cmd;

	if ( !tr.registered ) {
		return;
	}

	cmd = (swapBuffersCommand_t*)R_GetCommandBuffer(sizeof(*cmd));
	if (!cmd) {
		return;
	}

	cmd->commandId = RC_FLUSH;

	R_IssueRenderCommands( qfalse );

    // use the other buffers next frame, because another CPU
    // may still be rendering into the current ones
    R_InitNextFrame();

	if (frontEndMsec) {
		*frontEndMsec = tr.frontEndMsec;
	}
	tr.frontEndMsec = 0;
	if (backEndMsec) {
		*backEndMsec = backEnd.pc.msec;
	}
	backEnd.pc.msec = 0;

	for(int i=0;i<MAX_LIGHT_STYLES;i++)
	{
		styleUpdated[i] = false;
	}
}

qboolean RE_VR_BeginStereoReplayCapture( void ) {
	if ( !tr.registered ) {
		return qfalse;
	}
	if ( r_debugSurface->integer ) {
		return qfalse;
	}

	tr.vrStereoReplayCapture = qtrue;
	vrStereoReplayActive = qfalse;
	vrStereoReplayCommandBytes = 0;
	return qtrue;
}

void RE_VR_CancelStereoReplayCapture( void ) {
	tr.vrStereoReplayCapture = qfalse;
	vrStereoReplayActive = qfalse;
	vrStereoReplayCommandBytes = 0;
	R_InitNextFrame();
}

qboolean RE_VR_ReplayStereoFrame( stereoFrame_t stereoFrame, qboolean finalReplay ) {
	renderCommandList_t *cmdList;
	swapBuffersCommand_t *cmd;
	vrStereoReplayStats_t stats;
	static qboolean loggedReplayStats = qfalse;
	static qboolean loggedReplay3DStats = qfalse;

	if ( !tr.registered ) {
		return qfalse;
	}

	cmdList = &backEndData->commands;

	if ( !vrStereoReplayActive ) {
		cmd = (swapBuffersCommand_t *)R_GetCommandBufferReserved( sizeof( *cmd ), 0 );
		if ( !cmd ) {
			ri.Printf( PRINT_WARNING,
				"VR stereo replay: failed to reserve flush command (%d bytes used).\n",
				cmdList->used );
			RE_VR_CancelStereoReplayCapture();
			return qfalse;
		}
		cmd->commandId = RC_FLUSH;

		*(int *)( cmdList->cmds + cmdList->used ) = RC_END_OF_LIST;
		vrStereoReplayCommandBytes = cmdList->used + sizeof( int );
		if ( vrStereoReplayCommandBytes > (int)sizeof( vrStereoReplayCommands.cmds ) ) {
			RE_VR_CancelStereoReplayCapture();
			return qfalse;
		}

		memcpy( vrStereoReplayCommands.cmds, cmdList->cmds, vrStereoReplayCommandBytes );
		if ( !R_VR_InspectStereoReplayCommands( vrStereoReplayCommands.cmds, &stats ) ) {
			if ( !vrStereoReplayLoggedInvalid ) {
				ri.Printf( PRINT_WARNING,
					"VR stereo replay: invalid captured command stream (%d bytes, drawSurfs=%d, stretchPics=%d, swaps=%d, flushes=%d, unknown=%d).\n",
					vrStereoReplayCommandBytes, stats.drawSurfs, stats.stretchPics, stats.swaps, stats.flushes, stats.unknownCommand );
				vrStereoReplayLoggedInvalid = qtrue;
			}
			RE_VR_CancelStereoReplayCapture();
			return qfalse;
		}
		if ( stats.drawSurfs == 0 ) {
			if ( !vrStereoReplayLoggedInvalid ) {
				ri.Printf( PRINT_WARNING,
					"VR stereo replay: captured command stream has no 3D draw surfaces (%d bytes, stretchPics=%d, swaps=%d, flushes=%d); falling back to per-eye render.\n",
					vrStereoReplayCommandBytes, stats.stretchPics, stats.swaps, stats.flushes );
				vrStereoReplayLoggedInvalid = qtrue;
			}
			RE_VR_CancelStereoReplayCapture();
			return qfalse;
		}
		if ( !loggedReplayStats || ( stats.drawSurfs > 0 && !loggedReplay3DStats ) ) {
			ri.Printf( PRINT_ALL,
				"VR stereo replay: captured %d bytes (%d drawSurfs, %d stretchPics, %d swaps, %d flushes).\n",
				vrStereoReplayCommandBytes, stats.drawSurfs, stats.stretchPics, stats.swaps, stats.flushes );
			loggedReplayStats = qtrue;
			if ( stats.drawSurfs > 0 ) {
				loggedReplay3DStats = qtrue;
			}
		}
		vrStereoReplayActive = qtrue;
		tr.vrStereoReplayCapture = qfalse;
	}

	memcpy( vrStereoReplayScratch.cmds, vrStereoReplayCommands.cmds, vrStereoReplayCommandBytes );
	R_VR_PatchStereoReplayCommands( vrStereoReplayScratch.cmds, stereoFrame );

	if ( !r_skipBackEnd->integer ) {
		RB_ExecuteRenderCommands( vrStereoReplayScratch.cmds );
	}

	if ( finalReplay ) {
		R_InitNextFrame();
		vrStereoReplayActive = qfalse;
		vrStereoReplayCommandBytes = 0;
		tr.frontEndMsec = 0;
		backEnd.pc.msec = 0;

		for ( int i = 0; i < MAX_LIGHT_STYLES; i++ ) {
			styleUpdated[i] = false;
		}
	}

	return qtrue;
}

void RE_SubmitStereoFrame( ) {
	swapBuffersCommand_t    *cmd;

	if ( !tr.registered ) {
		return;
	}

	cmd = (swapBuffersCommand_t*)R_GetCommandBuffer(sizeof(*cmd));
	if (!cmd) {
		return;
	}

	cmd->commandId = RC_SWAP_BUFFERS;

	R_IssueRenderCommands( qtrue );
}
