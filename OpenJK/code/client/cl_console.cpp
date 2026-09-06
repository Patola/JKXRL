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

// console.c

#include "server/exe_headers.h"

#include "client.h"
#include "qcommon/stringed_ingame.h"
#include "qcommon/stv_version.h"

#include <VrCommon.h>

void Sys_QueEvent(
	int time, sysEventType_t type, int value, int value2,
	int ptrLength, void *ptr );

int g_console_field_width = 78;

console_t	con;

cvar_t		*con_conspeed;
cvar_t		*con_notifytime;
cvar_t		*con_drawnotify;
cvar_t		*con_opacity; // background alpha multiplier
cvar_t		*con_autoclear;

#define	DEFAULT_CONSOLE_WIDTH	78

#define VR_CONSOLE_COLUMNS 120
#define VR_CONSOLE_CHAR_WIDTH 4
#define VR_CONSOLE_CHAR_HEIGHT 8
#define VR_CONSOLE_LEFT 56
#define VR_CONSOLE_TOP 24
#define VR_CONSOLE_WIDTH 528
#define VR_CONSOLE_HEIGHT 432
#define VR_CONSOLE_TEXT_LEFT 72
#define VR_CONSOLE_OUTPUT_TOP 64
#define VR_CONSOLE_OUTPUT_BOTTOM 376
#define VR_CONSOLE_INPUT_Y 404

vec4_t	console_color = {0.509f, 0.609f, 0.847f, 1.0f};

static qboolean Con_UseVrLayout()
{
	const char *renderer = Cvar_VariableString( "cl_renderer" );
	const qboolean jkaVulkan = ( !Q_stricmpn( renderer, "rdsp-vulkan", 11 ) &&
		( renderer[11] == '\0' || renderer[11] == '-' ) ) ? qtrue : qfalse;
	const qboolean jkoVulkan = ( !Q_stricmpn( renderer, "rdjosp-vulkan", 13 ) &&
		( renderer[13] == '\0' || renderer[13] == '-' ) ) ? qtrue : qfalse;
	return ( jkaVulkan || jkoVulkan ) ? qtrue : qfalse;
}

enum vrConsolePhase_t
{
	VR_CONSOLE_CLOSED,
	VR_CONSOLE_OPENING,
	VR_CONSOLE_OPEN,
	VR_CONSOLE_CLOSING,
};

enum vrConsoleKeyType_t
{
	VR_CONSOLE_KEY_CHARACTER,
	VR_CONSOLE_KEY_CONTROL,
	VR_CONSOLE_KEY_SHIFT,
	VR_CONSOLE_KEY_CAPS,
	VR_CONSOLE_KEY_SPACER,
};

struct vrConsoleKey_t
{
	const char *label;
	int normalCharacter;
	int shiftedCharacter;
	int keyCode;
	float widthUnits;
	vrConsoleKeyType_t type;
	qboolean repeat;
};

struct vrConsoleKeyRow_t
{
	const vrConsoleKey_t *keys;
	int count;
};

struct vrConsolePointer_t
{
	qboolean valid;
	float x;
	float y;
	float distance;
};

#define VR_CHAR_KEY(label, normal, shifted) \
	{ label, normal, shifted, A_NULL, 1.0f, VR_CONSOLE_KEY_CHARACTER, qtrue }
#define VR_WIDE_CHAR_KEY(label, normal, shifted, width) \
	{ label, normal, shifted, A_NULL, width, VR_CONSOLE_KEY_CHARACTER, qtrue }
#define VR_CONTROL_KEY(label, key, width, repeats) \
	{ label, 0, 0, key, width, VR_CONSOLE_KEY_CONTROL, repeats }
#define VR_SPACER(width) \
	{ "", 0, 0, A_NULL, width, VR_CONSOLE_KEY_SPACER, qfalse }

static const vrConsoleKey_t vrConsoleNumberKeys[] = {
	VR_CONTROL_KEY( "ESC", A_ESCAPE, 1.4f, qfalse ),
	VR_CHAR_KEY( "` ~", '`', '~' ),
	VR_CHAR_KEY( "1 !", '1', '!' ),
	VR_CHAR_KEY( "2 @", '2', '@' ),
	VR_CHAR_KEY( "3 #", '3', '#' ),
	VR_CHAR_KEY( "4 $", '4', '$' ),
	VR_CHAR_KEY( "5 %", '5', '%' ),
	VR_CHAR_KEY( "6 ^", '6', '^' ),
	VR_CHAR_KEY( "7 &", '7', '&' ),
	VR_CHAR_KEY( "8 *", '8', '*' ),
	VR_CHAR_KEY( "9 (", '9', '(' ),
	VR_CHAR_KEY( "0 )", '0', ')' ),
	VR_CHAR_KEY( "- _", '-', '_' ),
	VR_CHAR_KEY( "= +", '=', '+' ),
	VR_CONTROL_KEY( "BKSP", A_BACKSPACE, 1.8f, qtrue ),
};

static const vrConsoleKey_t vrConsoleTopKeys[] = {
	VR_CONTROL_KEY( "TAB", A_TAB, 1.5f, qfalse ),
	VR_CHAR_KEY( "Q", 'q', 'Q' ), VR_CHAR_KEY( "W", 'w', 'W' ),
	VR_CHAR_KEY( "E", 'e', 'E' ), VR_CHAR_KEY( "R", 'r', 'R' ),
	VR_CHAR_KEY( "T", 't', 'T' ), VR_CHAR_KEY( "Y", 'y', 'Y' ),
	VR_CHAR_KEY( "U", 'u', 'U' ), VR_CHAR_KEY( "I", 'i', 'I' ),
	VR_CHAR_KEY( "O", 'o', 'O' ), VR_CHAR_KEY( "P", 'p', 'P' ),
	VR_CHAR_KEY( "[ {", '[', '{' ), VR_CHAR_KEY( "] }", ']', '}' ),
	VR_CHAR_KEY( "\\ |", '\\', '|' ),
};

static const vrConsoleKey_t vrConsoleHomeKeys[] = {
	{ "CAPS", 0, 0, A_CAPSLOCK, 1.8f, VR_CONSOLE_KEY_CAPS, qfalse },
	VR_CHAR_KEY( "A", 'a', 'A' ), VR_CHAR_KEY( "S", 's', 'S' ),
	VR_CHAR_KEY( "D", 'd', 'D' ), VR_CHAR_KEY( "F", 'f', 'F' ),
	VR_CHAR_KEY( "G", 'g', 'G' ), VR_CHAR_KEY( "H", 'h', 'H' ),
	VR_CHAR_KEY( "J", 'j', 'J' ), VR_CHAR_KEY( "K", 'k', 'K' ),
	VR_CHAR_KEY( "L", 'l', 'L' ), VR_CHAR_KEY( "; :", ';', ':' ),
	VR_CHAR_KEY( "' \"", '\'', '"' ),
	VR_CONTROL_KEY( "ENTER", A_ENTER, 2.2f, qfalse ),
};

static const vrConsoleKey_t vrConsoleBottomKeys[] = {
	{ "SHIFT", 0, 0, A_SHIFT, 2.3f, VR_CONSOLE_KEY_SHIFT, qfalse },
	VR_CHAR_KEY( "Z", 'z', 'Z' ), VR_CHAR_KEY( "X", 'x', 'X' ),
	VR_CHAR_KEY( "C", 'c', 'C' ), VR_CHAR_KEY( "V", 'v', 'V' ),
	VR_CHAR_KEY( "B", 'b', 'B' ), VR_CHAR_KEY( "N", 'n', 'N' ),
	VR_CHAR_KEY( "M", 'm', 'M' ), VR_CHAR_KEY( ", <", ',', '<' ),
	VR_CHAR_KEY( ". >", '.', '>' ), VR_CHAR_KEY( "/ ?", '/', '?' ),
	VR_WIDE_CHAR_KEY( "_", '_', '_', 1.2f ),
	VR_SPACER( 2.3f ),
	VR_CONTROL_KEY( "", A_CURSOR_UP, 1.2f, qtrue ),
	VR_CONTROL_KEY( "DEL", A_DELETE, 1.4f, qtrue ),
};

static const vrConsoleKey_t vrConsoleNavigationKeys[] = {
	VR_CONTROL_KEY( "INS", A_INSERT, 1.2f, qfalse ),
	VR_CONTROL_KEY( "HOME", A_HOME, 1.4f, qtrue ),
	VR_CONTROL_KEY( "PGUP", A_PAGE_UP, 1.4f, qtrue ),
	VR_CONTROL_KEY( "DEL", A_DELETE, 1.2f, qtrue ),
	VR_SPACER( 1.2f ),
	VR_WIDE_CHAR_KEY( "SPACE", ' ', ' ', 6.0f ),
	VR_CONTROL_KEY( "END", A_END, 1.2f, qtrue ),
	VR_CONTROL_KEY( "PGDN", A_PAGE_DOWN, 1.4f, qtrue ),
	VR_CONTROL_KEY( "", A_CURSOR_LEFT, 1.2f, qtrue ),
	VR_CONTROL_KEY( "", A_CURSOR_DOWN, 1.2f, qtrue ),
	VR_CONTROL_KEY( "", A_CURSOR_RIGHT, 1.2f, qtrue ),
};

static const vrConsoleKeyRow_t vrConsoleKeyRows[] = {
	{ vrConsoleNumberKeys, ARRAY_LEN( vrConsoleNumberKeys ) },
	{ vrConsoleTopKeys, ARRAY_LEN( vrConsoleTopKeys ) },
	{ vrConsoleHomeKeys, ARRAY_LEN( vrConsoleHomeKeys ) },
	{ vrConsoleBottomKeys, ARRAY_LEN( vrConsoleBottomKeys ) },
	{ vrConsoleNavigationKeys, ARRAY_LEN( vrConsoleNavigationKeys ) },
};
static constexpr int VR_CONSOLE_KEY_ROW_COUNT =
	static_cast<int>( ARRAY_LEN( vrConsoleKeyRows ) );

#undef VR_CHAR_KEY
#undef VR_WIDE_CHAR_KEY
#undef VR_CONTROL_KEY
#undef VR_SPACER

static constexpr float VR_CONSOLE_KEYBOARD_TOP = 472.0f;
static constexpr float VR_CONSOLE_KEYBOARD_ROW_HEIGHT = 32.0f;
static constexpr float VR_CONSOLE_KEYBOARD_ROW_GAP = 4.0f;
static constexpr float VR_CONSOLE_KEY_GAP = 3.0f;
static constexpr int VR_CONSOLE_OPEN_MILLISECONDS = 180;
static constexpr int VR_CONSOLE_CLOSE_MILLISECONDS = 120;
static constexpr int VR_CONSOLE_REPEAT_DELAY = 430;
static constexpr int VR_CONSOLE_REPEAT_INTERVAL = 65;

static vrConsolePhase_t vrConsolePhase = VR_CONSOLE_CLOSED;
static int vrConsolePhaseStart = 0;
static qboolean vrConsoleShift = qfalse;
static qboolean vrConsoleCaps = qfalse;
static qboolean vrConsoleBindingWasDown = qfalse;
static qboolean vrConsoleBindingLongPress = qfalse;
static int vrConsoleBindingPressStart = 0;
static vrConsolePointer_t vrConsolePointers[2] = {};
static qboolean vrConsoleTriggerWasDown[2] = {};
static int vrConsoleHeldKey[2] = { -1, -1 };
static int vrConsoleNextRepeat[2] = {};
static uint32_t vrConsoleConsumedButtons[2] = {};
static qboolean vrConsoleConsumedIndexTrigger[2] = {};
static qboolean vrConsoleConsumedGripTrigger[2] = {};
static cvar_t *vrConsoleButtonCvar = nullptr;
static cvar_t *vrConsoleHoldCvar = nullptr;
static cvar_t *vrConsoleAnimationCvar = nullptr;

static void Con_VrInitCvars()
{
	if ( vrConsoleButtonCvar == nullptr )
	{
		vrConsoleButtonCvar = Cvar_Get( "vr_console_button", "0", CVAR_ARCHIVE );
		vrConsoleHoldCvar = Cvar_Get( "vr_console_hold_ms", "600", CVAR_ARCHIVE );
		vrConsoleAnimationCvar = Cvar_Get( "vr_console_animation", "1", CVAR_ARCHIVE );
		Cvar_CheckRange( vrConsoleButtonCvar, 0.0f, 2.0f, qtrue );
		Cvar_CheckRange( vrConsoleHoldCvar, 300.0f, 1200.0f, qtrue );
		Cvar_CheckRange( vrConsoleAnimationCvar, 0.0f, 1.0f, qtrue );
	}
}

static qboolean Con_VrPhaseVisible()
{
	return vrConsolePhase != VR_CONSOLE_CLOSED ? qtrue : qfalse;
}

static qboolean Con_VrPhaseInteractive()
{
	return ( vrConsolePhase == VR_CONSOLE_OPENING ||
		vrConsolePhase == VR_CONSOLE_OPEN ) ? qtrue : qfalse;
}

static void Con_VrFeedback( int hand, qboolean opening )
{
	static sfxHandle_t openSound = 0;
	static sfxHandle_t closeSound = 0;
	sfxHandle_t *sound = opening ? &openSound : &closeSound;
	if ( *sound == 0 )
	{
		*sound = S_RegisterSound( "sound/interface/button1.mp3" );
	}
	if ( *sound > 0 )
	{
		S_StartLocalSound( *sound, CHAN_LOCAL_SOUND );
	}
	if ( hand >= 0 && hand < 2 && re.VR_ApplyHaptic != nullptr )
	{
		re.VR_ApplyHaptic( hand, opening ? 42 : 28, opening ? 0.26f : 0.18f );
	}
}

static void Con_VrSetOpen( qboolean open, int hand )
{
	Con_VrInitCvars();
	if ( open )
	{
		if ( Con_VrPhaseInteractive() )
		{
			return;
		}
		if ( con_autoclear->integer )
		{
			Field_Clear( &g_consoleField );
		}
		g_consoleField.widthInChars = g_console_field_width;
		Con_ClearNotify();
		Key_SetCatcher( Key_GetCatcher() | KEYCATCH_CONSOLE );
		vrConsolePhase = vrConsoleAnimationCvar->integer
			? VR_CONSOLE_OPENING : VR_CONSOLE_OPEN;
		vrConsolePhaseStart = Sys_Milliseconds();
		for ( int pointer = 0; pointer < 2; ++pointer )
		{
			vrConsoleHeldKey[pointer] = -1;
			vrConsoleNextRepeat[pointer] = 0;
		}
		Con_VrFeedback( hand, qtrue );
	}
	else
	{
		if ( vrConsolePhase == VR_CONSOLE_CLOSED ||
			 vrConsolePhase == VR_CONSOLE_CLOSING )
		{
			return;
		}
		Key_SetCatcher( Key_GetCatcher() & ~KEYCATCH_CONSOLE );
		vrConsolePhase = vrConsoleAnimationCvar->integer
			? VR_CONSOLE_CLOSING : VR_CONSOLE_CLOSED;
		vrConsolePhaseStart = Sys_Milliseconds();
		vrConsoleShift = qfalse;
		for ( int pointer = 0; pointer < 2; ++pointer )
		{
			vrConsoleHeldKey[pointer] = -1;
		}
		Con_VrFeedback( hand, qfalse );
	}
}

static void Con_VrUpdatePhase()
{
	Con_VrInitCvars();
	if ( !vrConsoleAnimationCvar->integer )
	{
		if ( vrConsolePhase == VR_CONSOLE_OPENING )
		{
			vrConsolePhase = VR_CONSOLE_OPEN;
		}
		else if ( vrConsolePhase == VR_CONSOLE_CLOSING )
		{
			vrConsolePhase = VR_CONSOLE_CLOSED;
		}
		return;
	}
	const int elapsed = Sys_Milliseconds() - vrConsolePhaseStart;
	if ( vrConsolePhase == VR_CONSOLE_OPENING &&
		 elapsed >= VR_CONSOLE_OPEN_MILLISECONDS )
	{
		vrConsolePhase = VR_CONSOLE_OPEN;
	}
	else if ( vrConsolePhase == VR_CONSOLE_CLOSING &&
		 elapsed >= VR_CONSOLE_CLOSE_MILLISECONDS )
	{
		vrConsolePhase = VR_CONSOLE_CLOSED;
	}
}

static qboolean Con_VrKeyRect(
	int rowIndex, int keyIndex, float *x, float *y, float *width, float *height )
{
	if ( rowIndex < 0 || rowIndex >= VR_CONSOLE_KEY_ROW_COUNT )
	{
		return qfalse;
	}
	const vrConsoleKeyRow_t &row = vrConsoleKeyRows[rowIndex];
	if ( keyIndex < 0 || keyIndex >= row.count )
	{
		return qfalse;
	}
	float totalUnits = 0.0f;
	for ( int key = 0; key < row.count; ++key )
	{
		totalUnits += row.keys[key].widthUnits;
	}
	const float availableWidth = VR_CONSOLE_WIDTH -
		VR_CONSOLE_KEY_GAP * static_cast<float>( row.count - 1 );
	const float unitWidth = availableWidth / totalUnits;
	*x = static_cast<float>( VR_CONSOLE_LEFT );
	for ( int key = 0; key < keyIndex; ++key )
	{
		*x += row.keys[key].widthUnits * unitWidth + VR_CONSOLE_KEY_GAP;
	}
	*y = VR_CONSOLE_KEYBOARD_TOP + rowIndex *
		( VR_CONSOLE_KEYBOARD_ROW_HEIGHT + VR_CONSOLE_KEYBOARD_ROW_GAP );
	*width = row.keys[keyIndex].widthUnits * unitWidth;
	*height = VR_CONSOLE_KEYBOARD_ROW_HEIGHT;
	return qtrue;
}

static int Con_VrFindKey( float x, float y )
{
	for ( int row = 0; row < VR_CONSOLE_KEY_ROW_COUNT; ++row )
	{
		for ( int key = 0; key < vrConsoleKeyRows[row].count; ++key )
		{
			if ( vrConsoleKeyRows[row].keys[key].type == VR_CONSOLE_KEY_SPACER )
			{
				continue;
			}
			float keyX, keyY, keyWidth, keyHeight;
			Con_VrKeyRect( row, key, &keyX, &keyY, &keyWidth, &keyHeight );
			if ( x >= keyX && x <= keyX + keyWidth &&
				 y >= keyY && y <= keyY + keyHeight )
			{
				return row * 32 + key;
			}
		}
	}
	return -1;
}

static const vrConsoleKey_t *Con_VrGetKey( int id )
{
	const int row = id / 32;
	const int key = id % 32;
	if ( row < 0 || row >= VR_CONSOLE_KEY_ROW_COUNT ||
		 key < 0 || key >= vrConsoleKeyRows[row].count )
	{
		return nullptr;
	}
	return &vrConsoleKeyRows[row].keys[key];
}

static void Con_VrQueueControlKey( int key )
{
	Sys_QueEvent( 0, SE_KEY, key, qtrue, 0, nullptr );
	Sys_QueEvent( 0, SE_KEY, key, qfalse, 0, nullptr );
}

static void Con_VrActivateKey( int id, int hand )
{
	const vrConsoleKey_t *key = Con_VrGetKey( id );
	if ( key == nullptr )
	{
		return;
	}
	if ( key->type == VR_CONSOLE_KEY_SPACER )
	{
		return;
	}
	if ( key->type == VR_CONSOLE_KEY_SHIFT )
	{
		vrConsoleShift = vrConsoleShift ? qfalse : qtrue;
	}
	else if ( key->type == VR_CONSOLE_KEY_CAPS )
	{
		vrConsoleCaps = vrConsoleCaps ? qfalse : qtrue;
	}
	else if ( key->type == VR_CONSOLE_KEY_CONTROL )
	{
		Con_VrQueueControlKey( key->keyCode );
	}
	else
	{
		int character = key->normalCharacter;
		if ( character >= 'a' && character <= 'z' )
		{
			if ( ( vrConsoleShift != qfalse ) != ( vrConsoleCaps != qfalse ) )
			{
				character = key->shiftedCharacter;
			}
		}
		else if ( vrConsoleShift )
		{
			character = key->shiftedCharacter;
		}
		Sys_QueEvent( 0, SE_CHAR, character, 0, 0, nullptr );
		vrConsoleShift = qfalse;
	}
	if ( hand >= 0 && hand < 2 && re.VR_ApplyHaptic != nullptr )
	{
		re.VR_ApplyHaptic( hand, 24, 0.16f );
	}
}

static void Con_VrUpdatePointersAndKeyboard(
	const vrControllerState_t *left, const vrControllerState_t *right )
{
	const vrControllerState_t *controllers[2] = { left, right };
	const int now = Sys_Milliseconds();
	for ( int hand = 0; hand < 2; ++hand )
	{
		vrConsolePointers[hand] = {};
		if ( Con_VrPhaseVisible() && re.VR_GetSpatialConsolePointer != nullptr )
		{
			vrConsolePointers[hand].valid = re.VR_GetSpatialConsolePointer(
				hand,
				&vrConsolePointers[hand].x,
				&vrConsolePointers[hand].y,
				&vrConsolePointers[hand].distance );
		}
		const qboolean triggerDown =
			( controllers[hand]->buttons & VR_CONTROLLER_BUTTON_TRIGGER ) ||
			controllers[hand]->indexTrigger > 0.5f ? qtrue : qfalse;
		const qboolean triggerPressed =
			triggerDown && !vrConsoleTriggerWasDown[hand] ? qtrue : qfalse;
		const int hoveredKey = vrConsolePointers[hand].valid
			? Con_VrFindKey( vrConsolePointers[hand].x, vrConsolePointers[hand].y )
			: -1;
		if ( vrConsolePhase == VR_CONSOLE_OPEN && triggerPressed && hoveredKey >= 0 )
		{
			Con_VrActivateKey( hoveredKey, hand );
			const vrConsoleKey_t *key = Con_VrGetKey( hoveredKey );
			vrConsoleHeldKey[hand] = key != nullptr && key->repeat
				? hoveredKey : -1;
			vrConsoleNextRepeat[hand] = now + VR_CONSOLE_REPEAT_DELAY;
		}
		else if ( vrConsolePhase == VR_CONSOLE_OPEN && triggerDown &&
			 vrConsoleHeldKey[hand] >= 0 && hoveredKey == vrConsoleHeldKey[hand] &&
			 now >= vrConsoleNextRepeat[hand] )
		{
			Con_VrActivateKey( vrConsoleHeldKey[hand], hand );
			vrConsoleNextRepeat[hand] = now + VR_CONSOLE_REPEAT_INTERVAL;
		}
		if ( !triggerDown || hoveredKey != vrConsoleHeldKey[hand] )
		{
			vrConsoleHeldKey[hand] = -1;
		}
		vrConsoleTriggerWasDown[hand] = triggerDown;
	}
}

static void Con_VrClearGameplayInput( vrControllerState_t *state )
{
	state->buttons = 0;
	state->touches = 0;
	state->indexTrigger = 0.0f;
	state->gripTrigger = 0.0f;
	state->joystick[0] = 0.0f;
	state->joystick[1] = 0.0f;
	state->joystickActive = qfalse;
	state->velocityFlags = 0;
	memset( state->linearVelocity, 0, sizeof( state->linearVelocity ) );
	memset( state->angularVelocity, 0, sizeof( state->angularVelocity ) );
}

static void Con_VrLatchConsumedInput(
	int hand,
	const vrControllerState_t *raw,
	vrControllerState_t *filtered,
	qboolean consoleVisible )
{
	if ( consoleVisible )
	{
		vrConsoleConsumedButtons[hand] |= raw->buttons;
		if ( raw->indexTrigger > 0.05f )
		{
			vrConsoleConsumedIndexTrigger[hand] = qtrue;
		}
		if ( raw->gripTrigger > 0.05f )
		{
			vrConsoleConsumedGripTrigger[hand] = qtrue;
		}
		Con_VrClearGameplayInput( filtered );
		return;
	}

	vrConsoleConsumedButtons[hand] &= raw->buttons;
	filtered->buttons &= ~vrConsoleConsumedButtons[hand];
	if ( vrConsoleConsumedIndexTrigger[hand] )
	{
		if ( raw->indexTrigger > 0.05f )
		{
			filtered->indexTrigger = 0.0f;
			filtered->buttons &= ~VR_CONTROLLER_BUTTON_TRIGGER;
		}
		else
		{
			vrConsoleConsumedIndexTrigger[hand] = qfalse;
		}
	}
	if ( vrConsoleConsumedGripTrigger[hand] )
	{
		if ( raw->gripTrigger > 0.05f )
		{
			filtered->gripTrigger = 0.0f;
			filtered->buttons &= ~VR_CONTROLLER_BUTTON_GRIP;
		}
		else
		{
			vrConsoleConsumedGripTrigger[hand] = qfalse;
		}
	}
}

void Con_VrFilterControllerInput(
	const vrControllerState_t *left,
	const vrControllerState_t *right,
	vrControllerState_t *filteredLeft,
	vrControllerState_t *filteredRight )
{
	if ( left == nullptr || right == nullptr ||
		 filteredLeft == nullptr || filteredRight == nullptr )
	{
		return;
	}
	Con_VrInitCvars();
	Con_VrUpdatePhase();
	*filteredLeft = *left;
	*filteredRight = *right;

	int bindingHand = 0;
	uint32_t bindingMask = VR_CONTROLLER_BUTTON_Y;
	if ( vrConsoleButtonCvar->integer == 1 )
	{
		bindingHand = 1;
		bindingMask = VR_CONTROLLER_BUTTON_B;
	}
	else if ( vrConsoleButtonCvar->integer == 2 &&
		 Cvar_VariableIntegerValue( "vr_control_scheme" ) >= 10 )
	{
		bindingHand = 1;
		bindingMask = VR_CONTROLLER_BUTTON_B;
	}
	const vrControllerState_t *bindingController = bindingHand == 0 ? left : right;
	const qboolean bindingDown =
		( bindingController->buttons & bindingMask ) != 0 ? qtrue : qfalse;
	const int now = Sys_Milliseconds();
	if ( bindingDown && !vrConsoleBindingWasDown )
	{
		vrConsoleBindingPressStart = now;
		vrConsoleBindingLongPress = qfalse;
	}
	if ( bindingDown && !vrConsoleBindingLongPress &&
		 now - vrConsoleBindingPressStart >= vrConsoleHoldCvar->integer )
	{
		vrConsoleBindingLongPress = qtrue;
		Con_VrSetOpen( Con_VrPhaseInteractive() ? qfalse : qtrue, bindingHand );
	}
	if ( !bindingDown && vrConsoleBindingWasDown )
	{
		if ( !vrConsoleBindingLongPress )
		{
			Con_VrQueueControlKey( A_TAB );
		}
		vrConsoleBindingLongPress = qfalse;
	}
	vrConsoleBindingWasDown = bindingDown;

	Con_VrUpdatePointersAndKeyboard( left, right );
	const qboolean consoleVisible = Con_VrPhaseVisible();
	vr.spatial_console_visible = consoleVisible != qfalse;
	Con_VrLatchConsumedInput( 0, left, filteredLeft, consoleVisible );
	Con_VrLatchConsumedInput( 1, right, filteredRight, consoleVisible );
	if ( !consoleVisible )
	{
		vrControllerState_t *bindingFiltered =
			bindingHand == 0 ? filteredLeft : filteredRight;
		bindingFiltered->buttons &= ~bindingMask;
	}
}

static void Con_DrawVrChar( int x, int y, int ch )
{
	ch &= 255;
	if ( ch == ' ' )
	{
		return;
	}

	const int row = ch >> 4;
	const int column = ch & 15;
	const float textureRow = row * 0.0625f;
	const float textureColumn = column * 0.0625f;
	re.DrawStretchPic(
		x,
		y,
		VR_CONSOLE_CHAR_WIDTH,
		VR_CONSOLE_CHAR_HEIGHT,
		textureColumn,
		textureRow,
		textureColumn + 0.03125f,
		textureRow + 0.0625f,
		cls.charSetShader );
}

static void Con_DrawVrString( int x, int y, const char *text )
{
	for ( ; text != nullptr && *text != '\0'; ++text, x += VR_CONSOLE_CHAR_WIDTH )
	{
		Con_DrawVrChar( x, y, *text );
	}
}

static void Con_DrawVrInput( int x, int y )
{
	if ( cls.state != CA_DISCONNECTED && !( Key_GetCatcher() & KEYCATCH_CONSOLE ) )
	{
		return;
	}

	Con_DrawVrChar( x, y, CONSOLE_PROMPT_CHAR );
	x += VR_CONSOLE_CHAR_WIDTH;

	const int visibleCharacters = std::max( 1, g_consoleField.widthInChars - 1 );
	const int length = static_cast<int>( strlen( g_consoleField.buffer ) );
	int firstCharacter = g_consoleField.scroll;
	if ( length <= visibleCharacters )
	{
		firstCharacter = 0;
	}
	else if ( firstCharacter + visibleCharacters > length )
	{
		firstCharacter = length - visibleCharacters;
	}
	firstCharacter = std::max( 0, firstCharacter );
	g_consoleField.scroll = firstCharacter;

	const int drawnCharacters = std::min(
		visibleCharacters,
		std::max( 0, length - firstCharacter ) );
	for ( int i = 0; i < drawnCharacters; ++i )
	{
		Con_DrawVrChar(
			x + i * VR_CONSOLE_CHAR_WIDTH,
			y,
			g_consoleField.buffer[firstCharacter + i] );
	}

	if ( ( static_cast<int>( cls.realtime >> 8 ) & 1 ) == 0 )
	{
		const int cursor = std::max(
			0,
			std::min(
				visibleCharacters,
				g_consoleField.cursor - firstCharacter ) );
		Con_DrawVrChar(
			x + cursor * VR_CONSOLE_CHAR_WIDTH,
			y,
			kg.key_overstrikeMode ? 11 : 10 );
	}
}

/*
================
Con_ToggleConsole_f
================
*/
void Con_ToggleConsole_f (void) {
	if ( Con_UseVrLayout() )
	{
		Con_VrUpdatePhase();
		Con_VrSetOpen( Con_VrPhaseInteractive() ? qfalse : qtrue, -1 );
		return;
	}

	// closing a full screen console restarts the demo loop
	if ( cls.state == CA_DISCONNECTED && Key_GetCatcher( ) == KEYCATCH_CONSOLE ) {
//		CL_StartDemoLoop();
		return;
	}

	if( con_autoclear->integer )
		Field_Clear( &g_consoleField );
	g_consoleField.widthInChars = g_console_field_width;

	Con_ClearNotify ();
	Key_SetCatcher( Key_GetCatcher( ) ^ KEYCATCH_CONSOLE );
}

/*
===================
Con_ToggleMenu_f
===================
*/
void Con_ToggleMenu_f( void ) {
	CL_KeyEvent( A_ESCAPE, qtrue, Sys_Milliseconds() );
	CL_KeyEvent( A_ESCAPE, qfalse, Sys_Milliseconds() );
}

/*
================
Con_Clear_f
================
*/
void Con_Clear_f (void) {
	int		i;

	for ( i = 0 ; i < CON_TEXTSIZE ; i++ ) {
		con.text[i] = (ColorIndex(COLOR_WHITE)<<8) | ' ';
	}

	Con_Bottom();		// go to end
}

/*
================
Con_Dump_f

Save the console contents out to a file
================
*/
void Con_Dump_f (void)
{
	int		l, x, i;
	short	*line;
	fileHandle_t	f;
	int		bufferlen;
	char	*buffer;
	char	filename[MAX_QPATH];

	if (Cmd_Argc() != 2)
	{
		Com_Printf ("%s\n", SE_GetString("CON_TEXT_DUMP_USAGE"));
		return;
	}

	Q_strncpyz( filename, Cmd_Argv( 1 ), sizeof( filename ) );
	COM_DefaultExtension( filename, sizeof( filename ), ".txt" );

	if(!COM_CompareExtension(filename, ".txt"))
	{
		Com_Printf( "Con_Dump_f: Only the \".txt\" extension is supported by this command!\n" );
		return;
	}

	f = FS_FOpenFileWrite( filename );
	if (!f)
	{
		Com_Printf ("ERROR: couldn't open %s.\n", filename);
		return;
	}

	Com_Printf ("Dumped console text to %s.\n", filename );

	// skip empty lines
	for (l = con.current - con.totallines + 1 ; l <= con.current ; l++)
	{
		line = con.text + (l%con.totallines)*con.linewidth;
		for (x=0 ; x<con.linewidth ; x++)
			if ((line[x] & 0xff) != ' ')
				break;
		if (x != con.linewidth)
			break;
	}

	bufferlen = con.linewidth + 2 * sizeof ( char );

	buffer = (char *)Z_Malloc( bufferlen, TAG_TEMP_WORKSPACE, qfalse );

	// write the remaining lines
	buffer[bufferlen-1] = 0;
	for ( ; l <= con.current ; l++)
	{
		line = con.text + (l%con.totallines)*con.linewidth;
		for(i=0; i<con.linewidth; i++)
			buffer[i] = (char) (line[i] & 0xff);
		for (x=con.linewidth-1 ; x>=0 ; x--)
		{
			if (buffer[x] == ' ')
				buffer[x] = 0;
			else
				break;
		}
		Q_strcat(buffer, bufferlen, "\n");
		FS_Write(buffer, strlen(buffer), f);
	}

	Z_Free( buffer );
	FS_FCloseFile( f );
}


/*
================
Con_ClearNotify
================
*/
void Con_ClearNotify( void ) {
	int		i;

	for ( i = 0 ; i < NUM_CON_TIMES ; i++ ) {
		con.times[i] = 0;
	}
}



/*
================
Con_CheckResize

If the line width has changed, reformat the buffer.
================
*/
void Con_CheckResize (void)
{
	int		i, j, width, oldwidth, oldtotallines, numlines, numchars;
	short	tbuf[CON_TEXTSIZE];

	width = Con_UseVrLayout()
		? VR_CONSOLE_COLUMNS
		: (cls.glconfig.vidWidth / SMALLCHAR_WIDTH) - 2;

	if (width == con.linewidth)
	{
		g_console_field_width = width;
		g_consoleField.widthInChars = width;
		return;
	}

	if (width < 1)			// video hasn't been initialized yet
	{
		con.xadjust = 1;
		con.yadjust = 1;
		width = DEFAULT_CONSOLE_WIDTH;
		con.linewidth = width;
		con.totallines = CON_TEXTSIZE / con.linewidth;
		for(i=0; i<CON_TEXTSIZE; i++)
		{
			con.text[i] = (ColorIndex(COLOR_WHITE)<<8) | ' ';
		}
	}
	else
	{
		if ( Con_UseVrLayout() )
		{
			con.xadjust = 1.0f;
			con.yadjust = 1.0f;
		}
		else
		{
			// on wide screens, we will center the text
			con.xadjust = 640.0f / cls.glconfig.vidWidth;
			con.yadjust = 480.0f / cls.glconfig.vidHeight;
		}

		oldwidth = con.linewidth;
		con.linewidth = width;
		oldtotallines = con.totallines;
		con.totallines = CON_TEXTSIZE / con.linewidth;
		numlines = oldtotallines;

		if (con.totallines < numlines)
			numlines = con.totallines;

		numchars = oldwidth;

		if (con.linewidth < numchars)
			numchars = con.linewidth;

		memcpy (tbuf, con.text, CON_TEXTSIZE * sizeof(short));
		for(i=0; i<CON_TEXTSIZE; i++)

			con.text[i] = (ColorIndex(COLOR_WHITE)<<8) | ' ';


		for (i=0 ; i<numlines ; i++)
		{
			for (j=0 ; j<numchars ; j++)
			{
				con.text[(con.totallines - 1 - i) * con.linewidth + j] =
						tbuf[((con.current - i + oldtotallines) %
							  oldtotallines) * oldwidth + j];
			}
		}

		Con_ClearNotify ();
	}

	con.current = con.totallines - 1;
	con.display = con.current;
	g_console_field_width = width;
	g_consoleField.widthInChars = width;
	for ( i = 0; i < COMMAND_HISTORY; ++i )
	{
		historyEditLines[i].widthInChars = width;
	}
}


/*
==================
Cmd_CompleteTxtName
==================
*/
void Cmd_CompleteTxtName( char *args, int argNum ) {
	if ( argNum == 2 )
		Field_CompleteFilename( "", "txt", qfalse, qtrue );
}

/*
================
Con_Init
================
*/
void Con_Init (void) {
	int		i;

	con_notifytime = Cvar_Get ("con_notifytime", "3", 0);
	con_drawnotify = Cvar_Get ("con_drawnotify", "0", CVAR_ARCHIVE_ND);
	con_conspeed = Cvar_Get ("scr_conspeed", "3", 0);
	Cvar_CheckRange (con_conspeed, 1.0f, 100.0f, qfalse);

	con_opacity = Cvar_Get ("con_opacity", "0.8", CVAR_ARCHIVE_ND);
	con_autoclear = Cvar_Get ("con_autoclear", "1", CVAR_ARCHIVE_ND);

	Field_Clear( &g_consoleField );
	g_consoleField.widthInChars = g_console_field_width;
	for ( i = 0 ; i < COMMAND_HISTORY ; i++ ) {
		Field_Clear( &historyEditLines[i] );
		historyEditLines[i].widthInChars = g_console_field_width;
	}

	Cmd_AddCommand ("toggleconsole", Con_ToggleConsole_f);
	Cmd_AddCommand ("togglemenu", Con_ToggleMenu_f);
	Cmd_AddCommand ("clear", Con_Clear_f);
	Cmd_AddCommand ("condump", Con_Dump_f);
	Cmd_SetCommandCompletionFunc( "condump", Cmd_CompleteTxtName );
}


/*
===============
Con_Linefeed
===============
*/
void Con_Linefeed (void)
{
	int		i;

	// mark time for transparent overlay
	if (con.current >= 0)
		con.times[con.current % NUM_CON_TIMES] = cls.realtime;

	con.x = 0;
	if (con.display == con.current)
		con.display++;
	con.current++;
	for(i=0; i<con.linewidth; i++)
		con.text[(con.current%con.totallines)*con.linewidth+i] = (ColorIndex(COLOR_WHITE)<<8) | ' ';
}

/*
================
CL_ConsolePrint

Handles cursor positioning, line wrapping, etc
All console printing must go through this in order to be logged to disk
If no console is visible, the text will appear at the top of the game window
================
*/
void CL_ConsolePrint( char *txt ) {
	int		y;
	int		c, l;
	int		color;

	// for some demos we don't want to ever show anything on the console
	if ( cl_noprint && cl_noprint->integer ) {
		return;
	}

	if (!con.initialized) {
		con.color[0] =
		con.color[1] =
		con.color[2] =
		con.color[3] = 1.0f;
		con.linewidth = -1;
		Con_CheckResize ();
		con.initialized = qtrue;
	}

	color = ColorIndex(COLOR_WHITE);

	while ( (c = (unsigned char )*txt) != 0 ) {
		if ( Q_IsColorString( (unsigned char*) txt ) ) {
			color = ColorIndex( *(txt+1) );
			txt += 2;
			continue;
		}

		// count word length
		for (l=0 ; l< con.linewidth ; l++) {
			if ( txt[l] <= ' ') {
				break;
			}

		}

		// word wrap
		if (l != con.linewidth && (con.x + l >= con.linewidth) ) {
			Con_Linefeed();

		}

		txt++;

		switch (c)
		{
		case '\n':
			Con_Linefeed ();
			break;
		case '\r':
			con.x = 0;
			break;
		default:	// display character and advance
			y = con.current % con.totallines;
			con.text[y*con.linewidth+con.x] = (color << 8) | c;
			con.x++;
			if (con.x >= con.linewidth) {

				Con_Linefeed();
				con.x = 0;
			}
			break;
		}
	}


	// mark time for transparent overlay

	if (con.current >= 0)
		con.times[con.current % NUM_CON_TIMES] = cls.realtime;
}


/*
==============================================================================

DRAWING

==============================================================================
*/


/*
================
Con_DrawInput

Draw the editline after a ] prompt
================
*/
static void Con_DrawInputAt( int x, int y, int width )
{
	if ( cls.state != CA_DISCONNECTED && !(Key_GetCatcher( ) & KEYCATCH_CONSOLE ) ) {
		return;
	}

	re.SetColor( con.color );

	SCR_DrawSmallChar( x, y, CONSOLE_PROMPT_CHAR );

	Field_Draw( &g_consoleField, x + SMALLCHAR_WIDTH, y,
		width - SMALLCHAR_WIDTH, qtrue, qtrue );
}

void Con_DrawInput (void) {
	const int y = con.vislines -
		( SMALLCHAR_HEIGHT * (re.Language_IsAsian() ? 1.5 : 2) );
	Con_DrawInputAt(
		static_cast<int>( con.xadjust ) + SMALLCHAR_WIDTH,
		y,
		SCREEN_WIDTH - 2 * SMALLCHAR_WIDTH );
}


/*
================
Con_DrawNotify

Draws the last few lines of output transparently over the game top
================
*/
void Con_DrawNotify (void)
{
	int		x, v;
	short	*text;
	int		i;
	int		time;
	int		currentColor;

	currentColor = 7;
	re.SetColor( g_color_table[currentColor] );

	v = 0;
	for (i= con.current-NUM_CON_TIMES+1 ; i<=con.current ; i++)
	{
		if (i < 0)
			continue;
		time = con.times[i % NUM_CON_TIMES];
		if (time == 0)
			continue;
		time = cls.realtime - time;
		if (time > con_notifytime->value*1000)
			continue;
		text = con.text + (i % con.totallines)*con.linewidth;

		// asian language needs to use the new font system to print glyphs...
		//
		// (ignore colours since we're going to print the whole thing as one string)
		//
		if (re.Language_IsAsian())
		{
			int iFontIndex = re.RegisterFont("ocr_a");	// this seems naughty
			const float fFontScale = 0.75f*con.yadjust;
			const int iPixelHeightToAdvance =   2+(1.3/con.yadjust) * re.Font_HeightPixels(iFontIndex, fFontScale);	// for asian spacing, since we don't want glyphs to touch.

			// concat the text to be printed...
			//
			char sTemp[4096]={0};	// ott
			for (x = 0 ; x < con.linewidth ; x++)
			{
				if ( ( (text[x]>>8)&Q_COLOR_BITS ) != currentColor ) {
					currentColor = (text[x]>>8)&Q_COLOR_BITS;
					strcat(sTemp,va("^%i", (text[x]>>8)&Q_COLOR_BITS) );
				}
				strcat(sTemp,va("%c",text[x] & 0xFF));
			}
			//
			// and print...
			//
			re.Font_DrawString(con.xadjust*(con.xadjust + (1*SMALLCHAR_WIDTH/*aesthetics*/)), con.yadjust*(v), sTemp, g_color_table[currentColor], iFontIndex, -1, fFontScale);

			v +=  iPixelHeightToAdvance;
		}
		else
		{
			for (x = 0 ; x < con.linewidth ; x++) {
				if ( ( text[x] & 0xff ) == ' ' ) {
					continue;
				}
				if ( ( (text[x]>>8)&Q_COLOR_BITS ) != currentColor ) {
					currentColor = (text[x]>>8)&Q_COLOR_BITS;
					re.SetColor( g_color_table[currentColor] );
				}
				SCR_DrawSmallChar( con.xadjust + (x+1)*SMALLCHAR_WIDTH, v, text[x] & 0xff );
			}
			v += SMALLCHAR_HEIGHT;
		}
	}

	re.SetColor( NULL );
}

/*
================
Con_DrawSolidConsole

Draws the console with the solid background
================
*/
void Con_DrawSolidConsole( float frac )
{
	int				i, x, y;
	int				rows;
	short			*text;
	int				row;
	int				lines;
	int				currentColor;

	lines = cls.glconfig.vidHeight * frac;
	if (lines <= 0)
		return;

	if (lines > cls.glconfig.vidHeight )
		lines = cls.glconfig.vidHeight;

	// draw the background
	y = frac * SCREEN_HEIGHT - 2;
	if ( y < 1 ) {
		y = 0;
	}
	else {
		// draw the background at full opacity only if fullscreen
		if (frac < 1.0f)
		{
			vec4_t con_color;
			MAKERGBA(con_color, 1.0f, 1.0f, 1.0f, Com_Clamp(0.0f, 1.0f, con_opacity->value));
			re.SetColor(con_color);
		}
		else
		{
			re.SetColor(NULL);
		}
		SCR_DrawPic( 0, 0, SCREEN_WIDTH, y, cls.consoleShader);
	}

	// draw the bottom bar and version number

	re.SetColor( console_color );
	re.DrawStretchPic( 0, y, SCREEN_WIDTH, 2, 0, 0, 0, 0, cls.whiteShader );

	i = strlen( Q3_VERSION );

	for (x=0 ; x<i ; x++) {
		SCR_DrawSmallChar( cls.glconfig.vidWidth - ( i - x + 1 ) * SMALLCHAR_WIDTH,
			(lines-(SMALLCHAR_HEIGHT+SMALLCHAR_HEIGHT/2)), Q3_VERSION[x] );
	}

	// draw the text
	con.vislines = lines;
	rows = (lines-SMALLCHAR_WIDTH)/SMALLCHAR_WIDTH;		// rows of text to draw

	y = lines - (SMALLCHAR_HEIGHT*3);

	// draw from the bottom up
	if (con.display != con.current)
	{
	// draw arrows to show the buffer is backscrolled
		re.SetColor( console_color );
		for (x=0 ; x<con.linewidth ; x+=4)
			SCR_DrawSmallChar( con.xadjust + (x+1)*SMALLCHAR_WIDTH, y, '^' );
		y -= SMALLCHAR_HEIGHT;
		rows--;
	}

	row = con.display;

	if ( con.x == 0 ) {
		row--;
	}

	currentColor = 7;
	re.SetColor( g_color_table[currentColor] );


	int iFontIndexForAsian = 0;	// kinda tacky, this just gets the first registered font, since Asian stuff ignores the contents anyway
	const float fFontScaleForAsian = 0.75f*con.yadjust;
	int iPixelHeightToAdvance = SMALLCHAR_HEIGHT;
	if (re.Language_IsAsian())
	{
		if (!iFontIndexForAsian)
		{
			iFontIndexForAsian = re.RegisterFont("ocr_a");	// must be a font that's used elsewhere
		}
		iPixelHeightToAdvance =   (1.3/con.yadjust) * re.Font_HeightPixels(iFontIndexForAsian, fFontScaleForAsian);	// for asian spacing, since we don't want glyphs to touch.
	}

	for (i=0 ; i<rows ; i++, y -= iPixelHeightToAdvance, row--)
	{
		if (row < 0)
			break;
		if (con.current - row >= con.totallines) {
			// past scrollback wrap point
			continue;
		}

		text = con.text + (row % con.totallines)*con.linewidth;


		// asian language needs to use the new font system to print glyphs...
		//
		// (ignore colours since we're going to print the whole thing as one string)
		//
		if (re.Language_IsAsian())
		{
			// concat the text to be printed...
			//
			char sTemp[4096]={0};	// ott
			for (x = 0 ; x < con.linewidth ; x++)
			{
				if ( ( (text[x]>>8)&Q_COLOR_BITS ) != currentColor ) {
					currentColor = (text[x]>>8)&Q_COLOR_BITS;
					strcat(sTemp,va("^%i", (text[x]>>8)&Q_COLOR_BITS) );
				}
				strcat(sTemp,va("%c",text[x] & 0xFF));
			}
			//
			// and print...
			//
			re.Font_DrawString(con.xadjust*(con.xadjust + (1*SMALLCHAR_WIDTH/*(aesthetics)*/)), con.yadjust*(y), sTemp, g_color_table[currentColor], iFontIndexForAsian, -1, fFontScaleForAsian);
		}
		else
		{
			for (x=0 ; x<con.linewidth ; x++) {
				if ( ( text[x] & 0xff ) == ' ' ) {
					continue;
				}

				if ( ( (text[x]>>8)&Q_COLOR_BITS ) != currentColor ) {
					currentColor = (text[x]>>8)&Q_COLOR_BITS;
					re.SetColor( g_color_table[currentColor] );
				}
				SCR_DrawSmallChar(  con.xadjust + (x+1)*SMALLCHAR_WIDTH, y, text[x] & 0xff );
			}
		}
	}

	// draw the input prompt, user text, and cursor if desired
	Con_DrawInput ();

	re.SetColor( NULL );
}

struct vrConsoleAnimation_t
{
	float openAmount;
	float scaleX;
	float scaleY;
	float opacity;
	float textAlpha;
	float keyboardReveal;
};

static float Con_VrEaseOut( float value )
{
	value = Com_Clamp( 0.0f, 1.0f, value );
	const float inverse = 1.0f - value;
	return 1.0f - inverse * inverse * inverse;
}

static vrConsoleAnimation_t Con_VrAnimation()
{
	Con_VrUpdatePhase();
	float openAmount = 1.0f;
	if ( vrConsolePhase == VR_CONSOLE_OPENING )
	{
		openAmount = Com_Clamp(
			0.0f, 1.0f,
			static_cast<float>( Sys_Milliseconds() - vrConsolePhaseStart ) /
				VR_CONSOLE_OPEN_MILLISECONDS );
	}
	else if ( vrConsolePhase == VR_CONSOLE_CLOSING )
	{
		openAmount = 1.0f - Com_Clamp(
			0.0f, 1.0f,
			static_cast<float>( Sys_Milliseconds() - vrConsolePhaseStart ) /
				VR_CONSOLE_CLOSE_MILLISECONDS );
	}
	else if ( vrConsolePhase == VR_CONSOLE_CLOSED )
	{
		openAmount = 0.0f;
	}

	vrConsoleAnimation_t animation = {};
	animation.openAmount = openAmount;
	animation.scaleX = std::max(
		0.025f, Con_VrEaseOut( openAmount / 0.72f ) );
	animation.scaleY = std::max(
		0.004f, Con_VrEaseOut( ( openAmount - 0.08f ) / 0.92f ) );
	animation.opacity = vrConsolePhase == VR_CONSOLE_CLOSING
		? Con_VrEaseOut( openAmount ) : 1.0f;
	animation.textAlpha = Con_VrEaseOut(
		( openAmount - 0.45f ) / 0.55f );
	animation.keyboardReveal = Con_VrEaseOut(
		( openAmount - 0.58f ) / 0.42f );
	return animation;
}

static void Con_VrSetColorWithAlpha( const float color[4], float alpha )
{
	vec4_t faded = {
		color[0], color[1], color[2],
		color[3] * Com_Clamp( 0.0f, 1.0f, alpha ),
	};
	re.SetColor( faded );
}

static float Con_VrKeyboardRowProgress( float reveal, int row )
{
	const float staggered = reveal *
		( static_cast<float>( VR_CONSOLE_KEY_ROW_COUNT ) + 0.75f ) -
		static_cast<float>( row ) * 0.88f;
	return Con_VrEaseOut( staggered );
}

static void Con_DrawVrArrow( int keyCode, float x, float y, float width, float height )
{
	const float centerX = x + width * 0.5f;
	const float centerY = y + height * 0.5f;
	const float stroke = 2.0f;

	if ( keyCode == A_CURSOR_UP || keyCode == A_CURSOR_DOWN )
	{
		const qboolean up = keyCode == A_CURSOR_UP ? qtrue : qfalse;
		const float direction = up ? -1.0f : 1.0f;
		const float stemY = up ? centerY - 3.0f : centerY - 8.0f;
		re.DrawStretchPic( centerX - 1.0f, stemY, stroke, 11.0f,
			0, 0, 0, 0, cls.whiteShader );
		for ( int step = 0; step < 3; ++step )
		{
			const float barWidth = stroke + static_cast<float>( step ) * 4.0f;
			const float barY = centerY + direction *
				( 9.0f - static_cast<float>( step ) * 2.0f );
			re.DrawStretchPic(
				centerX - barWidth * 0.5f,
				barY - stroke * 0.5f,
				barWidth,
				stroke,
				0, 0, 0, 0, cls.whiteShader );
		}
		return;
	}

	const qboolean left = keyCode == A_CURSOR_LEFT ? qtrue : qfalse;
	const float direction = left ? -1.0f : 1.0f;
	const float stemX = left ? centerX - 3.0f : centerX - 8.0f;
	re.DrawStretchPic( stemX, centerY - 1.0f, 11.0f, stroke,
		0, 0, 0, 0, cls.whiteShader );
	for ( int step = 0; step < 3; ++step )
	{
		const float barHeight = stroke + static_cast<float>( step ) * 4.0f;
		const float barX = centerX + direction *
			( 9.0f - static_cast<float>( step ) * 2.0f );
		re.DrawStretchPic(
			barX - stroke * 0.5f,
			centerY - barHeight * 0.5f,
			stroke,
			barHeight,
			0, 0, 0, 0, cls.whiteShader );
	}
}

static void Con_DrawVrKeyboard( const vrConsoleAnimation_t &animation )
{
	for ( int row = 0; row < VR_CONSOLE_KEY_ROW_COUNT; ++row )
	{
		const float rowProgress = Con_VrKeyboardRowProgress(
			animation.keyboardReveal, row );
		if ( rowProgress <= 0.001f )
		{
			continue;
		}
		for ( int keyIndex = 0;
			 keyIndex < vrConsoleKeyRows[row].count; ++keyIndex )
		{
			const vrConsoleKey_t &key = vrConsoleKeyRows[row].keys[keyIndex];
			if ( key.type == VR_CONSOLE_KEY_SPACER )
			{
				continue;
			}
			float x, y, width, height;
			Con_VrKeyRect( row, keyIndex, &x, &y, &width, &height );
			height *= rowProgress;
			const int keyId = row * 32 + keyIndex;
			qboolean hovered = qfalse;
			for ( int hand = 0; hand < 2; ++hand )
			{
				if ( vrConsolePointers[hand].valid &&
					 Con_VrFindKey(
						vrConsolePointers[hand].x,
						vrConsolePointers[hand].y ) == keyId )
				{
					hovered = qtrue;
				}
			}
			const qboolean latched = (
				( key.type == VR_CONSOLE_KEY_SHIFT && vrConsoleShift ) ||
				( key.type == VR_CONSOLE_KEY_CAPS && vrConsoleCaps ) )
				? qtrue : qfalse;
			vec4_t keyColor = {
				hovered ? 0.10f : 0.035f,
				hovered ? 0.34f : 0.095f,
				hovered ? 0.45f : 0.14f,
				0.90f * rowProgress,
			};
			if ( latched )
			{
				keyColor[0] = 0.42f;
				keyColor[1] = 0.31f;
				keyColor[2] = 0.06f;
			}
			re.SetColor( keyColor );
			re.DrawStretchPic(
				x, y, width, height, 0, 0, 0, 0, cls.whiteShader );

			vec4_t borderColor = {
				hovered ? 0.45f : 0.18f,
				hovered ? 0.95f : 0.52f,
				hovered ? 1.0f : 0.68f,
				rowProgress,
			};
			re.SetColor( borderColor );
			re.DrawStretchPic( x, y, width, 1, 0, 0, 0, 0, cls.whiteShader );
			re.DrawStretchPic( x, y + height - 1, width, 1, 0, 0, 0, 0, cls.whiteShader );
			re.DrawStretchPic( x, y, 1, height, 0, 0, 0, 0, cls.whiteShader );
			re.DrawStretchPic( x + width - 1, y, 1, height, 0, 0, 0, 0, cls.whiteShader );

			const float labelAlpha = Com_Clamp(
				0.0f, 1.0f, ( rowProgress - 0.55f ) / 0.45f );
			if ( labelAlpha > 0.0f )
			{
				vec4_t labelColor = { 0.82f, 0.94f, 1.0f, labelAlpha };
				re.SetColor( labelColor );
				if ( key.keyCode == A_CURSOR_UP || key.keyCode == A_CURSOR_DOWN ||
					 key.keyCode == A_CURSOR_LEFT || key.keyCode == A_CURSOR_RIGHT )
				{
					Con_DrawVrArrow( key.keyCode, x, y, width, height );
				}
				else
				{
					const int labelWidth = static_cast<int>( strlen( key.label ) ) *
						VR_CONSOLE_CHAR_WIDTH;
					Con_DrawVrString(
						static_cast<int>( x + ( width - labelWidth ) * 0.5f ),
						static_cast<int>( y + ( height - VR_CONSOLE_CHAR_HEIGHT ) * 0.5f ),
						key.label );
				}
			}
		}
	}
}

static void Con_DrawVrPointers()
{
	const vec4_t pointerColors[2] = {
		{ 0.20f, 0.95f, 1.0f, 1.0f },
		{ 1.0f, 0.70f, 0.18f, 1.0f },
	};
	for ( int hand = 0; hand < 2; ++hand )
	{
		const vrConsolePointer_t &pointer = vrConsolePointers[hand];
		if ( !pointer.valid || pointer.x < 42.0f || pointer.x > 598.0f ||
			 pointer.y < 12.0f || pointer.y > 664.0f )
		{
			continue;
		}
		re.SetColor( pointerColors[hand] );
		re.DrawStretchPic(
			pointer.x - 6.0f, pointer.y - 1.0f,
			12.0f, 2.0f, 0, 0, 0, 0, cls.whiteShader );
		re.DrawStretchPic(
			pointer.x - 1.0f, pointer.y - 6.0f,
			2.0f, 12.0f, 0, 0, 0, 0, cls.whiteShader );
	}
}

static void Con_DrawVrConsole()
{
	const vrConsoleAnimation_t animation = Con_VrAnimation();
	if ( re.VR_SetSpatialConsoleState != nullptr )
	{
		re.VR_SetSpatialConsoleState(
			qtrue,
			animation.scaleX,
			animation.scaleY,
			animation.opacity );
	}
	if ( re.VR_SetConsoleMode != nullptr )
	{
		re.VR_SetConsoleMode( qtrue );
	}

	const float opacity = Com_Clamp( 0.0f, 1.0f, con_opacity->value );
	vec4_t background = { 0.025f, 0.075f, 0.160f, opacity * 0.94f };
	re.SetColor( background );
	re.DrawStretchPic(
		VR_CONSOLE_LEFT,
		VR_CONSOLE_TOP,
		VR_CONSOLE_WIDTH,
		VR_CONSOLE_HEIGHT,
		0, 0, 0, 0,
		cls.whiteShader );

	Con_VrSetColorWithAlpha( console_color, animation.textAlpha );
	re.DrawStretchPic(
		VR_CONSOLE_LEFT,
		VR_CONSOLE_TOP,
		VR_CONSOLE_WIDTH,
		2,
		0, 0, 0, 0,
		cls.whiteShader );
	re.DrawStretchPic(
		VR_CONSOLE_LEFT,
		VR_CONSOLE_TOP + VR_CONSOLE_HEIGHT - 2,
		VR_CONSOLE_WIDTH,
		2,
		0, 0, 0, 0,
		cls.whiteShader );
	Con_VrSetColorWithAlpha( console_color, animation.textAlpha );
	Con_DrawVrString(
		VR_CONSOLE_TEXT_LEFT,
		VR_CONSOLE_TOP + VR_CONSOLE_CHAR_HEIGHT,
		"JKXR CONSOLE" );
	re.DrawStretchPic(
		VR_CONSOLE_TEXT_LEFT,
		VR_CONSOLE_INPUT_Y - 10,
		VR_CONSOLE_WIDTH - 2 * ( VR_CONSOLE_TEXT_LEFT - VR_CONSOLE_LEFT ),
		1,
		0, 0, 0, 0,
		cls.whiteShader );

	int row = con.display;
	if ( con.x == 0 )
	{
		--row;
	}

	int y = VR_CONSOLE_OUTPUT_BOTTOM;
	if ( con.display != con.current )
	{
		for ( int x = 0; x < con.linewidth; x += 4 )
		{
			Con_DrawVrChar(
				VR_CONSOLE_TEXT_LEFT + x * VR_CONSOLE_CHAR_WIDTH,
				y,
				'^' );
		}
		y -= VR_CONSOLE_CHAR_HEIGHT;
	}

	int currentColor = ColorIndex( COLOR_WHITE );
	Con_VrSetColorWithAlpha( g_color_table[currentColor], animation.textAlpha );
	for ( ; y >= VR_CONSOLE_OUTPUT_TOP; y -= VR_CONSOLE_CHAR_HEIGHT, --row )
	{
		if ( row < 0 )
		{
			break;
		}
		if ( con.current - row >= con.totallines )
		{
			continue;
		}

		short *text = con.text + ( row % con.totallines ) * con.linewidth;
		for ( int x = 0; x < con.linewidth; ++x )
		{
			if ( ( text[x] & 0xff ) == ' ' )
			{
				continue;
			}
			const int color = ( text[x] >> 8 ) & Q_COLOR_BITS;
			if ( color != currentColor )
			{
				currentColor = color;
				Con_VrSetColorWithAlpha(
					g_color_table[currentColor], animation.textAlpha );
			}
			Con_DrawVrChar(
				VR_CONSOLE_TEXT_LEFT + x * VR_CONSOLE_CHAR_WIDTH,
				y,
				text[x] & 0xff );
		}
	}

	Con_VrSetColorWithAlpha( g_color_table[ColorIndex( COLOR_WHITE )], animation.textAlpha );
	Con_DrawVrInput( VR_CONSOLE_TEXT_LEFT, VR_CONSOLE_INPUT_Y );
	Con_DrawVrKeyboard( animation );

	if ( vrConsolePhase == VR_CONSOLE_OPENING )
	{
		const float lineAlpha = Com_Clamp(
			0.0f, 1.0f, 1.4f - animation.openAmount * 2.0f );
		vec4_t lineColor = { 0.20f, 0.95f, 1.0f, lineAlpha };
		re.SetColor( lineColor );
		re.DrawStretchPic(
			VR_CONSOLE_LEFT,
			VR_CONSOLE_TOP + VR_CONSOLE_HEIGHT * 0.5f - 1.0f,
			VR_CONSOLE_WIDTH,
			2.0f,
			0, 0, 0, 0,
			cls.whiteShader );
	}
	if ( vrConsolePhase == VR_CONSOLE_OPEN )
	{
		Con_DrawVrPointers();
	}
	re.SetColor( NULL );
	if ( re.VR_SetConsoleMode != nullptr )
	{
		re.VR_SetConsoleMode( qfalse );
	}
}



/*
==================
Con_DrawConsole
==================
*/
void Con_DrawConsole( void ) {
	// check for console width changes from a vid mode change
	Con_CheckResize ();
	if ( Con_UseVrLayout() )
	{
		Con_VrUpdatePhase();
		if ( Con_VrPhaseVisible() )
		{
			Con_DrawVrConsole();
			return;
		}
		if ( re.VR_SetSpatialConsoleState != nullptr )
		{
			re.VR_SetSpatialConsoleState( qfalse, 0.0f, 0.0f, 0.0f );
		}
	}

	// if disconnected, render console full screen
	if ( cls.state == CA_DISCONNECTED ) {
		if ( !( Key_GetCatcher( ) & KEYCATCH_UI) ) {
			if ( Con_UseVrLayout() )
			{
				Con_DrawVrConsole();
			}
			else
			{
				Con_DrawSolidConsole( 1.0 );
			}
			return;
		}
	}

	if ( con.displayFrac ) {
		if ( Con_UseVrLayout() )
		{
			Con_DrawVrConsole();
		}
		else
		{
			Con_DrawSolidConsole( con.displayFrac );
		}
	} else {
		// draw notify lines
		if ( cls.state == CA_ACTIVE && con_drawnotify->integer ) {
			Con_DrawNotify ();
		}
	}
}

//================================================================

/*
==================
Con_RunConsole

Scroll it up or down
==================
*/
void Con_RunConsole (void) {
	if ( Con_UseVrLayout() )
	{
		Con_VrUpdatePhase();
		con.finalFrac = Con_VrPhaseVisible() ? 1.0f : 0.0f;
		con.displayFrac = con.finalFrac;
		return;
	}

	// decide on the destination height of the console
	if ( Key_GetCatcher( ) & KEYCATCH_CONSOLE )
		con.finalFrac = 0.5;		// half screen
	else
		con.finalFrac = 0;				// none visible

	// scroll towards the destination height
	if (con.finalFrac < con.displayFrac)
	{
		con.displayFrac -= con_conspeed->value*cls.realFrametime*0.001;
		if (con.finalFrac > con.displayFrac)
			con.displayFrac = con.finalFrac;

	}
	else if (con.finalFrac > con.displayFrac)
	{
		con.displayFrac += con_conspeed->value*cls.realFrametime*0.001;
		if (con.finalFrac < con.displayFrac)
			con.displayFrac = con.finalFrac;
	}

}


void Con_PageUp( void ) {
	con.display -= 2;
	if ( con.current - con.display >= con.totallines ) {
		con.display = con.current - con.totallines + 1;
	}
}

void Con_PageDown( void ) {
	con.display += 2;
	if (con.display > con.current) {
		con.display = con.current;
	}
}

void Con_Top( void ) {
	con.display = con.totallines;
	if ( con.current - con.display >= con.totallines ) {
		con.display = con.current - con.totallines + 1;
	}
}

void Con_Bottom( void ) {
	con.display = con.current;
}


void Con_Close( void ) {
	Field_Clear( &g_consoleField );
	Con_ClearNotify ();
	Key_SetCatcher( Key_GetCatcher( ) & ~KEYCATCH_CONSOLE );
	con.finalFrac = 0;				// none visible
	con.displayFrac = 0;
}
