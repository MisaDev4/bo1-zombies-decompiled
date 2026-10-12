#include "r_sky_load_obj.h"
#include "r_sky.h"
#include <universal/com_shared.h>
#include <universal/q_shared.h>
#include <string.h>

void __cdecl R_LoadSun(const char *name, sunflare_t *sun)
{
    char *v2; // eax
    const char *nameIter; // [esp+0h] [ebp-54h]
    char sunFile[68]; // [esp+4h] [ebp-50h] BYREF
    const char *firstCharToCopy; // [esp+4Ch] [ebp-8h]
    char *firstPeriod; // [esp+50h] [ebp-4h]

    iassert(name);
    iassert(sun);

    Com_Memset((unsigned int *)sun, 0, 96);
    firstCharToCopy = name;
    for ( nameIter = name; *nameIter; ++nameIter )
    {
        if ( *nameIter == '/' || *nameIter == '\\')
            firstCharToCopy = nameIter + 1;
    }
    I_strncpyz(sunFile, firstCharToCopy, 64);
    v2 = strchr(sunFile, '.');
    firstPeriod = v2;
    if ( v2 )
        *firstPeriod = 0;
    if ( sunFile[0] )
        R_LoadSunThroughDvars(sunFile, sun);
}

