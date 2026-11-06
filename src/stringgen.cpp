// Static string table entries with keyboard wording ("Press ENTER", the HUD help
// hint). The tables are served from patched copies (EN is UTF-16LE with BOM,
// DE/FR are Latin-1); every other entry stays byte-identical.
#include "common.h"
#include <string>
#include <vector>

void Files_AddRedirect(const char* relLower, const std::wstring& target);
uint8_t Glyph_Char(const char* name);
extern bool g_glyphFonts;

static bool ReadAll(const std::wstring& p, std::string& out)
{
    FILE* f = _wfopen(p.c_str(), L"rb");
    if (!f) return false;
    char buf[65536]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return true;
}

// Replace the quoted value of *KEY. Text is handled as a sequence of code units
// (1 byte for Latin-1, 2 bytes for UTF-16LE) so both encodings share one routine.
static bool SetValue(std::string& data, bool wide, const char* key, const std::string& valueLatin1)
{
    auto enc = [&](const std::string& s) {
        if (!wide) return s;
        std::string w;
        for (unsigned char c : s) { w += (char)c; w += '\0'; }
        return w;
    };
    std::string k = enc(std::string("*") + key);
    size_t p = 0;
    while ((p = data.find(k, p)) != std::string::npos) {
        size_t after = p + k.size();
        size_t unit = wide ? 2 : 1;
        // must be followed by whitespace, then a quote
        size_t q = after;
        while (q + unit <= data.size() && (data[q] == ' ' || data[q] == '\t') && (!wide || data[q + 1] == 0)) q += unit;
        if (q == after || q + unit > data.size() || data[q] != '"' || (wide && data[q + 1] != 0)) { p = after; continue; }
        size_t start = q + unit, end = start;
        while (end + unit <= data.size() && !(data[end] == '"' && (!wide || data[end + 1] == 0))) end += unit;
        if (end + unit > data.size()) return false;
        data.replace(start, end - start, enc(valueLatin1));
        return true;
    }
    return false;
}

void StringGen_Run()
{
    if (!g_cfg.xboxPrompts || !g_glyphFonts) return;
    uint8_t start = Glyph_Char("START"), help = Glyph_Char("DPAD_DOWN");
    if (!start || !help) return;
    std::wstring cache = std::wstring(g_dllDir) + L"\\RiddickFix_cache";
    CreateDirectoryW(cache.c_str(), nullptr);

    struct Tab { const wchar_t* path; const char* rel; const wchar_t* out; } tabs[] = {
        { L"\\Content\\Registry\\StringTable_Eng.txt",     "content\\registry\\stringtable_eng.txt",     L"\\StringTable_Eng.txt" },
        { L"\\Content_Ger\\Registry\\StringTable_Ger.txt", "content_ger\\registry\\stringtable_ger.txt", L"\\StringTable_Ger.txt" },
        { L"\\Content_Fre\\Registry\\StringTable_Fre.txt", "content_fre\\registry\\stringtable_fre.txt", L"\\StringTable_Fre.txt" },
    };
    for (auto& t : tabs) {
        std::string data;
        if (!ReadAll(std::wstring(g_gameRoot) + t.path, data)) continue;
        bool wide = data.size() >= 2 && (unsigned char)data[0] == 0xFF && (unsigned char)data[1] == 0xFE;
        int n = 0;
        // "Press START" (the title screen text does not resolve string references)
        std::string g(1, (char)start);
        std::string pressStart = wcsstr(t.out, L"Ger") ? g + " dr\xFC" "cken"
                               : wcsstr(t.out, L"Fre") ? "Appuyer sur " + g
                               : "Press " + g;
        n += SetValue(data, wide, "MENU_PRESSSTART", pressStart);
        // HUD help hint: keep the word, add the d-pad icon (the F1 key cap is hidden)
        std::string helpWord;
        {
            // read the current word to keep the translation
            std::string k = wide ? std::string("*\0G\0A\0M\0E\0M\0S\0G\0_\0H\0E\0L\0P\0", 26) : "*GAMEMSG_HELP";
            size_t p = data.find(k);
            if (p != std::string::npos) {
                size_t q = data.find(wide ? std::string("\"\0", 2) : "\"", p + k.size());
                if (q != std::string::npos) {
                    for (size_t i = q + (wide ? 2 : 1); i < data.size(); i += wide ? 2 : 1) {
                        if (data[i] == '"' && (!wide || data[i + 1] == 0)) break;
                        helpWord += data[i];
                    }
                }
            }
        }
        if (!helpWord.empty() && (unsigned char)helpWord.back() != help)
            n += SetValue(data, wide, "GAMEMSG_HELP", helpWord + " " + (char)help);
        std::wstring dst = cache + t.out;
        if (FILE* f = _wfopen(dst.c_str(), L"wb")) { fwrite(data.data(), 1, data.size(), f); fclose(f); }
        else continue;
        Files_AddRedirect(t.rel, dst);
        Log("stringgen: %ls: %d entries patched (%s)", t.out + 1, n, wide ? "UTF-16" : "Latin-1");
    }
}
