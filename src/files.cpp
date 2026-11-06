// File redirection: selected game data files are served from generated copies
// (Xbox-style menu button hints, console wording). Originals stay untouched.
#include "common.h"
#include <string>
#include <map>
#include <mutex>
#include <vector>

static std::map<std::string, std::wstring> g_redirect;   // lower-case relative path -> generated file
static std::mutex g_mx;
static int g_logged;

static std::vector<std::string> g_block;
void Files_AddBlock(const char* relLower) { std::lock_guard<std::mutex> l(g_mx); g_block.push_back(relLower); }

void Files_AddRedirect(const char* relLower, const std::wstring& target)
{
    std::lock_guard<std::mutex> l(g_mx);
    g_redirect[relLower] = target;
}

static HANDLE WINAPI Hook_CreateFileA(LPCSTR name, DWORD acc, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl)
{
    if (name) {
        std::string n = name;
        for (auto& c : n) c = (char)tolower((unsigned char)c), c = c == '/' ? (char)0x5C : c;
        if (g_cfg.engineLog && g_logged < 400 && (n.find(".xrg") != std::string::npos || n.find(".xcr") != std::string::npos ||
                                                 n.find(".txt") != std::string::npos || n.find(".xfc") != std::string::npos || n.find(".xtc") != std::string::npos)) {
            ++g_logged; Log("file: %s", name);
        }
        std::lock_guard<std::mutex> l(g_mx);
        for (auto& b : g_block)
            if (n.size() >= b.size() && n.compare(n.size() - b.size(), b.size(), b) == 0) {
                SetLastError(ERROR_FILE_NOT_FOUND);
                return INVALID_HANDLE_VALUE;
            }
        for (auto& r : g_redirect) {
            if (n.size() >= r.first.size() && n.compare(n.size() - r.first.size(), r.first.size(), r.first) == 0 &&
                (acc & GENERIC_WRITE) == 0) {
                HANDLE h = CreateFileW(r.second.c_str(), acc, share, sa, disp, flags, tmpl);
                if (h != INVALID_HANDLE_VALUE) return h;
            }
        }
    }
    return CreateFileA(name, acc, share, sa, disp, flags, tmpl);
}

static bool IsBlocked(const char* name)
{
    std::string n = name;
    for (auto& c : n) c = (char)tolower((unsigned char)c), c = c == '/' ? (char)0x5C : c;
    std::lock_guard<std::mutex> l(g_mx);
    for (auto& b : g_block)
        if (n.size() >= b.size() && n.compare(n.size() - b.size(), b.size(), b) == 0) return true;
    return false;
}

static DWORD WINAPI Hook_GetFileAttributesA(LPCSTR name)
{
    if (name && IsBlocked(name)) { SetLastError(ERROR_FILE_NOT_FOUND); return INVALID_FILE_ATTRIBUTES; }
    return GetFileAttributesA(name);
}

void Files_PatchModule(HMODULE mod)
{
    PatchIAT(mod, "kernel32.dll", "GetFileAttributesA", (void*)Hook_GetFileAttributesA);
    PatchIAT(mod, "kernel32.dll", "CreateFileA", (void*)Hook_CreateFileA);
}
