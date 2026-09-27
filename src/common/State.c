#include "State.h"

BOOL LsShouldPark(DWORD runningAppId, BOOL appMarkedRunning)
{
    return runningAppId != 0 && appMarkedRunning;
}
