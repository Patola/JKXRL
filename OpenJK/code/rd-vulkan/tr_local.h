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

#include "../qcommon/q_shared.h"
#include "../qcommon/qfiles.h"
#include "../qcommon/ojk_saved_game_helper.h"
#include "../rd-common/tr_common.h"
#include "../rd-common/tr_public.h"
#include "../rd-common/mdx_format.h"

extern cvar_t *se_language;
extern cvar_t *com_buildScript;

qhandle_t RE_RegisterShaderNoMip( const char *name );
void RE_SetColor( const float *color );
void RE_StretchPic(
	float x, float y, float w, float h,
	float s1, float t1, float s2, float t2,
	qhandle_t shader );
