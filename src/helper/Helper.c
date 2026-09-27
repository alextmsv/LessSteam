#define STRSAFE_NO_DEPRECATE
#define SECURITY_WIN32
#include <windows.h>
#include <shellapi.h>
#include <security.h>
#include <strsafe.h>
#include "Config.h"
#include "Log.h"
#include "Park.h"
#include "Process.h"
#include "Steam.h"
#include "resource.h"

#define WM_LS_TRAY (WM_APP + 1)
#define TICK_TIMER_ID 1
#define TICK_INTERVAL_MS 1000

#define ID_PRESET_LIMIT 1001
#define ID_PRESET_SLAY 1002
#define ID_PRESET_CUSTOM 1003
#define ID_OPEN_CONFIG 1010
#define ID_AUTOSTART 1011
#define ID_PAUSE 1012
#define ID_EXIT 1020

#define TASK_NAME L"LessSteam"
#define STEAM_SCAN_INTERVAL_MS 5000ULL
#define SERVICE_SCAN_INTERVAL_MS 5000ULL

static HWND g_window = NULL;
static UINT g_taskbarCreatedMessage = 0;
static NOTIFYICONDATAW g_notifyIcon = {0};
static WCHAR g_configPath[MAX_PATH] = {0};
static FILETIME g_configWriteTime = {0};
static LS_CONFIG g_config = {0};
static LS_PARK_TABLE g_table = {0};
static LS_PROCESS_NODE *g_processes = NULL;
static DWORD g_sessionId = 0;
static HANDLE g_steamProcess = NULL;
static ULONGLONG g_lastSteamScan = 0;
static BOOL g_parked = FALSE;
static DWORD g_appId = 0;
static ULONGLONG g_gameStart = 0;
static ULONGLONG g_lastServiceScan = 0;
static ULONGLONG g_lastTrim = 0;
static BOOL g_autostartInstalled = FALSE;

static BOOL EnableDebugPrivilege(void)
{
    HANDLE token = NULL;
    TOKEN_PRIVILEGES privileges = {0};
    BOOL enabled = FALSE;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
        return FALSE;

    privileges.PrivilegeCount = 1;
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (LookupPrivilegeValueW(NULL, SE_DEBUG_NAME, &privileges.Privileges[0].Luid) &&
        AdjustTokenPrivileges(token, FALSE, &privileges, sizeof(privileges), NULL, NULL))
        enabled = GetLastError() == ERROR_SUCCESS;

    CloseHandle(token);
    return enabled;
}

static DWORD RunHidden(LPWSTR commandLine)
{
    STARTUPINFOW startup = {0};
    PROCESS_INFORMATION process = {0};
    DWORD exitCode = (DWORD)-1;

    startup.cb = sizeof(startup);
    if (!CreateProcessW(NULL, commandLine, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &startup, &process))
        return (DWORD)-1;

    if (WaitForSingleObject(process.hProcess, 15000) == WAIT_OBJECT_0)
        GetExitCodeProcess(process.hProcess, &exitCode);

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return exitCode;
}

static BOOL BuildSchtasksCommand(LPWSTR buffer, size_t capacity, LPCWSTR arguments)
{
    WCHAR systemDirectory[MAX_PATH] = {0};

    if (GetSystemDirectoryW(systemDirectory, ARRAYSIZE(systemDirectory)) == 0)
        return FALSE;

    return SUCCEEDED(StringCchPrintfW(buffer, capacity, L"\"%s\\schtasks.exe\" %s", systemDirectory, arguments));
}

static void AppendXmlEscaped(LPWSTR buffer, size_t capacity, LPCWSTR text)
{
    for (; *text != L'\0'; text++)
    {
        WCHAR single[2] = {*text, L'\0'};

        switch (*text)
        {
        case L'&':
            StringCchCatW(buffer, capacity, L"&amp;");
            break;
        case L'<':
            StringCchCatW(buffer, capacity, L"&lt;");
            break;
        case L'>':
            StringCchCatW(buffer, capacity, L"&gt;");
            break;
        case L'"':
            StringCchCatW(buffer, capacity, L"&quot;");
            break;
        default:
            StringCchCatW(buffer, capacity, single);
            break;
        }
    }
}

static BOOL IsAutostartInstalled(void)
{
    WCHAR command[MAX_PATH * 2] = {0};

    if (!BuildSchtasksCommand(command, ARRAYSIZE(command), L"/Query /TN \"" TASK_NAME L"\""))
        return FALSE;

    return RunHidden(command) == 0;
}

static BOOL InstallAutostart(void)
{
    static WCHAR xml[8192];
    WCHAR exePath[MAX_PATH] = {0};
    WCHAR userName[256] = {0};
    WCHAR xmlPath[MAX_PATH] = {0};
    WCHAR arguments[MAX_PATH * 2] = {0};
    WCHAR command[MAX_PATH * 3] = {0};
    ULONG userNameLength = ARRAYSIZE(userName);
    HANDLE file = INVALID_HANDLE_VALUE;
    DWORD written = 0;
    BOOL ok = FALSE;
    const WCHAR bom = 0xFEFF;

    if (GetModuleFileNameW(NULL, exePath, ARRAYSIZE(exePath)) == 0 ||
        !GetUserNameExW(NameSamCompatible, userName, &userNameLength) ||
        GetTempPathW(ARRAYSIZE(xmlPath), xmlPath) == 0 ||
        FAILED(StringCchCatW(xmlPath, ARRAYSIZE(xmlPath), L"LessSteamTask.xml")))
        return FALSE;

    xml[0] = L'\0';
    StringCchCatW(xml, ARRAYSIZE(xml),
                  L"<?xml version=\"1.0\" encoding=\"UTF-16\"?>\r\n"
                  L"<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\r\n"
                  L"  <RegistrationInfo><Description>LessSteam helper</Description></RegistrationInfo>\r\n"
                  L"  <Triggers><LogonTrigger><Enabled>true</Enabled><UserId>");
    AppendXmlEscaped(xml, ARRAYSIZE(xml), userName);
    StringCchCatW(xml, ARRAYSIZE(xml),
                  L"</UserId></LogonTrigger></Triggers>\r\n"
                  L"  <Principals><Principal id=\"Author\"><UserId>");
    AppendXmlEscaped(xml, ARRAYSIZE(xml), userName);
    StringCchCatW(xml, ARRAYSIZE(xml),
                  L"</UserId><LogonType>InteractiveToken</LogonType><RunLevel>HighestAvailable</RunLevel>"
                  L"</Principal></Principals>\r\n"
                  L"  <Settings>\r\n"
                  L"    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>\r\n"
                  L"    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>\r\n"
                  L"    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>\r\n"
                  L"    <AllowHardTerminate>true</AllowHardTerminate>\r\n"
                  L"    <StartWhenAvailable>false</StartWhenAvailable>\r\n"
                  L"    <RunOnlyIfNetworkAvailable>false</RunOnlyIfNetworkAvailable>\r\n"
                  L"    <IdleSettings><StopOnIdleEnd>false</StopOnIdleEnd><RestartOnIdle>false</RestartOnIdle></IdleSettings>\r\n"
                  L"    <AllowStartOnDemand>true</AllowStartOnDemand>\r\n"
                  L"    <Enabled>true</Enabled>\r\n"
                  L"    <Hidden>false</Hidden>\r\n"
                  L"    <RunOnlyIfIdle>false</RunOnlyIfIdle>\r\n"
                  L"    <WakeToRun>false</WakeToRun>\r\n"
                  L"    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>\r\n"
                  L"    <Priority>7</Priority>\r\n"
                  L"    <RestartOnFailure><Interval>PT1M</Interval><Count>3</Count></RestartOnFailure>\r\n"
                  L"  </Settings>\r\n"
                  L"  <Actions Context=\"Author\"><Exec><Command>");
    AppendXmlEscaped(xml, ARRAYSIZE(xml), exePath);
    StringCchCatW(xml, ARRAYSIZE(xml), L"</Command></Exec></Actions>\r\n</Task>\r\n");

    file = CreateFileW(xmlPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;

    ok = WriteFile(file, &bom, sizeof(bom), &written, NULL) &&
         WriteFile(file, xml, (DWORD)(lstrlenW(xml) * sizeof(WCHAR)), &written, NULL);
    CloseHandle(file);

    if (ok &&
        SUCCEEDED(StringCchPrintfW(arguments, ARRAYSIZE(arguments), L"/Create /TN \"" TASK_NAME L"\" /XML \"%s\" /F",
                                   xmlPath)) &&
        BuildSchtasksCommand(command, ARRAYSIZE(command), arguments))
        ok = RunHidden(command) == 0;
    else
        ok = FALSE;

    DeleteFileW(xmlPath);
    LsLog(L"install autostart task: %s", ok ? L"OK" : L"FAILED");
    return ok;
}

static BOOL UninstallAutostart(void)
{
    WCHAR command[MAX_PATH * 2] = {0};
    BOOL ok = FALSE;

    if (BuildSchtasksCommand(command, ARRAYSIZE(command), L"/Delete /TN \"" TASK_NAME L"\" /F"))
        ok = RunHidden(command) == 0;

    LsLog(L"remove autostart task: %s", ok ? L"OK" : L"FAILED");
    return ok;
}

static BOOL EnsureConfigFile(void)
{
    HRSRC resource = NULL;
    HGLOBAL loaded = NULL;
    const void *data = NULL;
    DWORD size = 0;
    DWORD written = 0;
    HANDLE file = INVALID_HANDLE_VALUE;
    BOOL ok = FALSE;

    if (GetFileAttributesW(g_configPath) != INVALID_FILE_ATTRIBUTES)
        return TRUE;

    resource = FindResourceW(NULL, MAKEINTRESOURCEW(IDR_DEFAULT_INI), MAKEINTRESOURCEW(10) );
    if (resource == NULL || (loaded = LoadResource(NULL, resource)) == NULL ||
        (data = LockResource(loaded)) == NULL || (size = SizeofResource(NULL, resource)) == 0)
        return FALSE;

    file = CreateFileW(g_configPath, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;

    ok = WriteFile(file, data, size, &written, NULL) && written == size;
    CloseHandle(file);
    LsLog(L"wrote default %s", LS_CONFIG_FILE_NAME);
    return ok;
}

static void ReloadConfig(void)
{
    LsLoadConfig(g_configPath, &g_config);
    LsLogSetEnabled(g_config.logEnabled);
    LsLog(L"config loaded; preset=%s steamservice=%s", LsPresetName(g_config.preset),
          LsModeName(g_config.modes[LS_TARGET_STEAMSERVICE]));
}

static void ResumeSteamService(void)
{
    DWORD count = LsSnapshotProcesses(g_processes, LS_MAX_TRACKED_PROCESSES);
    DWORD index = 0;

    for (; index < count; index++)
    {
        if (LsImageIs(g_processes[index].imageName, L"steamservice.exe") &&
            LsResumeProcessId(g_processes[index].processId))
            LsLog(L"safety resume steamservice pid=%lu", g_processes[index].processId);
    }
}

static void ResumeOrphanedSteamProcesses(void)
{
    DWORD count = LsSnapshotProcesses(g_processes, LS_MAX_TRACKED_PROCESSES);
    DWORD index = 0;
    DWORD resumed = 0;

    for (; index < count; index++)
    {
        LPCWSTR image = g_processes[index].imageName;

        if (!LsImageIs(image, L"steamwebhelper.exe") && !LsImageIs(image, L"gameoverlayui.exe") &&
            !LsImageIs(image, L"gameoverlayui64.exe"))
            continue;

        if (LsIsProcessInSession(g_processes[index].processId, g_sessionId) &&
            LsResumeProcessId(g_processes[index].processId))
            resumed++;
    }

    if (resumed != 0)
        LsLog(L"steam.exe is not running; resumed %lu orphaned Steam processes", resumed);
}

static BOOL TrackSteamClient(ULONGLONG now)
{
    DWORD count = 0;
    DWORD index = 0;

    if (g_steamProcess != NULL)
    {
        if (WaitForSingleObject(g_steamProcess, 0) == WAIT_TIMEOUT)
            return TRUE;

        LsLog(L"steam.exe exited");
        CloseHandle(g_steamProcess);
        g_steamProcess = NULL;
        ResumeOrphanedSteamProcesses();
    }

    if (g_lastSteamScan != 0 && now - g_lastSteamScan < STEAM_SCAN_INTERVAL_MS)
        return FALSE;

    g_lastSteamScan = now;
    count = LsSnapshotProcesses(g_processes, LS_MAX_TRACKED_PROCESSES);
    for (; index < count; index++)
    {
        if (LsImageIs(g_processes[index].imageName, L"steam.exe") &&
            LsIsProcessInSession(g_processes[index].processId, g_sessionId))
        {
            g_steamProcess = OpenProcess(SYNCHRONIZE, FALSE, g_processes[index].processId);
            if (g_steamProcess != NULL)
            {
                LsLog(L"tracking steam.exe pid=%lu", g_processes[index].processId);
                return TRUE;
            }
        }
    }

    return FALSE;
}

static void ParkSteamService(void)
{
    DWORD count = LsSnapshotProcesses(g_processes, LS_MAX_TRACKED_PROCESSES);
    DWORD index = 0;

    for (; index < count; index++)
    {
        if (LsImageIs(g_processes[index].imageName, L"steamservice.exe"))
            LsParkProcess(&g_table, g_processes[index].processId, LS_TARGET_STEAMSERVICE,
                          g_config.modes[LS_TARGET_STEAMSERVICE]);
    }
}

static void RestoreAll(LPCWSTR reason)
{
    if (g_table.count != 0)
        LsLog(L"restoring: %s", reason);

    LsParkRestoreAll(&g_table);
    g_parked = FALSE;
}

static LONG WINAPI CrashFilter(EXCEPTION_POINTERS *exception)
{
    UNREFERENCED_PARAMETER(exception);

    LsParkRestoreAll(&g_table);
    return EXCEPTION_CONTINUE_SEARCH;
}

static void UpdateTrayTip(void)
{
    if (g_config.paused)
        StringCchCopyW(g_notifyIcon.szTip, ARRAYSIZE(g_notifyIcon.szTip), L"LessSteam: paused");
    else if (g_parked)
        StringCchPrintfW(g_notifyIcon.szTip, ARRAYSIZE(g_notifyIcon.szTip), L"LessSteam: %s - game %lu",
                         LsPresetDisplayName(g_config.preset), g_appId);
    else
        StringCchPrintfW(g_notifyIcon.szTip, ARRAYSIZE(g_notifyIcon.szTip), L"LessSteam: %s - idle",
                         LsPresetDisplayName(g_config.preset));

    g_notifyIcon.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_notifyIcon);
}

static void AddTrayIcon(void)
{
    g_notifyIcon.cbSize = sizeof(g_notifyIcon);
    g_notifyIcon.hWnd = g_window;
    g_notifyIcon.uID = 1;
    g_notifyIcon.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_notifyIcon.uCallbackMessage = WM_LS_TRAY;
    g_notifyIcon.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    StringCchCopyW(g_notifyIcon.szTip, ARRAYSIZE(g_notifyIcon.szTip), L"LessSteam");
    Shell_NotifyIconW(NIM_ADD, &g_notifyIcon);
    UpdateTrayTip();
}

static void ShowTrayMenu(void)
{
    HMENU menu = CreatePopupMenu();
    WCHAR status[128] = {0};
    POINT cursor = {0};
    UINT current = ID_PRESET_LIMIT;

    if (menu == NULL)
        return;

    if (g_config.paused)
        StringCchCopyW(status, ARRAYSIZE(status), L"Paused");
    else if (g_parked)
        StringCchPrintfW(status, ARRAYSIZE(status), L"Game running (AppID %lu)", g_appId);
    else if (g_steamProcess != NULL)
        StringCchCopyW(status, ARRAYSIZE(status), L"Steam idle");
    else
        StringCchCopyW(status, ARRAYSIZE(status), L"Steam not running");

    if (g_config.preset == LS_PRESET_SLAY)
        current = ID_PRESET_SLAY;
    else if (g_config.preset == LS_PRESET_CUSTOM)
        current = ID_PRESET_CUSTOM;

    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, status);
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, ID_PRESET_LIMIT, L"Limit Steam");
    AppendMenuW(menu, MF_STRING, ID_PRESET_SLAY, L"Slay Steam");
    AppendMenuW(menu, MF_STRING, ID_PRESET_CUSTOM, L"Custom (" LS_CONFIG_FILE_NAME L")");
    CheckMenuRadioItem(menu, ID_PRESET_LIMIT, ID_PRESET_CUSTOM, current, MF_BYCOMMAND);
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING | (g_config.paused ? MF_CHECKED : 0), ID_PAUSE, L"Pause");
    AppendMenuW(menu, MF_STRING, ID_OPEN_CONFIG, L"Open " LS_CONFIG_FILE_NAME);
    AppendMenuW(menu, MF_STRING | (g_autostartInstalled ? MF_CHECKED : 0), ID_AUTOSTART, L"Start with Windows");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, ID_EXIT, L"Exit");

    GetCursorPos(&cursor);
    SetForegroundWindow(g_window);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, cursor.x, cursor.y, 0, g_window, NULL);
    PostMessageW(g_window, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

static void Tick(void)
{
    ULONGLONG now = GetTickCount64();
    BOOL wasParked = g_parked;
    LS_PRESET previousPreset = g_config.preset;
    BOOL wasPaused = g_config.paused;
    BOOL steamRunning = FALSE;
    BOOL gameRunning = FALSE;

    if (LsConfigFileChanged(g_configPath, &g_configWriteTime))
    {
        ReloadConfig();
        RestoreAll(L"config changed");
    }

    steamRunning = TrackSteamClient(now);
    gameRunning = steamRunning && !g_config.paused && LsIsSteamGameRunning(&g_appId);

    if (gameRunning)
    {
        LS_MODE mode = g_config.modes[LS_TARGET_STEAMSERVICE];

        if (!g_parked)
        {
            g_parked = TRUE;
            g_gameStart = now;
            g_lastServiceScan = 0;
            g_lastTrim = now;
            LsLog(L"game detected: appid=%lu", g_appId);
        }

        if (mode != LS_MODE_OFF && g_table.count == 0 &&
            (mode != LS_MODE_FREEZE || now - g_gameStart >= LS_FREEZE_DELAY_MS) &&
            (g_lastServiceScan == 0 || now - g_lastServiceScan >= SERVICE_SCAN_INTERVAL_MS))
        {
            ParkSteamService();
            g_lastServiceScan = now;
        }

        LsParkSample(&g_table);

        if (now - g_lastTrim >= g_config.trimIntervalMs)
        {
            LsParkTrim(&g_table);
            g_lastTrim = now;
        }
    }
    else if (g_parked)
    {
        RestoreAll(g_config.paused ? L"paused" : L"game ended");
    }

    if (wasParked != g_parked || previousPreset != g_config.preset || wasPaused != g_config.paused)
        UpdateTrayTip();
}

static void SetPaused(BOOL paused)
{
    EnsureConfigFile();
    if (!LsWritePaused(g_configPath, paused))
        LsLog(L"cannot write Paused to %s error=%lu", g_configPath, GetLastError());
}

static void SelectPreset(LS_PRESET preset)
{
    EnsureConfigFile();
    if (!LsWritePreset(g_configPath, preset) || !LsWritePaused(g_configPath, FALSE))
    {
        LsLog(L"cannot write preset to %s error=%lu", g_configPath, GetLastError());
        return;
    }

    Tick();
}

static void OpenConfigFile(void)
{
    WCHAR parameters[MAX_PATH + 2] = {0};

    EnsureConfigFile();
    if (SUCCEEDED(StringCchPrintfW(parameters, ARRAYSIZE(parameters), L"\"%s\"", g_configPath)))
        ShellExecuteW(NULL, L"open", L"notepad.exe", parameters, NULL, SW_SHOWNORMAL);
}

static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == g_taskbarCreatedMessage && g_taskbarCreatedMessage != 0)
    {
        AddTrayIcon();
        return 0;
    }

    switch (message)
    {
    case WM_TIMER:
        if (wParam == TICK_TIMER_ID)
            Tick();
        return 0;

    case WM_LS_TRAY:
        if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_LBUTTONUP)
            ShowTrayMenu();
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case ID_PRESET_LIMIT:
            SelectPreset(LS_PRESET_LIMIT);
            break;
        case ID_PRESET_SLAY:
            SelectPreset(LS_PRESET_SLAY);
            break;
        case ID_PRESET_CUSTOM:
            SelectPreset(LS_PRESET_CUSTOM);
            break;
        case ID_OPEN_CONFIG:
            OpenConfigFile();
            break;
        case ID_AUTOSTART:
            if (g_autostartInstalled)
                UninstallAutostart();
            else
                InstallAutostart();
            g_autostartInstalled = IsAutostartInstalled();
            break;
        case ID_PAUSE:
            SetPaused(!g_config.paused);
            Tick();
            break;
        case ID_EXIT:
            SetPaused(TRUE);
            DestroyWindow(window);
            break;
        }
        return 0;

    case WM_QUERYENDSESSION:
        return TRUE;

    case WM_ENDSESSION:
        if (wParam)
            RestoreAll(L"session ending");
        return 0;

    case WM_DESTROY:
        KillTimer(window, TICK_TIMER_ID);
        RestoreAll(L"helper exiting");
        Shell_NotifyIconW(NIM_DELETE, &g_notifyIcon);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

static BOOL HasArgument(LPCWSTR expected)
{
    int argc = 0;
    int index = 1;
    BOOL found = FALSE;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    if (argv == NULL)
        return FALSE;

    for (; index < argc && !found; index++)
        found = LsImageIs(argv[index], expected);

    LocalFree(argv);
    return found;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previousInstance, LPWSTR commandLine, int showCommand)
{
    WNDCLASSEXW windowClass = {0};
    HANDLE instanceMutex = NULL;
    MSG message = {0};

    UNREFERENCED_PARAMETER(previousInstance);
    UNREFERENCED_PARAMETER(commandLine);
    UNREFERENCED_PARAMETER(showCommand);

    LsLogInit(L"helper");

    if (HasArgument(L"--install"))
        return InstallAutostart() ? 0 : 1;
    if (HasArgument(L"--uninstall"))
        return UninstallAutostart() ? 0 : 1;

    instanceMutex = CreateMutexW(NULL, TRUE, L"Local\\LessSteamHelper");
    if (instanceMutex == NULL || GetLastError() == ERROR_ALREADY_EXISTS)
        return 0;

    g_processes = LsAllocProcessBuffer();
    if (g_processes == NULL)
        return 1;

    ProcessIdToSessionId(GetCurrentProcessId(), &g_sessionId);
    LsParkInit();
    if (!EnableDebugPrivilege())
        LsLog(L"WARNING: SeDebugPrivilege unavailable; SteamService may be out of reach");

    SetUnhandledExceptionFilter(CrashFilter);

    LsBuildConfigPath(NULL, g_configPath, ARRAYSIZE(g_configPath));
    EnsureConfigFile();
    LsLoadConfig(g_configPath, &g_config);
    if (g_config.paused)
        SetPaused(FALSE);
    LsConfigFileChanged(g_configPath, &g_configWriteTime);
    ReloadConfig();

    ResumeSteamService();
    if (!TrackSteamClient(GetTickCount64()))
        ResumeOrphanedSteamProcesses();

    g_autostartInstalled = IsAutostartInstalled();
    g_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");

    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.lpszClassName = L"LessSteamHelperWindow";
    if (!RegisterClassExW(&windowClass))
        return 1;

    g_window = CreateWindowExW(0, windowClass.lpszClassName, L"LessSteam", 0, 0, 0, 0, 0, NULL, NULL, instance, NULL);
    if (g_window == NULL)
        return 1;

    ChangeWindowMessageFilterEx(g_window, g_taskbarCreatedMessage, MSGFLT_ALLOW, NULL);

    AddTrayIcon();
    SetTimer(g_window, TICK_TIMER_ID, TICK_INTERVAL_MS, NULL);
    LsLog(L"helper started");

    while (GetMessageW(&message, NULL, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    LsFreeProcessBuffer(g_processes);
    ReleaseMutex(instanceMutex);
    CloseHandle(instanceMutex);
    return (int)message.wParam;
}
