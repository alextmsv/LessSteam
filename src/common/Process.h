#ifndef LESSSTEAM_PROCESS_H
#define LESSSTEAM_PROCESS_H

#include <windows.h>

typedef struct LS_PROCESS_NODE
{
    DWORD processId;
    DWORD parentProcessId;
    WCHAR imageName[MAX_PATH];
} LS_PROCESS_NODE;

#define LS_MAX_TRACKED_PROCESSES 2048

LS_PROCESS_NODE *LsAllocProcessBuffer(void);
void LsFreeProcessBuffer(LS_PROCESS_NODE *processes);

DWORD LsSnapshotProcesses(LS_PROCESS_NODE *processes, DWORD capacity);
BOOL LsIsDescendantProcess(const LS_PROCESS_NODE *processes, DWORD count, DWORD processId, DWORD ancestorProcessId);
BOOL LsIsProcessInSession(DWORD processId, DWORD sessionId);
BOOL LsImageIs(LPCWSTR imageName, LPCWSTR expected);

#endif
