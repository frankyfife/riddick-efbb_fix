// Display mode / window handling for RndrGL.dll
//
// * RndrGL enumerates at most 512 display modes; modern monitors report 600+
//   (every resolution x refresh x scaling combination), so 4K is never seen.
//   We present a curated list: one entry per resolution (32 bpp) carrying the
//   highest refresh rate, sorted ascending.
// * A mode change to the resolution the desktop already uses is skipped, so the
//   native resolution behaves like borderless fullscreen (instant alt-tab).
// * The resolution the engine starts with is written to Environment.cfg
//   (VID_MODE) before the engine reads it, see Display_PrepareConfig().
#include "common.h"
#include <vector>
#include <algorithm>
#include <string>

static std::vector<DEVMODEA> g_modes;
static DEVMODEA g_desktop;

static void BuildModeList()
{
    g_desktop.dmSize = sizeof(DEVMODEA);
    EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &g_desktop);
    std::vector<DEVMODEA> all;
    DEVMODEA dm = {}; dm.dmSize = sizeof(dm);
    for (DWORD i = 0; EnumDisplaySettingsA(nullptr, i, &dm); ++i) {
        if (dm.dmBitsPerPel != 32 || dm.dmPelsWidth < 640 || dm.dmPelsHeight < 480) continue;
        if (dm.dmDisplayFixedOutput != DMDFO_DEFAULT && (dm.dmFields & DM_DISPLAYFIXEDOUTPUT)) continue;
        all.push_back(dm);
    }
    for (auto& m : all) {
        auto it = std::find_if(g_modes.begin(), g_modes.end(), [&](const DEVMODEA& x) {
            return x.dmPelsWidth == m.dmPelsWidth && x.dmPelsHeight == m.dmPelsHeight; });
        if (it == g_modes.end()) g_modes.push_back(m);
        else if (m.dmDisplayFrequency > it->dmDisplayFrequency) *it = m;
    }
    std::sort(g_modes.begin(), g_modes.end(), [](const DEVMODEA& a, const DEVMODEA& b) {
        return a.dmPelsWidth != b.dmPelsWidth ? a.dmPelsWidth < b.dmPelsWidth : a.dmPelsHeight < b.dmPelsHeight; });
    // the desktop mode keeps the desktop refresh rate (no needless mode switch)
    for (auto& m : g_modes)
        if (m.dmPelsWidth == g_desktop.dmPelsWidth && m.dmPelsHeight == g_desktop.dmPelsHeight)
            m.dmDisplayFrequency = g_desktop.dmDisplayFrequency;
    Log("display: desktop %lux%lu@%lu, %zu system modes -> %zu curated modes",
        g_desktop.dmPelsWidth, g_desktop.dmPelsHeight, g_desktop.dmDisplayFrequency, all.size(), g_modes.size());
}

static BOOL WINAPI Hook_EnumDisplaySettingsA(LPCSTR dev, DWORD idx, DEVMODEA* dm)
{
    if (dev || idx == ENUM_CURRENT_SETTINGS || idx == ENUM_REGISTRY_SETTINGS)
        return EnumDisplaySettingsA(dev, idx, dm);
    if (idx >= g_modes.size()) return FALSE;
    WORD size = dm->dmSize, extra = dm->dmDriverExtra;
    memcpy(dm, &g_modes[idx], std::min<size_t>(size ? size : sizeof(DEVMODEA), sizeof(DEVMODEA)));
    dm->dmSize = size; dm->dmDriverExtra = extra;
    return TRUE;
}

static bool g_modeChanged;

static LONG WINAPI Hook_ChangeDisplaySettingsA(DEVMODEA* dm, DWORD flags)
{
    if (!dm) {   // restore
        Log("display: restore desktop mode (changed=%d)", g_modeChanged);
        if (!g_modeChanged) return DISP_CHANGE_SUCCESSFUL;
        g_modeChanged = false;
        return ChangeDisplaySettingsA(nullptr, flags);
    }
    Log("display: engine requests %lux%lu %lubpp @%luHz flags %lx", dm->dmPelsWidth, dm->dmPelsHeight,
        dm->dmBitsPerPel, dm->dmDisplayFrequency, flags);
    if (g_cfg.borderless && dm->dmPelsWidth == g_desktop.dmPelsWidth && dm->dmPelsHeight == g_desktop.dmPelsHeight) {
        if (g_modeChanged) { ChangeDisplaySettingsA(nullptr, 0); g_modeChanged = false; }
        Log("display: native resolution, no mode switch (Borderless=1)");
        return DISP_CHANGE_SUCCESSFUL;
    }
    // other resolutions: real fullscreen mode switch with the best refresh rate
    for (auto& m : g_modes)
        if (m.dmPelsWidth == dm->dmPelsWidth && m.dmPelsHeight == dm->dmPelsHeight) {
            dm->dmDisplayFrequency = g_cfg.refresh ? (DWORD)g_cfg.refresh : m.dmDisplayFrequency;
            dm->dmFields |= DM_DISPLAYFREQUENCY;
        }
    LONG r = ChangeDisplaySettingsA(dm, flags | CDS_FULLSCREEN);
    if (r != DISP_CHANGE_SUCCESSFUL) {
        dm->dmFields &= ~DM_DISPLAYFREQUENCY;
        r = ChangeDisplaySettingsA(dm, flags | CDS_FULLSCREEN);
    }
    g_modeChanged = (r == DISP_CHANGE_SUCCESSFUL);
    Log("display: mode switch result %ld", r);
    return r;
}

static HWND WINAPI Hook_CreateWindowExA(DWORD ex, LPCSTR cls, LPCSTR title, DWORD style, int x, int y, int w, int h,
                                         HWND parent, HMENU menu, HINSTANCE inst, LPVOID param)
{
    HWND r = CreateWindowExA(ex, cls, title, style, x, y, w, h, parent, menu, inst, param);
    RECT rc = {}; GetClientRect(r, &rc);
    Log("display: CreateWindowExA style %lx ex %lx at %d,%d size %dx%d -> client %ldx%ld, dpi %u",
        style, ex, x, y, w, h, rc.right, rc.bottom, GetDpiForWindow(r));
    return r;
}

void Display_PatchModule(HMODULE rndr)
{
    PatchIAT(rndr, "user32.dll", "CreateWindowExA", (void*)Hook_CreateWindowExA);
    if (g_modes.empty()) BuildModeList();
    PatchIAT(rndr, "user32.dll", "EnumDisplaySettingsA", (void*)Hook_EnumDisplaySettingsA);
    PatchIAT(rndr, "user32.dll", "ChangeDisplaySettingsA", (void*)Hook_ChangeDisplaySettingsA);
}

// Set KEY=value in <game>\Environment.cfg (binary, CRLF kept) before the engine reads it.
static void SetCfgKey(const char* key, const char* value)
{
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\Environment.cfg", g_gameRoot);
    std::string text;
    if (FILE* f = _wfopen(path, L"rb")) {
        char buf[4096]; size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
        fclose(f);
    }
    char line[128];
    sprintf_s(line, "%s=%s", key, value);
    size_t klen = strlen(key);
    std::string out; bool found = false;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t e = text.find('\n', pos);
        std::string l = text.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
        pos = (e == std::string::npos) ? text.size() : e + 1;
        if (!l.empty() && l.back() == '\r') l.pop_back();
        if (_strnicmp(l.c_str(), key, klen) == 0 && l.size() > klen && l[klen] == '=') { l = line; found = true; }
        if (l.empty() && pos >= text.size()) break;
        out += l; out += "\r\n";
    }
    if (!found) { out += line; out += "\r\n"; }
    if (out != text)
        if (FILE* f = _wfopen(path, L"wb")) { fwrite(out.data(), 1, out.size(), f); fclose(f); }
    Log("cfg: %s", line);
}

static std::string GetCfgKey(const char* key)
{
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\Environment.cfg", g_gameRoot);
    std::string res;
    if (FILE* f = _wfopen(path, L"rb")) {
        char line[512]; size_t klen = strlen(key);
        while (fgets(line, sizeof(line), f))
            if (_strnicmp(line, key, klen) == 0 && line[klen] == '=') {
                res = line + klen + 1;
                while (!res.empty() && (res.back() == '\r' || res.back() == '\n')) res.pop_back();
            }
        fclose(f);
    }
    return res;
}

float g_uiSqueeze = 1.0f;
int g_screenW, g_screenH;

void Display_PrepareConfig()
{
    if (g_cfg.language[0]) {
        char lang[16]; WideCharToMultiByte(CP_ACP, 0, g_cfg.language, -1, lang, 16, nullptr, nullptr);
        SetCfgKey("LANGUAGE", lang);
    }
    // shader mode 6 = "2.0++" (NV_fragment_program2, the GeForce 6 / SM3 path).
    // "Auto" in the game deliberately never picks it (see readme), so set it.
    int sm = g_cfg.shaderMode;
    if (sm == -2) {
        sm = -1;
        DISPLAY_DEVICEA dd = { sizeof(dd) };
        for (DWORD i = 0; EnumDisplayDevicesA(nullptr, i, &dd, 0); ++i)
            if ((dd.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) && strstr(dd.DeviceString, "NVIDIA")) sm = 6;
    }
    if (sm >= 0 && sm <= 6) { char v[8]; sprintf_s(v, "%d", sm); SetCfgKey("XR_SHADERMODE", v); }
    // model LOD: the in-game "model quality: high" is xr_lodoffset -1000000
    // (always the most detailed LOD); "normal" (0) swaps models down early.
    if (g_cfg.modelLod == 1) SetCfgKey("xr_lodoffset", "-1000000.000000");
    else if (g_cfg.modelLod == 0) SetCfgKey("xr_lodoffset", "0.000000");
    DEVMODEA d = {}; d.dmSize = sizeof(d);
    EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &d);
    int w = g_cfg.width ? g_cfg.width : (int)d.dmPelsWidth;
    int h = g_cfg.height ? g_cfg.height : (int)d.dmPelsHeight;
    if (g_cfg.resMode) {
        int hz = g_cfg.refresh ? g_cfg.refresh : (int)d.dmDisplayFrequency;
        char val[64];
        sprintf_s(val, "%d %d 32 %d", w, h, hz);
        SetCfgKey("VID_MODE", val);
    } else {
        // "game": leave the in-game choice alone, but read it for the UI aspect
        std::string v = GetCfgKey("VID_MODE");
        int gw = 0, gh = 0;
        if (sscanf_s(v.c_str(), "%d %d", &gw, &gh) == 2 && gw > 0 && gh > 0) { w = gw; h = gh; }
    }
    g_screenW = w; g_screenH = h;
    // The 2D layer is a fixed 640x480 canvas scaled to the screen, so on a wide
    // screen everything is stretched by (w/h)/(4/3). Text and icons are drawn
    // pre-squeezed by the inverse (fontgen/guigen) so they end up square.
    if (g_cfg.uiAspect && h > 0) {
        float f = (4.0f / 3.0f) / ((float)w / (float)h);
        if (f > 0.99f && f < 1.01f) f = 1.0f;
        g_uiSqueeze = std::min(1.25f, std::max(0.4f, f));
    }
    Log("display: screen %dx%d, UI squeeze %.4f", w, h, g_uiSqueeze);
}
