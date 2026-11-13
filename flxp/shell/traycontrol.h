// The notification area's own controls, as luxshell's tray tiles have them (docs/SHELL-DESIGN.md 8.6): the volume of the
// default sink, the screen backlight and the touchpad, read and set through the same programs and files (wpctl, pactl or
// amixer, $LHSHELL_VOLUME forcing one; /sys/class/backlight or $LHSHELL_BACKLIGHT with brightnessctl or a write; xinput).
// Each call runs its commands and waits for them (a few ms): call from a worker thread. No win32 here.
#pragma once
#include <string>

namespace shell {
namespace tray {

struct Volume {
    bool ok = false;            // a backend answered
    int percent = 0;            // 0..100 (a level above 100 % is reported as it is)
    bool muted = false;
    std::string backend;        // wpctl | pactl | amixer
    std::string error;          // "no wpctl, pactl or amixer found", "pactl: <its first line>"
};
Volume read_volume();
bool set_volume(int percent);            // clamped to 0..100
bool set_mute(int on);                   // 1 mute, 0 unmute, -1 toggle

struct Backlight {
    bool present = false;       // a device under /sys/class/backlight (or $LHSHELL_BACKLIGHT)
    std::string device;
    int percent = 0;
    std::string error;          // "not writable (...)"
};
Backlight read_backlight();
bool set_backlight(int percent);         // at least 1 %: brightnessctl when installed, else a write to brightness

struct Touchpad {
    bool present = false;       // xinput lists a slave pointer named *touchpad* or *trackpad*
    bool enabled = false;
    int id = 0;
    std::string prop;           // "libinput Device Enabled" or "Device Enabled"
    std::string error;
};
Touchpad read_touchpad();
bool set_touchpad(bool enabled);

// the tooltips, as luxshell's: "Volume: 40%", "Volume: 40% (muted)", "Brightness: 60%", "Touchpad: on (click to turn it off)"
std::string volume_tip(const Volume& v);
std::string backlight_tip(const Backlight& b);
std::string touchpad_tip(const Touchpad& t);

} // namespace tray
} // namespace shell
