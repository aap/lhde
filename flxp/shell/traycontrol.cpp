// The tray's own controls through the same programs and files as luxshell's tray tiles (traycontrol.h).
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <string>
#include <vector>
#include "traycontrol.h"

namespace shell {
namespace tray {

namespace {

std::string quote(const std::string& s)
{
    std::string o = "'";
    for (char c : s) { if (c == '\'') o += "'\\''"; else o += c; }
    return o + "'";
}

// a command's standard output (stderr dropped); false when it could not run or exited non-zero
bool run(const std::string& cmd, std::string* out = nullptr)
{
    FILE* f = popen((cmd + " 2>/dev/null").c_str(), "r");
    if (!f) return false;
    std::string s;
    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    int st = pclose(f);
    if (out) *out = s;
    return st == 0;
}

bool have(const std::string& prog)
{
    const char* path = getenv("PATH");
    std::string p = path ? path : "/usr/bin:/bin";
    size_t i = 0;
    while (i <= p.size()) {
        size_t j = p.find(':', i);
        if (j == std::string::npos) j = p.size();
        std::string d = p.substr(i, j - i);
        if (!d.empty() && access((d + "/" + prog).c_str(), X_OK) == 0) return true;
        i = j + 1;
    }
    return false;
}

const char* SINK_W = "@DEFAULT_AUDIO_SINK@";
const char* SINK_P = "@DEFAULT_SINK@";

// the volume backend: $LHSHELL_VOLUME, else the first of wpctl, pactl, amixer that is installed
std::string backend(std::string* err)
{
    const char* f = getenv("LHSHELL_VOLUME");
    if (f && *f) {
        if (have(f)) return f;
        if (err) *err = std::string(f) + " not found";
        return "";
    }
    for (const char* b : { "wpctl", "pactl", "amixer" }) if (have(b)) return b;
    if (err) *err = "no wpctl, pactl or amixer found";
    return "";
}

std::string read_file(const std::string& path)
{
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return "";
    char buf[256];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    std::string s = buf;
    while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
    return s;
}

std::string backlight_dir()
{
    const char* e = getenv("LHSHELL_BACKLIGHT");
    return e && *e ? e : "/sys/class/backlight";
}

// the device the kernel ranks first: type firmware, else platform, else raw
std::string backlight_device()
{
    DIR* d = opendir(backlight_dir().c_str());
    if (!d) return "";
    std::string best;
    int rank = 9;
    while (struct dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        std::string t = read_file(backlight_dir() + "/" + e->d_name + "/type");
        int r = t == "firmware" ? 0 : t == "platform" ? 1 : t == "raw" ? 2 : 3;
        if (r < rank || (r == rank && e->d_name < best)) { rank = r; best = e->d_name; }
    }
    closedir(d);
    return best;
}

std::string first_line(const std::string& s)
{
    size_t n = s.find('\n');
    return n == std::string::npos ? s : s.substr(0, n);
}

} // namespace

Volume read_volume()
{
    Volume v;
    std::string b = backend(&v.error), out;
    if (b.empty()) return v;
    v.backend = b;
    if (b == "wpctl") {
        if (!run(std::string("wpctl get-volume ") + SINK_W, &out)) { v.error = "wpctl: " + first_line(out); return v; }
        size_t p = out.find("Volume:");
        if (p == std::string::npos) { v.error = "wpctl: " + first_line(out); return v; }
        v.percent = (int)(atof(out.c_str() + p + 7) * 100 + 0.5);
        v.muted = out.find("[MUTED]") != std::string::npos;
    } else if (b == "pactl") {
        std::string m;
        if (!run(std::string("pactl get-sink-volume ") + SINK_P, &out)) { v.error = "pactl: " + first_line(out); return v; }
        size_t pc = out.find('%');
        if (pc == std::string::npos) { v.error = "pactl: " + first_line(out); return v; }
        size_t s = pc;
        while (s > 0 && isdigit((unsigned char)out[s - 1])) s--;
        v.percent = atoi(out.c_str() + s);
        run(std::string("pactl get-sink-mute ") + SINK_P, &m);
        v.muted = m.find("yes") != std::string::npos;
    } else {
        if (!run("amixer get Master", &out)) { v.error = "amixer: " + first_line(out); return v; }
        size_t p = out.find("[");
        while (p != std::string::npos && !isdigit((unsigned char)out[p + 1])) p = out.find("[", p + 1);
        if (p == std::string::npos) { v.error = "amixer: no Master"; return v; }
        v.percent = atoi(out.c_str() + p + 1);
        v.muted = out.find("[off]") != std::string::npos;
    }
    v.ok = true;
    return v;
}

bool set_volume(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    std::string b = backend(nullptr);
    char cmd[160];
    if (b == "wpctl") snprintf(cmd, sizeof cmd, "wpctl set-volume %s %.2f", SINK_W, percent / 100.0);
    else if (b == "pactl") snprintf(cmd, sizeof cmd, "pactl set-sink-volume %s %d%%", SINK_P, percent);
    else if (b == "amixer") snprintf(cmd, sizeof cmd, "amixer -q set Master %d%%", percent);
    else return false;
    return run(cmd);
}

bool set_mute(int on)
{
    std::string b = backend(nullptr);
    std::string arg = on < 0 ? "toggle" : on ? "1" : "0";
    if (b == "wpctl") return run(std::string("wpctl set-mute ") + SINK_W + " " + arg);
    if (b == "pactl") return run(std::string("pactl set-sink-mute ") + SINK_P + " " + arg);
    if (b == "amixer") return run("amixer -q set Master " + std::string(on < 0 ? "toggle" : on ? "mute" : "unmute"));
    return false;
}

Backlight read_backlight()
{
    Backlight b;
    b.device = backlight_device();
    if (b.device.empty()) return b;
    std::string dir = backlight_dir() + "/" + b.device;
    int cur = atoi(read_file(dir + "/brightness").c_str()), max = atoi(read_file(dir + "/max_brightness").c_str());
    if (max <= 0) return b;
    b.present = true;
    b.percent = (cur * 100 + max / 2) / max;
    if (!have("brightnessctl") && access((dir + "/brightness").c_str(), W_OK) != 0)
        b.error = "not writable (no brightnessctl, and " + dir + "/brightness is not ours to write)";
    return b;
}

bool set_backlight(int percent)
{
    std::string dev = backlight_device();
    if (dev.empty()) return false;
    if (percent < 1) percent = 1;
    if (percent > 100) percent = 100;
    std::string dir = backlight_dir() + "/" + dev;
    int max = atoi(read_file(dir + "/max_brightness").c_str());
    if (max <= 0) return false;
    int raw = (percent * max + 50) / 100;
    if (raw < 1) raw = 1;
    if (have("brightnessctl")) return run("brightnessctl -q -d " + quote(dev) + " set " + std::to_string(raw));
    FILE* f = fopen((dir + "/brightness").c_str(), "w");
    if (!f) return false;
    fprintf(f, "%d\n", raw);
    return fclose(f) == 0;
}

Touchpad read_touchpad()
{
    Touchpad t;
    std::string out;
    if (!have("xinput")) { t.error = "no xinput"; return t; }
    if (!run("xinput list", &out)) return t;
    size_t i = 0;
    while (i < out.size()) {
        size_t j = out.find('\n', i);
        if (j == std::string::npos) j = out.size();
        std::string l = out.substr(i, j - i), low = l;
        for (char& c : low) c = (char)tolower((unsigned char)c);
        i = j + 1;
        size_t idp = l.find("id=");
        size_t sl = low.find("[slave");
        if (idp == std::string::npos || sl == std::string::npos || low.find("pointer", sl) == std::string::npos) continue;
        if (low.find("touchpad") == std::string::npos && low.find("trackpad") == std::string::npos) continue;
        t.id = atoi(l.c_str() + idp + 3);
        break;
    }
    if (!t.id) return t;
    t.present = true;
    run("xinput list-props " + std::to_string(t.id), &out);
    for (const char* prop : { "libinput Device Enabled (", "Device Enabled (" }) {
        size_t p = out.find(prop);
        if (p == std::string::npos) continue;
        size_t c = out.find(':', p);
        if (c == std::string::npos) continue;
        t.enabled = atoi(out.c_str() + c + 1) != 0;
        t.prop = std::string(prop, strlen(prop) - 2);
        break;
    }
    return t;
}

bool set_touchpad(bool enabled)
{
    Touchpad t = read_touchpad();
    if (!t.present) return false;
    return run("xinput set-prop " + std::to_string(t.id) + " " + quote(t.prop.empty() ? "libinput Device Enabled" : t.prop) +
               (enabled ? " 1" : " 0"));
}

std::string volume_tip(const Volume& v)
{
    if (!v.ok) return "Volume: " + v.error;
    return "Volume: " + std::to_string(v.percent) + "%" + (v.muted ? " (muted)" : "");
}

std::string backlight_tip(const Backlight& b)
{
    return "Brightness: " + std::to_string(b.percent) + "%" + (b.error.empty() ? "" : " (" + b.error + ")");
}

std::string touchpad_tip(const Touchpad& t)
{
    return t.enabled ? "Touchpad: on (click to turn it off)" : "Touchpad: off (click to turn it on)";
}

} // namespace tray
} // namespace shell
