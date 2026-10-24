![Original Call of Duty 2 main menu with the native CO-OP (ALPHA) entry](.github/images/main-menu.png)

# CoD2SP-CO-OP — ALPHA 0.1.0

**COD2 CO-OP · Cooperative Campaign Modification · Developed by Heperkoo**

Bringing **two-player cooperative campaign gameplay to the original Call of Duty 2 engine**, starting with **Red Army Training (`moscow`)**. Original maps, original Soviet campaign setting, host-authoritative simulation and native in-game menus.

### First native breakthrough: player 2 moves in the original campaign world

The runtime creates a second player through the game's own connection, spawn and movement functions. An independent network process has controlled that player inside Moscow while the host remained stationary. Both entities have their own native client state and sentient; the canonical protagonist stays intact.

| Demonstrated milestone | Result |
|---|---|
| Real client-backed players in one original SP world | **2** |
| Remote player-2 movement, host held stationary | **137.66 horizontal game units** |
| Native player snapshots received in one approximately five-second test | **96** |
| Original-world update pages received in the same test | **880** |
| Current Release build and automated CTest suites | **3 suites passed** |

**Current alpha:** host-side player-2 movement and state transport are verified. Reliable visible partner presentation, a second rendered game client and end-to-end campaign synchronization are the next acceptance milestones; this is not yet a completed playable release.

Target: Windows 10/11 x64, original x86 Steam game, exactly one host and one client over LAN.

## Engine analysis: what unlocked this progress

The work has moved from executable inspection to native engine experiments. Analysis of the legitimately initialized SP runtime and renderer recovered the interfaces needed to create and control a real second player.

| Finding | Engineering result |
|---|---|
| SP initially allocates one **34,488-byte game-client object**. A naive second slot overlaps level globals after just **72 bytes**. | Client and server-client storage are expanded before BSP entities load, with a reserved player-2 entity slot. |
| Native connect, begin/spawn and indexed think paths use a mixture of stack and register calling conventions. SP commands are **48 bytes**, so MP layouts cannot be copied blindly. | Exact-build wrappers route received input into native player-2 physics. |
| The SP renderer exposes a recoverable engine import interface, whose layout differs from MP references. | Hash- and signature-checked native command access and game-thread dispatch are working. |
| Ordinary SP entity rendering skips player-type entities. | A dedicated submission path for the real player's Soviet body, head and helmet is implemented; visibility still needs confirmation. |
| Native `j_head`, DObj and world-text interfaces were identified, together with the existing `generic_human` animation tree. | Head-anchored nametag and independent idle/walk/run animation code are implemented, awaiting visual acceptance. |
| SP has a fixed **64-menu registry**. | Host, Join, Soldier and Settings now share one native menu with tabs, resolving the menu-overflow failure. |
| Save serialization only writes one client record. | Experimental checkpoint requests are declined through a native nonfatal path; invalid two-player saves are not written. |
| Native snapshot/camera paths identify the local actor through `playerState.clientNum`. | A thin-client mirror experiment has a concrete native implementation path; runtime integration and testing remain open. |

Local Ghidra analysis discovered **4,447 functions** in a bounded **1,744,896-byte initialized SP code/data region**, with 4,447 decompilations, and **2,570 functions** in the renderer, with 2,567 decompilations. These are analysis coverage counts, not thousands of individually verified interfaces or a reconstructed engine. The renderer pass reached its analysis time limit. Protected on-disk SP/MP images expose loader code rather than a complete engine listing.

Source reconstruction was tested too: an x86 OpenCoD2 build executed against the installed assets but rejected Moscow through its MP-only map path; cod2m built as an x86 DLL; the CoD2rev_Server MSVC probe hit portability errors. This supports continuing with the original SP runtime to preserve the campaign's existing AI, dialogue and scripts.

A compact, independently written [findings and validation summary](research/findings.json) is tracked. Proprietary extracts, raw initialized code, decompiler output and reference repositories stay local and ignored.

## Implemented and tested

- x86 native runtime DLL, Steam launcher and Windows control panel built with CMake/MSVC.
- Executable/renderer hashes and native call signatures checked before engine access. Original Steam executables remain unmodified on disk.
- Native **CO-OP (ALPHA)** entry with Host, Join, Soldier and Settings tabs. These share one menu definition to avoid SP's fixed 64-menu registry limit.
- Bounded versioned UDP protocol: compatibility handshake, two-player limits, input/snapshot sequencing, reliable ordered events, timeout/reconnect and world-position pages.
- Experimental pre-map expansion of game-client/server-client storage. Native `ClientConnect(1)` / `ClientBegin(1)` create a second client-backed player with its own sentient. The canonical protagonist remains entity 0; the second has classname `coop_player`.
- Real remote movement: an independent network process moved player 2 approximately **137.66 horizontal units**, while player 1 stayed still. That capture received **96 native snapshots** and **880 world-update pages** in about five seconds. This verifies host-side movement and transport, not a second client world.
- Automated protocol/session, world-tracking and launcher UI checks; independent-process packet-loss/duplicate-delivery tests.

## Current implementation work

**Next visible milestone:** a Soviet partner placed near the host, with a verified body, animation and head-anchored name. After that, the priority is a second original-engine client applying host state while local campaign authority stays frozen.

- **Visible Soviet partner and nametag:** body/head/helmet assignment, idle/walk/run tree, native body submission and a `j_head` nametag are implemented as experimental code. Render counters ran, but the user still could not see the partner. Visibility, placement and animation acceptance remain open.
- **Low-level diagnostics:** native entity/client observations, stack diagnostics, model/bone/camera reports and a collision-checked regroup helper exist. The newest regroup/diagnostic helpers still need launcher wiring and runtime verification.
- **Client world mirror:** `src/engine/native_client_mirror.*` contains the opt-in thin-client experiment. It is not yet wired into the main runtime target or acceptance-tested. The intended client freezes local mission authority, applies host state and controls player 2.
- **Broader synchronization:** NPC templates/spawns, animation, weapons, damage, interactions, objectives and scripted progression still need authoritative engine integration.
- **Automated missions:** a mission scenario runner is planned/in progress; the current suite does not automatically complete every campaign mission.
- **Two game windows:** a legitimate second original-SP launch exited before a second renderer/window was observed. A two-window synchronization test has not passed.

## Limitations

**Experimental two-player checkpoints are disabled.** The original writer stores only one client record. The mod declines checkpoint requests through the native nonfatal path instead of writing invalid two-player saves. Save/restore, revive and campaign transitions are unsupported.

The player-2 experiment is opt-in, fresh-Moscow and one-shot per process. Rendering submissions do not prove visible pixels. World telemetry carries observed positions; it does not establish synchronized combat or mission logic. Sampled entity tracking can miss very rapid slot reuse until complete lifecycle hooks exist.

Join stays at the game menu instead of starting an independent campaign and presenting it as shared co-op. Wi-Fi/Ethernet IPv4 LAN is supported; public Internet servers, matchmaking and Spacewar are not integrated. There is no separate mod account/login. Normal Steam authentication remains intact.

## Build and test

Requirements: Visual Studio 2022 Build Tools with x86 C++, Windows SDK and CMake 3.24+. Python 3 is needed for the optional packet-loss test. Original game files are required for launch/UI staging, not compilation.

```powershell
.\tools\build.ps1 -Configuration Debug
.\tools\build.ps1 -Configuration Release
.\tests\network\run-integration.ps1 -Configuration Release
```

CTest covers protocol/session authority, native-world tracking and native launcher controls. Executed checks included **195 network/protocol assertions** and **1,443 world-tracker assertions**; counts can change with source updates. Fixture tests, native host movement and full gameplay acceptance are separate results.

Build output: `build/verified-x86/<Configuration>/`.

## Run

Start Steam normally and close any existing SP test instance:

```powershell
.\build\verified-x86\Release\COD2CoopControl.exe
```

The launcher stages the native UI into an isolated game-home and starts original Steam App **2630**. Select **CO-OP (ALPHA)** in the game, create a session and start Moscow.

Explicit experimental player-2 launch:

```powershell
.\build\verified-x86\Release\COD2CoopLauncher.exe --launch --native-menu --experimental-player2 --attach-after-launch --host --bind 127.0.0.1 --port 28961
```

Use a reachable local adapter address for LAN. Window sizes: `--width 640 --height 480`, 800×600 or 1024×768. Unsupported executable/renderer hashes fail closed. Logs and isolated profiles/saves stay under the selected runtime directory. Experimental hooks/buffers can remain resident until the mission ends and the game exits normally. Use the isolated profile rather than an existing campaign save directory.

```powershell
.\tools\package.ps1 -Configuration Release
```

Packaging creates `dist/COD2-Coop-0.1.0-alpha/` and a ZIP containing mod binaries, own UI definitions, staging tool and this README.

## Repository scope

Tracked: independently authored source, native UI, tests, build/staging/diagnostic tools, this README, a concise research findings summary and the representative main-menu capture above.

Ignored locally: `docs/`, all `analysis/` including the three research repositories, decompiler output/databases, extracted proprietary assets, captures/logs, saves, builds and packages. Private or occluded desktop captures are excluded; only the selected README image is published.

No third-party reconstruction repository or original game executable, map, texture, sound or model is bundled. Staging reads the user's installed menus and generates modified copies locally. Unofficial modification; unaffiliated with Activision or Infinity Ward.
