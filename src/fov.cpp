// Projection / aspect ratio handling (MSystem CRC_Viewport)
#include "common.h"
#include <math.h>

typedef void(__fastcall* PFN_VpUpdate)(uint8_t* vp);
static PFN_VpUpdate o_VpUpdate;
static int g_vpLogCount;
static float s_lastLoggedFov; static int s_lastW;

struct VpSnap { float fov, fovAsp, aspect, pax, pay, sx, sy; int w, h; };
static VpSnap g_seen[64];

static void __fastcall Hook_VpUpdate(uint8_t* vp)
{
    bool dirty = vp[0x54] != 0;
    // CRC_Viewport::Update turns the FOV angle into a projection scale of
    // 2*cot(fov/2) * (h/2) * fovAspect, i.e. twice the focal length that the
    // angle describes: "90 degrees" ends up as 53 (4:3) / 67 (16:9) degrees, a
    // 2x zoom. Feed it an angle whose cotangent is scaled accordingly so that the
    // requested FOV is the real horizontal FOV at 4:3 (Hor+ on wider screens).
    float* fov = (float*)(vp + 4);
    float orig = *fov;
    // only the main 3D views (camera, 3D menus): render-to-texture views such as
    // light projections and reflections must keep their exact frustum
    int vw = *(int*)(vp + 0x4c) - *(int*)(vp + 0x44), vh = *(int*)(vp + 0x50) - *(int*)(vp + 0x48);
    static int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    bool mainView = vw * 2 >= sw && vh * 2 >= sh;
    if (dirty && mainView && *(int*)vp == 0 && g_cfg.fovFactor != 1.0f && orig > 0.1f && orig < 179.0f) {
        float t = tanf(orig * 0.0087266463f) * g_cfg.fovFactor;
        float f = 2.0f * atanf(t) * 57.29578f;
        *fov = f < 170.0f ? f : 170.0f;
    }
    o_VpUpdate(vp);
    *fov = orig;
    if (!dirty || g_vpLogCount >= 60 || !(vw == sw && vh == sh)) return;
    if (s_lastLoggedFov == orig) return;
    s_lastLoggedFov = orig; s_lastW = vw;
    VpSnap s;
    s.fov = *(float*)(vp + 4); s.fovAsp = *(float*)(vp + 8); s.aspect = *(float*)(vp + 0x10);
    s.pax = *(float*)(vp + 0x14); s.pay = *(float*)(vp + 0x18);
    s.sx = *(float*)(vp + 0x1c); s.sy = *(float*)(vp + 0x20);
    s.w = *(int*)(vp + 0x4c) - *(int*)(vp + 0x44); s.h = *(int*)(vp + 0x50) - *(int*)(vp + 0x48);
    g_seen[g_vpLogCount++] = s;
    float hfov = s.sx > 0 ? 2.0f * atanf(s.w * 0.5f / s.sx) * 57.29578f : 0;
    float vfov = s.sy > 0 ? 2.0f * atanf(s.h * 0.5f / s.sy) * 57.29578f : 0;
    Log("viewport: mode %d fov %.2f fovAspect %.3f aspect %.3f pixAspect %.3f/%.3f view %dx%d scale %.1f/%.1f -> hFOV %.1f vFOV %.1f",
        *(int*)vp, s.fov, s.fovAsp, s.aspect, s.pax, s.pay, s.w, s.h, s.sx, s.sy, hfov, vfov);
}

void Fov_PatchMSystem(HMODULE msys)
{
    static const uint8_t expect[] = { 0x48,0x8B,0xC4, 0x48,0x81,0xEC,0x38,0x05,0x00,0x00, 0x48,0xC7,0x44,0x24,0x40,0xFE,0xFF,0xFF,0xFF };
    void* f = (void*)GetProcAddress(msys, "?Update@CRC_Viewport@@AEAAXXZ");
    if (f) o_VpUpdate = (PFN_VpUpdate)HookFunction(f, sizeof(expect), expect, (void*)Hook_VpUpdate);
    Log("fov: viewport hook %s", o_VpUpdate ? "ok" : "FAILED");
}
