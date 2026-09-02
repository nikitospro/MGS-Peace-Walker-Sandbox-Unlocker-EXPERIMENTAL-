small addition - nexus mods considers my mod an virus, i added source code so nexus mods can read it, i don't hide anything
yall can take my source code and copy it everywhere you want, i don't care



# Peace Walker Sandbox Unlocker v0.8 — Experimental Compatibility Build

Experimental cross-version build for **METAL GEAR SOLID: Peace Walker - Master Collection Version (PC)**.

This is the sandbox/toybox side project: it makes the real weapons, items, ammo, camos and uniforms available without waiting for normal Mother Base development. It intentionally does **not** complete missions, alter story progression, staff, ZEKE or AI boards.

## Why this build exists

Older releases were locked to one exact executable build and used hard-coded RVAs. A game update could move those functions even if Konami did not change the underlying Peace Walker save structures.

v0.8 removes the exact-EXE restriction. At runtime it:

1. parses the loaded PE image;
2. signature-scans for Peace Walker's save-state accessor;
3. signature-scans and classifies the read-only inventory/R&D metadata helpers;
4. waits for a loaded save;
5. validates the 24-byte availability table using several known content anchors, including Stealth Gun specs, Jungle Fatigues and FOX;
6. only after validation passes, applies the sandbox state.

If signatures are missing/ambiguous or the save layout does not match, the mod **fails closed and writes nothing**.

## Compatibility

- Launch executable (SHA-256 `540ccbfd3bbd697021203973005dafffe356c5dabfdba84ce0b1c073b0a0a416`) — scanner validated against this binary; previous fixed-address releases were live-tested on Steam Deck/Proton.
- Peace Walker Ver. 1.3.1 / Steam build 25052315 (September 2, 2026) — **UNTESTED / EXPERIMENTAL**. This build was made specifically to survive address shifts, but we did not have this executable available while building it.
- Future updates — unknown. The plugin will refuse to modify state if its signatures/layout checks stop matching.

Please report successful or failed game versions and attach `PWSandboxUnlocker.log`.

## Installation — Steam Deck / Proton

If Ultimate ASI Loader is already working for Peace Walker:

1. Put `PWSandboxUnlocker.asi` beside the actual `METAL GEAR SOLID PEACE WALKER.exe`.
2. Keep your working `winmm.dll` Ultimate ASI Loader proxy beside the EXE.
3. Steam launch option:

   `WINEDLLOVERRIDES="winmm=n,b" %command%`

4. Launch normally, load a save, enter Mother Base and wait a few seconds.
5. Check Mission Prep -> Weapons / Items / Uniform.

Do not run multiple `PWSandboxUnlocker.asi` versions at once.

## Logs

The plugin creates `PWSandboxUnlocker.log` beside the game executable.

Useful outcomes include:

- `EXPERIMENTAL signature-scanning build loaded` — code signatures were found.
- `layout validation PASSED` — the known Peace Walker save structures matched before any write.
- `SUCCESS` — sandbox state was applied.
- `signature ... not unique/found` — unsupported build; nothing modified.
- `layout ... failed` — unsupported structural change; writes stop/fail closed.

## Safety / scope

Back up your save before using an experimental build. The mod intentionally makes large permanent changes to equipment availability/development state.

It does not include or modify the game executable, does not ship Konami assets, and does not interact with Steam DRM, licensing or networking.

## Source

Source is included under `source/PWSandboxUnlocker.cpp` and released under the MIT License.
