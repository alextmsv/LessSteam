#ifndef LESSSTEAM_CONFIG_H
#define LESSSTEAM_CONFIG_H

#include <windows.h>

#define LS_CONFIG_FILE_NAME L"LessSteam.ini"

#define LS_FREEZE_DELAY_MS 15000ULL

#define LS_MIN_TRIM_INTERVAL_MS 1000
#define LS_MAX_TRIM_INTERVAL_MS 600000

typedef enum LS_MODE
{
    LS_MODE_OFF = 0,
    LS_MODE_TRIM,
    LS_MODE_THROTTLE,
    LS_MODE_IDLE,
    LS_MODE_FREEZE,
    LS_MODE_INVALID
} LS_MODE;

typedef enum LS_TARGET
{
    LS_TARGET_WEBHELPER = 0,
    LS_TARGET_OVERLAY,
    LS_TARGET_STEAMCLIENT,
    LS_TARGET_STEAMSERVICE,
    LS_TARGET_COUNT
} LS_TARGET;

typedef enum LS_PRESET
{
    LS_PRESET_LIMIT = 0,
    LS_PRESET_SLAY,
    LS_PRESET_CUSTOM,
    LS_PRESET_INVALID
} LS_PRESET;

typedef struct LS_CONFIG
{
    LS_PRESET preset;
    LS_MODE modes[LS_TARGET_COUNT];
    DWORD trimIntervalMs;
    BOOL logEnabled;
    BOOL paused;
} LS_CONFIG;

LS_MODE LsParseMode(LPCWSTR text);
LPCWSTR LsModeName(LS_MODE mode);
LS_PRESET LsParsePreset(LPCWSTR text);
LPCWSTR LsPresetName(LS_PRESET preset);
LPCWSTR LsPresetDisplayName(LS_PRESET preset);
LPCWSTR LsTargetKey(LS_TARGET target);

void LsGetPresetModes(LS_PRESET preset, LS_MODE modes[LS_TARGET_COUNT]);
LS_MODE LsClampMode(LS_TARGET target, LS_MODE mode);
DWORD LsDefaultTrimInterval(LS_PRESET preset);

void LsLoadConfig(LPCWSTR path, LS_CONFIG *config);
BOOL LsWritePreset(LPCWSTR path, LS_PRESET preset);
BOOL LsWritePaused(LPCWSTR path, BOOL paused);
BOOL LsBuildConfigPath(HMODULE module, LPWSTR path, DWORD capacity);
BOOL LsConfigFileChanged(LPCWSTR path, FILETIME *lastWriteTime);

#endif
