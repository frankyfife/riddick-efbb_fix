#include "common.h"
#include <stdlib.h>

Config g_cfg;

static int GetI(const wchar_t* path, const wchar_t* sec, const wchar_t* key, int def)
{
    return (int)GetPrivateProfileIntW(sec, key, def, path);
}
static float GetF(const wchar_t* path, const wchar_t* sec, const wchar_t* key, float def)
{
    wchar_t buf[64], d[64];
    swprintf_s(d, L"%g", def);
    GetPrivateProfileStringW(sec, key, d, buf, 64, path);
    return (float)_wtof(buf);
}

void ConfigLoad()
{
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\RiddickFix.ini", g_dllDir);
    g_cfg.resMode    = GetI(path, L"Display", L"ForceResolution", 1);
    g_cfg.width      = GetI(path, L"Display", L"Width", 0);
    g_cfg.height     = GetI(path, L"Display", L"Height", 0);
    g_cfg.refresh    = GetI(path, L"Display", L"RefreshRate", 0);
    {
        wchar_t b[16]; GetPrivateProfileStringW(L"Display", L"Borderless", L"auto", b, 16, path);
        g_cfg.borderless = !_wcsicmp(b, L"auto") ? -1 : _wtoi(b);   // -1 = only when Windows HDR is on
    }
    g_cfg.gammaRamp  = GetI(path, L"Display", L"GammaRamp", -1);   // -1 = auto (off under HDR)
    g_cfg.vsync      = GetI(path, L"Display", L"VSync", -1);
    g_cfg.fpsLimit   = GetI(path, L"Display", L"FPSLimit", 0);
    g_cfg.fixFov     = GetI(path, L"Display", L"WidescreenFOV", 1);
    g_cfg.fovFactor  = GetF(path, L"Display", L"FOVFactor", 1.0f);
    g_cfg.dpiAware   = GetI(path, L"Display", L"DPIAware", 1);
    g_cfg.uiAspect   = GetI(path, L"Display", L"FixUIAspect", 1);
    g_cfg.extFilter  = GetI(path, L"Renderer", L"CompatExtensions", 1);
    g_cfg.maxTexSize = GetI(path, L"Renderer", L"MaxTextureSize", 16384);
    {
        wchar_t sm[16]; GetPrivateProfileStringW(L"Renderer", L"ShaderMode", L"auto", sm, 16, path);
        g_cfg.shaderMode = !_wcsicmp(sm, L"auto") ? -2 : !_wcsicmp(sm, L"keep") ? -1 : _wtoi(sm);
    }
    g_cfg.anisotropy = GetI(path, L"Renderer", L"Anisotropy", 16);
    {
        wchar_t mq[16]; GetPrivateProfileStringW(L"Renderer", L"ModelQuality", L"high", mq, 16, path);
        g_cfg.modelLod = !_wcsicmp(mq, L"high") ? 1 : !_wcsicmp(mq, L"normal") ? 0 : -1;
    }
    {
        wchar_t e[16]; GetPrivateProfileStringW(L"Audio", L"EAX", L"auto", e, 16, path);
        g_cfg.eax = !_wcsicmp(e, L"auto") ? -2 : !_wcsicmp(e, L"keep") ? -1 : _wtoi(e) ? 1 : 0;
    }
    g_cfg.xinput     = GetI(path, L"Controller", L"Enable", 1);
    g_cfg.xboxPrompts= GetI(path, L"Controller", L"XboxPrompts", 1);
    g_cfg.skipIntro  = GetI(path, L"Game", L"SkipIntro", 1);
    GetPrivateProfileStringW(L"Game", L"Language", L"", g_cfg.language, 16, path);
    g_cfg.consoleMenus = GetI(path, L"Controller", L"ConsoleMenuHints", 1);
    g_cfg.stickMouse = GetI(path, L"Controller", L"SmoothLook", 1);
    g_cfg.turnRateX  = GetF(path, L"Controller", L"TurnRateX", 200.0f);
    g_cfg.turnRateY  = GetF(path, L"Controller", L"TurnRateY", 140.0f);
    g_cfg.lookCurve  = GetF(path, L"Controller", L"LookCurve", 1.8f);
    g_cfg.lookSensX  = GetF(path, L"Controller", L"LookSensitivityX", 1.0f);
    g_cfg.lookSensY  = GetF(path, L"Controller", L"LookSensitivityY", 1.0f);
    g_cfg.deadzoneL  = GetF(path, L"Controller", L"DeadzoneLeft", 0.24f);
    g_cfg.deadzoneR  = GetF(path, L"Controller", L"DeadzoneRight", 0.20f);
    g_cfg.invertY    = GetI(path, L"Controller", L"InvertY", 0);
    g_cfg.vibrationGain = GetF(path, L"Controller", L"VibrationStrength", 1.0f);
    g_cfg.engineLog  = GetI(path, L"Debug", L"EngineLog", 0);
    g_cfg.inputLog   = GetI(path, L"Debug", L"InputLog", 0);
    g_cfg.vibration  = GetI(path, L"Controller", L"Vibration", 1);
    Log("config: %dx%d@%d borderless=%d vsync=%d fps=%d fov=%d ext=%d xinput=%d prompts=%d",
        g_cfg.width, g_cfg.height, g_cfg.refresh, g_cfg.borderless, g_cfg.vsync, g_cfg.fpsLimit,
        g_cfg.fixFov, g_cfg.extFilter, g_cfg.xinput, g_cfg.xboxPrompts);
}
