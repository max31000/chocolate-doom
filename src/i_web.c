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
//     Web (Emscripten) host integration shared by the games.
//     Host contract: README.Web.md.
//

#ifdef BLISS_WEB

#include <stdio.h>
#include <string.h>

#include <emscripten.h>
#include <emscripten/eventloop.h>
#include <emscripten/html5.h>

#include "SDL.h"
#ifndef DISABLE_SDL2MIXER
#include "SDL_mixer.h"
#endif

#include "i_web.h"

boolean i_web_nonblocking_loop = false;

static boolean host_paused = false;
static int master_volume = 100;
static boolean master_volume_installed = false;
static boolean quit_requested = false;
static boolean disposed = false;
static Uint32 quit_at;

void I_Web_Emit(const char *type, const char *json)
{
    EM_ASM({
        var type = UTF8ToString($0);
        var json = UTF8ToString($1);
        if (Module["blissOnEventInternal"]) Module["blissOnEventInternal"](type, json);
        var cb = Module["onBlissEvent"];
        if (typeof cb != "function") return;
        try { cb(type, json); }
        catch (e) { console.error("[bliss] onBlissEvent handler failed", e); }
    }, type, json);
}

// Writes `s` as a JSON string literal (with quotes) into buf.
static void JsonString(char *buf, size_t size, const char *s)
{
    size_t n = 0;

    if (size < 3)
    {
        return;
    }

    buf[n++] = '"';

    for (; *s != '\0' && n + 7 < size; ++s)
    {
        unsigned char c = (unsigned char) *s;

        if (c == '"' || c == '\\')
        {
            buf[n++] = '\\';
            buf[n++] = c;
        }
        else if (c == '\n')
        {
            buf[n++] = '\\';
            buf[n++] = 'n';
        }
        else if (c < 0x20)
        {
            n += snprintf(buf + n, size - n, "\\u%04x", c);
        }
        else
        {
            buf[n++] = c;
        }
    }

    buf[n++] = '"';
    buf[n] = '\0';
}

static void ExitPointerLock(void)
{
    EM_ASM({
        if (typeof document != "undefined" && document.pointerLockElement)
            document.exitPointerLock();
    });
}

void I_Web_Exit(int code, const char *message)
{
    if (code == 0)
    {
        I_Web_Emit("quit", "{}");
    }
    else
    {
        char msg[640];
        char json[700];

        JsonString(msg, sizeof(msg), message != NULL ? message : "");
        snprintf(json, sizeof(json), "{\"code\":%d,\"message\":%s}", code, msg);
        I_Web_Emit("error", json);
    }

    emscripten_cancel_main_loop();
    ExitPointerLock();
    emscripten_unwind_to_js_event_loop();
}

boolean I_Web_PointerLocked(void)
{
    EmscriptenPointerlockChangeEvent status;

    if (emscripten_get_pointerlock_status(&status) != EMSCRIPTEN_RESULT_SUCCESS)
    {
        return false;
    }

    return status.isActive != 0;
}

boolean I_Web_HostPaused(void)
{
    return host_paused;
}

void I_Web_RequestQuit(int delay_ms)
{
    Uint32 t = SDL_GetTicks() + (Uint32) (delay_ms > 0 ? delay_ms : 0);

    if (!quit_requested || (Sint32) (t - quit_at) < 0)
    {
        quit_at = t;
    }

    quit_requested = true;
}

boolean I_Web_QuitRequested(void)
{
    return quit_requested;
}

boolean I_Web_QuitDue(void)
{
    return quit_requested && (Sint32) (SDL_GetTicks() - quit_at) >= 0;
}

//
// Master volume: a gain on the final SDL_mixer output (sound effects and
// music, including the OPL music which is mixed in as a post effect).
//

#ifndef DISABLE_SDL2MIXER
static Uint16 mix_format;

static void MasterVolumePostMix(void *udata, Uint8 *stream, int len)
{
    int volume = master_volume;
    int i;

    if (volume >= 100)
    {
        return;
    }

    if (mix_format == AUDIO_S16SYS)
    {
        Sint16 *samples = (Sint16 *) stream;
        int count = len / 2;

        for (i = 0; i < count; ++i)
        {
            samples[i] = (Sint16) ((samples[i] * volume) / 100);
        }
    }
    else if (mix_format == AUDIO_F32SYS)
    {
        float *samples = (float *) stream;
        int count = len / 4;
        float gain = volume / 100.0f;

        for (i = 0; i < count; ++i)
        {
            samples[i] *= gain;
        }
    }
}
#endif

void I_Web_InstallMasterVolume(void)
{
#ifndef DISABLE_SDL2MIXER
    int freq, channels;

    if (master_volume_installed)
    {
        return;
    }

    if (Mix_QuerySpec(&freq, &mix_format, &channels) == 0)
    {
        return;  // mixer not open (sound disabled)
    }

    Mix_SetPostMix(MasterVolumePostMix, NULL);
    master_volume_installed = true;
#endif
}

//
// Exported to the host page (Module._bliss_* / ccall).
//

// Host pause: stops the game (no tics, the clock is frozen) and the audio.
// Independent of the in-game pause (Pause key), which keeps working.
EMSCRIPTEN_KEEPALIVE int bliss_pause(void)
{
    if (!host_paused)
    {
        host_paused = true;
        I_SetTimerPaused(true);
        ExitPointerLock();
#ifndef DISABLE_SDL2MIXER
        Mix_PauseAudio(1);
#endif
        I_Web_Emit("pauseChanged", "{\"paused\":true,\"source\":\"host\"}");
    }

    return 1;
}

EMSCRIPTEN_KEEPALIVE int bliss_resume(void)
{
    if (host_paused)
    {
        host_paused = false;
        I_SetTimerPaused(false);
#ifndef DISABLE_SDL2MIXER
        Mix_PauseAudio(0);
#endif
        I_Web_Emit("pauseChanged", "{\"paused\":false,\"source\":\"host\"}");
    }

    return 0;
}

EMSCRIPTEN_KEEPALIVE int bliss_is_paused(void)
{
    return host_paused ? 1 : 0;
}

// 0..100, linear gain on everything the game plays. Returns the new value.
EMSCRIPTEN_KEEPALIVE int bliss_set_master_volume(int percent)
{
    if (percent < 0)
    {
        percent = 0;
    }
    else if (percent > 100)
    {
        percent = 100;
    }

    master_volume = percent;
    I_Web_InstallMasterVolume();

    return master_volume;
}

EMSCRIPTEN_KEEPALIVE int bliss_get_master_volume(void)
{
    return master_volume;
}

// Releases all held keys (SDL key-up events for every pressed key). pre.js
// calls it when the canvas loses focus: the key-ups would go elsewhere and
// the keys would stay held in the game.
EMSCRIPTEN_KEEPALIVE void bliss_release_keys(void)
{
    if (!disposed && SDL_WasInit(SDL_INIT_VIDEO))
    {
        SDL_ResetKeyboard();
    }
}

// Tears the module down for good: stops the main loop, closes the audio
// device and its AudioContext (Mix_CloseAudio + SDL_Quit, which also
// removes SDL's event handlers), exits the pointer lock and removes the
// pre.js listeners. No event is emitted. Idempotent; the module is
// unusable afterwards (create a new one to play again).
EMSCRIPTEN_KEEPALIVE void bliss_dispose(void)
{
    int i;

    if (disposed)
    {
        return;
    }

    disposed = true;
    emscripten_cancel_main_loop();
    ExitPointerLock();
#ifndef DISABLE_SDL2MIXER
    {
        int freq, channels;
        Uint16 format;

        for (i = 0; i < 8 && Mix_QuerySpec(&freq, &format, &channels) != 0; ++i)
        {
            Mix_CloseAudio();
        }
    }
#endif
    SDL_Quit();
    EM_ASM({
        if (Module["blissDisposeInternal"]) Module["blissDisposeInternal"]();
    });
}

boolean I_Web_Disposed(void)
{
    return disposed;
}

// Quits the game on the next frame (no quit sound / delay): runs the exit
// functions, closes the audio device and emits "quit".
EMSCRIPTEN_KEEPALIVE void bliss_quit(void)
{
    I_Web_RequestQuit(0);
}

#endif // BLISS_WEB
