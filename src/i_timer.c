//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
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
//      Timer functions.
//

#include "SDL.h"

#include "i_timer.h"
#include "i_web.h"
#include "doomtype.h"

//
// I_GetTime
// returns time in 1/35th second tics
//

static Uint32 basetime = 0;

#ifdef BLISS_WEB

// Web: the host can pause the game (bliss_pause). The game clock stands
// still while paused, so the game does not catch up on the paused time.

static boolean timer_paused = false;
static Uint32 paused_since = 0;
static Uint32 paused_total = 0;

void I_SetTimerPaused(boolean paused)
{
    if (paused && !timer_paused)
    {
        paused_since = SDL_GetTicks();
    }
    else if (!paused && timer_paused)
    {
        paused_total += SDL_GetTicks() - paused_since;
    }

    timer_paused = paused;
}

static Uint32 GetTicks(void)
{
    Uint32 ticks = timer_paused ? paused_since : SDL_GetTicks();

    // Never 0: basetime == 0 means "not set yet".
    return ticks - paused_total + 1;
}

#define SDL_GetTicks GetTicks

#endif

int  I_GetTime (void)
{
    Uint32 ticks;

    ticks = SDL_GetTicks();

    if (basetime == 0)
        basetime = ticks;

    ticks -= basetime;

    return (ticks * TICRATE) / 1000;    
}

//
// Same as I_GetTime, but returns time in milliseconds
//

int I_GetTimeMS(void)
{
    Uint32 ticks;

    ticks = SDL_GetTicks();

    if (basetime == 0)
        basetime = ticks;

    return ticks - basetime;
}

// Sleep for a specified number of ms

void I_Sleep(int ms)
{
    SDL_Delay(ms);
}

void I_WaitVBL(int count)
{
    I_Sleep((count * 1000) / 70);
}


void I_InitTimer(void)
{
    // initialize timer

    SDL_SetHint(SDL_HINT_WINDOWS_DISABLE_THREAD_NAMING, "1");

    SDL_Init(SDL_INIT_TIMER);
}

