#include <Windows.h>

#include "win_gamepad.h"
#include "win_main.h"
#include "win_dualsense.h"
#include <universal/assertive.h>
#include <cmath>
#include <universal/com_math.h>

const dvar_t *gpad_debug;
const dvar_t *gpad_button_lstick_deflect_max;
const dvar_t *gpad_button_rstick_deflect_max;
const dvar_t *gpad_button_deadzone;
const dvar_t *gpad_stick_deadzone_min;
const dvar_t *gpad_stick_deadzone_max;
const dvar_t *gpad_stick_pressed;
const dvar_t *gpad_stick_pressed_hysteresis;
const dvar_t *gpad_rumble;
const dvar_t *gpad_menu_scroll_delay_first;
const dvar_t *gpad_menu_scroll_delay_rest;
const dvar_t *gpad_buttonsConfig;
const dvar_t *gpad_sticksConfig;
const dvar_t *gpad_enabled;
const dvar_t *gpad_present;

GamePad s_gamePads[1];
int inputCounter;
bool hasInput[1];
_XINPUT_STATE delayedInputState[1];
void(__cdecl *s_removedCB)(int);
void(__cdecl *s_insertedCB)(int);

void GPad_InitAll()
{
    GamePad *gPad; // [esp+14h] [ebp-8h]
    signed int portIndex; // [esp+18h] [ebp-4h]

    gpad_debug = _Dvar_RegisterInt("gpad_debug", 0, 0x80000000, 0x7FFFFFFF, 0, "coder use only");
    gpad_button_lstick_deflect_max = _Dvar_RegisterFloat(
                                                                         "gpad_button_lstick_deflect_max",
                                                                         1.0,
                                                                         0.0,
                                                                         1.0,
                                                                         0,
                                                                         "Game pad maximum pad stick pressed value");
    gpad_button_rstick_deflect_max = _Dvar_RegisterFloat(
                                                                         "gpad_button_rstick_deflect_max",
                                                                         1.0,
                                                                         0.0,
                                                                         1.0,
                                                                         0,
                                                                         "Game pad maximum pad stick pressed value");
    gpad_button_deadzone = _Dvar_RegisterFloat(
                                                     "gpad_button_deadzone",
                                                     0.13,
                                                     0.0,
                                                     1.0,
                                                     0x80u,
                                                     "Game pad button deadzone threshhold");
    gpad_stick_deadzone_min = _Dvar_RegisterFloat(
                                                            "gpad_stick_deadzone_min",
                                                            0.2,
                                                            0.0,
                                                            1.0,
                                                            0x80u,
                                                            "Game pad minimum stick deadzone");
    gpad_stick_deadzone_max = _Dvar_RegisterFloat(
                                                            "gpad_stick_deadzone_max",
                                                            0.0099999998,
                                                            0.0,
                                                            1.0,
                                                            0x80u,
                                                            "Game pad maximum stick deadzone");
    gpad_stick_pressed = _Dvar_RegisterFloat(
                                                 "gpad_stick_pressed",
                                                 0.40000001,
                                                 0.0,
                                                 1.0,
                                                 0x80u,
                                                 "Game pad stick pressed threshhold");
    gpad_stick_pressed_hysteresis = _Dvar_RegisterFloat(
                                                                        "gpad_stick_pressed_hysteresis",
                                                                        0.1,
                                                                        0.0,
                                                                        1.0,
                                                                        0x80u,
                                                                        "Game pad stick pressed no-change-zone around gpad_stick_pressed to prevent bouncing");
    gpad_rumble = _Dvar_RegisterBool("gpad_rumble", 1, 1u, "Enable game pad rumble");
    gpad_menu_scroll_delay_first = _Dvar_RegisterInt(
                                                                     "gpad_menu_scroll_delay_first",
                                                                     420,
                                                                     0,
                                                                     1000,
                                                                     1u,
                                                                     "Menu scroll key-repeat delay, for the first repeat, in milliseconds");
    gpad_menu_scroll_delay_rest = _Dvar_RegisterInt(
                                                                    "gpad_menu_scroll_delay_rest",
                                                                    210,
                                                                    0,
                                                                    1000,
                                                                    1u,
                                                                    "Menu scroll key-repeat delay, for repeats after the first, in milliseconds");
    gpad_buttonsConfig = _Dvar_RegisterString(
                                                 "gpad_buttonsConfig",
                                                 "buttons_default",
                                                 1u,
                                                 "Game pad button configuration");
    gpad_sticksConfig = _Dvar_RegisterString(
                                                "gpad_sticksConfig",
                                                "thumbstick_default",
                                                1u,
                                                "Game pad stick configuration");
    gpad_enabled = _Dvar_RegisterBool("gpad_enabled", 0, 1u, "Game pad enabled");
    gpad_present = _Dvar_RegisterBool("gpad_present", 0, 0x40u, "Game pad present");
    DS_Init(); // DualSense settings (ds_*)
    for ( portIndex = 0; portIndex < 1; ++portIndex )
    {
        gPad = &s_gamePads[portIndex];
        gPad->feedback.Rumble.wLeftMotorSpeed = 0;
        gPad->feedback.Rumble.wRightMotorSpeed = 0;
        if ( !Sys_IsHeadless() ) // zombies: headless never writes to the user's game pad (rumble)
            XInputSetState(portIndex, &gPad->feedback.Rumble);
    }
}

// Reads the active pad: a native DualSense if one is connected, otherwise XInput.
static bool GPad_ReadState(int portIndex, _XINPUT_STATE *state)
{
    if (portIndex == 0 && DS_GetState(state))
        return true;
    return XInputGetState(portIndex, state) == 0;
}

void __cdecl GPad_UpdateAll()
{
    _XINPUT_STATE inputState; // [esp+0h] [ebp-14h] BYREF
    int portIndex; // [esp+10h] [ebp-4h]

    if ( gpad_debug->current.integer >= 0 )
    {
        GPad_RefreshAll();
        for ( portIndex = 0; portIndex < 1; ++portIndex )
        {
            if ( GPad_ReadState(portIndex, &inputState) )
            {
                GPad_UpdateSticks(portIndex, &inputState.Gamepad);
                GPad_UpdateDigitals(portIndex, &inputState.Gamepad);
                GPad_UpdateAnalogs(portIndex, &inputState.Gamepad);
            }
        }
        inputCounter = 0;
    }
    else
    {
        if ( !inputCounter )
        {
            for ( portIndex = 0; portIndex < 1; ++portIndex )
            {
                if ( hasInput[portIndex] )
                {
                    GPad_UpdateSticks(portIndex, &delayedInputState[portIndex].Gamepad);
                    GPad_UpdateDigitals(portIndex, &delayedInputState[portIndex].Gamepad);
                    GPad_UpdateAnalogs(portIndex, &delayedInputState[portIndex].Gamepad);
                }
            }
        }
        if ( --inputCounter <= gpad_debug->current.integer )
        {
            GPad_RefreshAll();
            for ( portIndex = 0; portIndex < 1; ++portIndex )
                hasInput[portIndex] = GPad_ReadState(portIndex, &delayedInputState[portIndex]);
            inputCounter = 0;
        }
    }
}

int GPad_RefreshAll()
{
    int result; // eax
    int portIndex; // [esp+0h] [ebp-4h]

    for ( portIndex = 0; portIndex < 1; ++portIndex )
    {
        GPad_Check(portIndex);
        result = portIndex + 1;
    }
    return result;
}

void __cdecl GPad_Check(int portIndex)
{
    GamePad *gPad; // [esp+0h] [ebp-8h]
    bool wasEnabled; // [esp+7h] [ebp-1h]

    if ( portIndex
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp",
                    100,
                    0,
                    "%s",
                    "(portIndex >= 0) && (portIndex < MAX_GPAD_COUNT)") )
    {
        __debugbreak();
    }
    gPad = &s_gamePads[portIndex];
    wasEnabled = gPad->enabled;
    if ( portIndex == 0 )
    {
        DS_Pump(); // scan/read/write the DualSense once per refresh
        DS_SetRumble(gPad->feedback.Rumble.wLeftMotorSpeed, gPad->feedback.Rumble.wRightMotorSpeed);
    }
    if ( portIndex == 0 && DS_IsConnected() )
        gPad->enabled = 1;
    else if ( XInputGetCapabilities(portIndex, 1u, &gPad->caps) )
        gPad->enabled = 0;
    else
        gPad->enabled = 1;
    gPad->keyboardEnabled = 0;
    if ( s_removedCB && wasEnabled && !gPad->enabled )
    {
        GPad_ResetState(portIndex);
        s_removedCB(portIndex);
    }
    else if ( s_insertedCB && !wasEnabled && gPad->enabled )
    {
        s_insertedCB(portIndex);
    }
}

void __cdecl GPad_UpdateDigitals(int portIndex, const _XINPUT_GAMEPAD *xpad)
{
    GamePad *gPad; // [esp+10h] [ebp-10h]
    float rightDeflect; // [esp+14h] [ebp-Ch]
    float leftDeflect; // [esp+1Ch] [ebp-4h]

    if ( portIndex
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp",
                    146,
                    0,
                    "%s",
                    "(portIndex >= 0) && (portIndex < MAX_GPAD_COUNT)") )
    {
        __debugbreak();
    }
    if ( !xpad && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp", 147, 0, "%s", "xpad") )
        __debugbreak();
    gPad = &s_gamePads[portIndex];
    gPad->lastDigitals = gPad->digitals;
    gPad->digitals = xpad->wButtons;
    rightDeflect = Dvar_GetFloat("gpad_button_rstick_deflect_max");
    if ( fabs(gPad->sticks[2]) > rightDeflect )
        gPad->digitals &= ~0x80u;
    if ( fabs(gPad->sticks[3]) > rightDeflect )
        gPad->digitals &= ~0x80u;
    leftDeflect = Dvar_GetFloat("gpad_button_lstick_deflect_max");
    if ( fabs(gPad->sticks[0]) > leftDeflect )
        gPad->digitals &= ~0x40u;
    if ( fabs(gPad->sticks[1]) > leftDeflect )
        gPad->digitals &= ~0x40u;
}

void __cdecl GPad_UpdateAnalogs(int portIndex, const _XINPUT_GAMEPAD *xpad)
{
    float v2; // [esp+4h] [ebp-1Ch]
    float v3; // [esp+8h] [ebp-18h]
    GamePad *gPad; // [esp+14h] [ebp-Ch]

    if ( portIndex
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp",
                    187,
                    0,
                    "%s",
                    "(portIndex >= 0) && (portIndex < MAX_GPAD_COUNT)") )
    {
        __debugbreak();
    }
    if ( !xpad && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp", 188, 0, "%s", "xpad") )
        __debugbreak();
    gPad = &s_gamePads[portIndex];
    gPad->lastAnalogs[0] = gPad->analogs[0];
    if ( (float)(0.0
                         - (float)((float)((float)((float)xpad->bLeftTrigger / 255.0) - gpad_button_deadzone->current.value)
                                         / (float)(1.0 - gpad_button_deadzone->current.value))) < 0.0 )
        v3 = (float)((float)((float)xpad->bLeftTrigger / 255.0) - gpad_button_deadzone->current.value)
             / (float)(1.0 - gpad_button_deadzone->current.value);
    else
        v3 = 0.0f;
    gPad->analogs[0] = v3;
    gPad->lastAnalogs[1] = gPad->analogs[1];
    if ( (float)(0.0
                         - (float)((float)((float)((float)xpad->bRightTrigger / 255.0) - gpad_button_deadzone->current.value)
                                         / (float)(1.0 - gpad_button_deadzone->current.value))) < 0.0 )
        v2 = (float)((float)((float)xpad->bRightTrigger / 255.0) - gpad_button_deadzone->current.value)
             / (float)(1.0 - gpad_button_deadzone->current.value);
    else
        v2 = 0.0f;
    gPad->analogs[1] = v2;
}

void __cdecl GPad_UpdateSticks(int portIndex, const _XINPUT_GAMEPAD *xpad)
{
    float rVec[2]; // [esp+4h] [ebp-18h] BYREF
    GamePad *gPad; // [esp+Ch] [ebp-10h]
    int stickIndex; // [esp+10h] [ebp-Ch]
    float lVec[2]; // [esp+14h] [ebp-8h] BYREF

    if ( portIndex
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp",
                    273,
                    0,
                    "%s",
                    "(portIndex >= 0) && (portIndex < MAX_GPAD_COUNT)") )
    {
        __debugbreak();
    }
    if ( !xpad && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp", 274, 0, "%s", "xpad") )
        __debugbreak();
    gPad = &s_gamePads[portIndex];
    GPad_ConvertStickToFloat(xpad->sThumbLX, xpad->sThumbLY, lVec, &lVec[1]);
    GPad_ConvertStickToFloat(xpad->sThumbRX, xpad->sThumbRY, rVec, &rVec[1]);
    stickIndex = 0;
    gPad->lastSticks[0] = gPad->sticks[0];
    gPad->sticks[stickIndex] = lVec[0];
    stickIndex = 1;
    gPad->lastSticks[1] = gPad->sticks[1];
    gPad->sticks[stickIndex] = lVec[1];
    stickIndex = 2;
    gPad->lastSticks[2] = gPad->sticks[2];
    gPad->sticks[stickIndex] = rVec[0];
    stickIndex = 3;
    gPad->lastSticks[3] = gPad->sticks[3];
    gPad->sticks[stickIndex] = rVec[1];
    GPad_UpdateSticksDown(gPad);
}

void __cdecl GPad_ConvertStickToFloat(__int16 inX, __int16 inY, float *outX, float *outY)
{
    float stickVec[2]; // [esp+Ch] [ebp-10h] BYREF
    float len; // [esp+14h] [ebp-8h]
    float deadZoneTotal; // [esp+18h] [ebp-4h]

    stickVec[0] = (float)inX / 32767.0;
    stickVec[1] = (float)inY / 32767.0;
    deadZoneTotal = gpad_stick_deadzone_min->current.value + gpad_stick_deadzone_max->current.value;
    len = Vec2Normalize(stickVec);
    if ( gpad_stick_deadzone_min->current.value <= len )
    {
        if ( len <= (float)(1.0 - gpad_stick_deadzone_max->current.value) )
            len = (float)(len - gpad_stick_deadzone_min->current.value) / (float)(1.0 - deadZoneTotal);
        else
            len = 1.0f;
    }
    else
    {
        len = 0.0f;
    }
    *outX = stickVec[0] * len;
    *outY = stickVec[1] * len;
}

void __cdecl GPad_UpdateSticksDown(GamePad *gPad)
{
    int dir; // [esp+Ch] [ebp-Ch]
    float threshold; // [esp+10h] [ebp-8h]
    float thresholda; // [esp+10h] [ebp-8h]
    int stickIter; // [esp+14h] [ebp-4h]

    for ( stickIter = 0; stickIter != 4; ++stickIter )
    {
        for ( dir = 0; dir != 2; ++dir )
        {
            threshold = gpad_stick_pressed->current.value;
            gPad->stickDownLast[stickIter][dir] = gPad->stickDown[stickIter][dir];
            if ( gPad->stickDownLast[stickIter][dir] )
                thresholda = threshold - gpad_stick_pressed_hysteresis->current.value;
            else
                thresholda = threshold + gpad_stick_pressed_hysteresis->current.value;
            if ( dir )
            {
                if ( dir != 1
                    && !Assert_MyHandler(
                                "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp",
                                258,
                                0,
                                "%s",
                                "dir == GPAD_STICK_NEG") )
                {
                    __debugbreak();
                }
                //gPad->stickDown[stickIter][dir] = COERCE_FLOAT(LODWORD(thresholda) ^ _mask__NegFloat_) > gPad->sticks[stickIter];
                gPad->stickDown[stickIter][dir] = -thresholda > gPad->sticks[stickIter];
            }
            else
            {
                gPad->stickDown[stickIter][0] = gPad->sticks[stickIter] > thresholda;
            }
        }
    }
}

void __cdecl GPad_ResetState(int portIndex)
{
    _XINPUT_GAMEPAD inputStateGamepad; // [esp+0h] [ebp-Ch] BYREF

    memset(&inputStateGamepad, 0, sizeof(inputStateGamepad));
    GPad_UpdateSticks(portIndex, &inputStateGamepad);
    GPad_UpdateDigitals(portIndex, &inputStateGamepad);
    GPad_UpdateAnalogs(portIndex, &inputStateGamepad);
}

bool __cdecl GPad_IsActive(int portIndex)
{
    if ( portIndex
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp",
                    469,
                    0,
                    "%s",
                    "(portIndex >= 0) && (portIndex < MAX_GPAD_COUNT)") )
    {
        __debugbreak();
    }
    return s_gamePads[portIndex].enabled;
}

double __cdecl GPad_GetButton(int portIndex, GamePadButton button)
{
    GamePad *gPad; // [esp+4h] [ebp-Ch]
    float value; // [esp+Ch] [ebp-4h]

    value = 0.0f;
    if ( portIndex
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp",
                    501,
                    0,
                    "%s",
                    "( portIndex >= 0 ) && ( portIndex < MAX_GPAD_COUNT )") )
    {
        __debugbreak();
    }
    if ( (button & 0x30000000) == 0
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp",
                    502,
                    0,
                    "%s",
                    "button & ( GPAD_DIGITAL_MASK | GPAD_ANALOG_MASK )") )
    {
        __debugbreak();
    }
    gPad = &s_gamePads[portIndex];
    if ( (button & 0x10000000) != 0 )
    {
        if ( (button & 0xEFFFFFFF & gPad->digitals) != 0 )
            return 1.0f;
        else
            return 0.0f;
    }
    else if ( (button & 0x20000000) != 0 )
    {
        return gPad->analogs[button & 0xDFFFFFFF];
    }
    return value;
}

bool __cdecl GPad_ButtonRequiresUpdates(int portIndex, GamePadButton button)
{
    return (button & 0x20000000) != 0 && GPad_GetButton(portIndex, button) > 0.0;
}

bool __cdecl GPad_IsButtonPressed(int portIndex, GamePadButton button)
{
    GamePad *gPad; // [esp+Ch] [ebp-Ch]
    unsigned __int32 but; // [esp+10h] [ebp-8h]
    bool lastDown; // [esp+16h] [ebp-2h]
    bool down; // [esp+17h] [ebp-1h]

    down = 0;
    lastDown = 0;
    if ( portIndex
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp",
                    539,
                    0,
                    "%s",
                    "( portIndex >= 0 ) && ( portIndex < MAX_GPAD_COUNT )") )
    {
        __debugbreak();
    }
    if ( (button & 0x30000000) == 0
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp",
                    540,
                    0,
                    "%s",
                    "button & ( GPAD_DIGITAL_MASK | GPAD_ANALOG_MASK )") )
    {
        __debugbreak();
    }
    gPad = &s_gamePads[portIndex];
    if ( (button & 0x10000000) != 0 )
    {
        but = button & 0xEFFFFFFF;
        if ( (button & 0xF) != 0 && (but & gPad->digitals) != 0 && (gPad->digitals & 0xF) != (button & 0xF) )
        {
            down = 0;
            lastDown = 0;
        }
        else
        {
            down = (but & gPad->digitals) != 0;
            lastDown = ((button & 0xF) == 0 || (but & gPad->lastDigitals) == 0 || (gPad->lastDigitals & 0xF) == (button & 0xF))
                            && (but & gPad->lastDigitals) != 0;
        }
    }
    else if ( (button & 0x20000000) != 0 )
    {
        down = gPad->analogs[button & 0xDFFFFFFF] > 0.0;
        lastDown = gPad->lastAnalogs[button & 0xDFFFFFFF] > 0.0;
    }
    return down && !lastDown;
}

bool __cdecl GPad_IsButtonReleased(int portIndex, GamePadButton button)
{
    GamePad *gPad; // [esp+Ch] [ebp-Ch]
    bool lastDown; // [esp+16h] [ebp-2h]
    bool down; // [esp+17h] [ebp-1h]

    down = 0;
    lastDown = 0;
    if ( portIndex
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp",
                    585,
                    0,
                    "%s",
                    "( portIndex >= 0 ) && ( portIndex < MAX_GPAD_COUNT )") )
    {
        __debugbreak();
    }
    if ( (button & 0x30000000) == 0
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp",
                    586,
                    0,
                    "%s",
                    "button & ( GPAD_DIGITAL_MASK | GPAD_ANALOG_MASK )") )
    {
        __debugbreak();
    }
    gPad = &s_gamePads[portIndex];
    if ( (button & 0x10000000) != 0 )
    {
        down = (button & 0xEFFFFFFF & gPad->digitals) != 0;
        lastDown = (button & 0xEFFFFFFF & gPad->lastDigitals) != 0;
    }
    else if ( (button & 0x20000000) != 0 )
    {
        down = gPad->analogs[button & 0xDFFFFFFF] > 0.0;
        lastDown = gPad->lastAnalogs[button & 0xDFFFFFFF] > 0.0;
    }
    return !down && lastDown;
}

double __cdecl GPad_GetStick(int portIndex, GamePadStick stick)
{
    if ( portIndex
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp",
                    615,
                    0,
                    "%s",
                    "( portIndex >= 0 ) && ( portIndex < MAX_GPAD_COUNT )") )
    {
        __debugbreak();
    }
    if ( (stick & 0x40000000) == 0
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp",
                    616,
                    0,
                    "%s",
                    "stick & GPAD_STICK_MASK") )
    {
        __debugbreak();
    }
    return s_gamePads[portIndex].sticks[stick];
}

bool __cdecl GPad_IsStickPressed(int portIndex, GamePadStick stick, GamePadStickDir stickDir)
{
    return GPad_GetStickChangedToPressedState(portIndex, stick, stickDir, 1);
}

bool __cdecl GPad_GetStickChangedToPressedState(
                int portIndex,
                GamePadStick stick,
                GamePadStickDir stickDir,
                bool pressedState)
{
    GamePad *gPad; // [esp+4h] [ebp-10h]
    unsigned __int32 stickIndex; // [esp+Ch] [ebp-8h]
    bool down; // [esp+13h] [ebp-1h]

    if ( portIndex
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp",
                    663,
                    0,
                    "%s",
                    "( portIndex >= 0 ) && ( portIndex < MAX_GPAD_COUNT )") )
    {
        __debugbreak();
    }
    if ( (stick & 0x40000000) == 0
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp",
                    664,
                    0,
                    "%s",
                    "stick & GPAD_STICK_MASK") )
    {
        __debugbreak();
    }
    if ( (unsigned int)stickDir >= GPAD_STICK_DIRCOUNT
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp",
                    665,
                    0,
                    "%s",
                    "( stickDir == GPAD_STICK_POS ) || ( stickDir == GPAD_STICK_NEG )") )
    {
        __debugbreak();
    }
    gPad = &s_gamePads[portIndex];
    stickIndex = stick & 0xBFFFFFFF;
    if ( (stick & 0xBFFFFFFF) >= 4
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp",
                    669,
                    0,
                    "%s",
                    "(unsigned)stickIndex < GPAD_STICK_COUNT") )
    {
        __debugbreak();
    }
    down = gPad->stickDown[stickIndex][stickDir];
    return down != gPad->stickDownLast[stickIndex][stickDir] && down == pressedState;
}

bool __cdecl GPad_IsStickReleased(int portIndex, GamePadStick stick, GamePadStickDir stickDir)
{
    return GPad_GetStickChangedToPressedState(portIndex, stick, stickDir, 0);
}

