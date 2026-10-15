# Chocolate Doom — web build

`chocolate-doom.js` (ES module, default export `createDoom`) + `chocolate-doom.wasm`, built with
Emscripten 6.0.10 from branch `bliss-web` of https://github.com/max31000/chocolate-doom
(Chocolate Doom by Simon Howard and contributors, GNU GPL v2 — see `COPYING.md`; source code at
the URL above).

No game data is included: the host page writes an IWAD (e.g. the freely redistributable
shareware `DOOM1.WAD` v1.9) into `/data` in `preRun`. Host contract (options, events, exported
functions, canvas/input/audio notes): `README.Web.md` in the repository.

```js
import createDoom from './chocolate-doom.js';
const Module = await createDoom({
  canvas,
  locateFile: (name) => new URL(name, import.meta.url).href,
  preRun: [(M) => { M.FS.writeFile('/data/DOOM1.WAD', wadBytes); }],
  onBlissEvent: (type, json) => console.log(type, JSON.parse(json)),
});
```
