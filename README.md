<div align="center">

# LessSteam

**Keep Steam out of your game's way.**

While a game is running, LessSteam throttles, trims or freezes Steam's background processes,<br>
and puts everything back the moment you quit.

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
![Platform](https://img.shields.io/badge/platform-Windows%2010%20%7C%2011%20x64-0078D6)
![Language](https://img.shields.io/badge/language-C-555555)
[![Release](https://img.shields.io/github/v/release/alextmsv/LessSteam)](https://github.com/alextmsv/LessSteam/releases/latest)

</div>

---

## Why

Steam keeps a whole Chromium browser (`steamwebhelper.exe`) and a SYSTEM service running next to your game. They wake up, animate the friends list, sync the library and hold hundreds of megabytes of RAM, all while you only want frames.

LessSteam steps in only while a game is running. Idle Steam is left completely alone.

## Presets

Pick one from the tray icon.

|  | 🟢 **Limit Steam** (default) | 🔴 **Slay Steam** |
|---|---|---|
| **Goal** | Steam stays usable | only the game matters |
| `steamwebhelper.exe` | lower priority, Efficiency Mode, memory trim | frozen |
| `GameOverlayUI.exe` | memory trim only, so Shift+Tab stays snappy | frozen |
| `steam.exe` | untouched | lower priority, Efficiency Mode |
| `SteamService.exe` | idle priority, Efficiency Mode, memory trim | frozen |
| **Overlay, chat, Steam window** | ✅ work | ❌ unresponsive until you quit the game |

> [!NOTE]
> `steam.exe` is never frozen, not even by Slay. Games talk to it through the Steamworks API (DRM, achievements, cloud saves, lobbies), so freezing it would freeze the game.

Want something in between? Choose **Custom** and set a mode per process in `LessSteam.ini`:

| Mode | What it does |
|---|---|
| `off` | leave the process alone |
| `trim` | lowest memory priority + periodic working-set trim |
| `throttle` | `trim` + below-normal priority + Windows Efficiency Mode |
| `idle` | `trim` + idle priority + Windows Efficiency Mode |
| `freeze` | `trim`, then suspend the process until the game exits |

## Install

1. Download the latest [release](https://github.com/alextmsv/LessSteam/releases/latest).
2. Close Steam completely (tray icon → **Exit**).
3. Copy the files from the archive next to `steam.exe` (usually `C:\Program Files (x86)\Steam`).
4. Run `LessSteamHelper.exe`, accept the UAC prompt, and tick **Start with Windows** in its tray menu. From then on it starts elevated at logon without a prompt.
5. Start Steam and play.

### Tray menu

- **Limit Steam / Slay Steam / Custom**: switch presets live, even mid-game.
- **Pause**: turn LessSteam off without closing it; everything is restored immediately.
- **Open LessSteam.ini**: fine-tune everything.
- **Start with Windows**: install or remove the logon task.
- **Exit**: restores everything and pauses LessSteam until the helper is started again.

### Uninstall

Untick **Start with Windows**, choose **Exit**, close Steam and delete `umpdc.dll`, `umpdc_system.dll`, `LessSteamHelper.exe` and `LessSteam.ini`.

## How it works

```
steam.exe ──loads──▶ umpdc.dll ──────▶ steamwebhelper.exe, GameOverlayUI.exe, steam.exe
                        │
                  LessSteam.ini  (presets, live reload)
                        │
LessSteamHelper.exe ────┴──────────▶ SteamService.exe (SYSTEM)
   (elevated, tray)
```

- **`umpdc.dll`** is a proxy that Steam loads from its own folder. It forwards the real Windows `UMPDC` exports to `umpdc_system.dll` and handles Steam's own processes, with no administrator rights needed.
- **`LessSteamHelper.exe`** is the elevated tray companion. It handles `SteamService.exe`, which the DLL cannot reach, and acts as a watchdog.
- A game counts as running when Steam's own `RunningAppID` and per-app `Running` registry values say so.
- Changes to `LessSteam.ini` are applied within a second, without restarting Steam.

### Smart trimming

The overlay and chat are rendered by `steamwebhelper.exe`. So LessSteam:

- stops trimming for 30 seconds whenever WebHelper is busy (overlay or chat open);
- stops trimming while the Steam window is in the foreground;
- trims only every 10 seconds with Limit.

That way an opened overlay never has to reload its UI from disk.

### Safety net

- Nothing is ever killed; every change is recorded and undone when the game exits.
- Freezing waits 15 seconds after launch, so Steam can finish first-time setup and overlay injection.
- If Steam crashes while WebHelper or the overlay are frozen, the helper resumes the orphans.
- If the helper crashed while `SteamService.exe` was frozen, it resumes it on the next start. It also restores everything on exit, on crash and on Windows shutdown, and Task Scheduler restarts it after a crash.
- Without the helper, `umpdc.dll` still works on its own; only `SteamService.exe` is left alone.

## Configuration

`LessSteam.ini` lives next to `steam.exe` and documents every option:

```ini
[General]
Preset=Limit          ; Limit | Slay | Custom
Paused=0
TrimIntervalMs=0      ; 0 = preset default (Limit 10 s, Slay 1 s)
Log=1                 ; %TEMP%\LessSteam.log

[Custom]
WebHelper=throttle
Overlay=trim
SteamClient=off
SteamService=idle
```

(Inline `;` comments are shown here for readability only; in the real file keep comments on their own lines.)

## Build

Requires Visual Studio 2022 Build Tools with the x64 C/C++ toolchain.

```bat
Build.cmd
```

This builds `umpdc.dll`, `LessSteamHelper.exe` and the tests, runs the tests, and refreshes `release\steam`.

| Folder | Contents |
|---|---|
| `src/common` | presets and config, process parking, logging, Steam state |
| `src/dll` | the `umpdc.dll` proxy |
| `src/helper` | `LessSteamHelper.exe` |
| `config` | default `LessSteam.ini`, also embedded in the helper |
| `tests` | regression tests |

## Credits

Built on [NoSteamWebHelper Reloaded](https://github.com/xowny/NoSteamWebHelper-Reloaded), itself a fork of the original [NoSteamWebHelper](https://github.com/Aetopia/NoSteamWebHelper) by Aetopia.

## License

[GPL-3.0](LICENSE)
