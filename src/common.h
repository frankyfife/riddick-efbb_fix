// RiddickFix - drop-in modernisation for The Chronicles of Riddick: Escape From Butcher Bay (Win64 build)
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <stdint.h>
#include <stdio.h>

// ---- logging -----------------------------------------------------------
void LogInit(const wchar_t* dir);
void Log(const char* fmt, ...);

// ---- configuration (RiddickFix.ini next to the DLL) ---------------------
struct Config {
    // display
    int  resMode;              // 0 = keep in-game choice, 1 = force (Width/Height, 0 = desktop)
    int  width, height;        // 0 = desktop resolution
    int  refresh;              // 0 = desktop refresh rate
    int  borderless;           // 1 = borderless window instead of exclusive mode switch
    int  vsync;                // -1 = keep game setting, 0 = off, 1 = on
    int  fpsLimit;             // 0 = unlimited
    int  fixFov;               // Hor+ widescreen correction
    float fovFactor;           // projection correction (2 = engine FOV is the real 4:3 FOV)
    int  uiAspect;             // 1 = draw text/button icons with square pixels on wide screens
    // renderer
    int  extFilter;            // 1 = present a short, 2004-compatible GL extension string
    int  maxTexSize;
    int  shaderMode;           // -1 = keep, -2 = auto (2.0++ on NVIDIA), 0..6
    int  anisotropy;           // 0 = game setting, else force (1..16)
    int  modelLod;             // -1 = keep game setting, 0 = normal, 1 = high (always full detail)
    // input
    int  xinput;               // 1 = native Xbox controller support
    int  xboxPrompts;          // 1 = Xbox button names/glyphs instead of keyboard names
    int  skipIntro;
    wchar_t language[16];
    int  consoleMenus;         // 1 = console button hints in the menus
    float lookSensX, lookSensY;
    int  stickMouse;           // right stick through the per-frame mouse look path
    float turnRateX, turnRateY, lookCurve;
    float deadzoneL, deadzoneR;
    int  invertY;
    int  vibration;
    float vibrationGain;
    int  dpiAware;
    int  engineLog;            // mirror engine console output into RiddickFix.log
    int  inputLog;
};
extern Config g_cfg;
extern float g_uiSqueeze;     // horizontal factor that undoes the 640x480 -> screen stretch (1 = none)
extern int g_screenW, g_screenH;
extern wchar_t g_dllDir[MAX_PATH];
extern wchar_t g_gameRoot[MAX_PATH];
void ConfigLoad();

// ---- hooking helpers ----------------------------------------------------
// Replace an import of `module`. Returns old pointer or nullptr.
void* PatchIAT(HMODULE module, const char* importDll, const char* func, void* replacement);
bool  WriteMem(void* dst, const void* src, size_t n);
// Find byte pattern ("48 8B ?? 05") inside the executable sections of a module.
uint8_t* FindPattern(HMODULE module, const char* pattern);
// Absolute 14-byte jump; returns false if bytes at target don't match `expect` (may be null).
bool PatchJump(void* at, void* to, size_t overwrite, const uint8_t* expect = nullptr);
void* HookFunction(void* target, size_t len, const uint8_t* expect, void* detour);
// Allocate executable memory within +-2GB of `near` (for trampolines).
void* AllocNear(void* nearAddr, size_t size);

// ---- subsystems ----------------------------------------------------------
void OnModuleLoaded(HMODULE mod, const char* name);   // dispatcher
void GL_PatchModule(HMODULE rndr);                     // renderer (RndrGL.dll)
void Display_PatchModule(HMODULE rndr);
void Display_PrepareConfig();
void Input_PatchModule(HMODULE mod, const char* name);
void Input_Init();
void Input_StopRumble();
void Fov_PatchMSystem(HMODULE msys);
void Prompts_PatchExe(HMODULE exe);
void Files_PatchModule(HMODULE mod);
void FontGen_Run();
void MenuGen_Run();
void StringGen_Run();
void Prompts_PatchGameClasses(HMODULE gc);
