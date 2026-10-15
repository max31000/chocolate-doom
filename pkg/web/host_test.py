"""End-to-end check of the Doom web build through pkg/web/host-test.html (dev only).

    python pkg/web/host_test.py --root M:/Projects \
        --page /bliss-lab/chocolate-doom/pkg/web/host-test.html \
        --module /bliss-lab/chocolate-doom/build-web/src/chocolate-doom.js \
        --wad /bliss/assets/doom/DOOM1.WAD --browser chromium --seconds 60 --shots out/

Serves --root on 127.0.0.1 (own http.server child process), opens the page HEADLESS and
muted, stubs the pointer lock (never grabs the real mouse), then: waits for "started", opens
the menu, starts a new game (E1M1, default skill), walks / turns / shoots / opens doors for
--seconds while measuring main-loop frame times, checks the AudioContext is running and the
game produces sound, host pause/resume (game clock frozen), master volume, and quits through
the menu (Quit Game -> y): "quit" event after the quit sound. Fails on console errors.
Requires: pip install playwright && playwright install chromium firefox
"""
import argparse
import json
import os
import socket
import statistics
import subprocess
import sys
import time

from playwright.sync_api import sync_playwright

INIT_SCRIPT = """
(() => {
  // Never lock the real pointer from a test.
  HTMLCanvasElement.prototype.requestPointerLock = function () { return Promise.resolve(); };
  Element.prototype.requestPointerLock = function () { return Promise.resolve(); };
  Document.prototype.exitPointerLock = function () {};

  // Tap everything connected to an AudioContext destination into an analyser:
  // window.__audioPeak() = peak |sample| since the last call.
  const orig = AudioNode.prototype.connect;
  const taps = new Map();
  let peak = 0;
  window.__audioContexts = [];
  AudioNode.prototype.connect = function (dest, ...rest) {
    if (dest instanceof AudioDestinationNode) {
      const ctx = dest.context;
      let tap = taps.get(ctx);
      if (!tap) {
        window.__audioContexts.push(ctx);
        tap = ctx.createAnalyser();
        tap.fftSize = 2048;
        const buf = new Float32Array(tap.fftSize);
        setInterval(() => {
          tap.getFloatTimeDomainData(buf);
          for (const v of buf) peak = Math.max(peak, Math.abs(v));
        }, 20);
        taps.set(ctx, tap);
      }
      orig.call(this, tap);
    }
    return orig.call(this, dest, ...rest);
  };
  window.__audioPeak = () => { const p = peak; peak = 0; return p; };

  // rAF timestamps (display frames) and rAF callback durations (the game's main loop runs
  // in requestAnimationFrame; window.__work collects durations of callbacks > 0.05 ms).
  window.__work = [];
  const origRaf = window.requestAnimationFrame.bind(window);
  window.requestAnimationFrame = (cb) => origRaf((t) => {
    const s = performance.now();
    try { cb(t); } finally {
      const d = performance.now() - s;
      if (window.__measureWork) window.__work.push(d);
    }
  });
  window.__raf = [];
  (function loop(t) { window.__raf.push(t); requestAnimationFrame(loop); })(performance.now());
})();
"""

MAINLOOP_HOOK = """
(() => {
  // Only the game's callbacks: our own __raf loop is registered before the module loads and
  // takes microseconds; filter by keeping everything and reporting the distribution.
  window.__measureWork = true;
})();
"""


COUNT_CALLS = """
(() => {
  const M = window.__doom;
  window.__calls = {};
  for (const k of ['_bliss_release_keys', '_bliss_mouse_wanted', '_bliss_pause', '_bliss_resume',
                   '_bliss_is_paused', '_bliss_set_master_volume']) {
    const f = M[k];
    M[k] = (...a) => { window.__calls[k] = (window.__calls[k] || 0) + 1; return f(...a); };
  }
})();
"""

DISPATCH_EVENTS = """
(() => {
  const c = document.getElementById('game');
  c.focus(); document.getElementById('vol').focus();
  c.dispatchEvent(new PointerEvent('pointerdown', { bubbles: true }));
  window.dispatchEvent(new KeyboardEvent('keydown', { key: 'a' }));
  Object.defineProperty(document, 'hidden', { configurable: true, get: () => true });
  document.dispatchEvent(new Event('visibilitychange'));
  delete document.hidden;
  document.dispatchEvent(new Event('visibilitychange'));
})();
"""


def stats(values):
    if not values:
        return {}
    s = sorted(values)
    q = lambda p: s[min(len(s) - 1, int(p * len(s)))]
    return {"n": len(s), "mean": round(statistics.fmean(s), 2), "p50": round(q(0.5), 2),
            "p95": round(q(0.95), 2), "p99": round(q(0.99), 2), "max": round(s[-1], 2)}


def frame_stats(ts):
    d = [b - a for a, b in zip(ts, ts[1:])]
    if not d:
        return {}
    total = (ts[-1] - ts[0]) / 1000
    out = {"fps": round(len(d) / total, 2), "seconds": round(total, 1)}
    out.update({f"frame_{k}_ms": v for k, v in stats(d).items() if k != "mean"})
    med = sorted(d)[len(d) // 2]
    out["pct_long_frames"] = round(100 * sum(1 for x in d if x > med * 1.5) / len(d), 3)
    return out


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True, help="directory served as http root")
    ap.add_argument("--page", required=True)
    ap.add_argument("--module", required=True)
    ap.add_argument("--wad", required=True)
    ap.add_argument("--browser", default="chromium", choices=["chromium", "firefox"])
    ap.add_argument("--seconds", type=int, default=60)
    ap.add_argument("--shots", default=None)
    ap.add_argument("--out", default=None, help="write the result JSON here")
    a = ap.parse_args()

    port = free_port()
    server = subprocess.Popen([sys.executable, "-m", "http.server", str(port), "--bind", "127.0.0.1",
                               "--directory", a.root], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    result = {"browser": a.browser, "ok": False}
    errors = []
    consolelog = []
    try:
        time.sleep(0.8)
        url = f"http://127.0.0.1:{port}{a.page}?module={a.module}&wad={a.wad}"
        if a.shots:
            os.makedirs(a.shots, exist_ok=True)

        def shot(page, name):
            if a.shots:
                page.locator("#game").screenshot(path=os.path.join(a.shots, f"{a.browser}-{name}.png"))

        with sync_playwright() as p:
            if a.browser == "chromium":
                browser = p.chromium.launch(headless=True, args=["--mute-audio",
                                            "--autoplay-policy=no-user-gesture-required"])
            else:
                browser = p.firefox.launch(headless=True, firefox_user_prefs={
                    "media.volume_scale": "0.0", "media.autoplay.default": 0,
                    "media.autoplay.block-webaudio": False})
            try:
                page = browser.new_page(viewport={"width": 1100, "height": 1000})
                page.add_init_script(INIT_SCRIPT)
                page.on("console", lambda m: (consolelog.append(f"{m.type}: {m.text}"),
                                              errors.append(m.text) if m.type == "error" else None))
                page.on("pageerror", lambda e: errors.append(f"pageerror: {e}"))
                t0 = time.time()
                page.goto(url)
                page.wait_for_function("window.__events && window.__events.some(e => e.type === 'started')",
                                       timeout=60000)
                result["startup_s"] = round(time.time() - t0, 2)
                started = page.evaluate("window.__events.find(e => e.type === 'started')")
                result["started"] = started["data"]
                page.evaluate(MAINLOOP_HOOK)
                page.locator("#game").click()   # focus (pointer lock is stubbed)
                page.evaluate("window.__audioPeak()")
                time.sleep(3)
                # Title screen / demo: OPL music (+ demo sound effects).
                result["title_audio_peak"] = round(page.evaluate("window.__audioPeak()"), 4)
                shot(page, "01-title")

                # Menu -> New Game -> episode 1 -> default skill ("Hurt me plenty").
                page.keyboard.press("Escape"); time.sleep(0.7)
                shot(page, "02-menu")
                for _ in range(3):
                    page.keyboard.press("Enter"); time.sleep(0.7)
                page.wait_for_function("window.__events.some(e => e.type === 'levelStarted' && !e.data.demo)",
                                       timeout=15000)
                result["level"] = page.evaluate(
                    "window.__events.filter(e => e.type === 'levelStarted' && !e.data.demo).pop().data")
                time.sleep(1.5)
                shot(page, "03-e1m1")

                page.evaluate("window.__raf = []; window.__work = []; window.__audioPeak()")
                peaks = []
                end = time.time() + a.seconds
                step = 0
                pattern = [("ArrowUp", 1.2), ("ArrowLeft", 0.35), ("Control", 0.15), ("ArrowUp", 0.8),
                           ("Space", 0.1), ("ArrowRight", 0.5), ("Control", 0.15), ("ArrowDown", 0.4)]
                while time.time() < end:
                    key, hold = pattern[step % len(pattern)]
                    page.keyboard.down(key); time.sleep(hold); page.keyboard.up(key)
                    if key == "Control":
                        time.sleep(0.25)
                    step += 1
                    if step % 8 == 0:
                        peaks.append(page.evaluate("window.__audioPeak()"))
                    if step == 24:
                        shot(page, "04-playing")
                shot(page, "05-after-play")
                raf = page.evaluate("window.__raf")
                work = page.evaluate("window.__work")
                result["frames"] = frame_stats(raf)
                result["mainloop_ms"] = stats(work)
                result["audio"] = page.evaluate(
                    "window.__audioContexts.map(c => ({state: c.state, sampleRate: c.sampleRate}))")
                result["audio_peaks"] = [round(x, 4) for x in peaks]

                # Host pause: no main loop work, game clock frozen; then resume.
                page.evaluate("window.__doom._bliss_pause()")
                time.sleep(0.3)
                n0 = page.evaluate("window.__work.length")
                time.sleep(1.0)
                n1 = page.evaluate("window.__work.length")
                paused_ok = page.evaluate("window.__doom._bliss_is_paused()") == 1
                # Frames still tick (quit/started checks) but do no game work: compare durations.
                w = page.evaluate(f"window.__work.slice({n0}, {n1})")
                result["pause"] = {"is_paused": paused_ok, "frames": n1 - n0,
                                   "max_ms": round(max(w), 3) if w else 0}
                page.evaluate("window.__doom._bliss_resume()")
                result["volume"] = [page.evaluate("window.__doom._bliss_set_master_volume(40)"),
                                    page.evaluate("window.__doom._bliss_set_master_volume(100)")]
                result["mouse_sensitivity"] = page.evaluate("window.__doom._bliss_set_mouse_sensitivity(7)")
                result["pause_events"] = page.evaluate(
                    "window.__events.filter(e => e.type === 'pauseChanged').map(e => e.data)")

                # Quit through the menu: Esc, 5 x down to "Quit Game", Enter, y.
                page.locator("#game").focus()
                page.keyboard.press("Escape"); time.sleep(0.5)
                for _ in range(5):
                    page.keyboard.press("ArrowDown"); time.sleep(0.25)
                page.keyboard.press("Enter"); time.sleep(0.6)
                shot(page, "06-quit-prompt")
                tq = time.time()
                page.keyboard.press("y")
                page.wait_for_function("window.__events.some(e => e.type === 'quit')", timeout=10000)
                result["quit_delay_s"] = round(time.time() - tq, 2)
                result["events"] = sorted({e["type"] for e in page.evaluate("window.__events")})

                # bliss_dispose: create again, check blur releases keys, dispose (twice), then no
                # pre.js listener reaches the module and no AudioContext runs; create once more.
                page.evaluate("window.__start()")
                page.wait_for_function(
                    "window.__events.filter(e => e.type === 'started').length >= 2", timeout=30000)
                time.sleep(1.5)
                page.evaluate(COUNT_CALLS)
                page.evaluate("document.getElementById('game').focus(); document.getElementById('vol').focus()")
                blur_calls = page.evaluate("window.__calls['_bliss_release_keys'] || 0")
                page.evaluate("window.__doom._bliss_dispose(); window.__doom._bliss_dispose();")
                time.sleep(0.5)
                page.evaluate("for (const k in window.__calls) window.__calls[k] = 0")
                page.evaluate(DISPATCH_EVENTS)
                time.sleep(0.5)
                calls_after = page.evaluate("Object.values(window.__calls).reduce((a, b) => a + b, 0)")
                states_after = page.evaluate("window.__audioContexts.map(c => c.state)")
                page.evaluate("window.__start()")
                page.wait_for_function(
                    "window.__events.filter(e => e.type === 'started').length >= 3", timeout=30000)
                time.sleep(1.5)
                states_third = page.evaluate("window.__audioContexts.map(c => c.state)")
                shot(page, "07-recreated")
                page.evaluate("window.__doom._bliss_dispose()")
                time.sleep(0.3)
                result["dispose"] = {
                    "blur_release_keys_calls": blur_calls,
                    "listener_calls_after_dispose": calls_after,
                    "audio_states_after_dispose": states_after,
                    "audio_states_recreated": states_third,
                    "audio_states_final": page.evaluate("window.__audioContexts.map(c => c.state)"),
                }
            finally:
                browser.close()
        result["console_errors"] = errors[:20]
        fps = result.get("frames", {}).get("fps", 0)
        result["ok"] = (not errors and fps > 30 and "quit" in result.get("events", [])
                        and result["pause"]["is_paused"]
                        and any(c["state"] == "running" for c in result["audio"])
                        and max(result["audio_peaks"] or [0]) > 0.001
                        and result["dispose"]["blur_release_keys_calls"] > 0
                        and result["dispose"]["listener_calls_after_dispose"] == 0
                        and "running" not in result["dispose"]["audio_states_after_dispose"]
                        and "running" in result["dispose"]["audio_states_recreated"]
                        and "running" not in result["dispose"]["audio_states_final"])
    except Exception:
        sys.stderr.write("\n".join(consolelog[-60:]) + "\n")
        raise
    finally:
        server.terminate()
        server.wait(5)
    text = json.dumps(result, indent=2)
    print(text)
    if a.out:
        with open(a.out, "w") as f:
            f.write(text)
    sys.exit(0 if result["ok"] else 1)


if __name__ == "__main__":
    main()
