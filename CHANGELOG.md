# Changelog

## 1.0.0 — 2026-09-28

First release, based on NoSteamWebHelper Reloaded.

- Two presets: **Limit Steam** (Steam stays usable) and **Slay Steam** (freeze everything the game does not need), plus a per-process **Custom** mode in `LessSteam.ini`.
- Handles `steamwebhelper.exe`, `GameOverlayUI.exe`, `steam.exe` and `SteamService.exe` with the modes `off`, `trim`, `throttle`, `idle` and `freeze`. `steam.exe` is never frozen.
- New elevated tray helper `LessSteamHelper.exe`: controls `SteamService.exe`, switches presets live, pauses LessSteam, opens the config, installs a logon task.
- Smart trimming: memory trims are held off while WebHelper is busy (overlay or chat open) and while the Steam window is focused. Limit trims every 10 seconds, Slay every second.
- Watchdog: resumes processes left frozen after a Steam or helper crash, and restores everything on exit and on Windows shutdown.
- Config changes are applied live, without restarting Steam.
