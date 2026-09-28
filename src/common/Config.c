#define STRSAFE_NO_DEPRECATE
#include <strsafe.h>
#include "Config.h"

static const LPCWSTR g_modeNames[LS_MODE_INVALID] = {
    L"off",
    L"trim",
    L"throttle",
    L"idle",
    L"freeze",
};

static const LPCWSTR g_targetKeys[LS_TARGET_COUNT] = {
    L"WebHelper",
    L"Overlay",
    L"SteamClient",
    L"SteamService",
};

static const LS_MODE g_presetModes[LS_PRESET_CUSTOM][LS_TARGET_COUNT] = {
    {LS_MODE_THROTTLE, LS_MODE_TRIM, LS_MODE_OFF, LS_MODE_IDLE},
    {LS_MODE_FREEZE, LS_MODE_FREEZE, LS_MODE_THROTTLE, LS_MODE_FREEZE},
};

static BOOL EqualsIgnoreCase(LPCWSTR left, LPCWSTR right)
{
    return CompareStringOrdinal(left, -1, right, -1, TRUE) == CSTR_EQUAL;
}

LS_MODE LsParseMode(LPCWSTR text)
{
    int index = 0;

    if (text == NULL)
        return LS_MODE_INVALID;

    for (; index < LS_MODE_INVALID; index++)
    {
        if (EqualsIgnoreCase(text, g_modeNames[index]))
            return (LS_MODE)index;
    }

    return LS_MODE_INVALID;
}

LPCWSTR LsModeName(LS_MODE mode)
{
    if (mode < LS_MODE_OFF || mode >= LS_MODE_INVALID)
        return L"invalid";

    return g_modeNames[mode];
}

LS_PRESET LsParsePreset(LPCWSTR text)
{
    if (text == NULL)
        return LS_PRESET_INVALID;

    if (EqualsIgnoreCase(text, L"Limit") || EqualsIgnoreCase(text, L"Limit Steam"))
        return LS_PRESET_LIMIT;
    if (EqualsIgnoreCase(text, L"Slay") || EqualsIgnoreCase(text, L"Slay Steam"))
        return LS_PRESET_SLAY;
    if (EqualsIgnoreCase(text, L"Custom"))
        return LS_PRESET_CUSTOM;

    return LS_PRESET_INVALID;
}

LPCWSTR LsPresetName(LS_PRESET preset)
{
    switch (preset)
    {
    case LS_PRESET_LIMIT:
        return L"Limit";
    case LS_PRESET_SLAY:
        return L"Slay";
    case LS_PRESET_CUSTOM:
        return L"Custom";
    default:
        return L"invalid";
    }
}

LPCWSTR LsPresetDisplayName(LS_PRESET preset)
{
    switch (preset)
    {
    case LS_PRESET_LIMIT:
        return L"Limit Steam";
    case LS_PRESET_SLAY:
        return L"Slay Steam";
    case LS_PRESET_CUSTOM:
        return L"Custom";
    default:
        return L"invalid";
    }
}

LPCWSTR LsTargetKey(LS_TARGET target)
{
    if (target < LS_TARGET_WEBHELPER || target >= LS_TARGET_COUNT)
        return L"invalid";

    return g_targetKeys[target];
}

void LsGetPresetModes(LS_PRESET preset, LS_MODE modes[LS_TARGET_COUNT])
{
    int target = 0;

    if (preset != LS_PRESET_SLAY)
        preset = LS_PRESET_LIMIT;

    for (; target < LS_TARGET_COUNT; target++)
        modes[target] = g_presetModes[preset][target];
}

LS_MODE LsClampMode(LS_TARGET target, LS_MODE mode)
{
    if (mode < LS_MODE_OFF || mode >= LS_MODE_INVALID)
        return LS_MODE_OFF;

    if (target == LS_TARGET_STEAMCLIENT && mode == LS_MODE_FREEZE)
        return LS_MODE_IDLE;

    return mode;
}

DWORD LsDefaultTrimInterval(LS_PRESET preset)
{
    return preset == LS_PRESET_SLAY ? 1000 : 10000;
}

void LsLoadConfig(LPCWSTR path, LS_CONFIG *config)
{
    WCHAR text[64] = {0};
    LS_MODE defaults[LS_TARGET_COUNT] = {LS_MODE_OFF};
    UINT trimIntervalMs = 0;
    int target = 0;

    if (config == NULL)
        return;

    GetPrivateProfileStringW(L"General", L"Preset", L"Limit", text, ARRAYSIZE(text), path);
    config->preset = LsParsePreset(text);
    if (config->preset == LS_PRESET_INVALID)
        config->preset = LS_PRESET_LIMIT;

    LsGetPresetModes(config->preset, config->modes);

    if (config->preset == LS_PRESET_CUSTOM)
    {
        LsGetPresetModes(LS_PRESET_LIMIT, defaults);
        for (target = 0; target < LS_TARGET_COUNT; target++)
        {
            LS_MODE mode;

            GetPrivateProfileStringW(L"Custom", g_targetKeys[target], g_modeNames[defaults[target]],
                                     text, ARRAYSIZE(text), path);
            mode = LsParseMode(text);
            config->modes[target] = (mode == LS_MODE_INVALID) ? defaults[target] : mode;
        }
    }

    for (target = 0; target < LS_TARGET_COUNT; target++)
        config->modes[target] = LsClampMode((LS_TARGET)target, config->modes[target]);

    trimIntervalMs = GetPrivateProfileIntW(L"General", L"TrimIntervalMs", 0, path);
    if (trimIntervalMs == 0)
        trimIntervalMs = LsDefaultTrimInterval(config->preset);
    else if (trimIntervalMs < LS_MIN_TRIM_INTERVAL_MS)
        trimIntervalMs = LS_MIN_TRIM_INTERVAL_MS;
    else if (trimIntervalMs > LS_MAX_TRIM_INTERVAL_MS)
        trimIntervalMs = LS_MAX_TRIM_INTERVAL_MS;
    config->trimIntervalMs = trimIntervalMs;

    config->logEnabled = GetPrivateProfileIntW(L"General", L"Log", 1, path) != 0;
    config->paused = GetPrivateProfileIntW(L"General", L"Paused", 0, path) != 0;
    config->autostartWithSteam = GetPrivateProfileIntW(L"General", L"AutostartWithSteam", 1, path) != 0;
    config->startWithWindows = (int)GetPrivateProfileIntW(L"General", L"StartWithWindows", (UINT)-1, path);
    if (config->startWithWindows > 0)
        config->startWithWindows = TRUE;
}

BOOL LsWritePreset(LPCWSTR path, LS_PRESET preset)
{
    if (preset < LS_PRESET_LIMIT || preset >= LS_PRESET_INVALID)
        return FALSE;

    return WritePrivateProfileStringW(L"General", L"Preset", LsPresetName(preset), path);
}

BOOL LsWriteFlag(LPCWSTR path, LPCWSTR key, BOOL value)
{
    return WritePrivateProfileStringW(L"General", key, value ? L"1" : L"0", path);
}

BOOL LsWritePaused(LPCWSTR path, BOOL paused)
{
    return LsWriteFlag(path, L"Paused", paused);
}

BOOL LsParkingConfigEqual(const LS_CONFIG *left, const LS_CONFIG *right)
{
    int target = 0;

    if (left->preset != right->preset || left->paused != right->paused ||
        left->trimIntervalMs != right->trimIntervalMs)
        return FALSE;

    for (; target < LS_TARGET_COUNT; target++)
    {
        if (left->modes[target] != right->modes[target])
            return FALSE;
    }

    return TRUE;
}

BOOL LsBuildConfigPath(HMODULE module, LPWSTR path, DWORD capacity)
{
    DWORD length = GetModuleFileNameW(module, path, capacity);
    LPWSTR separator = NULL;

    if (length == 0 || length >= capacity)
        return FALSE;

    separator = wcsrchr(path, L'\\');
    if (separator == NULL)
        return FALSE;

    separator[1] = L'\0';
    return SUCCEEDED(StringCchCatW(path, capacity, LS_CONFIG_FILE_NAME));
}

BOOL LsConfigFileChanged(LPCWSTR path, FILETIME *lastWriteTime)
{
    WIN32_FILE_ATTRIBUTE_DATA data = {0};
    FILETIME current = {0};

    if (GetFileAttributesExW(path, GetFileExInfoStandard, &data))
        current = data.ftLastWriteTime;

    if (CompareFileTime(&current, lastWriteTime) == 0)
        return FALSE;

    *lastWriteTime = current;
    return TRUE;
}
