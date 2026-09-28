#include <windows.h>
#include "Config.h"
#include "Log.h"
#include "Park.h"
#include "Process.h"
#include "Steam.h"
#include "Task.h"

#define RESCAN_INTERVAL_MS 15000ULL

static HANDLE g_stopEvent = NULL;
static LS_PARK_TABLE g_table = {0};
static WCHAR g_configPath[MAX_PATH] = {0};

static BOOL IsSteamClientProcess(void)
{
    WCHAR modulePath[MAX_PATH] = {0};
    LPCWSTR fileName = NULL;

    if (GetModuleFileNameW(NULL, modulePath, ARRAYSIZE(modulePath)) == 0)
        return FALSE;

    fileName = wcsrchr(modulePath, L'\\');
    fileName = (fileName == NULL) ? modulePath : fileName + 1;
    return LsImageIs(fileName, L"steam.exe");
}

static BOOL TargetForImage(LPCWSTR imageName, LS_TARGET *target)
{
    if (LsImageIs(imageName, L"steamwebhelper.exe"))
    {
        *target = LS_TARGET_WEBHELPER;
        return TRUE;
    }

    if (LsImageIs(imageName, L"gameoverlayui.exe") || LsImageIs(imageName, L"gameoverlayui64.exe"))
    {
        *target = LS_TARGET_OVERLAY;
        return TRUE;
    }

    return FALSE;
}

static BOOL ShouldApplyNow(LS_MODE mode, BOOL freezeAllowed)
{
    return mode != LS_MODE_OFF && (mode != LS_MODE_FREEZE || freezeAllowed);
}

static void ParkTargets(const LS_CONFIG *config, const LS_PROCESS_NODE *processes, DWORD count,
                        DWORD steamProcessId, DWORD steamSessionId, BOOL freezeAllowed)
{
    DWORD index = 0;

    for (; index < count; index++)
    {
        DWORD processId = processes[index].processId;
        LS_TARGET target = LS_TARGET_COUNT;
        LS_MODE mode = LS_MODE_OFF;

        if (!TargetForImage(processes[index].imageName, &target))
            continue;

        mode = config->modes[target];
        if (!ShouldApplyNow(mode, freezeAllowed) || LsParkIsTracked(&g_table, processId))
            continue;

        if (!LsIsDescendantProcess(processes, count, processId, steamProcessId) &&
            !LsIsProcessInSession(processId, steamSessionId))
            continue;

        LsParkProcess(&g_table, processId, target, mode);
    }

    if (ShouldApplyNow(config->modes[LS_TARGET_STEAMCLIENT], freezeAllowed) &&
        !LsParkIsTracked(&g_table, steamProcessId))
        LsParkProcess(&g_table, steamProcessId, LS_TARGET_STEAMCLIENT, config->modes[LS_TARGET_STEAMCLIENT]);
}

static BOOL IsSteamInForeground(DWORD steamProcessId)
{
    HWND foreground = GetForegroundWindow();
    DWORD processId = 0;

    if (foreground == NULL)
        return FALSE;

    GetWindowThreadProcessId(foreground, &processId);
    return processId == steamProcessId || LsParkIsTracked(&g_table, processId);
}

static DWORD WINAPI MonitorThreadProc(LPVOID parameter)
{
    DWORD steamProcessId = GetCurrentProcessId();
    DWORD steamSessionId = 0;
    HMODULE pinnedModule = NULL;
    LS_PROCESS_NODE *processes = NULL;
    LS_CONFIG config = {0};
    FILETIME configWriteTime = {0};
    BOOL parked = FALSE;
    ULONGLONG gameStart = 0;
    ULONGLONG lastScan = 0;
    ULONGLONG lastTrim = 0;

    UNREFERENCED_PARAMETER(parameter);

    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            (LPCWSTR)MonitorThreadProc, &pinnedModule) ||
        pinnedModule == NULL)
        return 0;

    if (WaitForSingleObject(g_stopEvent, 5000) != WAIT_TIMEOUT)
        return 0;

    ProcessIdToSessionId(steamProcessId, &steamSessionId);
    LsParkInit();

    LsBuildConfigPath(pinnedModule, g_configPath, ARRAYSIZE(g_configPath));
    LsConfigFileChanged(g_configPath, &configWriteTime);
    LsLoadConfig(g_configPath, &config);
    LsLogSetEnabled(config.logEnabled);
    LsLog(L"monitor started; preset=%s", LsPresetName(config.preset));

    if (config.autostartWithSteam && !LsIsHelperRunning())
        LsLog(LsRunHelperTask() ? L"started LessSteamHelper via scheduled task"
                                : L"cannot start LessSteamHelper; run LessSteamHelper.exe once to register it");

    processes = LsAllocProcessBuffer();
    if (processes == NULL)
    {
        LsLog(L"cannot allocate the process snapshot buffer");
        return 0;
    }

    while (WaitForSingleObject(g_stopEvent, 1000) == WAIT_TIMEOUT)
    {
        DWORD appId = 0;
        ULONGLONG now = GetTickCount64();

        if (LsConfigFileChanged(g_configPath, &configWriteTime))
        {
            LS_CONFIG previous = config;

            LsLoadConfig(g_configPath, &config);
            LsLogSetEnabled(config.logEnabled);

            if (!LsParkingConfigEqual(&previous, &config))
            {
                LsLog(L"config reloaded; preset=%s paused=%d", LsPresetName(config.preset), config.paused);
                if (parked)
                {
                    LsParkRestoreAll(&g_table);
                    parked = FALSE;
                }
                continue;
            }
        }

        if (!config.paused && LsIsSteamGameRunning(&appId))
        {
            BOOL freezeAllowed = FALSE;

            if (!parked)
            {
                parked = TRUE;
                gameStart = now;
                lastScan = 0;
                lastTrim = now;
                LsLog(L"game detected: appid=%lu", appId);
            }

            freezeAllowed = now - gameStart >= LS_FREEZE_DELAY_MS;

            if (lastScan == 0 || now - lastScan >= RESCAN_INTERVAL_MS)
            {
                DWORD count = LsSnapshotProcesses(processes, LS_MAX_TRACKED_PROCESSES);

                ParkTargets(&config, processes, count, steamProcessId, steamSessionId, freezeAllowed);
                lastScan = now;
            }

            LsParkSample(&g_table);

            if (now - lastTrim >= config.trimIntervalMs)
            {
                if (!IsSteamInForeground(steamProcessId))
                    LsParkTrim(&g_table);
                lastTrim = now;
            }
        }
        else if (parked)
        {
            LsLog(config.paused ? L"paused; restoring" : L"game ended; restoring");
            LsParkRestoreAll(&g_table);
            parked = FALSE;
        }
    }

    LsFreeProcessBuffer(processes);
    LsParkRestoreAll(&g_table);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE instanceHandle, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        HANDLE monitorThread = NULL;

        DisableThreadLibraryCalls(instanceHandle);

        if (!IsSteamClientProcess())
            return TRUE;

        LsLogInit(L"dll");

        g_stopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (g_stopEvent == NULL)
            return TRUE;

        monitorThread = CreateThread(NULL, 0, MonitorThreadProc, NULL, 0, NULL);
        if (monitorThread == NULL)
        {
            CloseHandle(g_stopEvent);
            g_stopEvent = NULL;
            return TRUE;
        }

        CloseHandle(monitorThread);
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        if (reserved == NULL && g_stopEvent != NULL)
            SetEvent(g_stopEvent);
    }

    return TRUE;
}
