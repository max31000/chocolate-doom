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
//     Web (Emscripten) host integration shared by the games: events to
//     the host page, host pause, master volume, non-blocking main loop.
//     See README.Web.md. Everything here exists only in Emscripten builds.
//

#ifndef __I_WEB__
#define __I_WEB__

#ifdef __EMSCRIPTEN__

#include "doomtype.h"

// When true, TryRunTics() returns instead of sleeping while it waits for
// the next tic (the browser main loop calls it again on the next frame).
// Set by games that drive D_RunFrame() from emscripten_set_main_loop().
extern boolean i_web_nonblocking_loop;

// Calls Module.onBlissEvent(type, json) on the host page.
void I_Web_Emit(const char *type, const char *json);

// Stops the browser main loop and leaves the runtime idle (no exit of the
// Wasm runtime, no exception to the host). Emits "quit" (code 0) or
// "error" {message}. Does not return.
void I_Web_Exit(int code, const char *message) NORETURN;

// True while the mouse pointer is locked to the game canvas.
boolean I_Web_PointerLocked(void);

// Installs the master volume (SDL_mixer post-mix gain) once the mixer is
// open. Safe to call repeatedly.
void I_Web_InstallMasterVolume(void);

// Host pause (bliss_pause): game frames must not run while true.
boolean I_Web_HostPaused(void);

// Deferred quit (the quit sound plays first): I_Quit() runs on the first
// frame after `delay_ms` milliseconds. Quit requested by the host
// (bliss_quit) uses delay 0.
void I_Web_RequestQuit(int delay_ms);
boolean I_Web_QuitDue(void);
boolean I_Web_QuitRequested(void);

// i_timer.c: freezes I_GetTime()/I_GetTimeMS() while paused, so the game
// does not try to catch up on the paused time afterwards.
void I_SetTimerPaused(boolean paused);

#endif // __EMSCRIPTEN__

#endif
