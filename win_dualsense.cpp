#include <Windows.h>
#include <setupapi.h>
#include <string.h>

extern "C"
{
#include <hidsdi.h>
}
#include <hidpi.h>

#include "win_dualsense.h"
#include "win_gamepad.h"

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

const dvar_t *ds_enabled;
const dvar_t *ds_connected;
const dvar_t *ds_lightbar_r;
const dvar_t *ds_lightbar_g;
const dvar_t *ds_lightbar_b;
const dvar_t *ds_lightbar_brightness;
const dvar_t *ds_player_led;
const dvar_t *ds_rumble_scale;
const dvar_t *ds_trigger_l_mode;
const dvar_t *ds_trigger_r_mode;
const dvar_t *ds_trigger_start;
const dvar_t *ds_trigger_strength;
const dvar_t *ds_touchpad_back;

namespace
{
    const USHORT SONY_VID = 0x054C;
    const USHORT PID_DUALSENSE = 0x0CE6;
    const USHORT PID_DUALSENSE_EDGE = 0x0DF2;

    const DWORD RESCAN_INTERVAL_MS = 1000;
    const DWORD OUTPUT_MIN_INTERVAL_MS = 8;     // ~120 Hz cap on output reports
    const DWORD OUTPUT_KEEPALIVE_MS = 1000;     // re-send occasionally so state recovers after sleep

    // Trigger effect modes (right = offset 10, left = offset 21 inside the common block)
    const BYTE TRIGGER_MODE_OFF = 0x00;
    const BYTE TRIGGER_MODE_RESISTANCE = 0x01;  // params: [0] start position (0-9), [1] force (0-255)

    struct DsDevice
    {
        HANDLE handle;
        HANDLE event;
        OVERLAPPED ov;
        bool pending;
        bool bluetooth;
        USHORT inLen;
        USHORT outLen;
        BYTE inBuf[128];
        BYTE btSeq;
    };

    struct DsOutput
    {
        BYTE lowMotor;
        BYTE highMotor;
        BYTE r, g, b;
        BYTE brightness;
        BYTE playerLeds;
        BYTE trigL[11];
        BYTE trigR[11];
        bool valid;
        bool operator==(const DsOutput &o) const { return memcmp(this, &o, sizeof(DsOutput)) == 0; }
    };

    DsDevice s_dev;
    bool s_connected;
    bool s_haveState;
    _XINPUT_STATE s_state;
    DWORD s_lastScan;
    DWORD s_lastOutputTime;
    DsOutput s_lastOutput;
    unsigned short s_rumbleLow;
    unsigned short s_rumbleHigh;
    bool s_shuttingDown;

    DWORD NowMs()
    {
        return GetTickCount();
    }

    BYTE ClampByte(int v)
    {
        return (BYTE)(v < 0 ? 0 : (v > 255 ? 255 : v));
    }

    DWORD Crc32(DWORD seedByte, const BYTE *data, size_t len)
    {
        DWORD crc = 0xFFFFFFFFu;
        crc ^= seedByte;
        for (int k = 0; k < 8; ++k)
            crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
        for (size_t i = 0; i < len; ++i)
        {
            crc ^= data[i];
            for (int k = 0; k < 8; ++k)
                crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
        }
        return ~crc;
    }

    void CloseDevice()
    {
        if (s_dev.handle != INVALID_HANDLE_VALUE && s_dev.handle != NULL)
        {
            CancelIo(s_dev.handle);
            CloseHandle(s_dev.handle);
        }
        if (s_dev.event)
            CloseHandle(s_dev.event);
        memset(&s_dev, 0, sizeof(s_dev));
        s_dev.handle = INVALID_HANDLE_VALUE;
        s_connected = false;
        s_haveState = false;
        s_lastOutput.valid = false;
        memset(&s_state, 0, sizeof(s_state));
    }

    bool TryOpenDevice()
    {
        GUID hidGuid;
        HidD_GetHidGuid(&hidGuid);

        HDEVINFO devInfo = SetupDiGetClassDevs(&hidGuid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (devInfo == INVALID_HANDLE_VALUE)
            return false;

        bool opened = false;
        SP_DEVICE_INTERFACE_DATA ifData;
        ifData.cbSize = sizeof(ifData);

        for (DWORD index = 0; !opened && SetupDiEnumDeviceInterfaces(devInfo, NULL, &hidGuid, index, &ifData); ++index)
        {
            DWORD needed = 0;
            SetupDiGetDeviceInterfaceDetail(devInfo, &ifData, NULL, 0, &needed, NULL);
            if (!needed)
                continue;

            SP_DEVICE_INTERFACE_DETAIL_DATA *detail = (SP_DEVICE_INTERFACE_DETAIL_DATA *)LocalAlloc(LPTR, needed);
            if (!detail)
                continue;
            detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

            if (SetupDiGetDeviceInterfaceDetail(devInfo, &ifData, detail, needed, NULL, NULL))
            {
                HANDLE h = CreateFile(
                    detail->DevicePath,
                    GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL,
                    OPEN_EXISTING,
                    FILE_FLAG_OVERLAPPED,
                    NULL);

                if (h != INVALID_HANDLE_VALUE)
                {
                    HIDD_ATTRIBUTES attr;
                    attr.Size = sizeof(attr);
                    bool good = false;

                    if (HidD_GetAttributes(h, &attr) && attr.VendorID == SONY_VID
                        && (attr.ProductID == PID_DUALSENSE || attr.ProductID == PID_DUALSENSE_EDGE))
                    {
                        PHIDP_PREPARSED_DATA pp = NULL;
                        HIDP_CAPS caps;
                        if (HidD_GetPreparsedData(h, &pp))
                        {
                            if (HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS)
                            {
                                // USB: 64-byte input (report 0x01).  Bluetooth: 78-byte input (report 0x31).
                                if (caps.InputReportByteLength == 64 || caps.InputReportByteLength == 78)
                                {
                                    memset(&s_dev, 0, sizeof(s_dev));
                                    s_dev.handle = h;
                                    s_dev.inLen = caps.InputReportByteLength;
                                    s_dev.outLen = caps.OutputReportByteLength;
                                    s_dev.bluetooth = (caps.InputReportByteLength == 78);
                                    s_dev.event = CreateEvent(NULL, TRUE, FALSE, NULL);
                                    s_dev.ov.hEvent = s_dev.event;
                                    good = (s_dev.event != NULL) && s_dev.outLen >= 48 && s_dev.outLen <= 128;
                                }
                            }
                            HidD_FreePreparsedData(pp);
                        }
                    }

                    if (good)
                    {
                        if (s_dev.bluetooth)
                        {
                            // Reading feature report 0x05 switches a Bluetooth pad from the
                            // reduced 0x01 report to the full 0x31 report.
                            BYTE feat[41];
                            memset(feat, 0, sizeof(feat));
                            feat[0] = 0x05;
                            HidD_GetFeature(h, feat, sizeof(feat));
                        }
                        opened = true;
                        s_connected = true;
                        s_lastOutput.valid = false;
                    }
                    else
                    {
                        if (s_dev.event)
                        {
                            CloseHandle(s_dev.event);
                            s_dev.event = NULL;
                        }
                        CloseHandle(h);
                        s_dev.handle = INVALID_HANDLE_VALUE;
                    }
                }
            }
            LocalFree(detail);
        }

        SetupDiDestroyDeviceInfoList(devInfo);
        return opened;
    }

    short StickToShort(BYTE v, bool invert)
    {
        int centered = (int)v - 128;
        if (invert)
            centered = -centered - 1;      // DualSense: up = 0, XInput: up = positive
        int out = (centered * 32767) / 127;
        if (out > 32767) out = 32767;
        if (out < -32767) out = -32767;
        return (short)out;
    }

    void ParseInput(const BYTE *buf, DWORD len)
    {
        int base;
        if (s_dev.bluetooth)
        {
            if (len < 12 || buf[0] != 0x31)
                return;
            base = 2;
        }
        else
        {
            if (len < 11 || buf[0] != 0x01)
                return;
            base = 1;
        }

        const BYTE lx = buf[base + 0];
        const BYTE ly = buf[base + 1];
        const BYTE rx = buf[base + 2];
        const BYTE ry = buf[base + 3];
        const BYTE l2 = buf[base + 4];
        const BYTE r2 = buf[base + 5];
        const BYTE b0 = buf[base + 7];
        const BYTE b1 = buf[base + 8];
        const BYTE b2 = buf[base + 9];

        WORD buttons = 0;

        switch (b0 & 0x0F) // hat switch
        {
        case 0: buttons |= XINPUT_GAMEPAD_DPAD_UP; break;
        case 1: buttons |= XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_RIGHT; break;
        case 2: buttons |= XINPUT_GAMEPAD_DPAD_RIGHT; break;
        case 3: buttons |= XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_RIGHT; break;
        case 4: buttons |= XINPUT_GAMEPAD_DPAD_DOWN; break;
        case 5: buttons |= XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT; break;
        case 6: buttons |= XINPUT_GAMEPAD_DPAD_LEFT; break;
        case 7: buttons |= XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_LEFT; break;
        default: break;
        }

        if (b0 & 0x10) buttons |= XINPUT_GAMEPAD_X; // Square
        if (b0 & 0x20) buttons |= XINPUT_GAMEPAD_A; // Cross
        if (b0 & 0x40) buttons |= XINPUT_GAMEPAD_B; // Circle
        if (b0 & 0x80) buttons |= XINPUT_GAMEPAD_Y; // Triangle

        if (b1 & 0x01) buttons |= XINPUT_GAMEPAD_LEFT_SHOULDER;
        if (b1 & 0x02) buttons |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
        if (b1 & 0x10) buttons |= XINPUT_GAMEPAD_BACK;  // Create
        if (b1 & 0x20) buttons |= XINPUT_GAMEPAD_START; // Options
        if (b1 & 0x40) buttons |= XINPUT_GAMEPAD_LEFT_THUMB;
        if (b1 & 0x80) buttons |= XINPUT_GAMEPAD_RIGHT_THUMB;

        if (ds_touchpad_back && ds_touchpad_back->current.enabled && (b2 & 0x02))
            buttons |= XINPUT_GAMEPAD_BACK; // touchpad click

        s_state.Gamepad.wButtons = buttons;
        s_state.Gamepad.bLeftTrigger = l2;
        s_state.Gamepad.bRightTrigger = r2;
        s_state.Gamepad.sThumbLX = StickToShort(lx, false);
        s_state.Gamepad.sThumbLY = StickToShort(ly, true);
        s_state.Gamepad.sThumbRX = StickToShort(rx, false);
        s_state.Gamepad.sThumbRY = StickToShort(ry, true);
        s_state.dwPacketNumber++;
        s_haveState = true;
    }

    void ReadInput()
    {
        for (int guard = 0; guard < 32; ++guard)
        {
            if (!s_dev.pending)
            {
                ResetEvent(s_dev.event);
                DWORD ignored = 0;
                if (!ReadFile(s_dev.handle, s_dev.inBuf, s_dev.inLen, &ignored, &s_dev.ov)
                    && GetLastError() != ERROR_IO_PENDING)
                {
                    CloseDevice();
                    return;
                }
                s_dev.pending = true;
            }

            DWORD got = 0;
            if (!GetOverlappedResult(s_dev.handle, &s_dev.ov, &got, FALSE))
            {
                if (GetLastError() == ERROR_IO_INCOMPLETE)
                    return; // nothing new this frame
                CloseDevice();
                return;
            }
            s_dev.pending = false;
            ParseInput(s_dev.inBuf, got);
        }
    }

    void BuildTrigger(BYTE *dst, int mode, int start, int strength)
    {
        memset(dst, 0, 11);
        if (mode == 1)
        {
            dst[0] = TRIGGER_MODE_RESISTANCE;
            dst[1] = ClampByte(start < 0 ? 0 : (start > 9 ? 9 : start));
            // strength setting is 0-8, map to the 0-255 force byte
            const int s = strength < 0 ? 0 : (strength > 8 ? 8 : strength);
            dst[2] = ClampByte((s * 255) / 8);
        }
        else
        {
            dst[0] = TRIGGER_MODE_OFF;
        }
    }

    void BuildDesiredOutput(DsOutput *out)
    {
        memset(out, 0, sizeof(*out));

        const bool rumbleOn = !s_shuttingDown && (gpad_rumble && gpad_rumble->current.enabled);
        float scale = ds_rumble_scale ? ds_rumble_scale->current.value : 1.0f;
        if (scale < 0.0f) scale = 0.0f;
        if (scale > 1.0f) scale = 1.0f;

        if (rumbleOn)
        {
            out->lowMotor = ClampByte((int)((s_rumbleLow >> 8) * scale));
            out->highMotor = ClampByte((int)((s_rumbleHigh >> 8) * scale));
        }

        if (s_shuttingDown)
        {
            out->r = out->g = out->b = 0;
            out->playerLeds = 0;
        }
        else
        {
            const int br = ds_lightbar_brightness ? ds_lightbar_brightness->current.integer : 0; // 0 = high, 2 = low
            out->r = ClampByte(ds_lightbar_r->current.integer);
            out->g = ClampByte(ds_lightbar_g->current.integer);
            out->b = ClampByte(ds_lightbar_b->current.integer);
            out->brightness = ClampByte(br < 0 ? 0 : (br > 2 ? 2 : br));
            out->playerLeds = ClampByte(ds_player_led->current.integer & 0x1F);

            const int start = ds_trigger_start->current.integer;
            const int strength = ds_trigger_strength->current.integer;
            BuildTrigger(out->trigL, ds_trigger_l_mode->current.integer, start, strength);
            BuildTrigger(out->trigR, ds_trigger_r_mode->current.integer, start, strength);
        }
        out->valid = true;
    }

    void SendOutput(const DsOutput &o)
    {
        BYTE buf[128];
        memset(buf, 0, sizeof(buf));

        int base;
        if (s_dev.bluetooth)
        {
            buf[0] = 0x31;
            buf[1] = (BYTE)((s_dev.btSeq++ & 0x0F) << 4);
            buf[2] = 0x10; // tag
            base = 3;
        }
        else
        {
            buf[0] = 0x02;
            base = 1;
        }

        BYTE *c = buf + base;
        c[0] = 0x01 | 0x02 | 0x04 | 0x08;   // rumble, haptics select, R2 effect, L2 effect
        c[1] = 0x04 | 0x10;                 // lightbar, player LEDs
        c[2] = o.highMotor;                 // right (weak) motor
        c[3] = o.lowMotor;                  // left (strong) motor
        memcpy(c + 10, o.trigR, 11);
        memcpy(c + 21, o.trigL, 11);
        c[38] = 0x02 | 0x04;                // lightbar setup control, improved rumble
        c[41] = 0x02;                       // release the default pulsing lightbar
        c[42] = o.brightness;
        c[43] = o.playerLeds;
        c[44] = o.r;
        c[45] = o.g;
        c[46] = o.b;

        if (s_dev.bluetooth)
        {
            const DWORD crc = Crc32(0xA2, buf, 74);
            buf[74] = (BYTE)(crc & 0xFF);
            buf[75] = (BYTE)((crc >> 8) & 0xFF);
            buf[76] = (BYTE)((crc >> 16) & 0xFF);
            buf[77] = (BYTE)((crc >> 24) & 0xFF);
        }

        // Synchronous, tiny write. If it fails the read side will notice the pad is gone.
        OVERLAPPED wov;
        memset(&wov, 0, sizeof(wov));
        wov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (!wov.hEvent)
            return;
        DWORD written = 0;
        if (!WriteFile(s_dev.handle, buf, s_dev.outLen, &written, &wov) && GetLastError() == ERROR_IO_PENDING)
            WaitForSingleObject(wov.hEvent, 20);
        CancelIoEx(s_dev.handle, &wov);
        CloseHandle(wov.hEvent);
    }

    void WriteOutputIfNeeded(bool force)
    {
        const DWORD now = NowMs();
        if (!force && now - s_lastOutputTime < OUTPUT_MIN_INTERVAL_MS)
            return;

        DsOutput desired;
        BuildDesiredOutput(&desired);

        if (!force && s_lastOutput.valid && desired == s_lastOutput && now - s_lastOutputTime < OUTPUT_KEEPALIVE_MS)
            return;

        SendOutput(desired);
        s_lastOutput = desired;
        s_lastOutputTime = now;
    }

    void UpdateConnectedDvar(bool connected)
    {
        static int last = -1;
        if (last != (connected ? 1 : 0) && ds_connected)
        {
            last = connected ? 1 : 0;
            Dvar_SetBoolByName("ds_connected", connected);
        }
    }
}

void DS_Init()
{
    s_dev.handle = INVALID_HANDLE_VALUE;
    s_shuttingDown = false;

    ds_enabled = _Dvar_RegisterBool("ds_enabled", 1, 1u, "Use native DualSense support (HID) when a DualSense is connected");
    ds_connected = _Dvar_RegisterBool("ds_connected", 0, 0, "A DualSense is currently connected");
    ds_lightbar_r = _Dvar_RegisterInt("ds_lightbar_r", 0, 0, 255, 1u, "DualSense lightbar red (0-255)");
    ds_lightbar_g = _Dvar_RegisterInt("ds_lightbar_g", 64, 0, 255, 1u, "DualSense lightbar green (0-255)");
    ds_lightbar_b = _Dvar_RegisterInt("ds_lightbar_b", 255, 0, 255, 1u, "DualSense lightbar blue (0-255)");
    ds_lightbar_brightness = _Dvar_RegisterInt("ds_lightbar_brightness", 0, 0, 2, 1u, "DualSense lightbar brightness: 0 = high, 1 = medium, 2 = low");
    ds_player_led = _Dvar_RegisterInt("ds_player_led", 4, 0, 31, 1u, "DualSense player LEDs bitmask (0 = off, 4 = center, 31 = all)");
    ds_rumble_scale = _Dvar_RegisterFloat("ds_rumble_scale", 1.0f, 0.0f, 1.0f, 1u, "DualSense rumble strength multiplier");
    ds_trigger_l_mode = _Dvar_RegisterInt("ds_trigger_l_mode", 0, 0, 1, 1u, "L2 adaptive trigger: 0 = off, 1 = resistance");
    ds_trigger_r_mode = _Dvar_RegisterInt("ds_trigger_r_mode", 0, 0, 1, 1u, "R2 adaptive trigger: 0 = off, 1 = resistance");
    ds_trigger_start = _Dvar_RegisterInt("ds_trigger_start", 2, 0, 9, 1u, "Adaptive trigger: where resistance begins in the pull (0-9)");
    ds_trigger_strength = _Dvar_RegisterInt("ds_trigger_strength", 4, 0, 8, 1u, "Adaptive trigger resistance strength (0-8)");
    ds_touchpad_back = _Dvar_RegisterBool("ds_touchpad_back", 1, 1u, "Touchpad click acts as the Back/Select button");
}

void DS_Shutdown()
{
    if (s_connected)
    {
        s_shuttingDown = true;
        WriteOutputIfNeeded(true);
    }
    CloseDevice();
    UpdateConnectedDvar(false);
}

void DS_Pump()
{
    if (!ds_enabled || !ds_enabled->current.enabled)
    {
        if (s_connected)
        {
            s_shuttingDown = true;
            WriteOutputIfNeeded(true);
            s_shuttingDown = false;
            CloseDevice();
        }
        UpdateConnectedDvar(false);
        return;
    }

    if (!s_connected)
    {
        const DWORD now = NowMs();
        if (now - s_lastScan >= RESCAN_INTERVAL_MS)
        {
            s_lastScan = now;
            TryOpenDevice();
        }
    }

    if (s_connected)
    {
        ReadInput();
        if (s_connected)
            WriteOutputIfNeeded(false);
    }

    UpdateConnectedDvar(s_connected);
}

bool DS_IsConnected()
{
    return s_connected;
}

bool DS_GetState(_XINPUT_STATE *out)
{
    if (!s_connected || !s_haveState)
        return false;
    *out = s_state;
    return true;
}

void DS_SetRumble(unsigned short low, unsigned short high)
{
    s_rumbleLow = low;
    s_rumbleHigh = high;
}
