// Emscripten --pre-js of the Doom web build (MODULARIZE + EXPORT_ES6, factory `createDoom`).
// `Module` here is the object the host passed to createDoom({...}). Contract: README.Web.md.
//
// - Data directory (`Module.blissDataDir`, default "/data"): created before the host's preRun
//   callbacks run, so they can FS.writeFile() the IWAD into it; it becomes the working
//   directory. The IWAD is `blissIwad` (default "DOOM1.WAD") in that directory.
// - Command line: built from the options unless the host passes `arguments` itself.
// - Canvas: focusable (keyboard input is bound to it), no context menu. Its backing store is
//   `blissWindowSize` (default 960x720, 4:3 = 320x200 with aspect ratio correction); the host
//   scales it with CSS.
// - Mouse: a click on the canvas requests the pointer lock while the game wants the mouse
//   (playing a level); Esc / leaving the level releases it. Keyboard play needs no mouse.
// - Audio: resumes the SDL2 AudioContext on the first user gesture (autoplay policy).
// - Hidden tab: the game is paused (bliss_pause) and resumed when visible again, unless
//   `blissAutoPause: false` or the host paused it itself.
(function () {
  var dataDir = Module['blissDataDir'] || '/data';
  Module['blissDataDir'] = dataDir;

  if (!Module['arguments']) {
    var size = Module['blissWindowSize'] || [960, 720];
    var args = ['-iwad', dataDir + '/' + (Module['blissIwad'] || 'DOOM1.WAD'),
                '-window', '-width', String(size[0]), '-height', String(size[1])];
    Module['arguments'] = args.concat(Module['blissArgs'] || []);
  }

  var preRun = Module['preRun'] || [];
  if (typeof preRun === 'function') preRun = [preRun];
  preRun.unshift(function () {
    // SDL addresses the canvas as "#canvas" (window + keyboard element); map it to the host's
    // canvas element whatever its id is.
    if (typeof specialHTMLTargets !== 'undefined' && Module['canvas'])
      specialHTMLTargets['#canvas'] = Module['canvas'];
    FS.mkdirTree(dataDir);
  });
  // After the host put the files in place.
  preRun.push(function () { FS.chdir(dataDir); });
  Module['preRun'] = preRun;

  // ---- events -----------------------------------------------------------------------------
  // Called by the game (I_Web_Emit) before the host's onBlissEvent.
  Module['blissOnEventInternal'] = function (type) {
    if (type === 'started') {
      Module['blissStarted'] = true;
      if (typeof Module['blissMasterVolume'] === 'number')
        Module['_bliss_set_master_volume'](Module['blissMasterVolume']);
      if (typeof document !== 'undefined' && document.hidden) autoPause(true);
    } else if (type === 'quit' || type === 'error') {
      Module['blissStarted'] = false;
      Module['blissExited'] = true;
    }
  };

  // ---- auto pause in hidden tabs ----------------------------------------------------------
  var autoPaused = false;
  function autoPause(hidden) {
    if (Module['blissAutoPause'] === false || !Module['blissStarted']) return;
    if (hidden) {
      if (!Module['_bliss_is_paused']()) { Module['_bliss_pause'](); autoPaused = true; }
    } else if (autoPaused) {
      autoPaused = false;
      Module['_bliss_resume']();
    }
  }
  if (typeof document !== 'undefined') {
    document.addEventListener('visibilitychange', function () { autoPause(document.hidden); });
  }

  // ---- canvas / input ---------------------------------------------------------------------
  function setupCanvas() {
    var canvas = Module['canvas'];
    if (!canvas || canvas.__blissSetup) return;
    canvas.__blissSetup = true;
    if (!canvas.hasAttribute('tabindex')) canvas.setAttribute('tabindex', '0');
    canvas.addEventListener('contextmenu', function (e) { e.preventDefault(); });
    canvas.addEventListener('pointerdown', function () {
      canvas.focus({ preventScroll: true });
      if (Module['blissStarted'] && Module['_bliss_mouse_wanted'] && Module['_bliss_mouse_wanted']() &&
          document.pointerLockElement !== canvas && canvas.requestPointerLock) {
        try {
          var p = canvas.requestPointerLock();
          if (p && p.catch) p.catch(function () { /* denied: keyboard play still works */ });
        } catch (e) { /* not supported */ }
      }
    });
  }
  setupCanvas();

  function resumeAudio() {
    var sdl2 = Module['SDL2'];
    var ctx = sdl2 && sdl2.audioContext;
    if (ctx && ctx.state === 'suspended') ctx.resume();
  }
  if (typeof window !== 'undefined') {
    ['keydown', 'pointerdown', 'touchstart'].forEach(function (type) {
      window.addEventListener(type, resumeAudio, { capture: true, passive: true });
    });
  }
})();
