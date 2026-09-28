#define STRSAFE_NO_DEPRECATE
#include <strsafe.h>
#include "Log.h"

static WCHAR g_component[16] = L"lessteam";
static volatile LONG g_logEnabled = TRUE;

void LsLogInit(LPCWSTR component)
{
    if (component != NULL)
        StringCchCopyW(g_component, ARRAYSIZE(g_component), component);
}

void LsLogSetEnabled(BOOL enabled)
{
    InterlockedExchange(&g_logEnabled, enabled ? TRUE : FALSE);
}

static BOOL BuildLogPath(LPWSTR path, DWORD capacity, LPCWSTR fileName)
{
    return GetTempPathW(capacity, path) != 0 && SUCCEEDED(StringCchCatW(path, capacity, fileName));
}

void LsLogRotate(void)
{
    WCHAR logPath[MAX_PATH] = {0};
    WCHAR previousPath[MAX_PATH] = {0};

    if (BuildLogPath(logPath, ARRAYSIZE(logPath), L"LessSteam.log") &&
        BuildLogPath(previousPath, ARRAYSIZE(previousPath), L"LessSteam.prev.log"))
        MoveFileExW(logPath, previousPath, MOVEFILE_REPLACE_EXISTING);
}

void LsLogRotateIfLarger(ULONGLONG maxBytes)
{
    WCHAR logPath[MAX_PATH] = {0};
    WIN32_FILE_ATTRIBUTE_DATA data = {0};

    if (BuildLogPath(logPath, ARRAYSIZE(logPath), L"LessSteam.log") &&
        GetFileAttributesExW(logPath, GetFileExInfoStandard, &data) &&
        (((ULONGLONG)data.nFileSizeHigh << 32) | data.nFileSizeLow) > maxBytes)
        LsLogRotate();
}

void LsLog(LPCWSTR format, ...)
{
    WCHAR logPath[MAX_PATH] = {0};
    WCHAR message[768] = {0};
    WCHAR line[1024] = {0};
    CHAR utf8[3072] = {0};
    SYSTEMTIME st = {0};
    DWORD written = 0;
    HANDLE fileHandle = INVALID_HANDLE_VALUE;
    int utf8Length = 0;
    va_list args;

    if (!g_logEnabled || format == NULL)
        return;

    if (!BuildLogPath(logPath, ARRAYSIZE(logPath), L"LessSteam.log"))
        return;

    va_start(args, format);
    StringCchVPrintfW(message, ARRAYSIZE(message), format, args);
    va_end(args);

    GetLocalTime(&st);
    StringCchPrintfW(line, ARRAYSIZE(line), L"[%04u-%02u-%02u %02u:%02u:%02u.%03u] [%s %lu] %s\r\n",
                     st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                     g_component, GetCurrentProcessId(), message);

    utf8Length = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, (int)sizeof(utf8), NULL, NULL);
    if (utf8Length <= 1)
        return;

    fileHandle = CreateFileW(logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (fileHandle == INVALID_HANDLE_VALUE)
        return;

    WriteFile(fileHandle, utf8, (DWORD)(utf8Length - 1), &written, NULL);
    CloseHandle(fileHandle);
}
