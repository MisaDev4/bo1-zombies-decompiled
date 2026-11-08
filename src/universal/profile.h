#pragma once

// zombies (p1): the bo1_frameperf measurement state (zones, first-use and render counters, the render frame ring) is
// declared and defined in every build. Only PROF_SCOPED differs: with Tracy (Debug, Profile) it opens a Tracy zone
// instead of a BO1ProfZone, so the frameperf report then lists no zones.
#include <intrin.h>

struct BO1ProfZone
{
    const char *name;
    BO1ProfZone *next;
    unsigned long long mainTicks;
    volatile long otherTicks1k; // other threads, rdtsc ticks / 1024
    unsigned int mainCalls;
    int mainDepth;
    volatile long registered;
};

extern bool g_bo1ProfOn;
extern unsigned long g_bo1ProfMainThread;
extern BO1ProfZone *volatile g_bo1ProfZones; // every zone entered at least once while on
void BO1_ProfRegister(BO1ProfZone *zone);

// First-use counters (render side): the first time since the profiler came on that a vertex shader, pixel shader,
// (vs, ps) pair, material pass or texture is bound. Read and cleared per frame by the frameperf report.
enum BO1FirstUseKind
{
    BO1_FIRST_VS,
    BO1_FIRST_PS,
    BO1_FIRST_PAIR,
    BO1_FIRST_PASS,
    BO1_FIRST_TEXTURE,
    BO1_FIRST_COMBO, // (vs, ps, vertex declaration, pass state bits) - what a driver compiles shader variants for
    BO1_FIRST_KINDS
};
extern volatile long g_bo1FirstUse[BO1_FIRST_KINDS];
extern volatile long g_bo1RenderFirstUse[BO1_FIRST_KINDS]; // same counts, read per render frame (RB_SwapBuffers)
void BO1_ProfFirstUse(int kind, unsigned long long key, const char *name); // call only when g_bo1ProfOn
int BO1_ProfFirstUseNames(char *out, int size); // recent first-used pass names (material:technique), then cleared

// p1 chunk 25: render-side resource work per render frame (any thread; summed into the frame that ends at the next
// Present) and per-frame GPU/Present timing (rb_backend.cpp RB_SwapBuffers). Measurement only, when g_bo1ProfOn.
enum BO1RenderCountKind
{
    BO1_RC_TEX_CREATE,    // Create(2D|Cube|Volume)Texture calls
    BO1_RC_TEX_CREATE_KT, // texels (K) of those textures' top mip, times faces / depth
    BO1_RC_TEX_UPLOAD,    // mip levels / faces / slices copied into locked textures
    BO1_RC_TEX_UPLOAD_KB, // source bytes copied
    BO1_RC_TEX_RELEASE,   // image textures released (Image_Release, either thread)
    BO1_RC_STREAM_DONE,   // streamed image parts put in place (R_StreamUpdate_ProcessFileCallbacks)
    BO1_RC_STREAM_KB,     // their file bytes
    BO1_RC_BUF_CREATE,    // vertex / index buffers created
    BO1_RC_BUF_LOCK,      // R_LockVertexBuffer / R_LockIndexBuffer calls
    BO1_RC_BUF_LOCK_KB,   // bytes locked by them (0 = whole buffer, not counted)
    BO1_RC_KINDS
};
extern volatile long g_bo1RenderCount[BO1_RC_KINDS];
inline void BO1_RenderCount(int kind, long n) { if (g_bo1ProfOn) _InterlockedExchangeAdd(&g_bo1RenderCount[kind], n); }

// GPU phases: frame start..draw commands, shadow maps, depth prepass, lit (+reflected, resolves), decal / sun /
// coronas / dlights / distortion, emissive (effects), post effects, rest of the frame (2D, UI)
#define BO1_GPU_SEGS 8
struct BO1RenderFrame
{
    volatile long frame;   // render frame number (1-based); written last, when the entry is complete
    volatile long state;   // 0 GPU times pending, 1 resolved, 2 GPU times unavailable
    float swapMs;          // Present end to Present end (the render thread's frame)
    float presentMs;       // time inside Present()
    float gpuMs;           // GPU timestamp: first command after the previous Present to the last before this one
    float gpuEndDeltaMs;   // GPU timestamp of this frame's end minus the previous frame's end
    unsigned int availMB;  // IDirect3DDevice9::GetAvailableTextureMem after Present
    float presentCpuMs;    // render thread CPU cycles inside Present (QueryThreadCycleTime), in ms
    float presentProcMs;   // whole-process CPU cycles during Present (QueryProcessCycleTime), in ms of one CPU
    float presentMainMs;   // main thread CPU cycles during Present
    char threads[128];     // Present >= 10 ms: the 3 threads with the most CPU during it, start module+offset:ms
    long first[BO1_FIRST_KINDS]; // first uses (profile.h BO1FirstUseKind) drawn in this render frame
    float gpuSeg[BO1_GPU_SEGS]; // GPU ms per phase (RB_BO1GpuMark in RB_StandardDrawCommands), -1 = not drawn
    long counts[BO1_RC_KINDS];
};
#define BO1_RENDER_RING 64
extern BO1RenderFrame g_bo1RenderRing[BO1_RENDER_RING]; // index frame % BO1_RENDER_RING

#ifdef TRACY_ENABLE
#ifndef TRACY_ON_DEMAND
#error This should be left ON
#endif

#include <tracy/public/tracy/Tracy.hpp>
#include <tracy/public/tracy/TracyC.h>
//#include <tracy/Tracy.hpp>
//#include <tracy/TracyC.h>

#define PROF_SCOPED_RUNTIME_NAME(name) ZoneScoped; ZoneName(name, strlen(name));
#define PROF_SCOPED(name) ZoneScopedN(name)
#define PROFLOAD_SCOPED(name) PROF_SCOPED(name)
#define PROF_THREADNAME(threadname) tracy::SetThreadName(threadname)
#else

// zombies (p1): without Tracy, PROF_SCOPED feeds a small zone profiler - measurement only, off unless the headless
// command line sets bo1_frameperf (win_main.cpp). Per zone: inclusive time of the current frame on the main thread
// (outermost entry only) and on all other threads, read and cleared once per frame by G_SP_FramePerfEnd.

struct BO1ProfScope
{
    BO1ProfZone *zone;
    unsigned long long start;
    bool main;

    explicit BO1ProfScope(BO1ProfZone *z) : zone(0), start(0), main(false)
    {
        if (!g_bo1ProfOn)
            return;
        if (!z->registered)
            BO1_ProfRegister(z);
        zone = z;
        main = __readfsdword(0x24) == g_bo1ProfMainThread;
        if (!main || !z->mainDepth++)
            start = __rdtsc();
    }
    ~BO1ProfScope()
    {
        if (!zone)
            return;
        if (main)
        {
            --zone->mainDepth;
            if (start)
            {
                zone->mainTicks += __rdtsc() - start;
                ++zone->mainCalls;
            }
        }
        else
        {
            _InterlockedExchangeAdd(&zone->otherTicks1k, (long)((__rdtsc() - start) >> 10));
        }
    }
};

#define BO1_PROF_CAT2(a, b) a##b
#define BO1_PROF_CAT(a, b) BO1_PROF_CAT2(a, b)
#define PROF_SCOPED(name) \
    static BO1ProfZone BO1_PROF_CAT(bo1ProfZone_, __LINE__) = { name }; \
    BO1ProfScope BO1_PROF_CAT(bo1ProfScope_, __LINE__)(&BO1_PROF_CAT(bo1ProfZone_, __LINE__))
#define PROF_SCOPED_RUNTIME_NAME(name)
#define PROFLOAD_SCOPED(name) PROF_SCOPED(name)
#define ZoneText(str, len)
#define ZoneTextF(fmt, ...)
#define ZoneName(str, len)
#define PROF_THREADNAME(threadname)
#define FrameMark

#endif


// mod (L43): bo1_mod_svprof 1 - exclusive server-thread time per subsystem, for the capacity benchmark (mods/stress,
// tools/stress.ps1). Each SVPROF_SCOPED charges the time since the last boundary to the category on top of a small stack,
// so nested scopes (script inside actor think, traces inside script) are counted once, in the innermost category.
// Only the server thread counts (the renderer / client share CM_* and DObjCalcSkel). Off by default: one load and branch.
#include <intrin.h>
enum SvProfCat
{
    SVPROF_OTHER,  // the rest of SV_PreFrame / SV_RunFrame / SV_PostFrame
    SVPROF_ENTS,   // G_RunFrameForEntity outside actor think (movers, scripts ents, corpses, physics)
    SVPROF_ACTOR,  // Actor_Think (senses, threat, movement, physics)
    SVPROF_PATH,   // A* searches (Path_FindPath*)
    SVPROF_ANIM,   // server anim update and skeleton builds (G_XAnimUpdate pass, DObjCalcSkel)
    SVPROF_TRACE,  // SV_Trace*, SV_SightTrace*, CM_*Trace on the server thread
    SVPROF_SCRIPT, // script VM (Scr_RunCurrentThreads, VM_Notify, Scr_ExecThread, Scr_ExecEntThread)
    SVPROF_BUILTIN, // script builtins (GetAiSpeciesArray, ...) called by the server VM, less their traces / paths
    SVPROF_SNAP,   // SV_SendClientMessages (snapshot build and send)
    SVPROF_CLIENT, // player think (ClientThink / bots / ClientEndFrame)
    SVPROF_TEAMMOVE, // Actor_GetTeamMoveStatus (dodge / slow-down over the sentients), less its traces
    SVPROF_PHYSICS, // Actor_Physics (ground, slide, step), less its traces
    SVPROF_COUNT
};
extern volatile int g_svProfOn;
extern unsigned long g_svProfThread;
extern unsigned long long g_svProfTicks[SVPROF_COUNT];
extern unsigned int g_svProfCalls[SVPROF_COUNT];
extern int g_svProfStack[64];
extern int g_svProfDepth;
extern unsigned long long g_svProfLast;
extern volatile long g_modRendWarn[6]; // mod (L43): renderer overflows since start: [0] shader constant sets, [1] skinned cache, [2] special models (r_mod_gfxEnts); [3] window peak of special-model slots wanted in a frame, [4] this frame's refused slots, [5] frames wanting more than 256

struct SvProfScope
{
    bool on;
    explicit SvProfScope(int cat)
    {
        on = g_svProfOn && __readfsdword(0x24) == g_svProfThread && g_svProfDepth < 63;
        if (!on)
            return;
        const unsigned long long now = __rdtsc();
        g_svProfTicks[g_svProfStack[g_svProfDepth]] += now - g_svProfLast;
        g_svProfStack[++g_svProfDepth] = cat;
        ++g_svProfCalls[cat];
        g_svProfLast = now;
    }
    ~SvProfScope()
    {
        if (!on)
            return;
        const unsigned long long now = __rdtsc();
        g_svProfTicks[g_svProfStack[g_svProfDepth]] += now - g_svProfLast;
        if (g_svProfDepth > 0)
            --g_svProfDepth;
        g_svProfLast = now;
    }
};
#define SVPROF_CAT2(a, b) a##b
#define SVPROF_CAT(a, b) SVPROF_CAT2(a, b)
#define SVPROF_SCOPED(cat) SvProfScope SVPROF_CAT(svProfScope_, __LINE__)(cat)
// per-builtin inclusive time (index into gScrCompilePub[0].func_table); the top ones are printed as BO1_SVBUILTINS
#define SVPROF_MAX_BUILTINS 4096
extern unsigned long long g_svProfBuiltinTicks[SVPROF_MAX_BUILTINS];
extern unsigned int g_svProfBuiltinCalls[SVPROF_MAX_BUILTINS];
struct SvProfBuiltin
{
    SvProfScope scope;
    unsigned long long t0;
    unsigned int idx;
    explicit SvProfBuiltin(unsigned int i) : scope(SVPROF_BUILTIN), t0(0), idx(i)
    {
        if (scope.on && i < SVPROF_MAX_BUILTINS)
            t0 = __rdtsc();
    }
    ~SvProfBuiltin()
    {
        if (!t0)
            return;
        g_svProfBuiltinTicks[idx] += __rdtsc() - t0;
        ++g_svProfBuiltinCalls[idx];
    }
};
