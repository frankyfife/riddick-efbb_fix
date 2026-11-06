// Controller vibration.
//
// The PC build never vibrates: GameClasses' client message handler for the
// rumble message (type 0x20, gated by the GAME_VIBRATION option) reads the
// effect name and destroys it right away - the Xbox code that started the
// effect was compiled out. The effects themselves still ship as curves in
// Content\Feedback\feedback.xrg (*ENVELOPE "name" { *CHANNEL n { *POINT { *TIME t
// *FORCE f } } }), but no PC module parses that file either.
//
// So: parse feedback.xrg here, redirect the name's destructor call in the
// handler (GameClasses+0x2078EF) to us, start the curve, and mix all running
// curves every frame into the two XInput motors (channel 0 = left/low,
// channel 1 = right/high).
#include "common.h"
#include <vector>
#include <string>
#include <map>
#include <mutex>
#include <algorithm>

struct Point { float t, f; };
struct Envelope { std::vector<Point> ch[2]; float length = 0; };
static std::map<std::string, Envelope> g_env;           // lower-case name
struct Active { const Envelope* env; double start; };
static std::vector<Active> g_active;
static std::mutex g_mx;

static double Now()
{
    static LARGE_INTEGER f; if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}

static std::string Lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }

void Rumble_Init()
{
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\Content\\Feedback\\feedback.xrg", g_gameRoot);
    FILE* f = _wfopen(path, L"rb");
    if (!f) { Log("rumble: feedback.xrg not found"); return; }
    std::string text; char buf[8192]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    fclose(f);
    // drop /* ... */ comments (the file starts with a name list in one)
    for (size_t a; (a = text.find("/*")) != std::string::npos;) {
        size_t b = text.find("*/", a + 2);
        text.erase(a, b == std::string::npos ? std::string::npos : b + 2 - a);
    }
    Envelope* cur = nullptr; int ch = -1; float t = 0;
    size_t p = 0;
    while ((p = text.find('*', p)) != std::string::npos) {
        ++p;
        size_t e = p; while (e < text.size() && (isalnum((unsigned char)text[e]) || text[e] == '_')) ++e;
        std::string kw = text.substr(p, e - p);
        size_t le = text.find_first_of("\r\n", e); if (le == std::string::npos) le = text.size();
        std::string arg = text.substr(e, le - e);
        if (kw == "ENVELOPE") {
            size_t q0 = arg.find('"'), q1 = arg.find('"', q0 + 1);
            if (q0 == std::string::npos || q1 == std::string::npos) { cur = nullptr; continue; }
            cur = &g_env[Lower(arg.substr(q0 + 1, q1 - q0 - 1))];
            *cur = Envelope(); ch = -1;
        } else if (kw == "CHANNEL" && cur) {
            ch = atoi(arg.c_str());
        } else if (kw == "TIME") {
            t = (float)atof(arg.c_str());
        } else if (kw == "FORCE" && cur && ch >= 0 && ch < 2) {
            cur->ch[ch].push_back({ t, (float)atof(arg.c_str()) });
            cur->length = std::max(cur->length, t);
        }
        p = e;
    }
    Log("rumble: %zu effects loaded from feedback.xrg", g_env.size());
}

static float Sample(const std::vector<Point>& pts, float t)
{
    if (pts.empty() || t < pts.front().t || t > pts.back().t) return 0;
    for (size_t i = 1; i < pts.size(); ++i)
        if (t <= pts[i].t) {
            float d = pts[i].t - pts[i - 1].t;
            float a = d > 0 ? (t - pts[i - 1].t) / d : 1.0f;
            return pts[i - 1].f + (pts[i].f - pts[i - 1].f) * a;
        }
    return pts.back().f;
}

void Rumble_Play(const char* name)
{
    if (!name || !*name) return;
    auto it = g_env.find(Lower(name));
    static int logged;
    if (logged < 20) { ++logged; Log("rumble: effect '%s'%s", name, it == g_env.end() ? " (unknown)" : ""); }
    if (it == g_env.end()) return;
    std::lock_guard<std::mutex> l(g_mx);
    g_active.push_back({ &it->second, Now() });
}

void Rumble_Stop()
{
    std::lock_guard<std::mutex> l(g_mx);
    g_active.clear();
}

// strongest running curve per motor (channel 0 = left, 1 = right)
void Rumble_GetForces(float& left, float& right)
{
    left = right = 0;
    std::lock_guard<std::mutex> l(g_mx);
    double now = Now();
    g_active.erase(std::remove_if(g_active.begin(), g_active.end(),
        [&](const Active& a) { return now - a.start > a.env->length + 0.05; }), g_active.end());
    for (auto& a : g_active) {
        float t = (float)(now - a.start);
        left = std::max(left, Sample(a.env->ch[0], t));
        right = std::max(right, Sample(a.env->ch[1], t));
    }
}

// ---- GameClasses hook ------------------------------------------------------------------
typedef void(__fastcall* PFN_StrDtor)(void* cstr);
static PFN_StrDtor o_StrDtor;

static const char* GcStrText(void* cstr)   // GameClasses CStr: [+8] -> u16 flags, then chars
{
    const uint8_t* d = *(const uint8_t**)((uint8_t*)cstr + 8);
    if (!d) return nullptr;
    uint16_t h = *(const uint16_t*)d;
    if (h & 0xA000) return nullptr;            // wide / special string
    return (const char*)d + 2;
}

static void __fastcall Hook_RumbleNameDtor(void* cstr)
{
    if (g_cfg.vibration) Rumble_Play(GcStrText(cstr));
    o_StrDtor(cstr);
}

void Rumble_PatchGameClasses(HMODULE gc)
{
    if (!g_cfg.vibration) return;
    uint8_t* site = (uint8_t*)gc + 0x2078EF;
    // ... call ReadStr(0x228BD0); lea rcx,[rsp+0xA8]; call StrDtor(0x11340); jmp ...
    static const uint8_t expect[] = { 0xE8, 0xE9, 0x12, 0x02, 0x00, 0x48, 0x8D, 0x8C, 0x24, 0xA8, 0x00, 0x00, 0x00, 0xE8 };
    if (memcmp(site - 13, expect, sizeof(expect)) != 0 || site + 5 + *(int32_t*)(site + 1) != (uint8_t*)gc + 0x11340) {
        Log("rumble: WARNING GameClasses rumble handler not recognised, vibration disabled");
        return;
    }
    o_StrDtor = (PFN_StrDtor)((uint8_t*)gc + 0x11340);
    // near stub: mov rax, imm64; jmp rax
    uint8_t* stub = (uint8_t*)AllocNear(site, 16);
    if (!stub) { Log("rumble: no stub memory"); return; }
    stub[0] = 0x48; stub[1] = 0xB8; void* h = (void*)Hook_RumbleNameDtor; memcpy(stub + 2, &h, 8);
    stub[10] = 0xFF; stub[11] = 0xE0;
    int32_t rel = (int32_t)(stub - (site + 5));
    WriteMem(site + 1, &rel, 4);
    Log("rumble: GameClasses rumble handler hooked");
}
