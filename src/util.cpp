#include "common.h"
#include <stdarg.h>
#include <string.h>
#include <vector>

static FILE* g_log;
static CRITICAL_SECTION g_logCs;

void LogInit(const wchar_t* dir)
{
    InitializeCriticalSection(&g_logCs);
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\RiddickFix.log", dir);
    g_log = _wfsopen(path, L"w", _SH_DENYWR);
}

void Log(const char* fmt, ...)
{
    if (!g_log) return;
    EnterCriticalSection(&g_logCs);
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(g_log, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
    LeaveCriticalSection(&g_logCs);
}

bool WriteMem(void* dst, const void* src, size_t n)
{
    DWORD old;
    if (!VirtualProtect(dst, n, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(dst, src, n);
    VirtualProtect(dst, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), dst, n);
    return true;
}

void* PatchIAT(HMODULE module, const char* importDll, const char* func, void* replacement)
{
    if (!module) return nullptr;
    auto base = (uint8_t*)module;
    auto dos = (IMAGE_DOS_HEADER*)base;
    auto nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return nullptr;
    for (auto imp = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); imp->Name; ++imp) {
        if (_stricmp((char*)(base + imp->Name), importDll) != 0) continue;
        auto thunk = (IMAGE_THUNK_DATA*)(base + imp->FirstThunk);
        auto orig  = (IMAGE_THUNK_DATA*)(base + (imp->OriginalFirstThunk ? imp->OriginalFirstThunk : imp->FirstThunk));
        for (; orig->u1.AddressOfData; ++orig, ++thunk) {
            if (IMAGE_SNAP_BY_ORDINAL(orig->u1.Ordinal)) continue;
            auto ibn = (IMAGE_IMPORT_BY_NAME*)(base + orig->u1.AddressOfData);
            if (strcmp((char*)ibn->Name, func) != 0) continue;
            void* old = (void*)thunk->u1.Function;
            if (old == replacement) return old;
            WriteMem(&thunk->u1.Function, &replacement, sizeof(void*));
            return old;
        }
    }
    return nullptr;
}

uint8_t* FindPattern(HMODULE module, const char* pattern)
{
    std::vector<int> pat;
    for (const char* p = pattern; *p;) {
        while (*p == ' ') ++p;
        if (!*p) break;
        if (p[0] == '?') { pat.push_back(-1); p += (p[1] == '?') ? 2 : 1; }
        else { pat.push_back((int)strtoul(p, nullptr, 16)); p += 2; }
    }
    auto base = (uint8_t*)module;
    auto nt = (IMAGE_NT_HEADERS*)(base + ((IMAGE_DOS_HEADER*)base)->e_lfanew);
    auto sec = IMAGE_FIRST_SECTION(nt);
    for (int s = 0; s < nt->FileHeader.NumberOfSections; ++s, ++sec) {
        if (!(sec->Characteristics & (IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_CNT_INITIALIZED_DATA))) continue;
        uint8_t* start = base + sec->VirtualAddress;
        size_t len = sec->Misc.VirtualSize;
        if (len < pat.size()) continue;
        for (size_t i = 0; i + pat.size() <= len; ++i) {
            size_t k = 0;
            for (; k < pat.size(); ++k) if (pat[k] >= 0 && start[i + k] != pat[k]) break;
            if (k == pat.size()) return start + i;
        }
    }
    return nullptr;
}

void* AllocNear(void* nearAddr, size_t size)
{
    SYSTEM_INFO si; GetSystemInfo(&si);
    uintptr_t gran = si.dwAllocationGranularity;
    uintptr_t target = (uintptr_t)nearAddr & ~(gran - 1);
    for (uintptr_t d = gran; d < 0x70000000; d += gran) {
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            uintptr_t a = target + sgn * (intptr_t)d;
            if (sgn < 0 && d > target) continue;
            void* p = VirtualAlloc((void*)a, size, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
            if (p) return p;
        }
    }
    return nullptr;
}

bool PatchJump(void* at, void* to, size_t overwrite, const uint8_t* expect)
{
    if (overwrite < 14) return false;
    if (expect && memcmp(at, expect, overwrite) != 0) return false;
    uint8_t buf[64];
    memset(buf, 0x90, sizeof(buf));
    buf[0] = 0xFF; buf[1] = 0x25; *(uint32_t*)(buf + 2) = 0;  // jmp [rip+0]
    *(uint64_t*)(buf + 6) = (uint64_t)to;
    return WriteMem(at, buf, overwrite);
}

// Detour `target`: the first `len` bytes (complete, position independent
// instructions, >= 14) are verified against `expect`, copied into a trampoline
// and replaced by a jump to `detour`. Returns the trampoline (call it to run the
// original) or nullptr.
void* HookFunction(void* target, size_t len, const uint8_t* expect, void* detour)
{
    if (len < 14 || len > 48) return nullptr;
    if (expect && memcmp(target, expect, len) != 0) return nullptr;
    uint8_t* tramp = (uint8_t*)VirtualAlloc(nullptr, 64, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (!tramp) return nullptr;
    memcpy(tramp, target, len);
    tramp[len] = 0xFF; tramp[len + 1] = 0x25; *(uint32_t*)(tramp + len + 2) = 0;
    *(uint64_t*)(tramp + len + 6) = (uint64_t)((uint8_t*)target + len);
    if (!PatchJump(target, detour, len)) return nullptr;
    return tramp;
}
