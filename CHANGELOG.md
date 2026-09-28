# Changelog

## Unreleased

- Every helper start begins a fresh `LessSteam.log`; the previous run is kept as `LessSteam.prev.log`. Without the helper, the DLL starts a new log once it exceeds 5 MB.

## 1.1.0 — 2026-09-28

- **Autostart with Steam** (on by default, toggle in the tray): `umpdc.dll` starts `LessSteamHelper.exe` when Steam starts, elevated and without a UAC prompt, via the `LessSteam` scheduled task. The DLL now checks that the helper really started and logs it if it did not.
- The scheduled task exists whenever either autostart option is on; **Start with Windows** only adds a logon trigger to it. The helper re-points the task at itself on every start, so moving it can no longer leave a broken task behind.
- LessSteam tray and program icon, sharp at every DPI (the helper is now DPI-aware).
- A helper that is not next to `steam.exe` now uses Steam's own `LessSteam.ini` (found through the `SteamPath` registry value), so the tray and `umpdc.dll` always share one config.
- Toggling autostart options no longer restores and re-applies the current game's limits; only changes to the preset, modes, trim interval or pause do.
- Lower overhead: Steam's process list is re-scanned every 15 seconds instead of 5 while a game runs. Measured cost: about 0.1% of one CPU core for the DLL and 0.02% for the helper.
- Fixed a false "busy" reading right after a process was parked.

## 1.0.0 — 2026-09-28

First release, based on NoSteamWebHelper Reloaded.

- Two presets: **Limit Steam** (Steam stays usable) and **Slay Steam** (freeze everything the game does not need), plus a per-process **Custom** mode in `LessSteam.ini`.
- Handles `steamwebhelper.exe`, `GameOverlayUI.exe`, `steam.exe` and `SteamService.exe` with the modes `off`, `trim`, `throttle`, `idle` and `freeze`. `steam.exe` is never frozen.
- New elevated tray helper `LessSteamHelper.exe`: controls `SteamService.exe`, switches presets live, pauses LessSteam, opens the config, installs a logon task.
- Smart trimming: memory trims are held off while WebHelper is busy (overlay or chat open) and while the Steam window is focused. Limit trims every 10 seconds, Slay every second.
- Watchdog: resumes processes left frozen after a Steam or helper crash, and restores everything on exit and on Windows shutdown.
- Config changes are applied live, without restarting Steam.
