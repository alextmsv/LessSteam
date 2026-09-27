#define STRSAFE_NO_DEPRECATE
#include <stdio.h>
#include <windows.h>
#include <strsafe.h>

#include "Config.h"
#include "State.h"

static int g_failures = 0;

static void Expect(BOOL condition, const char *name)
{
    if (condition)
    {
        printf("PASS: %s\n", name);
        return;
    }

    fprintf(stderr, "FAIL: %s\n", name);
    g_failures++;
}

static void TestState(void)
{
    Expect(LsShouldPark(2531310, TRUE), "RunningAppID with Running flag parks");
    Expect(!LsShouldPark(0, FALSE), "idle Steam does not park");
    Expect(!LsShouldPark(2531310, FALSE), "RunningAppID without Running flag does not park");
    Expect(!LsShouldPark(0, TRUE), "Running flag without RunningAppID does not park");
}

static void TestParsing(void)
{
    Expect(LsParseMode(L"freeze") == LS_MODE_FREEZE, "mode parse is exact");
    Expect(LsParseMode(L"THROTTLE") == LS_MODE_THROTTLE, "mode parse ignores case");
    Expect(LsParseMode(L"kill") == LS_MODE_INVALID, "unknown mode is invalid");
    Expect(LsParsePreset(L"slay") == LS_PRESET_SLAY, "preset parse ignores case");
    Expect(LsParsePreset(L"Limit Steam") == LS_PRESET_LIMIT, "preset accepts display name");
    Expect(LsParsePreset(L"nope") == LS_PRESET_INVALID, "unknown preset is invalid");
}

static void TestPresets(void)
{
    LS_MODE modes[LS_TARGET_COUNT] = {LS_MODE_OFF};

    LsGetPresetModes(LS_PRESET_LIMIT, modes);
    Expect(modes[LS_TARGET_WEBHELPER] != LS_MODE_FREEZE && modes[LS_TARGET_OVERLAY] != LS_MODE_FREEZE,
           "Limit never freezes WebHelper or the overlay");
    Expect(modes[LS_TARGET_OVERLAY] == LS_MODE_TRIM, "Limit only trims the overlay (no CPU throttling)");

    LsGetPresetModes(LS_PRESET_SLAY, modes);
    Expect(modes[LS_TARGET_WEBHELPER] == LS_MODE_FREEZE, "Slay freezes WebHelper");
    Expect(modes[LS_TARGET_STEAMSERVICE] == LS_MODE_FREEZE, "Slay freezes SteamService");
    Expect(modes[LS_TARGET_STEAMCLIENT] != LS_MODE_FREEZE, "Slay never freezes steam.exe");

    Expect(LsClampMode(LS_TARGET_STEAMCLIENT, LS_MODE_FREEZE) == LS_MODE_IDLE, "steam.exe freeze clamps to idle");
    Expect(LsClampMode(LS_TARGET_WEBHELPER, LS_MODE_FREEZE) == LS_MODE_FREEZE, "WebHelper freeze is allowed");
}

static BOOL WriteTempIni(LPWSTR path, DWORD capacity, const char *content)
{
    WCHAR directory[MAX_PATH] = {0};
    HANDLE file = INVALID_HANDLE_VALUE;
    DWORD written = 0;
    BOOL ok = FALSE;

    if (GetTempPathW(ARRAYSIZE(directory), directory) == 0 || GetTempFileNameW(directory, L"lss", 0, path) == 0)
        return FALSE;
    UNREFERENCED_PARAMETER(capacity);

    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;

    ok = WriteFile(file, content, (DWORD)lstrlenA(content), &written, NULL);
    CloseHandle(file);
    return ok;
}

static void TestLoadConfig(void)
{
    WCHAR path[MAX_PATH] = {0};
    LS_CONFIG config = {0};

    LsLoadConfig(L"Z:\\does\\not\\exist\\LessSteam.ini", &config);
    Expect(config.preset == LS_PRESET_LIMIT, "missing ini defaults to Limit");
    Expect(config.trimIntervalMs == 10000, "missing ini uses Limit's trim interval");
    Expect(!config.paused, "missing ini is not paused");

    if (!WriteTempIni(path, ARRAYSIZE(path),
                      "[General]\r\nPreset=Custom\r\nTrimIntervalMs=10\r\nLog=0\r\n"
                      "[Custom]\r\nWebHelper=freeze\r\nOverlay=bogus\r\nSteamClient=freeze\r\n"))
    {
        Expect(FALSE, "write temporary ini");
        return;
    }

    LsLoadConfig(path, &config);
    Expect(config.preset == LS_PRESET_CUSTOM, "Custom preset is read");
    Expect(config.modes[LS_TARGET_WEBHELPER] == LS_MODE_FREEZE, "Custom mode is read");
    Expect(config.modes[LS_TARGET_OVERLAY] == LS_MODE_TRIM, "invalid Custom mode falls back to Limit");
    Expect(config.modes[LS_TARGET_STEAMSERVICE] == LS_MODE_IDLE, "missing Custom key falls back to Limit");
    Expect(config.modes[LS_TARGET_STEAMCLIENT] == LS_MODE_IDLE, "Custom steam.exe freeze is clamped");
    Expect(config.trimIntervalMs == LS_MIN_TRIM_INTERVAL_MS, "trim interval is clamped");
    Expect(!config.logEnabled, "Log=0 disables logging");

    Expect(LsWritePreset(path, LS_PRESET_SLAY), "preset can be written");
    LsLoadConfig(path, &config);
    Expect(config.preset == LS_PRESET_SLAY && config.modes[LS_TARGET_WEBHELPER] == LS_MODE_FREEZE,
           "written preset is applied");
    Expect(config.trimIntervalMs == LS_MIN_TRIM_INTERVAL_MS, "explicit trim interval overrides the preset");

    Expect(LsWritePaused(path, TRUE), "Paused can be written");
    LsLoadConfig(path, &config);
    Expect(config.paused, "Paused=1 is read");
    LsWritePaused(path, FALSE);
    LsLoadConfig(path, &config);
    Expect(!config.paused, "Paused=0 is read");

    Expect(LsDefaultTrimInterval(LS_PRESET_SLAY) == 1000, "Slay trims every second");
    Expect(LsDefaultTrimInterval(LS_PRESET_LIMIT) == 10000, "Limit trims every ten seconds");

    DeleteFileW(path);
}

static void TestShippedIni(LPCWSTR path)
{
    LS_CONFIG config = {0};
    LS_MODE limit[LS_TARGET_COUNT] = {LS_MODE_OFF};
    int target = 0;
    BOOL same = TRUE;

    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
    {
        Expect(FALSE, "shipped LessSteam.ini exists");
        return;
    }

    LsLoadConfig(path, &config);
    Expect(config.preset == LS_PRESET_LIMIT, "shipped ini selects Limit");

    LsGetPresetModes(LS_PRESET_LIMIT, limit);
    for (; target < LS_TARGET_COUNT; target++)
    {
        WCHAR text[64] = {0};

        GetPrivateProfileStringW(L"Custom", LsTargetKey((LS_TARGET)target), L"", text, ARRAYSIZE(text), path);
        same = same && LsParseMode(text) == limit[target];
    }
    Expect(same, "shipped [Custom] section matches the Limit preset");
}

int wmain(int argc, wchar_t **argv)
{
    TestState();
    TestParsing();
    TestPresets();
    TestLoadConfig();
    if (argc > 1)
        TestShippedIni(argv[1]);

    return g_failures == 0 ? 0 : 1;
}
