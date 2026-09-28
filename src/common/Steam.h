#ifndef LESSSTEAM_STEAM_H
#define LESSSTEAM_STEAM_H

#include <windows.h>

BOOL LsIsSteamGameRunning(DWORD *appId);
BOOL LsGetSteamDirectory(LPWSTR path, DWORD capacity);

#endif
