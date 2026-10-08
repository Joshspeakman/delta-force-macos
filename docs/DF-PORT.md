# Delta Force 1 & 2: port notes

State as of 2026-10-08. The port was developed on Linux (CachyOS, SDL2/OpenGL host), comparing against the original
executables running under Proton, then built and checked on an Apple Silicon Mac (Metal host): see "Verified on the
Mac".

## The executables

| | Delta Force | Delta Force 2 |
|---|---|---|
| Steam app / build | 32620 / 19311 | 32630 / 19312 |
| File, SHA-256 (as shipped) | `Df.exe` 307bd095… | `Df2.exe` 0765acdf… |
| Compiler | Watcom C/C++ 10.x (sections `AUTO`, `DGROUP`, `.bss`; register calling convention) | MSVC 5 (`.text`) + MASM (`_text`, `_TEXT`) |
| Wrapper | SteamStub 2.1 | SteamStub 2.1 |
| DLLs recompiled with it | `Netsock.dll` (LAN), `Cmprssh0.dll` (rebased 0x11000000) | same + `Hw_d3d.dll` (Direct3D driver, rebased 0x12000000) |
| Stubbed | DirectPlay probe, MIDI, joystick | + `Netcomm.dll` (NovaWorld voice), `iBrowse.dll` (NovaWorld web pages) |

`recomp/tools/steamstub21.py` undoes SteamStub 2.1 on a copy in the build folder (the header is found from the stub's
first instructions, XOR-chained; the payload holds the XTEA-encrypted SteamDRMP.dll, whose code says where the AES key,
IV and stolen bytes of `.text`/`AUTO` sit). Format as documented by the Steamless project; the code is our own.

## What was specific to these games

**Self-modifying renderers.** Both games' MMX span renderers (the voxel terrain) rewrite their own instructions every
frame:
- *Patched fields*: immediates and displacements assembled as `0x1234567` placeholders, written with absolute-address
  stores. `lift.py smc_scan` finds every such store into code and `emit_smc` reads the field from guest memory when the
  instruction runs (4-byte fields through a marker value; 1-byte fields as a dispatch over all 256 values).
  `work/<game>/smc.json` lists them.
- *Code slots*: multiply-by-constant sequences copied in from tables (8-byte `lea`/`shl`/`neg` entries, 64 of them,
  the index signed so half sit below the table's base) followed by a 3-byte shift copied from a second table.
  `find_slots` recognises the writers and `emit_slot` lifts each slot as a dispatch on its current bytes.
- `W32_SMCLOG=1` reports, at every presented frame, any code byte that changed at run time — compare with smc.json to
  find a store the static scan missed (most hits are data kept inside Watcom's code section).

**Watcom.** VirtualSize 0 and a raw-size-only `.bss` (pe.py, loader.c); the relocation table seeds the analysis (the
C runtime's init tables are 6-byte entries at 2 mod 4); `repnz movsd` (F2 A5) is a rep copy — Capstone drops that F2,
lift.py reads the prefix bytes itself; segment register moves are no-ops in the flat model; one `call` jumps over an
inline string ("MU Block") to push its address.

**DF2.** MSVC two-level jump tables whose bound is compared in a stack slot, found from the instructions the function
walk decoded (not a backward decode); interpreter handler tables inside `_TEXT` seed the analysis.

**CPU.** DF1 refuses to start without MMX: cpuid reports a Pentium MMX for both games (`rt_cpuid_mmx`, DF_EDITION).
MMX `packsswb/packuswb/packssdw` needed their 64-bit forms. `tests/fuzz_sse.py` passes every MMX form both games use.

**Graphics.** DirectDraw comes from the Diablo II port (`runtime/win32/ddraw_pal.c`: 8/16/32-bit surfaces, palettes,
DirectX 6 `IDirectDraw4` + `IDirect3D3`). Added here: `IDirect3D3::FindDevice`; **depth buffers the game fills with the
CPU** — DF2's hardware driver draws the voxel terrain in software, depth included, then its Direct3D polygons against
it — sent with the new `DSR_DEPTH_UPLOAD` command (GL: `glTexSubImage2D` into the depth-stencil texture; Metal: a float
texture written as depth by a full-screen pass, `fs_zupload`); windowed modes draw with `StretchDIBits` /
`SetDIBitsToDevice` (df_imports.c), which become the frame on screen.

**Input.** Both games measure mouse-look between WM_MOUSEMOVEs and re-centre with SetCursorPos, so SetCursorPos posts the
WM_MOUSEMOVE Windows would; mouse messages carry client coordinates; DF2 makes a new main window when it changes driver
and the host follows it (Linux host and Mac host).

**Audio.** One streaming DirectSound buffer that the game mixes into from a multimedia timer; works as is. DF2's MSACM
calls (IMA and Microsoft ADPCM) are decoded in df_imports.c.

## Delta Force 1: Full Screen Hardware

DF1 shipped software-only; this port adds a hardware mode built the way DF2's Direct3D driver works
(`runtime/win32/df_hw.c`). The voxel terrain and its 16-bit depth are still drawn by the CPU, exactly as before;
everything else 3D (soldiers, buildings, trees, vehicles, effects, the first-person weapon) is drawn by the GPU with
filtered, mipmapped textures, depth-tested against the terrain's depth.

- **One entry point.** Every model polygon reaches `0x462df0(flags, verts, material)` (cdecl), or its span-buffer twin
  `0x46cac0` with the same arguments (objects queued in span mode, e.g. in Indonesia and Uzbekistan). Both are
  overrides (`lift.py DF_OVERRIDES['hw']`): inside the object part of the 3D frame they become GPU triangles, batched
  while the state stays the same; elsewhere they run as before.
- **The frame.** Hooks at `0x4856f8` (in `0x485610`, after the terrain and the occlusion test, before the queued lists
  are flushed) and `0x485739` (after the tracers) bracket the object part. With the first polygon, the view's clip
  rectangle of colour and depth goes up (`DSR_SURFACE_UPLOAD`, `DSR_DEPTH_UPLOAD`); at the end the GPU picture comes
  back into the game's own colour rows (`DSR_READBACK`), so the HUD, the 65K conversion and the present are unchanged.
  The game's 32-bit pixels carry a brightness in their top byte (`(r + 2g + b) / 4`, true of every colour table
  entry): pixels the GPU left alone keep the CPU's value, drawn ones get that byte. Colour and depth are reached only
  through the view (`[0x8ee1e0]`: `+0x34..0x40` clip, `+0x50` bpp, `+0x68` / `+0xc68` row pointers), never fixed
  addresses.
- **Pixel coverage.** The CPU fills the pixels whose centre `x + 0.5, y + 0.5` lies inside (rows `0x461e30`, spans
  `0x461cd0`); the pretransformed GPU path samples D3D-style at whole numbers, so vertices move half a pixel. With
  nearest, affine sampling (`DF_HW_POINT=1`) the GPU picture is then nearly the CPU's pixel for pixel.
- **Shade.** A vertex's shade is a row 0..63 of its palette's 64 x 256 colour table (`0xbfc100 + pal * 0x614`, `+4`).
  Row 32 holds the plain colours; rows 0..31 are a per-channel multiply of row 32 (within 2/255), rows 33..63 row 32
  lerped towards one haze colour (within ~10/255). Fixed function reproduces it: texture = row 32, stage 0
  `MODULATE(texture, diffuse)` with diffuse = the row's gain, stage 1 `BLENDDIFFUSEALPHA(current, TFACTOR)` with
  TFACTOR = the haze and diffuse alpha = 1 - the haze fraction. Materials with `+0x10 & 4` start at row 32.
- **Drawing kinds** (`flags & 0x73c`, the 32-bit table `0x4686d0` fills): 0 a raw colour; 4/8 a palette colour at the
  shade row; 0x14/0x18 textured, flat/gouraud, + 0x20 texel 0 a hole (alpha test); 0x21x the material's colour over the
  scene with alpha = texel / 255 (muzzle flashes, shadows); 0x41x/0x43x half row-32 colour, half the scene (smoke).
  All test depth strictly less; only the opaque and keyed kinds write it. 0x10, 0x30, 0x10x, 0x20x draw nothing in
  32-bit. Lines (`flags & 0x800`, tracers) and any other kind are left to the CPU after a sync (below).
- **Water.** The plane passes `0x48d040` (eye above the water) and `0x48d570` (below) blend with the picture and test
  the depth, objects included. When they will draw (water level `[0xbd5248]`, eye height and first row of the terrain
  view `[0xc94aa0]`), colour and depth come back first (new `DSR_DEPTH_READBACK`, Metal and GL), the CPU pass runs,
  and the next polygon uploads the picture again. Uzbekistan's first mission has water (level 15) every frame.
- **The setting.** Colors gets a fourth value, **Hardware** (`renderer = 3` in df.cfg), selectable with the arrows and
  persisted like the others (on Exit Game). It runs as 16 Million (`[0x67f4e4] = 1`; every other use of the value
  compares it with 0 or 2) with the GPU on: hooks on the apply (`0x407ac3`) and open (`0x407985`) copies, the arrow's
  store (`0x4127df`, cycling 0..3 and skipping unavailable values; Hardware needs 16 Million) and the label
  (`0x4121b2` / `0x412382`). 256 colours stays software (palettised buffer). `DF_HARDWARE=1` turns the GPU path on in
  any 32-bit mode.
- **Cost** (640x480, M1 Pro): one readback a frame ≈ 1.1-1.3 ms, upload ≈ 0.13 ms, GPU 0.4-0.7 ms for ~900-1000
  polygons in 60-130 draws; water missions sync twice a frame.

## Verified on Linux (against Proton)

- DF1: Choose Soldier, main menu, settings, campaign map, briefing, first Peru mission (terrain, trees, sky, HUD,
  weapon), walking, mouse-look; 16M colours, 256 colours, windowed; menu music and effects.
- DF2: Choose a Soldier, main menu, settings, quick missions, briefing, "Six of One" (snow terrain, falling snow, HUD,
  weapon), mouse-look; Full Screen (Software), Full Screen (Hardware/Direct3D), Windowed.

Click scripts that reach a mission (test host, `DS_TEST=1`, game coordinates 640x480, frame numbers):
- DF1: `600:click:318,439;900:click:320,142;1200:click:320,439;1600:click:320,439` (accept soldier, single player,
  accept mission, start mission). Settings: `900:click:320,202` from the main menu; Colors arrows at (294,221)/(543,221),
  Windowed (543,293), accept (320,439).
- DF2: `300:click:317,439;600:click:320,89;900:click:329,439;1300:click:322,439`. Settings `600:click:320,209`; driver
  arrows (270,226)/(574,226): Software → Hardware → Windowed; accept (320,439).
- Settings persist in the data folder: start each comparison from a fresh one.

## Verified on the Mac

Built with `recomp/build_df.sh` and installed with `install.sh` on Apple Silicon (arm64; no Rosetta). The same click
scripts reach the same screens as on Linux:
- DF1: 16M colours, 256 colours and Windowed, each into the first Peru mission; mouse-look in full screen.
- DF2: Full Screen (Software), Full Screen (Hardware) and Windowed, each into "Six of One"; mouse-look in both full
  screen drivers.
- DF2 Hardware's depth upload (`depth_upload` in renderer.m, `fs_zupload`): the snowflakes are hidden behind the
  terrain the CPU drew. Checked by uploading an all-near depth instead (every flake disappears) and by skipping the
  upload (flakes show through the near ground).
- Sound: the mixed output (`W32_AUDIO_DUMP`) has the menu music and the mission sounds.
- `tests/fuzz_sse.py`: every MMX form of both games matches Unicorn on arm64.
- `~/Applications/Delta Force.app` and `Delta Force 2.app` launch full screen and quit from the menu.

The game writes its settings (`df.cfg` / `df2.cfg`, and the soldier files) when it exits through its own Exit Game.
A run that is killed (a test's timeout) keeps the settings it started with.

Mac-specific fixes: `prof.c` (the sampling profiler's Darwin context and thread id) and `sims_imports.c` (no
absolute-time `clock_nanosleep` on macOS: the multimedia timer sleeps until each deadline instead).

## Known gaps

- Intro videos: DF2's `DF2INTRO.BIK` is skipped (no Bink decoder here; the game moves on). DF1 opens `INTRO.KDV`
  (NovaLogic's own format) at start-up; whether it plays as on Windows has not been checked.
- Windowed (both games): no mouse-look. The games' own windowed branch lets the mouse go (not the host), in DF1: the
  video mode routine (0x495ef0) calls 0x490e40 (ReleaseCapture, clears the capture flags at 0xcc0888/0xcc088c)
  instead of 0x490e10 (SetCapture, sets them); WM_SETFOCUS re-captures only while 0xcc0888 is set, and the re-centring
  in 0x497d20 (SetCursorPos) runs only when captured. Seen in a session that switched to Windowed and in one that
  started in it (check a run really is windowed: StretchDIBits in `W32_TRACE`). Not yet compared with the original
  under Proton; making windowed mode keep the mouse would need a hook at that call.
- LAN multiplayer: Netsock.dll is recompiled and its imports are implemented, but no game has been hosted or joined.
- Direct3D mode was compared against the original at a mission start: the terrain and its depth, and the textured,
  alpha-blended snowflakes the hardware driver adds, match; explosions, smoke and other effects have not been looked at.
- Mouse sensitivity is 1:1 with host pixels; the game's own sensitivity slider applies on top.

## Tools and switches

| | |
|---|---|
| `recomp/build_df.sh <folder>` | unwrap, analyse, lift (exe + DLLs), compile, link. `DF_RELINK=1`: runtime and host only. `DS_TRACE_HOOKS=0xA,0xB`: hooks at those addresses |
| `W32_SNAP=<addr>[:n\|:0]` `W32_SNAP_FILE=<path>` | at a hook, write registers + every committed page (one call, or every call up to 64) |
| `tests/replay.py <libgame> <snapshot> <addr>` | that call through the original x86 (Unicorn) and the recompiled code; registers and memory compared |
| `W32_SMCLOG=1` | code bytes changed at run time |
| `W32_DDLOG=1` | DirectDraw/Direct3D calls and each distinct draw state |
| `DF_MOUSELOG=1` | SetCursorPos and posted mouse messages |
| `DF_HARDWARE=1`, `DF_HWLOG=1` | DF1 Full Screen Hardware in any 32-bit mode; a line a second (polygons, draws, textures, syncs, readback cost, the object part's time with or without the GPU) and each new polygon kind |
| `DF_HW_DUAL=<n>` `DF_HW_DUAL_DIR=<dir>` `DF_HW_DUAL_FROM=<seg>` | the CPU draws every polygon too (its picture stays on screen); every n-th frame both pictures of that frame go to `<dir>/<frame>_cpu.ppm` / `_gpu.ppm` |
| `DF_HW_POINT=1`, `DF_HW_MIPS=0` | nearest, affine texturing as the CPU does (pixel comparisons); no mipmaps |
| `DF_HW_FLOOD=<level>` | raises the water of a mission that has some (Uzbekistan 1: 110 floods the start) to exercise the plane-pass sync |
| `W32_TRACE=<file>` | every Windows call |
| `W32_AUDIOLOG=1`, `W32_AUDIO_DUMP=<wav>` | mixer peaks, the mixed output |
| `tests/fuzz_insn.py`, `tests/fuzz_sse.py` | instruction forms vs Unicorn (`FUZZ_EXE`, `FUZZ_ANALYSIS`) |
| gdb | lifted code's line numbers are guest addresses: `break x86:<decimal address>` |

Reference runs: the unwrapped exe (`recomp/work/<game>/Df*.exe`) in a folder of links to the Steam files, under
Proton in a headless gamescope (`gamescope --backend headless -W 1024 -H 768 -w 640 -h 480`), XTEST input from
python-xlib, screenshots with `gamescopectl screenshot`.
