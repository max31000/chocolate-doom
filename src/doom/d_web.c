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
//     Doom web (Emscripten) build: browser main loop, host events and
//     Doom-specific host functions. Host contract: README.Web.md.
//

#ifdef BLISS_WEB

#include <stdio.h>

#include <emscripten.h>
#include <emscripten/eventloop.h>

#include "SDL.h"

#include "config.h"
#include "doomstat.h"
#include "i_system.h"
#include "i_web.h"
#include "d_web.h"

static const char *game_description = "";
static boolean started = false;
static int last_levelstarttic = -1;
static boolean last_paused = false;

static void EmitStarted(void)
{
    char json[256];
    int w = 0, h = 0;
    SDL_Window *window = SDL_GetWindowFromID(1);  // the game window

    if (window != NULL)
    {
        SDL_GetWindowSize(window, &w, &h);
    }

    // Both strings are fixed ASCII without quotes (PACKAGE_STRING and the
    // IWAD description from d_main.c).
    snprintf(json, sizeof(json),
             "{\"version\":\"%s\",\"game\":\"%s\",\"width\":%d,\"height\":%d}",
             PACKAGE_STRING, game_description, w, h);
    I_Web_Emit("started", json);
}

static void CheckGameEvents(void)
{
    char json[128];

    if (gamestate == GS_LEVEL && levelstarttic != last_levelstarttic)
    {
        last_levelstarttic = levelstarttic;
        snprintf(json, sizeof(json),
                 "{\"episode\":%d,\"map\":%d,\"skill\":%d,\"demo\":%s}",
                 gameepisode, gamemap, (int) gameskill + 1,
                 demoplayback ? "true" : "false");
        I_Web_Emit("levelStarted", json);
    }

    if (paused != last_paused)
    {
        last_paused = paused;
        I_Web_Emit("pauseChanged", paused ? "{\"paused\":true,\"source\":\"game\"}"
                                          : "{\"paused\":false,\"source\":\"game\"}");
    }
}

static void Frame(void)
{
    if (I_Web_Disposed())
    {
        emscripten_cancel_main_loop();
        return;
    }

    if (I_Web_QuitDue())
    {
        I_Quit();
    }

    if (!started)
    {
        started = true;
        I_Web_InstallMasterVolume();
        EmitStarted();
    }

    // Waiting for the quit sound to finish: the picture stands still, as
    // in the original (which blocked in I_WaitVBL).
    if (I_Web_HostPaused() || I_Web_QuitRequested())
    {
        return;
    }

    D_RunFrame();
    CheckGameEvents();
}

void D_Web_StartMainLoop(const char *description)
{
    if (description != NULL)
    {
        game_description = description;
    }

    // fps 0: requestAnimationFrame. Returns to the browser right away.
    emscripten_set_main_loop(Frame, 0, 0);
    emscripten_unwind_to_js_event_loop();
}

//
// Exported to the host page.
//

// Mouse sensitivity as in Options -> Mouse sensitivity (0..9 on the menu
// slider; values up to 20 work). Returns the new value.
EMSCRIPTEN_KEEPALIVE int bliss_set_mouse_sensitivity(int value)
{
    if (value < 0)
    {
        value = 0;
    }
    else if (value > 20)
    {
        value = 20;
    }

    mouseSensitivity = value;

    return mouseSensitivity;
}

EMSCRIPTEN_KEEPALIVE int bliss_get_mouse_sensitivity(void)
{
    return mouseSensitivity;
}

#endif // BLISS_WEB
