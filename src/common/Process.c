#define STRSAFE_NO_DEPRECATE
#include <windows.h>
#include <tlhelp32.h>
#include <strsafe.h>
#include "Process.h"

LS_PROCESS_NODE *LsAllocProcessBuffer(void)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(LS_PROCESS_NODE) * LS_MAX_TRACKED_PROCESSES);
}

void LsFreeProcessBuffer(LS_PROCESS_NODE *processes)
{
    if (processes != NULL)
        HeapFree(GetProcessHeap(), 0, processes);
}

DWORD LsSnapshotProcesses(LS_PROCESS_NODE *processes, DWORD capacity)
{
    HANDLE snapshot = INVALID_HANDLE_VALUE;
    PROCESSENTRY32W entry = {0};
    DWORD count = 0;

    if (processes == NULL)
        return 0;

    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;

    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            if (count >= capacity)
                break;

            processes[count].processId = entry.th32ProcessID;
            processes[count].parentProcessId = entry.th32ParentProcessID;
            if (FAILED(StringCchCopyW(processes[count].imageName, ARRAYSIZE(processes[count].imageName),
                                      entry.szExeFile)))
                processes[count].imageName[0] = L'\0';
            count++;
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return count;
}

static const LS_PROCESS_NODE *FindProcessNode(const LS_PROCESS_NODE *processes, DWORD count, DWORD processId)
{
    DWORD index = 0;

    for (; index < count; index++)
    {
        if (processes[index].processId == processId)
            return &processes[index];
    }

    return NULL;
}

BOOL LsIsDescendantProcess(const LS_PROCESS_NODE *processes, DWORD count, DWORD processId, DWORD ancestorProcessId)
{
    DWORD currentProcessId = processId;
    DWORD depth = 0;

    while (currentProcessId != 0 && depth++ < count)
    {
        const LS_PROCESS_NODE *node = NULL;

        if (currentProcessId == ancestorProcessId)
            return TRUE;

        node = FindProcessNode(processes, count, currentProcessId);
        if (node == NULL || node->parentProcessId == currentProcessId)
            break;

        currentProcessId = node->parentProcessId;
    }

    return FALSE;
}

BOOL LsIsProcessInSession(DWORD processId, DWORD sessionId)
{
    DWORD processSessionId = 0;

    return ProcessIdToSessionId(processId, &processSessionId) && processSessionId == sessionId;
}

BOOL LsImageIs(LPCWSTR imageName, LPCWSTR expected)
{
    return CompareStringOrdinal(imageName, -1, expected, -1, TRUE) == CSTR_EQUAL;
}
