// Controller glyphs inside the game fonts.
//
// The text renderer draws strings only, so (as in our Dark Athena mod) the fonts
// carry the button icons: unused Latin-1 characters (Spanish/Icelandic letters
// that no EN/DE/FR text uses) are re-pointed to icons painted into the free
// rows of the font atlas. The atlas is switched from 8-bit alpha (0x40000, where
// GL_MODULATE drops the texture colour) to 32-bit colour (0x800 - the format
// MiniFont2.xfc/Numbers.xfc already use), so the icons keep their colours.
//
// A/B/X/Y are the original Xbox icons from the game's own GUI.xtc; the other
// buttons are our own artwork (glyph_art.h). The generated fonts are written to
// a cache folder and served in place of Content\Fonts\*.xfc.
#include "common.h"
#include "glyph_art.h"
#include <vector>
#include <string>
#include <map>
#include <algorithm>
#include <math.h>

void Files_AddRedirect(const char* relLower, const std::wstring& target);
extern bool g_glyphFonts;

struct Img { int w = 0, h = 0; std::vector<uint8_t> px; };   // RGBA

// glyph -> donor character
struct GlyphSlot { const char* name; uint8_t ch; };
static const GlyphSlot kSlots[] = {
    { "A", 0xF0 }, { "B", 0xF1 }, { "X", 0xF2 }, { "Y", 0xF3 },
    { "LB", 0xF5 }, { "RB", 0xF8 }, { "LT", 0xFA }, { "RT", 0xFD },
    { "BACK", 0xE3 }, { "START", 0xE5 }, { "LS", 0xEC }, { "RS", 0xED },
    { "DPAD", 0xD0 }, { "DPAD_DOWN", 0xD1 },
};

uint8_t Glyph_Char(const char* name)
{
    for (auto& s : kSlots) if (!strcmp(s.name, name)) return s.ch;
    return 0;
}

static bool ReadFileAll(const std::wstring& p, std::vector<uint8_t>& out)
{
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz; GetFileSizeEx(h, &sz);
    out.resize((size_t)sz.QuadPart);
    DWORD rd = 0; BOOL ok = ReadFile(h, out.data(), (DWORD)out.size(), &rd, nullptr);
    CloseHandle(h);
    return ok && rd == out.size();
}

// ---- DXT5 decode (for the GUI.xtc icons) -----------------------------------------
static void DecodeDXT5(const uint8_t* src, int w, int h, Img& img)
{
    img.w = w; img.h = h; img.px.assign(w * h * 4, 0);
    for (int by = 0; by < h; by += 4)
        for (int bx = 0; bx < w; bx += 4, src += 16) {
            uint8_t a0 = src[0], a1 = src[1], ap[8] = { a0, a1 };
            if (a0 > a1) for (int i = 1; i < 7; ++i) ap[i + 1] = (uint8_t)(((7 - i) * a0 + i * a1) / 7);
            else { for (int i = 1; i < 5; ++i) ap[i + 1] = (uint8_t)(((5 - i) * a0 + i * a1) / 5); ap[6] = 0; ap[7] = 255; }
            uint64_t ab = 0; for (int i = 0; i < 6; ++i) ab |= (uint64_t)src[2 + i] << (8 * i);
            uint16_t c0 = src[8] | (src[9] << 8), c1 = src[10] | (src[11] << 8);
            uint32_t cb = src[12] | (src[13] << 8) | (src[14] << 16) | ((uint32_t)src[15] << 24);
            auto ex = [](uint16_t c, uint8_t* o) { o[0] = ((c >> 11) & 31) * 255 / 31; o[1] = ((c >> 5) & 63) * 255 / 63; o[2] = (c & 31) * 255 / 31; };
            uint8_t cp[4][3]; ex(c0, cp[0]); ex(c1, cp[1]);
            for (int k = 0; k < 3; ++k) { cp[2][k] = (uint8_t)((2 * cp[0][k] + cp[1][k]) / 3); cp[3][k] = (uint8_t)((cp[0][k] + 2 * cp[1][k]) / 3); }
            for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) {
                if (bx + x >= w || by + y >= h) continue;
                int n = y * 4 + x;
                uint8_t* o = &img.px[((by + y) * w + bx + x) * 4];
                const uint8_t* c = cp[(cb >> (2 * n)) & 3];
                o[0] = c[0]; o[1] = c[1]; o[2] = c[2]; o[3] = ap[(ab >> (3 * n)) & 7];
            }
        }
}

// IMAGEDIRECTORY5 record follows the texture name: 9 dwords
// [0x300, flags, payload, w, h, format, 0, offset, ...]; offset -> table of mip
// offsets; mip: 7-dword header, 16-byte prefix (4, size, 0, 16), DXT5 blocks.
static bool LoadXtcIcon(const std::vector<uint8_t>& d, const char* name, Img& out)
{
    size_t nl = strlen(name);
    for (size_t i = 0; i + nl + 48 < d.size(); ++i) {
        if (memcmp(&d[i], name, nl) != 0 || d[i + nl] != ' ' && d[i + nl] != 0) continue;
        for (size_t r = i + nl; r < i + nl + 24; ++r) {
            const uint32_t* f = (const uint32_t*)&d[r];
            if (f[0] != 0x300 || f[5] != 0x800 || f[3] == 0 || f[3] > 256 || f[4] == 0 || f[4] > 256) continue;
            uint32_t off = f[7];
            if (off + 4 > d.size()) continue;
            uint32_t m0 = *(const uint32_t*)&d[off];
            if (m0 + 44 > d.size()) continue;
            const uint32_t* mh = (const uint32_t*)&d[m0];
            int w = (int)mh[3], h = (int)mh[4];
            size_t n = (size_t)((w + 3) / 4) * ((h + 3) / 4) * 16;
            if (mh[5] != 0x800 || m0 + 44 + n > d.size()) continue;
            DecodeDXT5(&d[m0 + 44], w, h, out);
            return true;
        }
    }
    return false;
}

// ---- DXT5 encode (squeezed button icons go back into GUI.xtc) --------------------------
static uint16_t To565(const float c[3])
{
    auto q = [](float v, int m) { int i = (int)lroundf(std::min(255.0f, std::max(0.0f, v)) * m / 255.0f); return i; };
    return (uint16_t)((q(c[0], 31) << 11) | (q(c[1], 63) << 5) | q(c[2], 31));
}

static void EncodeDXT5Block(const uint8_t (&px)[16][4], uint8_t* o)
{
    // alpha: 8-value mode, a0 = max > a1 = min
    uint8_t amax = 0, amin = 255;
    for (auto& p : px) { amax = std::max(amax, p[3]); amin = std::min(amin, p[3]); }
    o[0] = amax; o[1] = amin;
    uint64_t ab = 0;
    if (amax != amin) {
        int pal[8] = { amax, amin };
        for (int i = 1; i < 7; ++i) pal[i + 1] = ((7 - i) * amax + i * amin) / 7;
        for (int n = 0; n < 16; ++n) {
            int best = 0, bd = 1 << 30;
            for (int k = 0; k < 8; ++k) { int dd = abs(pal[k] - px[n][3]); if (dd < bd) { bd = dd; best = k; } }
            ab |= (uint64_t)best << (3 * n);
        }
    }
    for (int i = 0; i < 6; ++i) o[2 + i] = (uint8_t)(ab >> (8 * i));
    // colour: endpoints = extremes along the principal axis of the visible pixels
    float mean[3] = {}; int cnt = 0;
    for (auto& p : px) if (p[3] > 0) { for (int k = 0; k < 3; ++k) mean[k] += p[k]; ++cnt; }
    if (!cnt) { memset(o + 8, 0, 8); return; }
    for (float& m : mean) m /= cnt;
    float cov[6] = {};
    for (auto& p : px) if (p[3] > 0) {
        float d0 = p[0] - mean[0], d1 = p[1] - mean[1], d2 = p[2] - mean[2];
        cov[0] += d0 * d0; cov[1] += d0 * d1; cov[2] += d0 * d2; cov[3] += d1 * d1; cov[4] += d1 * d2; cov[5] += d2 * d2;
    }
    float ax[3] = { 1, 1, 1 };
    for (int it = 0; it < 8; ++it) {
        float n0 = cov[0] * ax[0] + cov[1] * ax[1] + cov[2] * ax[2];
        float n1 = cov[1] * ax[0] + cov[3] * ax[1] + cov[4] * ax[2];
        float n2 = cov[2] * ax[0] + cov[4] * ax[1] + cov[5] * ax[2];
        float l = sqrtf(n0 * n0 + n1 * n1 + n2 * n2);
        if (l < 1e-6f) break;
        ax[0] = n0 / l; ax[1] = n1 / l; ax[2] = n2 / l;
    }
    float tmin = 1e9f, tmax = -1e9f;
    for (auto& p : px) if (p[3] > 0) {
        float t = (p[0] - mean[0]) * ax[0] + (p[1] - mean[1]) * ax[1] + (p[2] - mean[2]) * ax[2];
        tmin = std::min(tmin, t); tmax = std::max(tmax, t);
    }
    float e0[3], e1[3];
    for (int k = 0; k < 3; ++k) { e0[k] = mean[k] + ax[k] * tmax; e1[k] = mean[k] + ax[k] * tmin; }
    uint16_t c0 = To565(e0), c1 = To565(e1);
    if (c0 < c1) std::swap(c0, c1);
    uint32_t idx = 0;
    if (c0 != c1) {
        auto ex = [](uint16_t c, float* f) { f[0] = ((c >> 11) & 31) * 255.0f / 31; f[1] = ((c >> 5) & 63) * 255.0f / 63; f[2] = (c & 31) * 255.0f / 31; };
        float pal[4][3]; ex(c0, pal[0]); ex(c1, pal[1]);
        for (int k = 0; k < 3; ++k) { pal[2][k] = (2 * pal[0][k] + pal[1][k]) / 3; pal[3][k] = (pal[0][k] + 2 * pal[1][k]) / 3; }
        for (int n = 0; n < 16; ++n) {
            int best = 0; float bd = 1e30f;
            for (int k = 0; k < 4; ++k) {
                float dd = 0; for (int j = 0; j < 3; ++j) { float t = pal[k][j] - px[n][j]; dd += t * t; }
                if (dd < bd) { bd = dd; best = k; }
            }
            idx |= (uint32_t)best << (2 * n);
        }
    }
    o[8] = (uint8_t)c0; o[9] = (uint8_t)(c0 >> 8); o[10] = (uint8_t)c1; o[11] = (uint8_t)(c1 >> 8);
    memcpy(o + 12, &idx, 4);
}

// ---- image helpers ----------------------------------------------------------------
static Img Resample(const Img& s, int w, int h)   // area average, premultiplied
{
    Img d; d.w = w; d.h = h; d.px.assign(w * h * 4, 0);
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) {
        double x0 = (double)x * s.w / w, x1 = (double)(x + 1) * s.w / w;
        double y0 = (double)y * s.h / h, y1 = (double)(y + 1) * s.h / h;
        double acc[4] = {}, wsum = 0;
        for (int sy = (int)y0; sy < (int)ceil(y1) && sy < s.h; ++sy)
            for (int sx = (int)x0; sx < (int)ceil(x1) && sx < s.w; ++sx) {
                double wx = std::min(x1, sx + 1.0) - std::max(x0, (double)sx);
                double wy = std::min(y1, sy + 1.0) - std::max(y0, (double)sy);
                double wt = wx * wy; if (wt <= 0) continue;
                const uint8_t* p = &s.px[(sy * s.w + sx) * 4];
                double a = p[3] / 255.0;
                acc[0] += p[0] * a * wt; acc[1] += p[1] * a * wt; acc[2] += p[2] * a * wt; acc[3] += p[3] * wt; wsum += wt;
            }
        uint8_t* o = &d.px[(y * w + x) * 4];
        if (wsum > 0 && acc[3] > 0) {
            double a = acc[3] / wsum;
            for (int k = 0; k < 3; ++k) o[k] = (uint8_t)std::min(255.0, acc[k] / wsum / (a / 255.0) + 0.5);
            o[3] = (uint8_t)(a + 0.5);
        }
    }
    return d;
}

static void EncodeDXT5(const Img& img, uint8_t* dst)
{
    for (int by = 0; by < img.h || by == 0; by += 4)
        for (int bx = 0; bx < img.w || bx == 0; bx += 4, dst += 16) {
            uint8_t px[16][4];
            for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) {
                int sx = std::min(bx + x, img.w - 1), sy = std::min(by + y, img.h - 1);   // replicate edge for 1x1/2x2 mips
                memcpy(px[y * 4 + x], &img.px[(sy * img.w + sx) * 4], 4);
            }
            EncodeDXT5Block(px, dst);
        }
}

static Img CropAlpha(const Img& s)
{
    int x0 = s.w, y0 = s.h, x1 = -1, y1 = -1;
    for (int y = 0; y < s.h; ++y) for (int x = 0; x < s.w; ++x)
        if (s.px[(y * s.w + x) * 4 + 3] > 8) { x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y); }
    if (x1 < 0) return s;
    Img d; d.w = x1 - x0 + 1; d.h = y1 - y0 + 1; d.px.resize(d.w * d.h * 4);
    for (int y = 0; y < d.h; ++y) memcpy(&d.px[y * d.w * 4], &s.px[((y + y0) * s.w + x0) * 4], d.w * 4);
    return d;
}

// ---- font container ------------------------------------------------------------------
struct DirEnt { std::string name; uint32_t slot, next, child, data, len, dim, fmt; };

static std::vector<DirEnt> ReadDir(const std::vector<uint8_t>& d)
{
    std::vector<DirEnt> v;
    uint32_t p = *(const uint32_t*)&d[24];
    while (p + 48 <= d.size()) {
        char nm[25] = {}; memcpy(nm, &d[p], 24);
        if (!nm[0]) break;
        bool ok = true; for (char* c = nm; *c; ++c) if (*c < 32 || *c > 126) ok = false;
        if (!ok) break;
        const uint32_t* f = (const uint32_t*)&d[p + 24];
        v.push_back({ nm, p, f[0], f[1], f[2], f[3], f[4], f[5] });
        p += 48;
    }
    return v;
}

// rebuild with replaced data blocks; all pointers are absolute file offsets
static std::vector<uint8_t> Rebuild(const std::vector<uint8_t>& d, const std::map<uint32_t, std::vector<uint8_t>>& newData,
                                    const std::map<uint32_t, uint32_t>& newFmt, const std::map<uint32_t, uint32_t>& newDim)
{
    auto ents = ReadDir(d);
    uint32_t dirOff = *(const uint32_t*)&d[24];
    std::map<uint32_t, uint32_t> lens;
    for (auto& e : ents) if (e.data >= 48) lens[e.data] = std::max(lens[e.data], e.len);
    std::vector<uint8_t> out(d.begin(), d.begin() + 48);
    std::map<uint32_t, uint32_t> remap; remap[0] = 0;
    for (auto& [off, len] : lens) {
        remap[off] = (uint32_t)out.size();
        auto it = newData.find(off);
        if (it != newData.end()) out.insert(out.end(), it->second.begin(), it->second.end());
        else out.insert(out.end(), d.begin() + off, d.begin() + off + len);
    }
    uint32_t newDir = (uint32_t)out.size();
    int64_t delta = (int64_t)newDir - dirOff;
    for (auto& e : ents) {
        uint8_t rec[48] = {};
        memcpy(rec, e.name.c_str(), std::min<size_t>(24, e.name.size()));
        uint32_t len = e.len;
        auto it = newData.find(e.data);
        if (it != newData.end() && len) len = (uint32_t)it->second.size();
        uint32_t f[6] = { e.next ? (uint32_t)(e.next + delta) : 0, e.child ? (uint32_t)(e.child + delta) : 0,
                          remap.count(e.data) ? remap[e.data] : (uint32_t)(e.data + delta), len,
                          newDim.count(e.slot) ? newDim.at(e.slot) : e.dim,
                          newFmt.count(e.slot) ? newFmt.at(e.slot) : e.fmt };
        memcpy(rec + 24, f, 24);
        out.insert(out.end(), rec, rec + 48);
    }
    *(uint32_t*)&out[24] = newDir;
    return out;
}

// Undo the horizontal 640x480 -> wide screen stretch: the drawn quad width and
// the pen advance shrink by g_uiSqueeze, the texture is sampled squeezed.
static void SqueezeChars(std::vector<uint8_t>& d, const DirEnt& cd)
{
    if (g_uiSqueeze == 1.0f) return;
    int n = (int)(cd.len / 34);
    for (int i = 0; i < n; ++i) {
        uint8_t* e = &d[cd.data + i * 34];
        *(float*)(e + 20) *= g_uiSqueeze;
        int16_t adv = *(int16_t*)(e + 30);
        *(int16_t*)(e + 30) = (int16_t)lroundf(adv * g_uiSqueeze);
    }
}

static bool WriteFileAll(const std::wstring& p, const std::vector<uint8_t>& out)
{
    HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0; WriteFile(h, out.data(), (DWORD)out.size(), &wr, nullptr); CloseHandle(h);
    return wr == out.size();
}

// fonts without icons: metrics only
static bool SqueezeFont(const std::wstring& src, const std::wstring& dst)
{
    std::vector<uint8_t> d;
    if (!ReadFileAll(src, d) || d.size() < 64 || memcmp(d.data(), "MOS DATAFILE2.0", 15) != 0) return false;
    for (auto& e : ReadDir(d))
        if (e.name == "CHARDESC" && e.data + e.len <= d.size()) {
            SqueezeChars(d, e);
            return WriteFileAll(dst, d);
        }
    return false;
}

static bool BuildFont(const std::wstring& src, const std::wstring& dst, const std::map<std::string, Img>& icons)
{
    std::vector<uint8_t> d;
    if (!ReadFileAll(src, d) || d.size() < 64 || memcmp(d.data(), "MOS DATAFILE2.0", 15) != 0) return false;
    auto ents = ReadDir(d);
    const DirEnt* cd = nullptr; std::vector<const DirEnt*> mips;
    for (auto& e : ents) { if (e.name == "CHARDESC") cd = &e; if (e.name == "MIPMAP") mips.push_back(&e); }
    if (!cd || mips.empty()) return false;
    std::sort(mips.begin(), mips.end(), [](const DirEnt* a, const DirEnt* b) { return a->len > b->len; });
    const uint32_t* mh = (const uint32_t*)&d[mips[0]->data];
    if (mh[0] != 0x300 || mh[5] != 0x40000) { Log("fontgen: unexpected atlas format %x/%x", mh[0], mh[5]); return false; }
    int W = (int)mh[3], H = (int)mh[4];
    const uint8_t* alpha = &d[mips[0]->data + 28];

    // chars
    int n = (int)(cd->len / 34);
    std::map<int, uint32_t> where;
    for (int i = 0; i < n; ++i) where[*(const uint16_t*)&d[cd->data + i * 34 + 32]] = cd->data + i * 34;
    if (!where.count('A')) return false;
    float cellH = *(const float*)&d[where['A'] + 24];
    int16_t yoff = *(const int16_t*)&d[where['A'] + 2];

    // largest empty band
    int best0 = -1, bestLen = 0;
    for (int y = 0; y < H;) {
        int y0 = y;
        while (y < H) { bool empty = true; for (int x = 0; x < W; ++x) if (alpha[y * W + x]) { empty = false; break; } if (!empty) break; ++y; }
        if (y - y0 > bestLen) { bestLen = y - y0; best0 = y0; }
        ++y;
    }
    int cell = (int)cellH;
    int H0 = H;
    bool grow = bestLen < cell + 8;
    if (grow) {
        // no room: double the atlas height, rescale all v coordinates and put
        // the icons into the new lower half
        H = H0 * 2;
        for (auto& [code, e] : where) {
            *(float*)&d[e + 8]  *= 0.5f;
            *(float*)&d[e + 16] *= 0.5f;
        }
        best0 = H0; bestLen = H0;
    }

    // colour atlas: white with original alpha
    Img atlas; atlas.w = W; atlas.h = H; atlas.px.assign(W * H * 4, 255);
    for (int i = 0; i < W * H; ++i) atlas.px[i * 4 + 3] = i < W * H0 ? alpha[i] : 0;

    int symH = (int)(cellH * 0.86f + 0.5f);
    int capCenter = (int)(cellH * 0.47f);
    int x = 0, y = (best0 + 4 + 3) & ~3, placed = 0;
    for (auto& slot : kSlots) {
        auto it = icons.find(slot.name);
        if (it == icons.end() || !where.count(slot.ch)) continue;
        const Img& ic = it->second;
        int gw = std::max(4, (int)(ic.w * (float)symH / ic.h + 0.5f));
        if (x + gw > W) { x = 0; y += cell + 4; }
        if (y + cell > best0 + bestLen) { Log("fontgen: atlas full at %s", slot.name); break; }
        Img g = Resample(ic, gw, symH);
        int gy = y + capCenter - symH / 2;
        for (int yy = 0; yy < symH; ++yy) for (int xx = 0; xx < gw; ++xx) {
            if (gy + yy < 0 || gy + yy >= H) continue;
            memcpy(&atlas.px[((gy + yy) * W + x + xx) * 4], &g.px[(yy * gw + xx) * 4], 4);
        }
        uint32_t e = where[slot.ch];
        float f[6] = { (float)x / W, (float)y / H, (float)(x + gw) / W, (float)(y + cell) / H, (float)gw, cellH };
        *(int16_t*)&d[e + 2] = yoff;
        memcpy(&d[e + 4], f, sizeof(f));
        *(int16_t*)&d[e + 30] = (int16_t)(gw + 2);
        x += ((gw + 3) & ~3) + 4;
        ++placed;
    }

    SqueezeChars(d, *cd);

    // all mip levels as 32-bit BGRA
    std::map<uint32_t, std::vector<uint8_t>> newData;
    std::map<uint32_t, uint32_t> newFmt, newDim;
    for (auto* m : mips) {
        const uint32_t* h0 = (const uint32_t*)&d[m->data];
        int mw = (int)h0[3], mhh = (int)h0[4];
        if (grow) mhh = std::max(1, mhh * H / H0);
        Img lvl = (mw == W && mhh == H) ? atlas : Resample(atlas, mw, mhh);
        std::vector<uint8_t> blk(28 + mw * mhh * 4);
        uint32_t hdr[7] = { 0x300, 0, (uint32_t)(mw * mhh * 4), (uint32_t)mw, (uint32_t)mhh, 0x800, 0 };
        memcpy(blk.data(), hdr, 28);
        for (int i = 0; i < mw * mhh; ++i) {       // RGBA -> BGRA
            uint8_t* o = &blk[28 + i * 4]; const uint8_t* s = &lvl.px[i * 4];
            o[0] = s[2]; o[1] = s[1]; o[2] = s[0]; o[3] = s[3];
        }
        newData[m->data] = std::move(blk);
        newFmt[m->slot] = 0x800;
        if (grow) newDim[m->slot] = ((uint32_t)mhh << 16) | (uint32_t)mw;
    }
    auto out = Rebuild(d, newData, newFmt, newDim);
    HANDLE h = CreateFileW(dst.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr; WriteFile(h, out.data(), (DWORD)out.size(), &wr, nullptr); CloseHandle(h);
    Log("fontgen: %ls -> %d icons, atlas %dx%d, cell %d", src.c_str(), placed, W, H, cell);
    return wr == out.size();
}

// The menu button hints draw GUI_Button_{A,B,X,Y}_32 as textured quads on the
// stretched 640x480 canvas. Redraw them squeezed into the middle of the texture
// (transparent sides), every mip level, so they come out round on screen.
static bool SqueezeGuiButtons(std::vector<uint8_t>& d)
{
    int done = 0;
    for (const char* name : { "GUI_Button_A_32", "GUI_Button_B_32", "GUI_Button_X_32", "GUI_Button_Y_32" }) {
        size_t nl = strlen(name);
        for (size_t i = 0; i + nl + 48 < d.size(); ++i) {
            if (memcmp(&d[i], name, nl) != 0 || d[i + nl] != ' ' && d[i + nl] != 0) continue;
            bool ok = false;
            for (size_t r = i + nl; r < i + nl + 24 && !ok; ++r) {
                const uint32_t* f = (const uint32_t*)&d[r];
                if (f[0] != 0x300 || f[5] != 0x800 || f[3] == 0 || f[3] > 256 || f[4] == 0 || f[4] > 256) continue;
                uint32_t tbl = f[7];
                if (tbl + 4 > d.size()) continue;
                uint32_t m0 = *(const uint32_t*)&d[tbl];
                if (m0 + 44 > d.size()) continue;
                const uint32_t* mh = (const uint32_t*)&d[m0];
                int W = (int)mh[3], H = (int)mh[4];
                if (mh[5] != 0x800 || m0 + 44 + (size_t)((W + 3) / 4) * ((H + 3) / 4) * 16 > d.size()) continue;
                Img src; DecodeDXT5(&d[m0 + 44], W, H, src);
                int sw = std::max(1, (int)lroundf(W * g_uiSqueeze));
                Img narrow = Resample(src, sw, H);
                Img sq; sq.w = W; sq.h = H; sq.px.assign(W * H * 4, 0);
                int ox = (W - sw) / 2;
                for (int y = 0; y < H; ++y) memcpy(&sq.px[(y * W + ox) * 4], &narrow.px[y * sw * 4], sw * 4);
                for (int lvl = 0; lvl < 16; ++lvl) {
                    if (tbl + lvl * 4 + 4 > d.size()) break;
                    uint32_t m = *(const uint32_t*)&d[tbl + lvl * 4];
                    if (m + 44 > d.size()) break;
                    const uint32_t* h = (const uint32_t*)&d[m];
                    int w = (int)h[3], hh = (int)h[4];
                    if (h[0] != 0x300 || h[5] != 0x800 || w <= 0 || hh <= 0 || w > W || hh > H) break;
                    size_t n = (size_t)((w + 3) / 4) * ((hh + 3) / 4) * 16;
                    if (m + 44 + n > d.size()) break;
                    Img l = (w == W && hh == H) ? sq : Resample(sq, w, hh);
                    EncodeDXT5(l, &d[m + 44]);
                }
                ok = true;
            }
            if (ok) { ++done; break; }
        }
    }
    Log("guigen: %d button textures squeezed by %.3f", done, g_uiSqueeze);
    return done > 0;
}

static void GuiGen_Run(std::vector<uint8_t>& gui, const std::wstring& cache)
{
    if (g_uiSqueeze >= 1.0f) return;
    std::wstring guiPath = std::wstring(g_gameRoot) + L"\\Content\\GUI.xtc";
    if (gui.empty() && !ReadFileAll(guiPath, gui)) return;
    std::wstring dst = cache + L"\\GUI.xtc", stampPath = dst + L".stamp";
    char stamp[96]; sprintf_s(stamp, "v1 %zu %.4f", gui.size(), g_uiSqueeze);
    std::vector<uint8_t> old;
    bool fresh = ReadFileAll(stampPath, old) && old.size() == strlen(stamp) && !memcmp(old.data(), stamp, old.size()) &&
                 GetFileAttributesW(dst.c_str()) != INVALID_FILE_ATTRIBUTES;
    if (!fresh) {
        if (!SqueezeGuiButtons(gui) || !WriteFileAll(dst, gui)) return;
        WriteFileAll(stampPath, std::vector<uint8_t>(stamp, stamp + strlen(stamp)));
    }
    Files_AddRedirect("content\\gui.xtc", dst);
}

void FontGen_Run()
{
    std::map<std::string, Img> icons;
    std::vector<uint8_t> gui;
    std::wstring guiPath = std::wstring(g_gameRoot) + L"\\Content\\GUI.xtc";
    if (g_cfg.xboxPrompts && ReadFileAll(guiPath, gui)) {
        const char* names[][2] = { { "A", "GUI_Button_A_32" }, { "B", "GUI_Button_B_32" }, { "X", "GUI_Button_X_32" }, { "Y", "GUI_Button_Y_32" } };
        for (auto& n : names) { Img im; if (LoadXtcIcon(gui, n[1], im)) icons[n[0]] = CropAlpha(im); else Log("fontgen: %s not found", n[1]); }
    }
    if (g_cfg.xboxPrompts)
        for (auto& a : g_glyphArt) {
            Img im; im.w = a.w; im.h = a.h; im.px.assign(a.rgba, a.rgba + a.w * a.h * 4);
            icons[a.name] = im;
        }
    std::wstring cache = std::wstring(g_dllDir) + L"\\RiddickFix_cache";
    CreateDirectoryW(cache.c_str(), nullptr);
    for (const wchar_t* f : { L"Text.xfc", L"Headings.xfc", L"Palatino.xfc", L"Clairvaux.xfc", L"Numbers.xfc", L"MiniFont2.xfc" }) {
        bool withIcons = g_cfg.xboxPrompts && (!wcscmp(f, L"Text.xfc") || !wcscmp(f, L"Headings.xfc"));
        if (!withIcons && g_uiSqueeze == 1.0f) continue;
        std::wstring src = std::wstring(g_gameRoot) + L"\\Content\\Fonts\\" + f;
        std::wstring dst = cache + L"\\" + f;
        bool ok = withIcons ? BuildFont(src, dst, icons) : SqueezeFont(src, dst);
        if (ok) {
            char rel[64]; sprintf_s(rel, "content\\fonts\\%ls", f);
            for (char* c = rel; *c; ++c) *c = (char)tolower((unsigned char)*c);
            Files_AddRedirect(rel, dst);
            if (withIcons) g_glyphFonts = true;
        } else Log("fontgen: %ls failed", f);
    }
    GuiGen_Run(gui, cache);
}
