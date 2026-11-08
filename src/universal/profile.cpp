#include "profile.h"

#include <windows.h>

// zombies (p1): the zone profiler behind PROF_SCOPED (profile.h). Measurement only; g_bo1ProfOn is set by
// G_SP_FramePerfBegin when the headless command line has bo1_frameperf.
bool g_bo1ProfOn;
unsigned long g_bo1ProfMainThread;
BO1ProfZone *volatile g_bo1ProfZones;

void BO1_ProfRegister(BO1ProfZone *zone)
{
    if (InterlockedCompareExchange(&zone->registered, 1, 0) != 0)
        return;
    BO1ProfZone *head;
    do
    {
        head = g_bo1ProfZones;
        zone->next = head;
    } while (InterlockedCompareExchangePointer((void *volatile *)&g_bo1ProfZones, zone, head) != head);
}

#include <unordered_set>
#include <string.h>

volatile long g_bo1FirstUse[BO1_FIRST_KINDS];
volatile long g_bo1RenderFirstUse[BO1_FIRST_KINDS];
volatile long g_bo1RenderCount[BO1_RC_KINDS];
BO1RenderFrame g_bo1RenderRing[BO1_RENDER_RING];
static SRWLOCK s_firstLock = SRWLOCK_INIT;
static std::unordered_set<unsigned long long> *s_firstSeen[BO1_FIRST_KINDS];
static char s_firstNames[512];
static int s_firstNamesLen, s_firstNamesDropped;

void BO1_ProfFirstUse(int kind, unsigned long long key, const char *name)
{
    AcquireSRWLockExclusive(&s_firstLock);
    if (!s_firstSeen[kind])
        s_firstSeen[kind] = new std::unordered_set<unsigned long long>;
    if (s_firstSeen[kind]->insert(key).second)
    {
        _InterlockedIncrement(&g_bo1FirstUse[kind]);
        _InterlockedIncrement(&g_bo1RenderFirstUse[kind]);
        if (name)
        {
            const int len = (int)strlen(name);
            if (s_firstNamesLen + len + 2 < (int)sizeof(s_firstNames))
            {
                if (s_firstNamesLen)
                    s_firstNames[s_firstNamesLen++] = ' ';
                memcpy(s_firstNames + s_firstNamesLen, name, len);
                s_firstNamesLen += len;
                s_firstNames[s_firstNamesLen] = 0;
            }
            else
            {
                ++s_firstNamesDropped;
            }
        }
    }
    ReleaseSRWLockExclusive(&s_firstLock);
}

int BO1_ProfFirstUseNames(char *out, int size)
{
    AcquireSRWLockExclusive(&s_firstLock);
    int dropped = s_firstNamesDropped;
    if (out && size > 0)
    {
        strncpy_s(out, size, s_firstNames, _TRUNCATE);
    }
    s_firstNamesLen = 0;
    s_firstNames[0] = 0;
    s_firstNamesDropped = 0;
    ReleaseSRWLockExclusive(&s_firstLock);
    return dropped;
}

// mod (L43): bo1_mod_svprof (profile.h SvProfScope); the window report is BO1_SvProfFrame in sv_main_mp.cpp
volatile int g_svProfOn;
unsigned long g_svProfThread;
unsigned long long g_svProfTicks[SVPROF_COUNT];
unsigned int g_svProfCalls[SVPROF_COUNT];
int g_svProfStack[64];
int g_svProfDepth;
unsigned long long g_svProfLast;
volatile long g_modRendWarn[6];
unsigned long long g_svProfBuiltinTicks[SVPROF_MAX_BUILTINS];
unsigned int g_svProfBuiltinCalls[SVPROF_MAX_BUILTINS];
