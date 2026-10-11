# Recompiler: Delta Force and Delta Force 2 as native code

Static recompilation of the user's own Steam `Df.exe` / `Df2.exe` (and the NovaLogic DLLs in the game folder) into C,
compiled for Apple Silicon or Linux x86-64. Nothing derived from the games is committed: `build_df.sh` regenerates
everything locally from the user's files. Installing and playing: [README](../README.md); what was specific to these
games and what was checked on each platform: [docs/DF-PORT.md](../docs/DF-PORT.md).

```sh
recomp/build_df.sh "/path/to/Delta Force 2"      # -> recomp/work/df2 (a minute or two)
```

`build_df.sh` checks the executable's SHA-256 (Steam builds 19311 and 19312 only), undoes SteamStub 2.1 on a copy
(`tools/steamstub21.py`), analyses and lifts it, does the same for `Netsock.dll`, `Cmprssh0.dll` (rebased to
0x11000000) and, for Delta Force 2, `Hw_d3d.dll` (0x12000000), compiles everything in parallel and links it with the
runtime (`tools/build_runtime.sh`: Metal on macOS, SDL/OpenGL on Linux). `DF_RELINK=1` keeps the lifted objects and
rebuilds only the runtime and host.

The recompiler (analysis, lifter, execution model, x87, indirect calls, test harnesses) was inherited from the
Dungeon Siege II port and works unchanged here; the runtime still carries modules of the earlier ports (Direct3D 9,
HLSL, Miles, DirectPlay 8) that these games don't load.

## Status

| Part | State |
|---|---|
| Analysis and lifting (`tools/analyze.py`, `tools/lift.py`) | both executables and their DLLs; Watcom C (DF1) and MSVC 5 + MASM (DF2) specifics, self-modifying span renderers (`smc_scan`, code slots): see [docs/DF-PORT.md](../docs/DF-PORT.md) |
| Game-specific hooks (`tools/lift.py` `DF_HOOKS` / `DF_OVERRIDES`, `runtime/win32/df_hooks.c`) | one table and one runtime module per feature: `df_net.c` (multiplayer, NovaWorld), `df_frame.c` (high frame rates), `df_res.c` (screen shapes; arrays moved with `DF_MOVED`), `df_hw.c` (DF1 Full Screen Hardware) |
| DirectDraw and Direct3D 6 (`runtime/win32/ddraw_pal.c`, from the Diablo II port) on the renderer in `../src/renderer` | 8/16/32-bit surfaces, palettes, DF2's Direct3D driver, CPU-written depth buffers (`DSR_DEPTH_UPLOAD`) |
| Sound | the games' streaming DirectSound buffer on Core Audio (SDL on Linux); DF2's MSACM ADPCM decoded in `df_imports.c` |
| Network | `Netsock.dll` recompiled over `winsock.c` (UDP 3568); `p2p_dht.c` (the NovaWorld list), `portmap.c` (NAT-PMP, UPnP) |
| Host (`host/main.m`, `host/linux.c`), launch window (`host/launcher.m`), sandbox | working on macOS (Apple Silicon) and Linux |

Test mode: `DS_TEST=1` (background window, no mouse capture, no launch window), `W32_AUDIO_MUTE=1`,
`DS_SCRIPT=...` or `DS_CMDFILE=<file>` (scripted input in the game's display-mode coordinates), `DS_RESOLUTION=WxH`.
Logs: `W32_TRACE=<file>` (every Windows call), `W32_SEHLOG=1` (exceptions and the SEH chain), `W32_REGLOG=1`,
`W32_AUDIOLOG=1`, `W32_DDLOG=1`, `W32_NETLOG=1`. The Delta Force switches (`DF_*`) are listed in
[docs/DF-PORT.md](../docs/DF-PORT.md#tools-and-switches). Crash and hang reports go to `<data>/CrashReports`.

## Execution model

- **Memory:** one 4 GB host region; guest address = offset. The image sits at its preferred base `0x400000`, so all
  pointers the game stores (vtables, saves, its own structures) keep their 32-bit values.
- **Functions:** one C function per original function. Registers are C locals, written back to a per-thread context
  around calls. The guest stack is real (`call` pushes the original return address; arguments are read where the
  original reads them), so calling conventions, `this` pointers and stack-walking code behave as before.
- **Flags:** lazy — flag-setting instructions record their operands; consumers (`jcc`, `setcc`, `adc`, `sahf` …)
  compute only the flags they need.
- **x87:** eight doubles + TOP + control/status words, the precision model Dungeon Siege II was measured to run with
  under Rosetta: double arithmetic, float rounding at 32-bit stores, `fist(p)` honouring the rounding
  mode. x87 register forms are decoded from the opcode bytes (Intel semantics), not from disassembler text.
- **Indirect calls/jumps:** an address → function table; jump tables become `switch` statements; imports are dispatched
  through `rt_import` (IAT slots hold thunk addresses, so a call through a register still lands there).
- Junk decodes (data read as code) and padding become `rt_unhandled()` traps.

## Verification and known gaps

The instruction-level harness (`tests/harness.py`, `tests/fuzz_insn.py`, `tests/test_slice.py`) comes from the
Dungeon Siege 1 port, where every instruction form the game used was checked bit-for-bit against Unicorn.
`tests/fuzz_sse.py` passes every MMX form both Delta Force games use (on arm64 too), and `tests/replay.py` runs one
call through the original x86 and the recompiled code from a snapshot and compares them. The games themselves were
checked against the originals under Proton: see [docs/DF-PORT.md](../docs/DF-PORT.md).

- **SEH:** access violations and RaiseException go through the game's handlers. First touches of reserved archive
  pages pass over C++ `catch(...)` frames (the game reads archives inside them); C++ catch continuations are not
  implemented, and the game has not needed them so far.
- **Indirect calls** to addresses outside the function table stop the game with the address logged (add it as a
  seed in `work/observed_targets.json` and rebuild).
- **x87 tag word** is not modelled; transcendentals use the host math library.
