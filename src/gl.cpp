// OpenGL compatibility layer for RndrGL.dll
#include "common.h"
#include <string>
#include <string.h>

typedef const unsigned char* (WINAPI* PFN_glGetString)(unsigned int);
static PFN_glGetString o_glGetString;
static std::string g_extFiltered;

// Everything RndrGL (2005) knows how to use. Anything else is hidden so that the
// engine's extension parser only sees a string of era-appropriate length.
static const char* const kWanted[] = {
    "GL_ARB_fragment_program", "GL_ARB_multitexture", "GL_ARB_texture_compression",
    "GL_ARB_texture_cube_map", "GL_ARB_texture_env_combine", "GL_ARB_texture_env_dot3",
    "GL_ARB_vertex_buffer_object", "GL_ARB_vertex_program", "GL_ARB_window_pos",
    "GL_EXT_bgra", "GL_EXT_secondary_color", "GL_EXT_stencil_two_side", "GL_EXT_stencil_wrap",
    "GL_EXT_texture_compression_s3tc", "GL_EXT_texture_filter_anisotropic", "GL_EXT_texture_lod_bias",
    "GL_NV_copy_depth_to_color", "GL_NV_fence", "GL_NV_fragment_program", "GL_NV_fragment_program_option",
    "GL_NV_fragment_program2", "GL_NV_occlusion_query", "GL_NV_register_combiners",
    "GL_NV_register_combiners2", "GL_NV_texture_shader", "GL_NV_vertex_program",
    "GL_ATI_fragment_shader", "GL_ATI_separate_stencil",
};

static bool HasExt(const char* all, const char* name)
{
    size_t n = strlen(name);
    for (const char* p = all; (p = strstr(p, name)) != nullptr; p += n) {
        bool startOk = (p == all) || p[-1] == ' ';
        bool endOk = p[n] == ' ' || p[n] == 0;
        if (startOk && endOk) return true;
    }
    return false;
}

static const unsigned char* WINAPI Hook_glGetString(unsigned int name)
{
    const unsigned char* r = o_glGetString(name);
    if (name == 0x1F03 /*GL_EXTENSIONS*/ && r && g_cfg.extFilter) {
        if (g_extFiltered.empty()) {
            for (const char* e : kWanted)
                if (HasExt((const char*)r, e)) { g_extFiltered += e; g_extFiltered += ' '; }
            Log("GL: driver extension string %zu chars -> filtered %zu chars: %s",
                strlen((const char*)r), g_extFiltered.size(), g_extFiltered.c_str());
        }
        return (const unsigned char*)g_extFiltered.c_str();
    }
    if (name != 0x1F03 && r) Log("GL: glGetString(%x) = %s", name, r);
    return r;
}

typedef void(WINAPI* PFN_glViewport)(int, int, int, int);
static PFN_glViewport o_glViewport, o_glScissor;
static int g_vpLogged;
static void WINAPI Hook_glViewport(int x, int y, int w, int h)
{
    if (g_vpLogged < 40) { ++g_vpLogged; Log("GL: glViewport(%d,%d,%d,%d)", x, y, w, h); }
    o_glViewport(x, y, w, h);
}
static int g_scLogged;
static void WINAPI Hook_glScissor(int x, int y, int w, int h)
{
    if (g_scLogged < 20) { ++g_scLogged; Log("GL: glScissor(%d,%d,%d,%d)", x, y, w, h); }
    o_glScissor(x, y, w, h);
}

// frame pacing / statistics
static BOOL(WINAPI* o_SwapBuffers)(HDC);
static LARGE_INTEGER g_qpf, g_last, g_statStart;
static int g_frames;
static double g_worst;
static BOOL WINAPI Hook_SwapBuffers(HDC dc)
{
    BOOL r = o_SwapBuffers(dc);
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    if (!g_qpf.QuadPart) { QueryPerformanceFrequency(&g_qpf); g_statStart = g_last = now; }
    // optional limiter
    if (g_cfg.fpsLimit > 0) {
        double target = 1.0 / g_cfg.fpsLimit;
        for (;;) {
            QueryPerformanceCounter(&now);
            double dt = (double)(now.QuadPart - g_last.QuadPart) / g_qpf.QuadPart;
            if (dt >= target) break;
            if (target - dt > 0.002) Sleep(1); else YieldProcessor();
        }
    }
    double dt = (double)(now.QuadPart - g_last.QuadPart) / g_qpf.QuadPart;
    if (dt > g_worst) g_worst = dt;
    g_last = now;
    ++g_frames;
    double span = (double)(now.QuadPart - g_statStart.QuadPart) / g_qpf.QuadPart;
    if (span >= 10.0) {
        if (g_cfg.engineLog) Log("fps: %.1f average, slowest frame %.1f ms", g_frames / span, g_worst * 1000.0);
        g_frames = 0; g_worst = 0; g_statStart = now;
    }
    return r;
}

void GL_PatchModule(HMODULE rndr)
{
    if (void* o = PatchIAT(rndr, "gdi32.dll", "SwapBuffers", (void*)Hook_SwapBuffers)) o_SwapBuffers = (BOOL(WINAPI*)(HDC))o;
    o_glViewport = (PFN_glViewport)PatchIAT(rndr, "opengl32.dll", "glViewport", (void*)Hook_glViewport);
    o_glScissor  = (PFN_glViewport)PatchIAT(rndr, "opengl32.dll", "glScissor", (void*)Hook_glScissor);
    // RndrGL clamps GL_MAX_TEXTURE_SIZE to 2048 (MRndrGL.cpp, 0x100084b2). Screen
    // copies for post effects are made into textures of that limit, so at 4K only
    // a part of the frame survives. Raise the clamp to 16384.
    {
        uint8_t* p = (uint8_t*)rndr + 0x84b2;
        static const uint8_t expect[] = { 0x3d,0x00,0x08,0x00,0x00, 0x7e,0x0a, 0xc7,0x85,0xb4,0x34,0x00,0x00,0x00,0x08,0x00,0x00 };
        if (memcmp(p, expect, sizeof(expect)) == 0) {
            uint32_t lim = (uint32_t)g_cfg.maxTexSize;
            WriteMem(p + 1, &lim, 4);
            WriteMem(p + 13, &lim, 4);
            Log("GL: texture size clamp raised to %u", lim);
        } else Log("GL: WARNING texture clamp not found");
    }
    void* old = PatchIAT(rndr, "opengl32.dll", "glGetString", (void*)Hook_glGetString);
    if (old) o_glGetString = (PFN_glGetString)old;
    Log("GL: glGetString hook %s", old ? "ok" : "FAILED");
}
