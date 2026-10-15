#pragma once

// Native Sony DualSense (PS5) / DualSense Edge support over HID (USB + Bluetooth).
// The pad is translated into an XINPUT_STATE so the rest of the gamepad
// pipeline (deadzones, bindings, menus) works unchanged.

#include <universal/dvar.h>
#include <XInput.h>

extern const dvar_t *ds_enabled;
extern const dvar_t *ds_connected;
extern const dvar_t *ds_lightbar_r;
extern const dvar_t *ds_lightbar_g;
extern const dvar_t *ds_lightbar_b;
extern const dvar_t *ds_lightbar_brightness;
extern const dvar_t *ds_player_led;
extern const dvar_t *ds_rumble_scale;
extern const dvar_t *ds_trigger_l_mode;
extern const dvar_t *ds_trigger_r_mode;
extern const dvar_t *ds_trigger_start;
extern const dvar_t *ds_trigger_strength;
extern const dvar_t *ds_touchpad_back;

void DS_Init();                                  // register dvars
void DS_Shutdown();                              // reset lightbar/triggers/rumble and close the device
void DS_Pump();                                  // scan / read / write; call once per frame
bool DS_IsConnected();
bool DS_GetState(_XINPUT_STATE *out);            // latest translated state, false if no DualSense
void DS_SetRumble(unsigned short low, unsigned short high); // 0..65535, same as XINPUT_VIBRATION
