# Web (Emscripten) build — Bliss

Branch `bliss-web` of [max31000/chocolate-doom](https://github.com/max31000/chocolate-doom)
(a plain git mirror of [chocolate-doom/chocolate-doom](https://github.com/chocolate-doom/chocolate-doom))
adds a browser build of **Chocolate Doom** (`chocolate-doom` target only) for the Bliss host page
(<https://mvv42.ru/bliss/>). It is the unchanged Chocolate Doom engine — same renderer, same
35 Hz game logic, same OPL (AdLib/Sound Blaster) music emulation — with a thin host layer:

* `src/i_web.c`, `src/i_web.h` — events to the host page, host pause, master volume.
* `src/doom/d_web.c`, `src/doom/d_web.h` — browser main loop, Doom events/functions.
* `pkg/web/pre.js`, `pkg/web/library_bliss.js` — JS side (data dir, canvas, pointer lock, audio).
* small `#ifdef __EMSCRIPTEN__` patches (no change to native builds): `D_DoomLoop` runs from
  `emscripten_set_main_loop` (requestAnimationFrame) instead of `while (1)`; `TryRunTics`
  and the screen wipe never sleep (a frame without a new tic draws nothing); the quit sound
  delay is a deferred quit; ENDOOM is skipped; `I_Quit`/`I_Error` emit `quit`/`error` and stop
  the main loop instead of `exit()`; OPL detection is skipped for the software emulator (it
  waits for the audio callback, which cannot run while the main thread waits); the window is
  not resizable/high-DPI/full screen; keyboard bound to the canvas; mouse only under pointer
  lock. No ASYNCIFY, no pthreads (no COOP/COEP needed, works in an iframe).

## Build

Requires emsdk (tested with **6.0.10**), CMake and Ninja.

```sh
source /path/to/emsdk/emsdk_env.sh
emcmake cmake -S . -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_SDL2_NET=OFF
cmake --build build-web --target chocolate-doom
# -> build-web/src/chocolate-doom.js (ES module) + build-web/src/chocolate-doom.wasm
```

Link flags of note (`src/CMakeLists.txt`): `-sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createDoom`,
`-sUSE_SDL=2 -sUSE_SDL_MIXER=2 -sSDL2_MIXER_FORMATS=[]` (music is OPL emulation, no decoders),
`-sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=64MB -sSTACK_SIZE=1MB -sENVIRONMENT=web`,
`--pre-js pkg/web/pre.js --js-library pkg/web/library_bliss.js`. No game data in the build.

Release: `.github/workflows/web-release.yml` builds in `emscripten/emsdk:6.0.10` on every push /
pull request (check + workflow artifact) and, for a tag `web-vX.Y.Z`, attaches
`doom-web-X.Y.Z.tgz` (+ `.sha256`) to the GitHub release of that tag. Archive root:
`chocolate-doom.js`, `chocolate-doom.wasm`, `COPYING.md`, `README.md`.

## Host contract

```js
import createDoom from './chocolate-doom.js';

const Module = await createDoom({
  canvas,                                   // HTMLCanvasElement (any id)
  locateFile: (name) => urlOf[name],        // 'chocolate-doom.wasm'
  blissDataDir: '/data',                    // default '/data' (MEMFS); becomes the working dir
  // blissIwad: 'DOOM1.WAD',                // IWAD file name in blissDataDir (default)
  // blissWindowSize: [960, 720],           // canvas backing store (default 960x720)
  // blissMasterVolume: 100,                // applied at start (0..100)
  // blissAutoPause: true,                  // pause while the tab is hidden (default true)
  // blissArgs: ['-skill', '4'],            // extra Chocolate Doom command line arguments
  // arguments: [...],                      // replaces the whole command line (then pass -iwad etc.)
  preRun: [(M) => {                         // /data already exists here
    M.addRunDependency('wad');              // lets the WAD download overlap the wasm download
    wadPromise.then((bytes) => {            // ArrayBuffer of DOOM1.WAD
      M.FS.writeFile('/data/DOOM1.WAD', new Uint8Array(bytes));
      M.removeRunDependency('wad');
    });
  }],
  onBlissEvent(type, json) { const data = JSON.parse(json); /* see Events */ },
});
// The promise resolves before the game has started: call _bliss_* after the "started" event.
```

Default command line: `-iwad /data/DOOM1.WAD -window -width 960 -height 720` (+ `blissArgs`).
Config and savegames live in MEMFS (`/libsdl/chocolate-doom/`): they work within a session and
are **not persisted** (a new module instance starts from defaults).

### Events (`onBlissEvent(type, json)`)

| type | json |
|---|---|
| `started` | `{"version":"Chocolate Doom 3.1.1","game":"DOOM Shareware","width":960,"height":720}` — first frame of the main loop |
| `levelStarted` | `{"episode":1,"map":1,"skill":3,"demo":false}` — skill 1..5; title demos have `demo:true` |
| `pauseChanged` | `{"paused":bool,"source":"host"\|"game"}` — `host`: bliss_pause/resume; `game`: Pause key |
| `quit` | `{}` — after Quit Game → `y` (after the quit sound, 1.5 s) or `_bliss_quit()` |
| `error` | `{"code":-1,"message":"..."}` — I_Error (e.g. missing/invalid IWAD) |

After `quit`/`error` the main loop is stopped, the audio device closed and the SDL event
listeners removed; the module stays idle (no exception to the host). Create a new module to
play again.

### Exported functions (`Module._name(...)` or `Module.ccall`)

| function | |
|---|---|
| `_bliss_pause()` / `_bliss_resume()` | host pause: no tics, game clock frozen (no catch-up afterwards), audio paused. Independent of the in-game Pause key |
| `_bliss_is_paused()` | 1 while host-paused |
| `_bliss_set_master_volume(0..100)` → int | linear gain on all game audio (sound effects + music), on top of the in-game volume sliders. `_bliss_get_master_volume()` |
| `_bliss_set_mouse_sensitivity(0..20)` → int | as Options → Mouse Sensitivity (menu slider 0..9, default 5). `_bliss_get_mouse_sensitivity()` |
| `_bliss_set_window_size(w, h)` | canvas backing store size (320x200 .. 4096x4096); adjusted to 4:3 like a native window resize |
| `_bliss_quit()` | quit on the next frame (no quit sound), emits `quit` |
| `_bliss_mouse_wanted()` | 1 while the game wants the mouse (used by pre.js) |

### Canvas, input, audio

* **Canvas**: backing store `blissWindowSize` (default **960×720** = 320×200 with Chocolate
  Doom's aspect ratio correction, 4:3). Rendering is Chocolate Doom's own: 320×200 →
  integer upscale → linear scale to the window. The engine never follows the page size; the
  host sets the displayed size with CSS (keep 4:3, e.g. `aspect-ratio: 4/3`). For a sharper
  picture on large displays call `_bliss_set_window_size(cssW*dpr, cssH*dpr)` (4:3).
  `pre.js` gives the canvas `tabindex=0` if it has none, focuses it on pointerdown and
  suppresses its context menu. `document.title` is never changed.
* **Keyboard** is bound to the canvas (`SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT="#canvas"`, text
  input off): the game sees keys only while the canvas has focus; F1..F12, arrows, Tab, Ctrl
  combos are prevented from reaching the browser then (F5 does not reload). Focus the canvas
  when the game page opens and after closing host dialogs. Alt+Enter (full screen) is ignored —
  full screen belongs to the host. Vanilla controls: arrows move/turn, Ctrl fire, Space use,
  Shift run, Alt strafe, 1..7 weapons, Tab map, Esc menu.
* **Mouse** (optional): a click on the canvas requests the pointer lock while the game wants
  the mouse (in a level, not in menus/intermissions). Mouse movement and button presses reach
  the game only while locked (cursor moves over an unlocked canvas are ignored; the click that
  requests the lock may count as a shot if the lock is granted before the game reads it); Esc
  releases it (and opens the menu),
  the game releases it when it no longer wants the mouse.
* **Audio**: SDL2 + SDL2_mixer (Emscripten ports). Sound effects are the original digitized
  sounds; music is Chocolate Doom's OPL3 emulation (Nuked OPL3) of the MUS tracks — authentic
  AdLib/Sound Blaster music (`snd_musicdevice` 3). The mixer runs at the AudioContext rate. The
  AudioContext is resumed on the first keydown/pointerdown (autoplay policy).
* **Hidden tab**: paused (`source:"host"` events) and resumed when visible, unless the host
  paused itself or `blissAutoPause: false`.

### Data

The shareware IWAD `DOOM1.WAD` v1.9 (4196020 bytes, SHA-1
`5b2e249b9c5133ec987b3ea77596381dc0d6bc1d`) is freely redistributable and is supplied by the
host. Other IWADs work too (`blissIwad`), if the host is allowed to serve them.

## Testing

`pkg/web/host-test.html` is a dev host page (`?module=<chocolate-doom.js url>&wad=<WAD url>`).
`pkg/web/host_test.py` (Python Playwright) serves a directory, opens the page **headless and
muted** (Chromium `--mute-audio`, Firefox `media.volume_scale=0.0`; pointer lock stubbed),
starts a new game from the menu, plays ~60 s with the keyboard while measuring frame times,
checks the AudioContext and audio output, host pause, volume, quit through the menu, and fails
on console errors. See the docstring for the command line.
