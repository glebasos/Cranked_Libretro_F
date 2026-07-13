# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A Windows/MSVC port of **Cranked** (a Playdate console emulator, originally by TheLogicMaster) focused on its **libretro core** for RetroArch, plus the SDL2/ImGui **desktop app** with debugging views. Lua games run well: graphics, text, sprites, file APIs, and sample/file-player audio all work. Native (ARM/pdex.bin) games use unicorn emulation and are less tested. Synth/instrument/sequence audio sources are still silent stubs.

## Building (Windows/MSVC)

One-time setup from a fresh clone: `scripts\setup_windows.bat` — fetches submodules (must use `--checkout --force`; the `update = merge` in .gitmodules breaks shallow fetches), clones standalone asio into `core/libs/asio_standalone`, and applies the `patches/*.patch` files to the `ffi`, `unicorn`, and `lua54` submodules. **The submodule patches are not committed inside the submodules** — after any submodule re-checkout, re-apply them or the build breaks (link collision on `crc32`, MASM failures in libffi).

Then, from a VS x64 developer environment (vcvars64):

```bat
:: Stage zlib once (libzip does find_package(ZLIB); there is no system zlib on Windows)
cmake -S core/libs/zlib -B <deps>/build-zlib -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=<deps>/zlib
cmake --build <deps>/build-zlib --target install

:: Configure + build the libretro core (RelWithDebInfo keeps PDBs for the crash handler's stack traces)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSKIP_TESTING=ON -DSKIP_JAVA=ON -DSKIP_DESKTOP=ON -DUSE_CAPSTONE=OFF -DZLIB_ROOT=<deps>/zlib -DZLIB_USE_STATIC_LIBS=ON -DUNICORN_ARCH=arm -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build --target cranked_libretro    :: -> build/libretro/cranked_libretro.dll
```

Desktop app: add `-DSKIP_DESKTOP=OFF -DSDL2_DIR=<SDL2-devel-VC>/cmake`, target `cranked_desktop`, and put `SDL2.dll` next to the exe. It takes a `.pdx` directory as its argument (not zips). Keys: arrows = d-pad, **A = Playdate B**, **S = Playdate A**. It writes `imgui.ini` and `appData/` (game saves) into its working directory.

## Testing changes against real games

The core loads `.pdx` directories and `.pdx.zip` (extracts to %TEMP%, auto-cleans). Headless verification loop with RetroArch:

```
retroarch -L cranked_libretro.dll game.pdx --max-frames=300 --max-frames-ss --max-frames-ss-path=shot.png
```

Gotchas learned the hard way:
- **RetroArch pauses when unfocused** — headless runs freeze after one frame unless `pause_nonactive = "false"` is set (use `--appendconfig`).
- `log_cb` output needs `--verbose --log-file`; core `fprintf(stderr, …)` always reaches the console.
- Keyboard in RetroArch: **X = Playdate B, Z = Playdate A** (RetroPad B→PD A, A→PD B in `libretro/core.cpp updateInputs`).
- Synthetic OS keypresses don't reach RetroArch, so input callbacks can't be exercised interactively in automated runs. Instead set `CRANKED_AUTOTEST=1`: the core presses A around frame 120 and spins the crank from frame 240 (see `updateInputs`).

Debugging toolkit already wired in:
- `libretro/core.cpp` installs a vectored exception handler printing dbghelp-symbolized stack traces on access violations (works because builds are RelWithDebInfo).
- The lua54 patch makes `LUAI_TRY` print swallowed C++ exception messages (`LUAI_TRY native exception: …`) — otherwise Lua-boundary errors surface only as "Native exception" with no detail.
- The desktop app's main loop prints `Uncaught emulator error: …` for anything the emulator throws.
- To read a game's logic: unpack `.pdz` with the pdz tool from the playdate-reverse-engineering repo, then decompile `.luac` with scratchminer's unluac fork — output is near-perfect Lua.

## Architecture

- `core/Cranked.{hpp,cpp}` — top-level emulator object; frontends drive it via `update()`. Input edges (`pressedInputs`) are computed here from `currentInputs`, which frontends set each frame.
- Two execution engines: `LuaEngine`/`LuaRuntime` (patched Lua 5.4 running game bytecode + `core/resources/runtime.lua` CoreLibs shim) and `NativeEngine` (unicorn ARM emulation for `pdex.bin`, libffi for emulated→host calls). Games may use either or both.
- `Rom.cpp` parses all Playdate formats (pdz/pdi/pdt/pft/pda). System assets are embedded via `scripts/embed_resource.py` into `core/gen/*.hpp` at build time (needs Python).
- `core/gen/PlaydateAPI.hpp` + `PlaydateFunctionMappings.cpp` — generated C API bindings shared by both engines. `LuaRuntime.cpp` holds the Lua-side wrappers.
- `HeapAllocator` — one 4096-aligned block acting as the emulated heap; host↔virtual addresses translate at engine boundaries.
- `Audio` (`core/Audio.{hpp,cpp}`) — `Audio::sampleAudio` mixes `mainChannel` + `channels`; `playSampleAudio` implements SamplePlayer/FilePlayer playback. FilePlayer buffers whole files via `loadSample` (no streaming). The libretro core pumps 735 frames per 60fps video frame at 44100 Hz; the desktop app uses SDL queue-mode audio from the main thread. **Everything is single-threaded** — audio mixing mutates player state and must stay on the emulator thread.
- `libretro/core.cpp` — entire libretro port: content extraction, XRGB8888 framebuffer, input/crank mapping, core options, audio pump, crash handler.

## Semantics that are easy to get wrong (bugs fixed here; don't regress)

- `clearClipRect` resets the clip to **full bitmap bounds**; an empty rect silently discards all drawing (`drawPixel` tests `clipRect.contains`).
- Format magic checks are `strncmp` with ident length — file payloads follow the 12-byte magic with no NUL, so `strcmp` always mismatches (this made audio never load).
- `.pda` header: uint24 sample rate at offset 12, uint8 format at 15. ADPCM blocks start after a uint16 blockSize word; the per-block predictor is signed int16; step must be seeded from the header's stepIndex.
- Playback repeat semantics: 0 = loop forever, -1 = ping-pong, N = play N times. **Lua `play()` with no args means 1** — a nil→0 coercion loops forever, and games that poll `isPlaying()` then hang (this froze whole games on intro screens).
- Lua `playdate.file.*` APIs tolerate nil paths (device behavior); throwing kills the game's top-level script.
- `import` is include-once: a module must be registered as loaded **before** its body executes, so circular imports are no-ops (as on device) instead of infinite recursion.
- Lua callbacks (`invokeLuaCallback`/`invokeLuaInputCallback`) must use `lua_pcall`, never `lua_call` — an error in a game's input handler otherwise reaches the Lua panic handler and aborts the process.
- `setDitherPattern` semantics (implemented in `drawPixel`'s `DitherColor` case): black pixels on transparent — white pixels on transparent if the drawing color was **white** when set — and **alpha is inverted for both colors**: 1.0 = transparent, 0 = opaque (verified against RootBear on hardware: beer at 0.25 renders ~75% black, foam at 0.9 nearly white). Before this was implemented, dithered fills rendered solid black (RootBear's glass). `BayerTable<N>`'s formula is only valid for N=8; 2x2/4x4 use explicit matrices in `ditherThreshold`.
- **Lua 5.4 `lua_tointeger` returns 0 for floats with fractional parts** — `LuaVal::getIntField`/`getIntElement` and `getPolygonPoints` convert via `lua_tonumber` + cast. Before this fix, fractional polygon coords collapsed to 0 and RootBear's pour stream drew at the sprite's left edge instead of under the tap (CoreLibs `drawSineWave` → `transformPolygon` produces fractional coords).
- **Never register raw C-API `int32` predicates to Lua** — the wrapper pushes an integer and `0` is truthy in Lua. `isCrankDocked`, `getFlipped`, `getReduceFlashing`, `shouldDisplay24HourTime`, sprite `isVisible`/`updatesEnabled`/`collisionsEnabled`, and sound `isPlaying`/`didUnderrun` use `_lua` bool wrappers. Symptom: the "Use the crank!" indicator always visible, drawn flipped at top-left (`if playdate.getFlipped()` always true).
- `gfx.setFont` also accepts a font **path string** (undocumented device behavior; falls back to `asUserdataObject` for font objects) — RootBear's score jar relies on it.
- MSVC silently ignores C++20 parenthesized aggregate initialization of array members in ctor init lists — it compiles and never evaluates the initializers (this shipped empty system fonts with zero errors). Grep `\[[0-9]\]{};` in headers and check those members' ctors if a subsystem is mysteriously default-initialized on Windows only.
- MSVC portability already handled in-tree: no `std::aligned_alloc`/`vasprintf`/`SIGTRAP`/`uint`; `fs::path` is wide — use `.string()`/`.generic_string()` (Playdate paths want forward slashes); `/Zc:__cplusplus /utf-8 /bigobj` are required globally; `FFI_BUILDING` must be defined for libffi consumers; standalone asio (`ASIO_STANDALONE`) replaces boost::asio (real headers at `asio_standalone/include`, not `asio/include` which is a symlink-as-file on Windows).

## Known gaps

Synth/instrument/sequence audio silent; FilePlayer doesn't stream (whole file buffered); no save states; Lua-side sound finish/loop callbacks not implemented; some native games show display artifacts; encrypted Catalog games unsupported.
