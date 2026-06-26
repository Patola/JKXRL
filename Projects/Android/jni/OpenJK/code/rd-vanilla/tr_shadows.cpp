/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
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

/*

  for a projection shadow:

  point[x] += light vector * ( z - shadow plane )
  point[y] +=
  point[z] = shadow plane

  1 0 light[x] / light[z]

*/

#define _STENCIL_REVERSE

typedef struct {
	int		i2;
	int		facing;
} edgeDef_t;

#define	MAX_EDGE_DEFS	32

static	edgeDef_t	edgeDefs[SHADER_MAX_VERTEXES][MAX_EDGE_DEFS];
static	int			numEdgeDefs[SHADER_MAX_VERTEXES];
static	int			facing[SHADER_MAX_INDEXES/3];
static	vec3_t		shadowXyz[SHADER_MAX_VERTEXES];


void R_AddEdgeDef( int i1, int i2, int facing ) {
	int		c;

	c = numEdgeDefs[ i1 ];
	if ( c == MAX_EDGE_DEFS ) {
		return;		// overflow
	}
	edgeDefs[ i1 ][ c ].i2 = i2;
	edgeDefs[ i1 ][ c ].facing = facing;

	numEdgeDefs[ i1 ]++;
}

void R_RenderShadowEdges( void ) {
	int		i;
	int		c;
	int		j;
	int		i2;
	//int		c_edges, c_rejected;
#if 0
	int		c2, k;
	int		hit[2];
#endif
#ifdef _STENCIL_REVERSE
	int		numTris;
	int		o1, o2, o3;
#endif

	// an edge is NOT a silhouette edge if its face doesn't face the light,
	// or if it has a reverse paired edge that also faces the light.
	// A well behaved polyhedron would have exactly two faces for each edge,
	// but lots of models have dangling edges or overfanned edges
#if 0
	c_edges = 0;
	c_rejected = 0;
#endif

	for ( i = 0 ; i < tess.numVertexes ; i++ ) {
		c = numEdgeDefs[ i ];
		for ( j = 0 ; j < c ; j++ ) {
			if ( !edgeDefs[ i ][ j ].facing ) {
				continue;
			}

			//with this system we can still get edges shared by more than 2 tris which
			//produces artifacts including seeing the shadow through walls. So for now
			//we are going to render all edges even though it is a tiny bit slower. -rww
#if 1
			i2 = edgeDefs[ i ][ j ].i2;
			qglBegin( GL_TRIANGLE_STRIP );
				qglVertex3fv( tess.xyz[ i ] );
				qglVertex3fv( shadowXyz[ i ] );
				qglVertex3fv( tess.xyz[ i2 ] );
				qglVertex3fv( shadowXyz[ i2 ] );
			qglEnd();
#else
			hit[0] = 0;
			hit[1] = 0;

			i2 = edgeDefs[ i ][ j ].i2;
			c2 = numEdgeDefs[ i2 ];
			for ( k = 0 ; k < c2 ; k++ ) {
				if ( edgeDefs[ i2 ][ k ].i2 == i ) {
					hit[ edgeDefs[ i2 ][ k ].facing ]++;
				}
			}

			// if it doesn't share the edge with another front facing
			// triangle, it is a sil edge
			if ( hit[ 1 ] == 0 ) {
				qglBegin( GL_TRIANGLE_STRIP );
				qglVertex3fv( tess.xyz[ i ] );
				qglVertex3fv( shadowXyz[ i ] );
				qglVertex3fv( tess.xyz[ i2 ] );
				qglVertex3fv( shadowXyz[ i2 ] );
				qglEnd();
				c_edges++;
			} else {
				c_rejected++;
			}
#endif
		}
	}

#ifdef _STENCIL_REVERSE
	//Carmack Reverse<tm> method requires that volumes
	//be capped properly -rww
	numTris = tess.numIndexes / 3;

	for ( i = 0 ; i < numTris ; i++ )
	{
		if ( !facing[i] )
		{
			continue;
		}

		o1 = tess.indexes[ i*3 + 0 ];
		o2 = tess.indexes[ i*3 + 1 ];
		o3 = tess.indexes[ i*3 + 2 ];

		qglBegin(GL_TRIANGLES);
			qglVertex3fv(tess.xyz[o1]);
			qglVertex3fv(tess.xyz[o2]);
			qglVertex3fv(tess.xyz[o3]);
		qglEnd();
		qglBegin(GL_TRIANGLES);
			qglVertex3fv(shadowXyz[o3]);
			qglVertex3fv(shadowXyz[o2]);
			qglVertex3fv(shadowXyz[o1]);
		qglEnd();
	}
#endif
}

//#define _DEBUG_STENCIL_SHADOWS

/*
=================
RB_ShadowTessEnd

triangleFromEdge[ v1 ][ v2 ]


  set triangle from edge( v1, v2, tri )
  if ( facing[ triangleFromEdge[ v1 ][ v2 ] ] && !facing[ triangleFromEdge[ v2 ][ v1 ] ) {
  }
=================
*/
void RB_DoShadowTessEnd( vec3_t lightPos );
void RB_ShadowTessEnd( void )
{
#if 0
	if (backEnd.currentEntity &&
		(backEnd.currentEntity->directedLight[0] ||
			backEnd.currentEntity->directedLight[1] ||
			backEnd.currentEntity->directedLight[2]))
	{ //an ent that has its light set for it
		RB_DoShadowTessEnd(NULL);
		return;
	}

//	if (!tess.dlightBits)
//	{
//		return;
//	}

	int i = 0;
	dlight_t *dl;

	R_TransformDlights( backEnd.refdef.num_dlights, backEnd.refdef.dlights, &backEnd.ori );
/*	while (i < tr.refdef.num_dlights)
	{
		if (tess.dlightBits & (1 << i))
		{
			dl = &tr.refdef.dlights[i];

			RB_DoShadowTessEnd(dl->transformed);
		}

		i++;
	}
	*/
			dl = &tr.refdef.dlights[0];

			RB_DoShadowTessEnd(dl->transformed);

#else //old ents-only way
	RB_DoShadowTessEnd(NULL);
#endif
}

// v0.2: fuzzy soft shadows via a screen-space blur of the solid mask (RB_ShadowBlurFinish).
//  Instead of stacking N jittered solid layers (banded penumbra), we render ONE solid
//  union mask and blur it for a continuous fuzzy edge. Gated by r_shadowBlur on mode 5
//  (default 16 = penumbra width in px; 0 falls back to the layered penumbra).
//  Pipeline (avoids compositing a textured quad in the offset VR eye viewport, which
//  is the part that never worked - everything textured happens in our own aux FBOs,
//  and only pixel-aligned ops touch the eye FBO):
//    1. accumulate the solid union into the eye stencil (1 tap, no jitter)
//    2. glCopyTexImage2D the eye colour -> sceneTex
//    3. bind an aux FBO (colour = maskTex, depth-stencil = the eye's shared d-s
//       texture) and draw the alpha-scaled mask where stencil != 0  -> maskTex
//    4. glGenerateMipmap(maskTex): the mip chain is a true area-average pyramid;
//       sample a pinned LOD = log2(r_shadowBlur) for a smooth, temporally stable blur
//    5. composite in an aux FBO: sceneTex * (1 - blurredMask) -> finalTex. The eye
//       ALPHA is the VR compositor's layer opacity, so finalTex alpha is kept = 1
//    6. glBlitFramebuffer finalTex -> eye colour (pixel-aligned, no viewport math)
//  FBO entry points are loaded via ri.GL_GetProcAddress (see pGenFramebuffers etc).
//  Cost is ~1 mask render per caster + a fixed per-view blur, so in crowded scenes it
//  beats the per-caster*N layered cost while looking smoother.
//
// TODO (further out): shadow mapping + PCF - the modern approach (one depth pass from
//  the light, percentage-closer filtered), cheaper and self-shadowing, but needs a
//  programmable (GLSL) pipeline this fixed-function renderer does not have.
// Number of solid shadow layers (taps) for the active stencil mode. Modes 2 and 4
// are hard = 1 layer; mode 5 (Ultra) stacks r_shadowSoft jittered solid layers.
qboolean R_ShadowBlurActive( void );	// fwd; defined with the blur pipeline below
int R_NumShadowTaps( void )
{
	if ( r_shadows->integer != 5 ) {
		return 1;
	}
	// Blur mode renders ONE solid mask and softens it in screen space, so it needs
	// just a single tap (the softness comes from the blur, not from stacked layers).
	if ( R_ShadowBlurActive() ) {
		return 1;
	}
	int n = r_shadowSoft->integer;
	if ( n < 1 ) n = 1;
	if ( n > MAX_SHADOW_TAPS ) n = MAX_SHADOW_TAPS;
	return n;
}

// Which soft-shadow tap a shadow-marker shader belongs to (-1 if it isn't one).
int R_ShadowTapForShader( const shader_t *sh )
{
	for ( int t = 0 ; t < MAX_SHADOW_TAPS ; t++ ) {
		if ( sh == tr.shadowShader[t] ) {
			return t;
		}
	}
	return -1;
}

void RB_DoShadowTessEnd( vec3_t lightPos )
{
	int		i;
	int		numTris;
	vec3_t	lightDir;

	if ( glConfig.stencilBits < 4 ) {
		return;
	}

	// Each shadow surface is queued once per soft-shadow tap, each tap using its own
	// marker shader so they sort tap-major into the backend. This call renders ONE
	// tap's volume into the stencil; all of a tap's casters accumulate, then the
	// backend darkens that tap as a single SOLID layer (RB_ShadowDarkenTap) and clears
	// for the next. Stacking the solid layers - each binary, so independent of winding
	// magnitude - builds a soft penumbra without ever revealing the model's internal
	// structure (which is what a count-based gradient leaked).
	int numTaps = R_NumShadowTaps();
	int tap = R_ShadowTapForShader( tess.shader );
	if ( tap < 0 ) tap = 0;
	float spread = r_shadowSoftSpread->value;

	vec3_t	worldxyz;
	vec3_t	entLight;
	float	groundDist;
	vec3_t	baseDir;

	VectorCopy( backEnd.currentEntity->lightDir, entLight );
	entLight[2] = 0.0f;
	VectorNormalize(entLight);

	//Oh well, just cast them straight down no matter what onto the ground plane.
	VectorSet(baseDir, entLight[0]*0.3f, entLight[1]*0.3f, 1.0f);

	// Jitter the light direction for this tap, evenly around a ring centred on the
	// true direction (centre / no jitter for hard modes). Each tap therefore lands a
	// slightly shifted solid silhouette; their stacked overlap is the penumbra.
	if ( numTaps > 1 ) {
		float ang = (float)tap * ( 2.0f * M_PI ) / (float)numTaps;
		lightDir[0] = baseDir[0] + cos( ang ) * spread;
		lightDir[1] = baseDir[1] + sin( ang ) * spread;
		lightDir[2] = baseDir[2];
	} else {
		VectorCopy( baseDir, lightDir );
	}

	// project vertexes away from the (jittered) light direction
	for ( i = 0 ; i < tess.numVertexes ; i++ ) {
		VectorAdd(tess.xyz[i], backEnd.ori.origin, worldxyz);
		groundDist = worldxyz[2] - backEnd.currentEntity->e.shadowPlane;
		groundDist += 16.0f; //fudge factor
		VectorMA( tess.xyz[i], -groundDist, lightDir, shadowXyz[i] );
	}

	// decide which triangles face the light and build the silhouette edges
	memset( numEdgeDefs, 0, 4 * tess.numVertexes );

	numTris = tess.numIndexes / 3;
	for ( i = 0 ; i < numTris ; i++ ) {
		int		i1, i2, i3;
		vec3_t	d1, d2, normal;
		float	*v1, *v2, *v3;
		float	d;

		i1 = tess.indexes[ i*3 + 0 ];
		i2 = tess.indexes[ i*3 + 1 ];
		i3 = tess.indexes[ i*3 + 2 ];

		v1 = tess.xyz[ i1 ];
		v2 = tess.xyz[ i2 ];
		v3 = tess.xyz[ i3 ];

		VectorSubtract( v2, v1, d1 );
		VectorSubtract( v3, v1, d2 );
		CrossProduct( d1, d2, normal );
		d = DotProduct( normal, lightDir );

		facing[ i ] = ( d > 0 ) ? 1 : 0;

		R_AddEdgeDef( i1, i2, facing[ i ] );
		R_AddEdgeDef( i2, i3, facing[ i ] );
		R_AddEdgeDef( i3, i1, facing[ i ] );
	}

	GL_Bind( tr.whiteImage );
	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO );

#ifndef _DEBUG_STENCIL_SHADOWS
	qglColor3f( 0.2f, 0.2f, 0.2f );

	// don't write to the color buffer; just accumulate the volume into the stencil
	qglColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );

	qglEnable( GL_STENCIL_TEST );
	qglStencilFunc( GL_ALWAYS, 1, 255 );
#else
	qglColor3f( 1.0f, 0.0f, 0.0f );
	qglPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
#endif

#ifdef _STENCIL_REVERSE
	qglDepthFunc(GL_LESS);

	// The layer is darkened with a binary (stencil != 0) test, so winding sign is
	// irrelevant - use the fast single-drawcall path where available.
	if (glConfig.doStencilShadowsInOneDrawcall)
	{
		GL_Cull(CT_TWO_SIDED);
		qglStencilOpSeparate(GL_FRONT, GL_KEEP, GL_INCR_WRAP, GL_KEEP);
		qglStencilOpSeparate(GL_BACK, GL_KEEP, GL_DECR_WRAP, GL_KEEP);

		R_RenderShadowEdges();
	}
	else
	{
		GL_Cull(CT_FRONT_SIDED);
		qglStencilOp(GL_KEEP, GL_INCR, GL_KEEP);

		R_RenderShadowEdges();

		GL_Cull(CT_BACK_SIDED);
		qglStencilOp(GL_KEEP, GL_DECR, GL_KEEP);

		R_RenderShadowEdges();
	}

	qglDepthFunc(GL_LEQUAL);
#else
	// mirrors have the culling order reversed
	if ( backEnd.viewParms.isMirror ) {
		qglCullFace( GL_FRONT );
		qglStencilOp( GL_KEEP, GL_KEEP, GL_INCR );

		R_RenderShadowEdges();

		qglCullFace( GL_BACK );
		qglStencilOp( GL_KEEP, GL_KEEP, GL_DECR );

		R_RenderShadowEdges();
	} else {
		qglCullFace( GL_BACK );
		qglStencilOp( GL_KEEP, GL_KEEP, GL_INCR );

		R_RenderShadowEdges();

		qglCullFace( GL_FRONT );
		qglStencilOp( GL_KEEP, GL_KEEP, GL_DECR );

		R_RenderShadowEdges();
	}
#endif

	// reenable writing to the color buffer (the per-tap darken happens in the backend)
	qglColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );

#ifdef _DEBUG_STENCIL_SHADOWS
	qglPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
#endif
}


/*
=================
RB_ShadowFinish

Darken everything that is is a shadow volume.
We have to delay this until everything has been shadowed,
because otherwise shadows from different body parts would
overlap and double darken.
=================
*/
void RB_ShadowFinish( void ) {
	if ( r_shadows->integer != 2 && r_shadows->integer < 4 ) {
		return;
	}
	if ( glConfig.stencilBits < 4 ) {
		return;
	}

#ifdef _DEBUG_STENCIL_SHADOWS
	return;
#endif

	qglEnable( GL_STENCIL_TEST );
	qglStencilFunc( GL_NOTEQUAL, 0, 255 );

	qglStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );

	bool planeZeroBack = false;
	if (qglIsEnabled(GL_CLIP_PLANE0))
	{
		planeZeroBack = true;
		qglDisable (GL_CLIP_PLANE0);
	}
	GL_Cull(CT_TWO_SIDED);
	//qglDisable (GL_CULL_FACE);

	GL_Bind( tr.whiteImage );

	// Darken the shadowed pixels by drawing a large quad in EYE space through the
	// existing (VR) projection matrix - the same projection the scene renders with.
	// An identity projection does not rasterize under the VR renderer, so we keep
	// the projection and only reset the modelview, placing the quad a few units in
	// front of the eye so it fills the frustum. Depth testing is off so it covers
	// everything the stencil marks as shadowed.
	qglMatrixMode( GL_MODELVIEW );
	qglPushMatrix();
	qglLoadIdentity();

	float shadowAlpha = r_shadowAlpha->value;
	if ( shadowAlpha < 0.0f ) shadowAlpha = 0.0f;
	else if ( shadowAlpha > 1.0f ) shadowAlpha = 1.0f;

	GL_State( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHTEST_DISABLE );

	int numTaps = ( r_shadows->integer >= 5 ) ? r_shadowSoft->integer : 1;
	if ( numTaps < 1 ) numTaps = 1;
	if ( numTaps > MAX_SHADOW_TAPS ) numTaps = MAX_SHADOW_TAPS;

	if ( numTaps <= 1 )
	{
		// hard shadow: one darkening pass over any shadowed pixel
		qglStencilFunc( GL_NOTEQUAL, 0, 255 );
		qglColor4f( 0.0f, 0.0f, 0.0f, shadowAlpha );
		qglBegin( GL_QUADS );
		qglVertex3f( -200.0f,  200.0f, -10.0f );
		qglVertex3f(  200.0f,  200.0f, -10.0f );
		qglVertex3f(  200.0f, -200.0f, -10.0f );
		qglVertex3f( -200.0f, -200.0f, -10.0f );
		qglEnd ();
	}
	else
	{
		// Ultra: darken the unioned multi-tap mask as a single SOLID pass (binary,
		// like the hard modes) so no internal model structure shows through. The
		// jitter spread slightly widens/softens the silhouette. NOTE: the proper
		// layered-penumbra darkening is handled by the per-tap path in the backend
		// (RB_ShadowDarkenTap); this is the fallback when that path isn't active.
		qglStencilFunc( GL_NOTEQUAL, 0, 255 );
		qglColor4f( 0.0f, 0.0f, 0.0f, shadowAlpha );
		qglBegin( GL_QUADS );
		qglVertex3f( -200.0f,  200.0f, -10.0f );
		qglVertex3f(  200.0f,  200.0f, -10.0f );
		qglVertex3f(  200.0f, -200.0f, -10.0f );
		qglVertex3f( -200.0f, -200.0f, -10.0f );
		qglEnd ();
	}

	qglColor4f(1,1,1,1);
	qglDisable( GL_STENCIL_TEST );
	if (planeZeroBack)
	{
		qglEnable (GL_CLIP_PLANE0);
	}
	qglMatrixMode( GL_MODELVIEW );
	qglPopMatrix();
}


/*
=================
RB_ShadowDarkenTap

Darken one solid shadow layer: everything the current tap's volumes marked in the
stencil (binary, NOTEQUAL 0 - so no internal model structure shows), then clear the
stencil so the next tap starts fresh. Per-tap alpha is set so that stacking all the
layers reaches the requested total darkness in the umbra, while edges touched by
fewer layers stay lighter - that overlap is the soft penumbra.
=================
*/
/*
=================================================================================
Screen-space blurred soft shadows (mode 5, r_shadowBlur > 0)

The solid union mask is in the eye stencil. We capture the eye colour, draw the
mask into an aux texture (sharing the eye's depth-stencil), downsample it (linear
filtering = a cheap box blur), composite scene*(1-blurredMask) in an aux FBO, then
blit the result back. Every TEXTURED pass runs in our own aux FBOs with a normal
0..w viewport, so it avoids the offset VR eye viewport; only pixel-aligned
glCopyTexImage2D / glBlitFramebuffer touch the eye FBO.
=================================================================================
*/
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif
#ifndef GL_DRAW_FRAMEBUFFER_BINDING
#define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#endif
#ifndef GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME
#define GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME 0x8CD1
#endif

static PFNGLGENFRAMEBUFFERSPROC          pGenFramebuffers;
static PFNGLDELETEFRAMEBUFFERSPROC       pDeleteFramebuffers;
static PFNGLBINDFRAMEBUFFERPROC          pBindFramebuffer;
static PFNGLFRAMEBUFFERTEXTURE2DPROC     pFramebufferTexture2D;
static PFNGLCHECKFRAMEBUFFERSTATUSPROC   pCheckFramebufferStatus;
static PFNGLBLITFRAMEBUFFERPROC          pBlitFramebuffer;
static PFNGLGETFRAMEBUFFERATTACHMENTPARAMETERIVPROC pGetFramebufferAttachmentParameteriv;
static PFNGLGENERATEMIPMAPPROC                pGenerateMipmap;

static int shadowBlurGLState = 0;	// 0 untried, 1 ok, -1 unavailable
static qboolean R_ShadowBlurGLReady( void )
{
	if ( shadowBlurGLState != 0 ) {
		return (qboolean)( shadowBlurGLState == 1 );
	}
	pGenFramebuffers      = (PFNGLGENFRAMEBUFFERSPROC)ri.GL_GetProcAddress("glGenFramebuffers");
	pDeleteFramebuffers   = (PFNGLDELETEFRAMEBUFFERSPROC)ri.GL_GetProcAddress("glDeleteFramebuffers");
	pBindFramebuffer      = (PFNGLBINDFRAMEBUFFERPROC)ri.GL_GetProcAddress("glBindFramebuffer");
	pFramebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2DPROC)ri.GL_GetProcAddress("glFramebufferTexture2D");
	pCheckFramebufferStatus = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)ri.GL_GetProcAddress("glCheckFramebufferStatus");
	pBlitFramebuffer      = (PFNGLBLITFRAMEBUFFERPROC)ri.GL_GetProcAddress("glBlitFramebuffer");
	pGetFramebufferAttachmentParameteriv =
		(PFNGLGETFRAMEBUFFERATTACHMENTPARAMETERIVPROC)ri.GL_GetProcAddress("glGetFramebufferAttachmentParameteriv");
	pGenerateMipmap       = (PFNGLGENERATEMIPMAPPROC)ri.GL_GetProcAddress("glGenerateMipmap");
	qboolean ok = (qboolean)( pGenFramebuffers && pDeleteFramebuffers && pBindFramebuffer &&
		pFramebufferTexture2D && pCheckFramebufferStatus && pBlitFramebuffer &&
		pGetFramebufferAttachmentParameteriv && pGenerateMipmap );
	shadowBlurGLState = ok ? 1 : -1;
	if ( !ok ) {
		ri.Printf( PRINT_ALL, "r_shadowBlur: framebuffer entry points unavailable; using layered Ultra\n" );
	}
	return ok;
}

qboolean R_ShadowBlurActive( void )
{
	return (qboolean)( r_shadows->integer == 5 && r_shadowBlur->integer > 0
		&& glConfig.stencilBits >= 4 && R_ShadowBlurGLReady() );
}

static GLuint shadowBlurFBO = 0;
static GLuint shadowSceneTex = 0, shadowMaskTex = 0, shadowFinalTex = 0;
static int    shadowBlurW = 0, shadowBlurH = 0;

static GLuint R_BlurMakeTex( int w, int h, GLint filter )
{
	GLuint t = 0;
	qglGenTextures( 1, &t );
	qglBindTexture( GL_TEXTURE_2D, t );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	qglTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL );
	return t;
}

static void R_ShadowBlurEnsureTargets( int w, int h )
{
	if ( shadowBlurFBO && shadowBlurW == w && shadowBlurH == h ) {
		return;
	}
	if ( shadowSceneTex ) qglDeleteTextures( 1, &shadowSceneTex );
	if ( shadowMaskTex )  qglDeleteTextures( 1, &shadowMaskTex );
	if ( shadowFinalTex ) qglDeleteTextures( 1, &shadowFinalTex );
	shadowSceneTex = R_BlurMakeTex( w, h, GL_LINEAR );
	shadowMaskTex  = R_BlurMakeTex( w, h, GL_LINEAR );
	shadowFinalTex = R_BlurMakeTex( w, h, GL_NEAREST );
	// the mask is sampled through its mip chain (rebuilt each frame) so the blur is a
	// true area average; needs a mipmap min filter for trilinear LOD sampling
	qglBindTexture( GL_TEXTURE_2D, shadowMaskTex );
	qglTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
	if ( !shadowBlurFBO ) pGenFramebuffers( 1, &shadowBlurFBO );
	shadowBlurW = w; shadowBlurH = h;
}

// fullscreen NDC quad textured with tex, into the bound aux FBO/viewport
static void R_BlurDrawTex( GLuint tex )
{
	qglBindTexture( GL_TEXTURE_2D, tex );
	qglEnable( GL_TEXTURE_2D );
	qglMatrixMode( GL_PROJECTION ); qglPushMatrix(); qglLoadIdentity();
	qglMatrixMode( GL_MODELVIEW );  qglPushMatrix(); qglLoadIdentity();
	qglBegin( GL_QUADS );
		qglTexCoord2f( 0, 0 ); qglVertex3f( -1, -1, 0 );
		qglTexCoord2f( 1, 0 ); qglVertex3f(  1, -1, 0 );
		qglTexCoord2f( 1, 1 ); qglVertex3f(  1,  1, 0 );
		qglTexCoord2f( 0, 1 ); qglVertex3f( -1,  1, 0 );
	qglEnd();
	qglMatrixMode( GL_PROJECTION ); qglPopMatrix();
	qglMatrixMode( GL_MODELVIEW );  qglPopMatrix();
}

void RB_ShadowBlurFinish( void )
{
	int factor = r_shadowBlur->integer;
	if ( factor < 1 ) factor = 1;
	if ( factor > 64 ) factor = 64;

	// find the eye framebuffer + its depth-stencil texture (and its size)
	GLint eyeFbo = 0, dsTex = 0, w = 0, h = 0;
	qglGetIntegerv( GL_DRAW_FRAMEBUFFER_BINDING, &eyeFbo );
	pGetFramebufferAttachmentParameteriv( GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
		GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &dsTex );
	if ( !dsTex ) {	// no shared depth-stencil texture; bail (stencil already holds the mask, nothing else darkens)
		return;
	}
	qglBindTexture( GL_TEXTURE_2D, dsTex );
	qglGetTexLevelParameteriv( GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w );
	qglGetTexLevelParameteriv( GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h );
	if ( w < 2 || h < 2 ) {
		return;
	}

	R_ShadowBlurEnsureTargets( w, h );

	float total = r_shadowAlpha->value;
	if ( total < 0.0f ) total = 0.0f; else if ( total > 1.0f ) total = 1.0f;

	GLint savedVp[4];
	qglGetIntegerv( GL_VIEWPORT, savedVp );

	// 1) capture the eye colour
	qglBindTexture( GL_TEXTURE_2D, shadowSceneTex );
	qglCopyTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, w, h, 0 );

	// 2) draw the (alpha-scaled) mask where the stencil is set, into shadowMaskTex,
	//    sharing the eye's depth-stencil so the stencil test works
	pBindFramebuffer( GL_DRAW_FRAMEBUFFER, shadowBlurFBO );
	pFramebufferTexture2D( GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, shadowMaskTex, 0 );
	pFramebufferTexture2D( GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, dsTex, 0 );
	qglViewport( 0, 0, w, h );
	qglDisable( GL_TEXTURE_2D );
	qglClearColor( 0, 0, 0, 0 );
	qglClear( GL_COLOR_BUFFER_BIT );
	qglEnable( GL_STENCIL_TEST );
	qglStencilFunc( GL_NOTEQUAL, 0, 255 );
	qglStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );
	GL_State( GLS_DEPTHTEST_DISABLE );
	qglColor4f( total, total, total, total );
	qglMatrixMode( GL_PROJECTION ); qglPushMatrix(); qglLoadIdentity();
	qglMatrixMode( GL_MODELVIEW );  qglPushMatrix(); qglLoadIdentity();
	qglBegin( GL_QUADS );
		qglVertex3f( -1, -1, 0 ); qglVertex3f( 1, -1, 0 );
		qglVertex3f(  1,  1, 0 ); qglVertex3f( -1, 1, 0 );
	qglEnd();
	qglMatrixMode( GL_PROJECTION ); qglPopMatrix();
	qglMatrixMode( GL_MODELVIEW );  qglPopMatrix();
	qglDisable( GL_STENCIL_TEST );

	// detach the shared depth-stencil AND the mask (attaching the composite target
	// frees the mask so we can generate its mipmaps without a feedback loop)
	pFramebufferTexture2D( GL_DRAW_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0 );
	pFramebufferTexture2D( GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, shadowFinalTex, 0 );

	// 3) build the mask's mip chain (each level a true 2x2 average). We then sample a
	//    blurred level with trilinear filtering for a smooth, area-averaged, temporally
	//    stable penumbra - no undersampling jaggies like a single big downsample gave.
	qglBindTexture( GL_TEXTURE_2D, shadowMaskTex );
	pGenerateMipmap( GL_TEXTURE_2D );
	float lod = logf( (float)factor ) / logf( 2.0f );	// r_shadowBlur ~ penumbra width in px
	if ( lod < 0.0f ) lod = 0.0f;

	// 4) composite scene * (1 - blurredMask) into shadowFinalTex. The eye colour's
	//    ALPHA is the VR compositor's layer opacity, so it must stay = 1: clear alpha
	//    to 1 and mask alpha out of both passes (darken RGB only). Otherwise the blit
	//    writes scene*(1-mask) into alpha as well and the whole eye layer renders as a
	//    translucent veil with bright effects (bolts, glow, water) washed out.
	qglViewport( 0, 0, w, h );
	qglClearColor( 0, 0, 0, 1 );
	qglClear( GL_COLOR_BUFFER_BIT );
	qglColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE );
	qglColor4f( 1, 1, 1, 1 );
	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO | GLS_DEPTHTEST_DISABLE );	// opaque copy of the scene
	R_BlurDrawTex( shadowSceneTex );
	// darken by the blurred mask; pin the LOD so the penumbra width is uniform on screen
	qglBindTexture( GL_TEXTURE_2D, shadowMaskTex );
	qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD, lod );
	qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, lod );
	GL_State( GLS_SRCBLEND_ZERO | GLS_DSTBLEND_ONE_MINUS_SRC_COLOR | GLS_DEPTHTEST_DISABLE );	// darken by mask
	R_BlurDrawTex( shadowMaskTex );
	qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_LOD, -1000.0f );
	qglTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, 1000.0f );
	qglColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );

	// 5) blit the composited result back over the eye colour, then restore state
	pBindFramebuffer( GL_READ_FRAMEBUFFER, shadowBlurFBO );
	pBindFramebuffer( GL_DRAW_FRAMEBUFFER, eyeFbo );
	pBlitFramebuffer( 0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST );
	pBindFramebuffer( GL_FRAMEBUFFER, eyeFbo );	// restore both READ + DRAW to the eye fbo

	qglViewport( savedVp[0], savedVp[1], savedVp[2], savedVp[3] );
	qglColor4f( 1, 1, 1, 1 );
	GL_Bind( tr.whiteImage );	// resync the renderer's texture-state tracking

	// clear the stencil so the next frame's shadows start fresh
	qglStencilMask( 0xFF );
	qglClearStencil( 0 );
	qglClear( GL_STENCIL_BUFFER_BIT );
}

void RB_ShadowDarkenTap( int tap, int numTaps )
{
	if ( glConfig.stencilBits < 4 ) {
		return;
	}
	if ( r_shadows->integer != 2 && r_shadows->integer < 4 ) {
		return;
	}

	// Blur mode: a single solid mask is in the stencil now; soften it in screen space
	// instead of the flat solid darken.
	if ( R_ShadowBlurActive() ) {
		RB_ShadowBlurFinish();
		return;
	}

	float total = r_shadowAlpha->value;
	if ( total < 0.0f ) total = 0.0f;
	else if ( total > 1.0f ) total = 1.0f;
	if ( numTaps < 1 ) numTaps = 1;
	float a = ( numTaps <= 1 ) ? total : ( 1.0f - powf( 1.0f - total, 1.0f / (float)numTaps ) );

	qglEnable( GL_STENCIL_TEST );
	qglStencilFunc( GL_NOTEQUAL, 0, 255 );
	qglStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );

	bool planeZeroBack = false;
	if ( qglIsEnabled( GL_CLIP_PLANE0 ) ) {
		planeZeroBack = true;
		qglDisable( GL_CLIP_PLANE0 );
	}
	GL_Cull( CT_TWO_SIDED );
	GL_Bind( tr.whiteImage );

	// Eye-space darkening quad through the live (VR) projection - see RB_ShadowFinish.
	qglMatrixMode( GL_MODELVIEW );
	qglPushMatrix();
	qglLoadIdentity();

	qglColor4f( 0.0f, 0.0f, 0.0f, a );
	GL_State( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHTEST_DISABLE );

	qglBegin( GL_QUADS );
	qglVertex3f( -200.0f,  200.0f, -10.0f );
	qglVertex3f(  200.0f,  200.0f, -10.0f );
	qglVertex3f(  200.0f, -200.0f, -10.0f );
	qglVertex3f( -200.0f, -200.0f, -10.0f );
	qglEnd ();

	qglColor4f( 1, 1, 1, 1 );
	qglMatrixMode( GL_MODELVIEW );
	qglPopMatrix();

	// clear the stencil so the next solid layer accumulates on its own
	qglStencilMask( 0xFF );
	qglClearStencil( 0 );
	qglClear( GL_STENCIL_BUFFER_BIT );

	qglDisable( GL_STENCIL_TEST );
	if ( planeZeroBack ) {
		qglEnable( GL_CLIP_PLANE0 );
	}
}


/*
=================
RB_ProjectionShadowDeform

=================
*/
void RB_ProjectionShadowDeform( void ) {
	float	*xyz;
	int		i;
	float	h;
	vec3_t	ground;
	vec3_t	light;
	float	groundDist;
	float	d;
	vec3_t	lightDir;

	xyz = ( float * ) tess.xyz;

	ground[0] = backEnd.ori.axis[0][2];
	ground[1] = backEnd.ori.axis[1][2];
	ground[2] = backEnd.ori.axis[2][2];

	groundDist = backEnd.ori.origin[2] - backEnd.currentEntity->e.shadowPlane;

	VectorCopy( backEnd.currentEntity->lightDir, lightDir );
	d = DotProduct( lightDir, ground );
	// don't let the shadows get too long or go negative
	if ( d < 0.5 ) {
		VectorMA( lightDir, (0.5 - d), ground, lightDir );
		d = DotProduct( lightDir, ground );
	}
	d = 1.0 / d;

	light[0] = lightDir[0] * d;
	light[1] = lightDir[1] * d;
	light[2] = lightDir[2] * d;

	for ( i = 0; i < tess.numVertexes; i++, xyz += 4 ) {
		h = DotProduct( xyz, ground ) + groundDist;

		xyz[0] -= light[0] * h;
		xyz[1] -= light[1] * h;
		xyz[2] -= light[2] * h;
	}
}

//update tr.screenImage
void RB_CaptureScreenImage(void)
{
	int radX = 2048;
	int radY = 2048;
	int x = glConfig.vidWidth/2;
	int y = glConfig.vidHeight/2;
	int cX, cY;

	GL_Bind( tr.screenImage );
	//using this method, we could pixel-filter the texture and all sorts of crazy stuff.
	//but, it is slow as hell.
	/*
	static byte *tmp = NULL;
	if (!tmp)
	{
		tmp = (byte *)R_Malloc((sizeof(byte)*4)*(glConfig.vidWidth*glConfig.vidHeight), TAG_ICARUS, qtrue);
	}
	qglReadPixels(0, 0, glConfig.vidWidth, glConfig.vidHeight, GL_RGBA, GL_UNSIGNED_BYTE, tmp);
	qglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 512, 512, 0, GL_RGBA, GL_UNSIGNED_BYTE, tmp);
	*/

	if (radX > glConfig.maxTextureSize)
	{
		radX = glConfig.maxTextureSize;
	}
	if (radY > glConfig.maxTextureSize)
	{
		radY = glConfig.maxTextureSize;
	}

	while (glConfig.vidWidth < radX)
	{
		radX /= 2;
	}
	while (glConfig.vidHeight < radY)
	{
		radY /= 2;
	}

	cX = x-(radX/2);
	cY = y-(radY/2);

	if (cX+radX > glConfig.vidWidth)
	{ //would it go off screen?
		cX = glConfig.vidWidth-radX;
	}
	else if (cX < 0)
	{ //cap it off at 0
		cX = 0;
	}

	if (cY+radY > glConfig.vidHeight)
	{ //would it go off screen?
		cY = glConfig.vidHeight-radY;
	}
	else if (cY < 0)
	{ //cap it off at 0
		cY = 0;
	}

	qglCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16, cX, cY, radX, radY, 0);
}


//yeah.. not really shadow-related.. but it's stencil-related. -rww
float tr_distortionAlpha = 1.0f; //opaque
float tr_distortionStretch = 0.0f; //no stretch override
qboolean tr_distortionPrePost = qfalse; //capture before postrender phase?
qboolean tr_distortionNegate = qfalse; //negative blend mode
void RB_DistortionFill(void)
{
	float alpha = tr_distortionAlpha;
	float spost = 0.0f;
	float spost2 = 0.0f;

	if ( glConfig.stencilBits < 4 )
	{
		return;
	}

	//ok, cap the stupid thing now I guess
	if (!tr_distortionPrePost)
	{
		RB_CaptureScreenImage();
	}

	qglEnable(GL_STENCIL_TEST);
	qglStencilFunc(GL_NOTEQUAL, 0, 0xFFFFFFFF);
	qglStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

	qglDisable (GL_CLIP_PLANE0);
	GL_Cull( CT_TWO_SIDED );

	//reset the view matrices and go into ortho mode
	qglMatrixMode(GL_PROJECTION);
	qglPushMatrix();
	qglLoadIdentity();
	qglOrtho(0, glConfig.vidWidth, glConfig.vidHeight, 32, -1, 1);
	qglMatrixMode(GL_MODELVIEW);
	qglPushMatrix();
	qglLoadIdentity();

	if (tr_distortionStretch)
	{ //override
		spost = tr_distortionStretch;
		spost2 = tr_distortionStretch;
	}
	else
	{ //do slow stretchy effect
		spost = sin(tr.refdef.time*0.0005f);
		if (spost < 0.0f)
		{
			spost = -spost;
		}
		spost *= 0.2f;

		spost2 = sin(tr.refdef.time*0.0005f);
		if (spost2 < 0.0f)
		{
			spost2 = -spost2;
		}
		spost2 *= 0.08f;
	}

	if (alpha != 1.0f)
	{ //blend
		GL_State(GLS_SRCBLEND_SRC_ALPHA|GLS_DSTBLEND_SRC_ALPHA);
	}
	else
	{ //be sure to reset the draw state
		GL_State(0);
	}

	qglBegin(GL_QUADS);
		qglColor4f(1.0f, 1.0f, 1.0f, alpha);
		qglTexCoord2f(0+spost2, 1-spost);
		qglVertex2f(0, 0);

		qglTexCoord2f(0+spost2, 0+spost);
		qglVertex2f(0, glConfig.vidHeight);

		qglTexCoord2f(1-spost2, 0+spost);
		qglVertex2f(glConfig.vidWidth, glConfig.vidHeight);

		qglTexCoord2f(1-spost2, 1-spost);
		qglVertex2f(glConfig.vidWidth, 0);
	qglEnd();

	if (tr_distortionAlpha == 1.0f && tr_distortionStretch == 0.0f)
	{ //no overrides
		if (tr_distortionNegate)
		{ //probably the crazy alternate saber trail
			alpha = 0.8f;
			GL_State(GLS_SRCBLEND_ZERO|GLS_DSTBLEND_ONE_MINUS_SRC_COLOR);
		}
		else
		{
			alpha = 0.5f;
			GL_State(GLS_SRCBLEND_SRC_ALPHA|GLS_DSTBLEND_SRC_ALPHA);
		}

		spost = sin(tr.refdef.time*0.0008f);
		if (spost < 0.0f)
		{
			spost = -spost;
		}
		spost *= 0.08f;

		spost2 = sin(tr.refdef.time*0.0008f);
		if (spost2 < 0.0f)
		{
			spost2 = -spost2;
		}
		spost2 *= 0.2f;

		qglBegin(GL_QUADS);
			qglColor4f(1.0f, 1.0f, 1.0f, alpha);
			qglTexCoord2f(0+spost2, 1-spost);
			qglVertex2f(0, 0);

			qglTexCoord2f(0+spost2, 0+spost);
			qglVertex2f(0, glConfig.vidHeight);

			qglTexCoord2f(1-spost2, 0+spost);
			qglVertex2f(glConfig.vidWidth, glConfig.vidHeight);

			qglTexCoord2f(1-spost2, 1-spost);
			qglVertex2f(glConfig.vidWidth, 0);
		qglEnd();
	}

	//pop the view matrices back
	qglMatrixMode(GL_PROJECTION);
	qglPopMatrix();
	qglMatrixMode(GL_MODELVIEW);
	qglPopMatrix();

	qglDisable( GL_STENCIL_TEST );
}
