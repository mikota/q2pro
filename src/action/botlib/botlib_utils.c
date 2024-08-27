#include "../g_local.h"
#include "../acesrc/acebot.h"
#include "botlib.h"

/*
This file is for common utilties that are used by the botlib functions

*/

void BOTLIB_Debug(const char *debugmsg, ...)
{
    if (!bot_debug->value)
        return;
    gi.dprintf(debugmsg);
}
