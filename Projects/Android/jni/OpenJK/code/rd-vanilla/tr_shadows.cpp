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

// TODO (soft-shadow performance, future work):
//  * Screen-space blur: render the shadow mask once (hard stencil), copy it to a
//    texture (RB_CaptureScreenImage already exists), blur it and composite once,
//    instead of accumulating N jittered volume passes. Decouples cost from softness.
//  * Shadow mapping + PCF: the modern approach (one depth pass from the light,
//    percentage-closer filtered) - cheaper and self-shadowing, but needs a
//    programmable (GLSL) pipeline this fixed-function renderer does not have.
// Number of solid shadow layers (taps) for the active stencil mode. Modes 2 and 4
// are hard = 1 layer; mode 5 (Ultra) stacks r_shadowSoft jittered solid layers.
int R_NumShadowTaps( void )
{
	if ( r_shadows->integer != 5 ) {
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
void RB_ShadowDarkenTap( int tap, int numTaps )
{
	if ( glConfig.stencilBits < 4 ) {
		return;
	}
	if ( r_shadows->integer != 2 && r_shadows->integer < 4 ) {
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
