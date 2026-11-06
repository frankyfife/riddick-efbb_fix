// Entry point: winmm.dll proxy + module load tracking
#include "common.h"
#include "proxy_names.h"
#include <psapi.h>
#include <string.h>

extern "C" void* g_proxy_table[PROXY_COUNT] = {};
wchar_t g_dllDir[MAX_PATH];
wchar_t g_gameRoot[MAX_PATH];
static HMODULE g_self, g_realWinmm;

static void ProxyInit()
{
    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    wcscat_s(sys, L"\\winmm.dll");
    g_realWinmm = LoadLibraryW(sys);
    for (int i = 0; i < PROXY_COUNT; ++i)
        g_proxy_table[i] = g_realWinmm ? (void*)GetProcAddress(g_realWinmm, g_proxy_names[i]) : nullptr;
}

// ---------------------------------------------------------------------------
// module tracking: every game module gets its LoadLibrary imports redirected,
// so that we see RndrGL / GameWorld / GameClasses / dinput as they come in.
static HMODULE g_seen[64];
static int g_seenCount;
static CRITICAL_SECTION g_modCs;

static HMODULE WINAPI Hook_LoadLibraryA(LPCSTR name);
static HMODULE WINAPI Hook_LoadLibraryExA(LPCSTR name, HANDLE f, DWORD flags);
typedef void(__fastcall* PFN_ConsoleWrite)(void* cstr);
static PFN_ConsoleWrite o_ConsoleWrite, o_ConsoleWriteL;
static void __fastcall Hook_ConsoleWrite(void* s);
static void __fastcall Hook_ConsoleWriteL(void* s);

static bool IsGameModule(HMODULE m, char* outName, size_t n)
{
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(m, path, MAX_PATH)) return false;
    if (_wcsnicmp(path, g_gameRoot, wcslen(g_gameRoot)) != 0) return false;
    if (m == g_self) return false;
    const wchar_t* fn = wcsrchr(path, L'\\');
    WideCharToMultiByte(CP_ACP, 0, fn ? fn + 1 : path, -1, outName, (int)n, nullptr, nullptr);
    return true;
}

static void ScanModules()
{
    EnterCriticalSection(&g_modCs);
    HMODULE mods[512]; DWORD needed = 0;
    if (EnumProcessModules(GetCurrentProcess(), mods, sizeof(mods), &needed)) {
        int count = (int)(needed / sizeof(HMODULE));
        for (int i = 0; i < count && i < 512; ++i) {
            bool known = false;
            for (int k = 0; k < g_seenCount; ++k) if (g_seen[k] == mods[i]) { known = true; break; }
            if (known) continue;
            char name[MAX_PATH];
            if (!IsGameModule(mods[i], name, sizeof(name))) continue;
            if (g_seenCount < 64) g_seen[g_seenCount++] = mods[i];
            PatchIAT(mods[i], "kernel32.dll", "LoadLibraryA", (void*)Hook_LoadLibraryA);
            PatchIAT(mods[i], "kernel32.dll", "LoadLibraryExA", (void*)Hook_LoadLibraryExA);
            if (void* o = PatchIAT(mods[i], "MSystem.dll", "ConsoleWrite", (void*)Hook_ConsoleWrite)) if (o != (void*)Hook_ConsoleWrite) o_ConsoleWrite = (PFN_ConsoleWrite)o;
            if (void* o = PatchIAT(mods[i], "MSystem.dll", "ConsoleWriteL", (void*)Hook_ConsoleWriteL)) if (o != (void*)Hook_ConsoleWriteL) o_ConsoleWriteL = (PFN_ConsoleWrite)o;
            Log("module %s at %p", name, mods[i]);
            OnModuleLoaded(mods[i], name);
        }
    }
    LeaveCriticalSection(&g_modCs);
}

// Engine console -> RiddickFix.log (diagnostics)
static void LogCStr(void* cstr)
{
    __try {
        auto vt = *(void***)cstr;
        auto str = ((const char* (__fastcall*)(void*))vt[0x28 / 8])(cstr);
        if (str && *str) Log("con: %s", str);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static void __fastcall Hook_ConsoleWrite(void* s)  { if (g_cfg.engineLog) LogCStr(s); o_ConsoleWrite(s); }
static void __fastcall Hook_ConsoleWriteL(void* s) { if (g_cfg.engineLog) LogCStr(s); o_ConsoleWriteL(s); }

static HMODULE WINAPI Hook_LoadLibraryA(LPCSTR name)
{
    HMODULE m = LoadLibraryA(name);
    if (m) ScanModules();
    return m;
}
static HMODULE WINAPI Hook_LoadLibraryExA(LPCSTR name, HANDLE f, DWORD flags)
{
    HMODULE m = LoadLibraryExA(name, f, flags);
    if (m) ScanModules();
    return m;
}

void OnModuleLoaded(HMODULE mod, const char* name)
{
    if (!_stricmp(name, "RndrGL.dll")) {
        GL_PatchModule(mod);
        Display_PatchModule(mod);
    }
    Files_PatchModule(mod);
    if (!_stricmp(name, "MSystem.dll")) Fov_PatchMSystem(mod);
    if (!_stricmp(name, "SbzEngine.exe")) Prompts_PatchExe(mod);
    if (!_strnicmp(name, "GameClasses", 11)) Prompts_PatchGameClasses(mod);
    Input_PatchModule(mod, name);
}

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = h;
        DisableThreadLibraryCalls(h);
        InitializeCriticalSection(&g_modCs);
        ProxyInit();
        GetModuleFileNameW(h, g_dllDir, MAX_PATH);
        if (wchar_t* p = wcsrchr(g_dllDir, L'\\')) *p = 0;
        // game root = two levels above System\Win64_AMD64
        wcscpy_s(g_gameRoot, g_dllDir);
        for (int i = 0; i < 2; ++i) if (wchar_t* p = wcsrchr(g_gameRoot, L'\\')) *p = 0;
        LogInit(g_dllDir);
        ConfigLoad();
        Log("RiddickFix loaded, game root %ls", g_gameRoot);
        if (g_cfg.dpiAware) {
            typedef BOOL(WINAPI* SetCtx)(DPI_AWARENESS_CONTEXT);
            if (auto f = (SetCtx)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"))
                Log("DPI awareness: %s (err %lu)", f(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) ? "per-monitor v2" : "FAILED", GetLastError());
        }
        Display_PrepareConfig();
        FontGen_Run();
        MenuGen_Run();
        StringGen_Run();
        Input_Init();
        ScanModules();
    }
    return TRUE;
}
