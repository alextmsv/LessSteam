@echo off
setlocal

set "ROOT=%~dp0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VCVARS="

if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find VC\Auxiliary\Build\vcvars64.bat`) do (
        set "VCVARS=%%I"
    )
)

if not defined VCVARS (
    echo Could not locate vcvars64.bat.
    echo Install Visual Studio Build Tools with the x64 C/C++ toolchain.
    exit /b 1
)

call "%VCVARS%" >nul
pushd "%ROOT%"

set "BUILD=build"
set "OBJ=build\obj"
set "BIN=build\bin"
set "RELEASE=release\steam"
set "CFLAGS=/nologo /W4 /WX /O1 /MT /DUNICODE /D_UNICODE /TC /Isrc\common"

if exist "%BUILD%" rd /q /s "%BUILD%"
if not exist "%RELEASE%" md "%RELEASE%"
md "%OBJ%\common" "%OBJ%\dll" "%OBJ%\helper" "%OBJ%\tests" "%BIN%"

cl %CFLAGS% /Fo"%OBJ%\common\\" /c src\common\Config.c src\common\Log.c src\common\Park.c src\common\Process.c src\common\State.c src\common\Steam.c src\common\Task.c || goto :fail
lib /nologo /out:"%OBJ%\common\LessSteamCommon.lib" "%OBJ%\common\*.obj" || goto :fail

cl %CFLAGS% /Fo"%OBJ%\dll\\" /c src\dll\Library.c || goto :fail
lib /nologo /def:src\dll\umpdc.def /machine:x64 /name:umpdc.dll /out:"%OBJ%\dll\umpdc_forward.lib" || goto :fail
link /nologo /DLL /OUT:"%BIN%\umpdc.dll" "%OBJ%\dll\Library.obj" "%OBJ%\dll\umpdc_forward.exp" "%OBJ%\common\LessSteamCommon.lib" kernel32.lib user32.lib advapi32.lib || goto :fail
copy /y "%SystemRoot%\System32\umpdc.dll" "%BIN%\umpdc_system.dll" >nul || goto :fail

rc /nologo /fo "%OBJ%\helper\Helper.res" src\helper\Helper.rc || goto :fail
cl %CFLAGS% /Fo"%OBJ%\helper\\" /c src\helper\Helper.c || goto :fail
link /nologo /SUBSYSTEM:WINDOWS /MANIFEST:EMBED /MANIFESTUAC:"level='requireAdministrator' uiAccess='false'" /OUT:"%BIN%\LessSteamHelper.exe" "%OBJ%\helper\Helper.obj" "%OBJ%\helper\Helper.res" "%OBJ%\common\LessSteamCommon.lib" kernel32.lib user32.lib advapi32.lib shell32.lib secur32.lib || goto :fail

cl %CFLAGS% /Fo"%OBJ%\tests\\" /c tests\Tests.c || goto :fail
link /nologo /OUT:"%BIN%\LessSteamTests.exe" "%OBJ%\tests\Tests.obj" "%OBJ%\common\LessSteamCommon.lib" kernel32.lib user32.lib advapi32.lib || goto :fail
"%BIN%\LessSteamTests.exe" config\LessSteam.ini || goto :fail

copy /y "%BIN%\umpdc.dll" "%RELEASE%\umpdc.dll" >nul || goto :fail
copy /y "%BIN%\umpdc_system.dll" "%RELEASE%\umpdc_system.dll" >nul || goto :fail
copy /y "%BIN%\LessSteamHelper.exe" "%RELEASE%\LessSteamHelper.exe" >nul || goto :fail
copy /y config\LessSteam.ini "%RELEASE%\LessSteam.ini" >nul || goto :fail

echo.
echo Build complete. Release payload refreshed:
echo   %ROOT%%RELEASE%
popd
exit /b 0

:fail
echo Build FAILED.
popd
exit /b 1
