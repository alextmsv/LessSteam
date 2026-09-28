#define STRSAFE_NO_DEPRECATE
#include <strsafe.h>
#include "Task.h"

DWORD LsRunHidden(LPWSTR commandLine, DWORD timeoutMs)
{
    STARTUPINFOW startup = {0};
    PROCESS_INFORMATION process = {0};
    DWORD exitCode = (DWORD)-1;

    startup.cb = sizeof(startup);
    if (!CreateProcessW(NULL, commandLine, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &startup, &process))
        return (DWORD)-1;

    if (WaitForSingleObject(process.hProcess, timeoutMs) == WAIT_OBJECT_0)
        GetExitCodeProcess(process.hProcess, &exitCode);

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return exitCode;
}

BOOL LsBuildSchtasksCommand(LPWSTR buffer, size_t capacity, LPCWSTR arguments)
{
    WCHAR systemDirectory[MAX_PATH] = {0};

    if (GetSystemDirectoryW(systemDirectory, ARRAYSIZE(systemDirectory)) == 0)
        return FALSE;

    return SUCCEEDED(StringCchPrintfW(buffer, capacity, L"\"%s\\schtasks.exe\" %s", systemDirectory, arguments));
}

BOOL LsIsHelperRunning(void)
{
    HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, LS_HELPER_MUTEX_NAME);

    if (mutex != NULL)
    {
        CloseHandle(mutex);
        return TRUE;
    }

    return GetLastError() == ERROR_ACCESS_DENIED;
}

BOOL LsRunHelperTask(void)
{
    WCHAR command[MAX_PATH * 2] = {0};

    if (!LsBuildSchtasksCommand(command, ARRAYSIZE(command), L"/Run /TN \"" LS_TASK_NAME L"\""))
        return FALSE;

    return LsRunHidden(command, 15000) == 0;
}
