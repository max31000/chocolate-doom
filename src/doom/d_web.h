//
// Copyright(C) 2026 Bliss contributors
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//     Doom web (Emscripten) build: browser main loop and host events.
//

#ifndef __D_WEB__
#define __D_WEB__

#ifdef __EMSCRIPTEN__

#include "doomtype.h"

void D_RunFrame(void);

// Runs D_RunFrame() from emscripten_set_main_loop (requestAnimationFrame)
// and does not return.
void D_Web_StartMainLoop(const char *description) NORETURN;

#endif

#endif
