// Xbox button prompts.
//
// For every bindable action the game builds three dynamic strings
// CONTROLLER_<ACTION>_A/_B/_C ("Press E", "press E", "pressing E") from the
// keyboard binding (SbzEngine 0x407940, "Â§LCONTROLLER_PC_BUTTON_AÂ§p0<key>Â§pq").
// Tutorial, info and mission texts reference them (Â§LCONTROLLER_USE_B ...).
// The PC string tables still carry the complete Xbox grammar of the console
// version (CONTROLLER_XBOX_BUTTON_1_A = "Press the Â§p0 button", ..._TRIGGER_A =
// "Pull the Â§p0", CONTROLLER_XBOX_00..0e = button names) in all three languages,
// so after the original ran we overwrite the three strings with the Xbox form.
#include "common.h"
#include <string.h>
#include <string>

bool Input_PadActive();

typedef int(__fastcall* PFN_BuildAction)(void* self, void* actionStr, void* profile, void* dyn, int analog);
static PFN_BuildAction o_BuildAction;
typedef void* (__fastcall* PFN_CStrCtor)(void* self, const char* s);
static PFN_CStrCtor pCStrCtor = (PFN_CStrCtor)0x464480;   // SbzEngine (fixed base, no relocs)

static const char* CStrText(void* cstr)
{
    auto vt = *(void***)cstr;
    return ((const char* (__fastcall*)(void*))vt[0x28 / 8])(cstr);
}

struct PromptDef { const char* action; const char* family; const char* param; };
// family: grammar key prefix in the string table; param: button name (Â§L key or literal)
// glyph: icon name in the font (fontgen.cpp); text: fallback button name (§L key)
struct PromptDef2 { const char* action; const char* family; const char* glyph; const char* text; };
static const PromptDef2 kPrompts[] = {
    { "JUMP",          "BUTTON_3",         "A",         "§LCONTROLLER_XBOX_00" },
    { "RELOAD",        "BUTTON_3",         "B",         "§LCONTROLLER_XBOX_01" },
    { "USE",           "BUTTON_3",         "X",         "§LCONTROLLER_XBOX_02" },
    { "CYCLEWEAPON",   "BUTTON_3",         "Y",         "§LCONTROLLER_XBOX_03" },
    { "GUI_OK",        "BUTTON_3",         "A",         "§LCONTROLLER_XBOX_00" },
    { "GUI_OK2",       "BUTTON_3",         "A",         "§LCONTROLLER_XBOX_00" },
    { "GUI_CANCEL",    "BUTTON_3",         "B",         "§LCONTROLLER_XBOX_01" },
    { "FLASHLIGHT",    "BUTTON_3",         "LB",        "LB" },
    { "ZOOM",          "BUTTON_3",         "RB",        "RB" },
    { "JOURNAL",       "BUTTON_3",         "BACK",      "§LCONTROLLER_XBOX_09" },
    { "PAUSE",         "BUTTON_3",         "START",     "§LCONTROLLER_XBOX_08" },
    { "HELP",          "BUTTON_3",         "DPAD_DOWN", "§LCONTROLLER_XBOX_0e" },
    { "CROUCHTOGGLE",  "BUTTON_3",         "LS",        "§LCONTROLLER_XBOX_0a" },
    { "CROUCH",        "BUTTON_3",         "LS",        "§LCONTROLLER_XBOX_0a" },
    { "EYESHINE",      "BUTTON_3",         "RS",        "§LCONTROLLER_XBOX_0b" },
    { "PRIMARYFIRE",   "BUTTON_3",         "RT",        "§LCONTROLLER_XBOX_07" },
    { "SECONDARYFIRE", "BUTTON_3",         "LT",        "§LCONTROLLER_XBOX_06" },
    { "LEAN",          "DPAD",             "DPAD",      "§LCONTROLLER_XBOX_0e" },
    { "LEANLEFT",      "DPAD",             "DPAD",      "§LCONTROLLER_XBOX_0e" },
    { "LEANRIGHT",     "DPAD",             "DPAD",      "§LCONTROLLER_XBOX_0e" },
    { "LEANUP",        "DPAD",             "DPAD",      "§LCONTROLLER_XBOX_0e" },
    { "FORWARD",       "THUMBSTICK",       nullptr,     "§LCONTROLLER_XBOX_0c" },
    { "BACKWARD",      "THUMBSTICK",       nullptr,     "§LCONTROLLER_XBOX_0c" },
    { "STRAFELEFT",    "THUMBSTICK",       nullptr,     "§LCONTROLLER_XBOX_0c" },
    { "STRAFERIGHT",   "THUMBSTICK",       nullptr,     "§LCONTROLLER_XBOX_0c" },
    { "MOVE_FORWARD",  "THUMBSTICK",       nullptr,     "§LCONTROLLER_XBOX_0c" },
    { "MOVE_BACKWARD", "THUMBSTICK",       nullptr,     "§LCONTROLLER_XBOX_0c" },
    { "MOVE_LEFT",     "THUMBSTICK",       nullptr,     "§LCONTROLLER_XBOX_0c" },
    { "MOVE_RIGHT",    "THUMBSTICK",       nullptr,     "§LCONTROLLER_XBOX_0c" },
    { "LOOK_UP",       "THUMBSTICK",       nullptr,     "§LCONTROLLER_XBOX_0d" },
    { "LOOK_DOWN",     "THUMBSTICK",       nullptr,     "§LCONTROLLER_XBOX_0d" },
    { "LOOK_LEFT",     "THUMBSTICK",       nullptr,     "§LCONTROLLER_XBOX_0d" },
    { "LOOK_RIGHT",    "THUMBSTICK",       nullptr,     "§LCONTROLLER_XBOX_0d" },
};
uint8_t Glyph_Char(const char* name);
bool g_glyphFonts;   // set when the fonts with icons were generated

static void SetDyn(void* dyn, const char* key, const char* value)
{
    alignas(16) uint8_t cstr[32] = {};
    pCStrCtor(cstr, value);                      // the callee takes ownership (by-value CStr)
    auto vt = *(void***)dyn;
    ((void(__fastcall*)(void*, const char*, void*))vt[0x248 / 8])(dyn, key, cstr);
}

static bool g_pressStartDone;

static int __fastcall Hook_BuildAction(void* self, void* actionStr, void* profile, void* dyn, int analog)
{
    int code = o_BuildAction(self, actionStr, profile, dyn, analog);
    if (!dyn || !g_cfg.xboxPrompts) return code;
    const char* name = CStrText(actionStr);
    if (!name) return code;
    std::string base = name;
    if (base.size() > 1 && base.back() == '2') base.pop_back();
    for (auto& p : kPrompts) {
        if (base != p.action) continue;
        char param[64];
        uint8_t ch = (g_glyphFonts && p.glyph) ? Glyph_Char(p.glyph) : 0;
        if (ch) { param[0] = (char)ch; param[1] = 0; }
        else strcpy_s(param, p.text);
        const char* family = ch ? p.family : (strcmp(p.family, "BUTTON_3") ? p.family : "BUTTON_1");
        for (char form : { 'A', 'B', 'C' }) {
            char key[96], val[192];
            sprintf_s(key, "CONTROLLER_%s_%c", name, form);
            sprintf_s(val, "\xA7LCONTROLLER_XBOX_%s_%c\xA7p0%s\xA7pq", family, form, param);
            SetDyn(dyn, key, val);
        }
        break;
    }
    if (!g_pressStartDone) {
        g_pressStartDone = true;
        char v[96]; uint8_t st = g_glyphFonts ? Glyph_Char("START") : 0;
        if (st) sprintf_s(v, "\xA7LCONTROLLER_XBOX_BUTTON_3_A\xA7p0%c\xA7pq", st);
        else strcpy_s(v, "\xA7LCONTROLLER_XBOX_BUTTON_3_A\xA7p0\xA7LCONTROLLER_XBOX_08\xA7pq");
        SetDyn(dyn, "MENU_PRESSSTART", v);
    }
    return code;
}

void Prompts_PatchExe(HMODULE exe)
{
    if (!g_cfg.xboxPrompts) return;
    static const uint8_t expectCtor[] = { 0x48 };   // sanity: code present
    if (*(uint8_t*)pCStrCtor != expectCtor[0] && *(uint8_t*)pCStrCtor != 0x40) {
        Log("prompts: WARNING CStr ctor not recognised"); return;
    }
    static const uint8_t expect[] = { 0x48,0x8B,0xC4, 0x48,0x81,0xEC,0x78,0x01,0x00,0x00,
                                      0x48,0xC7,0x84,0x24,0x20,0x01,0x00,0x00,0xFE,0xFF,0xFF,0xFF };
    o_BuildAction = (PFN_BuildAction)HookFunction((uint8_t*)exe + 0x7940, sizeof(expect), expect, (void*)Hook_BuildAction);
    Log("prompts: action prompt hook %s", o_BuildAction ? "ok" : "FAILED");
}

// HUD help hint: the F1 key cap texture is drawn next to "Help". The help action
// lives on d-pad down now and the hint text carries that icon (stringgen.cpp),
// so the key cap is pointed at a texture name that does not exist.
void Prompts_PatchGameClasses(HMODULE gc)
{
    if (!g_cfg.xboxPrompts || !g_glyphFonts) return;
    uint8_t* p = FindPattern(gc, "47 55 49 5F 42 75 74 74 6F 6E 5F 46 31 5F 36 34 00");   // "GUI_Button_F1_64"
    if (!p) { Log("prompts: F1 key cap texture name not found"); return; }
    static const char none[] = "GUI_NoKeyCap____";
    WriteMem(p, none, 16);
    Log("prompts: HUD F1 key cap hidden");
}
