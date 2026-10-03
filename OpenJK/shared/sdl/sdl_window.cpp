/*
===========================================================================
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

#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include "qcommon/qcommon.h"
#include "rd-common/tr_types.h"
#include "sys/sys_local.h"
#include "sdl_icon.h"

enum rserr_t
{
	RSERR_OK,

	RSERR_INVALID_FULLSCREEN,
	RSERR_INVALID_MODE,

	RSERR_UNKNOWN
};

static SDL_Window *screen = NULL;
static VkInstance vulkanInstance = VK_NULL_HANDLE;
static VkSurfaceKHR vulkanSurface = VK_NULL_HANDLE;
static float displayAspect;

cvar_t *r_sdlDriver;

// Window cvars
cvar_t	*r_fullscreen = 0;
cvar_t	*r_noborder;
cvar_t	*r_centerWindow;
cvar_t	*r_customwidth;
cvar_t	*r_customheight;
cvar_t	*r_swapInterval;
cvar_t	*r_stereo;
cvar_t	*r_mode;
cvar_t	*r_displayRefresh;

// Window surface cvars
cvar_t	*r_stencilbits;
cvar_t	*r_depthbits;
cvar_t	*r_colorbits;
cvar_t	*r_ignorehwgamma;
cvar_t  *r_ext_multisample;

/*
** R_GetModeInfo
*/
typedef struct vidmode_s
{
    const char *description;
    int         width, height;
} vidmode_t;

const vidmode_t r_vidModes[] = {
    { "Mode  0: 320x240",		320,	240 },
    { "Mode  1: 400x300",		400,	300 },
    { "Mode  2: 512x384",		512,	384 },
    { "Mode  3: 640x480",		640,	480 },
    { "Mode  4: 800x600",		800,	600 },
    { "Mode  5: 960x720",		960,	720 },
    { "Mode  6: 1024x768",		1024,	768 },
    { "Mode  7: 1152x864",		1152,	864 },
    { "Mode  8: 1280x1024",		1280,	1024 },
    { "Mode  9: 1600x1200",		1600,	1200 },
    { "Mode 10: 2048x1536",		2048,	1536 },
    { "Mode 11: 856x480 (wide)", 856,	 480 },
    { "Mode 12: 2400x600(surround)",2400,600 }
};
static const int	s_numVidModes = ARRAY_LEN( r_vidModes );

#define R_MODE_FALLBACK (4) // 640x480

qboolean R_GetModeInfo( int *width, int *height, int mode ) {
	const vidmode_t	*vm;

    if ( mode < -1 ) {
        return qfalse;
	}
	if ( mode >= s_numVidModes ) {
		return qfalse;
	}

	if ( mode == -1 ) {
		*width = r_customwidth->integer;
		*height = r_customheight->integer;
		return qtrue;
	}

	vm = &r_vidModes[mode];

    *width  = vm->width;
    *height = vm->height;

    return qtrue;
}

/*
** R_ModeList_f
*/
static void R_ModeList_f( void )
{
	int i;

	Com_Printf( "\n" );
	Com_Printf( "Mode -2: Use desktop resolution\n" );
	Com_Printf( "Mode -1: Use r_customWidth and r_customHeight variables\n" );
	for ( i = 0; i < s_numVidModes; i++ )
	{
		Com_Printf( "%s\n", r_vidModes[i].description );
	}
	Com_Printf( "\n" );
}

/*
===============
WIN_Minimize

Minimize the game so that user is back at the desktop
===============
*/
void WIN_Minimize(void)
{
	SDL_MinimizeWindow( screen );
}


static void WIN_DestroyVulkanSurface()
{
	if ( vulkanSurface != VK_NULL_HANDLE && vulkanInstance != VK_NULL_HANDLE )
	{
		SDL_Vulkan_DestroySurface( vulkanInstance, vulkanSurface, NULL );
	}

	vulkanSurface = VK_NULL_HANDLE;
	vulkanInstance = VK_NULL_HANDLE;
}

void WIN_Present( window_t *window )
{
	if ( r_fullscreen->modified )
	{
		bool	fullscreen;
		bool	needToToggle;
		bool	sdlToggled = qfalse;

		// Find out the current state
		fullscreen = (SDL_GetWindowFlags( screen ) & SDL_WINDOW_FULLSCREEN) != 0;

		if ( r_fullscreen->integer && Cvar_VariableIntegerValue( "in_nograb" ) )
		{
			Com_Printf( "Fullscreen not allowed with in_nograb 1\n" );
			Cvar_Set( "r_fullscreen", "0" );
			r_fullscreen->modified = qfalse;
		}

		// Is the state we want different from the current state?
		needToToggle = !!r_fullscreen->integer != fullscreen;

		if ( needToToggle )
		{
			sdlToggled = SDL_SetWindowFullscreen( screen, r_fullscreen->integer != 0 );

			// SDL_WM_ToggleFullScreen didn't work, so do it the slow way
			if ( !sdlToggled )
				Cbuf_AddText( "vid_restart\n" );

			IN_Restart();
		}

		r_fullscreen->modified = qfalse;
	}
}

/*
===============
WIN_CompareModes
===============
*/
static int WIN_CompareModes( const void *a, const void *b )
{
	const float ASPECT_EPSILON = 0.001f;
	SDL_Rect *modeA = (SDL_Rect *)a;
	SDL_Rect *modeB = (SDL_Rect *)b;
	float aspectA = (float)modeA->w / (float)modeA->h;
	float aspectB = (float)modeB->w / (float)modeB->h;
	int areaA = modeA->w * modeA->h;
	int areaB = modeB->w * modeB->h;
	float aspectDiffA = fabs( aspectA - displayAspect );
	float aspectDiffB = fabs( aspectB - displayAspect );
	float aspectDiffsDiff = aspectDiffA - aspectDiffB;

	if( aspectDiffsDiff > ASPECT_EPSILON )
		return 1;
	else if( aspectDiffsDiff < -ASPECT_EPSILON )
		return -1;
	else
		return areaA - areaB;
}

/*
===============
WIN_DetectAvailableModes
===============
*/
static bool WIN_DetectAvailableModes(void)
{
	int i, j;
	char buf[ MAX_STRING_CHARS ] = { 0 };
	SDL_Rect *modes;
	int numModes = 0;

	SDL_DisplayID display = SDL_GetDisplayForWindow( screen );
	if ( display == 0 )
	{
		Com_Printf( S_COLOR_YELLOW "WARNING: Couldn't get window display index, no resolutions detected: %s\n", SDL_GetError() );
		return false;
	}

	// SDL3 dropped SDL_GetWindowDisplayMode(); use the desktop mode's pixel
	// format (what the window actually renders at) to filter candidate modes.
	const SDL_DisplayMode *desktopMode = SDL_GetDesktopDisplayMode( display );
	if( desktopMode == NULL )
	{
		Com_Printf( S_COLOR_YELLOW "WARNING: Couldn't get desktop display mode, no resolutions detected (%s).\n", SDL_GetError() );
		return false;
	}

	int numDisplayModes = 0;
	SDL_DisplayMode **displayModes = SDL_GetFullscreenDisplayModes( display, &numDisplayModes );
	if ( displayModes == NULL )
		Com_Error( ERR_FATAL, "SDL_GetFullscreenDisplayModes() FAILED (%s)", SDL_GetError() );

	modes = (SDL_Rect *)SDL_calloc( (size_t)numDisplayModes, sizeof( SDL_Rect ) );
	if ( !modes )
		Com_Error( ERR_FATAL, "Out of memory" );

	for( i = 0; i < numDisplayModes; i++ )
	{
		const SDL_DisplayMode *mode = displayModes[ i ];

		if( !mode->w || !mode->h )
		{
			Com_Printf( "Display supports any resolution\n" );
			SDL_free( modes );
			SDL_free( displayModes );
			return true;
		}

		if( desktopMode->format != mode->format )
			continue;

		// SDL can give the same resolution with different refresh rates.
		// Only list resolution once.
		for( j = 0; j < numModes; j++ )
		{
			if( mode->w == modes[ j ].w && mode->h == modes[ j ].h )
				break;
		}

		if( j != numModes )
			continue;

		modes[ numModes ].w = mode->w;
		modes[ numModes ].h = mode->h;
		numModes++;
	}

	SDL_free( displayModes );

	if( numModes > 1 )
		qsort( modes, numModes, sizeof( SDL_Rect ), WIN_CompareModes );

	for( i = 0; i < numModes; i++ )
	{
		const char *newModeString = va( "%ux%u ", modes[ i ].w, modes[ i ].h );

		if( strlen( newModeString ) < (int)sizeof( buf ) - strlen( buf ) )
			Q_strcat( buf, sizeof( buf ), newModeString );
		else
			Com_Printf( "Skipping mode %ux%u, buffer too small\n", modes[ i ].w, modes[ i ].h );
	}

	if( *buf )
	{
		buf[ strlen( buf ) - 1 ] = 0;
		Com_Printf( "Available modes: '%s'\n", buf );
		Cvar_Set( "r_availableModes", buf );
	}

	SDL_free( modes );
	return true;
}

/*
===============
WIN_SetMode
===============
*/

static rserr_t WIN_SetMode(glconfig_t *glConfig, const windowDesc_t *windowDesc, const char *windowTitle, int mode, qboolean fullscreen, qboolean noborder)
{
	SDL_Surface *icon = NULL;
	// SDL3 windows are shown by default (SDL_WINDOW_SHOWN was removed; use
	// SDL_WINDOW_HIDDEN to start hidden). 0 == shown.
	Uint64 flags = 0;
	SDL_DisplayID display = 0;
	const SDL_DisplayMode *desktopMode = NULL;
	int x = SDL_WINDOWPOS_UNDEFINED, y = SDL_WINDOWPOS_UNDEFINED;

	if ( windowDesc->api == GRAPHICS_API_VULKAN )
	{
		flags |= SDL_WINDOW_VULKAN;
	}

	Com_Printf( "Initializing display\n");

#ifdef Q3_LITTLE_ENDIAN
	const Uint32 iconRmask = 0x000000FF, iconGmask = 0x0000FF00, iconBmask = 0x00FF0000, iconAmask = 0xFF000000;
#else
	const Uint32 iconRmask = 0xFF000000, iconGmask = 0x00FF0000, iconBmask = 0x0000FF00, iconAmask = 0x000000FF;
#endif

	icon = SDL_CreateSurfaceFrom(
		CLIENT_WINDOW_ICON.width,
		CLIENT_WINDOW_ICON.height,
		SDL_GetPixelFormatForMasks( CLIENT_WINDOW_ICON.bytes_per_pixel * 8,
			iconRmask, iconGmask, iconBmask, iconAmask ),
		(void *)CLIENT_WINDOW_ICON.pixel_data,
		CLIENT_WINDOW_ICON.bytes_per_pixel * CLIENT_WINDOW_ICON.width );

	// If a window exists, note its display index
	if ( screen != NULL )
	{
		display = SDL_GetDisplayForWindow( screen );
		if ( display == 0 )
		{
			Com_DPrintf( "SDL_GetDisplayForWindow() failed: %s\n", SDL_GetError() );
		}
	}

	if( display != 0 && ( desktopMode = SDL_GetDesktopDisplayMode( display ) ) != NULL )
	{
		displayAspect = (float)desktopMode->w / (float)desktopMode->h;

		Com_Printf( "Display aspect: %.3f\n", displayAspect );
	}
	else
	{
		Com_Printf( "Cannot determine display aspect, assuming 1.333\n" );
	}

	Com_Printf( "...setting mode %d:", mode );

	if (mode == -2)
	{
		// use desktop video resolution
		if( desktopMode != NULL && desktopMode->h > 0 )
		{
			glConfig->vidWidth = desktopMode->w;
			glConfig->vidHeight = desktopMode->h;
		}
		else
		{
			glConfig->vidWidth = 640;
			glConfig->vidHeight = 480;
			Com_Printf( "Cannot determine display resolution, assuming 640x480\n" );
		}

		//glConfig.windowAspect = (float)glConfig.vidWidth / (float)glConfig.vidHeight;
	}
	else if ( !R_GetModeInfo( &glConfig->vidWidth, &glConfig->vidHeight, /*&glConfig.windowAspect,*/ mode ) )
	{
		Com_Printf( " invalid mode\n" );
		SDL_DestroySurface( icon );
		return RSERR_INVALID_MODE;
	}

	Com_Printf( " %d %d\n", glConfig->vidWidth, glConfig->vidHeight);

	// Center window
	if( r_centerWindow->integer && !fullscreen && desktopMode != NULL )
	{
		x = ( desktopMode->w / 2 ) - ( glConfig->vidWidth / 2 );
		y = ( desktopMode->h / 2 ) - ( glConfig->vidHeight / 2 );
	}

	// Destroy existing state if it exists
	if( screen != NULL )
	{
		WIN_DestroyVulkanSurface();
		SDL_GetWindowPosition( screen, &x, &y );
		Com_DPrintf( "Existing window at %dx%d before being destroyed\n", x, y );
		SDL_DestroyWindow( screen );
		screen = NULL;
	}

	if( fullscreen )
	{
		flags |= SDL_WINDOW_FULLSCREEN;
		glConfig->isFullscreen = qtrue;
	}
	else
	{
		if( noborder )
			flags |= SDL_WINDOW_BORDERLESS;

		glConfig->isFullscreen = qfalse;
	}

	{
		// Just create a regular window
		if( ( screen = SDL_CreateWindow( windowTitle,
				glConfig->vidWidth, glConfig->vidHeight, flags ) ) == NULL )
		{
			Com_Printf( "SDL_CreateWindow failed: %s\n", SDL_GetError( ) );
			SDL_DestroySurface( icon );
			return RSERR_UNKNOWN;
		}
		else
		{
			SDL_SetWindowPosition( screen, x, y );
			SDL_SetWindowFocusable( screen, true );
			SDL_ShowWindow( screen );
			SDL_RaiseWindow( screen );
			SDL_SyncWindow( screen );

#ifndef MACOS_X
			SDL_SetWindowIcon( screen, icon );
#endif
			if( fullscreen )
			{
				if( !SDL_SetWindowFullscreenMode( screen, NULL ) )
				{
					Com_DPrintf( "SDL_SetWindowFullscreenMode failed: %s\n", SDL_GetError( ) );
				}
			}

			if ( windowDesc->api == GRAPHICS_API_VULKAN )
			{
				vulkanInstance = reinterpret_cast<VkInstance>( windowDesc->vk.instance );
				if ( vulkanInstance != VK_NULL_HANDLE )
				{
					if ( !SDL_Vulkan_CreateSurface( screen, vulkanInstance, NULL, &vulkanSurface ) )
					{
						Com_Printf( "SDL_Vulkan_CreateSurface failed: %s\n", SDL_GetError() );
						WIN_DestroyVulkanSurface();
						SDL_DestroySurface( icon );
						return RSERR_UNKNOWN;
					}
				}
				else
				{
					Com_DPrintf( "Vulkan window requested without VkInstance; no SDL surface was created.\n" );
				}
			}
		}
	}

	SDL_DestroySurface( icon );

	if (!WIN_DetectAvailableModes())
	{
		return RSERR_UNKNOWN;
	}

	return RSERR_OK;
}

/*
===============
WIN_StartDriverAndSetMode
===============
*/
static qboolean WIN_StartDriverAndSetMode(glconfig_t *glConfig, const windowDesc_t *windowDesc, int mode, qboolean fullscreen, qboolean noborder)
{
	rserr_t err;

	if (!SDL_WasInit(SDL_INIT_VIDEO))
	{
		const char *driverName;

		if (!SDL_Init(SDL_INIT_VIDEO))
		{
			Com_Printf( "SDL_Init( SDL_INIT_VIDEO ) FAILED (%s)\n", SDL_GetError());
			return qfalse;
		}

		driverName = SDL_GetCurrentVideoDriver();

		if (!driverName)
		{
			Com_Error( ERR_FATAL, "No video driver initialized" );
			return qfalse;
		}

		Com_Printf( "SDL using driver \"%s\"\n", driverName );
		Cvar_Set( "r_sdlDriver", driverName );
	}

	int numDisplays = 0;
	SDL_DisplayID *displays = SDL_GetDisplays( &numDisplays );
	if ( displays == NULL || numDisplays <= 0 )
	{
		SDL_free( displays );
		Com_Error( ERR_FATAL, "SDL_GetDisplays() FAILED (%s)", SDL_GetError() );
	}
	SDL_free( displays );

	if (fullscreen && Cvar_VariableIntegerValue( "in_nograb" ) )
	{
		Com_Printf( "Fullscreen not allowed with in_nograb 1\n");
		Cvar_Set( "r_fullscreen", "0" );
		r_fullscreen->modified = qfalse;
		fullscreen = qfalse;
	}

	err = WIN_SetMode(glConfig, windowDesc, CLIENT_WINDOW_TITLE, mode, fullscreen, noborder);

	switch ( err )
	{
		case RSERR_INVALID_FULLSCREEN:
			Com_Printf( "...WARNING: fullscreen unavailable in this mode\n" );
			return qfalse;
		case RSERR_INVALID_MODE:
			Com_Printf( "...WARNING: could not set the given mode (%d)\n", mode );
			return qfalse;
		case RSERR_UNKNOWN:
			Com_Printf( "...ERROR: no display modes could be found.\n" );
			return qfalse;
		default:
			break;
	}

	return qtrue;
}


window_t WIN_Init( const windowDesc_t *windowDesc, glconfig_t *glConfig )
{
	Cmd_AddCommand("modelist", R_ModeList_f);
	Cmd_AddCommand("minimize", WIN_Minimize);

	r_sdlDriver			= Cvar_Get( "r_sdlDriver",			"",			CVAR_ROM );

	// Window cvars
	r_fullscreen		= Cvar_Get( "r_fullscreen",			"0",		CVAR_ARCHIVE|CVAR_LATCH );
	r_noborder			= Cvar_Get( "r_noborder",			"0",		CVAR_ARCHIVE|CVAR_LATCH );
	r_centerWindow		= Cvar_Get( "r_centerWindow",		"0",		CVAR_ARCHIVE|CVAR_LATCH );
	r_customwidth		= Cvar_Get( "r_customwidth",		"1600",		CVAR_ARCHIVE|CVAR_LATCH );
	r_customheight		= Cvar_Get( "r_customheight",		"1024",		CVAR_ARCHIVE|CVAR_LATCH );
	r_swapInterval		= Cvar_Get( "r_swapInterval",		"0",		CVAR_ARCHIVE_ND );
	r_stereo			= Cvar_Get( "r_stereo",				"0",		CVAR_ARCHIVE_ND|CVAR_LATCH );
	r_mode				= Cvar_Get( "r_mode",				"4",		CVAR_ARCHIVE|CVAR_LATCH );
	r_displayRefresh	= Cvar_Get( "r_displayRefresh",		"0",		CVAR_LATCH );
	Cvar_CheckRange( r_displayRefresh, 0, 240, qtrue );

	// Window render surface cvars
	r_stencilbits		= Cvar_Get( "r_stencilbits",		"8",		CVAR_ARCHIVE_ND|CVAR_LATCH );
	r_depthbits			= Cvar_Get( "r_depthbits",			"0",		CVAR_ARCHIVE_ND|CVAR_LATCH );
	r_colorbits			= Cvar_Get( "r_colorbits",			"0",		CVAR_ARCHIVE_ND|CVAR_LATCH );
	r_ignorehwgamma		= Cvar_Get( "r_ignorehwgamma",		"0",		CVAR_ARCHIVE_ND|CVAR_LATCH );
	r_ext_multisample	= Cvar_Get( "r_ext_multisample",	"0",		CVAR_ARCHIVE_ND|CVAR_LATCH );
	Cvar_Get( "r_availableModes", "", CVAR_ROM );

	// Create the window and set up the context
	if(!WIN_StartDriverAndSetMode( glConfig, windowDesc, r_mode->integer,
										(qboolean)r_fullscreen->integer, (qboolean)r_noborder->integer ))
	{
		if( r_mode->integer != R_MODE_FALLBACK )
		{
			Com_Printf( "Setting r_mode %d failed, falling back on r_mode %d\n", r_mode->integer, R_MODE_FALLBACK );

			if (!WIN_StartDriverAndSetMode( glConfig, windowDesc, R_MODE_FALLBACK, qfalse, qfalse ))
			{
				// Nothing worked, give up
				Com_Error( ERR_FATAL, "WIN_Init() - could not create SDL3 window" );
			}
		}
		else
		{
			Com_Error( ERR_FATAL, "WIN_Init() - could not create SDL3 window" );
		}
	}

	// SDL3 removed SDL_SetWindowBrightness()/SDL_SetWindowGammaRamp(); per-window
	// gamma-ramp control is no longer available, so report no gamma support
	// (WIN_SetGamma becomes a no-op as a result).
	glConfig->deviceSupportsGamma = qfalse;

	// This depends on SDL_INIT_VIDEO, hence having it here
	IN_Init( screen );

	// window_t is only really useful for Windows if the renderer wants to create a D3D context.
	window_t window = {};

	window.api = windowDesc->api;
	window.handle = screen;
	window.vulkanSurface = reinterpret_cast<void *>( vulkanSurface );


	return window;
}

/*
===============
WIN_Shutdown
===============
*/
void WIN_Shutdown( void )
{
	Cmd_RemoveCommand("modelist");
	Cmd_RemoveCommand("minimize");

	IN_Shutdown();

	WIN_DestroyVulkanSurface();

	SDL_DestroyWindow( screen );
	SDL_QuitSubSystem( SDL_INIT_VIDEO );
	screen = NULL;
}

void WIN_SetGamma( glconfig_t *glConfig, byte red[256], byte green[256], byte blue[256] )
{
	// SDL3 removed SDL_SetWindowBrightness()/SDL_SetWindowGammaRamp(); per-window
	// gamma-ramp control is no longer supported. deviceSupportsGamma is always
	// qfalse (see WIN_Init), so callers never rely on this doing anything.
	// Kept as a no-op to preserve the interface declared in sys_public.h.
	(void)glConfig; (void)red; (void)green; (void)blue;
}
