#include "win_main.h"
#include "win_net.h"
#include <game_mp/g_main_mp.h>
#include <sound/snd_driver_xaudio2.h>
#include "win_localize.h"
#include <monkey/monkey.h>
#include <qcommon/threads.h>
#include "win_syscon.h"
#include <live/live_steam.h>
#include "win_input.h"
#include <client/cl_keys.h>
#include <ui/ui_main.h>
#include <client/client.h>
#include <clientscript/cscr_stringlist.h>
#include <qcommon/mem_track.h>
#include <client/con_channels.h>
#include <universal/com_memory.h>
#include <gfx_d3d/rb_backend.h>
#include "win_shared.h"
#include <gfx_d3d/r_dvars.h>
#include <mjpeg/mjpeg.h>
#include <mjpeg/avi.h>
#include <universal/com_buildinfo.h>
#undef UNICODE // Module32First ascii version in below Tlhelp32.h
#include <TlHelp32.h>
#include <DbgHelp.h>
#include "win_mini_dumper.h"
#include <universal/physicalmemory.h>
#include <universal/q_parse.h>
#include <universal/timing.h>
#include "win_wndproc.h"
#include <client/screen_placement.h>
#include <client_mp/cl_input_mp.h>
#include <ui/ui_utils.h>
#include <client_mp/cl_scrn_mp.h>
#include <tl/tl_system.h>
#include <qcommon/tl_support.h>
#include "win_configure.h"
#include <direct.h>
#include "win_steam.h"
#include <universal/com_expressions_eval.h>
#include <gfx_d3d/r_material.h>
#include <ui/keycodes.h>
#include <cgame_mp/cg_newDraw_mp.h>
#include <ui_mp/ui_main_mp.h>

const dvar_t *sys_configureGHz;
const dvar_t *sys_sysMB;
const dvar_t *sys_gpu;
const dvar_t *sys_configSum;
const dvar_t *sys_SSE;

bool g_allowMature = true;

int s_nosnd;
SysInfo sys_info;

// zombies: bo1_headless - a windowless test mode that never takes the desktop.
// '+set bo1_headless 1' on the command line (read in WinMain, before anything can open a window) turns this
// process into a dedicated LAN server bound to loopback that logs to main\console_mp.log (logfile 2) and never
// shows a window: no game window (dedicated skips the renderer; R_CreateGameWindow refuses), no console window
// (Sys_ShowConsole), no message box (every one reachable from the dedicated path returns or ends the process),
// no crash dialog (WER no-UI), and nothing that touches the desktop (FixWindowsDesktop, SetCursorPos, SetFocus).
//
// zombies: bo1_headless_client - '+set bo1_headless_client 1' together with bo1_headless. The run is a listen
// server with a local client instead of a dedicated server, so the renderer creates the game window. It is only
// ever started by tools\headless.ps1 -Client, on the private desktop WinSta0\bo1_headless, so that window is never
// on the user's desktop. Sys_IsHeadless() stays true, so every no-UI guard above still holds (no message box, no
// console window, no crash dialog, fatal errors exit). On top of that this mode never touches anything outside its
// own desktop: the window is forced windowed (no display mode change), no gamma ramp (vidConfig.deviceSupportsGamma
// is cleared), no sound or voice device (nosnd; no mixer/microphone change; cinematics without sound tracks), no
// mouse or gamepad reads and no cursor moves, no focus change, no "display required" power request, and no
// DirectDraw exclusive mode for the video memory probe. Screenshots read the game's own back buffer, never the
// screen (r_screenshot.cpp).
static bool s_headless;
static bool s_headlessClient;
static const dvar_t *bo1_autoquit;
static int s_autoquitStartMs;

bool __cdecl Sys_IsHeadless()
{
    return s_headless;
}

bool __cdecl Sys_IsHeadlessClient()
{
    return s_headlessClient;
}

// value of '<name> <int>' in a raw command line ('+set name 1'); the last whole-word match wins
static int Sys_CmdlineIntValue(const char *cmdline, const char *name)
{
    int value = 0;
    size_t len = strlen(name);
    for (const char *p = strstr(cmdline, name); p; p = strstr(p + len, name))
    {
        bool startsWord = p == cmdline || p[-1] == ' ' || p[-1] == '+' || p[-1] == '"';
        const char *v = p + len;
        if (!startsWord || (*v != ' ' && *v != '"'))
            continue;
        while (*v == ' ' || *v == '"')
            ++v;
        value = atoi(v);
    }
    return value;
}

// x6c38: <exe directory>\main\<name>. The emergency copy is found next to the exe in every run (a normal launch may
// start in another working directory; headless runs start in the exe's directory, so nothing changes there).
static void Sys_EmergencyPath(const char *name, char *path, size_t size)
{
    char exePath[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, exePath, sizeof(exePath));
    char *slash = len && len < sizeof(exePath) ? strrchr(exePath, '\\') : NULL;
    if (slash)
    {
        *slash = 0;
        _snprintf(path, size - 1, "%s\\main\\%s", exePath, name);
    }
    else
    {
        _snprintf(path, size - 1, "main\\%s", name);
    }
    path[size - 1] = 0;
}

// last-resort log line for paths where the engine may hold locks (fatal errors, crashes): appended straight to the
// log the engine writes (fs_homepath\main, and headless runs pin fs_homepath to the working directory)
// x6c38: every run, not only headless ones: asserts, crashes and Sys_Error of a normal launch report themselves too
static void Sys_HeadlessEmergencyLog(const char *fmt, ...)
{
    char msg[2048];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(msg, sizeof(msg) - 1, fmt, ap);
    va_end(ap);
    msg[sizeof(msg) - 1] = 0;
    OutputDebugStringA(msg);
    char consolePath[MAX_PATH + 32];
    Sys_EmergencyPath("console_mp.log", consolePath, sizeof(consolePath));
    HANDLE f = CreateFileA(consolePath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE)
    {
        DWORD written;
        WriteFile(f, msg, (DWORD)strlen(msg), &written, NULL);
        CloseHandle(f);
    }
    // the engine's own writes (its CRT FILE keeps its position) can overwrite the line above while other threads
    // still log; this copy survives (headless.ps1 prints it). WinMain deletes it at start.
    char emergencyPath[MAX_PATH + 32];
    Sys_EmergencyPath("bo1_emergency.log", emergencyPath, sizeof(emergencyPath));
    f = CreateFileA(emergencyPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE)
    {
        DWORD written;
        WriteFile(f, msg, (DWORD)strlen(msg), &written, NULL);
        CloseHandle(f);
    }
}

// zombies L5 (BO1Zombies's own - the SP exe hangs here too): quitting while the D3D device is lost.
// A full-screen exclusive D3D9 device is lost when its window loses focus (Alt+Tab, a click on another monitor); the
// render thread then stays in RB_SwapBuffers' TestCooperativeLevel loop (SP 0x006EB3A8) until the window is active
// again. Every quit path needs that thread (Com_Quit_f -> SV_Shutdown -> CL_FreePerLocalClientMemory -> DB_SyncXAssets
// waits in R_EndRemoteScreenUpdate; R_Shutdown waits for the shutdown event; a map / front-end load waits in the
// remote screen update), so a close from the taskbar or Task Manager never finished: Task Manager then killed the
// process as hung. The render thread owns the window and keeps pumping it in that loop, so it sees the request
// (WM_CLOSE, WM_QUIT, or Com_Quit_f) and, if the device is still lost, ends the process itself.
static volatile LONG s_quitRequested;

void __cdecl Sys_SetQuitRequested()
{
    InterlockedExchange(&s_quitRequested, 1);
}

bool __cdecl Sys_IsQuitRequested()
{
    return s_quitRequested != 0 || Sys_QueryWin32QuitEvent();
}

void __cdecl Sys_QuitWithLostDevice(int lostMs)
{
    // no Com_Printf / engine file writes here: the main thread may hold their locks while it waits for this thread
    char msg[256];
    _snprintf(msg, sizeof(msg) - 1,
        "Quit requested while the Direct3D device is lost (%d ms): the renderer cannot finish the shutdown until the "
        "window is active again; exiting now.\n", lostMs);
    msg[sizeof(msg) - 1] = 0;
    OutputDebugStringA(msg);
    char consolePath[MAX_PATH + 32];
    Sys_EmergencyPath("console_mp.log", consolePath, sizeof(consolePath));
    HANDLE f = CreateFileA(consolePath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE)
    {
        DWORD written;
        WriteFile(f, msg, (DWORD)strlen(msg), &written, NULL);
        CloseHandle(f);
    }
    // the desktop display mode is already back (D3D9 restores it when the exclusive window loses focus). No exit():
    // its atexit handlers and DLL detach would run while the blocked main thread holds engine locks.
    timeEndPeriod(1u);
    TerminateProcess(GetCurrentProcess(), 0);
}

// headless thread dumps go to their own file: appending to console_mp.log while the process lives would be
// overwritten by the engine's next writes (its CRT FILE keeps its own position)
static void Sys_HeadlessThreadLog(const char *fmt, ...)
{
    char msg[4096];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(msg, sizeof(msg) - 1, fmt, ap);
    va_end(ap);
    msg[sizeof(msg) - 1] = 0;
    HANDLE f = CreateFileA("main\\bo1_threads.log", FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE)
    {
        DWORD written;
        WriteFile(f, msg, (DWORD)strlen(msg), &written, NULL);
        CloseHandle(f);
    }
}

// headless: end the process without any UI (the log line is the only report)
static void Sys_HeadlessDie(unsigned int exitCode, const char *what)
{
    Sys_HeadlessEmergencyLog("BO1_HEADLESS %s\n", what);
    TerminateProcess(GetCurrentProcess(), exitCode);
}

// headless: an assert has no debugger or message box to report to; log its text before it breaks (assertive.cpp)
// x6c38: every run logs it (a normal launch's assert breaks into the unhandled exception filter without its text)
void __cdecl Sys_HeadlessLogAssert(const char *filename, int line, const char *message)
{
    Sys_HeadlessEmergencyLog("BO1_HEADLESS ASSERT: %s(%d): %s\n", filename, line, message);
}

// headless: record where a crash happened, then let WER (no UI, see WinMain) write Application Error 1000 and end it
static LONG __stdcall Sys_HeadlessExceptionFilter(_EXCEPTION_POINTERS *info)
{
    const EXCEPTION_RECORD *rec = info->ExceptionRecord;
    HMODULE module = NULL;
    char modulePath[MAX_PATH] = "?";
    const char *moduleName = modulePath;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)rec->ExceptionAddress, &module);
    if (module && GetModuleFileNameA(module, modulePath, sizeof(modulePath)))
    {
        const char *slash = strrchr(modulePath, '\\');
        moduleName = slash ? slash + 1 : modulePath;
    }
    Sys_HeadlessEmergencyLog(
        "BO1_HEADLESS CRASH: exception 0x%08X at 0x%08X (%s+0x%X), thread %u\n",
        (unsigned int)rec->ExceptionCode,
        (unsigned int)rec->ExceptionAddress,
        moduleName,
        (unsigned int)rec->ExceptionAddress - (unsigned int)module,
        (unsigned int)GetCurrentThreadId());
    // the return addresses on the faulting stack that point into this module, innermost first (a stack scan, so
    // stale words can appear): feed the offsets to llvm-symbolizer for a call stack
    if (module && module == GetModuleHandleA(NULL))
    {
        const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)((const char *)module + ((const IMAGE_DOS_HEADER *)module)->e_lfanew);
        unsigned int base = (unsigned int)module;
        unsigned int end = base + nt->OptionalHeader.SizeOfImage;
        const unsigned int *sp = (const unsigned int *)info->ContextRecord->Esp;
        char line[1024];
        int len = _snprintf(line, sizeof(line), "BO1_HEADLESS STACK:");
        int found = 0;
        for (int i = 0; i < 2048 && found < 40 && len < (int)sizeof(line) - 16; ++i)
        {
            unsigned int v;
            if (IsBadReadPtr(sp + i, 4))
                break;
            v = sp[i];
            if (v > base + 0x1000 && v < end)
            {
                len += _snprintf(line + len, sizeof(line) - len, " +0x%X", v - base);
                ++found;
            }
        }
        Sys_HeadlessEmergencyLog("%s\n", line);
        // the same stack by its EBP chain (the exe keeps frame pointers): the caller of each frame, innermost
        // first, no stale words. A frame that omits EBP is skipped over, not listed.
        const unsigned int *fp = (const unsigned int *)info->ContextRecord->Ebp;
        len = _snprintf(line, sizeof(line), "BO1_HEADLESS FRAMES:");
        for (int i = 0; i < 40 && len < (int)sizeof(line) - 16; ++i)
        {
            if ((unsigned int)fp < info->ContextRecord->Esp || ((unsigned int)fp & 3) || IsBadReadPtr(fp, 8))
                break;
            const unsigned int ret = fp[1];
            if (ret > base + 0x1000 && ret < end)
                len += _snprintf(line + len, sizeof(line) - len, " +0x%X", ret - base);
            if (fp[0] <= (unsigned int)fp)
                break;
            fp = (const unsigned int *)fp[0];
        }
        Sys_HeadlessEmergencyLog("%s\n", line);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// x6c38: a normal launch keeps its own filter (the minidumper or PrivateUnhandledExceptionFilter); this logs the same
// crash / stack lines to main\bo1_emergency.log first, then hands the exception to it unchanged
static LPTOP_LEVEL_EXCEPTION_FILTER s_prevExceptionFilter;

static LONG __stdcall Sys_LoggingExceptionFilter(_EXCEPTION_POINTERS *info)
{
    Sys_HeadlessExceptionFilter(info);
    if (s_prevExceptionFilter)
        return s_prevExceptionFilter(info);
    return EXCEPTION_CONTINUE_SEARCH;
}

// zombies: headless hang evidence - '+set bo1_threaddump <ms>' (read from the command line, so it works while
// Com_Init is still loading) logs to main\bo1_threads.log every thread's EIP and its stack's return addresses into this exe
// ("BO1_HEADLESS THREAD <tid>: eip +0x.. stack +0x.. ...", offsets for llvm-symbolizer, which headless.ps1 runs),
// that many ms after start and again 10 s later, so two dumps show which threads moved. Each thread is suspended
// only while its context and stack words are copied (no allocation, no lock while it is suspended).
static unsigned int s_threadDumpMs;
static DWORD s_mainThreadId;

static bool Sys_IsCallReturn(unsigned int base, unsigned int end, unsigned int v)
{
    // a return address follows a call: E8 rel32 (5 bytes back) or FF /2 (2, 3, 6 or 7 bytes back)
    if (v < base + 0x1000 + 7 || v >= end)
        return false;
    const unsigned char *p = (const unsigned char *)v;
    if (p[-5] == 0xE8)
        return true;
    if (p[-2] == 0xFF && (p[-1] & 0x38) == 0x10)
        return true;
    if (p[-3] == 0xFF && (p[-2] & 0x38) == 0x10)
        return true;
    if (p[-6] == 0xFF && (p[-5] & 0x38) == 0x10)
        return true;
    if (p[-7] == 0xFF && (p[-6] & 0x38) == 0x10)
        return true;
    return false;
}

// x6 c32: the stack by dbghelp's StackWalk64, which uses the PDB's frame data for the /Oy (no frame pointer) frames
// the EBP chain skips and the stack scan mixes with stale words. It runs after the thread is resumed, on the copied
// stack (the read callback serves the copy; other addresses, e.g. code, are read live). dbghelp is loaded at run
// time, only by this headless evidence path.
typedef BOOL(WINAPI *SysSymInitialize_t)(HANDLE, PCSTR, BOOL);
typedef DWORD(WINAPI *SysSymSetOptions_t)(DWORD);
typedef BOOL(WINAPI *SysStackWalk64_t)(DWORD, HANDLE, HANDLE, LPSTACKFRAME64, PVOID, PREAD_PROCESS_MEMORY_ROUTINE64,
    PFUNCTION_TABLE_ACCESS_ROUTINE64, PGET_MODULE_BASE_ROUTINE64, PTRANSLATE_ADDRESS_ROUTINE64);
static SysStackWalk64_t s_stackWalk64;
static PFUNCTION_TABLE_ACCESS_ROUTINE64 s_symFunctionTableAccess64;
static PGET_MODULE_BASE_ROUTINE64 s_symGetModuleBase64;
static const unsigned int *s_walkStack;
static unsigned int s_walkEsp;
static unsigned int s_walkCopied;

static BOOL __stdcall Sys_HeadlessWalkRead(HANDLE, DWORD64 addr, PVOID buffer, DWORD size, LPDWORD read)
{
    const unsigned int a = (unsigned int)addr;
    if (a >= s_walkEsp && a - s_walkEsp + size <= s_walkCopied)
    {
        memcpy(buffer, (const char *)s_walkStack + (a - s_walkEsp), size);
        *read = size;
        return TRUE;
    }
    SIZE_T got = 0;
    const BOOL ok = ReadProcessMemory(GetCurrentProcess(), (const void *)a, buffer, size, &got);
    *read = (DWORD)got;
    return ok;
}

static bool Sys_HeadlessWalkInit()
{
    static int state; // 0 not tried, 1 ready, 2 unavailable
    if (!state)
    {
        state = 2;
        HMODULE dbghelp = LoadLibraryA("dbghelp.dll");
        if (!dbghelp)
            return false;
        SysSymInitialize_t symInitialize = (SysSymInitialize_t)GetProcAddress(dbghelp, "SymInitialize");
        SysSymSetOptions_t symSetOptions = (SysSymSetOptions_t)GetProcAddress(dbghelp, "SymSetOptions");
        s_stackWalk64 = (SysStackWalk64_t)GetProcAddress(dbghelp, "StackWalk64");
        s_symFunctionTableAccess64 = (PFUNCTION_TABLE_ACCESS_ROUTINE64)GetProcAddress(dbghelp, "SymFunctionTableAccess64");
        s_symGetModuleBase64 = (PGET_MODULE_BASE_ROUTINE64)GetProcAddress(dbghelp, "SymGetModuleBase64");
        if (!symInitialize || !symSetOptions || !s_stackWalk64 || !s_symFunctionTableAccess64 || !s_symGetModuleBase64)
            return false;
        char exeDir[MAX_PATH];
        GetModuleFileNameA(NULL, exeDir, sizeof(exeDir));
        char *slash = strrchr(exeDir, '\\');
        if (slash)
            *slash = 0;
        symSetOptions(SYMOPT_UNDNAME | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
        if (!symInitialize(GetCurrentProcess(), exeDir, TRUE))
            return false;
        state = 1;
    }
    return state == 1;
}

static void Sys_HeadlessDumpThreads(int pass)
{
    HMODULE module = GetModuleHandleA(NULL);
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)((const char *)module + ((const IMAGE_DOS_HEADER *)module)->e_lfanew);
    unsigned int base = (unsigned int)module;
    unsigned int end = base + nt->OptionalHeader.SizeOfImage;
    static unsigned int stack[8192];
    static char line[4096];
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return;
    Sys_HeadlessThreadLog("BO1_HEADLESS THREADDUMP %d at %u ms (main thread %u)\n", pass, Sys_Milliseconds(), (unsigned int)s_mainThreadId);
    THREADENTRY32 te;
    te.dwSize = sizeof(te);
    for (BOOL ok = Thread32First(snap, &te); ok; ok = Thread32Next(snap, &te))
    {
        if (te.th32OwnerProcessID != GetCurrentProcessId() || te.th32ThreadID == GetCurrentThreadId())
            continue;
        HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
        if (!th)
            continue;
        CONTEXT ctx;
        memset(&ctx, 0, sizeof(ctx));
        ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        SIZE_T copied = 0;
        bool got = false;
        if (SuspendThread(th) != (DWORD)-1)
        {
            if (GetThreadContext(th, &ctx))
            {
                got = true;
                MEMORY_BASIC_INFORMATION mbi;
                SIZE_T want = sizeof(stack);
                if (VirtualQuery((const void *)ctx.Esp, &mbi, sizeof(mbi)))
                {
                    SIZE_T left = (SIZE_T)mbi.BaseAddress + mbi.RegionSize - ctx.Esp;
                    if (left < want)
                        want = left;
                }
                ReadProcessMemory(GetCurrentProcess(), (const void *)ctx.Esp, stack, want, &copied);
            }
            ResumeThread(th);
        }
        CloseHandle(th);
        if (!got)
            continue;
        int len = _snprintf(line, sizeof(line), "BO1_HEADLESS THREAD %u: eip %s0x%X stack",
            (unsigned int)te.th32ThreadID,
            ctx.Eip >= base && ctx.Eip < end ? "+" : "abs ",
            ctx.Eip >= base && ctx.Eip < end ? (unsigned int)(ctx.Eip - base) : (unsigned int)ctx.Eip);
        int found = 0;
        for (unsigned int i = 0; i < copied / 4 && found < 48 && len < (int)sizeof(line) - 16; ++i)
        {
            if (Sys_IsCallReturn(base, end, stack[i]))
            {
                len += _snprintf(line + len, sizeof(line) - len, " +0x%X", stack[i] - base);
                ++found;
            }
        }
        Sys_HeadlessThreadLog("%s\n", line);
        // x6 c32: the same stack by its EBP chain, read from the copy (as the crash FRAMES line): no stale words;
        // frames that omit EBP are skipped over, not listed.
        len = _snprintf(line, sizeof(line), "BO1_HEADLESS THREADFRAMES %u: ebp", (unsigned int)te.th32ThreadID);
        unsigned int fp = ctx.Ebp;
        for (int i = 0; i < 48 && len < (int)sizeof(line) - 16; ++i)
        {
            if (fp < ctx.Esp || (fp & 3) || fp - ctx.Esp + 8 > copied)
                break;
            const unsigned int *frame = &stack[(fp - ctx.Esp) / 4];
            if (frame[1] > base + 0x1000 && frame[1] < end)
                len += _snprintf(line + len, sizeof(line) - len, " +0x%X", frame[1] - base);
            if (frame[0] <= fp)
                break;
            fp = frame[0];
        }
        Sys_HeadlessThreadLog("%s\n", line);
        if (Sys_HeadlessWalkInit())
        {
            // every frame's return address, this exe's as +offsets (for llvm-symbolizer), others as abs
            len = _snprintf(line, sizeof(line), "BO1_HEADLESS THREADWALK %u: walk", (unsigned int)te.th32ThreadID);
            s_walkStack = stack;
            s_walkEsp = ctx.Esp;
            s_walkCopied = (unsigned int)copied;
            CONTEXT walkCtx = ctx;
            STACKFRAME64 sf;
            memset(&sf, 0, sizeof(sf));
            sf.AddrPC.Offset = ctx.Eip;
            sf.AddrPC.Mode = AddrModeFlat;
            sf.AddrFrame.Offset = ctx.Ebp;
            sf.AddrFrame.Mode = AddrModeFlat;
            sf.AddrStack.Offset = ctx.Esp;
            sf.AddrStack.Mode = AddrModeFlat;
            for (int i = 0; i < 64 && len < (int)sizeof(line) - 20; ++i)
            {
                if (!s_stackWalk64(IMAGE_FILE_MACHINE_I386, GetCurrentProcess(), NULL, &sf, &walkCtx, Sys_HeadlessWalkRead,
                        s_symFunctionTableAccess64, s_symGetModuleBase64, NULL))
                    break;
                const unsigned int pc = (unsigned int)sf.AddrPC.Offset;
                if (!pc)
                    break;
                if (pc >= base && pc < end)
                    len += _snprintf(line + len, sizeof(line) - len, " +0x%X", pc - base);
                else
                    len += _snprintf(line + len, sizeof(line) - len, " abs:%X", pc);
            }
            Sys_HeadlessThreadLog("%s\n", line);
        }
    }
    CloseHandle(snap);
}

static DWORD __stdcall Sys_HeadlessThreadDumpThread(void *)
{
    Sleep(s_threadDumpMs);
    Sys_HeadlessDumpThreads(1);
    Sleep(10000);
    Sys_HeadlessDumpThreads(2);
    return 0;
}

// k1: headless client hitch evidence - '+set bo1_hitchwatch <ms>' (command line, headless only): while the main
// thread is inside one watched client frame (CG_DrawActiveFrame, see G_SP_MeasureClientCgBegin) for longer than <ms>,
// a watchdog thread writes Sys_HeadlessDumpThreads to main\bo1_threads.log, then again every 200 ms of the same
// frame (at most 4 dumps per frame, 240 in total). The header carries the watched frame number, which the
// BO1_CLIENTHITCH console line of that frame repeats. Do not combine with bo1_threaddump (shared buffers).
static unsigned int s_hitchWatchMs;
static unsigned int s_framePerfMs; // p1: '+set bo1_frameperf <ms>' (command line; L43: real window too), G_SP_FramePerfMark

unsigned int __cdecl Sys_FramePerfHitchMs()
{
    return s_framePerfMs;
}
static volatile LONG s_hitchFrameSeq; // odd while the main thread is inside a watched frame
static volatile DWORD s_hitchFrameStart;

unsigned int __cdecl Sys_HitchWatchBegin()
{
    if (!s_hitchWatchMs)
        return 0;
    s_hitchFrameStart = GetTickCount();
    return (unsigned int)(InterlockedIncrement(&s_hitchFrameSeq) + 1) / 2;
}

void __cdecl Sys_HitchWatchEnd()
{
    if (s_hitchWatchMs && (s_hitchFrameSeq & 1))
        InterlockedIncrement(&s_hitchFrameSeq);
}

static DWORD __stdcall Sys_HitchWatchThread(void *)
{
    LONG dumpedSeq = -1;
    int dumps = 0;
    int total = 0;
    while (total < 240)
    {
        Sleep(10);
        const LONG seq = s_hitchFrameSeq;
        if (!(seq & 1))
            continue;
        if (seq != dumpedSeq)
            dumps = 0;
        const DWORD age = GetTickCount() - s_hitchFrameStart;
        if (dumps >= 4 || age < s_hitchWatchMs + 200u * dumps || seq != s_hitchFrameSeq)
            continue;
        dumpedSeq = seq;
        ++dumps;
        ++total;
        Sys_HeadlessThreadLog("BO1_HEADLESS HITCH frame %u age %u ms dump %d\n", (unsigned int)(seq + 1) / 2, (unsigned int)age, dumps);
        Sys_HeadlessDumpThreads(total);
        Sys_HeadlessThreadLog("BO1_HEADLESS HITCH frame %u still_inside %d\n", (unsigned int)(seq + 1) / 2, seq == s_hitchFrameSeq ? 1 : 0);
    }
    return 0;
}

// bo1_autoquit <ms>: quit once this long has passed since Com_Init returned (the + commands, a map load included,
// run inside Com_Init). 0 = off.
// zombies: headless client evidence - '+set bo1_shots "20000 40000"' takes 'screenshotJpeg bo1_<ms>' (the back
// buffer, r_screenshot.cpp) that many ms after init, in order, and logs the client's state with each one
static const dvar_t *bo1_shots;
static int s_shotsTaken;

// Headless evidence only: command-buffer waits also run during loading. Start this
// real-time schedule at CA_ACTIVE so menu commands run after cgame initialization.
static const dvar_t *bo1_clientCommands;
static int s_clientCommandsStart = -1;
static int s_clientCommandsRun;

static void Sys_HeadlessClientCommandsFrame(int elapsed)
{
    if (!Sys_IsHeadlessClient() || !bo1_clientCommands || !bo1_clientCommands->current.string[0])
        return;
    if (CL_GetLocalClientConnectionState(0) != CA_ACTIVE)
        return;
    if (s_clientCommandsStart < 0)
        s_clientCommandsStart = elapsed;
    const char *p = bo1_clientCommands->current.string;
    for (int i = 0; *p; ++i)
    {
        char *end;
        const long at = strtol(p, &end, 10);
        if (end == p || *end != ':' || at < 0)
            return;
        p = end + 1;
        const char *separator = strchr(p, ';');
        const int length = separator ? (int)(separator - p) : (int)strlen(p);
        if (i >= s_clientCommandsRun)
        {
            if (elapsed - s_clientCommandsStart < at)
                return;
            char command[1024];
            if (length >= sizeof(command) - 1)
                return;
            memcpy(command, p, length);
            command[length] = '\n';
            command[length + 1] = 0;
            ++s_clientCommandsRun;
            Com_Printf(16, "bo1_clientCommands: %d ms after CA_ACTIVE; %s", elapsed - s_clientCommandsStart, command);
            Cbuf_AddText(0, command);
            return;
        }
        p += length;
        if (*p == ';')
            ++p;
    }
}

// zombies: headless evidence - '+set bo1_menudump "main_text levels_zombie"' prints, at each bo1_shots shot,
// the named menus' event scripts and their items' (index, name, events): how the SP front end's menus are driven.
static const dvar_t *bo1_menudump;

static const char *Sys_HeadlessExpToString(const ExpressionStatement *exp);
static void Sys_HeadlessMenuDumpEvents(const char *owner, const GenericEventHandler *handler)
{
    for (; handler; handler = handler->next)
        for (const GenericEventScript *script = handler->eventScript; script; script = script->next)
        {
            if (script->condition.filename)
                Com_Printf(16, "bo1_menudump: %s %s: type %d if (%s) fireOnTrue %d\n", owner,
                    handler->name ? handler->name : "", script->type, Sys_HeadlessExpToString(&script->condition),
                    script->fireOnTrue);
            if (script->action && script->action[0])
                Com_Printf(16, "bo1_menudump: %s %s: %s\n", owner, handler->name ? handler->name : "", script->action);
        }
}

// x6: an expression (visible when / forecolor alpha / image material) as its RPN tokens, for reading the SP menus'
// conditions. Type 1 = op index (not yet run), 2 = resolved function pointer (looked up in rpnFunctions).
extern const char *g_expOperatorNames[];
extern const char *g_expFunctionNames[457];
extern void(__cdecl *rpnFunctions[480])(const int, itemDef_s *, OperandStack *);
static const char *Sys_HeadlessExpToString(const ExpressionStatement *exp)
{
    static char text[1024];
    int len = 0;
    text[0] = 0;
    if (!exp || !exp->filename || !exp->rpn)
        return text;
    for (int i = 0; i < exp->numRpn && exp->rpn[i].type != 3 && len < 960; ++i)
    {
        const expressionRpn *r = &exp->rpn[i];
        const char *tok = "?";
        char buf[96];
        if (r->type == 0)
        {
            const Operand &c = r->data.constant;
            if (c.dataType == VAL_INT)
                tok = va("%d", c.internals.intVal);
            else if (c.dataType == VAL_FLOAT)
                tok = va("%g", c.internals.floatVal);
            else
                tok = va("\"%s\"", c.internals.string ? c.internals.string : "");
        }
        else
        {
            int fn = -1;
            if (r->type == 1)
                fn = Expression_GetFunctionForOp(r->data.cmdIdx);
            else
                for (int k = 0; k < 480; ++k)
                    if ((void *)rpnFunctions[k] == r->data.cmd)
                    {
                        fn = k;
                        break;
                    }
            tok = fn < 0 ? "?fn" : fn < 24 ? g_expOperatorNames[fn] : (fn - 24 < 457 && g_expFunctionNames[fn - 24] ? g_expFunctionNames[fn - 24] : "?f");
        }
        I_strncpyz(buf, tok, sizeof(buf));
        len += _snprintf(text + len, sizeof(text) - len - 1, "%s ", buf);
    }
    return text;
}

static void Sys_HeadlessMenuDump()
{
    if (!bo1_menudump || !bo1_menudump->current.string[0])
        return;
    // the UI's menus, then the cgame's HUD menus (cgDC, CG_LoadHudMenu)
    for (int pass = 0; pass < 2; ++pass)
    {
    UiContext *dc = pass ? &cgDC[0] : &UI_GetInfo(0)->uiDC;
    for (int m = 0; m < dc->menuCount; ++m)
    {
        const menuDef_t *menu = dc->Menus[m];
        char token[64];
        const char *p = bo1_menudump->current.string;
        bool wanted = false;
        while (*p && !wanted)
        {
            int n = 0;
            while (*p == ' ')
                ++p;
            while (*p && *p != ' ' && n < 63)
                token[n++] = *p++;
            token[n] = 0;
            wanted = n && (!strcmp(token, "*") || !I_stricmp(token, menu->window.name)); // "*" = every loaded menu
        }
        if (!wanted)
            continue;
        Com_Printf(16, "bo1_menudump: menu '%s' items %d\n", menu->window.name, menu->itemCount);
        if (menu->visibleExp.filename)
            Com_Printf(16, "bo1_menudump: %s visible: %s\n", menu->window.name, Sys_HeadlessExpToString(&menu->visibleExp));
        Sys_HeadlessMenuDumpEvents(menu->window.name, menu->onEvent);
        for (const ItemKeyHandler *key = menu->onKey; key; key = key->next)
            for (const GenericEventScript *script = key->keyScript; script; script = script->next)
                Com_Printf(16, "bo1_menudump: %s key %d: %s\n", menu->window.name, key->key, script->action ? script->action : "");
        for (int i = 0; i < menu->itemCount; ++i)
        {
            const itemDef_s *item = menu->items[i];
            Com_Printf(16, "bo1_menudump: %s/%d '%s' type %d style %d rect %g %g %g %g bg '%s' dvar '%s' dvarTest '%s' "
                "enableDvar '%s' dvarFlags 0x%x flags 0x%x ownerdraw %d text '%s'\n", menu->window.name, i,
                item->window.name ? item->window.name : "", item->type, item->window.style, item->window.rect.x,
                item->window.rect.y, item->window.rect.w, item->window.rect.h,
                item->window.background ? Material_GetName(item->window.background) : "", item->dvar ? item->dvar : "",
                item->dvarTest ? item->dvarTest : "", item->enableDvar ? item->enableDvar : "", item->dvarFlags,
                item->window.dynamicFlags[0], item->window.ownerDraw,
                (item->type == 0 || item->type == 1) && item->typeData.textDef && item->typeData.textDef->text
                    ? item->typeData.textDef->text : "");
            if (item->visibleExp.filename)
                Com_Printf(16, "bo1_menudump: %s/%d visible: %s\n", menu->window.name, i, Sys_HeadlessExpToString(&item->visibleExp));
            if (item->forecolorAExp.filename)
                Com_Printf(16, "bo1_menudump: %s/%d forecolorA: %s\n", menu->window.name, i, Sys_HeadlessExpToString(&item->forecolorAExp));
            if (item->type == 2 && item->typeData.imageDef && item->typeData.imageDef->materialExp.filename)
                Com_Printf(16, "bo1_menudump: %s/%d material: %s\n", menu->window.name, i, Sys_HeadlessExpToString(&item->typeData.imageDef->materialExp));
            Sys_HeadlessMenuDumpEvents(va("%s/%d '%s'", menu->window.name, i, item->window.name ? item->window.name : ""),
                item->onEvent);
        }
    }
    }
}

extern int g_bo1SceneBrushTotal, g_bo1SceneBrushDrawn; // r_dpvs.cpp (mapkit-fix-4a)
static void Sys_HeadlessShotsFrame(int elapsed)
{
    if (!Sys_IsHeadlessClient() || !bo1_shots || !bo1_shots->current.string[0])
        return;
    const char *p = bo1_shots->current.string;
    // j1: a token "A-B/S" is every S ms from A to B (a burst at one window without a long command line).
    for (int i = 0;;)
    {
        while (*p == ' ' || *p == ',')
            ++p;
        if (!*p)
            return;
        char *end;
        int at = (int)strtol(p, &end, 10), last = at, step = 1;
        if (end == p)
            return;
        p = end;
        if (*p == '-')
        {
            last = (int)strtol(p + 1, &end, 10);
            p = end;
            if (*p == '/')
            {
                step = (int)strtol(p + 1, &end, 10);
                p = end;
            }
            if (step < 1)
                step = 1;
        }
        while (*p && *p != ' ' && *p != ',')
            ++p;
        for (; at <= last; at += step, ++i)
        {
            if (i < s_shotsTaken)
                continue;
            if (elapsed < at)
                return;
            ++s_shotsTaken;
            Com_Printf(16, "bo1_shots: %d ms after init; connection state %d, sv_running %d, mapname '%s', cl_paused %d, level.time %d; screenshotJpeg bo1_%d\n",
                elapsed,
                CL_GetLocalClientConnectionState(0),
                com_sv_running->current.enabled,
                Dvar_GetString("mapname"),
                Dvar_GetInt("cl_paused"),
                com_sv_running->current.enabled ? level.time : 0,
                at);
            Com_Printf(16, "bo1_shots: scene brush models drawn %d of %d (mapkit-fix-4a)\n", g_bo1SceneBrushDrawn, g_bo1SceneBrushTotal);
            Com_Printf(16, "bo1_shots: ui catcher %d, top menu '%s', open menus %d\n",
                Key_IsCatcherActive(0, 16), UI_GetTopActiveMenuName(0) ? UI_GetTopActiveMenuName(0) : "",
                UI_GetInfo(0)->uiDC.openMenuCount);
            for (int m = 0; m < UI_GetInfo(0)->uiDC.openMenuCount; ++m)
                Com_Printf(16, "bo1_shots: open menu %d '%s'\n", m, UI_GetInfo(0)->uiDC.menuStack[m].menu->window.name);
            Sys_HeadlessMenuDump();
            Cbuf_AddText(0, va("screenshotJpeg bo1_%d\n", at));
            return;
        }
    }
}

// zombies: headless evidence - real-time timeline (ms after init) of the game-over restart: loadgame_continue,
// the local client's connection state, sv_running, the local player's pm_type and whether cgame drew the HUD.
static int s_timelineConn = -2;
static int s_timelineSv = -2;
static int s_timelinePm = -2;
static int s_timelineHud = -2;
static int s_hudPmType = -1;
static bool s_hudDrawn;

void __cdecl Sys_HeadlessNoteHud(int pmType, bool hudDrawn)
{
    s_hudPmType = pmType;
    s_hudDrawn = hudDrawn;
}

void __cdecl Sys_HeadlessTimeline(const char *what)
{
    if (!Sys_IsHeadless() || !s_autoquitStartMs)
        return;
    Com_Printf(16, "bo1_timeline: %d ms after init; %s\n", Sys_Milliseconds() - s_autoquitStartMs, what);
}

static void Sys_HeadlessTimelineFrame(int elapsed)
{
    if (!Sys_IsHeadless())
        return;
    const int conn = Sys_IsHeadlessClient() ? (int)CL_GetLocalClientConnectionState(0) : -1;
    const int sv = com_sv_running->current.enabled;
    if (conn != CA_ACTIVE)
    {
        s_hudPmType = -1;
        s_hudDrawn = false;
    }
    const int pm = s_hudPmType;
    const int hud = s_hudDrawn;
    if (conn == s_timelineConn && sv == s_timelineSv && pm == s_timelinePm && hud == s_timelineHud)
        return;
    s_timelineConn = conn;
    s_timelineSv = sv;
    s_timelinePm = pm;
    s_timelineHud = hud;
    Com_Printf(16, "bo1_timeline: %d ms after init; connection state %d, sv_running %d, pm_type %d, hud %d, level.time %d\n",
        elapsed, conn, sv, pm, hud, sv ? level.time : 0);
}

// x6c35: headless menu sweep - 'bo1_menusweep pausedmenu,options_new_pc,pausedmenu+popup_restart_warning,...'
// drives every item of each listed menu the way a player's keyboard does: the menu stack is set to the entry
// ('a+b' = b opened on top of a), then per item: focus it (Item_SetFocus, the mouse-over path; items it refuses are
// hidden or inert and are skipped), and one key through Menu_HandleKey (what UI_KeyEvent hands the top menu):
// dvar items (slider / yes-no / multi / dvar enum) get Right then Left (the value must change and come back), list
// boxes Down then Up, bind items Enter (wait for a key) then Esc (cancel), everything else Enter (Item_Action).
// After an Enter the resulting stack is logged and the entry's stack restored (missing menus opened, extra menus
// closed). Items whose scripts would restart the renderer / sound / level or leave the game are logged, not run.
// One step per bo1_menusweep_stepms (real ms) so deferred (Cbuf) commands run between steps; the first item of
// each entry waits bo1_menusweep_shotms and takes screenshotJpeg c35_<entry> (0 = no shot).
extern int g_waitingForKey; // ui_shared.cpp: a bind item waits for the key to bind
static const dvar_t *bo1_menusweep_stepms;
static const dvar_t *bo1_menusweep_shotms;
static char s_sweepList[1024];
static const char *s_sweepNext;          // next entry in s_sweepList
static char s_sweepEntry[128];            // current entry
static menuDef_t *s_sweepStack[8];        // the entry's stack, bottom first
static int s_sweepStackCount;
static int s_sweepItem = -1;              // current item index (-1: entry not set up)
static int s_sweepPhase;                  // 0 = press, 1 = check / reverse, 2 = check reverse / restore, 3 = check restore
static int s_sweepKey;
static int s_sweepLastMs;
static char s_sweepBefore[256];
static int s_sweepDriven, s_sweepSkipped, s_sweepDenied;

// the keys bound to a bind item's command, as "keynum keynum"
static const char *Key_KeynumToStringBinding(const char *command)
{
    static char text[32];
    int keys[2];
    Key_GetCommandAssignment(0, command, keys, 0);
    _snprintf(text, sizeof(text) - 1, "%d %d", keys[0], keys[1]);
    text[sizeof(text) - 1] = 0;
    return text;
}

static bool Sys_SweepActive()
{
    return s_sweepNext != nullptr;
}

static const char *Sys_SweepStackString(UiContext *dc)
{
    static char text[512];
    int len = 0;
    text[0] = 0;
    for (int m = 0; m < dc->openMenuCount && len < (int)sizeof(text) - 64; ++m)
        len += _snprintf(text + len, sizeof(text) - len - 1, "%s%s", m ? "," : "", dc->menuStack[m].menu->window.name);
    text[len] = 0;
    return text;
}

static bool Sys_SweepMenuOpen(UiContext *dc, const menuDef_t *menu)
{
    for (int m = 0; m < dc->openMenuCount; ++m)
        if (dc->menuStack[m].menu == menu)
            return true;
    return false;
}

// the entry's stack: open what is missing (bottom first), then close everything else (top first)
static void Sys_SweepRestoreStack(UiContext *dc)
{
    // an item that left the menus (pause: Resume) dropped the UI key catcher: bring the in-game menu back the way
    // toggleMenu does (CL_ToggleMenu_f), so the rest runs paused with the menus drawn
    if (!(CL_GetLocalClientUIGlobals(0)->keyCatchers & 0x10) && CL_GetLocalClientConnectionState(0) == CA_ACTIVE)
    {
        Com_Printf(16, "bo1_menusweep: UI catcher off; UI_SetActiveMenu ingame\n");
        UI_SetActiveMenu(0, UIMENU_INGAME);
    }
    for (int s = 0; s < s_sweepStackCount; ++s)
        if (!Sys_SweepMenuOpen(dc, s_sweepStack[s]))
            Menus_OpenByName(0, dc, s_sweepStack[s]->window.name);
    for (int m = dc->openMenuCount - 1; m >= 0; --m)
    {
        menuDef_t *menu = dc->menuStack[m].menu;
        bool wanted = false;
        for (int s = 0; s < s_sweepStackCount; ++s)
            wanted |= s_sweepStack[s] == menu;
        if (!wanted)
            Menus_Close(0, dc, menu);
    }
}

static bool Sys_SweepItemDenied(const itemDef_s *item, const char **why)
{
    // whole command words only (a menu named popup_..._quit is not a quit)
    static const char *deny[] = { "vid_restart", "snd_restart", "quit", "disconnect", "fast_restart", "map_restart",
        "restart_level", "savegame", "loadgame", "updatesavegame", "startsingleplayer", "vid_restart_popmenu_apply" };
    for (const GenericEventHandler *handler = item->onEvent; handler; handler = handler->next)
    {
        if (!handler->name || I_stricmp(handler->name, "action"))
            continue;
        for (const GenericEventScript *script = handler->eventScript; script; script = script->next)
        {
            if (!script->action)
                continue;
            char words[1024];
            I_strncpyz(words, script->action, sizeof(words));
            for (char *tok = strtok(words, " ;\"\t\r\n"); tok; tok = strtok(nullptr, " ;\"\t\r\n"))
                for (const char *d : deny)
                    if (!I_stricmp(tok, d))
                    {
                        *why = d;
                        return true;
                    }
        }
    }
    return false;
}

static bool Sys_SweepNextEntry(UiContext *dc)
{
    while (s_sweepNext && *s_sweepNext)
    {
        while (*s_sweepNext == ' ' || *s_sweepNext == ',')
            ++s_sweepNext;
        int n = 0;
        while (*s_sweepNext && *s_sweepNext != ' ' && *s_sweepNext != ',' && n < (int)sizeof(s_sweepEntry) - 1)
            s_sweepEntry[n++] = *s_sweepNext++;
        s_sweepEntry[n] = 0;
        if (!n)
            continue;
        s_sweepStackCount = 0;
        char names[128];
        I_strncpyz(names, s_sweepEntry, sizeof(names));
        bool ok = true;
        for (char *tok = strtok(names, "+"); tok && s_sweepStackCount < 8; tok = strtok(nullptr, "+"))
        {
            menuDef_t *menu = Menus_FindByName(dc, tok);
            if (!menu)
            {
                Com_Printf(16, "bo1_menusweep: entry '%s': no menu '%s'\n", s_sweepEntry, tok);
                ok = false;
                break;
            }
            s_sweepStack[s_sweepStackCount++] = menu;
        }
        if (!ok || !s_sweepStackCount)
            continue;
        Sys_SweepRestoreStack(dc);
        Com_Printf(16, "bo1_menusweep: entry '%s' items %d, stack '%s'\n", s_sweepEntry,
            s_sweepStack[s_sweepStackCount - 1]->itemCount, Sys_SweepStackString(dc));
        s_sweepItem = -1;
        return true;
    }
    Com_Printf(16, "bo1_menusweep: done; driven %d, skipped (hidden / inert) %d, not run (restart / quit) %d\n",
        s_sweepDriven, s_sweepSkipped, s_sweepDenied);
    s_sweepNext = nullptr;
    return false;
}

static void Sys_SweepPress(UiContext *dc, menuDef_t *menu, int key)
{
    Menu_HandleKey(0, dc, menu, key, 1);
    Menu_HandleKey(0, dc, menu, key, 0);
}

static void Sys_HeadlessMenuSweepFrame()
{
    if (!Sys_SweepActive())
        return;
    const int now = Sys_Milliseconds();
    UiContext *dc = &UI_GetInfo(0)->uiDC;
    const int wait = s_sweepItem < 0 && s_sweepPhase == 0 && bo1_menusweep_shotms->current.integer > 0
        ? bo1_menusweep_shotms->current.integer : bo1_menusweep_stepms->current.integer;
    if (now - s_sweepLastMs < wait)
        return;
    s_sweepLastMs = now;
    if (!s_sweepStackCount)
    {
        Sys_SweepNextEntry(dc); // the entry is opened now; its shot waits bo1_menusweep_shotms
        return;
    }
    menuDef_t *menu = s_sweepStack[s_sweepStackCount - 1];
    if (s_sweepItem < 0)
    {
        if (s_sweepPhase == 0 && bo1_menusweep_shotms->current.integer > 0)
        {
            // the entry has been open bo1_menusweep_shotms: its fade-in is over
            Cbuf_AddText(0, va("screenshotJpeg c35_%s\n", s_sweepEntry));
            s_sweepPhase = 1;
            return;
        }
        s_sweepPhase = 0;
        s_sweepItem = 0;
    }
    for (;;)
    {
        if (s_sweepItem >= menu->itemCount)
        {
            s_sweepStackCount = 0;
            s_sweepPhase = 0;
            return; // the next frame after stepms sets up the next entry
        }
        itemDef_s *item = menu->items[s_sweepItem];
        const bool dvarItem = item->dvar && (item->type == 8 || item->type == 9 || item->type == 10 || item->type == 11);
        const bool bindItem = item->type == 12 || item->type == 16;
        const bool listItem = item->type == 4;
        if (s_sweepPhase == 0)
        {
            if (!Sys_SweepMenuOpen(dc, menu) || dc->menuStack[dc->openMenuCount - 1].menu != menu)
                Sys_SweepRestoreStack(dc);
            const int focused = Item_SetFocus(0, dc, item, item->window.rect.x + item->window.rect.w * 0.5f,
                item->window.rect.y + item->window.rect.h * 0.5f);
            if (!focused)
            {
                ++s_sweepSkipped;
                ++s_sweepItem;
                continue; // hidden, disabled or decoration: no time spent
            }
            const char *why = nullptr;
            if (!dvarItem && !bindItem && !listItem && Sys_SweepItemDenied(item, &why))
            {
                Com_Printf(16, "bo1_menusweep: %s/%d type %d '%s': not run (action has '%s')\n", menu->window.name,
                    s_sweepItem, item->type, item->window.name ? item->window.name : "", why);
                ++s_sweepDenied;
                ++s_sweepItem;
                return;
            }
            s_sweepKey = dvarItem ? K_RIGHTARROW : listItem ? K_DOWNARROW : K_ENTER;
            const char *dvarName = bindItem ? nullptr : item->dvar;
            I_strncpyz(s_sweepBefore, bindItem && item->dvar ? Key_KeynumToStringBinding(item->dvar) :
                dvarName && Dvar_FindVar(dvarName) ? Dvar_GetVariantString(dvarName) : "", sizeof(s_sweepBefore));
            Sys_SweepPress(dc, menu, s_sweepKey);
            ++s_sweepDriven;
            s_sweepPhase = 1;
            return;
        }
        const char *dvarName = bindItem ? nullptr : item->dvar;
        const char *value = bindItem && item->dvar ? Key_KeynumToStringBinding(item->dvar)
            : dvarName && Dvar_FindVar(dvarName) ? Dvar_GetVariantString(dvarName) : "";
        if (s_sweepPhase == 1)
        {
            Com_Printf(16, "bo1_menusweep: %s/%d type %d '%s' key %d dvar '%s' '%s' -> '%s' stack '%s'%s\n",
                menu->window.name, s_sweepItem, item->type, item->window.name ? item->window.name : "", s_sweepKey,
                item->dvar ? item->dvar : "", s_sweepBefore, value, Sys_SweepStackString(dc),
                bindItem ? (g_waitingForKey ? " waiting for a key" : " NOT waiting for a key") : "");
            if (dvarItem && !strcmp(value, s_sweepBefore))
            {
                // at the top of its range (a full slider): Left changes it, Right brings it back
                Sys_SweepPress(dc, menu, K_LEFTARROW);
                s_sweepPhase = 5;
                return;
            }
            if (dvarItem || listItem || bindItem)
            {
                Sys_SweepPress(dc, menu, dvarItem ? K_LEFTARROW : listItem ? K_UPARROW : K_ESCAPE);
                s_sweepPhase = 2;
                return;
            }
            Sys_SweepRestoreStack(dc);
            s_sweepPhase = 3;
            return;
        }
        if (s_sweepPhase == 5)
        {
            Com_Printf(16, "bo1_menusweep: %s/%d key %d dvar '%s' '%s' -> '%s'\n", menu->window.name, s_sweepItem,
                K_LEFTARROW, item->dvar, s_sweepBefore, value);
            Sys_SweepPress(dc, menu, K_RIGHTARROW);
            s_sweepPhase = 2;
            return;
        }
        if (s_sweepPhase == 2)
            Com_Printf(16, "bo1_menusweep: %s/%d back dvar '%s' '%s' (%s) stack '%s'\n", menu->window.name, s_sweepItem,
                item->dvar ? item->dvar : "", value, strcmp(value, s_sweepBefore) ? "CHANGED" : "restored",
                Sys_SweepStackString(dc));
        else
            Com_Printf(16, "bo1_menusweep: %s/%d restored stack '%s'\n", menu->window.name, s_sweepItem,
                Sys_SweepStackString(dc));
        if (s_sweepPhase == 2 && (!Sys_SweepMenuOpen(dc, menu) || dc->menuStack[dc->openMenuCount - 1].menu != menu))
            Sys_SweepRestoreStack(dc);
        s_sweepPhase = 0;
        ++s_sweepItem;
        return;
    }
}

static void Sys_HeadlessMenuSweep_f()
{
    if (Cmd_Argc() < 2)
        return;
    I_strncpyz(s_sweepList, Cmd_Argv(1), sizeof(s_sweepList));
    s_sweepNext = s_sweepList;
    s_sweepStackCount = 0;
    s_sweepItem = -1;
    s_sweepPhase = 0;
    s_sweepDriven = s_sweepSkipped = s_sweepDenied = 0;
    s_sweepLastMs = Sys_Milliseconds() - 100000;
    Com_Printf(16, "bo1_menusweep: start '%s'\n", s_sweepList);
}

// zombies: headless evidence - '+set bo1_dvardump 1' prints every registered dvar when bo1_autoquit fires, one
// line each: bo1_dvardump<TAB>name<TAB>type<TAB>flags<TAB>reset<TAB>current (compared against SP's dvar pool)
static const dvar_t *bo1_dvardump;

static void __cdecl Sys_DvarDumpOne(const dvar_s *dvar, void *)
{
    Com_Printf(16, "bo1_dvardump\t%s\t%d\t0x%x\t%s\t%s\n", dvar->name, dvar->type, dvar->flags,
        Dvar_DisplayableResetValue(dvar), Dvar_DisplayableValue(dvar));
}

// L20: bo1_quitnotify "<level notify> <count> <level ms>" (headless only, off when empty) quits <level ms> of level
// time after the level notify has fired <count> times, counted over the whole process (so across fast restarts;
// level.time runs on through SV_MapRestart). A check whose events follow level time (orch/restart.sh: two game overs,
// then the 3rd map's round 1) ends on game state instead of real time, so machine load (a slow load or a slower 4x)
// cannot cut it short; bo1_autoquit stays the real-time safety. Called from G_SP_MeasureNotify (level notifies).
static const dvar_t *bo1_quitnotify;
static int s_quitNotifySeen;
static int s_quitNotifyAt = -1;

void __cdecl Sys_HeadlessLevelNotify(const char *name)
{
    if (!Sys_IsHeadless() || !bo1_quitnotify || !*bo1_quitnotify->current.string || s_quitNotifyAt >= 0 || !name)
        return;
    // "name,count,ms" (commas: a +set on the command line keeps it one token) or "name count ms"
    char spec[128] = {};
    strncpy_s(spec, bo1_quitnotify->current.string, _TRUNCATE);
    for (char *c = spec; *c; ++c)
        if (*c == ',')
            *c = ' ';
    char want[64] = {};
    int count = 1, delay = 0;
    if (sscanf_s(spec, "%63s %d %d", want, (unsigned int)sizeof(want), &count, &delay) < 1
        || strcmp(name, want))
        return;
    if (++s_quitNotifySeen < count)
        return;
    s_quitNotifyAt = level.time + (delay > 0 ? delay : 0);
    Com_Printf(16, "bo1_quitnotify: level.time %d, level notify '%s' #%d; quitting at level.time %d\n", level.time,
        name, s_quitNotifySeen, s_quitNotifyAt);
}

static void Sys_AutoQuitFrame()
{
    int elapsed = Sys_Milliseconds() - s_autoquitStartMs;
    Sys_HeadlessTimelineFrame(elapsed);
    Sys_HeadlessClientCommandsFrame(elapsed);
    Sys_HeadlessShotsFrame(elapsed);
    Sys_HeadlessMenuSweepFrame();
    if (s_quitNotifyAt >= 0 && com_sv_running->current.enabled && level.time >= s_quitNotifyAt)
    {
        Com_Printf(16, "bo1_quitnotify: %d ms after init; level.time %d, %d '%s' notifies. Quitting.\n", elapsed,
            level.time, s_quitNotifySeen, bo1_quitnotify->current.string);
        s_quitNotifyAt = -1;
        Com_Quit_f();
        return;
    }
    if (!bo1_autoquit || bo1_autoquit->current.integer <= 0)
        return;
    if (elapsed < bo1_autoquit->current.integer)
        return;
    Com_Printf(16, "bo1_autoquit: %d ms after init; sv_running %d, mapname '%s', level.time %d. Quitting.\n",
        elapsed,
        com_sv_running->current.enabled,
        Dvar_GetString("mapname"),
        com_sv_running->current.enabled ? level.time : 0);
    if (bo1_dvardump && bo1_dvardump->current.enabled)
        Dvar_ForEach(Sys_DvarDumpOne, 0);
    Com_Quit_f();
}

char sys_exitCmdLine[1024];
sysEvent_t eventQue[256];
int eventHead;
int eventTail;
char sys_processSemaphoreFile[260];

bool __cdecl PC_StartWithNoSounds()
{
    if ( G_ExitAfterToolComplete() )
        s_nosnd = 1;
    if ( !SD_Xaudio2CanInit() )
        s_nosnd = 1;
    return s_nosnd != 0;
}

void __cdecl Sys_GetInfo(SysInfo *info)
{
    memcpy(info, &sys_info, sizeof(SysInfo));
}

bool __cdecl Sys_HasConfigureChecksumChanged(int checksum)
{
    bool changed; // [esp+3h] [ebp-1h]

    Sys_RegisterInfoDvars();
    if ( G_OnlyConnectingPaths() )
        return 0;
    changed = 0;
    if ( sys_configSum->current.integer && sys_configSum->current.integer != checksum )
        changed = Sys_ShouldUpdateForConfigChange();
    if ( !sys_configSum->current.integer || sys_configSum->current.integer != checksum )
        Dvar_SetInt((dvar_s *)sys_configSum, checksum);
    return changed;
}

bool __cdecl Sys_ShouldUpdateForConfigChange()
{
    HWND ActiveWindow; // eax
    char *v2; // [esp-Ch] [ebp-Ch]
    char *v3; // [esp-8h] [ebp-8h]

    if ( Sys_IsHeadless() ) // zombies: headless never prompts
        return 0;
    v3 = Win_LocalizeRef("WIN_CONFIGURE_UPDATED_TITLE");
    v2 = Win_LocalizeRef("WIN_CONFIGURE_UPDATED_BODY");
    ActiveWindow = GetActiveWindow();
    return MessageBoxA(ActiveWindow, v2, v3, 0x44u) == 6;
}

void Sys_RegisterInfoDvars()
{
    float value; // xmm0_4

    sys_configureGHz = _Dvar_RegisterFloat(
                                             "sys_configureGHz",
                                             0.0,
                                             -3.4028235e38,
                                             3.4028235e38,
                                             0x11u,
                                             "Normalized total CPU power, based on cpu type, count, and speed; used in autoconfigure");
    sys_sysMB = _Dvar_RegisterInt("sys_sysMB", 0, 0x80000000, 0x7FFFFFFF, 0x11u, "Physical memory in the system");
    sys_gpu = _Dvar_RegisterString("sys_gpu", (char *)"", 0x11u, "GPU description");
    sys_configSum = _Dvar_RegisterInt("sys_configSum", 0, 0x80000000, 0x7FFFFFFF, 0x11u, "Configuration checksum");
    sys_SSE = _Dvar_RegisterBool("sys_SSE", sys_info.SSE, 0x40u, "Operating system allows Streaming SIMD Extensions");
    value = sys_info.cpuGHz;
    _Dvar_RegisterFloat("sys_cpuGHz", value, -3.4028235e38, 3.4028235e38, 0x40u, "Measured CPU speed");
    _Dvar_RegisterString("sys_cpuName", sys_info.cpuName, 0x40u, "CPU name description");
}

bool __cdecl Sys_HasInfoChanged()
{
    Sys_RegisterInfoDvars();
    return (sys_configureGHz->current.value > sys_info.configureGHz * 1.100000023841858
             || sys_info.configureGHz * 0.8999999761581421 > sys_configureGHz->current.value
             || sys_sysMB->current.integer > sys_info.sysMB + 32
             || sys_sysMB->current.integer < sys_info.sysMB - 32
             || strcmp(sys_gpu->current.string, sys_info.gpuDescription))
            && Sys_ShouldUpdateForInfoChange();
}

bool __cdecl Sys_ShouldUpdateForInfoChange()
{
    HWND ActiveWindow; // eax
    char *v2; // [esp-Ch] [ebp-Ch]
    char *v3; // [esp-8h] [ebp-8h]

    Sys_ArchiveInfo(0);
    if ( G_OnlyConnectingPaths() || Sys_IsHeadless() ) // zombies: headless never prompts
        return 0;
    v3 = Win_LocalizeRef("WIN_COMPUTER_CHANGE_TITLE");
    v2 = Win_LocalizeRef("WIN_COMPUTER_CHANGE_BODY");
    ActiveWindow = GetActiveWindow();
    return MessageBoxA(ActiveWindow, v2, v3, 0x44u) == 6;
}

void __cdecl Sys_ArchiveInfo(int checksum)
{
    float value; // xmm0_4

    Sys_RegisterInfoDvars();
    value = sys_info.configureGHz;
    Dvar_SetFloat((dvar_s *)sys_configureGHz, value);
    Dvar_SetInt((dvar_s *)sys_sysMB, sys_info.sysMB);
    Dvar_SetString((dvar_s *)sys_gpu, sys_info.gpuDescription);
    Dvar_SetInt((dvar_s *)sys_configSum, checksum);
}

void __cdecl    Sys_DirectXFatalError()
{
    HWND ActiveWindow; // eax
    char *v1; // [esp-Ch] [ebp-Ch]
    char *v2; // [esp-8h] [ebp-8h]

    if ( Monkey_IsRunning() )
    {
        Monkey_Error("Sys_DirectXFatalError");
        exit(-1);
    }
    if ( Sys_IsHeadless() ) // zombies: no message box, no ShellExecute
        Sys_HeadlessDie(3, "Sys_DirectXFatalError");
    Sys_EnterCriticalSection(CRITSECT_FATAL_ERROR);
    v2 = Win_LocalizeRef("WIN_DIRECTX_INIT_TITLE");
    v1 = Win_LocalizeRef("WIN_DIRECTX_INIT_BODY");
    ActiveWindow = GetActiveWindow();
    MessageBoxA(ActiveWindow, v1, v2, 0x10u);
    ShellExecuteA(0, "open", "Docs\\TechHelp\\Tech Help\\Information\\DirectX.htm", 0, 0, 3);
    //BLOPS_NULLSUB();
    exit(-1);
}

void __cdecl    Sys_OutOfMemErrorInternal(const char *filename, int line)
{
    const char *v2; // eax
    HWND ActiveWindow; // eax
    char *v4; // [esp-Ch] [ebp-Ch]
    char *v5; // [esp-8h] [ebp-8h]

    if ( Monkey_IsRunning() )
    {
        v2 = va("Sys_OutOfMemErrorInternal(%s, %d)", filename, line);
        Monkey_Error(v2);
        exit(-1);
    }
    if ( Sys_IsHeadless() ) // zombies: no message box
        Sys_HeadlessDie(3, va("Out of memory: filename '%s', line %d", filename, line));
    Sys_EnterCriticalSection(CRITSECT_FATAL_ERROR);
    Com_Printf(16, "Out of memory: filename '%s', line %d\n", filename, line);
    v5 = Win_LocalizeRef("WIN_OUT_OF_MEM_TITLE");
    v4 = Win_LocalizeRef("WIN_OUT_OF_MEM_BODY");
    ActiveWindow = GetActiveWindow();
    MessageBoxA(ActiveWindow, v4, v5, 0x10u);
    //BLOPS_NULLSUB();
    exit(-1);
}

void __cdecl Sys_QuitAndStartProcess(const char *exeName)
{
    I_strncpyz(sys_exitCmdLine, exeName, 1024);
    Cbuf_AddText(0, "quit\n");
}

void __cdecl Sys_OpenURL(const char *url, int doexit)
{
#if 0 // BO1TODO: re-enable this later when it's rewritten
    const char *v2; // eax
    HWND__ *wnd; // [esp+0h] [ebp-4h]

    if ( !ShellExecuteA(0, "open", url, 0, 0, 9) )
    {
        v2 = va("EXE_ERR_COULDNT_OPEN_URL %s", url);
        Com_Error(ERR_DROP, v2);
    }
    wnd = GetForegroundWindow();
    if ( wnd )
        ShowWindow(wnd, 3);
    if ( doexit )
        Cbuf_AddText(0, "quit\n");
#endif
}

void    Sys_Error(char *error, ...)
{
    char string[4100]; // [esp+20h] [ebp-1008h] BYREF
    va_list va; // [esp+1034h] [ebp+Ch] BYREF
    MSG Msg;

    va_start(va, error);
    Sys_EnterCriticalSection(CRITSECT_COM_ERROR);
    Com_PrintStackTrace();
    com_errorEntered = 1;
    Sys_SuspendOtherThreads();
    _vsnprintf(string, 0x1000u, error, va);

    // zombies: headless ends here - no error console, no message box, and no FixWindowsDesktop (it resets the
    // display mode and the desktop gamma ramp)
    // x6c38: a normal launch keeps the text in main\bo1_emergency.log too, then goes on to its error box
    Sys_HeadlessEmergencyLog("BO1_HEADLESS Sys_Error: %s\n", string);
    if ( Sys_IsHeadless() )
        TerminateProcess(GetCurrentProcess(), 2);

    if ( Monkey_IsRunning() )
    {
        Monkey_Error(string);
        exit(0);
    }

    FixWindowsDesktop();

    if (IsDedicatedServer())
    {
        Sys_SetErrorText(string);
    }
    else
    {
        if (Sys_IsMainThread())
        {
            Sys_ShowConsole();
            Conbuf_AppendText((char*)"\n\n");
            Conbuf_AppendText(string);
            Conbuf_AppendText((char *)"\n");
        }

        Sys_SetErrorText(string);

        // wait for the user to quit
        while (GetMessage(&Msg, 0, 0, 0))
        {
            TranslateMessage(&Msg);
            DispatchMessage(&Msg);
        }
    }
    //BLOPS_NULLSUB();
    exit(0);
}

void __cdecl    Sys_Quit()
{
    Sys_EnterCriticalSection(CRITSECT_COM_ERROR);
    timeEndPeriod(1u);
    Sys_SpawnQuitProcess();
    LiveSteam_Shutdown();
    CL_ShutdownAll();
    IN_Shutdown();
    Key_Shutdown();
    Sys_DestroyConsole();
    Sys_NormalExit();
    Win_ShutdownLocalization();
    RefreshQuitOnErrorCondition();
    Dvar_Shutdown();
    Cmd_Shutdown();
    //BLOPS_NULLSUB();
    //BLOPS_NULLSUB();
    Sys_ShutdownEvents();
    SL_Shutdown(SCRIPTINSTANCE_SERVER);
    SL_Shutdown(SCRIPTINSTANCE_CLIENT);
    if ( !com_errorEntered )
        track_shutdown(0);
    Con_ShutdownChannels();
    exit(0);
}

void Sys_SpawnQuitProcess()
{
    char *v0; // eax
    void *v1; // [esp-8h] [ebp-14h]
    unsigned int v2; // [esp-4h] [ebp-10h]
    void *msgBuf; // [esp+0h] [ebp-Ch] BYREF
    unsigned int error; // [esp+4h] [ebp-8h]

    if ( sys_exitCmdLine[0] && !Sys_IsHeadless() ) // zombies: headless never launches another process
    {
        if ( !LiveSteam_LaunchOtherApp(sys_exitCmdLine) )
        {
            error = GetLastError();
            FormatMessageA(0x1300u, 0, error, 0x400u, (LPSTR)&msgBuf, 0, 0);
            v2 = error;
            v1 = msgBuf;
            v0 = Win_LocalizeRef("WIN_COULDNT_START_PROCESS");
            Com_Error(ERR_FATAL, "%s %s %s(0x%08x)", v0, sys_exitCmdLine, v1, v2);
        }
    }
}

int enable_OutputDebugString = 1;

void __cdecl Sys_Print(char *msg)
{
    if ( enable_OutputDebugString )
        OutputDebugStringA(msg);
    Conbuf_AppendTextInMainThread(msg);
}

char *__cdecl Sys_GetClipboardData()
{
    SIZE_T v0; // eax
    SIZE_T v1; // eax
    HANDLE hClipboardData; // [esp+0h] [ebp-Ch]
    char *data; // [esp+4h] [ebp-8h]
    char *cliptext; // [esp+8h] [ebp-4h]

    data = 0;
    if ( OpenClipboard(0) )
    {
        hClipboardData = GetClipboardData(1u);
        if ( hClipboardData )
        {
            cliptext = (char *)GlobalLock(hClipboardData);
            if ( cliptext )
            {
                v0 = GlobalSize(hClipboardData);
                data = (char *)Z_Malloc(v0 + 1, "Sys_GetClipboardData", 11);
                v1 = GlobalSize(hClipboardData);
                I_strncpyz(data, cliptext, v1);
                GlobalUnlock(hClipboardData);
                strtok(data, "\n\r\b");
            }
        }
        CloseClipboard();
    }
    return data;
}

void __cdecl Sys_QueEvent(unsigned int time, sysEventType_t type, int value, int value2, int ptrLength, void *ptr)
{
    sysEvent_t *ev; // [esp+0h] [ebp-4h]

    Sys_EnterCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
    ev = &eventQue[(unsigned __int8)eventHead];
    if ( eventHead - eventTail >= 256 )
    {
        Com_Printf(16, "Sys_QueEvent: overflow\n");
        if ( ev->evPtr )
            Z_Free((char *)ev->evPtr, 11);
        ++eventTail;
    }
    ++eventHead;
    if ( !time )
        time = Sys_Milliseconds();
    ev->evTime = time;
    ev->evType = type;
    ev->evValue = value;
    ev->evValue2 = value2;
    ev->evPtrLength = ptrLength;
    ev->evPtr = ptr;
    Sys_LeaveCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
}

void Sys_ShutdownEvents()
{
    sysEvent_t *ev; // [esp+0h] [ebp-4h]

    Sys_EnterCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
    while ( eventHead > eventTail )
    {
        ev = &eventQue[(unsigned __int8)eventTail++];
        if ( ev->evPtr )
            Z_Free((char *)ev->evPtr, 11);
    }
    Sys_LeaveCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
}

void __cdecl Sys_LoadingKeepAlive()
{
    sysEvent_t result; // [esp+0h] [ebp-48h] BYREF
    sysEvent_t v1; // [esp+18h] [ebp-30h]
    sysEvent_t ev; // [esp+30h] [ebp-18h]

    Monkey_KeepAlive();
    do
    {
        v1 = *Win_GetEvent(&result);
        ev = v1;
    }
    while ( v1.evType );
}

sysEvent_t *__cdecl Win_GetEvent(sysEvent_t *result)
{
    int v2; // [esp+0h] [ebp-50h]
    char *b; // [esp+10h] [ebp-40h]
    tagMSG msg; // [esp+18h] [ebp-38h] BYREF
    char *s; // [esp+34h] [ebp-1Ch]
    sysEvent_t ev; // [esp+38h] [ebp-18h] BYREF

    Sys_EnterCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
    if ( eventHead <= eventTail )
    {
        if ( Sys_QueryWin32QuitEvent() )
        {
            // L5 (BO1Zombies's own; SP 0x00867C00 quits holding it too): Com_Quit_f does not return and waits for the
            // render thread, whose window procedure queues input events under this lock - leave it first
            Sys_LeaveCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
            Com_Quit_f();
        }

        if (IsDedicatedServer())
        {
            while (PeekMessageA(&msg, 0, 0, 0, 0))
            {
                if (!GetMessageA(&msg, 0, 0, 0))
                    Com_Quit_f();
                g_msgTime = msg.time;
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }
        }
        
        s = Sys_ConsoleInput();
        if ( s )
        {
            v2 = strlen(s);
            b = (char *)Com_AllocEvent(v2 + 1);
            I_strncpyz(b, s, v2);
            Sys_QueEvent(0, SE_CONSOLE, 0, 0, v2 + 1, b);
        }
        if ( eventHead <= eventTail )
        {
            memset(&ev, 0, sizeof(ev));
            ev.evTime = Sys_Milliseconds();
        }
        else
        {
            ev = eventQue[(unsigned __int8)eventTail++];
        }
        Sys_LeaveCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
        *result = ev;
        return result;
    }
    else
    {
        ev = eventQue[(unsigned __int8)eventTail++];
        Sys_LeaveCriticalSection(CRITSECT_SYS_EVENT_QUEUE);
        *result = ev;
        return result;
    }
}

sysEvent_t *__cdecl Sys_GetEvent(sysEvent_t *result)
{
    sysEvent_t v2; // [esp+0h] [ebp-30h] BYREF
    sysEvent_t v3; // [esp+18h] [ebp-18h]

    v3 = *Win_GetEvent(&v2);
    *result = v3;
    return result;
}

void __cdecl Sys_Mjpeg()
{
    const char *v0; // eax
    unsigned int v1; // [esp-Ch] [ebp-38h]
    int v2; // [esp-8h] [ebp-34h]
    unsigned int unsignedInt; // [esp-4h] [ebp-30h]
    int heights[4]; // [esp+1Ch] [ebp-10h]

    heights[0] = 360;
    heights[1] = 480;
    heights[2] = 720;
    heights[3] = 1080;
    if ( Cmd_Argc() == 2 )
    {
        mjpeg_init();
        unsignedInt = r_clipFPS->current.unsignedInt;
        v2 = heights[r_clipSize->current.integer];
        v1 = (vidConfig.displayWidth * v2 / vidConfig.displayHeight) & 0xFFF0;
        v0 = Cmd_Argv(1);
        mjpeg_create(v0, v1, v2, unsignedInt);
    }
}

void __cdecl Sys_MjpegClose()
{
    mjpeg_close();
}

cmd_function_s Sys_In_Restart_f_VAR;
cmd_function_s Sys_Net_Restart_f_VAR;
cmd_function_s Sys_Mjpeg_VAR;
cmd_function_s Sys_MjpegClose_VAR;
cmd_function_s Sys_Listen_f_VAR;
cmd_function_s Sys_Connect_f_VAR;

// zombies: headless evidence - drive the menus through the game's own UI entry points, never OS input:
// 'bo1_uikey <keynum>' sends a key down + up through UI_KeyEvent (what a key press reaches the menus by), and
// 'bo1_menuaction <menu> <item index>' runs that item's "action" event script (Item_Action, a click on it).
// Registered only for the headless client; use them from bo1_clientCommands.
static void Sys_HeadlessUiKey_f()
{
    if (Cmd_Argc() < 2)
        return;
    const int key = atoi(Cmd_Argv(1));
    Com_Printf(16, "bo1_uikey: %d, top menu '%s'\n", key, UI_GetTopActiveMenuName(0) ? UI_GetTopActiveMenuName(0) : "");
    UI_KeyEvent(0, key, 1);
    UI_KeyEvent(0, key, 0);
}

static void Sys_HeadlessMenuAction_f()
{
    if (Cmd_Argc() < 3)
        return;
    UiContext *dc = &UI_GetInfo(0)->uiDC;
    menuDef_t *menu = Menus_FindByName(dc, Cmd_Argv(1));
    const int index = atoi(Cmd_Argv(2));
    if (!menu || index < 0 || index >= menu->itemCount)
    {
        Com_Printf(16, "bo1_menuaction: no item %d in menu '%s'\n", index, Cmd_Argv(1));
        return;
    }
    Com_Printf(16, "bo1_menuaction: %s item %d\n", Cmd_Argv(1), index);
    Item_Action(0, dc, menu->items[index]);
}

// 'bo1_itemkey <menu> <item index> <keynum>' hands one key press to that item's own key handler (Item_HandleKey:
// a multi / slider / yes-no item changes its dvar the way the focused item does for that key), then logs the dvar.
static void Sys_HeadlessItemKey_f()
{
    if (Cmd_Argc() < 4)
        return;
    UiContext *dc = &UI_GetInfo(0)->uiDC;
    menuDef_t *menu = Menus_FindByName(dc, Cmd_Argv(1));
    const int index = atoi(Cmd_Argv(2));
    const int key = atoi(Cmd_Argv(3));
    if (!menu || index < 0 || index >= menu->itemCount)
    {
        Com_Printf(16, "bo1_itemkey: no item %d in menu '%s'\n", index, Cmd_Argv(1));
        return;
    }
    itemDef_s *item = menu->items[index];
    const char *before = item->dvar ? Dvar_GetVariantString(item->dvar) : "";
    char beforeCopy[256];
    I_strncpyz(beforeCopy, before, sizeof(beforeCopy));
    // The item handlers act only on the focused item (Item_ShouldHandleKey): focus it first, as moving the
    // cursor onto it does.
    const int focused = Item_SetFocus(0, dc, item, item->window.rect.x + item->window.rect.w * 0.5f,
        item->window.rect.y + item->window.rect.h * 0.5f);
    Com_Printf(16, "bo1_itemkey: focus %d\n", focused);
    const int handled = Item_HandleKey(0, dc, item, key, 1);
    Item_HandleKey(0, dc, item, key, 0);
    Com_Printf(16, "bo1_itemkey: %s item %d key %d handled %d dvar '%s' '%s' -> '%s'\n", Cmd_Argv(1), index, key,
        handled, item->dvar ? item->dvar : "", beforeCopy, item->dvar ? Dvar_GetVariantString(item->dvar) : "");
}

// zombies mouse-1: headless mouse through the paths real input takes.
//   'bo1_uimouse move <x> <y>' - cursor at 640x480 virtual (x, y): the client pixel IN_MouseMove would read is handed
//       to CL_MouseEvent (IN_MouseMove itself reads the OS cursor, which a headless run never touches);
//   'bo1_uimouse click' / 'wheelup' / 'wheeldown' - posts WM_LBUTTONDOWN+UP / WM_MOUSEWHEEL to the game window, so
//       MainWndProc -> IN_MouseEvent / Sys_QueEvent -> CL_KeyEvent -> UI_KeyEvent run as for a real click / wheel;
//   'bo1_uimouse look <dx> <dy>' - a relative motion (what turns the view when CL_MouseEvent does not give it to the UI);
//   'bo1_uimouse key <keynum>' - a key down + up queued like MainWndProc queues a key (Sys_QueEvent -> CL_KeyEvent);
//   'bo1_uimouse state' - prints the cursor and the focused menu / item.
static void Sys_HeadlessUiMouseState(const char *what)
{
    UiContext *dc = &UI_GetInfo(0)->uiDC;
    menuDef_t *menu = Menu_GetFocused(dc);
    const int index = menu ? menu->cursorItem[0] : -1;
    const itemDef_s *item = menu && index >= 0 && index < menu->itemCount ? menu->items[index] : nullptr;
    Com_Printf(16, "bo1_uimouse: %s cursor %.1f %.1f visible %d menu '%s' item %d '%s' '%s' catchers 0x%x\n", what,
        dc->cursor.x, dc->cursor.y, dc->isCursorVisible, menu && menu->window.name ? menu->window.name : "", index,
        item && item->window.name ? item->window.name : "", item && (item->type == 0 || item->type == 1) && item->typeData.textDef && item->typeData.textDef->text ? item->typeData.textDef->text : "",
        CL_GetLocalClientUIGlobals(0)->keyCatchers);
}

static void Sys_HeadlessUiMouse_f()
{
    static int s_lastX = -1, s_lastY = -1; // -1: no move yet (the first move carries no delta)
    if (Cmd_Argc() < 2)
        return;
    const char *op = Cmd_Argv(1);
    float vx = 0.0f, vy = 0.0f;
    bool move = false;
    if (!strcmp(op, "move") && Cmd_Argc() >= 4)
    {
        vx = atof(Cmd_Argv(2));
        vy = atof(Cmd_Argv(3));
        move = true;
    }
    else if (!strcmp(op, "moveitem") && Cmd_Argc() >= 4) // 'moveitem <menu> <index>': onto the centre of that item
    {
        menuDef_t *menu = Menus_FindByName(&UI_GetInfo(0)->uiDC, Cmd_Argv(2));
        const int index = atoi(Cmd_Argv(3));
        if (!menu || index < 0 || index >= menu->itemCount)
        {
            Com_Printf(16, "bo1_uimouse: no item %d in menu '%s'\n", index, Cmd_Argv(2));
            return;
        }
        // the item's rect as Rect_ContainsPoint places it, then back through the cursor's own placement (align 4)
        const rectDef_s &r = menu->items[index]->window.rect;
        rectDef_s placed = r;
        const ScreenPlacement *scrPlace = &scrPlaceView[UI_GetInfo(0)->uiDC.contextIndex];
        ScrPlace_ApplyRect(scrPlace, &placed.x, &placed.y, &placed.w, &placed.h, r.horzAlign, r.vertAlign);
        const float x0 = ScrPlace_ApplyX(scrPlace, 0.0f, 4), y0 = ScrPlace_ApplyY(scrPlace, 0.0f, 4);
        vx = (placed.x + placed.w * 0.5f - x0) / (ScrPlace_ApplyX(scrPlace, 1.0f, 4) - x0);
        vy = (placed.y + placed.h * 0.5f - y0) / (ScrPlace_ApplyY(scrPlace, 1.0f, 4) - y0);
        move = true;
    }
    if (move)
    {
        const float scale = ScrPlace_HiResGetScale();
        const int x = (int)(vx * scale * scrPlaceFull.scaleVirtualToFull[0] + 0.5f);
        const int y = (int)(vy * scale * scrPlaceFull.scaleVirtualToFull[1] + 0.5f);
        const int recenter = CL_MouseEvent(x, y, s_lastX < 0 ? 0 : x - s_lastX, s_lastY < 0 ? 0 : y - s_lastY);
        s_lastX = x;
        s_lastY = y;
        Com_Printf(16, "bo1_uimouse: move to client %d %d, CL_MouseEvent %d\n", x, y, recenter);
    }
    else if (!strcmp(op, "look") && Cmd_Argc() >= 4) // 'look <dx> <dy>': a relative mouse motion at the last position
    {
        const int recenter = CL_MouseEvent(s_lastX < 0 ? 0 : s_lastX, s_lastY < 0 ? 0 : s_lastY, atoi(Cmd_Argv(2)), atoi(Cmd_Argv(3)));
        Com_Printf(16, "bo1_uimouse: look %s %s, CL_MouseEvent %d\n", Cmd_Argv(2), Cmd_Argv(3), recenter);
    }
    else if (!strcmp(op, "click"))
    {
        PostMessageA(g_wv.hWnd, WM_LBUTTONDOWN, MK_LBUTTON, 0);
        PostMessageA(g_wv.hWnd, WM_LBUTTONUP, 0, 0);
    }
    else if (!strcmp(op, "wheelup") || !strcmp(op, "wheeldown"))
    {
        PostMessageA(g_wv.hWnd, WM_MOUSEWHEEL, (WPARAM)((unsigned int)(unsigned short)(short)(!strcmp(op, "wheelup") ? 120 : -120) << 16), 0);
    }
    else if (!strcmp(op, "key") && Cmd_Argc() >= 3) // 'key <keynum>': a key down + up through the event queue (CL_KeyEvent)
    {
        Sys_QueEvent(Sys_Milliseconds(), SE_KEY, atoi(Cmd_Argv(2)), 1, 0, 0);
        Sys_QueEvent(Sys_Milliseconds(), SE_KEY, atoi(Cmd_Argv(2)), 0, 0, 0);
    }
    else if (!strcmp(op, "stack")) // the open menus Display_MouseMove walks, and the focused menu's items under the cursor
    {
        UiContext *dc = &UI_GetInfo(0)->uiDC;
        for (int i = dc->openMenuCount - 1; i >= 0; --i)
        {
            menuDef_t *m = dc->menuStack[i].menu;
            Com_Printf(16, "bo1_uimouse: stack %d '%s' fullScreen %d 3d %d dyn 0x%x static 0x%x animating %d\n", i,
                m->window.name ? m->window.name : "", m->fullScreen, m->ui3dWindowId,
                Window_GetDynamicFlags(dc->contextIndex, &m->window), m->window.staticFlags,
                Menu_ItemsAreAnimating(0, dc, m));
        }
        if (menuDef_t *m = Menu_GetFocused(dc))
        {
            for (int i = 0; i < m->itemCount; ++i)
            {
                if (Rect_ContainsPoint(dc->contextIndex, Window_GetRect(&m->items[i]->window), dc->cursor.x, dc->cursor.y))
                    Com_Printf(16, "bo1_uimouse: under cursor item %d '%s' type %d dyn 0x%x visible %d\n", i,
                        m->items[i]->window.name ? m->items[i]->window.name : "", m->items[i]->type,
                        Window_GetDynamicFlags(dc->contextIndex, &m->items[i]->window),
                        Item_IsVisible(0, dc->contextIndex, m->items[i]));
            }
        }
    }
    Sys_HeadlessUiMouseState(op);
}

// zombies L5: 'bo1_wmclose' posts WM_CLOSE to the game window (what the window's close button, Alt+F4 or the taskbar's
// "Close window" send), so a headless client can test closing through MainWndProc. The window is on the private desktop.
static void Sys_HeadlessWmClose_f()
{
    Com_Printf(16, "bo1_wmclose: posting WM_CLOSE to the game window %p\n", g_wv.hWnd);
    if (g_wv.hWnd)
        PostMessageA(g_wv.hWnd, WM_CLOSE, 0, 0);
}

static cmd_function_s Sys_HeadlessWmClose_f_VAR;
static cmd_function_s Sys_HeadlessUiMouse_f_VAR;
static cmd_function_s Sys_HeadlessUiKey_f_VAR;
static cmd_function_s Sys_HeadlessMenuAction_f_VAR;
static cmd_function_s Sys_HeadlessItemKey_f_VAR;
static cmd_function_s Sys_HeadlessMenuSweep_f_VAR;

void __cdecl Sys_Init()
{
    const char *BuildDisplayNameR; // eax
    char *v1; // eax
    const char *v2; // eax
    char *v3; // eax
    _OSVERSIONINFOA osversion; // [esp+14h] [ebp-A0h] BYREF

    timeBeginPeriod(1u);
    bo1_autoquit = _Dvar_RegisterInt("bo1_autoquit", 0, 0, 0x7FFFFFFF, 0, "zombies: quit this many ms after init (0 = off); for headless test runs");
    bo1_quitnotify = _Dvar_RegisterString("bo1_quitnotify", "", 0, "L20: headless - quit <level ms> of level time after level notify <name> fired <count> times (\"start_of_round,3,20000\"; empty = off)");
    bo1_dvardump = _Dvar_RegisterBool("bo1_dvardump", false, 0, "zombies: print every dvar (type, flags, reset, current) when bo1_autoquit fires");
    bo1_menudump = _Dvar_RegisterString("bo1_menudump", "", 0, "zombies: headless client - menus whose scripts bo1_shots prints");
    bo1_shots = _Dvar_RegisterString("bo1_shots", "", 0, "zombies: headless client - back buffer screenshots at these ms after init (\"20000 40000\")");
    bo1_menusweep_stepms = _Dvar_RegisterInt("bo1_menusweep_stepms", 150, 1, 60000, 0, "x6c35: headless client - real ms between bo1_menusweep steps");
    bo1_menusweep_shotms = _Dvar_RegisterInt("bo1_menusweep_shotms", 0, 0, 60000, 0, "x6c35: headless client - ms each bo1_menusweep entry is open before its screenshot (0 = none)");
    bo1_clientCommands = _Dvar_RegisterString("bo1_clientCommands", "", 0, "Headless client commands at real ms after CA_ACTIVE (1000:+scores;2000:screenshotJpeg scores)");
    if (Sys_IsHeadlessClient())
    {
        Cmd_AddCommandInternal("bo1_uikey", Sys_HeadlessUiKey_f, &Sys_HeadlessUiKey_f_VAR);
        Cmd_AddCommandInternal("bo1_menuaction", Sys_HeadlessMenuAction_f, &Sys_HeadlessMenuAction_f_VAR);
        Cmd_AddCommandInternal("bo1_itemkey", Sys_HeadlessItemKey_f, &Sys_HeadlessItemKey_f_VAR);
        Cmd_AddCommandInternal("bo1_menusweep", Sys_HeadlessMenuSweep_f, &Sys_HeadlessMenuSweep_f_VAR);
        Cmd_AddCommandInternal("bo1_wmclose", Sys_HeadlessWmClose_f, &Sys_HeadlessWmClose_f_VAR);
        Cmd_AddCommandInternal("bo1_uimouse", Sys_HeadlessUiMouse_f, &Sys_HeadlessUiMouse_f_VAR);
    }
    Cmd_AddCommandInternal("in_restart", Sys_In_Restart_f, &Sys_In_Restart_f_VAR);
    Cmd_AddCommandInternal("net_restart", Sys_Net_Restart_f, &Sys_Net_Restart_f_VAR);
    Cmd_AddCommandInternal("movie_start", Sys_Mjpeg, &Sys_Mjpeg_VAR);
    Cmd_AddCommandInternal("movie_stop", Sys_MjpegClose, &Sys_MjpegClose_VAR);
    Cmd_AddCommandInternal("net_listen", Sys_Listen_f, &Sys_Listen_f_VAR);
    Cmd_AddCommandInternal("net_connect", Sys_Connect_f, &Sys_Connect_f_VAR);
    osversion.dwOSVersionInfoSize = 148;
#pragma warning(push)
#pragma warning(disable : 4996) // cannot call GetVersionExA nowadays without this warning suppression
    if ( !GetVersionExA(&osversion) )
        Sys_Error((char*)"Couldn't get OS info");
#pragma warning(pop)
    if ( osversion.dwMajorVersion < 4 )
    {
        BuildDisplayNameR = Com_GetBuildDisplayNameR();
        v1 = va("%s requires Windows version 4 or greater", BuildDisplayNameR);
        Sys_Error(v1);
    }
    if ( !osversion.dwPlatformId )
    {
        v2 = Com_GetBuildDisplayNameR();
        v3 = va("%s doesn't run on Win32s", v2);
        Sys_Error(v3);
    }
    Com_Printf(16, "CPU vendor is \"%s\"\n", sys_info.cpuVendor);
    Com_Printf(16, "CPU name is \"%s\"\n", sys_info.cpuName);
    if ( sys_info.logicalCpuCount == 1 )
        Com_Printf(16, "%i logical CPU%s reported\n", 1, "");
    else
        Com_Printf(16, "%i logical CPU%s reported\n", sys_info.logicalCpuCount, "s");
    if ( sys_info.physicalCpuCount == 1 )
        Com_Printf(16, "%i physical CPU%s detected\n", 1, "");
    else
        Com_Printf(16, "%i physical CPU%s detected\n", sys_info.physicalCpuCount, "s");
    Com_Printf(16, "Measured CPU speed is %.2lf GHz\n", sys_info.cpuGHz);
    Com_Printf(16, "Total CPU performance is estimated as %.2lf GHz\n", sys_info.configureGHz);
    Com_Printf(16, "System memory is %i MB (capped at 1 GB)\n", sys_info.sysMB);
    Com_Printf(16, "Video card is \"%s\"\n", sys_info.gpuDescription);
    if ( sys_info.SSE )
        Com_Printf(16, "Streaming SIMD Extensions (SSE) %ssupported\n", "");
    else
        Com_Printf(16, "Streaming SIMD Extensions (SSE) %ssupported\n", "not ");
    Com_Printf(16, "\n");
    IN_Init();
}

void __cdecl Sys_In_Restart_f()
{
    IN_Shutdown();
    IN_Init();
}

void __cdecl Sys_Net_Restart_f()
{
    NET_Restart();
}

int __cdecl Sys_CheckCrashOrRerun()
{
#ifdef BO1_PURE
    HWND ActiveWindow; // eax
    char *v2; // [esp-Ch] [ebp-20h]
    char *v3; // [esp-8h] [ebp-1Ch]
    unsigned int procID; // [esp+0h] [ebp-14h] BYREF
    int answer; // [esp+4h] [ebp-10h]
    DWORD byteCount; // [esp+8h] [ebp-Ch] BYREF
    void *file; // [esp+Ch] [ebp-8h]
    unsigned int id; // [esp+10h] [ebp-4h] BYREF

    if ( !sys_processSemaphoreFile[0] )
        return 1;
    procID = GetCurrentProcessId();
    file = CreateFileA(sys_processSemaphoreFile, 0x80000000, 0, 0, 3u, 2u, 0);
    if ( file != (void *)-1 )
    {
        if ( ReadFile(file, &id, 4u, &byteCount, 0) && byteCount == 4 )
        {
            CloseHandle(file);
            if ( procID != id && Sys_IsGameProcess(id) )
                return 0;
            v3 = Win_LocalizeRef("WIN_IMPROPER_QUIT_TITLE");
            v2 = Win_LocalizeRef("WIN_IMPROPER_QUIT_BODY");
            ActiveWindow = GetActiveWindow();
            answer = MessageBoxA(ActiveWindow, v2, v3, 0x33u);
            if ( answer == 6 )
            {
                Com_ForceSafeMode();
            }
            else if ( answer == 2 )
            {
                return 0;
            }
        }
        else
        {
            CloseHandle(file);
        }
    }
    file = CreateFileA(sys_processSemaphoreFile, 0x40000000u, 0, 0, 2u, 2u, 0);
    if ( file == (void *)-1 )
        Sys_NoFreeFilesError();
    if ( !WriteFile(file, &procID, 4u, &byteCount, 0) || byteCount != 4 )
    {
        CloseHandle(file);
        Sys_NoFreeFilesError();
    }
    CloseHandle(file);
    return 1;
#else
    return 1; // disable "do you wanna restart in safe mode?" prompt
#endif
}

void    Sys_NoFreeFilesError()
{
    HWND ActiveWindow; // eax
    char *v1; // [esp-Ch] [ebp-Ch]
    char *v2; // [esp-8h] [ebp-8h]

    if ( Monkey_IsRunning() )
    {
        Monkey_Error("Sys_NoFreeFilesError");
        exit(-1);
    }
    if ( Sys_IsHeadless() ) // zombies: no message box
        Sys_HeadlessDie(3, "Sys_NoFreeFilesError");
    Sys_EnterCriticalSection(CRITSECT_FATAL_ERROR);
    v2 = Win_LocalizeRef("WIN_DISK_FULL_TITLE");
    v1 = Win_LocalizeRef("WIN_DISK_FULL_BODY");
    ActiveWindow = GetActiveWindow();
    MessageBoxA(ActiveWindow, v1, v2, 0x10u);
    //BLOPS_NULLSUB();
    exit(-1);
}

int __cdecl Sys_IsGameProcess(unsigned int id)
{
    //tagMODULEENTRY32 me; // [esp+0h] [ebp-350h] BYREF
    MODULEENTRY32 me; // [esp+0h] [ebp-350h] BYREF
    int isGame; // [esp+22Ch] [ebp-124h]
    char *i; // [esp+230h] [ebp-120h]
    char *moduleName; // [esp+234h] [ebp-11Ch]
    char modulePath[268]; // [esp+238h] [ebp-118h] BYREF
    void *snapshot; // [esp+348h] [ebp-8h]
    void *process; // [esp+34Ch] [ebp-4h]

    process = OpenProcess(0x1F0FFFu, 0, id);
    if ( !process )
        return 0;
    CloseHandle(process);
    snapshot = CreateToolhelp32Snapshot(8u, id);
    if ( snapshot == (void *)-1 )
        return 0;
    isGame = 0;
    me.dwSize = 548;
    if ( Module32First(snapshot, &me) )
    {
        GetModuleFileNameA(0, modulePath, 0x104u);
        modulePath[259] = 0;
        moduleName = modulePath;
        for ( i = modulePath; *i; ++i )
        {
            if ( *i == 92 || *i == 58 )
                moduleName = i + 1;
        }
        while ( I_stricmp(me.szModule, moduleName) )
        {
            if ( !Module32Next(snapshot, &me) )
                goto LABEL_15;
        }
        isGame = 1;
    }
LABEL_15:
    CloseHandle(snapshot);
    return isGame;
}

void __cdecl Sys_NormalExit()
{
    DeleteFileA(sys_processSemaphoreFile);
}

char g_ExceptionStr[32768];
int __cdecl PrivateUnhandledExceptionFilter(_EXCEPTION_POINTERS *ExceptionInfo)
{
    char *v2; // eax
    unsigned int ExceptionCode; // [esp+148h] [ebp-10h]
    int j; // [esp+14Ch] [ebp-Ch]
    int ja; // [esp+14Ch] [ebp-Ch]
    int i; // [esp+154h] [ebp-4h]

    strcpy(g_ExceptionStr, "Exception: ");
    ExceptionCode = ExceptionInfo->ExceptionRecord->ExceptionCode;
    if (ExceptionCode > 0xC0000005)
    {
        switch (ExceptionCode)
        {
        case 0xC0000006:
            strcat(g_ExceptionStr, "EXCEPTION_IN_PAGE_ERROR\n");
            break;
        case 0xC000001D:
            strcat(g_ExceptionStr, "EXCEPTION_ILLEGAL_INSTRUCTION\n");
            break;
        case 0xC0000025:
            strcat(g_ExceptionStr, "EXCEPTION_NONCONTINUABLE_EXCEPTION\n");
            break;
        case 0xC0000026:
            strcat(g_ExceptionStr, "EXCEPTION_INVALID_DISPOSITION\n");
            break;
        case 0xC000008C:
            strcat(g_ExceptionStr, "EXCEPTION_ARRAY_BOUNDS_EXCEEDED\n");
            break;
        case 0xC000008D:
            strcat(g_ExceptionStr, "EXCEPTION_FLT_DENORMAL_OPERAND\n");
            break;
        case 0xC000008E:
            strcat(g_ExceptionStr, "EXCEPTION_FLT_DIVIDE_BY_ZERO\n");
            break;
        case 0xC000008F:
            strcat(g_ExceptionStr, "EXCEPTION_FLT_INEXACT_RESULT\n");
            break;
        case 0xC0000090:
            strcat(g_ExceptionStr, "EXCEPTION_FLT_INVALID_OPERATION\n");
            break;
        case 0xC0000091:
            strcat(g_ExceptionStr, "EXCEPTION_FLT_OVERFLOW\n");
            break;
        case 0xC0000092:
            strcat(g_ExceptionStr, "EXCEPTION_FLT_STACK_CHECK\n");
            break;
        case 0xC0000093:
            strcat(g_ExceptionStr, "EXCEPTION_FLT_UNDERFLOW\n");
            break;
        case 0xC0000094:
            strcat(g_ExceptionStr, "EXCEPTION_INT_DIVIDE_BY_ZERO\n");
            break;
        case 0xC0000095:
            strcat(g_ExceptionStr, "EXCEPTION_INT_OVERFLOW\n");
            break;
        case 0xC0000096:
            strcat(g_ExceptionStr, "EXCEPTION_PRIV_INSTRUCTION\n");
            break;
        case 0xC00000FD:
            strcat(g_ExceptionStr, "EXCEPTION_STACK_OVERFLOW\n");
            break;
        default:
            goto LABEL_28;
        }
    }
    else
    {
        switch (ExceptionCode)
        {
        case 0xC0000005:
            strcat(g_ExceptionStr, "EXCEPTION_ACCESS_VIOLATION\n");
            break;
        case 0x80000002:
            strcat(g_ExceptionStr, "EXCEPTION_DATATYPE_MISALIGNMENT\n");
            break;
        case 0x80000003:
            strcat(g_ExceptionStr, "EXCEPTION_BREAKPOINT\n");
            break;
        case 0x80000004:
            strcat(g_ExceptionStr, "EXCEPTION_SINGLE_STEP\n");
            break;
        default:
        LABEL_28:
            strcat(g_ExceptionStr, "UNKNOWN\n");
            break;
        }
    }
    sprintf(
        &g_ExceptionStr[strlen(g_ExceptionStr)],
        "Exception Address: %08x\n\n",
        ExceptionInfo->ExceptionRecord->ExceptionAddress);
    if ((ExceptionInfo->ContextRecord->ContextFlags & 0x10001) != 0)
        sprintf(
            &g_ExceptionStr[strlen(g_ExceptionStr)],
            "EBP: %08x\tEIP: %08x\tSEGCS: %08x\tESP: %08x\tSEGSS: %08x\tEFLAGS: %08x\n\n",
            ExceptionInfo->ContextRecord->Ebp,
            ExceptionInfo->ContextRecord->Eip,
            ExceptionInfo->ContextRecord->SegCs,
            ExceptionInfo->ContextRecord->Esp,
            ExceptionInfo->ContextRecord->SegSs,
            ExceptionInfo->ContextRecord->EFlags);
    if ((ExceptionInfo->ContextRecord->ContextFlags & 0x10004) != 0)
        sprintf(
            &g_ExceptionStr[strlen(g_ExceptionStr)],
            "SEGGS: %08x\tSEGFS: %08x\tSEGES: %08x\tSEGDS: %08x\n",
            ExceptionInfo->ContextRecord->SegGs,
            ExceptionInfo->ContextRecord->SegFs,
            ExceptionInfo->ContextRecord->SegEs,
            ExceptionInfo->ContextRecord->SegDs);
    if ((ExceptionInfo->ContextRecord->ContextFlags & 0x10002) != 0)
        sprintf(
            &g_ExceptionStr[strlen(g_ExceptionStr)],
            "EDI: %08x\tESI: %08x\nEAX: %08x\tEBX: %08x\nECX: %08x\tEDX: %08x\n\n",
            ExceptionInfo->ContextRecord->Edi,
            ExceptionInfo->ContextRecord->Esi,
            ExceptionInfo->ContextRecord->Eax,
            ExceptionInfo->ContextRecord->Ebx,
            ExceptionInfo->ContextRecord->Ecx,
            ExceptionInfo->ContextRecord->Edx);
    if ((ExceptionInfo->ContextRecord->ContextFlags & 0x10001) != 0)
    {
        strcat(g_ExceptionStr, "Stack Bytes: \n");
        for (i = 0; i < 128; ++i)
        {
            for (j = 0; j < 8; ++j)
            {
                sprintf(
                    &g_ExceptionStr[strlen(g_ExceptionStr)],
                    "%02x",
                    *(unsigned __int8 *)(ExceptionInfo->ContextRecord->Esp + j + 8 * i));
                strcat(g_ExceptionStr, " ");
            }
            strcat(g_ExceptionStr, "\t[");
            for (ja = 0; ja < 8; ++ja)
            {
                if (*(unsigned __int8 *)(ExceptionInfo->ContextRecord->Esp + ja + 8 * i) >= 0x21u)
                    sprintf(
                        &g_ExceptionStr[strlen(g_ExceptionStr)],
                        "%c",
                        *(unsigned __int8 *)(ExceptionInfo->ContextRecord->Esp + ja + 8 * i));
                else
                    sprintf(&g_ExceptionStr[strlen(g_ExceptionStr)], ".");
            }
            strcat(g_ExceptionStr, "]\n");
        }
        strcat(g_ExceptionStr, "\n");
    }
    v2 = Win_LocalizeRef("WIN_ERROR");
    Com_Error(ERR_FATAL, v2);
    return 1;
}

char sys_cmdline[1024];
char g_open_automate_benchmark[260];
int __stdcall WinMain(HINSTANCE__ *hInstance, HINSTANCE__ *hPrevInstance, char *lpCmdLine, int nCmdShow)
{
    char *v5; // eax
    //jpeg_decompress_struct *SCRIPT_DEBUGGER_SMOKE_TEST_SUCCESS_EXIT_CODE; // [esp+0h] [ebp-4h]

    // zombies: decide headless first, before anything can open a window
    s_headless = Sys_CmdlineIntValue(lpCmdLine, "bo1_headless") != 0;
    s_headlessClient = s_headless && Sys_CmdlineIntValue(lpCmdLine, "bo1_headless_client") != 0;
    if ( s_headless )
    {
        typedef HRESULT(__stdcall *WerSetFlags_t)(DWORD);
        WerSetFlags_t werSetFlags = (WerSetFlags_t)GetProcAddress(GetModuleHandleA("kernel32.dll"), "WerSetFlags");
        if ( werSetFlags )
            werSetFlags(32); // WER_FAULT_REPORTING_NO_UI: a crash is still reported (Application Error 1000), silently
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
        _set_error_mode(_OUT_TO_STDERR); // CRT runtime errors: no message box
        _set_abort_behavior(0, _WRITE_ABORT_MSG);
        // headless client: its window gets no keyboard, so no IME / text services either (their floating language
        // bar, class CiceroUIWndFrame, is a visible window of this process); before any window exists
        HMODULE imm = LoadLibraryA("imm32.dll");
        typedef BOOL(__stdcall *ImmDisable_t)(DWORD);
        ImmDisable_t immDisableTfs = imm ? (ImmDisable_t)GetProcAddress(imm, "ImmDisableTextFrameService") : NULL;
        ImmDisable_t immDisableIme = imm ? (ImmDisable_t)GetProcAddress(imm, "ImmDisableIME") : NULL;
        if ( immDisableTfs )
            immDisableTfs((DWORD)-1);
        if ( immDisableIme )
            immDisableIme((DWORD)-1);
    }
    if ( !StartingDedicatedServer(lpCmdLine) && !s_headless && CheckRemoteSession() )
        return 0;
    g_allowMature = 1;
    if ( I_stristr(lpCmdLine, "minidump") || !I_stristr(lpCmdLine, "nodump") )
        Sys_StartMiniDump(1);
    else
        SetUnhandledExceptionFilter((LPTOP_LEVEL_EXCEPTION_FILTER)PrivateUnhandledExceptionFilter);
    if ( s_headless ) // zombies: after the minidumper, whose filter this replaces
        SetUnhandledExceptionFilter(Sys_HeadlessExceptionFilter);
    else // x6c38: a normal launch logs the crash too, then runs the filter set above
        s_prevExceptionFilter = SetUnhandledExceptionFilter(Sys_LoggingExceptionFilter);
    s_mainThreadId = GetCurrentThreadId();
    {
        // zombies: Sys_HeadlessEmergencyLog's surviving copy, cleared at the start of every run (x6c38: not only headless)
        char emergencyPath[MAX_PATH + 32];
        Sys_EmergencyPath("bo1_emergency.log", emergencyPath, sizeof(emergencyPath));
        DeleteFileA(emergencyPath);
    }
    s_threadDumpMs = s_headless ? (unsigned int)Sys_CmdlineIntValue(lpCmdLine, "bo1_threaddump") : 0;
    if ( s_threadDumpMs ) // zombies: headless hang evidence (Sys_HeadlessDumpThreads)
    {
        DeleteFileA("main\\bo1_threads.log");
        CloseHandle(CreateThread(NULL, 0, Sys_HeadlessThreadDumpThread, NULL, 0, NULL));
    }
    // L43: also in a real window (tools\realtest.ps1 reads its BO1_FRAMEPERF lines); off unless on the command line
    s_framePerfMs = (unsigned int)Sys_CmdlineIntValue(lpCmdLine, "bo1_frameperf");
    s_hitchWatchMs = s_headless && !s_threadDumpMs ? (unsigned int)Sys_CmdlineIntValue(lpCmdLine, "bo1_hitchwatch") : 0;
    if ( s_hitchWatchMs ) // k1: headless client hitch evidence (Sys_HitchWatchThread)
    {
        DeleteFileA("main\\bo1_threads.log");
        CloseHandle(CreateThread(NULL, 0, Sys_HitchWatchThread, NULL, 0, NULL));
    }
    Sys_InitializeCriticalSections();
    Sys_InitMainThread();
    PMem_Init();
    track_init();
    Win_InitLocalization();
    if ( !I_strnicmp(lpCmdLine, "allowdupe", 9) && lpCmdLine[9] <= 32
        || (v5 = strstr(lpCmdLine, "g_connectpaths 3"), v5)
        || (Sys_GetSemaphoreFileName(), Sys_CheckCrashOrRerun()) )
    {
        s_nosnd = I_stristr(lpCmdLine, "nosnd") != 0;
        // zombies: headless never opens a sound device (a headless client must stay silent). p1: except a headless
        // client with '+set bo1_headless_sound 1' (performance runs closer to real play): the sound system loads and
        // mixes as usual, and snd_driver_xaudio2.cpp sets the mastering voice volume to 0, so nothing is heard.
        if ( s_headless && !(s_headlessClient && Sys_CmdlineIntValue(lpCmdLine, "bo1_headless_sound")) )
            s_nosnd = 1;
        if ( !hPrevInstance )
        {
            Com_InitParse();
            Dvar_Init();
            InitTiming();
            Sys_FindInfo();
            g_wv.hInstance = hInstance;
            I_strncpyz(sys_cmdline, lpCmdLine, 1024);
            // x6: the headless settings below must always fit (a cut '+set dedicated 1' or '+set r_aaSamples 1' changes
            // what runs); a caller line too long for them loses its tail instead (headless.ps1 refuses such lines)
            if ( s_headless && strlen(sys_cmdline) > 1024 - 1 - 96 )
                sys_cmdline[1024 - 1 - 96] = 0;
            // c2: the same for Com_ParseCommandLine's 64 (L43; retail 32) console lines (one per '+' or newline after the leading text):
            // past line 32 the rest of the line, suffix included, joins line 32 (c8: '+set r_aaSamples 1' ended up
            // inside net_ip, the back buffer stayed 4x multisampled and GetRenderTargetData failed)
            if ( s_headless )
            {
                const int suffixLines = s_headlessClient ? 4 : 3;
                int lines = 1;
                for ( char *p = sys_cmdline; *p; ++p )
                {
                    if ( *p != '+' && *p != '\n' )
                        continue;
                    if ( lines + suffixLines == 64 ) // mod (L43): com_consoleLines keeps 64 now (was 32: the horde flags' tail, +exec included, was cut)
                    {
                        *p = 0;
                        break;
                    }
                    ++lines;
                }
            }
            if ( s_headlessClient )
            {
                // zombies: headless client is a listen server on loopback with a local client whose window lives
                // on the private desktop: always windowed, no multisampling (the back buffer screenshot needs a
                // D3DSWAPEFFECT_COPY swap chain); appended last so these win over the caller's values
                // p1: '+set bo1_headless_aa <2..16>' keeps multisampling for performance runs at the player's
                // settings (the swap chain stays DISCARD, so bo1_shots screenshots do not work in that mode)
                const int aa = Sys_CmdlineIntValue(lpCmdLine, "bo1_headless_aa");
                char suffix[96];
                sprintf_s(suffix, " +set logfile 2 +set net_ip 127.0.0.1 +set r_fullscreen 0 +set r_aaSamples %i",
                    aa >= 2 && aa <= 16 ? aa : 1);
                I_strncat(sys_cmdline, 1024, suffix);
            }
            else if ( s_headless )
            {
                // zombies: headless is a dedicated LAN server on loopback (no renderer, no sound, no firewall
                // prompt), logging every line; appended last so these win over the caller's values
                I_strncat(sys_cmdline, 1024, " +set dedicated 1 +set logfile 2 +set net_ip 127.0.0.1");
            }
            if ( g_open_automate_benchmark[0] )
            {
                I_strncat(sys_cmdline, 1024, " +set r_open_automate 1 +set com_introPlayed 1 +set ui_autoContinue 1 +devmap ");
                I_strncat(sys_cmdline, 1024, g_open_automate_benchmark);
            }
            Win_RegisterClass();
            if ( !s_headless ) // zombies: headless keeps its error mode (set above)
                SetErrorMode(1u);
            Sys_Milliseconds();
            //BLOPS_NULLSUB(SCRIPT_DEBUGGER_SMOKE_TEST_SUCCESS_EXIT_CODE);
            tlPrintf("Hello from the wonderful world of TL\n"); // fuck you
            Sys_SetupTLCallbacks(0x900000);
            if ( !Sys_IsMainThread()
                && !Assert_MyHandler(
                            "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_main.cpp",
                            1684,
                            0,
                            "%s",
                            "Sys_IsMainThread()") )
            {
                __debugbreak();
            }

            // ADD: Steam Init
            Steam_Init();
            // END

            Com_Init(sys_cmdline);
            Steam_PrintInitStatus(); // zombies: Steam_Init ran before the console existed

            if (!IsDedicatedServer())
            {
                Cbuf_AddText(0, "readStats\n");
            }

            PrintWorkingDir();
            if ( s_headlessClient )
                Com_Printf(16, "bo1_headless_client: listen server + local client, window on the private desktop only, no sound, no input, logfile 2, net_ip 127.0.0.1\n");
            else if ( s_headless )
                Com_Printf(16, "bo1_headless: dedicated, no windows, logfile 2, net_ip 127.0.0.1\n");
            else
                SetFocus(g_wv.hWnd);
            s_autoquitStartMs = Sys_Milliseconds();
            if ( com_script_debugger_smoke_test->current.enabled )
                exit(31415);
            while ( 1 )
            {
                // if not running as a game client, sleep a bit
#ifdef BO1_MP
                if (g_wv.isMinimized || IsDedicatedServer())
#elif BO1_SP
                if (g_wv.isMinimized)
#endif
                {
                    Sleep(5);
                }

                // run the game
                Com_Frame();
                Sys_AutoQuitFrame(); // zombies
                
                //while ( !Dvar_GetBool("onlinegame") );
                //PbServerProcessEvents();
            }
        }
    }
    Win_ShutdownLocalization();
    track_shutdown(0);
    return 0;
}

void Sys_FindInfo()
{
    sys_info.logicalCpuCount = Sys_GetCpuCount();
    sys_info.cpuGHz = 1.0 / (((double)1LL - (double)0LL) * msecPerRawTimerTick * 1000000.0);
    sys_info.sysMB = Sys_SystemMemoryMB();
    Sys_DetectVideoCard(512, sys_info.gpuDescription);
    sys_info.SSE = Sys_SupportsSSE();
    Sys_DetectCpuVendorAndName(sys_info.cpuVendor, sys_info.cpuName);
    Sys_SetAutoConfigureGHz(&sys_info);
}

int Sys_GetSemaphoreFileName()
{
    char *i; // [esp+0h] [ebp-118h]
    const char *moduleName; // [esp+4h] [ebp-114h]
    char modulePath[268]; // [esp+8h] [ebp-110h] BYREF

    GetModuleFileNameA(0, modulePath, 0x104u);
    modulePath[259] = 0;
    moduleName = modulePath;
    for ( i = modulePath; *i; ++i )
    {
        if ( *i == 92 || *i == 58 )
        {
            moduleName = i + 1;
        }
        else if ( *i == 46 )
        {
            *i = 0;
        }
    }
    return sprintf(sys_processSemaphoreFile, "__%s", moduleName);
}

void Win_RegisterClass()
{
    tagWNDCLASSEXA wce; // [esp+0h] [ebp-30h] BYREF

    memset((unsigned __int8 *)&wce, 0, sizeof(wce));
    wce.cbSize = 48;
    wce.lpfnWndProc = (WNDPROC)MainWndProc;
    wce.hInstance = g_wv.hInstance;
    wce.hIcon = LoadIconA(g_wv.hInstance, (LPCSTR)1);
    wce.hCursor = LoadCursorA(0, (LPCSTR)0x7F00);
    wce.hbrBackground = CreateSolidBrush(0);
    wce.lpszClassName = "CoDBlackOps";
    if ( !RegisterClassExA(&wce) )
        Com_Error(ERR_FATAL, "EXE_ERR_COULDNT_REGISTER_WINDOW");
}

void PrintWorkingDir()
{
    char cwd[260]; // [esp+0h] [ebp-108h] BYREF

    _getcwd(cwd, 256);
    Com_Printf(16, "Working directory: %s\n", cwd);
}

char __cdecl CheckRemoteSession()
{
    if ( !GetSystemMetrics(4096) )
        return 0;
    MessageBoxA(0, "The game can not be run over a remote desktop connection.", "CoD", 0);
    return 1;
}

bool __cdecl StartingDedicatedServer(char *cmdline)
{
    const char *v1; // eax
    const char *p; // [esp+18h] [ebp-4h]

    v1 = strstr(cmdline, "dedicated");
    if ( !v1 )
        return 0;
    for ( p = (const char *)(strlen("dedicated") + v1); *p && *p == 32; ++p )
        ;
    if ( *p )
    {
        if ( *p == 34 )
            ++p;
    }
    return atoi(p) != 0;
}

