// Console button hints for the menus.
//
// The PC menus (Content\GUI\CubeWnd) draw their hints as *OUTSIDEBUTTON - the
// grey PC key cap. The Xbox variant *BUTTONDESCn a/b/x/y,<caption> (drawn with
// GUI_Button_A_32 .. _Y_32 through BUTTON_DESCRIPTOR) is still parsed by the PC
// build, the lines are merely commented out. We generate a copy of the menu
// script with the console hints active and let the engine load it instead of
// the precompiled CubeWnd.XCR (the original files stay untouched).
#include "common.h"
#include <string>
#include <vector>
#include <regex>

void Files_AddRedirect(const char* relLower, const std::wstring& target);
void Files_AddBlock(const char* relLower);

static bool ReadAll(const std::wstring& p, std::string& out)
{
    FILE* f = _wfopen(p.c_str(), L"rb");
    if (!f) return false;
    char buf[65536]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return true;
}

static void ReplaceAll(std::string& s, const std::string& a, const std::string& b)
{
    for (size_t p = 0; (p = s.find(a, p)) != std::string::npos; p += b.size()) s.replace(p, a.size(), b);
}

static std::string Transform(const std::string& src, int& outsideCount, int& descCount)
{
    if (!g_cfg.xboxPrompts || !g_cfg.consoleMenus) {
        outsideCount = descCount = 0;
        return src;
    }
    std::string out;
    out.reserve(src.size() + 4096);
    size_t pos = 0;
    while (pos < src.size()) {
        size_t e = src.find('\n', pos);
        size_t end = (e == std::string::npos) ? src.size() : e + 1;
        std::string line = src.substr(pos, end - pos);
        pos = end;
        std::string eol;
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) { eol.insert(eol.begin(), line.back()); line.pop_back(); }

        size_t ns = line.find_first_not_of(" \t");
        std::string indent = ns == std::string::npos ? line : line.substr(0, ns);
        std::string body = ns == std::string::npos ? "" : line.substr(ns);

        // 1) re-enable commented console hints (not the dynamic delete hint)
        if (body.rfind("//", 0) == 0) {
            std::string inner = body.substr(2);
            size_t k = inner.find_first_not_of(" \t");
            if (k != std::string::npos && inner.compare(k, 11, "*BUTTONDESC") == 0 && inner.find("WARNING") == std::string::npos) {
                body = inner.substr(k);
                ++descCount;
            }
        }
        // 2) PC key caps -> console hint (bottom right = A, left = B). Where the
        //    original console hint follows as a commented line (CubeInv0: page
        //    arrows, back, continue), the key cap is simply dropped.
        bool consoleFollows = false;
        if (body.rfind("*OUTSIDEBUTTON", 0) == 0) {
            size_t look = pos;
            for (int i = 0; i < 6 && look < src.size(); ++i) {
                size_t le = src.find('\n', look);
                std::string nl = src.substr(look, (le == std::string::npos ? src.size() : le) - look);
                look = le == std::string::npos ? src.size() : le + 1;
                size_t f = nl.find_first_not_of(" \t");
                if (f == std::string::npos) continue;
                if (nl.compare(f, 13, "//*BUTTONDESC") == 0 || nl.compare(f, 14, "// *BUTTONDESC") == 0) { consoleFollows = true; break; }
                if (nl.compare(f, 14, "*OUTSIDEBUTTON") != 0) break;
            }
        }
        if (consoleFollows) {
            body = "//" + body;
            ++outsideCount;
        }
        else if (body.rfind("*OUTSIDEBUTTON", 0) == 0) {
            size_t c0 = body.find("caption='");
            size_t p0 = body.find("pos='");
            if (c0 != std::string::npos && p0 != std::string::npos) {
                size_t c1 = body.find('\'', c0 + 9);
                std::string caption = body.substr(c0 + 9, c1 - (c0 + 9));
                bool right = body.compare(p0 + 5, 6, "bottom") == 0 && body.compare(p0 + 11, 5, "right") == 0;
                body = right ? "*BUTTONDESC0 a," + caption : "*BUTTONDESC1 b," + caption;
                ++outsideCount;
            }
        }
        // 3) "Back" always lives on B on the console
        if (body.rfind("*BUTTONDESC0 a,\xA7LMENU_BACK", 0) == 0) body = "*BUTTONDESC1 b," + body.substr(15);
        out += indent + body + eol;
    }
    return out;
}

void MenuGen_Run()
{
    if (!(g_cfg.xboxPrompts && g_cfg.consoleMenus) && !g_cfg.skipIntro) return;
    std::wstring cache = std::wstring(g_dllDir) + L"\\RiddickFix_cache";
    CreateDirectoryW(cache.c_str(), nullptr);
    struct { const wchar_t* file; const char* xcr; const char* xrg; } menus[] = {
        { L"CubeWnd",  "content\\gui\\cubewnd.xcr",  "content\\gui\\cubewnd.xrg" },
        { L"CubeInv0", "content\\gui\\cubeinv0.xcr", "content\\gui\\cubeinv0.xrg" },
    };
    for (auto& m : menus) {
        std::string src;
        std::wstring in = std::wstring(g_gameRoot) + L"\\Content\\Gui\\" + m.file + L".xrg";
        if (!ReadAll(in, src)) { Log("menugen: %ls not found", in.c_str()); continue; }
        int outside = 0, desc = 0;
        std::string out = Transform(src, outside, desc);
        if (g_cfg.skipIntro) {
            // startup chain: main_blank_logos -> esrb -> vug_logo -> ... -> sbz_logo -> main
            ReplaceAll(out, "cg_rootmenu \\\"esrb\\\"", "cg_rootmenu \\\"main\\\"");
            ReplaceAll(out, "cg_rootmenu \\\"vug_logo\\\"", "cg_rootmenu \\\"main\\\"");
        }
        std::wstring dst = cache + L"\\" + m.file + L".xrg";
        if (FILE* f = _wfopen(dst.c_str(), L"wb")) { fwrite(out.data(), 1, out.size(), f); fclose(f); }
        else continue;
        Files_AddBlock(m.xcr);
        Files_AddRedirect(m.xrg, dst);
        Log("menugen: %ls: %d key caps -> console hints, %d hints re-enabled, skip intro %d", m.file, outside, desc, g_cfg.skipIntro);
    }
}
