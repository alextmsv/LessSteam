#define STRSAFE_NO_DEPRECATE
#include <strsafe.h>
#include "State.h"
#include "Steam.h"

static DWORD ReadSteamDwordValue(LPCWSTR subKey, LPCWSTR valueName, DWORD fallbackValue)
{
    DWORD value = fallbackValue;
    DWORD size = sizeof(value);

    if (RegGetValueW(HKEY_CURRENT_USER, subKey, valueName, RRF_RT_REG_DWORD, NULL, &value, &size) != ERROR_SUCCESS)
        return fallbackValue;

    return value;
}

static BOOL ReadSteamAppRunning(DWORD appId)
{
    WCHAR subKey[128] = {0};

    if (FAILED(StringCchPrintfW(subKey, ARRAYSIZE(subKey), L"SOFTWARE\\Valve\\Steam\\Apps\\%lu", appId)))
        return FALSE;

    return ReadSteamDwordValue(subKey, L"Running", FALSE) != FALSE;
}

BOOL LsIsSteamGameRunning(DWORD *appId)
{
    DWORD runningAppId = ReadSteamDwordValue(L"SOFTWARE\\Valve\\Steam", L"RunningAppID", 0);
    BOOL appMarkedRunning = FALSE;

    if (runningAppId != 0)
        appMarkedRunning = ReadSteamAppRunning(runningAppId);

    if (appId != NULL)
        *appId = runningAppId;

    return LsShouldPark(runningAppId, appMarkedRunning);
}

BOOL LsGetSteamDirectory(LPWSTR path, DWORD capacity)
{
    DWORD size = capacity * sizeof(WCHAR);
    LPWSTR cursor = NULL;

    if (RegGetValueW(HKEY_CURRENT_USER, L"SOFTWARE\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, NULL, path, &size) !=
            ERROR_SUCCESS ||
        path[0] == L'\0')
        return FALSE;

    for (cursor = path; *cursor != L'\0'; cursor++)
    {
        if (*cursor == L'/')
            *cursor = L'\\';
    }

    return TRUE;
}
