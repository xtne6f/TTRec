#include <Windows.h>
#include "Util.h"

#ifdef NO_CRT
#pragma function(memset)
#pragma function(memcpy)

void * __cdecl memset(void *Dest, int Pattern, size_t Length)
{
    RtlFillMemory(Dest, Length, (BYTE)Pattern);
    return Dest;
}

void * __cdecl memcpy(void *Dest, const void *Src, size_t Length)
{
    RtlCopyMemory(Dest, Src, Length);
    return Dest;
}

#endif
