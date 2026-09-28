#ifndef LESSSTEAM_TASK_H
#define LESSSTEAM_TASK_H

#include <windows.h>

#define LS_TASK_NAME L"LessSteam"
#define LS_HELPER_MUTEX_NAME L"Local\\LessSteamHelper"

DWORD LsRunHidden(LPWSTR commandLine, DWORD timeoutMs);
BOOL LsBuildSchtasksCommand(LPWSTR buffer, size_t capacity, LPCWSTR arguments);
BOOL LsIsHelperRunning(void);
BOOL LsRunHelperTask(void);

#endif
