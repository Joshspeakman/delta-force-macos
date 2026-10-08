# Delta Force and Delta Force 2, native

NovaLogic's **Delta Force** (1998) and **Delta Force 2** (1999), statically recompiled from your own Steam copies into
native code: no Wine, no Rosetta, no emulator. It uses the same recompiler as the Dungeon Siege, Fallout: New Vegas,
Diablo II and Sims ports. The target is Apple Silicon Macs (Metal, Core Audio); the Linux build (SDL, OpenGL) runs the same
recompiled code.

Nothing from the games is in this repository. The build verifies your `Df.exe` / `Df2.exe`, undoes the Steam wrapper
on a copy in the build folder, recompiles that copy, and reads the game's data from your folder.

## State

| | Delta Force (Steam 32620) | Delta Force 2 (Steam 32630) |
|---|---|---|
| Menus, soldier, campaign/quick missions, briefing | working (Mac, Linux) | working (Mac, Linux) |
| Missions: voxel terrain, models, HUD, movement, mouse-look | working (Mac, Linux) | working (Mac, Linux) |
| Renderers | **16 million colours** and **256 colours** (Settings → Colors), **Hardware** (new: objects on the GPU over the CPU terrain, Settings → Colors), **windowed** | **Full Screen (Software)**, **Full Screen (Hardware)** (Direct3D, `Hw_d3d.dll` recompiled), **Windowed** |
| Frame rate | a frame per display refresh when the CPU terrain keeps up (120 at 640x480 here), otherwise every second refresh (about 60 at 1184x768); the game's 62.5 Hz tick unchanged | the same (Software: 120 in light views; Hardware: 60) |
| Screen shapes | the "1024x768" choice renders at the screen's shape (768 lines, wider view on wider screens) | the same, all three drivers |
| Sound and music | working (Mac, Linux) | working (Mac, Linux) |
| LAN games ("LAN (IPX)" in the game, played over TCP/IP) | working (Mac, Linux) | working (Mac, Linux) |
| Internet games by address ("Internet") | working (Mac, Linux) | — (DF2 has no such option: use NovaWorld) |
| NovaWorld: a public game list with no server (BitTorrent DHT) | working (Mac, Linux) | working (Mac, Linux) |
| NovaWorld voice (`Netcomm.dll`) | — | not available (stubbed) |
| macOS app (`install.sh`) | working (Apple Silicon) | working (Apple Silicon) |

"Working (Linux)" means checked against the original game running under Proton with the same inputs (screenshots
compared). "Working (Mac)" means the same scripted runs on an Apple Silicon Mac reach the same screens: every renderer
into a mission, mouse-look, the mixed sound, and both apps launched from `~/Applications`. Windowed mode has no
mouse-look (the games' own windowed code lets the mouse go); see [docs/DF-PORT.md](docs/DF-PORT.md) for this and the
other known gaps, and how to test.

## Install

Copy the Steam game folder over from the PC as it is (`steamapps/common/Delta Force` or `.../Delta Force 2`), then:

```sh
git clone <this repository> && cd delta-force-macos
./install.sh --game-dir "/path/to/Delta Force"      # Delta Force.app (macOS) or ~/.local/bin/delta-force (Linux)
./install.sh --game-dir "/path/to/Delta Force 2"    # Delta Force 2.app or delta-force-2
```

Needs the Xcode command line tools (macOS) or clang plus the SDL2, GLEW, freetype, fontconfig, libsndfile and libpng
development packages (Linux), and python3. The build takes a minute or two. The game folder is only read; settings,
soldiers and saves go to the data folder (`~/Games/DeltaForceNative`, `~/Games/DeltaForce2Native`; `--data-dir` to
choose).

On macOS the app opens with a launch window: **Screen Shape** (the display's own shape, or the original 4:3),
**Frame Rate** (the display's rate, 60, or the game's original pacing), **NovaWorld** (the public game list on or
off), **Router** (forward the game's port when hosting, or leave the router alone) and **Updates** (rebuilds the app
from the newest `main` on GitHub, from your own game copy). The choices are kept in the data folder's
`launcher.plist`. Its art and the app's icon are the game's logo and background from its Steam store page, downloaded
by `install.sh` into the data folder's `art/` (with `--no-downloads`: the game's own splash and menu screens).

Choose the renderer and resolution in the game's own Settings screen, as on Windows.

## Multiplayer

Everything is chosen in the game's own multiplayer screens:
- **LAN (IPX)**: host, or search and join, on the local network. There is no IPX here; it runs over TCP/IP (UDP port
  3568) and finds games by broadcast.
- **Internet** (DF1): join by typing the host's address. The host's Host screen shows it ("Your IP").
- **NovaWorld**: NovaLogic's service is gone. Hosting with NovaWorld lists the game on BitTorrent's public DHT (no
  server anyone has to run), and on the Join screen NovaWorld → Search lists every game hosted that way, plus the
  ones on your network. NovaLogic's login and web pages are not used.

A hosted game asks the router to forward UDP 3568 (NAT-PMP or UPnP) while it is hosted. If the router can't, forward
UDP 3568 to the host by hand. A firewall on the host must let UDP 3568 in (on macOS, allow the app if it asks).
The launch window's NovaWorld and Router rows turn these off (or, from a terminal, `DF_NO_P2P=1` keeps games off
the DHT, and `DS_NO_PORTMAP=1` leaves the router alone).

A listed game is reachable by anyone, and the games' network code is from 1998. The game runs in a sandbox (macOS; Linux
5.13+ with Landlock): it can read only its game, program and data folders, write only its data folder, and can't start or open
other programs. A joining player takes network traffic from the chosen host only. Game passwords travel unencrypted,
as in 1998: don't reuse a real password. Details, and what was tested, in
[docs/DF-PORT.md](docs/DF-PORT.md#security).

## Building by hand

```sh
recomp/build_df.sh "/path/to/Delta Force 2"           # -> recomp/work/df2 (Linux: app/DeltaForce2Linux; macOS: libgame.dylib + app/DeltaForce2)
recomp/work/df2/app/DeltaForce2Linux --exe recomp/work/df2/Df2.exe --game "/path/to/Delta Force 2" --data /tmp/df2data
```

`DF_RELINK=1 recomp/build_df.sh ...` rebuilds only the runtime and host (after changes there).

## How it works

[docs/DF-PORT.md](docs/DF-PORT.md) covers what was specific to these games: the SteamStub 2.1 wrapper, the self-modifying
MMX span renderers (instruction fields patched at run time, and multiply-by-constant code copied in from tables), Watcom
C quirks, DirectDraw and Direct3D 6 on the shared renderer (including depth buffers the game writes itself), and the
testing tools. The recompiler itself is described in [recomp/README.md](recomp/README.md).
