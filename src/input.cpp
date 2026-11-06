// Native Xbox controller support (XInput).
//
// The PC build already ships a complete gamepad profile (the "...2" action slots,
// ACTION_MOVE_*, ACTION_LOOK_*, ACTION_GUI_* - see SbzEngine 0x408b60), written for
// a 2004 DirectInput pad. Modern controllers through DirectInput report a merged
// trigger axis and a different axis order, so the profile never matches.
//
// We disable the engine's DirectInput joystick and feed a virtual pad with exactly
// the layout the profile expects into CInputContextCore (same calls and value
// semantics as MSystem's own CDI_Device_Joystick, see MSystem 0x1009c0b0/0x1009c340):
//
//   axis 0 (0x80/0x81)  right stick Y   (down/up)      -> LOOK_DOWN / LOOK_UP
//   axis 1 (0x82/0x83)  right stick X   (right/left)   -> LOOK_RIGHT / LOOK_LEFT
//   axis 2 (0x84/0x85)  left stick Y    (down/up)      -> MOVE_BACKWARD / MOVE_FORWARD
//   axis 3 (0x86/0x87)  left stick X    (right/left)   -> MOVE_RIGHT / MOVE_LEFT
//   axis 4 (0x88)       right trigger                  -> PRIMARYFIRE2
//   axis 5 (0x8A)       left trigger                   -> SECONDARYFIRE2
//   0xA0 A  jump / GUI ok        0xA1 B  reload / GUI cancel
//   0xA2 X  use                  0xA3 Y  cycle weapon
//   0xA4 LB flashlight           0xA5 RB zoom
//   0xA6 Back journal            0xA7 Start pause
//   0xA8 LS crouch               0xA9 RS night vision
//   0xB0..0xB3 d-pad up/right/down/left -> lean / GUI navigation
//
// That is the original Xbox "Default controller" scheme (Config_Xbox_1.mpp).
#include "common.h"
#include <xinput.h>
#pragma warning(disable: 4996)
#include <math.h>
#include <vector>

typedef DWORD(WINAPI* PFN_XInputGetState)(DWORD, XINPUT_STATE*);
typedef DWORD(WINAPI* PFN_XInputSetState)(DWORD, XINPUT_VIBRATION*);
static PFN_XInputGetState pXInputGetState;
static PFN_XInputSetState pXInputSetState;

// engine context calls (vtable of CInputContextCore)
typedef void(__fastcall* PFN_DownKey)(void* ctx, int code, unsigned short ch, double time, int rep, int x, int y);
typedef void(__fastcall* PFN_UpKey)(void* ctx, int code, double time, int x, int y);
typedef void(__fastcall* PFN_Update)(void* ctx);
static PFN_Update o_Update;

static void Down(void* ctx, int code, int value)
{
    if (g_cfg.inputLog) Log("pad: down %02x %d", code, value);
    auto vt = *(void***)ctx;
    ((PFN_DownKey)vt[0x58 / 8])(ctx, code, 0, 0.0, 1, value, 0);
}
static void Up(void* ctx, int code, int value)
{
    if (g_cfg.inputLog && value == 0) Log("pad: up %02x", code);
    auto vt = *(void***)ctx;
    ((PFN_UpKey)vt[0x60 / 8])(ctx, code, 0.0, value, 0);
}

// ---- virtual pad state ------------------------------------------------------
struct PadState { WORD buttons; float lx, ly, rx, ry, lt, rt; bool connected; };

static int  g_axisVal[16];     // last signed value sent per axis (-255..255)
static bool g_axisDown[32];    // per half-axis code: currently "pressed"
static bool g_btnDown[16];
static bool g_povDown[4];
static int  g_activePad = -1;
static DWORD g_lastGuiTick;
typedef bool(__fastcall* PFN_GetShowCursor)(void* ctx);
static PFN_GetShowCursor pGetShowCursor;   // menus show the mouse cursor
static DWORD g_lastPoll;

// test mode: scripted virtual pad (RIDDICKFIX_PADSCRIPT=<file>)
struct ScriptStep { DWORD t; WORD buttons; float lx, ly, rx, ry, lt, rt; };
static std::vector<ScriptStep> g_script;
static DWORD g_scriptStart;

static void LoadScript()
{
    char path[MAX_PATH];
    if (!GetEnvironmentVariableA("RIDDICKFIX_PADSCRIPT", path, MAX_PATH)) return;
    FILE* f = fopen(path, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        ScriptStep s = {};
        unsigned b = 0;
        if (line[0] == '#') continue;
        if (sscanf(line, "%lu %x %f %f %f %f %f %f", &s.t, &b, &s.lx, &s.ly, &s.rx, &s.ry, &s.lt, &s.rt) >= 2) {
            s.buttons = (WORD)b; g_script.push_back(s);
        }
    }
    fclose(f);
    Log("input: test pad script with %zu steps", g_script.size());
}

// live test channel: RIDDICKFIX_PADLIVE=<file>; file content "<seq> <button-mask hex> <ms> [lx ly rx ry lt rt]"
static char g_livePath[MAX_PATH];
static unsigned g_liveSeq;
static DWORD g_liveUntil, g_liveCheck;
static ScriptStep g_live;
static void PollLive()
{
    if (!g_livePath[0]) return;
    DWORD now = GetTickCount();
    if (now - g_liveCheck < 50) return;
    g_liveCheck = now;
    FILE* f = fopen(g_livePath, "r");
    if (!f) return;
    unsigned seq = 0, b = 0, ms = 0; ScriptStep s = {};
    int n = fscanf(f, "%u %x %u %f %f %f %f %f %f", &seq, &b, &ms, &s.lx, &s.ly, &s.rx, &s.ry, &s.lt, &s.rt);
    fclose(f);
    if (n >= 3 && seq != g_liveSeq) {
        g_liveSeq = seq; s.buttons = (WORD)b; g_live = s; g_liveUntil = now + ms;
        Log("input: live step %u buttons %x for %u ms", seq, b, ms);
    }
}

static bool ReadRealPad(PadState& p)
{
    p = {};
    if (!pXInputGetState) return false;
    // stick to one pad; re-scan every second when none is connected
    if (g_activePad < 0) {
        DWORD now = GetTickCount();
        if (now - g_lastPoll < 1000) return false;
        g_lastPoll = now;
        for (int i = 0; i < 4; ++i) {
            XINPUT_STATE st;
            if (pXInputGetState(i, &st) == ERROR_SUCCESS) { g_activePad = i; Log("input: controller %d connected", i); break; }
        }
        if (g_activePad < 0) return false;
    }
    XINPUT_STATE st;
    if (pXInputGetState(g_activePad, &st) != ERROR_SUCCESS) {
        Log("input: controller %d disconnected", g_activePad);
        g_activePad = -1;
        return false;
    }
    auto& gp = st.Gamepad;
    p.connected = true;
    p.buttons = gp.wButtons;
    p.lx = gp.sThumbLX / 32767.0f; p.ly = gp.sThumbLY / 32767.0f;
    p.rx = gp.sThumbRX / 32767.0f; p.ry = gp.sThumbRY / 32767.0f;
    p.lt = gp.bLeftTrigger / 255.0f; p.rt = gp.bRightTrigger / 255.0f;
    return true;
}

// The real controller is always read; test input (live file / script) is only
// added on top while a test step is active.
static bool ReadPad(PadState& p)
{
    bool real = ReadRealPad(p);
    const ScriptStep* t = nullptr;
    if (g_livePath[0]) {
        PollLive();
        if (GetTickCount() < g_liveUntil) t = &g_live;
    } else if (!g_script.empty()) {
        if (!g_scriptStart) g_scriptStart = GetTickCount();
        DWORD now = GetTickCount() - g_scriptStart;
        for (auto& s : g_script) if (s.t <= now) t = &s;
    }
    if (t) {
        p.connected = true;
        p.buttons |= t->buttons;
        if (t->lx || t->ly) { p.lx = t->lx; p.ly = t->ly; }
        if (t->rx || t->ry) { p.rx = t->rx; p.ry = t->ry; }
        if (t->lt) p.lt = t->lt;
        if (t->rt) p.rt = t->rt;
        return true;
    }
    return real;
}

// Radial dead zone + response curve, returns components scaled to 0..255 like the
// engine's own conversion (MSystem 0x1009e1e0).
static void Stick(float x, float y, float dz, float expo, float& ox, float& oy)
{
    float m = sqrtf(x * x + y * y);
    if (m < 1e-6f) { ox = oy = 0; return; }
    float n = (fminf(m, 1.0f) - dz) / (1.0f - dz);
    if (n <= 0) { ox = oy = 0; return; }
    n = powf(n, expo);
    ox = x / m * n * 255.0f;
    oy = y / m * n * 255.0f;
}

// Send one signed axis value through the pos/neg code pair, mirroring
// CDI_Device_Joystick: |v| > 128 is a key press, otherwise a value-only release.
static void SendAxis(void* ctx, int axis, int v)
{
    if (v == g_axisVal[axis]) return;
    g_axisVal[axis] = v;
    int pos = 0x80 + axis * 2, neg = pos + 1;
    int on = v >= 0 ? pos : neg, off = v >= 0 ? neg : pos;
    int mag = v >= 0 ? v : -v;
    if (g_axisDown[off - 0x80]) { g_axisDown[off - 0x80] = false; }
    Up(ctx, off, 0);
    if (mag > 128) { Down(ctx, on, mag); g_axisDown[on - 0x80] = true; }
    else           { Up(ctx, on, mag);   g_axisDown[on - 0x80] = false; }
}

static void SendButton(void* ctx, int idx, bool down)
{
    if (g_btnDown[idx] == down) return;
    g_btnDown[idx] = down;
    if (down) Down(ctx, 0xA0 + idx, 255); else Up(ctx, 0xA0 + idx, 0);
}

static void SendPov(void* ctx, int dir, bool down)
{
    if (g_povDown[dir] == down) return;
    g_povDown[dir] = down;
    if (down) Down(ctx, 0xB0 + dir, 255); else Up(ctx, 0xB0 + dir, 0);
    // d-pad down has no gameplay action in the pad profile: use it for the
    // in-game help (ACTION_HELP, F1 by default), shown as "Help <d-pad>" in the HUD
    if (dir == 2) { if (down) Down(ctx, 0x3B, 0); else Up(ctx, 0x3B, 0); }
}

static void PadToEngine(void* ctx)
{
    PadState p;
    if (!ReadPad(p) || !p.connected) {
        // release everything once on disconnect
        for (int a = 0; a < 6; ++a) SendAxis(ctx, a, 0);
        for (int b = 0; b < 10; ++b) SendButton(ctx, b, false);
        for (int d = 0; d < 4; ++d) SendPov(ctx, d, false);
        return;
    }
    float mx, my, lx, ly;
    Stick(p.lx, p.ly, g_cfg.deadzoneL, 1.0f, mx, my);
    Stick(p.rx, p.ry, g_cfg.deadzoneR, g_cfg.lookCurve, lx, ly);
    lx *= g_cfg.lookSensX;
    ly *= g_cfg.lookSensY * (g_cfg.invertY ? -1.0f : 1.0f);

    // Right stick. The profile's stick binding drives "lookvelocity", which the
    // game applies in its simulation ticks - at 120 fps the camera visibly steps.
    // The mouse binding ("look arg0,arg1" on MOUSEMOVE) is applied every frame, so
    // the stick is fed through that path as per-frame mouse deltas instead.
    static LARGE_INTEGER s_qpf, s_prev;
    static float s_accX, s_accY;
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    if (!s_qpf.QuadPart) { QueryPerformanceFrequency(&s_qpf); s_prev = now; }
    float dt = (float)((double)(now.QuadPart - s_prev.QuadPart) / s_qpf.QuadPart);
    s_prev = now;
    if (dt > 0.1f) dt = 0.1f;
    bool menu = false;   // no reliable "menu open" signal; the mouse path is harmless in menus
    if (g_cfg.inputLog) { static bool lastMenu = true; if (menu != lastMenu) { lastMenu = menu; Log("input: menu cursor %d", menu); } }
    if (g_cfg.stickMouse && !menu) {
        SendAxis(ctx, 0, 0);
        SendAxis(ctx, 1, 0);
        // degrees per second -> mouse counts (measured: ~0.234 deg per count at default sensitivity)
        const float degPerCount = 0.234f;
        s_accX += lx / 255.0f * g_cfg.turnRateX / degPerCount * dt;
        s_accY += -ly / 255.0f * g_cfg.turnRateY / degPerCount * dt;
        int ix = (int)s_accX, iy = (int)s_accY;
        if (ix || iy) {
            s_accX -= ix; s_accY -= iy;
            auto vt = *(void***)ctx;
            ((PFN_DownKey)vt[0x58 / 8])(ctx, 0xFB, 0, 0.0, 1, ix, iy);
        }
    } else {
        s_accX = s_accY = 0;
        // XInput Y is up-positive, DirectInput (and the profile) is down-positive
        SendAxis(ctx, 0, (int)lroundf(-ly));
        SendAxis(ctx, 1, (int)lroundf(lx));
    }
    SendAxis(ctx, 2, (int)lroundf(-my));
    SendAxis(ctx, 3, (int)lroundf(mx));
    auto trig = [](float t) { float n = (t - 0.12f) / 0.88f; return n <= 0 ? 0 : (int)lroundf(fminf(n, 1.0f) * 255.0f); };
    SendAxis(ctx, 4, trig(p.rt));
    SendAxis(ctx, 5, trig(p.lt));

    WORD b = p.buttons;
    SendButton(ctx, 0, (b & XINPUT_GAMEPAD_A) != 0);
    SendButton(ctx, 1, (b & XINPUT_GAMEPAD_B) != 0);
    SendButton(ctx, 2, (b & XINPUT_GAMEPAD_X) != 0);
    SendButton(ctx, 3, (b & XINPUT_GAMEPAD_Y) != 0);
    SendButton(ctx, 4, (b & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0);
    SendButton(ctx, 5, (b & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0);
    SendButton(ctx, 6, (b & XINPUT_GAMEPAD_BACK) != 0);
    SendButton(ctx, 7, (b & XINPUT_GAMEPAD_START) != 0);
    SendButton(ctx, 8, (b & XINPUT_GAMEPAD_LEFT_THUMB) != 0);
    SendButton(ctx, 9, (b & XINPUT_GAMEPAD_RIGHT_THUMB) != 0);
    SendPov(ctx, 0, (b & XINPUT_GAMEPAD_DPAD_UP) != 0);
    SendPov(ctx, 1, (b & XINPUT_GAMEPAD_DPAD_RIGHT) != 0);
    SendPov(ctx, 2, (b & XINPUT_GAMEPAD_DPAD_DOWN) != 0);
    SendPov(ctx, 3, (b & XINPUT_GAMEPAD_DPAD_LEFT) != 0);
}

// ---- force feedback -----------------------------------------------------------
// CInputContextCore keeps the active feedback envelopes (Content\Feedbackeedback.xrg)
// per device and evaluates them each frame (MSystem 0x10137680: float force for
// device n, channel c). The PC joystick device simply ignores the value, so we
// fetch both channels ourselves and drive the two XInput motors.
typedef unsigned long long(__fastcall* PFN_Ticks)();
typedef uint8_t* (__fastcall* PFN_TimerObj)();
typedef float(__fastcall* PFN_TimerScale)(uint8_t*);
typedef float(__fastcall* PFN_EnvForce)(uint8_t* mgr, int dev, double* time, int channel);
static PFN_Ticks pTicks; static PFN_TimerObj pTimerObj; static PFN_TimerScale pTimerScale; static PFN_EnvForce pEnvForce;
static WORD g_motorL = 0xFFFF, g_motorR = 0xFFFF;

static void Rumble(void* ctx)
{
    if (!pXInputSetState || g_activePad < 0) return;
    float l = 0, r = 0;
    if (g_cfg.vibration && pEnvForce) {
        double t = (double)pTicks() * (double)pTimerScale(pTimerObj());
        l = pEnvForce((uint8_t*)ctx + 0xac8, 0, &t, 0);
        r = pEnvForce((uint8_t*)ctx + 0xac8, 0, &t, 1);
    }
    WORD ml = (WORD)(fminf(fmaxf(l * g_cfg.vibrationGain, 0.0f), 1.0f) * 65535.0f);
    WORD mr = (WORD)(fminf(fmaxf(r * g_cfg.vibrationGain, 0.0f), 1.0f) * 65535.0f);
    if (ml == g_motorL && mr == g_motorR) return;
    g_motorL = ml; g_motorR = mr;
    XINPUT_VIBRATION v = { ml, mr };
    pXInputSetState(g_activePad, &v);
}

void Input_StopRumble()
{
    if (pXInputSetState && g_activePad >= 0) { XINPUT_VIBRATION v = {}; pXInputSetState(g_activePad, &v); }
}

// Alt+F4: the engine swallows it (DirectInput keyboard), so watch for it here.
static void CheckAltF4()
{
    if ((GetAsyncKeyState(VK_MENU) & 0x8000) && (GetAsyncKeyState(VK_F4) & 0x8000)) {
        Log("Alt+F4 -> quitting");
        Input_StopRumble();
        ChangeDisplaySettingsA(nullptr, 0);
        TerminateProcess(GetCurrentProcess(), 0);
    }
}

static void __fastcall Hook_Update(void* ctx)
{
    o_Update(ctx);
    PadToEngine(ctx);
    Rumble(ctx);
    CheckAltF4();
}

// ---- menu navigation ---------------------------------------------------------
// SbzEngine 0x4385C0 translates a raw key into GUI keys (GUI_UP .. GUI_BUTTON5)
// using hard-wired keyboard keys plus the profile's ACTION_GUI_* slots. Before a
// profile exists (and for the extra buttons) the pad has no translation, so we
// append the console mapping here.
typedef int(__fastcall* PFN_GuiTranslate)(uint8_t* in, uint8_t* out, intptr_t cap, int mode);
static PFN_GuiTranslate o_GuiTranslate;

static int PadToGui(int code)
{
    switch (code) {
    case 0xA0: return 0xE4;  // A      -> GUI_OK
    case 0xA1: return 0xE5;  // B      -> GUI_CANCEL
    case 0xA2: return 0xE6;  // X      -> GUI_BUTTON2
    case 0xA3: return 0xE7;  // Y      -> GUI_BUTTON3
    case 0xA7: return 0xE8;  // Start  -> GUI_START
    case 0xA6: return 0xE9;  // Back   -> GUI_BACK
    case 0xB0: case 0x85: return 0xE0;  // d-pad / left stick up
    case 0xB2: case 0x84: return 0xE1;  // down
    case 0xB3: case 0x87: return 0xE2;  // left
    case 0xB1: case 0x86: return 0xE3;  // right
    }
    return 0;
}

static int __fastcall Hook_GuiTranslate(uint8_t* in, uint8_t* out, intptr_t cap, int mode)
{
    int n = o_GuiTranslate(in, out, cap, mode);
    g_lastGuiTick = GetTickCount();
    uint32_t raw = *(uint32_t*)(in + 8);
    int gui = PadToGui(raw & 0x1FF);
    if (!gui || (mode != -1 && mode != 0) || (raw & 0x8000) || n >= cap) return n;
    for (int i = 0; i < n; ++i)
        if ((*(uint32_t*)(out + i * 0x28 + 8) & 0x1FF) == (uint32_t)gui) return n;  // profile already did it
    uint8_t* e = out + n * 0x28;
    *(uint32_t*)(e + 8)    = (raw & 0xFFFFFE00) | gui;
    *(uint64_t*)(e + 0xC)  = *(uint64_t*)(in + 0xC);
    *(uint64_t*)(e + 0x18) = *(uint64_t*)(in + 0x18);
    *(uint16_t*)(e + 0x20) = *(uint16_t*)(in + 0x20);
    return n + 1;
}

bool Input_PadActive() { return g_activePad >= 0 || !g_script.empty(); }

void Input_Init()
{
    if (!g_cfg.xinput) return;
    HMODULE x = LoadLibraryW(L"xinput1_4.dll");
    if (!x) x = LoadLibraryW(L"xinput9_1_0.dll");
    if (x) {
        pXInputGetState = (PFN_XInputGetState)GetProcAddress(x, "XInputGetState");
        pXInputSetState = (PFN_XInputSetState)GetProcAddress(x, "XInputSetState");
    }
    LoadScript();
    GetEnvironmentVariableA("RIDDICKFIX_PADLIVE", g_livePath, MAX_PATH);
    Log("input: XInput %s", pXInputGetState ? "ready" : "NOT available");
}

void Input_PatchModule(HMODULE mod, const char* name)
{
    if (!g_cfg.xinput) return;
    if (!_stricmp(name, "SbzEngine.exe")) {
        static const uint8_t expect[] = { 0x48,0x8B,0xC4, 0x48,0x81,0xEC,0xD8,0x04,0x00,0x00,
                                          0x48,0xC7,0x44,0x24,0x60,0xFE,0xFF,0xFF,0xFF };
        o_GuiTranslate = (PFN_GuiTranslate)HookFunction((uint8_t*)mod + 0x385C0, sizeof(expect), expect, (void*)Hook_GuiTranslate);
        Log("input: menu translation hook %s", o_GuiTranslate ? "ok" : "FAILED");
        return;
    }
    if (_stricmp(name, "MSystem.dll") != 0) return;
    // 1) the engine's DirectInput joystick would see the same controller with a
    //    different layout -> stop its device enumeration.
    auto enumCb = (uint8_t*)GetProcAddress(mod, "?DI_EnumJoysticksCallback@CInputContext_DInput@@KAHPEBUDIDEVICEINSTANCEA@@PEAX@Z");
    static const uint8_t expectCb[] = { 0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0xC2 };
    static const uint8_t stopCb[] = { 0x31, 0xC0, 0xC3 };   // return DIENUM_STOP
    if (enumCb && memcmp(enumCb, expectCb, sizeof(expectCb)) == 0) WriteMem(enumCb, stopCb, sizeof(stopCb));
    else Log("input: WARNING joystick enumeration callback not recognised");

    pGetShowCursor = (PFN_GetShowCursor)GetProcAddress(mod, "?GetShowCursor@CInputContextCore@@UEAA_NXZ");
    // force feedback helpers (verified by their first bytes)
    {
        uint8_t* b = (uint8_t*)mod;
        static const uint8_t eTicks[] = { 0x48,0x83,0xEC,0x38,0x48,0x8D,0x4C,0x24,0x20,0xFF,0x15 };
        static const uint8_t eObj[]   = { 0x48,0x83,0xEC,0x28,0x83,0x3D };
        static const uint8_t eEnv[]   = { 0x48,0x83,0xEC,0x38,0x48,0x8B,0x41,0x08,0x48,0x85,0xC0 };
        if (!memcmp(b + 0x4200, eTicks, sizeof(eTicks)) && !memcmp(b + 0x4270, eObj, sizeof(eObj)) &&
            !memcmp(b + 0x137680, eEnv, sizeof(eEnv))) {
            pTicks = (PFN_Ticks)(b + 0x4200); pTimerObj = (PFN_TimerObj)(b + 0x4270);
            pTimerScale = (PFN_TimerScale)(b + 0x27c0); pEnvForce = (PFN_EnvForce)(b + 0x137680);
            Log("input: force feedback bridge ready");
        } else Log("input: WARNING force feedback functions not recognised");
    }

    // 2) hook CInputContext_DInput::Update through its vtable slot
    void* upd = (void*)GetProcAddress(mod, "?Update@CInputContext_DInput@@UEAAXXZ");
    void** slot = (void**)((uint8_t*)mod + 0x1def90);
    if (upd && *slot == upd) {
        o_Update = (PFN_Update)upd;
        void* h = (void*)Hook_Update;
        WriteMem(slot, &h, sizeof(h));
        Log("input: hooked CInputContext_DInput::Update");
    } else {
        Log("input: WARNING unexpected MSystem layout, controller support disabled");
    }
}
