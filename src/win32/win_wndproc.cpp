#include "win_wndproc.h"
#include <client/client.h>
#include <gfx_d3d/r_init.h>
#include "win_main.h"
#include "win_input.h"
#include <gfx_d3d/r_dvars.h>
#include <client/cl_keys.h>
#include <universal/com_memory.h>
#include <cgame_mp/cg_newDraw_mp.h>
#include <gfx_d3d/rb_backend.h>

unsigned __int8 virtualKeyConvert[146][2] =
{
  { 0u, 0u },
  { 200u, 200u },
  { 201u, 201u },
  { 0u, 0u },
  { 202u, 202u },
  { 203u, 203u },
  { 204u, 204u },
  { 0u, 0u },
  { 127u, 127u },
  { 9u, 9u },
  { 0u, 0u },
  { 0u, 0u },
  { 186u, 0u },
  { 13u, 191u },
  { 0u, 0u },
  { 0u, 0u },
  { 160u, 160u },
  { 159u, 159u },
  { 158u, 158u },
  { 153u, 153u },
  { 151u, 151u },
  { 0u, 0u },
  { 0u, 0u },
  { 0u, 0u },
  { 0u, 0u },
  { 0u, 0u },
  { 0u, 0u },
  { 27u, 27u },
  { 0u, 0u },
  { 0u, 0u },
  { 0u, 0u },
  { 0u, 0u },
  { 32u, 32u },
  { 184u, 164u },
  { 190u, 163u },
  { 188u, 166u },
  { 182u, 165u },
  { 185u, 156u },
  { 183u, 154u },
  { 187u, 157u },
  { 189u, 155u },
  { 0u, 0u },
  { 0u, 0u },
  { 0u, 0u },
  { 0u, 0u },
  { 192u, 161u },
  { 193u, 162u },
  { 0u, 0u },
  { 48u, 48u },
  { 49u, 49u },
  { 50u, 50u },
  { 51u, 51u },
  { 52u, 52u },
  { 53u, 53u },
  { 54u, 54u },
  { 55u, 55u },
  { 56u, 56u },
  { 57u, 57u },
  { 0u, 0u },
  { 0u, 0u },
  { 0u, 0u },
  { 0u, 0u },
  { 0u, 0u },
  { 0u, 0u },
  { 0u, 0u },
  { 97u, 65u },
  { 98u, 66u },
  { 99u, 67u },
  { 100u, 68u },
  { 101u, 69u },
  { 102u, 70u },
  { 103u, 71u },
  { 104u, 72u },
  { 105u, 73u },
  { 106u, 74u },
  { 107u, 75u },
  { 108u, 76u },
  { 109u, 77u },
  { 110u, 78u },
  { 111u, 79u },
  { 112u, 80u },
  { 113u, 81u },
  { 114u, 82u },
  { 115u, 83u },
  { 116u, 84u },
  { 117u, 85u },
  { 118u, 86u },
  { 119u, 87u },
  { 120u, 88u },
  { 121u, 89u },
  { 122u, 90u },
  { 0u, 0u },
  { 0u, 0u },
  { 0u, 0u },
  { 0u, 0u },
  { 0u, 0u },
  { 192u, 192u },
  { 188u, 188u },
  { 189u, 189u },
  { 190u, 190u },
  { 185u, 185u },
  { 186u, 186u },
  { 187u, 187u },
  { 182u, 182u },
  { 183u, 183u },
  { 184u, 184u },
  { 198u, 198u },
  { 196u, 196u },
  { 0u, 0u },
  { 195u, 195u },
  { 193u, 193u },
  { 194u, 194u },
  { 167u, 167u },
  { 168u, 168u },
  { 169u, 169u },
  { 170u, 170u },
  { 171u, 171u },
  { 172u, 172u },
  { 173u, 173u },
  { 174u, 174u },
  { 175u, 175u },
  { 176u, 176u },
  { 177u, 177u },
  { 178u, 178u },
  { 0u, 0u },
  { 0u, 0u },
  { 0u, 0u },
  { 0u, 0u },
  { 0u, 0u },
  { 0u, 0u },
  { 0u, 0u },
  { 0u, 0u },
  { 0u, 0u },
  { 0u, 0u },
  { 0u, 0u },
  { 0u, 0u },
  { 0u, 0u },
  { 0u, 0u },
  { 0u, 0u },
  { 0u, 0u },
  { 0u, 0u },
  { 0u, 0u },
  { 0u, 0u },
  { 0u, 0u },
  { 197u, 197u },
  { 0u, 0u }
};


unsigned __int8 extendedVirtualKeyConvert[21][2] =
{
  { 181u, 128u },
  { 191u, 129u },
  { 223u, 130u },
  { 224u, 131u },
  { 225u, 132u },
  { 228u, 133u },
  { 229u, 134u },
  { 230u, 135u },
  { 231u, 136u },
  { 232u, 137u },
  { 233u, 138u },
  { 236u, 139u },
  { 241u, 140u },
  { 242u, 141u },
  { 243u, 142u },
  { 246u, 143u },
  { 248u, 144u },
  { 249u, 145u },
  { 250u, 146u },
  { 252u, 147u },
  { 0u, 0u }
};


WinVars_t g_wv;
int g_blockHook;
unsigned int MSH_MOUSEWHEEL;

const dvar_t *r_autopriority;

void __cdecl Sys_SetBlockSystemHotkeys(int block)
{
    HINSTANCE__ *module; // [esp+0h] [ebp-4h]

#ifdef BO1_PURE // This hook lags the entire systemwide keyboard for Windows whenever the application is paused/debugging. HORRIBLE
    if ( !g_wv.sysMsgTime )
    {
        module = GetModuleHandleA(0);
        g_wv.sysMsgTime = (unsigned int)SetWindowsHookExA(13, (HOOKPROC)LowLevelKeyboardProc, module, 0);
    }
#endif
    g_blockHook = block;
}

LRESULT __stdcall LowLevelKeyboardProc(int nCode, WPARAM wParam, unsigned int *lParam)
{
    bool isControlKeyDown; // [esp+7h] [ebp-1h]

    if ( nCode || !g_blockHook )
        return CallNextHookEx(0, nCode, wParam, (LPARAM)lParam);
    isControlKeyDown = GetAsyncKeyState(17) >> 15 != 0;
    if ( *lParam == 27 && isControlKeyDown )
        return 1;
    if ( *lParam == 9 && (lParam[2] & 0x20) != 0 )
        return 1;
    if ( *lParam == 13 && (lParam[2] & 0x20) != 0 )
        return 1;
    if ( *lParam == 91 || *lParam == 92 )
        return 1;
    else
        return CallNextHookEx(0, nCode, wParam, (LPARAM)lParam);
}

bool __cdecl Sys_AllowVidRestart()
{
    return CL_GetLocalClientConnectionState(0) == 0;
}

void __cdecl Sys_UpdateHotkeyBlock()
{
    bool block; // [esp+3h] [ebp-1h]

    block = g_wv.activeApp != 0;
    if ( Sys_AllowVidRestart() )
        block = 0;
    if ( !vidConfig.isFullscreen )
        block = 0;
    Sys_SetBlockSystemHotkeys(block);
}

int __stdcall MainWndProc(HWND__ *hWnd, UINT uMsg, signed int wParam, LPARAM lParam)
{
    int result; // eax
    HANDLE v5; // eax
    HANDLE CurrentProcess; // eax
    int buttons; // [esp+4h] [ebp-60h]
    HMONITOR__ *monitorHandle; // [esp+8h] [ebp-5Ch]
    tagMONITORINFO monitorInfo; // [esp+Ch] [ebp-58h] BYREF
    tagRECT rc; // [esp+34h] [ebp-30h] BYREF
    tagRECT r; // [esp+44h] [ebp-20h] BYREF
    int style; // [esp+54h] [ebp-10h]
    int yPos; // [esp+58h] [ebp-Ch]
    int xPos; // [esp+5Ch] [ebp-8h]
    unsigned __int8 code; // [esp+63h] [ebp-1h]

    if ( !Sys_IsHeadless() ) // zombies: a headless client never keeps the user's display awake (ES_DISPLAY_REQUIRED)
        SetThreadExecutionState(2u);
    if ( uMsg == MSH_MOUSEWHEEL )
    {
        if ( wParam <= 0 )
        {
            Sys_QueEvent(g_msgTime, SE_KEY, 205, 1, 0, 0);
            Sys_QueEvent(g_msgTime, SE_KEY, 205, 0, 0, 0);
        }
        else
        {
            Sys_QueEvent(g_msgTime, SE_KEY, 206, 1, 0, 0);
            Sys_QueEvent(g_msgTime, SE_KEY, 206, 0, 0, 0);
        }
        return DefWindowProcA(hWnd, uMsg, wParam, lParam);
    }
    else if ( uMsg > 0x100 )
    {
        if ( uMsg > 0x112 )
        {
            switch ( uMsg )
            {
                case 0x200u:
                case 0x201u:
                case 0x202u:
                case 0x204u:
                case 0x205u:
                case 0x207u:
                case 0x208u:
                case 0x20Bu:
                case 0x20Cu:
                    buttons = (wParam & 1) != 0;
                    if ( (wParam & 2) != 0 )
                        buttons |= 2u;
                    if ( (wParam & 0x10) != 0 )
                        buttons |= 4u;
                    if ( (wParam & 0x20) != 0 )
                        buttons |= 8u;
                    if ( (wParam & 0x40) != 0 )
                        buttons |= 0x10u;
                    IN_MouseEvent(buttons);
                    return DefWindowProcA(hWnd, uMsg, wParam, lParam);
                case 0x20Au:
                    if ( SHIWORD(wParam) <= 0 )
                    {
                        Sys_QueEvent(g_msgTime, SE_KEY, 205, 1, 0, 0);
                        Sys_QueEvent(g_msgTime, SE_KEY, 205, 0, 0, 0);
                    }
                    else
                    {
                        Sys_QueEvent(g_msgTime, SE_KEY, 206, 1, 0, 0);
                        Sys_QueEvent(g_msgTime, SE_KEY, 206, 0, 0, 0);
                    }
                    return DefWindowProcA(hWnd, uMsg, wParam, lParam);
                case 0x218u:
                    if ( (unsigned int)wParam > 1 )
                        return DefWindowProcA(hWnd, uMsg, wParam, lParam);
                    result = 1112363332;
                    break;
                default:
                    return DefWindowProcA(hWnd, uMsg, wParam, lParam);
            }
        }
        else if ( uMsg == 274 )
        {
            if ( wParam == 61760 )
                return 0;
            else
                return DefWindowProcA(hWnd, uMsg, wParam, lParam);
        }
        else
        {
            switch ( uMsg )
            {
                case 0x101u:
                case 0x105u:
                    code = MapKey(lParam, wParam);
                    if ( code )
                        Sys_QueEvent(g_msgTime, SE_KEY, code, 0, 0, 0);
                    result = 0;
                    break;
                case 0x102u:
                    Sys_QueEvent(g_msgTime, SE_CHAR, wParam, 0, 0, 0);
                    return DefWindowProcA(hWnd, uMsg, wParam, lParam);
                case 0x104u:
                    // Alt+F4: the game takes every other WM_SYSKEYDOWN as a key and never lets DefWindowProc see it,
                    // so Windows never turned Alt+F4 into SC_CLOSE. DefWindowProc sends WM_SYSCOMMAND SC_CLOSE, then
                    // WM_CLOSE, which quits like the window's close button (case 0x10 below).
                    if ( wParam == VK_F4 && (lParam & (1 << 29)) != 0 )
                        return DefWindowProcA(hWnd, uMsg, wParam, lParam);
                    if ( wParam != 13 )
                        goto LABEL_60;
                    if ( CL_GetLocalClientConnectionState(0) == 8 )
                    {
                        result = 0;
                    }
                    else
                    {
                        if ( r_fullscreen && Dvar_GetInt("developer") )
                        {
                            Dvar_SetBool((dvar_s *)r_fullscreen, !r_fullscreen->current.enabled);
                            Cbuf_AddText(0, "vid_restart\n");
                        }
                        result = 0;
                    }
                    break;
                default:
                    return DefWindowProcA(hWnd, uMsg, wParam, lParam);
            }
        }
    }
    else if ( uMsg == 256 )
    {
LABEL_60:
        code = MapKey(lParam, wParam);
        if ( code )
            Sys_QueEvent(g_msgTime, SE_KEY, code, 1, 0, 0);
        return 0;
    }
    else
    {
        switch ( uMsg )
        {
            case 1u:
                g_wv.hWnd = hWnd;
                if ( !r_reflectionProbeGenerate
                    && !Assert_MyHandler(
                                "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_wndproc.cpp",
                                549,
                                0,
                                "%s",
                                "r_reflectionProbeGenerate") )
                {
                    __debugbreak();
                }
                if ( !r_fullscreen
                    && !Assert_MyHandler(
                                "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_wndproc.cpp",
                                550,
                                0,
                                "%s",
                                "r_fullscreen") )
                {
                    __debugbreak();
                }
                if ( r_reflectionProbeGenerate->current.enabled && r_fullscreen->current.enabled )
                {
                    Dvar_SetBool((dvar_s *)r_fullscreen, 0);
                    Cbuf_AddText(0, "vid_restart\n");
                }
                r_autopriority = _Dvar_RegisterBool(
                                                     "r_autopriority",
                                                     0,
                                                     1u,
                                                     "Automatically set the priority of the windows process when the game is minimized");
                MSH_MOUSEWHEEL = RegisterWindowMessageA("MSWHEEL_ROLLMSG");
                return DefWindowProcA(hWnd, uMsg, wParam, lParam);
            case 2u:
                g_wv.hWnd = 0;
                return DefWindowProcA(hWnd, uMsg, wParam, lParam);
            case 3u:
                if ( r_fullscreen->current.enabled )
                {
                    IN_ActivateMouse(0);
                }
                else
                {
                    xPos = (__int16)lParam;
                    yPos = SHIWORD(lParam);
                    r.left = 0;
                    r.top = 0;
                    r.right = 1;
                    r.bottom = 1;
                    style = GetWindowLongA(hWnd, -16);
                    AdjustWindowRect(&r, style, 0);
                    Dvar_SetInt((dvar_s *)vid_xpos, r.left + xPos);
                    Dvar_SetInt((dvar_s *)vid_ypos, r.top + yPos);
                    Dvar_ClearModified(vid_xpos);
                    Dvar_ClearModified(vid_ypos);
                    if ( g_wv.activeApp )
                        IN_Activate(1);
                }
                return DefWindowProcA(hWnd, uMsg, wParam, lParam);
            case 6u:
                VID_AppActivate((unsigned __int16)wParam, HIWORD(wParam));
                return DefWindowProcA(hWnd, uMsg, wParam, lParam);
            case 7u:
                if ( !r_autopriority->current.enabled )
                    return DefWindowProcA(hWnd, uMsg, wParam, lParam);
                CurrentProcess = GetCurrentProcess();
                SetPriorityClass(CurrentProcess, 0x20u);
                result = 0;
                break;
            case 8u:
                if ( !r_autopriority->current.enabled )
                    return DefWindowProcA(hWnd, uMsg, wParam, lParam);
                v5 = GetCurrentProcess();
                SetPriorityClass(v5, 0x40u);
                result = 0;
                break;
            case 0x10u:
                Sys_SetQuitRequested(); // L5: seen by the lost-device loop even when the main thread cannot run "quit"
                Cbuf_AddText(0, "quit\n");
                result = 0;
                break;
            case 0x11u:
                if ( lParam != 1 )
                    return DefWindowProcA(hWnd, uMsg, wParam, lParam);
                result = 1;
                break;
            case 0x16u:
                if ( lParam == 1 )
                    Com_Quit_f();
                return DefWindowProcA(hWnd, uMsg, wParam, lParam);
            case 0x7Eu:
                monitorHandle = MonitorFromWindow(hWnd, 2u);
                monitorInfo.cbSize = 40;
                GetMonitorInfoA(monitorHandle, &monitorInfo);
                GetWindowRect(hWnd, &rc);
                if ( rc.right - rc.left == monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left
                    && rc.bottom - rc.top == monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top )
                {
                    MoveWindow(
                        hWnd,
                        monitorInfo.rcMonitor.left,
                        monitorInfo.rcMonitor.top,
                        rc.right - rc.left,
                        rc.bottom - rc.top,
                        1);
                }
                IN_RecenterMouse();
                return DefWindowProcA(hWnd, uMsg, wParam, lParam);
            default:
                return DefWindowProcA(hWnd, uMsg, wParam, lParam);
        }
    }
    return result;
}

void __cdecl VID_AppActivate(unsigned int activeState, int minimize)
{
    bool v2; // [esp+0h] [ebp-8h]

    g_wv.isMinimized = minimize;
    Key_ClearStates(0);
    v2 = activeState && !g_wv.isMinimized;
    g_wv.activeApp = v2;
    if ( v2 )
        Com_TouchMemory();
    IN_Activate(g_wv.activeApp);
    Sys_UpdateHotkeyBlock();
}

unsigned __int8 __cdecl MapKey(int key, unsigned int wParam)
{
    unsigned int result; // [esp+0h] [ebp-10h]
    int i; // [esp+Ch] [ebp-4h]

    if ( BYTE2(key) == 41 )
        return 126;
    result = 0;
    if ( wParam && wParam <= 0x91 )
        result = AdustKeyForNumericKeypad(virtualKeyConvert[wParam][(key >> 24) & 1], wParam, (key >> 24) & 1);
    if ( !result )
    {
        result = (unsigned __int8)MapVirtualKeyA(wParam, 2u);
        if ( result > 0x7F )
        {
            for ( i = 0; extendedVirtualKeyConvert[i][0]; ++i )
            {
                if ( result == extendedVirtualKeyConvert[i][0] )
                {
                    result = extendedVirtualKeyConvert[i][1];
                    break;
                }
            }
        }
    }
    if ( result >= 0x100
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_wndproc.cpp",
                    485,
                    0,
                    "%s",
                    "(result >= 0) && (result < 256)") )
    {
        __debugbreak();
    }
    return result;
}

unsigned int __cdecl AdustKeyForNumericKeypad(unsigned int key, unsigned int wParam, unsigned int extended)
{
    if ( (CL_GetLocalClientUIGlobals(0)->keyCatchers & 0x11) == 0 )
        return key;
    if ( extended )
        return key;
    return !IsNumLockAffectedVK(wParam) ? key : 0;
}

bool __cdecl IsNumLockAffectedVK(unsigned int wParam)
{
    return wParam >= 0x60 && wParam <= 0x69 || wParam == 110;
}

