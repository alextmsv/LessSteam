# Changelog

## Unreleased

- **Autostart with Steam** (on by default, toggle in the tray): `umpdc.dll` starts `LessSteamHelper.exe` when Steam starts, elevated and without a UAC prompt, via the `LessSteam` scheduled task.
- The scheduled task now exists whenever either autostart option is on; **Start with Windows** only adds a logon trigger to it.
- Toggling autostart options no longer restores and re-applies the current game's limits; only changes to the preset, modes, trim interval or pause do.

## 1.0.0 — 2026-09-28

First release, based on NoSteamWebHelper Reloaded.

- Two presets: **Limit Steam** (Steam stays usable) and **Slay Steam** (freeze everything the game does not need), plus a per-process **Custom** mode in `LessSteam.ini`.
- Handles `steamwebhelper.exe`, `GameOverlayUI.exe`, `steam.exe` and `SteamService.exe` with the modes `off`, `trim`, `throttle`, `idle` and `freeze`. `steam.exe` is never frozen.
- New elevated tray helper `LessSteamHelper.exe`: controls `SteamService.exe`, switches presets live, pauses LessSteam, opens the config, installs a logon task.
- Smart trimming: memory trims are held off while WebHelper is busy (overlay or chat open) and while the Steam window is focused. Limit trims every 10 seconds, Slay every second.
- Watchdog: resumes processes left frozen after a Steam or helper crash, and restores everything on exit and on Windows shutdown.
- Config changes are applied live, without restarting Steam.
