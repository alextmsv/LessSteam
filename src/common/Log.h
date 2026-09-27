#ifndef LESSSTEAM_LOG_H
#define LESSSTEAM_LOG_H

#include <windows.h>

void LsLogInit(LPCWSTR component);
void LsLogSetEnabled(BOOL enabled);
void LsLog(LPCWSTR format, ...);

#endif
