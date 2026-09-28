#include "Park.h"
#include "Log.h"

typedef BOOL(WINAPI *pfnGetProcessInformation)(HANDLE, PROCESS_INFORMATION_CLASS, LPVOID, DWORD);
typedef BOOL(WINAPI *pfnSetProcessInformation)(HANDLE, PROCESS_INFORMATION_CLASS, LPVOID, DWORD);
typedef LONG(NTAPI *pfnNtProcessControl)(HANDLE);

static pfnGetProcessInformation g_GetProcessInformation = NULL;
static pfnSetProcessInformation g_SetProcessInformation = NULL;
static pfnNtProcessControl g_NtSuspendProcess = NULL;
static pfnNtProcessControl g_NtResumeProcess = NULL;

static const LPCWSTR g_targetNames[LS_TARGET_COUNT] = {
    L"webhelper",
    L"overlay",
    L"steamclient",
    L"steamservice",
};

void LsParkInit(void)
{
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");

    if (kernel32 != NULL)
    {
        g_GetProcessInformation = (pfnGetProcessInformation)GetProcAddress(kernel32, "GetProcessInformation");
        g_SetProcessInformation = (pfnSetProcessInformation)GetProcAddress(kernel32, "SetProcessInformation");
    }

    if (ntdll != NULL)
    {
        g_NtSuspendProcess = (pfnNtProcessControl)GetProcAddress(ntdll, "NtSuspendProcess");
        g_NtResumeProcess = (pfnNtProcessControl)GetProcAddress(ntdll, "NtResumeProcess");
    }

    if (g_SetProcessInformation == NULL)
        LsLog(L"WARNING: SetProcessInformation unavailable; EcoQoS and memory priority disabled");
    if (g_NtSuspendProcess == NULL || g_NtResumeProcess == NULL)
        LsLog(L"WARNING: NtSuspendProcess/NtResumeProcess unavailable; freeze mode disabled");
}

static LPCWSTR TargetName(LS_TARGET target)
{
    if (target < LS_TARGET_WEBHELPER || target >= LS_TARGET_COUNT)
        return L"unknown";

    return g_targetNames[target];
}

static DWORD PriorityClassForMode(LS_MODE mode)
{
    switch (mode)
    {
    case LS_MODE_THROTTLE:
        return BELOW_NORMAL_PRIORITY_CLASS;
    case LS_MODE_IDLE:
        return IDLE_PRIORITY_CLASS;
    default:
        return 0;
    }
}

static BOOL ModeUsesEfficiencyMode(LS_MODE mode)
{
    return mode == LS_MODE_THROTTLE || mode == LS_MODE_IDLE;
}

static BOOL SetPowerThrottling(HANDLE processHandle, ULONG controlMask, ULONG stateMask)
{
    PROCESS_POWER_THROTTLING_STATE ppt = {0};

    if (g_SetProcessInformation == NULL)
    {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return FALSE;
    }

    ppt.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    ppt.ControlMask = controlMask;
    ppt.StateMask = stateMask;
    return g_SetProcessInformation(processHandle, ProcessPowerThrottling, &ppt, sizeof(ppt));
}

static BOOL SetMemoryPriorityValue(HANDLE processHandle, DWORD priority)
{
    MEMORY_PRIORITY_INFORMATION mpi = {0};

    if (g_SetProcessInformation == NULL)
    {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return FALSE;
    }

    mpi.MemoryPriority = priority;
    return g_SetProcessInformation(processHandle, ProcessMemoryPriority, &mpi, sizeof(mpi));
}

static BOOL TrimWorkingSet(HANDLE processHandle)
{
    return SetProcessWorkingSetSize(processHandle, (SIZE_T)-1, (SIZE_T)-1);
}

static ULONGLONG QueryCpuTime(HANDLE processHandle)
{
    FILETIME creation = {0};
    FILETIME exitTime = {0};
    FILETIME kernel = {0};
    FILETIME user = {0};

    if (!GetProcessTimes(processHandle, &creation, &exitTime, &kernel, &user))
        return 0;

    return (((ULONGLONG)kernel.dwHighDateTime << 32) | kernel.dwLowDateTime) +
           (((ULONGLONG)user.dwHighDateTime << 32) | user.dwLowDateTime);
}

BOOL LsParkIsTracked(const LS_PARK_TABLE *table, DWORD processId)
{
    LONG index = 0;

    for (; index < table->count; index++)
    {
        if (table->entries[index].processId == processId)
            return TRUE;
    }

    return FALSE;
}

BOOL LsParkProcess(LS_PARK_TABLE *table, DWORD processId, LS_TARGET target, LS_MODE mode)
{
    DWORD access = PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_INFORMATION | PROCESS_SET_QUOTA | SYNCHRONIZE;
    DWORD priorityClass = PriorityClassForMode(mode);
    HANDLE processHandle = NULL;
    LS_PARKED *entry = NULL;
    BOOL trimmed = FALSE;

    if (table == NULL || mode <= LS_MODE_OFF || mode >= LS_MODE_INVALID)
        return FALSE;

    if (LsParkIsTracked(table, processId))
        return TRUE;

    if (table->count >= LS_MAX_PARKED)
    {
        LsLog(L"park table full; skipping %s pid=%lu", TargetName(target), processId);
        return FALSE;
    }

    if (mode == LS_MODE_FREEZE)
        access |= PROCESS_SUSPEND_RESUME;

    processHandle = OpenProcess(access, FALSE, processId);
    if (processHandle == NULL)
    {
        LsLog(L"OpenProcess %s pid=%lu FAILED error=%lu", TargetName(target), processId, GetLastError());
        return FALSE;
    }

    entry = &table->entries[table->count];
    ZeroMemory(entry, sizeof(*entry));
    entry->processId = processId;
    entry->processHandle = processHandle;
    entry->target = target;
    entry->mode = mode;

    if (priorityClass != 0)
    {
        entry->originalPriorityClass = GetPriorityClass(processHandle);
        entry->priorityApplied = entry->originalPriorityClass != 0 &&
                                 SetPriorityClass(processHandle, priorityClass);
    }

    if (ModeUsesEfficiencyMode(mode))
    {
        PROCESS_POWER_THROTTLING_STATE original = {0};

        original.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        if (g_GetProcessInformation != NULL &&
            g_GetProcessInformation(processHandle, ProcessPowerThrottling, &original, sizeof(original)))
        {
            entry->originalThrottleControlMask = original.ControlMask;
            entry->originalThrottleStateMask = original.StateMask;
        }

        entry->efficiencyApplied = SetPowerThrottling(processHandle, PROCESS_POWER_THROTTLING_EXECUTION_SPEED,
                                                      PROCESS_POWER_THROTTLING_EXECUTION_SPEED);
    }

    {
        MEMORY_PRIORITY_INFORMATION original = {0};

        entry->originalMemoryPriority = MEMORY_PRIORITY_NORMAL;
        if (g_GetProcessInformation != NULL &&
            g_GetProcessInformation(processHandle, ProcessMemoryPriority, &original, sizeof(original)))
            entry->originalMemoryPriority = original.MemoryPriority;

        entry->memoryPriorityApplied = SetMemoryPriorityValue(processHandle, MEMORY_PRIORITY_VERY_LOW);
    }

    trimmed = TrimWorkingSet(processHandle);
    entry->lastCpuTime = QueryCpuTime(processHandle);
    entry->lastSampleTick = GetTickCount64();

    if (mode == LS_MODE_FREEZE)
        entry->frozen = g_NtSuspendProcess != NULL && g_NtSuspendProcess(processHandle) >= 0;

    table->count++;

    LsLog(L"park %s pid=%lu mode=%s priority=%d eco=%d mempri=%d trim=%d frozen=%d",
          TargetName(target), processId, LsModeName(mode), entry->priorityApplied, entry->efficiencyApplied,
          entry->memoryPriorityApplied, trimmed, entry->frozen);
    return TRUE;
}

static void RestoreEntry(LS_PARKED *entry)
{
    HANDLE processHandle = entry->processHandle;

    if (processHandle == NULL)
        return;

    if (WaitForSingleObject(processHandle, 0) == WAIT_TIMEOUT)
    {
        if (entry->frozen && g_NtResumeProcess != NULL && g_NtResumeProcess(processHandle) < 0)
            LsLog(L"NtResumeProcess %s pid=%lu FAILED", TargetName(entry->target), entry->processId);

        if (entry->priorityApplied)
            SetPriorityClass(processHandle, entry->originalPriorityClass);

        if (entry->efficiencyApplied)
            SetPowerThrottling(processHandle, entry->originalThrottleControlMask, entry->originalThrottleStateMask);

        if (entry->memoryPriorityApplied)
            SetMemoryPriorityValue(processHandle, entry->originalMemoryPriority);

        LsLog(L"restore %s pid=%lu", TargetName(entry->target), entry->processId);
    }

    CloseHandle(processHandle);
    ZeroMemory(entry, sizeof(*entry));
}

void LsParkSample(LS_PARK_TABLE *table)
{
    ULONGLONG now = GetTickCount64();
    LONG index = 0;

    while (index < table->count)
    {
        LS_PARKED *entry = &table->entries[index];
        ULONGLONG cpuTime = 0;
        ULONGLONG elapsedMs = 0;

        if (WaitForSingleObject(entry->processHandle, 0) != WAIT_TIMEOUT)
        {
            LONG last = table->count - 1;

            LsLog(L"%s pid=%lu exited", TargetName(entry->target), entry->processId);
            CloseHandle(entry->processHandle);
            if (index != last)
                *entry = table->entries[last];
            ZeroMemory(&table->entries[last], sizeof(table->entries[last]));
            table->count--;
            continue;
        }

        elapsedMs = now - entry->lastSampleTick;
        if (elapsedMs < 500)
        {
            index++;
            continue;
        }

        cpuTime = QueryCpuTime(entry->processHandle);
        if (entry->target != LS_TARGET_STEAMCLIENT && cpuTime >= entry->lastCpuTime)
        {
            ULONGLONG percent = (cpuTime - entry->lastCpuTime) / (elapsedMs * 100);

            if (percent >= LS_ACTIVE_CPU_PERCENT)
            {
                if (now >= table->activeUntil)
                    LsLog(L"%s pid=%lu busy (%llu%% CPU); holding off trims", TargetName(entry->target),
                          entry->processId, percent);
                table->activeUntil = now + LS_ACTIVE_GRACE_MS;
            }
        }

        entry->lastCpuTime = cpuTime;
        entry->lastSampleTick = now;
        index++;
    }
}

void LsParkTrim(LS_PARK_TABLE *table)
{
    LONG index = 0;

    if (GetTickCount64() < table->activeUntil)
        return;

    for (; index < table->count; index++)
    {
        LS_PARKED *entry = &table->entries[index];

        if (!entry->frozen && entry->target != LS_TARGET_STEAMCLIENT)
            TrimWorkingSet(entry->processHandle);
    }
}

void LsParkRestoreAll(LS_PARK_TABLE *table)
{
    LONG index = 0;

    for (; index < table->count && index < LS_MAX_PARKED; index++)
        RestoreEntry(&table->entries[index]);

    table->count = 0;
    table->activeUntil = 0;
}

BOOL LsResumeProcessId(DWORD processId)
{
    HANDLE processHandle = NULL;
    BOOL resumed = FALSE;

    if (g_NtResumeProcess == NULL)
        return FALSE;

    processHandle = OpenProcess(PROCESS_SUSPEND_RESUME, FALSE, processId);
    if (processHandle == NULL)
        return FALSE;

    resumed = g_NtResumeProcess(processHandle) >= 0;
    CloseHandle(processHandle);
    return resumed;
}
