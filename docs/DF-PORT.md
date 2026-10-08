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

## Multiplayer

Both games play TCP/IP games through their own `Netsock.dll` (recompiled): UDP port 3568, a receiving socket bound to
it and an unbound one for sending, sessions found by a search datagram to an address the player gives. LAN games went
over IPX through DirectPlay and NovaWorld through NovaLogic's servers and web pages; neither exists here.

**What was added** (`runtime/win32/df_net.c`, hooks in `tools/lift.py` DF_HOOKS 'net'):
- Netsock's C runtime imports (`msvcrt.c`: fopen/fgets/fprintf, sscanf, qsort, time/localtime/asctime,
  _beginthread, _findfirst…). Without them nothing networked ever ran.
- **LAN**: each game picks DirectPlay or Netsock in one connection-type switch for hosting and one for joining (DF1
  0x446910 / 0x470c00, DF2 0x45c370 / 0x408600; types 0 LAN, 1 Internet, 2 modem, 3 serial, 4 NovaWorld). LAN takes
  the Internet path, searching at the broadcast address.
- **Several hosts in one search**: the games leave Netsock's 16-byte session instance id zero, and the game lists one
  session per id, so a broadcast search could list one host only, and Netsock (which sends everything to the address
  it searched) would join every host that answered. Each hosted session gets an id (at the call of
  netsock_HostSession), the joiner remembers which address answered with which id, and from netsock_JoinSession on
  (the chosen session's id) winsock.c sends the game's traffic for the search address to that host only.
- **NovaWorld**: `p2p_dht.c` is a DHT client (BEP 5, client side only, a read-only node). A NovaWorld host announces
  its port under a key of the game's own; the Join screen's NovaWorld search (hooks on the join screen and the join
  state machine, which used to start NovaLogic's browser) goes to 255.255.255.254, which winsock.c sends to every
  host the DHT names and to the local network. The games' reports to novaworld.net and DF2 Netsock's usage records
  to 208.231.90.230 are not sent.
- **Routers**: a host forwards UDP 3568 while it hosts (portmap.c: NAT-PMP, then UPnP). Netsock answers peers at
  port 3568 from its unbound socket, which a joiner's router would drop; sends now leave from the game port's socket,
  and go to the port a peer's router actually used when it changed it (`W32_NET_NAT=0` turns this off).

**Verified**:
- Two copies on one Mac (`W32_NETVIRT`, below), with a local fake DHT:
  - DF1: Internet, LAN and NovaWorld through the menus;
  - DF2: LAN and NovaWorld;
  - two LAN hosts listed separately, and the one chosen joined;
  - each time, both copies in the mission with two players.
- Two computers on one network (Mac and the Linux build):
  - DF1 Internet by address;
  - DF2 NovaWorld through a fake DHT on the Mac;
  - in both games, the Linux copy joined the Mac's game.
  The Linux machine's firewall (ufw) drops the LAN search's answers (they answer a broadcast), so LAN search across
  those two machines was not checked; with the firewall open it is the same path as the one-computer test.
- The DHT client against the real DHT, with a throwaway key: announce acknowledged, and found by a second node within
  seconds.

**Not verified**: a game across the internet between two different networks (both routers' port mapping); more than
two players; long games.

### Security

A game hosted with NovaWorld is listed publicly, so anyone can send datagrams to its host, and a malicious host can
send them to whoever joins. The games' 1998 network code was never written for that. What stands between it and the
player's computer:

- **The recompilation.** Guest memory is a separate 4 GB region (followed by a 256 MB inaccessible guard) that the
  game's code can't address outside of. No new code can be injected: only the recompiled code exists, and a jump
  anywhere else ends the game. Starting programs is refused, and files are mapped into the data folder (the game
  folder is read only).
- **The sandbox.** From start-up the process can read only the game, program and data folders, write only the data
  folder and temporary files, and start or have opened no other program.
  - macOS: the system sandbox (no process exec, no LaunchServices, no Apple events).
  - Linux 5.13+: Landlock; system folders and what graphics need stay readable.
  This holds even if a bug gave an attacker the native process. `DF_NO_SANDBOX=1` turns it off.
- **Joiners take datagrams from their host only.** While searching, only from the addresses searched (and the local
  network after a broadcast). Hosts take anyone, as a public game must.
- **The DHT client.** It parses small answers and keeps addresses; nothing from the DHT is run.
  - Transaction ids are random 32-bit values.
  - Addresses at loopback, private or reserved ranges are not taken from the public DHT.
  - Lists are capped per answer.
  - Hosts that never answer stop being asked.
- **Router.** A forward is held only while hosting and removed at exit. One left by a crash is removed at the next
  start.

Checked:
- The DHT parser with 2,000,000 damaged answers under AddressSanitizer and UndefinedBehaviorSanitizer
  (`tests/p2p_dht_fuzz.c`, in CI).
- Each game's host and joiner, with a runtime built with `DF_SANITIZE=address`, flooded for two minutes with about
  90,000–106,000 damaged datagrams made from real game traffic (`tests/net_flood.py`, corpus from
  `W32_NETCAPTURE`). No sanitizer report, no fault, the games kept running.
- A joiner given junk from another address dropped all of it.

Known limits:
- Anyone can join a public game and spam joins, as in 1998; use a game password for a private game. Passwords
  travel unencrypted, so don't reuse a real one.
- A host's public address is visible on the DHT while it hosts.
- Netsock's compressed mode (`Cmprssh0.dll`) was not exercised by the games' default settings.

Switches:
- `DF_NO_P2P=1`: no DHT.
- `P2P_DHT_BOOTSTRAP=host:port,...`: other bootstrap nodes (tests).
- `P2P_DHT_LOG=1`: lookup progress.
- `DF_NW_BROWSER=1`: NovaLogic's original NovaWorld screens.
- `W32_NETLOG=1`: every datagram.
- `W32_NETVIRT=k`: copy k of several on one computer is 127.0.0.k (Netsock answers on a fixed port, so two copies need
  two addresses). Use different k for every copy, including other testers' on the same machine.

## Known gaps

- Intro videos: DF2's `DF2INTRO.BIK` is skipped (no Bink decoder here; the game moves on). DF1 opens `INTRO.KDV`
  (NovaLogic's own format) at start-up; whether it plays as on Windows has not been checked.
- Windowed (both games): no mouse-look. The games' own windowed branch lets the mouse go (not the host), in DF1: the
  video mode routine (0x495ef0) calls 0x490e40 (ReleaseCapture, clears the capture flags at 0xcc0888/0xcc088c)
  instead of 0x490e10 (SetCapture, sets them); WM_SETFOCUS re-captures only while 0xcc0888 is set, and the re-centring
  in 0x497d20 (SetCursorPos) runs only when captured. Seen in a session that switched to Windowed and in one that
  started in it (check a run really is windowed: StretchDIBits in `W32_TRACE`). Not yet compared with the original
  under Proton; making windowed mode keep the mouse would need a hook at that call.
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
| `W32_TRACE=<file>` | every Windows call |
| `W32_NETLOG=1`, `W32_NETVIRT=k`, `P2P_DHT_LOG=1` | network: see Multiplayer |
| `W32_AUDIOLOG=1`, `W32_AUDIO_DUMP=<wav>` | mixer peaks, the mixed output |
| `tests/fuzz_insn.py`, `tests/fuzz_sse.py` | instruction forms vs Unicorn (`FUZZ_EXE`, `FUZZ_ANALYSIS`) |
| gdb | lifted code's line numbers are guest addresses: `break x86:<decimal address>` |

Reference runs: the unwrapped exe (`recomp/work/<game>/Df*.exe`) in a folder of links to the Steam files, under
Proton in a headless gamescope (`gamescope --backend headless -W 1024 -H 768 -w 640 -h 480`), XTEST input from
python-xlib, screenshots with `gamescopectl screenshot`.
