# Delta Force and Delta Force 2, native

NovaLogic's **Delta Force** (1998) and **Delta Force 2** (1999), statically recompiled from your own Steam copies into
native code: no Wine, no Rosetta, no emulator. It uses the same recompiler as
[dungeon-siege-macos](https://github.com/Joshspeakman/dungeon-siege-macos) and the author's Diablo II, Fallout: New
Vegas and Sims ports. The target is Apple Silicon Macs (Metal, Core Audio); the Linux build (SDL, OpenGL) runs the
same recompiled code.

Nothing from the games is in this repository. `./install.sh` verifies your `Df.exe` / `Df2.exe`, undoes the Steam
wrapper on a copy in the build folder, recompiles that copy (with the game's own `Netsock.dll`, `Cmprssh0.dll` and,
for Delta Force 2, `Hw_d3d.dll`) and installs an app. The app reads the game's data from your folder, which is only
read and has to stay where it is.

## State

| | Delta Force (Steam 32620) | Delta Force 2 (Steam 32630) |
|---|---|---|
| Menus, soldier, campaign/quick missions, briefing | working (Mac, Linux) | working (Mac, Linux) |
| Missions: voxel terrain, models, HUD, movement, mouse-look | working (Mac, Linux) | working (Mac, Linux) |
| Renderers | **16 million colours** and **256 colours** (Settings → Colors), **Hardware** (new: objects on the GPU over the CPU terrain, Settings → Colors), **windowed** | **Full Screen (Software)**, **Full Screen (Hardware)** (Direct3D, `Hw_d3d.dll` recompiled), **Windowed** |
| Frame rate | a frame per display refresh when the CPU terrain keeps up (120 at 640x480 on a 120 Hz display), otherwise every second refresh (about 60 at 1184x768); the game's 62.5 Hz tick unchanged | the same (Software: 120 in light views; Hardware: about 60) |
| Screen shapes | the "1024x768" choice renders at the screen's shape (768 lines, wider view on wider screens) | the same, all three drivers |
| Sound and music | working (Mac, Linux) | working (Mac, Linux) |
| LAN games ("LAN (IPX)" in the game, played over TCP/IP) | working (Mac, Linux) | working (Mac, Linux) |
| Internet games by address ("Internet") | working (Mac, Linux) | — (DF2 has no such option: use NovaWorld) |
| NovaWorld: a public game list with no server (BitTorrent DHT) | working (Mac, Linux) | working (Mac, Linux) |
| NovaWorld voice (`Netcomm.dll`) | — | not available (stubbed) |
| macOS app (`install.sh`), launch window, Classic/Modern controls | working (Apple Silicon) | working (Apple Silicon) |

"Working (Linux)" means checked against the original game running under Proton with the same inputs (screenshots
compared). "Working (Mac)" means the same scripted runs on an Apple Silicon Mac reach the same screens: every renderer
into a mission, mouse-look, the mixed sound, and both apps launched from `~/Applications`. See
[Known issues](#known-issues), and [docs/DF-PORT.md](docs/DF-PORT.md) for what was checked and how.

## Requirements

- An Apple Silicon Mac. The app asks for macOS 13 or later.
- The Xcode command line tools (`clang`, `git`, `python3`): `xcode-select --install`. `install.sh` doesn't check for
  them itself; without them the build stops at its first `clang` or `python3`.
- The first build makes `recomp/.venv` and installs Capstone and Unicorn into it with pip, so it needs the internet
  once. The launch window's art is also downloaded from Steam's store page unless you pass `--no-downloads`.
- The **Steam, Windows** version of each game you want, matching the supported hashes below:

  | Game | Steam app | Build | Executable (SHA-256) |
  |---|---|---|---|
  | Delta Force | 32620 | 19311 | `Df.exe` `307bd0956f9b05f690b872b9a2823020a66b42eb22519f6d2418cf6cfe5fd98c` |
  | Delta Force 2 | 32630 | 19312 | `Df2.exe` `0765acdff5fc7eb077959179b0e980743287fc962b20ccac502dd94f272aa717` |

  Any other executable (a disc or another store's version, an older Steam build, a modified exe) is refused with
  `not the supported Steam Df.exe` (or `Df2.exe`).
- Build time and peak disk use have not been verified for a clean install. Allow room for your game files,
  generated source and object files, the app, and additional update worktrees; time varies with the Mac.

## Getting the game files

Each game folder needs `Df.exe` or `Df2.exe`, the `.pff` archives, `Netsock.dll` and `Cmprssh0.dll` (and for Delta
Force 2 `Hw_d3d.dll`), as Steam installs them. Two ways to get them onto the Mac:

- **From a Windows PC:** copy the Steam folders over as they are: `steamapps/common/Delta Force` and
  `steamapps/common/Delta Force 2`.
- **On the Mac, with SteamCMD (untested download route):** Valve's command-line client can request Windows
  content. Install it with `brew install --cask steamcmd` ([Homebrew package](https://formulae.brew.sh/cask/steamcmd));
  the commands below use that package's `steamcmd` wrapper. A manual Valve install uses `./steamcmd.sh` instead.
  Replace `YOUR_STEAM_USERNAME` with an account that owns the game:

  ```sh
  steamcmd +@sSteamCmdForcePlatformType windows +force_install_dir "$HOME/Games/Delta Force" \
           +login "YOUR_STEAM_USERNAME" +app_update 32620 validate +quit
  steamcmd +@sSteamCmdForcePlatformType windows +force_install_dir "$HOME/Games/Delta Force 2" \
           +login "YOUR_STEAM_USERNAME" +app_update 32630 validate +quit
  ```

  Keep `force_install_dir` before `login`. SteamCMD handles the password and Steam Guard prompts.
  Valve's [macOS download](https://steamcdn-a.akamaihd.net/client/installer/steamcmd_osx.tar.gz), inspected on
  2026-10-10, contains an Intel (`x86_64`) executable and needs [Rosetta](https://support.apple.com/en-us/102527)
  on Apple Silicon (`softwareupdate --install-rosetta`). The recompiled games themselves run natively. These
  game downloads have not been tested here; a successful download must still match the supported hashes.

**The Steam wrapper.** Steam's executables are wrapped in SteamStub 2.1, which hides the program's code until Steam
starts it. The build removes the wrapper from a **copy** in `recomp/work/<game>` so the code can be read and
recompiled (`recomp/tools/steamstub21.py`); your game folder is never changed. The installed app contains that
unwrapped copy of your executable (`Contents/Resources/Df.exe` or `Df2.exe`) and the code recompiled from it, so it is
a build of your copy for your Mac. Steam isn't needed to play.

## Install

Keep the clone in a path without spaces: the current compile scripts split source paths on whitespace.

```sh
git clone https://github.com/Joshspeakman/delta-force-macos.git
cd delta-force-macos
./install.sh --game-dir "/path/to/Delta Force"      # Delta Force.app (macOS) or ~/.local/bin/delta-force (Linux)
./install.sh --game-dir "/path/to/Delta Force 2"    # Delta Force 2.app or delta-force-2
```

Each run builds and installs one game; which one follows from the folder (`Df.exe` or `Df2.exe`). Open the apps from
`~/Applications`.

| Option | What it does |
|---|---|
| `--game-dir DIR` | The Steam game folder (required: there is no search) |
| `--app-dir DIR` | Where the app goes (default `~/Applications`; on Linux, where the program goes, default `~/.local/share/delta-force` or `delta-force-2`) |
| `--data-dir DIR` | Where settings, soldiers, saves and logs go (default `~/Games/DeltaForceNative` or `~/Games/DeltaForce2Native`) |
| `--branch main\|nightly` | The branch the app's **Updates** row follows (default `main`). The installer builds the checkout you run it from, so for the nightly code run `git checkout nightly` first, then `./install.sh --branch nightly ...`. It is the same app either way (there is no separate Nightly app) |
| `--no-downloads` | Don't download the launch window's art and the icon from Steam's store page: Delta Force then uses its own splash and menu screens, Delta Force 2 its menu screen with the title written over it |

Running the same `./install.sh --game-dir ...` command again rebuilds and reinstalls the app; the data folder is kept.

**Linux:** the same `install.sh`, with clang, python3 and the SDL2, GLEW, freetype, fontconfig, libsndfile and
libpng development packages. It installs a launcher in `~/.local/bin` (`delta-force`, `delta-force-2`); there is no
launch window or updater there.

## Where things go

| What | Where (Delta Force; Delta Force 2 in `~/Games/DeltaForce2Native`) |
|---|---|
| The app | `~/Applications/Delta Force.app`, `~/Applications/Delta Force 2.app` |
| The game's data | read from your Steam folder, never written to |
| Settings (`df.cfg` / `df2.cfg`), soldiers, saves | `~/Games/DeltaForceNative/game/` (the writable copy of game-folder files) |
| The launch window's settings and art | `launcher.plist` and `art/` in the data folder |
| Your soldiers' original key maps | `backup/controls` in the data folder (saved once, before Controls first changes them) |
| Log | `DeltaForce.log` (`DeltaForce2.log`) in the data folder; at the next launch after it exceeds 5 MB, the previous log is kept as `.old.log` |
| Update output | `update.log` in the data folder |
| Crash and hang reports | `CrashReports` in the data folder |
| Build files | in the clone: `recomp/work/df1`, `recomp/work/df2` (the unwrapped copy and the generated code), `recomp/.venv`, `build/` (the updater's checkouts) |

## The launch window

On macOS the app opens with a launch window laid out like the Dungeon Siege ports': a banner (the game's logo, its
background and one of its screenshots), the **Controls** row, and buttons for each group. **Play** starts the game.
The choices are kept in the data folder's `launcher.plist` and apply at the next Play. Defaults are in bold.

| Setting | Choices |
|---|---|
| Controls | **Classic** (the game's own keys: arrows to move, mouse to look) · Modern (W A S D, Space, C, Z, R, G, mouse wheel: see [Controls](#controls)) · Custom (shown when you changed keys in the game's Remap screen: left as it is) |
| Display → Screen Shape | **This Display** (the game's "1024x768" choice plays at the display's shape, e.g. 1184 × 768 on a 16:10 screen: a wider view) · Original 4:3 (black bars at the sides, as the game shipped) |
| Display → Frame Rate | **Display (its refresh rate)** (smooth motion between the game's 62.5 ticks a second) · 60 FPS (only on displays faster than 60 Hz: cooler and quieter) · Original (the game's own pacing, as it shipped) |
| Multiplayer → NovaWorld | **Public Game List** (NovaWorld finds games through the BitTorrent DHT) · LAN and Addresses (no public list; join by address or on your network) |
| Multiplayer → Router | **Open Port When Hosting** (asks your router, by UPnP or NAT-PMP, to forward UDP 3568; closed on exit) · Leave Router Alone (forward UDP 3568 yourself to host online) |
| Key Guide | every key of the chosen layout |
| Updates | shows whether a newer commit of the app's branch is on GitHub; press it to rebuild the app from it, from your own copy (see [Updating](#updating)) |

The renderer and the resolution are chosen in the game's own Settings screen, as on Windows: in Delta Force,
Colors (16 Million, 256, or **Hardware**, this port's GPU mode, which needs 16 Million) and Windowed; in Delta Force 2,
the driver (Full Screen Software, Full Screen Hardware, Windowed). The wider screen shape applies to the "1024x768"
choice; the other sizes and the menus (640x480) are as they were. The games save their settings when you leave
through their own **Exit Game**.

## Multiplayer

Everything is chosen in the game's own multiplayer screens:
- **LAN (IPX)**: host, or search and join, on the local network. There is no IPX here; it runs over TCP/IP (UDP port
  3568) and finds games by broadcast.
- **Internet** (DF1): join by typing the host's address. The host's Host screen shows it ("Your IP").
- **NovaWorld**: NovaLogic's service is gone. Hosting with NovaWorld lists the game on BitTorrent's public DHT (no
  server anyone has to run), and on the Join screen NovaWorld → Search lists every game hosted that way, plus the
  ones on your network. NovaLogic's login and web pages are not used.

A hosted game asks the router to forward UDP 3568 (NAT-PMP or UPnP) while it is hosted; the forward is removed at
exit, and one left by a crash is removed at the next start. If the router can't, forward UDP 3568 to the host by
hand. A firewall on the host must let UDP 3568 in (on macOS, allow the app if it asks), and macOS asks once whether
the app may use the local network: allow it for LAN games (System Settings → Privacy & Security → Local Network).
The launch window's NovaWorld and Router rows turn these off (or, from a terminal, `DF_NO_P2P=1` keeps games off
the DHT, and `DS_NO_PORTMAP=1` leaves the router alone).

A listed game is reachable by anyone, and the games' network code is from 1998. The game runs in a sandbox (macOS;
Linux 5.13+ with Landlock): it can read only its game, program and data folders, write only its data folder, and can't
start or open other programs. A joining player takes network traffic from the chosen host only. While you host on
NovaWorld, your public address is visible on the DHT. Game passwords travel unencrypted, as in 1998: don't reuse a
real password. Details, and what was tested, in [docs/DF-PORT.md](docs/DF-PORT.md#security).

## Controls

The launch window's **Controls** row writes the chosen layout into your soldiers' own key maps (the copies in the
data folder; the originals are backed up once to `backup/controls`), so the game's Settings › Keyboard Remap shows it
and can change any key. A map changed there shows as *Custom* and is left alone. Choosing Classic again gives back the
original keys. **Key Guide** lists them:

**Classic** (as in the game's manual):

| Action | Key |
|---|---|
| Move · look | arrow keys (Shift walks) · mouse |
| Fire · scope | left button or Ctrl · right button or S |
| Jump · stand / crouch / prone | Space · fn-Delete / fn-Right / fn-Down (Delete / End / Page Down) |
| Weapons | 1–0; Backspace or the middle button cycles |
| Change magazine · binoculars · drop | M · B · D |
| Map · goals · orders · waypoint · audio command | C · G · O · W · A |
| Talk · team talk | T · Y |

**Modern**:

| Action | Key |
|---|---|
| Move · look | W A S D (Shift walks) · mouse |
| Fire · scope | left button (or Ctrl) · right button (or Q) |
| Jump · crouch · prone · stand | Space · C · Z · X |
| Reload (change magazine) · grenade | R · G |
| Weapons | 1–8; the mouse wheel or Backspace cycles; laser designator 0 |
| Binoculars · drop item | B · H |
| Objectives · map · orders · recent messages | Tab · M · O · fn-Right (End) |
| Next waypoint · audio command · next flag · team tags | , · V · 9 · F |
| Delta Force 2 only | night vision N, scope zoom E, scope zero fn-Up / fn-Down |
| Delta Force only | previous waypoint E |
| Forward observer · turbo · video resolution | J · \` · fn-Delete |
| Talk · team talk | T · Y |

Mouse-look is 1:1 with screen pixels; the game's sensitivity slider applies on top.

## Updating

The launch window's **Updates** row asks GitHub, each time it opens, whether the branch the app follows (`main`, or
`nightly` if installed with `--branch nightly`) has a newer commit than the one the app was built from; a build from a
newer local commit is never offered an older one. Press it to update: the branch is fetched into a worktree of the
clone you installed from (`build/stable` or `build/nightly`), the app is rebuilt from your own copy of the game
(with the same game folder, data folder and options), and offers a Restart button when it finishes.
Your soldiers, saves and settings are kept. If an update fails, the details are in `update.log` in the data folder.

- `main` is the stable line; `nightly` has the newest, experimental work.
- Keep the clone where it was when you installed: the updater runs in it. An app installed from a folder that isn't a
  git clone of this repository has no updater. It never asks for a password: "Couldn't check" means GitHub wasn't
  reachable (or git couldn't fetch without asking).
- From the terminal instead: `git pull` in the clone and repeat the original install command, including
  `--game-dir`, any custom folders, `--branch nightly` and `--no-downloads` if used.

## Uninstalling

```sh
rm -rf ~/Applications/"Delta Force.app" ~/Applications/"Delta Force 2.app"
rm -rf ~/Games/DeltaForceNative ~/Games/DeltaForce2Native   # soldiers, saves and settings: copy them first if you want them
rm -rf /path/to/delta-force-macos                           # the clone, with the unwrapped copies in recomp/work
```

On Linux: `~/.local/bin/delta-force*` and `~/.local/share/delta-force*`. Your Steam folders were never changed.

## Known issues

- Windowed mode (both games) has no mouse-look: the games' own windowed code lets the mouse go.
- Delta Force 2's intro video (Bink) is skipped. Whether Delta Force's intro (`INTRO.KDV`) plays as on Windows hasn't
  been checked.
- NovaWorld voice chat (Delta Force 2) isn't available.
- In Delta Force 2's Hardware (Direct3D) driver and Delta Force's new Hardware mode, explosions, smoke and other
  effects haven't been compared closely with the original.
- Delta Force 2 Hardware runs at about 60 fps, not 120; heavy views in software can drop to every second refresh, and
  the wider screen shape costs the CPU renderer frames (Delta Force software at 1184x768: about 60).
- Screen shapes stop at 768 lines (a taller picture is scaled up); more needs deeper changes to both games.
- High frame rates haven't been checked in multiplayer, on a 60 Hz or external display, or through a death and respawn.
- Multiplayer has been checked with two copies on one Mac and between a Mac and a Linux PC on one network; not yet
  across the internet between two different networks, with more than two players, or in long games.
- Settings are saved only when you leave through the game's own Exit Game; quitting another way keeps the old ones.
- Visual comparisons against the original Steam games are tracked in
  [issue 1](https://github.com/Joshspeakman/delta-force-macos/issues/1).

## Troubleshooting

| Message or symptom | What to do |
|---|---|
| `not the supported Steam Df.exe` (`Df2.exe`) | Only the supported Steam builds (19311, 19312) are accepted; newer builds may differ. Let Steam verify the game's files (or run SteamCMD's `app_update ... validate`) and copy the folder again |
| `... has neither Df.exe nor Df2.exe` | `--game-dir` must be the game's own folder (`steamapps/common/Delta Force`), not the Steam library |
| `missing .../Netsock.dll` (or `Cmprssh0.dll`, `Hw_d3d.dll`) | Copy the whole game folder, not only the executable and archives |
| `clang: command not found`, `xcrun: error: invalid active developer path` | Install the Xcode command line tools: `xcode-select --install` |
| `no art for the launch window` / `no icon could be made` | The Steam store page couldn't be reached (or `--no-downloads`); the game itself is unaffected. Repeat the original `./install.sh --game-dir ...` command later for the art |
| The game stopped and an alert offered a report | The report is in `CrashReports` in the data folder (Show Report opens it); a hang you had to force-quit is reported the next time the app opens. The alert currently says "Dungeon Siege" (inherited text); it is about this game. Please attach the report and the log to a [GitHub issue](https://github.com/Joshspeakman/delta-force-macos/issues) |
| Settings or controls you changed in the game are gone | Leave through the game's Exit Game: the games write their settings only then |
| No mouse-look | You're in windowed mode (see [Known issues](#known-issues)) |
| Nobody sees your NovaWorld game | Check Multiplayer → NovaWorld is Public Game List on both sides; let the router forward UDP 3568 (or forward it yourself); allow the app through the macOS firewall |
| Updates says "Couldn't check" | GitHub wasn't reachable, or git couldn't fetch the clone's `origin` without a password; press it to retry |

## Building by hand

```sh
recomp/build_df.sh "/path/to/Delta Force 2"           # -> recomp/work/df2 (Linux: app/DeltaForce2Linux; macOS: libgame.dylib + app/DeltaForce2)
recomp/work/df2/app/DeltaForce2Linux --exe recomp/work/df2/Df2.exe --game "/path/to/Delta Force 2" --data /tmp/df2data
```

`DF_RELINK=1 recomp/build_df.sh ...` rebuilds only the runtime and host (after changes there). Tests that need no game
files: `recomp/tests/metal_regressions.sh` (the Metal renderer under the sanitizers) and the C tests in `recomp/tests`
(among them the DHT client and its fuzzer, and frame pacing); Linux CI runs `recomp/tests/check_linux.sh`. The switches
used to test with the games are in [docs/DF-PORT.md](docs/DF-PORT.md#tools-and-switches).

## How it works

`recomp/build_df.sh` unwraps the executable, analyses it, lifts every function from x86 to C, does the same for the
game's DLLs, and compiles it all for arm64; the runtime (`recomp/runtime/win32`) implements the Windows APIs the games
use (DirectDraw and Direct3D 6, DirectSound, Winsock, ...) and the host (`recomp/host`) runs the window, input, the
launch window and the Metal renderer (`src/renderer`).
[docs/DF-PORT.md](docs/DF-PORT.md) covers what was specific to these games: the SteamStub 2.1 wrapper, the
self-modifying MMX span renderers (instruction fields patched at run time, and multiply-by-constant code copied in
from tables), Watcom C quirks, DirectDraw and Direct3D 6 on the shared renderer (including depth buffers the game
writes itself), the high frame rates, screen shapes, Delta Force's Hardware mode, multiplayer and the testing tools.
The recompiler itself is described in [recomp/README.md](recomp/README.md).

## Documentation

- [docs/DF-PORT.md](docs/DF-PORT.md): the port notes: the executables, what was specific to these games, what was
  verified on Linux and the Mac, multiplayer and its security, known gaps, tools and switches.
- [recomp/README.md](recomp/README.md): the recompiler's execution model and verification.
- [NOTICE.md](NOTICE.md): notices and credits.

## Legal

Delta Force and NovaWorld are trademarks of their owners (NovaLogic's games are now THQ Nordic's). This project is
not affiliated with or endorsed by them or by Valve. It contains no part of either game: you need your own Steam
copies, the build reads them from your disk, and everything generated from them, the unwrapped executable included,
stays on your Mac (in the clone's `recomp/work` and in the app). Details and credits: [NOTICE.md](NOTICE.md).

## License

The code in this repository is under the MIT License: [LICENSE](LICENSE). The third-party code in `recomp/third_party`
keeps its own licenses (see [NOTICE.md](NOTICE.md)).
