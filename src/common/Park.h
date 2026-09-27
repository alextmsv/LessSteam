#ifndef LESSSTEAM_PARK_H
#define LESSSTEAM_PARK_H

#include <windows.h>
#include "Config.h"

typedef struct LS_PARKED
{
    DWORD processId;
    HANDLE processHandle;
    LS_TARGET target;
    LS_MODE mode;
    BOOL frozen;
    BOOL priorityApplied;
    DWORD originalPriorityClass;
    BOOL efficiencyApplied;
    ULONG originalThrottleControlMask;
    ULONG originalThrottleStateMask;
    BOOL memoryPriorityApplied;
    DWORD originalMemoryPriority;
    ULONGLONG lastCpuTime;
    ULONGLONG lastSampleTick;
} LS_PARKED;

#define LS_MAX_PARKED 64

#define LS_ACTIVE_CPU_PERCENT 10
#define LS_ACTIVE_GRACE_MS 30000ULL

typedef struct LS_PARK_TABLE
{
    LS_PARKED entries[LS_MAX_PARKED];
    LONG count;
    ULONGLONG activeUntil;
} LS_PARK_TABLE;

void LsParkInit(void);
BOOL LsParkIsTracked(const LS_PARK_TABLE *table, DWORD processId);
BOOL LsParkProcess(LS_PARK_TABLE *table, DWORD processId, LS_TARGET target, LS_MODE mode);
void LsParkSample(LS_PARK_TABLE *table);
void LsParkTrim(LS_PARK_TABLE *table);
void LsParkRestoreAll(LS_PARK_TABLE *table);
BOOL LsResumeProcessId(DWORD processId);

#endif
