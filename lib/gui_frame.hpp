
//
//  Build:  sudo apt install libwayland-dev libxkbcommon-dev libdecor-0-dev libcairo2-dev fonts-dejavu-core
//          g++ -std=c++17 *.cpp -o calc $(pkg-config --cflags --libs wayland-client wayland-cursor xkbcommon libdecor-0 cairo)
// ============================================================================
#include "../lib/algebra.hpp"
#include "../lib/Alpha_solver_eq.hpp"
#include "../lib/Calculator_calculus.hpp"
#include "../lib/function_solver.hpp"
#include "../lib/math_graph.hpp"
#include "../lib/math_logic.hpp"
#include "../lib/matrix.hpp"
#include "../lib/menu.hpp"
#include "../lib/table_calculate.hpp"

#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon.h>
#include <libdecor.h>
#include <cairo.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <poll.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#define RGB32(r,g,b) ((uint32_t)(((r)<<16)|((g)<<8)|(b)))

// ---------- สี ----------
static const uint32_t COL_BG      = RGB32(30, 33, 40);
static const uint32_t COL_PANEL   = RGB32(20, 22, 27);
static const uint32_t COL_BORDER  = RGB32(70, 76, 90);
static const uint32_t COL_TEXT    = RGB32(222, 226, 235);
static const uint32_t COL_DIM     = RGB32(140, 146, 160);
static const uint32_t COL_GREEN   = RGB32(120, 230, 150);
static const uint32_t COL_RED     = RGB32(255, 120, 120);
static const uint32_t COL_BTN     = RGB32(55, 110, 200);
static const uint32_t COL_NUM     = RGB32(78, 84, 96);
static const uint32_t COL_OP      = RGB32(214, 130, 40);
static const uint32_t COL_OK      = RGB32(46, 160, 90);
static const uint32_t COL_CLEAR   = RGB32(190, 60, 60);
static const uint32_t COL_CHIP    = RGB32(60, 66, 80);

// ---------- Wayland / framebuffer state ----------
// ฟอนต์: วาดด้วย cairo (ฟอนต์ monospace ของระบบผ่าน fontconfig) แต่คงขนาดช่องตัวอักษรเท่าฟอนต์ X11 เดิม
//   S = 9x15 (ปุ่ม/ช่องกรอก)   M = 7x13 (ผลลัพธ์/กราฟ)   L = 10x20 (หัวข้อ)
// เปลี่ยนฟอนต์ได้ด้วยตัวแปรสภาพแวดล้อม MATHS_FONT เช่น  MATHS_FONT="JetBrains Mono" ./calc
struct GuiFont { int cw, h, ascent, descent; double size; };
static GuiFont g_fontSd = {9, 15, 12, 3, 0.0};
static GuiFont g_fontMd = {7, 13, 11, 2, 0.0};
static GuiFont g_fontLd = {10, 20, 16, 4, 0.0};
static GuiFont* g_fontS = &g_fontSd;    // ฟอนต์ปุ่ม/ช่องกรอก
static GuiFont* g_fontM = &g_fontMd;    // ฟอนต์ monospace สำหรับผลลัพธ์/กราฟ
static GuiFont* g_fontL = &g_fontLd;    // ฟอนต์ใหญ่ (หัวข้อ)
static std::string g_fontFamily = "monospace";

static wl_display*             g_wl = nullptr;
static wl_registry*            g_registry = nullptr;
static wl_compositor*          g_compositor = nullptr;
static wl_shm*                 g_shm = nullptr;
static wl_seat*                g_seat = nullptr;
static wl_data_device_manager* g_ddm = nullptr;
static wl_data_device*         g_dataDev = nullptr;
static wl_surface*             g_surf = nullptr;
static libdecor*               g_dec = nullptr;       // ขอบหน้าต่าง/ไตเติลบาร์ (ใช้ได้ทั้ง GNOME, KDE, sway ...)
static libdecor_frame*         g_frame = nullptr;
static bool g_running = true, g_configured = false;

static uint32_t* g_px = nullptr;          // พิกเซลของบัฟเฟอร์ที่กำลังวาดอยู่ (XRGB8888)
static int g_w = 0, g_h = 0;

// wl_shm: 3 บัฟเฟอร์ใน pool เดียว  (วาดลงบัฟเฟอร์ที่ compositor ปล่อยแล้วเท่านั้น)
struct ShmBuf { wl_buffer* buf = nullptr; uint32_t* data = nullptr; bool busy = false; };
static const int NBUF = 3;
static ShmBuf g_bufs[NBUF];
static void* g_poolMem = nullptr;
static size_t g_poolSize = 0;

static int CreateShmFile(size_t size) {
    int fd = -1;
#ifdef MFD_CLOEXEC
    fd = memfd_create("maths-solver-shm", MFD_CLOEXEC);
#endif
    if (fd < 0) {
        const char* dir = getenv("XDG_RUNTIME_DIR");
        if (!dir) return -1;
        std::string path = std::string(dir) + "/maths-solver-XXXXXX";
        std::vector<char> tmpl(path.begin(), path.end());
        tmpl.push_back('\0');
        fd = mkostemp(tmpl.data(), O_CLOEXEC);
        if (fd >= 0) unlink(tmpl.data());
    }
    if (fd < 0) return -1;
    if (ftruncate(fd, (off_t)size) < 0) { close(fd); return -1; }
    return fd;
}
static void BufRelease(void* data, wl_buffer*) { ((ShmBuf*)data)->busy = false; }
static const wl_buffer_listener g_bufListener = { BufRelease };

static void DestroyBuffers() {
    for (auto& b : g_bufs) { if (b.buf) wl_buffer_destroy(b.buf); b = ShmBuf(); }
    if (g_poolMem) { munmap(g_poolMem, g_poolSize); g_poolMem = nullptr; g_poolSize = 0; }
    g_px = nullptr;
}
static void CreateBuffer(int w, int h) {
    if (w <= 0 || h <= 0 || !g_shm) return;
    DestroyBuffers();
    size_t stride = (size_t)w * 4, one = stride * (size_t)h, total = one * NBUF;
    int fd = CreateShmFile(total);
    if (fd < 0) { fprintf(stderr, "Cannot create shared-memory buffer\n"); g_running = false; return; }
    void* mem = mmap(nullptr, total, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mem == MAP_FAILED) { close(fd); fprintf(stderr, "mmap failed\n"); g_running = false; return; }
    wl_shm_pool* pool = wl_shm_create_pool(g_shm, fd, (int32_t)total);
    for (int i = 0; i < NBUF; ++i) {
        g_bufs[i].data = (uint32_t*)((char*)mem + (size_t)i * one);
        g_bufs[i].buf = wl_shm_pool_create_buffer(pool, (int32_t)((size_t)i * one), w, h, (int32_t)stride,
                                                  WL_SHM_FORMAT_XRGB8888);
        wl_buffer_add_listener(g_bufs[i].buf, &g_bufListener, &g_bufs[i]);
    }
    wl_shm_pool_destroy(pool);
    close(fd);
    g_poolMem = mem; g_poolSize = total;
    g_w = w; g_h = h;
    g_px = g_bufs[0].data;
}
// หาบัฟเฟอร์ที่ว่าง (ถ้า compositor ยังไม่ปล่อย จะรอรับ event สักครู่)
static int AcquireBuffer() {
    for (int tries = 0; tries < 100 && g_running; ++tries) {
        for (int i = 0; i < NBUF; ++i) if (g_bufs[i].buf && !g_bufs[i].busy) return i;
        if (libdecor_dispatch(g_dec, 10) < 0) break;
    }
    return -1;
}

// ---------- Drawing primitives ----------
struct TextCmd { int x, y; std::string s; uint32_t c; GuiFont* f; };
static std::vector<TextCmd> g_text;

static void FillRectPx(int x, int y, int w, int h, uint32_t c) {
    int x0 = std::max(x, 0), y0 = std::max(y, 0);
    int x1 = std::min(x + w, g_w), y1 = std::min(y + h, g_h);
    for (int j = y0; j < y1; ++j)
        for (int i = x0; i < x1; ++i) g_px[j * g_w + i] = c;
}
static void Border(int x, int y, int w, int h, uint32_t c) {
    FillRectPx(x, y, w, 1, c); FillRectPx(x, y + h - 1, w, 1, c);
    FillRectPx(x, y, 1, h, c); FillRectPx(x + w - 1, y, 1, h, c);
}
// ฟอนต์ monospace: ความกว้าง = จำนวนตัวอักษร (นับเป็น UTF-8 code point) x ความกว้างช่อง
static int Utf8Len(const std::string& s) {
    int n = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++n;
    return n;
}
static int TextW(const GuiFont* f, const std::string& s) { return Utf8Len(s) * (f ? f->cw : 8); }
static int FontH(const GuiFont* f) { return f ? f->ascent + f->descent : 14; }
static int CharW(const GuiFont* f) { return f ? std::max(1, f->cw) : 8; }

static void DrawText_(int x, int y, const std::string& s, uint32_t c, GuiFont* f) {
    if (!s.empty()) g_text.push_back({x, y, s, c, f});    // y = ขอบบนของข้อความ
}
static void DrawTextCenter(int cx, int cy, const std::string& s, uint32_t c, GuiFont* f) {
    DrawText_(cx - TextW(f, s) / 2, cy - FontH(f) / 2, s, c, f);
}
static void DrawTextRight(int xr, int y, const std::string& s, uint32_t c, GuiFont* f) {
    DrawText_(xr - TextW(f, s), y, s, c, f);
}
// ตัดให้พอดีความกว้าง maxW (เก็บ "ท้าย" ของข้อความ)
static std::string FitTail(GuiFont* f, std::string s, int maxW) {
    int maxc = std::max(0, maxW / CharW(f)), n = Utf8Len(s);
    size_t i = 0;
    while (n > maxc && i < s.size()) {
        ++i; while (i < s.size() && ((unsigned char)s[i] & 0xC0) == 0x80) ++i;
        --n;
    }
    return s.substr(i);
}
// ตัดให้พอดีความกว้าง maxW (เก็บ "หัว" ของข้อความ)
static std::string FitHead(GuiFont* f, std::string s, int maxW) {
    int maxc = std::max(0, maxW / CharW(f));
    if (Utf8Len(s) <= maxc) return s;
    size_t i = 0; int n = 0;
    while (i < s.size() && n < maxc) {
        ++i; while (i < s.size() && ((unsigned char)s[i] & 0xC0) == 0x80) ++i;
        ++n;
    }
    return s.substr(0, i);
}

// หาขนาดฟอนต์ cairo ที่ทำให้ความกว้างตัวอักษร = ความกว้างช่องของแต่ละขนาด
static void InitFonts() {
    const char* fam = getenv("MATHS_FONT");
    if (fam && *fam) g_fontFamily = fam;
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, 1, 1);
    cairo_t* cr = cairo_create(s);
    cairo_select_font_face(cr, g_fontFamily.c_str(), CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 100.0);
    cairo_text_extents_t te;
    cairo_text_extents(cr, "M", &te);
    double ratio = te.x_advance / 100.0;
    if (ratio < 0.2 || ratio > 1.5) ratio = 0.6;
    for (GuiFont* f : {&g_fontSd, &g_fontMd, &g_fontLd}) f->size = f->cw / ratio;
    cairo_destroy(cr);
    cairo_surface_destroy(s);
}
// วาดข้อความทั้งหมดของเฟรมทับลงบนบัฟเฟอร์พิกเซล
static void FlushText() {
    if (g_text.empty() || !g_px) return;
    cairo_surface_t* sf = cairo_image_surface_create_for_data((unsigned char*)g_px, CAIRO_FORMAT_RGB24,
                                                              g_w, g_h, g_w * 4);
    cairo_t* cr = cairo_create(sf);
    cairo_font_options_t* fo = cairo_font_options_create();
    cairo_font_options_set_antialias(fo, CAIRO_ANTIALIAS_GRAY);
    cairo_font_options_set_hint_style(fo, CAIRO_HINT_STYLE_SLIGHT);
    cairo_font_options_set_hint_metrics(fo, CAIRO_HINT_METRICS_ON);
    cairo_set_font_options(cr, fo);
    cairo_font_options_destroy(fo);
    cairo_select_font_face(cr, g_fontFamily.c_str(), CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    const GuiFont* cur = nullptr;
    for (const auto& t : g_text) {
        const GuiFont* f = t.f ? t.f : g_fontS;
        if (f != cur) { cur = f; cairo_set_font_size(cr, f->size); }
        cairo_set_source_rgb(cr, ((t.c >> 16) & 255) / 255.0, ((t.c >> 8) & 255) / 255.0, (t.c & 255) / 255.0);
        cairo_move_to(cr, t.x, t.y + f->ascent);
        cairo_show_text(cr, t.s.c_str());
    }
    cairo_destroy(cr);
    cairo_surface_flush(sf);
    cairo_surface_destroy(sf);
}

// ---------- Immediate-mode UI ----------
struct UIState {
    int mx = 0, my = 0;
    bool down = false, pressed = false, released = false;
    int active = 0;
} ui;
static int g_nextId = 0;
static int NextId() { return ++g_nextId; }

static bool Hit(int x, int y, int w, int h) {
    return ui.mx >= x && ui.mx < x + w && ui.my >= y && ui.my < y + h;
}
static uint32_t Shade(uint32_t c, int d) {
    int r = std::clamp((int)((c >> 16) & 255) + d, 0, 255);
    int g = std::clamp((int)((c >> 8) & 255) + d, 0, 255);
    int b = std::clamp((int)(c & 255) + d, 0, 255);
    return RGB32(r, g, b);
}

// ปุ่มทั่วไป (คืน true เมื่อคลิก)
static bool DrawButton(int x, int y, int w, int h, const std::string& label, uint32_t base,
                       GuiFont* font = nullptr, bool selected = false) {
    int id = NextId();
    bool over = Hit(x, y, w, h);
    if (over && ui.pressed) ui.active = id;
    bool clicked = ui.released && ui.active == id && over;
    uint32_t bg = (ui.active == id && ui.down && over) ? Shade(base, -25)
                : over ? Shade(base, 20) : base;
    FillRectPx(x, y, w, h, bg);
    Border(x, y, w, h, selected ? RGB32(255, 255, 255) : Shade(base, -50));
    if (selected) Border(x + 1, y + 1, w - 2, h - 2, RGB32(255, 255, 255));
    GuiFont* f = font ? font : g_fontS;
    if (TextW(f, label) > w - 6 && f == g_fontS) f = g_fontM;
    DrawTextCenter(x + w / 2, y + h / 2, FitHead(f, label, w - 4), RGB32(255, 255, 255), f);
    return clicked;
}

// ---------- ช่องกรอกข้อความ + ระบบโฟกัส ----------
static std::string* g_focusStr = nullptr;
static std::vector<std::string*> g_tabNow, g_tabPrev;
static void ResetFocus() { g_focusStr = nullptr; }

static bool TextBox(int x, int y, int w, int h, std::string& s, const std::string& hint,
                    GuiFont* f = nullptr) {
    if (!f) f = g_fontS;
    bool over = Hit(x, y, w, h);
    bool clicked = over && ui.pressed;
    if (clicked) g_focusStr = &s;
    bool foc = (g_focusStr == &s);
    g_tabNow.push_back(&s);
    FillRectPx(x, y, w, h, foc ? RGB32(255, 255, 255) : RGB32(232, 235, 240));
    Border(x, y, w, h, foc ? RGB32(40, 100, 220) : RGB32(150, 154, 164));
    if (foc) Border(x + 1, y + 1, w - 2, h - 2, RGB32(40, 100, 220));
    int ty = y + (h - FontH(f)) / 2;
    if (s.empty() && !foc) {
        DrawText_(x + 6, ty, FitHead(f, hint, w - 12), RGB32(150, 154, 166), f);
    } else {
        std::string shown = FitTail(f, s, w - 16);
        DrawText_(x + 6, ty, shown, RGB32(20, 22, 28), f);
        if (foc) FillRectPx(x + 7 + TextW(f, shown), y + 4, 2, h - 8, RGB32(40, 100, 220));
    }
    return clicked;
}

// ---------- เหตุการณ์ที่รอประมวลผลใน Frame ----------
static int  g_wheel = 0, g_wheelKey = 0, g_wheelH = 0, g_hKey = 0;
static bool g_enter = false, g_escape = false;
static char g_pendingChar = 0;
static std::vector<std::function<void()>> g_deferred;
static void Defer(std::function<void()> fn) { g_deferred.push_back(std::move(fn)); }

// ============================================================
//                    ตัวช่วยทั่วไป
// ============================================================
static Calculator g_calc;
static FunctionSolver g_fs(g_calc);
static MathLogic g_logic(&g_calc);
static double g_ans = 0.0;

static void SetAns(double v) { g_ans = v; g_calc.solver().setAns(v); }

static std::string Fmt(double v) {
    if (std::isnan(v)) return "NaN";
    if (std::isinf(v)) return v > 0 ? "inf" : "-inf";
    char buf[64];
    snprintf(buf, sizeof buf, "%.10g", v);
    return buf;
}
static std::string FmtComplex(const std::complex<double>& z) {
    if (std::fabs(z.imag()) < 1e-9) return Fmt(z.real());
    return Fmt(z.real()) + (z.imag() >= 0 ? " + " : " - ") + Fmt(std::fabs(z.imag())) + "i";
}
static std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && isspace((unsigned char)s[a])) ++a;
    while (b > a && isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}
// เติม * ให้การคูณโดยนัย เช่น 2x -> 2*x, 3(x+1) -> 3*(x+1), (x+1)(x-1) -> (x+1)*(x-1)
static std::string ImplicitMul(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        o += c;
        if (i + 1 >= s.size()) break;
        char n = s[i + 1];
        bool cDigit = isdigit((unsigned char)c) || c == '.';
        bool nAlpha = isalpha((unsigned char)n);
        bool ins = false;
        if (cDigit) {
            size_t j = i;                                   // ถอยไปต้นเลข: ถ้าตามหลังตัวอักษร = ชื่อ เช่น log2( ไม่แทรก *
            while (j > 0 && (isdigit((unsigned char)s[j - 1]) || s[j - 1] == '.')) --j;
            bool inIdent = j > 0 && (isalpha((unsigned char)s[j - 1]) || s[j - 1] == '_');
            if (!inIdent) {
                if (nAlpha) {
                    bool sci = (n == 'e' || n == 'E') && i + 2 < s.size() &&
                               (isdigit((unsigned char)s[i + 2]) || s[i + 2] == '+' || s[i + 2] == '-');
                    ins = !sci;
                } else if (n == '(') ins = true;
            }
        } else if (c == ')' && (nAlpha || n == '(' || isdigit((unsigned char)n))) ins = true;
        if (ins) o += '*';
    }
    return o;
}
static std::string ReplaceAns(const std::string& s) {      // แทนคำว่า "ans" (ทั้งคำ) ด้วยค่าล่าสุด
    char num[64];
    snprintf(num, sizeof num, "(%.15g)", g_ans);
    std::string out;
    for (size_t i = 0; i < s.size();) {
        bool match = s.compare(i, 3, "ans") == 0 &&
                     (i == 0 || !(isalnum((unsigned char)s[i - 1]) || s[i - 1] == '_')) &&
                     (i + 3 >= s.size() || !(isalnum((unsigned char)s[i + 3]) || s[i + 3] == '_'));
        if (match) { out += num; i += 3; }
        else out += s[i++];
    }
    return ImplicitMul(out);
}
// แบ่งด้วยตัวคั่นชุดหนึ่ง โดยไม่แบ่งในวงเล็บ/วงเล็บเหลี่ยม (ข้ามส่วนว่าง)
static std::vector<std::string> SplitTop(const std::string& s, const std::string& seps) {
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    for (char c : s) {
        if (c == '(' || c == '[') ++depth; else if (c == ')' || c == ']') depth = std::max(0, depth - 1);
        if (depth == 0 && seps.find(c) != std::string::npos) {
            if (!Trim(cur).empty()) out.push_back(Trim(cur));
            cur.clear();
        } else cur += c;
    }
    if (!Trim(cur).empty()) out.push_back(Trim(cur));
    return out;
}
// ---------- ตัวแปร a-z, A-Z และพารามิเตอร์ (ใช้ความสามารถของ AlphaSolverEq) ----------
// ช่อง Vars (แถบบนสุด) เก็บค่าตัวแปร/พารามิเตอร์ เช่น  a=2, b=3  -> แทนค่าให้ทุกสูตรในทุกหมวดอัตโนมัติ
static std::string g_paramStr;
static std::string StripWord(const std::string& s, const std::string& w) {     // แทนคำ (ทั้งคำ) ด้วย 0 เช่น inf
    std::string out;
    for (size_t i = 0; i < s.size();) {
        if (isalpha((unsigned char)s[i])) {
            size_t j = i;
            while (j < s.size() && isalpha((unsigned char)s[j])) ++j;
            std::string word = s.substr(i, j - i);
            out += (word == w) ? "0" : word;
            i = j;
        } else out += s[i++];
    }
    return out;
}
static std::vector<char> FindVars(const std::string& e) { return AlphaSolverEq::findVariables(StripWord(e, "inf")); }
static const std::map<char, double>& ParamMap() {
    static std::string lastStr = "\x01";
    static std::map<char, double> lastMap;
    static std::string lastErr;
    if (lastStr != g_paramStr) {
        lastStr = g_paramStr; lastMap.clear(); lastErr.clear();
        try {
            for (auto& t : SplitTop(g_paramStr, ",;")) {
                size_t eq = t.find('=');
                std::string k = eq == std::string::npos ? "" : Trim(t.substr(0, eq));
                if (k.size() != 1 || !isalpha((unsigned char)k[0]))
                    throw std::runtime_error("use the form  a=2, b=3  (one letter a-z / A-Z = value)");
                if (k[0] == 'e') throw std::runtime_error("'e' is the constant 2.718..., choose another letter");
                lastMap[k[0]] = g_calc.evaluate(ReplaceAns(t.substr(eq + 1)));
            }
        } catch (const std::exception& ex) { lastErr = ex.what(); lastMap.clear(); }
    }
    if (!lastErr.empty()) throw std::runtime_error("Vars box: " + lastErr);
    return lastMap;
}
static void SetParam(char k, double v) {
    std::map<char, double> mp = ParamMap();
    mp[k] = v;
    std::string s;
    for (auto& kv : mp) {
        char buf[64]; snprintf(buf, sizeof buf, "%.15g", kv.second);
        s += (s.empty() ? "" : ", ") + std::string(1, kv.first) + "=" + buf;
    }
    g_paramStr = s;
}
// แทนค่าพารามิเตอร์ลงในสูตร (ยกเว้นตัวอักษรใน keep)
static std::string ApplyParamsKeep(const std::string& e, const std::string& keep) {
    std::map<char, double> mp = ParamMap();
    for (char c : keep) mp.erase(c);
    if (mp.empty()) return e;
    return AlphaSolverEq::substituteVariables(e, mp);
}
static std::string ApplyParams(const std::string& e) { return ApplyParamsKeep(e, ""); }
struct Prepared { std::string sub; std::string xexpr; char var = 'x'; };
// เตรียมสูตรตัวแปรเดียว: แทนพารามิเตอร์, หาตัวแปร (x ก่อน, ไม่งั้นตัวที่เหลือตัวเดียว), แปลงเป็น x ให้ Calculator
static Prepared PrepVar(const std::string& raw, char varField = '\0', bool allowFree = false) {
    std::string e = ReplaceAns(raw);
    const auto& P = ParamMap();
    std::vector<char> V = FindVars(e), unb;
    for (char c : V) if (!P.count(c)) unb.push_back(c);
    char var = varField;
    if (!var) {
        if (std::find(unb.begin(), unb.end(), 'x') != unb.end()) var = 'x';
        else if (unb.size() == 1) var = unb[0];
        else if (unb.empty()) var = 'x';
        else {
            std::string list;
            for (char c : unb) { if (!list.empty()) list += ", "; list += c; }
            throw std::runtime_error("Ambiguous variable (found: " + list + "). Fill in the Variable box, or give values in the Vars box.");
        }
    }
    if (!isalpha((unsigned char)var)) throw std::invalid_argument("Variable name must be a letter (a-z or A-Z).");
    Prepared p;
    p.var = var;
    p.sub = ApplyParamsKeep(e, std::string(1, var));
    if (allowFree) { p.xexpr = p.sub; return p; }          // สัญลักษณ์: ตัวอักษรอื่นเป็นค่าคงที่ได้
    for (char c : FindVars(p.sub))
        if (c != var) throw std::runtime_error(std::string("Unknown variable '") + c + "'. Give it a value in the Vars box (top right), e.g.  " + c + "=2");
    p.xexpr = AlphaSolverEq::normalizeVariable(p.sub, var);
    return p;
}
static double EvalNum(const std::string& s) {
    std::string e = ApplyParams(ReplaceAns(s));
    auto vs = FindVars(e);
    if (!vs.empty())
        throw std::runtime_error(std::string("Unknown variable '") + vs[0] + "'. Give it a value in the Vars box (top right), e.g.  " + vs[0] + "=2");
    return g_calc.evaluate(e);
}
// ค่าในเซลล์: ถ้าเป็นตัวเลขล้วนอ่านตรง ๆ (เร็ว สำหรับตารางใหญ่) ไม่งั้นประเมินเป็นสูตร
static double CellValue(const std::string& raw) {
    const char* s = raw.c_str();
    while (isspace((unsigned char)*s)) ++s;
    if (!*s) return 0.0;
    char* end = nullptr;
    double v = strtod(s, &end);
    while (*end && isspace((unsigned char)*end)) ++end;
    if (!*end) return v;
    return EvalNum(raw);
}
static std::vector<double> ParseList(const std::string& s, const char* what = "numbers") {
    std::vector<double> v;
    for (auto& t : SplitTop(s, ", ;\t")) v.push_back(EvalNum(t));
    if (v.empty()) throw std::runtime_error(std::string("Please enter ") + what + " separated by commas");
    return v;
}
// สูตรตัวแปรเดียว (ตัวแปร a-z/A-Z ใดก็ได้ + พารามิเตอร์ใน Vars) -> สูตรในตัวแปร x สำหรับ Calculator
static std::string Norm(const std::string& expr) { return PrepVar(expr).xexpr; }
// ช่วง เช่น "[0,inf)", "(-1,5]", "R"
static Interval ParseInterval(const std::string& s) {
    std::string t = Trim(s);
    if (t == "R" || t == "all" || t == "(-inf,inf)") return Interval(-INFINITY, INFINITY, false, false);
    if (t.size() < 5 || (t[0] != '[' && t[0] != '(') || (t.back() != ']' && t.back() != ')'))
        throw std::runtime_error("Interval format: [a,b]  (a,b)  [a,inf)  or R   -- got: " + t);
    auto parts = SplitTop(t.substr(1, t.size() - 2), ",");
    if (parts.size() != 2) throw std::runtime_error("Interval needs two ends: [a,b]");
    return Interval(EvalNum(parts[0]), EvalNum(parts[1]), t[0] == '[', t.back() == ']');
}
static std::map<char, double> ParseGuess(const std::string& s) {      // "x=1, y=2"
    std::map<char, double> m;
    for (auto& t : SplitTop(s, ",;")) {
        size_t eq = t.find('=');
        if (eq == std::string::npos || Trim(t.substr(0, eq)).size() != 1)
            throw std::runtime_error("Use the form  x=1, y=2  (one letter = value)");
        m[Trim(t.substr(0, eq))[0]] = EvalNum(t.substr(eq + 1));
    }
    return m;
}

struct CoutCapture {
    std::ostringstream ss;
    std::streambuf* old;
    CoutCapture() : old(std::cout.rdbuf(ss.rdbuf())) {}
    ~CoutCapture() { std::cout.rdbuf(old); }
};
static std::string CleanAscii(const std::string& s) {      // ตัดรหัสสี ANSI และไบต์ที่ฟอนต์ไม่รองรับ
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c == 27) {
            if (i + 1 < s.size() && s[i + 1] == '[') {
                i += 2;
                while (i < s.size() && !isalpha((unsigned char)s[i])) ++i;
            }
        } else if (c == '\t') out += ' ';
        else if (c == '\n' || (c >= 32 && c < 127)) out += (char)c;
    }
    return out;
}
static std::string FormatTable(const DataTable& t, int maxRows = 3000, int maxCols = 200) {
    std::string out;
    char buf[96];
    int cc = std::min(t.getCols(), maxCols), rr = std::min(t.getRows(), maxRows);
    for (int c = 0; c < cc; ++c) { snprintf(buf, sizeof buf, "%-14s", t.getHeader(c).c_str()); out += buf; }
    out += "\n";
    for (int r = 0; r < rr; ++r) {
        for (int c = 0; c < cc; ++c) { snprintf(buf, sizeof buf, "%-14.8g", t.getCell(r, c)); out += buf; }
        out += "\n";
    }
    if (rr < t.getRows() || cc < t.getCols())
        out += "... showing " + std::to_string(rr) + " of " + std::to_string(t.getRows()) + " rows, " +
               std::to_string(cc) + " of " + std::to_string(t.getCols()) + " columns\n";
    return out;
}
static std::string FormatMatrix(const Matrix& m) {
    std::vector<std::vector<std::string>> cells(m.getRows());
    size_t w = 1;
    for (int i = 0; i < m.getRows(); ++i)
        for (int j = 0; j < m.getCols(); ++j) {
            cells[i].push_back(Fmt(m.getElement(i, j)));
            w = std::max(w, cells[i].back().size());
        }
    std::string out;
    for (int i = 0; i < m.getRows(); ++i) {
        out += "[ ";
        for (int j = 0; j < m.getCols(); ++j) out += std::string(w - cells[i][j].size(), ' ') + cells[i][j] + "  ";
        out += "]\n";
    }
    return out;
}
static std::vector<std::string> BuildLines(const std::string& text, int perLine, bool wrap) {
    std::vector<std::string> lines;
    perLine = std::max(1, perLine);
    size_t start = 0;
    while (start <= text.size()) {
        size_t nl = text.find('\n', start);
        std::string line = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        if (wrap) {
            if (line.empty()) lines.push_back("");
            for (size_t i = 0; i < line.size(); i += perLine) lines.push_back(line.substr(i, perLine));
        } else lines.push_back(line.substr(0, perLine));
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    while (!lines.empty() && lines.back().empty()) lines.pop_back();
    return lines;
}

// ---------- แผงแสดงผลลัพธ์ (เลื่อนได้) ----------
static int g_resCols = 80, g_resRows = 24;      // ขนาดล่าสุดของแผงผลลัพธ์ (ใช้กำหนดความละเอียดกราฟอัตโนมัติ)
static void ResultPanel(int x, int y, int w, int h, const std::string& text, const std::string& err,
                        int& scroll, bool wrap = true, int* hscroll = nullptr) {
    FillRectPx(x, y, w, h, COL_PANEL);
    Border(x, y, w, h, COL_BORDER);
    int fh = FontH(g_fontM) + 2, cw = CharW(g_fontM);
    int cols = std::max(10, (w - 16 - 22) / cw), rows = std::max(2, (h - 8) / fh);
    g_resCols = cols; g_resRows = rows;
    bool isErr = !err.empty();
    std::string show = isErr ? "Error: " + err : text;
    if (show.empty()) {
        DrawText_(x + 10, y + 8, "Result appears here.", COL_DIM, g_fontM);
        return;
    }
    bool hs = hscroll && !wrap && !isErr;
    auto lines = BuildLines(show, hs ? 1000000 : cols, isErr ? true : wrap);
    int maxScroll = std::max(0, (int)lines.size() - rows);
    int maxLen = 0;
    if (hs) for (auto& l : lines) maxLen = std::max(maxLen, (int)l.size());
    int hmax = hs ? std::max(0, maxLen - cols) : 0;
    if (Hit(x, y, w, h) && g_wheel) { scroll += g_wheel * 3; g_wheel = 0; }
    if (Hit(x, y, w, h) && g_wheelH && hs) { *hscroll += g_wheelH * 8; g_wheelH = 0; }
    if (g_wheelKey) { scroll += g_wheelKey; g_wheelKey = 0; }
    if (g_hKey && hs) { *hscroll += g_hKey * 8; g_hKey = 0; }
    scroll = std::clamp(scroll, 0, maxScroll);
    int hoff = 0;
    if (hs) { *hscroll = std::clamp(*hscroll, 0, hmax); hoff = *hscroll; }
    for (int i = 0; i < rows && i + scroll < (int)lines.size(); ++i) {
        const std::string& ln = lines[i + scroll];
        DrawText_(x + 8, y + 4 + i * fh, hs ? (hoff < (int)ln.size() ? ln.substr(hoff, cols) : std::string()) : ln,
                  isErr ? COL_RED : COL_GREEN, g_fontM);
    }
    if (maxScroll > 0) {
        if (DrawButton(x + w - 20, y + 2, 18, 18, "^", COL_CHIP, g_fontM)) scroll = std::max(0, scroll - 3);
        if (DrawButton(x + w - 20, y + h - 20, 18, 18, "v", COL_CHIP, g_fontM)) scroll = std::min(maxScroll, scroll + 3);
        int trackY = y + 24, trackH = h - 48;
        FillRectPx(x + w - 14, trackY, 6, trackH, RGB32(40, 44, 54));
        int thumbH = std::max(14, trackH * rows / (int)lines.size());
        int thumbY = trackY + (trackH - thumbH) * scroll / maxScroll;
        FillRectPx(x + w - 14, thumbY, 6, thumbH, RGB32(110, 120, 140));
    }
    if (hmax > 0) {
        if (DrawButton(x + w - 64, y + h - 20, 18, 18, "<", COL_CHIP, g_fontM)) *hscroll = std::max(0, *hscroll - 16);
        if (DrawButton(x + w - 43, y + h - 20, 18, 18, ">", COL_CHIP, g_fontM)) *hscroll = std::min(hmax, *hscroll + 16);
        DrawText_(x + w - 240, y + h - 18, "cols " + std::to_string(hoff + 1) + "-" + std::to_string(std::min(maxLen, hoff + cols)) + " of " + std::to_string(maxLen) + "  (<- ->)", COL_DIM, g_fontM);
    }
}

// ============================================================
//                    โครงสร้างข้อมูลของเมนู/ฟอร์ม
// ============================================================
enum FeatureKind { FK_FORM, FK_CALC, FK_MATRIX, FK_TABLE };
struct Field {
    std::string label, hint, value;
    std::vector<std::string> choices;     // ไม่ว่าง = ช่องเลือกตัวเลือก
    int choice = 0;
};
struct Feature;
using RunFn = std::function<std::string(Feature&)>;
struct Feature {
    std::string title, desc;
    FeatureKind kind = FK_FORM;
    int preset = 0;                                   // ใช้กับ FK_MATRIX / FK_TABLE
    std::vector<Field> fields;
    std::vector<std::vector<std::string>> examples;   // ตัวอย่างสำหรับปุ่ม Example (ช่อง choice ใส่เลข index)
    int exampleIdx = 0;
    RunFn run;
    std::function<void(Feature&)> refresh;            // ปรับป้ายชื่อช่องตามตัวเลือก
    bool isGraph = false;
    bool noWrap = false;                              // ผลลัพธ์เป็นตาราง: ไม่ตัดบรรทัด แต่เลื่อนซ้าย-ขวาได้
    int hscroll = 0;
    std::string dynLabel;                             // ไม่ว่าง = เพิ่ม/ลบช่องได้ (เช่น สมการที่ k)
    int dynStart = 0, dynMin = 1, dynMax = 8;
    std::string result, error;
    int scroll = 0;
};
struct Category { std::string title, sub; std::vector<Feature> features; };
static std::vector<Category> g_cats;

static Field TF(const std::string& label, const std::string& hint, const std::string& def = "") {
    Field f; f.label = label; f.hint = hint; f.value = def; return f;
}
static Field CF(const std::string& label, std::vector<std::string> opts) {
    Field f; f.label = label; f.choices = std::move(opts); return f;
}
static Feature MkF(const std::string& title, const std::string& desc, std::vector<Field> fields,
                   RunFn run, std::vector<std::vector<std::string>> ex = {}) {
    Feature f; f.title = title; f.desc = desc; f.fields = std::move(fields);
    f.run = std::move(run); f.examples = std::move(ex);
    return f;
}
static Feature MkLab(const std::string& title, const std::string& desc, FeatureKind k, int preset) {
    Feature f; f.title = title; f.desc = desc; f.kind = k; f.preset = preset; return f;
}

// อ่านค่าจากช่อง
static std::string Opt(Feature& f, int i) { return ReplaceAns(Trim(f.fields[i].value)); }
static std::string Req(Feature& f, int i) {
    std::string s = Opt(f, i);
    if (s.empty()) throw std::runtime_error("Please fill in: " + f.fields[i].label);
    return s;
}
// สูตรตรรกศาสตร์ (ใช้ข้อความดิบ ไม่แทรก * หรือแทนค่า ans)
static std::string LReq(Feature& f, int i) {
    std::string s = Trim(f.fields[i].value);
    if (s.empty()) throw std::runtime_error("Please fill in: " + f.fields[i].label);
    return s;
}
static std::string LOpt(Feature& f, int i) { return Trim(f.fields[i].value); }
static double Num(Feature& f, int i) { return EvalNum(Req(f, i)); }
static double NumOr(Feature& f, int i, double d) { return Opt(f, i).empty() ? d : EvalNum(Opt(f, i)); }
static char VarOf(Feature& f, int i) { std::string s = Opt(f, i); return s.empty() ? '\0' : s[0]; }
// เตรียมสูตรจากช่อง exprIdx โดยใช้ช่อง varIdx (ว่าง = ตรวจหาอัตโนมัติ; ตัวแปร a-z, A-Z)
static Prepared PV(Feature& f, int exprIdx, int varIdx, bool allowFree = false) { return PrepVar(Req(f, exprIdx), varIdx >= 0 ? VarOf(f, varIdx) : '\0', allowFree); }
static int Pick(Feature& f, int i) { return f.fields[i].choice; }
static std::vector<double> ParseSet(const std::string& s) {
    std::string t = s;
    for (char& c : t) if (c == '{' || c == '}') c = ' ';
    return ParseList(t, "set elements");
}

// ---------- กราฟ ----------
// โหมด Y อัตโนมัติ  -> ใช้ MathGraph (ผลเหมือน terminal)
// โหมด Y กำหนดเอง  -> วาดด้วย Canvas ภายในไฟล์นี้ เพื่อให้เลื่อนขึ้น/ลง/ซูมแกน y ได้
enum PlotKind { GK_FUNC, GK_DERIV, GK_INTEG, GK_LIMIT, GK_INDEF, GK_AREA2, GK_INVERSE, GK_SCATTER };
struct PlotSpec {
    int kind = GK_FUNC;
    std::string e1, e2, title;
    double a = 0, b = 0;
    DataTable table;
    int xc = 0, yc = 1;
    std::vector<int> ycs;                // หลายซีรีส์ (คอลัมน์ y หลายตัว) ถ้าไม่ว่างจะใช้แทน yc
    std::vector<std::string> names;      // ชื่อซีรีส์สำหรับคำอธิบาย
    std::string disp1, disp2;            // สูตรเดิม (ตัวแปรเดิม) สำหรับแสดงชื่อกราฟ
    char var = 'x';
    bool lines = false;                  // ลากเส้นเชื่อมจุด
};
struct GView {
    std::string xmin, xmax, ymin, ymax, w, h;       // ช่องว่าง = อัตโนมัติ
    double cx0 = -10, cx1 = 10, cy0 = -1, cy1 = 1;   // ช่วงที่แสดงอยู่จริงในภาพล่าสุด
    std::string defX0, defX1;                       // ค่าเมื่อกด Reset
};
static GView MakeView(const char* a, const char* b) { GView v; v.xmin = a; v.xmax = b; v.defX0 = a; v.defX1 = b; return v; }
static GView g_gv = MakeView("-10", "10");
static GView g_gvScatter = MakeView("", "");

struct Canvas {
    int W, H; double x0, x1, y0, y1;
    std::vector<std::string> g;
    Canvas(int w, int h, double xa, double xb, double ya, double yb)
        : W(w), H(h), x0(xa), x1(xb), y0(ya), y1(yb), g(h, std::string(w, ' ')) {}
    double fc(double x) const { return (x - x0) / (x1 - x0) * (W - 1); }
    double fr(double y) const { return (H - 1) - (y - y0) / (y1 - y0) * (H - 1); }
    int col(double x) const { return (int)std::clamp((x - x0) / (x1 - x0) * (W - 1), -1e6, 1e6); }
    int row(double y) const { return H - 1 - (int)std::clamp((y - y0) / (y1 - y0) * (H - 1), -1e6, 1e6); }
    bool inX(double x) const { return x >= x0 && x <= x1; }
    bool inY(double y) const { return y >= y0 && y <= y1; }
    void set(int c, int r, char ch) { if (c >= 0 && c < W && r >= 0 && r < H) g[r][c] = ch; }
    void mark(double x, double y, char ch) { if (inX(x) && inY(y)) set(col(x), row(y), ch); }
    void markBlank(double x, double y, char ch) {
        if (!(inX(x) && inY(y))) return;
        int c = col(x), r = row(y);
        if (c >= 0 && c < W && r >= 0 && r < H && g[r][c] == ' ') g[r][c] = ch;
    }
    void fill(int c, double ya, double yb, char ch) {
        int ra = row(ya), rb = row(yb);
        int lo = std::max(0, std::min(ra, rb)), hi = std::min(H - 1, std::max(ra, rb));
        for (int r = lo; r <= hi; ++r) set(c, r, ch);
    }
    void axes() {
        bool zr = inY(0.0), zc = inX(0.0);
        if (zr) for (int c = 0; c < W; ++c) set(c, row(0.0), '-');
        if (zc) for (int r = 0; r < H; ++r) set(col(0.0), r, '|');
        if (zr && zc) set(col(0.0), row(0.0), '+');
    }
    std::string render(const std::string& title) const {
        std::ostringstream o;
        o << "\n--- ASCII High-Res Graph (" << W << "x" << H << "): " << title << " ---\n";
        for (auto& r : g) o << r << "\n";
        o << "x Range: [" << x0 << " to " << x1 << "], y Range: [" << y0 << " to " << y1 << "]\n\n";
        return o.str();
    }
};
static bool EvalAt(const std::string& e, double x, double& y) {
    try { g_calc.setVariable('x', x); y = g_calc.evaluate(e); return std::isfinite(y) && std::fabs(y) < 1e5; }
    catch (...) { return false; }
}
static std::string CustomRender(const PlotSpec& s, int W, int H, double x0, double x1, double y0, double y1) {
    Canvas c(W, H, x0, x1, y0, y1);
    c.axes();
    double dx = (x1 - x0) / (W - 1);
    std::string title, footer;
    auto D1 = [&]() { return s.disp1.empty() ? s.e1 : s.disp1; };
    auto D2 = [&]() { return s.disp2.empty() ? s.e2 : s.disp2; };
    auto tgt = [](double v) { return std::isinf(v) ? (v > 0 ? std::string("inf") : std::string("-inf")) : Fmt(v); };
    switch (s.kind) {
    case GK_FUNC: {
        title = "f(" + std::string(1, s.var) + ") = " + D1();
        for (int i = 0; i < W; ++i) { double x = x0 + i * dx, y; if (EvalAt(s.e1, x, y)) c.mark(x, y, '*'); }
        break; }
    case GK_DERIV: {
        double slope = g_calc.derivative(s.e1, s.a);
        g_calc.setVariable('x', s.a);
        double ya = g_calc.evaluate(s.e1);
        title = "f(" + std::string(1, s.var) + ") = " + D1() + " | Tangent at " + std::string(1, s.var) + "=" + Fmt(s.a);
        for (int i = 0; i < W; ++i) {
            double x = x0 + i * dx, y;
            if (!EvalAt(s.e1, x, y)) continue;
            c.mark(x, slope * (x - s.a) + ya, '/');
            c.mark(x, y, '*');
        }
        c.mark(s.a, ya, 'O');
        footer = " Slope m (d/dx at x=" + Fmt(s.a) + ") = " + Fmt(slope) + "\n";
        break; }
    case GK_INTEG: {
        double area = g_calc.integral(s.e1, s.a, s.b);
        double pa = std::isinf(s.a) ? (s.a < 0 ? x0 : x1) : s.a, pb = std::isinf(s.b) ? (s.b < 0 ? x0 : x1) : s.b;
        if (pa > pb) std::swap(pa, pb);
        title = "Integral Area of f(x) over [" + tgt(s.a) + ", " + tgt(s.b) + "]";
        for (int i = 0; i < W; ++i) {
            double x = x0 + i * dx, y;
            if (!EvalAt(s.e1, x, y)) continue;
            if (x >= pa && x <= pb) c.fill(i, y, 0.0, '#');
            c.mark(x, y, '*');
        }
        footer = " Definite Integral (Area) = " + Fmt(area) + "\n";
        break; }
    case GK_AREA2: {
        double a = std::min(s.a, s.b), b = std::max(s.a, s.b), area = 0; bool has = true;
        try { area = g_calc.integral("abs((" + s.e1 + ")-(" + s.e2 + "))", a, b); } catch (...) { has = false; }
        title = "Area between f1=[*] " + D1() + " and f2=[~] " + D2();
        for (int i = 0; i < W; ++i) {
            double x = x0 + i * dx, v1, v2;
            if (!EvalAt(s.e1, x, v1) || !EvalAt(s.e2, x, v2)) continue;
            if (x >= a && x <= b) c.fill(i, v1, v2, '#');
            c.mark(x, v1, '*'); c.mark(x, v2, '~');
        }
        footer = has ? " Enclosed Area over [" + Fmt(a) + ", " + Fmt(b) + "] = " + Fmt(area) + "\n" : " [Error] Unable to compute enclosed area\n";
        break; }
    case GK_LIMIT: {
        double lim = 0; bool has = true; std::string status;
        try { lim = g_calc.limit(s.e1, s.a); status = "lim (x->" + tgt(s.a) + ") = " + tgt(lim); }
        catch (const std::exception& e) { has = false; status = std::string("Limit undefined/does not exist (") + e.what() + ")"; }
        title = "Limit Plot of f(x) at x -> " + tgt(s.a);
        if (!std::isinf(s.a) && c.inX(s.a)) {
            int cc = c.col(s.a);
            for (int r = 0; r < H; ++r) if (c.g[r][cc] == ' ') c.g[r][cc] = ':';
            if (has && !std::isinf(lim) && c.inY(lim)) c.set(cc, c.row(lim), 'O');
        }
        for (int i = 0; i < W; ++i) { double x = x0 + i * dx, y; if (EvalAt(s.e1, x, y)) c.mark(x, y, '*'); }
        footer = " Result: " + status + "\n";
        break; }
    case GK_INDEF: {
        title = "f(x) [*] vs Indefinite Integral F(x) [I] (ref=" + Fmt(s.a) + ")";
        for (int i = 0; i < W; ++i) {
            double x = x0 + i * dx;
            try {
                g_calc.setVariable('x', x);
                double y = g_calc.evaluate(s.e1);
                double F = g_calc.integral(s.e1, s.a, x);
                if (std::isfinite(y) && std::isfinite(F)) { c.mark(x, F, 'I'); c.mark(x, y, '*'); }
            } catch (...) {}
        }
        break; }
    case GK_INVERSE: {
        title = "f=[*] " + D1() + "   f^-1=[o]   y=x [.]";
        for (int i = 0; i < W; ++i) { double x = x0 + i * dx; c.markBlank(x, x, '.'); }
        int samples = std::max(W, H) * 8;
        std::vector<std::pair<double, double>> pts;
        for (int i = 0; i <= samples; ++i) { double t = x0 + (x1 - x0) * i / samples, y; if (EvalAt(s.e1, t, y)) pts.emplace_back(t, y); }
        for (auto& p : pts) c.mark(p.first, p.second, '*');
        for (auto& p : pts) c.mark(p.second, p.first, 'o');
        try {
            if (g_fs.isOneToOne(s.e1)) {
                std::string inv = g_fs.inverseExpression(s.e1);
                footer = inv.empty() ? " f is one-to-one, so f^-1 is a function (no closed form shown).\n" : " f^-1(x) = " + inv + "\n";
            } else footer = " [Note] f is not one-to-one, so [o] is only the reflected relation, not a function.\n";
        } catch (...) {}
        break; }
    case GK_SCATTER: {
        title = s.title;
        auto xs = s.table.getColumn(s.xc);
        std::vector<int> cols = s.ycs.empty() ? std::vector<int>{s.yc} : s.ycs;
        static const char sym[] = "*o+x#@%&";
        footer = " Series:";
        for (size_t k = 0; k < cols.size(); ++k) {
            char ch = sym[k % 8];
            auto ys = s.table.getColumn(cols[k]);
            if (s.lines) {                                    // เชื่อมจุดตามลำดับ x
                std::vector<size_t> idx(xs.size());
                for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
                std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return xs[a] < xs[b]; });
                for (size_t i = 0; i + 1 < idx.size(); ++i) {
                    double xa = xs[idx[i]], ya = ys[idx[i]], xb = xs[idx[i + 1]], yb = ys[idx[i + 1]];
                    if ((xa < x0 && xb < x0) || (xa > x1 && xb > x1)) continue;
                    double dc = std::fabs(c.fc(xb) - c.fc(xa)), dr = std::fabs(c.fr(yb) - c.fr(ya));
                    int steps = (int)std::clamp(std::max(dc, dr), 1.0, 4000.0);
                    for (int t = 1; t < steps; ++t) { double u = (double)t / steps; c.markBlank(xa + (xb - xa) * u, ya + (yb - ya) * u, '.'); }
                }
            }
            for (size_t i = 0; i < xs.size(); ++i) c.mark(xs[i], ys[i], ch);
            footer += std::string("  [") + ch + "] " + (k < s.names.size() ? s.names[k] : "y" + std::to_string(cols[k] + 1));
        }
        footer += "\n";
        break; }
    }
    return c.render(title) + footer;
}
static void ParseRanges(const std::string& out, GView& v) {
    size_t p = out.rfind("x Range: [");
    if (p == std::string::npos) return;
    double a, b, c, d;
    if (sscanf(out.c_str() + p, "x Range: [%lf to %lf], y Range: [%lf to %lf]", &a, &b, &c, &d) == 4) {
        v.cx0 = a; v.cx1 = b; v.cy0 = c; v.cy1 = d;
    }
}
static std::string GraphOut(GView& v, const PlotSpec& s) {
    auto blank = [](const std::string& t) { return Trim(t).empty(); };
    bool autoX = blank(v.xmin) || blank(v.xmax), autoY = blank(v.ymin) || blank(v.ymax);
    double x0 = -10, x1 = 10;
    if (!autoX) {
        x0 = EvalNum(v.xmin); x1 = EvalNum(v.xmax);
        if (!(x0 < x1)) throw std::runtime_error("xMin must be less than xMax");
    } else if (s.kind == GK_SCATTER) {
        auto xs = s.table.getColumn(s.xc);
        if (!xs.empty()) { x0 = *std::min_element(xs.begin(), xs.end()); x1 = *std::max_element(xs.begin(), xs.end()); }
        if (std::fabs(x1 - x0) < 1e-6) { x0 -= 1; x1 += 1; }
    }
    int W = blank(v.w) ? std::max(20, g_resCols - 3) : (int)EvalNum(v.w);
    int H = blank(v.h) ? std::max(8, g_resRows - 6) : (int)EvalNum(v.h);
    W = std::clamp(W, 20, 300); H = std::clamp(H, 8, 120);
    std::string out;
    bool multi = s.kind == GK_SCATTER && (s.ycs.size() > 1 || s.lines);
    if (autoY && !multi) {
        MathGraph g(W, H);
        CoutCapture cap;
        try {
            switch (s.kind) {
                case GK_FUNC:    g.plotFunction(g_calc, s.e1, x0, x1); break;
                case GK_DERIV:   g.plotDerivative(g_calc, s.e1, s.a, x0, x1); break;
                case GK_INTEG:   g.plotIntegralArea(g_calc, s.e1, s.a, s.b, x0, x1); break;
                case GK_LIMIT:   g.plotLimit(g_calc, s.e1, s.a, x0, x1); break;
                case GK_INDEF:   g.plotIndefiniteIntegral(g_calc, s.e1, s.a, x0, x1); break;
                case GK_AREA2:   g.plotAreaBetweenCurves(g_calc, s.e1, s.e2, s.a, s.b, x0, x1); break;
                case GK_INVERSE: g.plotInverseFunction(g_calc, s.e1, x0, x1); break;
                case GK_SCATTER:
                    if (autoX) g.plotDataTable(s.table, s.xc, s.yc, s.title);
                    else g.plotDataTableRange(s.table, s.xc, s.yc, s.title, x0, x1);
                    break;
            }
        } catch (const std::exception& e) { cap.ss << "[Error] " << e.what() << "\n"; }
        out = cap.ss.str();
    } else {
        double y0, y1;
        if (autoY) {                                           // สเกล y อัตโนมัติจากข้อมูลทุกซีรีส์ในช่วง x ที่เห็น
            std::vector<int> cols = s.ycs.empty() ? std::vector<int>{s.yc} : s.ycs;
            auto xs = s.table.getColumn(s.xc);
            bool any = false; y0 = 1e300; y1 = -1e300;
            for (int cidx : cols) {
                auto ys = s.table.getColumn(cidx);
                for (size_t i = 0; i < xs.size(); ++i)
                    if (xs[i] >= x0 && xs[i] <= x1 && std::isfinite(ys[i])) { y0 = std::min(y0, ys[i]); y1 = std::max(y1, ys[i]); any = true; }
            }
            if (!any) throw std::runtime_error("No data points inside the x range");
            if (std::fabs(y1 - y0) < 1e-6) { y0 -= 1; y1 += 1; }
            double pad = (y1 - y0) * 0.05; y0 -= pad; y1 += pad;
        } else {
            y0 = EvalNum(v.ymin); y1 = EvalNum(v.ymax);
            if (!(y0 < y1)) throw std::runtime_error("yMin must be less than yMax");
        }
        out = CustomRender(s, W, H, x0, x1, y0, y1);
    }
    out = CleanAscii(out);
    ParseRanges(out, v);
    if (s.var != 'x') out += std::string(" (variable '") + s.var + "' is drawn on the horizontal axis; the plotter labels it x)\n";
    return out;
}
static PlotSpec SpecV(int kind, const Prepared& p, double a = 0, double b = 0, const Prepared* p2 = nullptr) {
    PlotSpec s; s.kind = kind; s.e1 = p.xexpr; s.disp1 = p.sub; s.var = p.var; s.a = a; s.b = b;
    if (p2) { s.e2 = p2->xexpr; s.disp2 = p2->sub; }
    return s;
}
static PlotSpec Spec(int kind, const std::string& e1, double a = 0, double b = 0, const std::string& e2 = "") {
    PlotSpec s; s.kind = kind; s.e1 = e1; s.e2 = e2; s.a = a; s.b = b; return s;
}

// ---------- สร้างแค็ตตาล็อกฟีเจอร์ ----------
static const char* DIST_NAMES[] = {"Binomial PDF", "Binomial CDF", "Poisson PDF", "Poisson CDF",
                                   "Hypergeometric", "Geometric", "Normal PDF", "Normal CDF"};
static const char* DIST_LABELS[8][4] = {
    {"k", "n", "p", ""}, {"k", "n", "p", ""}, {"k", "lambda", "", ""}, {"k", "lambda", "", ""},
    {"k", "N (population)", "K (successes)", "n (draws)"}, {"k", "p", "", ""},
    {"x", "mean (default 0)", "stdev (default 1)", ""}, {"x", "mean (default 0)", "stdev (default 1)", ""}};

static ModelText MTxt(Feature& f, int d) {       // fields d..d+3 = Domain, Constants, Relations, Functions
    ModelText t;
    t.domain = LOpt(f, d); t.constants = LOpt(f, d + 1); t.relations = LOpt(f, d + 2); t.functions = LOpt(f, d + 3);
    return t;
}
static const char* ADV_TABLEAU_TEXT =
"TABLEAU RULES AND STRATEGY\n\n"
"A tableau starts with the premises and the NEGATED conclusion. A branch CLOSES when it contains phi and ~phi.\n\n"
"alpha rules (no split):\n"
"   A & B        =>  A , B                 ~(A | B)   =>  ~A , ~B\n"
"   ~(A -> B)    =>  A , ~B                ~~A        =>  A\n\n"
"beta rules (the branch splits):\n"
"   A | B        =>  A  |  B               ~(A & B)   =>  ~A  |  ~B\n"
"   A -> B       =>  ~A  |  B              A <-> B    =>  (A , B)  |  (~A , ~B)\n"
"   ~(A <-> B)   =>  (A , ~B)  |  (~A , B)\n\n"
"gamma rules (universal; may be used again for every new constant):\n"
"   forall x A(x)   =>  A(t) for EVERY ground term t on the branch\n"
"   ~exists x A(x)  =>  ~A(t) for every t\n\n"
"delta rules (existential; introduce a NEW constant c):\n"
"   exists x A(x)   =>  A(c)                ~forall x A(x)  =>  ~A(c)\n\n"
"Strategy used here: alpha, then delta, then beta, then gamma (gamma last, because it can be applied forever).\n\n"
"Outcomes\n"
"   all branches closed         => premises + ~conclusion unsatisfiable => the argument is VALID (soundness + completeness)\n"
"   a complete open branch      => its constants and atoms form a COUNTER-MODEL (the argument is invalid)\n"
"   an infinite branch          => no answer: first-order validity is only semi-decidable\n"
"                                  (e.g. forall x exists y R(x,y) generates c1, c2, c3, ... forever)\n";
static const char* ADV_SYNTAX_TEXT =
"INPUT SYNTAX FOR THE ADVANCED LOGIC FEATURES\n\n"
"First-order formulas\n"
"   atoms        P(x)   R(x,y)   Rxy   f(x,a) = b   x != y      (Rxy is read as R(x,y) for single-letter terms)\n"
"   connectives  ~   &   |   ->   <->      (also not and or implies iff, and the symbols of the notes)\n"
"   quantifiers  forall x A    exists y A   bind tightly (like ~). For a long scope use a dot:  forall x. P(x) -> Q(x)\n"
"   symbols      variables  u v w x y z (with digits);  constants  a b c john 0 1 ...;  functions  f(..) g(..)\n"
"                predicates  start with a capital letter: P  Q  R  Man  Prime\n\n"
"Models (finite)\n"
"   Domain       m1, m2, m3          or   1..4\n"
"   Constants    a=m1, b=m2\n"
"   Relations    P={m1}; R={(m1,m1),(m1,m2)}        (empty relation:  P={})\n"
"   Functions    f(m1)=m2, f(m2)=m1, g(m1,m2)=m1       (must be defined for every argument)\n"
"   Assignment   x=m1, y=m2          (for formulas with free variables)\n\n"
"Turing machines   rule:  state read -> newstate write move     e.g.  q0 1 -> q0 1 R   (move L, R or N; '_' = blank; '*' = any symbol)\n"
"                  separate rules with ';'. The machine halts when no rule applies.\n\n"
"Recursive functions   Z   S   P(i,k)   n (constant)   C(f; g1,g2)   R(g; h)   M(f)   and the library names add mul pred monus exp fact sg nsg dist\n"
"Goedel numbering      symbols ~ & | -> <-> forall exists ( ) , = != + * letters digits  (see the code table in the output)\n";

static void BuildCatalog() {
    // ===================== [1] Algebra =====================
    {
        Category c{"Algebra Calculator", "Continuous calc, ans memory, all functions", {}};
        c.features.push_back(MkLab("Algebra Calculator", "", FK_CALC, 0));
        g_cats.push_back(c);
    }
    // ===================== [2] Calculus =====================
    {
        Category c{"Calculus Solver", "Derivative, Integral, Limit - any variable a-z / A-Z", {}};
        const char* VH = "x, t, a ...";
        c.features.push_back(MkF("Derivative  d/dx f(x) at x0", "Numeric derivative at a point. Any variable letter works (blank = auto-detect).",
            {TF("f(x)", "e.g. x^2 + sin(x)   or   t^3 + 2t"), TF("x0  (point)", "e.g. 2"), TF("Variable (blank = auto)", VH)},
            [](Feature& f) -> std::string {
                Prepared p = PV(f, 0, 2); double x0 = Num(f, 1);
                double v = g_calc.derivative(p.xexpr, x0); SetAns(v);
                std::string V(1, p.var);
                return "d/d" + V + " f   at " + V + " = " + Fmt(x0) + "   :   " + Fmt(v);
            }, {{"x^2+sin(x)", "2", ""}, {"t^3+2t", "1", ""}}));
        c.features.push_back(MkF("Definite / Improper Integral", "Integral of f over [a, b]. Use inf / -inf for improper integrals.",
            {TF("f(x)", "e.g. x^2   or   t^2 + k   (k set in Vars)"), TF("a  (lower)", "e.g. 0"), TF("b  (upper)", "e.g. 3  or  inf"), TF("Variable (blank = auto)", VH)},
            [](Feature& f) -> std::string {
                Prepared p = PV(f, 0, 3); double a = Num(f, 1), b = Num(f, 2);
                double v = g_calc.integral(p.xexpr, a, b); SetAns(v);
                return "Integral [" + Fmt(a) + " -> " + Fmt(b) + "] f d" + std::string(1, p.var) + " = " + Fmt(v);
            }, {{"x^2", "0", "3", ""}, {"1/(t^2+1)", "0", "inf", ""}, {"e^(-x)", "0", "inf", ""}}));
        c.features.push_back(MkF("Indefinite Integral F(x)", "F(x0) = integral of f from x_ref to x0.",
            {TF("f(x)", "e.g. 2x"), TF("x0  (evaluate at)", "e.g. 3"), TF("x_ref  (start, default 0)", "0", "0"), TF("Variable (blank = auto)", VH)},
            [](Feature& f) -> std::string {
                Prepared p = PV(f, 0, 3); double x0 = Num(f, 1), xr = NumOr(f, 2, 0.0);
                double v = g_calc.indefiniteIntegral(p.xexpr, x0, xr); SetAns(v);
                std::string V(1, p.var);
                return "F(" + Fmt(x0) + ") = Integral[" + V + ": " + Fmt(xr) + " -> " + Fmt(x0) + "] f d" + V + " = " + Fmt(v);
            }, {{"2*x", "3", "0", ""}, {"2*s", "3", "0", ""}}));
        c.features.push_back(MkF("Limit  lim x->a f(x)", "Two-sided or one-sided limit. a may be inf / -inf.",
            {TF("f(x)", "e.g. sin(x)/x   or   sin(t)/t"), TF("a  (variable tends to)", "e.g. 0"), CF("Direction", {"both sides", "right (a+)", "left (a-)"}), TF("Variable (blank = auto)", VH)},
            [](Feature& f) -> std::string {
                Prepared p = PV(f, 0, 3); double a = Num(f, 1);
                char side = Pick(f, 2) == 1 ? '+' : Pick(f, 2) == 2 ? '-' : '\0';
                double v = g_calc.limit(p.xexpr, a, 1e-6, side); SetAns(v);
                return "lim " + std::string(1, p.var) + "->" + Fmt(a) + (side ? std::string(1, side) : std::string()) + " f = " + Fmt(v);
            }, {{"sin(x)/x", "0", "0", ""}, {"1/x", "0", "1", ""}, {"sin(t)/t", "0", "0", ""}}));
        c.features.push_back(MkF("Symbolic Derivative  f'(x)", "Returns f'(x) as a formula. Other letters are treated as constants.",
            {TF("f(x)", "e.g. x^2 + sin(x)   or   a*t^2"), TF("Variable (blank = auto)", VH)},
            [](Feature& f) -> std::string {
                Prepared p = PV(f, 0, 1, true);
                return "d/d" + std::string(1, p.var) + "  = " + g_calc.symbolicDerivative(p.sub, p.var);
            }, {{"x^2+sin(x)", ""}, {"x*e^x", ""}, {"a*t^2+b*t", "t"}}));
        c.features.push_back(MkF("Symbolic Indefinite Integral  F(x)", "Returns F(x) as a formula (without + C). Other letters are constants.",
            {TF("f(x)", "e.g. x^2 + sin(x)   or   a*t"), TF("Variable (blank = auto)", VH)},
            [](Feature& f) -> std::string {
                Prepared p = PV(f, 0, 1, true);
                return "Integral d" + std::string(1, p.var) + "  = " + g_calc.symbolicIntegral(p.sub, p.var) + " + C";
            }, {{"x^2+sin(x)", ""}, {"1/(x^2+1)", ""}, {"a*t+b", "t"}}));
        g_cats.push_back(c);
    }
    // ===================== [3] Equation Solver =====================
    {
        Category c{"Alpha Equation Solver", "Polynomial, factorization, f(x)=0, systems - any variable a-z / A-Z", {}};
        const char* VH = "x, t, a ... (blank = auto)";
        c.features.push_back(MkF("Polynomial Solver (degree 2, 3, 4, N)", "Coefficients from the highest power down, e.g. x^2-3x+2 -> 1 -3 2",
            {TF("Coefficients", "1 -3 2")},
            [](Feature& f) -> std::string {
                auto co = ParseList(Req(f, 0), "coefficients");
                size_t s = 0; while (s + 1 < co.size() && co[s] == 0) ++s;
                std::vector<double> use(co.begin() + s, co.end());
                auto roots = g_calc.solver().solvePolynomial(use);
                std::string t = "Degree " + std::to_string(use.size() - 1) + " polynomial, " + std::to_string(roots.size()) + " root(s):\n";
                for (size_t i = 0; i < roots.size(); ++i) t += "  x" + std::to_string(i + 1) + " = " + FmtComplex(roots[i]) + "\n";
                if (!roots.empty()) SetAns(roots[0].real());
                return t;
            }, {{"1 -3 2"}, {"1 0 0 -1"}, {"1 0 -5 0 4"}}));
        c.features.push_back(MkF("Polynomial Factorization", "Factor a polynomial. Choose the variable letter for the printed factors.",
            {TF("Coefficients", "1 -3 2"), TF("Variable (blank = x)", "x, t, a, ...")},
            [](Feature& f) -> std::string {
                auto co = ParseList(Req(f, 0), "coefficients");
                char v = VarOf(f, 1); if (!v) v = 'x';
                return g_calc.solver().factorizePolynomial(co, v);
            }, {{"1 -6 11 -6", ""}, {"1 -3 2", "t"}}));
        c.features.push_back(MkF("General Equation  f(x) = 0", "Numeric root finder (Newton). Any variable letter; parameters from the Vars box.",
            {TF("f(x)  (= 0)", "e.g. cos(x) - x   or   cos(t) - t"), TF("Initial guess", "1", "1"), TF("Variable", VH)},
            [](Feature& f) -> std::string {
                Prepared p = PV(f, 0, 2);
                g_calc.solver().setAns(g_ans);
                double v = g_calc.solver().solveGeneralEquation(g_calc, p.sub, NumOr(f, 1, 1.0), 1000, 1e-7, p.var);
                SetAns(v); return std::string(1, p.var) + " = " + Fmt(v);
            }, {{"cos(x)-x", "1", ""}, {"t^3-2t-5", "2", ""}}));
        c.features.push_back(MkF("Derivative Equation  f'(x) = target", "Find the variable where f' equals a value (target 0 = critical point).",
            {TF("f(x)", "e.g. x^3 - 3x"), TF("Target value", "0", "0"), TF("Initial guess", "1", "1"), TF("Variable", VH)},
            [](Feature& f) -> std::string {
                Prepared p = PV(f, 0, 3);
                double v = g_calc.solver().solveDerivativeEq(g_calc, p.sub, NumOr(f, 1, 0.0), NumOr(f, 2, 1.0), 1000, 1e-7, p.var);
                SetAns(v); return std::string(1, p.var) + " = " + Fmt(v);
            }, {{"x^3-3x", "0", "2", ""}, {"t^3-3t", "0", "2", ""}}));
        c.features.push_back(MkF("Integral Bound  Integral[a..b] f dx = target", "Find the upper bound b.",
            {TF("f(x)", "e.g. 2x"), TF("a  (lower bound)", "0", "0"), TF("Target value", "e.g. 9"), TF("Initial guess", "1", "1"), TF("Variable", VH)},
            [](Feature& f) -> std::string {
                Prepared p = PV(f, 0, 4);
                double v = g_calc.solver().solveIntegralBound(g_calc, p.sub, NumOr(f, 1, 0.0), Num(f, 2), NumOr(f, 3, 1.0), 1000, 1e-7, p.var);
                SetAns(v); return "b = " + Fmt(v);
            }, {{"2x", "0", "9", "1", ""}, {"2t", "0", "9", "1", "t"}}));
        c.features.push_back(MkF("Limit Equation  lim x->a f(x) = target", "Find a such that the limit equals target.",
            {TF("f(x)", "e.g. x^2"), TF("Target value", "e.g. 9"), TF("Initial guess", "1", "1"), TF("Variable", VH)},
            [](Feature& f) -> std::string {
                Prepared p = PV(f, 0, 3);
                double v = g_calc.solver().solveLimitEq(g_calc, p.sub, Num(f, 1), NumOr(f, 2, 1.0), 1000, 1e-7, p.var);
                SetAns(v); return "a = " + Fmt(v);
            }, {{"x^2", "9", "2", ""}, {"t^2", "9", "2", ""}}));
        c.features.push_back(MkF("Indefinite Integral Equation  F(x) = target", "Find the variable where F (integral from x_ref) equals target.",
            {TF("f(x)", "e.g. 2x"), TF("Target value", "e.g. 16"), TF("x_ref (default 0)", "0", "0"), TF("Initial guess", "1", "1"), TF("Variable", VH)},
            [](Feature& f) -> std::string {
                Prepared p = PV(f, 0, 4);
                double v = g_calc.solver().solveIndefiniteIntegralEq(g_calc, p.sub, Num(f, 1), NumOr(f, 2, 0.0), NumOr(f, 3, 1.0), 1000, 1e-7, p.var);
                SetAns(v); return std::string(1, p.var) + " = " + Fmt(v);
            }, {{"2x", "16", "0", "1", ""}}));
        c.features.push_back(MkF("Two-Sided Equation  lhs = rhs", "Any equation in one variable (a-z, A-Z). Use ans for your last result.",
            {TF("Equation", "e.g. x^2 + 2 = 3x   or   t^2 = 9"), TF("Initial guess", "1", "1"), TF("Variable", VH)},
            [](Feature& f) -> std::string {
                Prepared p = PV(f, 0, 2);
                g_calc.solver().setAns(g_ans);
                double v = g_calc.solver().solveTwoSideEquation(g_calc, p.sub, NumOr(f, 1, 1.0), 1000, 1e-7, p.var);
                SetAns(v); return std::string(1, p.var) + " = " + Fmt(v);
            }, {{"x^2+2=3x", "1", ""}, {"x + 2 = ans", "1", ""}, {"t^2 = 9", "1", "t"}}));
        {
            Feature f = MkF("Linear System  (any # of equations / variables)",
                "One equation per box. Variables are detected automatically (any letters a-z, A-Z). Use [+ Add Equation] for more.",
                {TF("Equation 1", "e.g. 2x + 3y = 8"), TF("Equation 2", "e.g. x - y = 1")},
                [](Feature& f) -> std::string {
                    std::vector<std::string> eqs;
                    for (auto& fd : f.fields) { std::string s = ApplyParams(ReplaceAns(Trim(fd.value))); if (!s.empty()) eqs.push_back(s); }
                    if (eqs.empty()) throw std::runtime_error("Please enter at least one equation");
                    auto sol = g_calc.solver().solveLinearSystem(g_calc, eqs);
                    std::string t = std::to_string(eqs.size()) + " equation(s), " + std::to_string(sol.size()) + " variable(s):\n";
                    for (auto& kv : sol) t += "  " + std::string(1, kv.first) + " = " + Fmt(kv.second) + "\n";
                    if (!sol.empty()) SetAns(sol.begin()->second);
                    return t;
                }, {{"2x+3y=8", "x-y=1"}, {"x+y+z=6", "2x-y+z=3", "x+2y-z=2"}, {"2a+3b=8", "a-b=1"}});
            f.dynLabel = "Equation"; f.dynStart = 0; f.dynMin = 1; f.dynMax = 10;
            c.features.push_back(f);
        }
        {
            Feature f = MkF("Nonlinear System  (any # of equations / variables)",
                "Newton / Gauss-Newton. Optionally give initial guesses like  x=1, y=2  (any letters).",
                {TF("Initial guess (optional)", "x=1, y=2"), TF("Equation 1", "e.g. x^2 + y^2 = 25"), TF("Equation 2", "e.g. x - y = 1")},
                [](Feature& f) -> std::string {
                    std::vector<std::string> eqs;
                    for (size_t i = 1; i < f.fields.size(); ++i) { std::string s = ApplyParams(ReplaceAns(Trim(f.fields[i].value))); if (!s.empty()) eqs.push_back(s); }
                    if (eqs.empty()) throw std::runtime_error("Please enter at least one equation");
                    std::map<char, double> guess;
                    if (!Opt(f, 0).empty()) guess = ParseGuess(Opt(f, 0));
                    auto sol = g_calc.solver().solveNonlinearSystem(g_calc, eqs, guess);
                    std::string t = "Solution:\n";
                    for (auto& kv : sol) t += "  " + std::string(1, kv.first) + " = " + Fmt(kv.second) + "\n";
                    t += "(Different initial guesses may reveal other solutions.)";
                    if (!sol.empty()) SetAns(sol.begin()->second);
                    return t;
                }, {{"x=3,y=3", "x^2+y^2=25", "x-y=1"}, {"a=3,b=3", "a^2+b^2=25", "a-b=1"}});
            f.dynLabel = "Equation"; f.dynStart = 1; f.dynMin = 1; f.dynMax = 10;
            c.features.push_back(f);
        }
        g_cats.push_back(c);
    }
    // ===================== [4] Graph =====================
    {
        Category c{"2D Text Graph Plotter", "9 plot types, any variable a-z / A-Z, zoom, pan up/down/left/right", {}};
        auto G = [](Feature f) { f.isGraph = true; return f; };
        const char* VH = "x, t, a ... (blank = auto)";
        c.features.push_back(G(MkF("Plot f(x)", "Plot a function of any variable letter. Use Zoom / Left / Right / Up / Down to move the view.",
            {TF("f(x)", "e.g. sin(x)   x^2 - 4   or   a*sin(t) (a in Vars)"), TF("Variable", VH)},
            [](Feature& f) -> std::string { return GraphOut(g_gv, SpecV(GK_FUNC, PV(f, 0, 1))); },
            {{"sin(x)", ""}, {"x^2-4", ""}, {"x^3-3x", ""}, {"sin(t)/t", ""}})));
        c.features.push_back(G(MkF("Plot Polynomial with Roots", "Coefficients from the highest power down. Roots are listed below the graph.",
            {TF("Coefficients", "1 -3 2")},
            [](Feature& f) -> std::string {
                auto co = ParseList(Req(f, 0), "coefficients");
                size_t s0 = 0; while (s0 + 1 < co.size() && co[s0] == 0) ++s0;
                std::vector<double> use(co.begin() + s0, co.end());
                std::string e;                                    // สร้างสมการเองด้วย '*' ชัดเจน
                for (size_t i = 0; i < use.size(); ++i) {
                    int p = (int)use.size() - 1 - (int)i;
                    char buf[64]; snprintf(buf, sizeof buf, "%.12g", use[i]);
                    e += (i ? "+" : "") + std::string("(") + buf + ")" + (p > 0 ? "*x^" + std::to_string(p) : "");
                }
                std::string out = GraphOut(g_gv, Spec(GK_FUNC, e));
                auto roots = g_calc.solver().solvePolynomial(use);
                out += "\nRoots:";
                for (auto& z : roots) out += "   " + FmtComplex(z);
                return out;
            }, {{"1 -3 2"}, {"1 0 -4"}, {"1 0 1"}})));
        c.features.push_back(G(MkF("Plot Derivative & Tangent at x0", "Graph of f with its tangent line at the point x0.",
            {TF("f(x)", "e.g. x^2   or   t^2"), TF("x0", "e.g. 1"), TF("Variable", VH)},
            [](Feature& f) -> std::string { return GraphOut(g_gv, SpecV(GK_DERIV, PV(f, 0, 2), Num(f, 1))); },
            {{"x^2", "1", ""}, {"t^3", "1", ""}})));
        c.features.push_back(G(MkF("Plot Integral Area over [a, b]", "Shaded area under f.",
            {TF("f(x)", "e.g. x^2"), TF("a", "e.g. 0"), TF("b", "e.g. 2"), TF("Variable", VH)},
            [](Feature& f) -> std::string { return GraphOut(g_gv, SpecV(GK_INTEG, PV(f, 0, 3), Num(f, 1), Num(f, 2))); },
            {{"x^2", "0", "2", ""}, {"s^2", "0", "2", ""}})));
        c.features.push_back(G(MkF("Plot Limit  x -> a", "Show f approaching a.",
            {TF("f(x)", "e.g. sin(x)/x"), TF("a", "e.g. 0"), TF("Variable", VH)},
            [](Feature& f) -> std::string { return GraphOut(g_gv, SpecV(GK_LIMIT, PV(f, 0, 2), Num(f, 1))); },
            {{"sin(x)/x", "0", ""}, {"sin(t)/t", "0", ""}})));
        c.features.push_back(G(MkF("Plot Indefinite Integral F(x)", "Graph of F(x) = integral of f from x_ref.",
            {TF("f(x)", "e.g. cos(x)"), TF("x_ref (default 0)", "0", "0"), TF("Variable", VH)},
            [](Feature& f) -> std::string { return GraphOut(g_gv, SpecV(GK_INDEF, PV(f, 0, 2), NumOr(f, 1, 0.0))); },
            {{"cos(x)", "0", ""}, {"cos(t)", "0", ""}})));
        c.features.push_back(MkLab("Plot Scatter Plot from DataTable", "Enter a data table and plot two columns.", FK_TABLE, 6));
        c.features.push_back(G(MkF("Plot Area Between Two Curves", "Shaded area between f1 and f2 on [a, b] (same variable for both).",
            {TF("f1(x)", "e.g. x"), TF("f2(x)", "e.g. x^2"), TF("a", "e.g. 0"), TF("b", "e.g. 1"), TF("Variable", VH)},
            [](Feature& f) -> std::string {
                Prepared p1 = PV(f, 0, 4), p2 = PV(f, 1, 4);
                if (p1.var != p2.var && VarOf(f, 4) == '\0') p2 = PrepVar(Req(f, 1), p1.var);
                return GraphOut(g_gv, SpecV(GK_AREA2, p1, Num(f, 2), Num(f, 3), &p2));
            }, {{"x", "x^2", "0", "1", ""}, {"t", "t^2", "0", "1", ""}})));
        c.features.push_back(G(MkF("Plot Inverse Function f^-1(x)", "f, its inverse (reflection) and y = x.",
            {TF("f(x)", "e.g. 2x + 1  or  x^3"), TF("Variable", VH)},
            [](Feature& f) -> std::string { return GraphOut(g_gv, SpecV(GK_INVERSE, PV(f, 0, 1))); },
            {{"2x+1", ""}, {"x^3", ""}, {"2t+1", ""}})));
        g_cats.push_back(c);
    }
    // ===================== [5] Matrix =====================
    {
        Category c{"Multi-Matrix Calculator", "N matrices of any size n x m, with calculus", {}};
        const char* names[] = {"Add N Matrices", "Subtract N Matrices", "Multiply N Matrices", "Transpose Matrix  (A^T)",
                               "Determinant  det(A)", "Inverse Matrix  (A^-1)", "Matrix Derivative (each element)",
                               "Matrix Integral (each element)", "Matrix Indefinite Integral (each element)"};
        for (int i = 0; i < 9; ++i) c.features.push_back(MkLab(names[i], "Opens the matrix editor.", FK_MATRIX, i));
        c.features.push_back(MkLab("Plot Scatter Plot from DataTable", "Enter a data table and plot two columns.", FK_TABLE, 6));
        g_cats.push_back(c);
    }
    // ===================== [6] Table & Probability =====================
    {
        Category c{"Table & Probability Calculator", "Statistics, distributions, probability tables", {}};
        c.features.push_back(MkLab("Input Data Table & Column Statistics", "Editable table: stats, computed columns, transpose.", FK_TABLE, 0));
        c.features.push_back(MkF("Binomial Distribution Table", "Table of P(X = k) and P(X <= k) for k = 0..n.",
            {TF("n  (trials)", "e.g. 10"), TF("p  (success probability)", "e.g. 0.5")},
            [](Feature& f) -> std::string {
                return FormatTable(TableCalculate::generateBinomialTable((int)Num(f, 0), Num(f, 1)));
            }, {{"10", "0.5"}}));
        c.features.push_back(MkF("Poisson Distribution Table", "Table of P(X = k) and P(X <= k).",
            {TF("lambda", "e.g. 3"), TF("max k  (default 20)", "20", "20")},
            [](Feature& f) -> std::string {
                return FormatTable(TableCalculate::generatePoissonTable(Num(f, 0), (int)NumOr(f, 1, 20)));
            }, {{"3", "12"}}));
        c.features.push_back(MkF("Standard Normal Z-Table", "pdf and cdf of Z.",
            {TF("z min", "-3.4", "-3.4"), TF("z max", "3.4", "3.4"), TF("step", "0.1", "0.1")},
            [](Feature& f) -> std::string {
                return FormatTable(TableCalculate::generateNormalZTable(NumOr(f, 0, -3.4), NumOr(f, 1, 3.4), NumOr(f, 2, 0.1)));
            }, {{"-3.4", "3.4", "0.1"}}));
        {
            Feature f = MkF("Probability Distributions Calculator", "Choose a distribution and fill its parameters.",
                {CF("Distribution", {DIST_NAMES[0], DIST_NAMES[1], DIST_NAMES[2], DIST_NAMES[3], DIST_NAMES[4], DIST_NAMES[5], DIST_NAMES[6], DIST_NAMES[7]}),
                 TF("", ""), TF("", ""), TF("", ""), TF("", "")},
                [](Feature& f) -> std::string {
                    int d = Pick(f, 0);
                    double a = Num(f, 1);
                    switch (d) {
                        case 0: return "P(X = " + Fmt(a) + ") = " + Fmt(TableCalculate::binomPDF((int)a, (int)Num(f, 2), Num(f, 3)));
                        case 1: return "P(X <= " + Fmt(a) + ") = " + Fmt(TableCalculate::binomCDF((int)a, (int)Num(f, 2), Num(f, 3)));
                        case 2: return "P(X = " + Fmt(a) + ") = " + Fmt(TableCalculate::poissonPDF((int)a, Num(f, 2)));
                        case 3: return "P(X <= " + Fmt(a) + ") = " + Fmt(TableCalculate::poissonCDF((int)a, Num(f, 2)));
                        case 4: return "P(X = " + Fmt(a) + ") = " + Fmt(TableCalculate::hypergeomPDF((int)a, (int)Num(f, 2), (int)Num(f, 3), (int)Num(f, 4)));
                        case 5: return "P(X = " + Fmt(a) + ") = " + Fmt(TableCalculate::geometricPDF((int)a, Num(f, 2)));
                        case 6: return "pdf(" + Fmt(a) + ") = " + Fmt(TableCalculate::normalPDF(a, NumOr(f, 2, 0), NumOr(f, 3, 1)));
                        default: return "cdf(" + Fmt(a) + ") = " + Fmt(TableCalculate::normalCDF(a, NumOr(f, 2, 0), NumOr(f, 3, 1)));
                    }
                }, {{"0", "3", "10", "0.5", ""}, {"1", "3", "10", "0.5", ""}, {"6", "1.96", "0", "1", ""}});
            f.refresh = [](Feature& f) {
                int d = Pick(f, 0);
                for (int i = 0; i < 4; ++i) {
                    std::string l = DIST_LABELS[d][i];
                    f.fields[i + 1].label = l.empty() ? "(not used)" : l;
                    f.fields[i + 1].hint = l.empty() ? "" : "value";
                }
            };
            c.features.push_back(f);
        }
        c.features.push_back(MkF("Combinatorics  (nCr, nPr)", "Combinations and permutations.",
            {TF("n", "e.g. 10"), TF("r", "e.g. 3")},
            [](Feature& f) -> std::string {
                int n = (int)Num(f, 0), r = (int)Num(f, 1);
                return "nCr = " + std::to_string(TableCalculate::nCr(n, r)) + "\nnPr = " + std::to_string(TableCalculate::nPr(n, r));
            }, {{"10", "3"}}));
        c.features.push_back(MkLab("Contingency / Joint Probability Table", "Marginal probabilities of a joint table.", FK_TABLE, 4));
        g_cats.push_back(c);
    }
    // ===================== [7] Relations & Functions =====================
    {
        Category c{"Relations & Functions", "Domain, range, inverse, composition, f(x+h)", {}};
        {
            Feature f = MkF("Cartesian Product  A x B (x C ...)", "Any number of sets. Elements separated by commas.",
                {TF("Set 1", "e.g. 1, 2, 3"), TF("Set 2", "e.g. 4, 5")},
                [](Feature& f) -> std::string {
                    std::vector<FunctionSolver::NumSet> sets;
                    for (auto& fd : f.fields) { std::string s = Trim(fd.value); if (!s.empty()) sets.push_back(ParseSet(ReplaceAns(s))); }
                    if (sets.size() < 1) throw std::runtime_error("Please enter at least one set");
                    auto tuples = FunctionSolver::cartesianProductN(sets);
                    return "Number of elements: " + std::to_string(tuples.size()) + "\n" + FunctionSolver::toString(tuples, 400);
                }, {{"1,2,3", "4,5"}, {"1,2", "5,6", "3,4"}});
            f.dynLabel = "Set"; f.dynStart = 0; f.dynMin = 2; f.dynMax = 6;
            c.features.push_back(f);
        }
        c.features.push_back(MkF("Relation from Condition", "r = {(x,y) in AxB | condition}. Shows domain, range, inverse, type.",
            {TF("A", "e.g. 1, 2, 3, 4"), TF("B", "e.g. 1, 2, 3, 4, 5, 6, 7"), TF("Condition", "e.g. y = 2x + 1   or   x < y")},
            [](Feature& f) -> std::string {
                auto A = ParseSet(Req(f, 0)), B = ParseSet(Req(f, 1));
                std::string cond = ApplyParamsKeep(Req(f, 2), "xy");
                auto r = g_fs.relationFromCondition(A, B, cond);
                std::string t = "r = " + FunctionSolver::toString(r) + "\n";
                t += "Domain  = " + FunctionSolver::toString(FunctionSolver::domainOf(r)) + "\n";
                t += "Range   = " + FunctionSolver::toString(FunctionSolver::rangeOf(r)) + "\n";
                t += "r^-1    = " + FunctionSolver::toString(FunctionSolver::inverseRelation(r)) + "\n";
                t += "r^-1 condition: " + FunctionSolver::inverseCondition(cond) + "\n";
                t += std::string("Is a function: ") + (FunctionSolver::isFunction(r) ? "yes" : "no") + "\n";
                t += std::string("One-to-one:   ") + (FunctionSolver::isOneToOne(r) ? "yes" : "no");
                return t;
            }, {{"1,2,3,4", "1,2,3,4,5,6,7", "y=2x+1"}, {"1,2,3", "1,2,3", "x<y"}}));
        c.features.push_back(MkF("Analyze Relation Equation", "Vertical line test, domain, range, solved form.",
            {TF("Equation", "e.g. x^2 + y^2 = 25   or   y = 1/(x+1)")},
            [](Feature& f) -> std::string {
                RelationReport r = g_fs.analyzeRelation(ApplyParamsKeep(Req(f, 0), "xy"));
                std::string t = std::string("Function (vertical line test): ") + (r.isFunction ? "yes" : "no");
                if (!r.isFunction) t += "\n  e.g. x = " + Fmt(r.counterExampleX) + " gives y in " + FunctionSolver::toString(r.counterExampleY);
                t += "\nStandard form: " + r.standardForm;
                if (!r.solvedForm.empty()) t += "\nSolved for y:  " + r.solvedForm;
                if (r.explicitForm) t += "\ny = " + r.explicitExpr;
                t += "\nDomain = " + FunctionSolver::toString(r.domain);
                t += "\nRange  = " + FunctionSolver::toString(r.range);
                return t;
            }, {{"x^2+y^2=25"}, {"y=1/(x+1)"}, {"y^2=x"}}));
        c.features.push_back(MkF("Analyze Function f(x)", "Domain, range, increasing/decreasing, one-to-one. Any variable letter a-z / A-Z.",
            {TF("f(x)", "e.g. sqrt(x-2)   1/(x+1)   or   sqrt(t-2)"), TF("Variable (blank = auto)", "x, t, a ...")},
            [](Feature& f) -> std::string { Prepared p = PV(f, 0, 1); return g_fs.describeFunction(p.sub, p.var); },
            {{"sqrt(x-2)", ""}, {"1/(x+1)", ""}, {"x^2", ""}, {"sqrt(t-2)", ""}}));
        {
            Feature f = MkF("Algebra of Functions  (N functions)",
                "Combine any number of functions with one operation, left to right. Use [+ Add Function] for more.",
                {CF("Operation", {"f1 + f2 + ...", "f1 - f2 - ...", "f1 * f2 * ...", "f1 / f2 / ..."}),
                 TF("Function 1", "e.g. x^2"), TF("Function 2", "e.g. x - 1")},
                [](Feature& f) -> std::string {
                    std::vector<std::string> fs;
                    for (size_t i = 1; i < f.fields.size(); ++i) { std::string s = ReplaceAns(Trim(f.fields[i].value)); if (!s.empty()) fs.push_back(PrepVar(s).sub); }
                    if (fs.size() < 2) throw std::runtime_error("Please enter at least 2 functions");
                    int op = Pick(f, 0);
                    const char* sym[] = {" + ", " - ", " * ", " / "};
                    std::string cur = fs[0], label = "f1", t;
                    for (size_t k = 1; k < fs.size(); ++k) {
                        const std::string& g = fs[k];
                        cur = op == 0 ? g_fs.add(cur, g) : op == 1 ? g_fs.subtract(cur, g) : op == 2 ? g_fs.multiply(cur, g) : g_fs.divide(cur, g);
                        label += sym[op] + ("f" + std::to_string(k + 1));
                        t += "Step " + std::to_string(k) + ":  " + label + "  =  " + cur + "\n";
                    }
                    t += "\nResult  h(x) = " + cur + "\n";
                    try { t += "Domain = " + FunctionSolver::toString(g_fs.domain(cur)) + "\n"; } catch (...) {}
                    try { t += "Range  = " + FunctionSolver::toString(g_fs.range(cur)) + "\n"; } catch (...) {}
                    return t;
                }, {{"0", "x^2", "x-1"}, {"2", "x", "x+1", "x-2"}, {"3", "x^2-1", "x-1"}});
            f.dynLabel = "Function"; f.dynStart = 1; f.dynMin = 2; f.dynMax = 8;
            c.features.push_back(f);
        }
        {
            Feature f = MkF("Composite Functions  (N functions)",
                "Compose any number of functions. Use [+ Add Function] for more. Both orders are shown.",
                {CF("Order shown first", {"f1 applied first:  fn(...f2(f1(x)))", "fn applied first:  f1(f2(...fn(x)))"}),
                 TF("Function 1", "e.g. 2x + 1"), TF("Function 2", "e.g. x^2")},
                [](Feature& f) -> std::string {
                    std::vector<std::string> fs;
                    for (size_t i = 1; i < f.fields.size(); ++i) { std::string s = ReplaceAns(Trim(f.fields[i].value)); if (!s.empty()) fs.push_back(PrepVar(s).sub); }
                    if (fs.size() < 2) throw std::runtime_error("Please enter at least 2 functions");
                    int n = (int)fs.size();
                    auto build = [&](bool firstApplied, std::string& steps) -> std::string {
                        std::string cur, name;
                        if (firstApplied) {                       // fn( ... f2( f1(x) ) )
                            cur = fs[0]; name = "f1(x)";
                            for (int k = 1; k < n; ++k) {
                                cur = g_fs.compose(fs[k], cur);
                                name = "f" + std::to_string(k + 1) + "(" + name + ")";
                                steps += "  " + name + "  =  " + cur + "\n";
                            }
                        } else {                                  // f1( f2( ... fn(x) ) )
                            cur = fs[n - 1]; name = "f" + std::to_string(n) + "(x)";
                            for (int k = n - 2; k >= 0; --k) {
                                cur = g_fs.compose(fs[k], cur);
                                name = "f" + std::to_string(k + 1) + "(" + name + ")";
                                steps += "  " + name + "  =  " + cur + "\n";
                            }
                        }
                        return cur;
                    };
                    std::string t;
                    for (int pass = 0; pass < 2; ++pass) {
                        bool firstApplied = (Pick(f, 0) == 0) == (pass == 0);
                        std::string steps;
                        std::string cur = build(firstApplied, steps);
                        t += std::string(pass == 0 ? "=== " : "\n=== ") + (firstApplied ? "f1 applied first" : "fn applied first") + " ===\n" + steps;
                        t += "Result = " + cur + "\n";
                        try { t += "Domain = " + FunctionSolver::toString(g_fs.domain(cur)) + "\n"; } catch (...) {}
                    }
                    return t;
                }, {{"0", "2x+1", "x^2"}, {"0", "x+1", "x^2", "sqrt(x)"}, {"1", "x-1", "2x", "x^3"}});
            f.dynLabel = "Function"; f.dynStart = 1; f.dynMin = 2; f.dynMax = 8;
            c.features.push_back(f);
        }
        c.features.push_back(MkF("Inverse Function  f^-1", "Formula (linear f) and numeric f^-1(y0). Any variable letter a-z / A-Z.",
            {TF("f(x)", "e.g. 3x - 6   or   3t - 6"), TF("y0  (optional, find f^-1(y0))", "e.g. 9"), TF("Variable (blank = auto)", "x, t, a ...")},
            [](Feature& f) -> std::string {
                Prepared p = PV(f, 0, 2);
                const std::string& e = p.sub; char v = p.var;
                std::string t = std::string("f is one-to-one: ") + (g_fs.isOneToOne(e, v) ? "yes" : "NO (no inverse function)") + "\n";
                std::string inv = g_fs.inverseExpression(e, v);
                t += inv.empty() ? "f is not linear: no closed formula, use f^-1(y0) below.\n" : "f^-1(x) = " + inv + "\n";
                if (!Opt(f, 1).empty()) { double y0 = Num(f, 1); double r = g_fs.inverseAt(e, y0, v); SetAns(r); t += "f^-1(" + Fmt(y0) + ") = " + Fmt(r); }
                return t;
            }, {{"3x-6", "9", ""}, {"x^3", "8", ""}, {"3t-6", "9", ""}}));
        c.features.push_back(MkF("Function Type on A -> B  (Function / Onto / 1-1)", "Check a relation or f(x) between two sets / intervals.",
            {CF("Mode", {"Finite sets + relation condition", "f(x) on real intervals"}),
             TF("A", "e.g. 1,2,3   or   [0,inf)"), TF("B", "e.g. 3,5,7   or   [0,inf)"), TF("Condition  /  f(x)", "e.g. y=2x+1   or   x^2"),
             TF("Variable (intervals mode)", "x, t, a ... (blank = auto)")},
            [](Feature& f) -> std::string {
                if (Pick(f, 0) == 0) {
                    auto A = ParseSet(Req(f, 1)), B = ParseSet(Req(f, 2));
                    auto r = g_fs.relationFromCondition(A, B, ApplyParamsKeep(Req(f, 3), "xy"));
                    return "r = " + FunctionSolver::toString(r) +
                           std::string("\nFunction A -> B: ") + (FunctionSolver::isFunctionFromAToB(r, A, B) ? "yes" : "no") +
                           "\nOnto B:          " + (FunctionSolver::isOntoB(r, B) ? "yes" : "no") +
                           "\nOne-to-one:      " + (FunctionSolver::isOneToOne(r) ? "yes" : "no");
                }
                Interval A = ParseInterval(Req(f, 1)), B = ParseInterval(Req(f, 2));
                Prepared p = PV(f, 3, 4);
                return std::string("f: A -> B is a function: ") + (g_fs.isFunctionFromTo(p.sub, A, B, p.var) ? "yes" : "no") +
                       "\nOnto B:     " + (g_fs.isOnto(p.sub, A, B, p.var) ? "yes" : "no") +
                       "\nOne-to-one: " + (g_fs.isOneToOneOn(p.sub, A, p.var) ? "yes" : "no");
            }, {{"0", "1,2,3", "3,5,7,9", "y=2x+1", ""}, {"1", "[0,inf)", "[0,inf)", "x^2", ""}, {"1", "[0,inf)", "[0,inf)", "t^2", "t"}}));
        {
            Feature f = MkF("Function Notation  f(x+h), f(a+1), ...", "Define functions, then expand an expression or solve an equation using them.",
                {CF("Mode", {"Expand / evaluate expression", "Solve equation"}),
                 TF("Expression / Equation", "e.g. (f(x+h)-f(x))/h     or     f(t+1) = 10"),
                 TF("Values (optional)", "e.g. a=2, h=1"),
                 TF("Definition 1", "e.g. f(x) = x^2 + 1")},
                [](Feature& f) -> std::string {
                    std::vector<std::string> defs;
                    for (size_t i = 3; i < f.fields.size(); ++i) { std::string s = Trim(f.fields[i].value); if (!s.empty()) defs.push_back(s); }
                    if (defs.empty()) throw std::runtime_error("Please enter at least one definition");
                    std::map<char, double> vals = ParamMap();                      // ค่าจากช่อง Vars + ค่าในฟอร์ม
                    if (!Opt(f, 2).empty()) for (auto& kv : ParseGuess(Opt(f, 2))) vals[kv.first] = kv.second;
                    std::string q = Req(f, 1);
                    if (Pick(f, 0) == 0) {
                        CallResult r = g_fs.evaluateCall(defs, q, vals);
                        std::string t = "Substituted: " + r.substituted + "\nSimplified:  " + r.simplified;
                        if (r.hasValue) { t += "\nValue = " + Fmt(r.value); SetAns(r.value); }
                        return t;
                    }
                    CallEquationResult r = g_fs.solveCallEquation(defs, q, '\0', vals);
                    std::string t = "Equation: " + r.equation + "\nUnknown: " + std::string(1, r.unknown);
                    if (r.identity) t += "\n(true for every value)";
                    if (!r.factored.empty()) t += "\nFactored: " + r.factored;
                    if (!r.roots.empty()) { t += "\nRoots:"; for (auto& z : r.roots) t += "   " + FmtComplex(z); }
                    t += "\nReal roots:";
                    for (double d : r.realRoots) t += "   " + Fmt(d);
                    if (!r.realRoots.empty()) SetAns(r.realRoots[0]);
                    return t;
                }, {{"0", "(f(x+h)-f(x))/h", "", "f(x)=x^2+1"}, {"1", "f(t+1)=10", "", "f(x)=x^2+1"}});
            f.dynLabel = "Definition"; f.dynStart = 3; f.dynMin = 1; f.dynMax = 5;
            c.features.push_back(f);
        }
        g_cats.push_back(c);
    }
    // ===================== [8] Mathematical Logic =====================
    {
        Category c{"Mathematical Logic", "Truth tables, tautology, equivalence, arguments, proofs, quantifiers", {}};
        auto L = [](Feature f) { f.noWrap = true; return f; };
        const char* FH = "e.g. (p -> q) & (q -> r)";
        c.features.push_back(L(MkF("Truth Table", "Build the truth table of any formula (up to 16 variables). Connectives: ~ & | xor -> <-> nand nor.",
            {TF("Formula", FH), CF("Columns", {"All sub-formulas", "Variables + result only"}), CF("Values", {"T / F", "1 / 0"})},
            [](Feature& f) -> std::string {
                std::string fm = LReq(f, 0);
                auto t = g_logic.truthTable(fm, Pick(f, 1) == 0);
                std::string head = "Formula: " + MathLogic::toString(g_logic.parse(fm)) + "\n" + std::to_string(t.vars.size()) + " variable(s), " +
                                   std::to_string(t.values.size()) + " row(s)\n\n";
                auto cl = g_logic.classify(fm);
                std::string verdict = cl.kind == MathLogic::Verdict::Tautology ? "TAUTOLOGY (all rows true)"
                                    : cl.kind == MathLogic::Verdict::Contradiction ? "CONTRADICTION (all rows false)" : "CONTINGENCY (some true, some false)";
                return head + MathLogic::formatTruthTable(t, Pick(f, 2) == 0) + "\nResult: " + verdict;
            }, {{"(p -> q) & (q -> r)", "0", "0"}, {"~(p & q) <-> (~p | ~q)", "0", "0"}, {"p xor q", "1", "1"}, {"(p | q) & ~(p & q)", "0", "0"}})));
        c.features.push_back(L(MkF("Tautology / Contradiction / Contingency", "Classify a formula; shows a model, a counter-model, minterms and maxterms.",
            {TF("Formula", "e.g. p | ~p   or   (p & q) -> p")},
            [](Feature& f) -> std::string { return g_logic.classifyReport(LReq(f, 0)); },
            {{"p | ~p"}, {"p & ~p"}, {"(p -> q) & (q -> r)"}, {"((p -> q) & (q -> r)) -> (p -> r)"}})));
        c.features.push_back(L(MkF("Logical Equivalence & Implication", "Compare two formulas; names the law when one applies (De Morgan, contrapositive, ...).",
            {TF("Formula A", "e.g. ~(p & q)"), TF("Formula B", "e.g. ~p | ~q")},
            [](Feature& f) -> std::string { return g_logic.equivalenceReport(LReq(f, 0), LReq(f, 1)); },
            {{"~(p & q)", "~p | ~q"}, {"p -> q", "~q -> ~p"}, {"p -> q", "q -> p"}, {"p <-> q", "(p -> q) & (q -> p)"}})));
        {
            Feature f = MkF("Argument Validity (premises => conclusion)",
                "Checks validity by critical rows. Detects the rule (MP, MT, HS, DS, ...) or a fallacy. Use [+ Add Premise] for more.",
                {TF("Conclusion", "e.g. r"), TF("Premise 1", "e.g. p -> q"), TF("Premise 2", "e.g. q -> r")},
                [](Feature& f) -> std::string {
                    std::vector<std::string> prem;
                    for (size_t i = 1; i < f.fields.size(); ++i) { std::string s = LOpt(f, (int)i); if (!s.empty()) prem.push_back(s); }
                    return g_logic.argumentReport(prem, LReq(f, 0));
                }, {{"q", "p -> q", "p"}, {"p", "p -> q", "q"}, {"~p", "p -> q", "~q"}, {"r", "p -> q", "q -> r", "p"}, {"q | s", "(p -> q) & (r -> s)", "p | r"}});
            f.noWrap = true; f.dynLabel = "Premise"; f.dynStart = 1; f.dynMin = 1; f.dynMax = 8;
            c.features.push_back(f);
        }
        {
            Feature f = MkF("Proof by Inference Rules (natural deduction)",
                "Derives the goal from the premises with MP, MT, HS, DS, Simplification, Conjunction, Addition, Resolution and conditional proof.",
                {TF("Goal", "e.g. r"), TF("Premise 1", "e.g. p -> q"), TF("Premise 2", "e.g. q -> r"), TF("Premise 3", "e.g. p")},
                [](Feature& f) -> std::string {
                    std::vector<std::string> prem;
                    for (size_t i = 1; i < f.fields.size(); ++i) { std::string s = LOpt(f, (int)i); if (!s.empty()) prem.push_back(s); }
                    return g_logic.proveReport(prem, LReq(f, 0));
                }, {{"r", "p -> q", "q -> r", "p"}, {"p -> r", "p -> q", "q -> r"}, {"r & q", "p | q", "~p", "q -> r"}, {"~q -> ~p", "p -> q"}});
            f.noWrap = true; f.dynLabel = "Premise"; f.dynStart = 1; f.dynMin = 1; f.dynMax = 8;
            c.features.push_back(f);
        }
        c.features.push_back(L(MkF("Normal Forms & Simplification (Boolean algebra)", "NNF, canonical DNF/CNF (minterms/maxterms) and minimal DNF/CNF by Quine-McCluskey.",
            {TF("Formula", "e.g. (p -> q) & (q -> r)")},
            [](Feature& f) -> std::string { return g_logic.normalFormsReport(LReq(f, 0)); },
            {{"(p -> q) & (q -> r)"}, {"p xor q"}, {"(p & q) | (p & ~q) | (~p & q)"}, {"p <-> q"}})));
        c.features.push_back(L(MkF("Conditional: Converse, Inverse, Contrapositive", "Enter p -> q to get its related conditionals and which ones are equivalent.",
            {TF("Conditional  p -> q", "e.g. rain -> wet")},
            [](Feature& f) -> std::string { return g_logic.conditionalReport(LReq(f, 0)); },
            {{"p -> q"}, {"rain -> wet"}, {"(a & b) -> c"}})));
        c.features.push_back(L(MkF("Evaluate with Truth Values (step by step)", "Give p=T, q=F, ... and see each sub-formula evaluated.",
            {TF("Formula", FH), TF("Truth values", "p=T, q=F, r=T")},
            [](Feature& f) -> std::string { return g_logic.stepsReport(LReq(f, 0), MathLogic::parseAssignment(LReq(f, 1))); },
            {{"(p -> q) & ~r", "p=T, q=F, r=T"}, {"(p | q) -> (p & q)", "p=T, q=F"}})));
        c.features.push_back(L(MkF("Quantifiers over a Finite Universe (forall / exists)", "Open sentences with comparisons. Universe: {1,2,3} or 1..10 (or 0..1 step 0.5). Shows witness / counterexample and the negation.",
            {TF("Statement", "e.g. forall x in {1,2,3}: x^2 > 1"), TF("Truth values (optional)", "p=T, q=F")},
            [](Feature& f) -> std::string {
                std::map<std::string, bool> a;
                if (!LOpt(f, 1).empty()) a = MathLogic::parseAssignment(LOpt(f, 1));
                return g_logic.quantifierReport(LReq(f, 0), a);
            }, {{"forall x in {1,2,3}: x^2 > 1", ""}, {"exists x in 1..10: x^2 = 49", ""}, {"forall x in 1..5: exists y in 1..5: x + y = 6", ""},
                {"forall x in {2,4,6}: x > 1 & x < 7", ""}, {"~(exists x in {1,2,3}: x > 5)", ""}})));
        c.features.push_back(L(MkF("Reference: connectives, laws, rules, overview", "Truth rules of the connectives, equivalence laws, rules of inference & fallacies, and an overview of mathematical logic.",
            {CF("Topic", {"Connectives & truth rules", "Equivalence laws", "Rules of inference & fallacies", "Overview: mathematical logic"})},
            [](Feature& f) -> std::string {
                switch (Pick(f, 0)) {
                    case 0: return MathLogic::connectivesText();
                    case 1: return MathLogic::lawsText();
                    case 2: return MathLogic::rulesText();
                    default: return MathLogic::overviewText();
                }
            })));
        g_cats.push_back(c);
    }
    // ===================== [9] Advanced Logic (University) =====================
    {
        Category c{"Advanced Logic (University)", "First-order logic, models, tableaux, resolution, computability, Goedel", {}};
        auto L = [](Feature f) { f.noWrap = true; return f; };
        c.features.push_back(L(MkF("First-Order Formula Analyzer", "Well-formedness, language, free / bound variables, sentence?, complexity comp(phi), subformulas, terms.",
            {TF("Formula", "e.g. forall x (P(x) -> exists y R(x,y))")},
            [](Feature& f) -> std::string { return AdvancedLogic::analyzeFormula(LReq(f, 0)); },
            {{"forall x (P(x) -> exists y R(x,y)) & Q(a)"}, {"Rxy | forall x Px"}, {"forall x (x = a -> P(f(x), b))"}, {"exists x forall y (R(x,y) <-> ~R(y,y))"}})));
        c.features.push_back(L(MkF("Model Checker  (M |= phi)", "Evaluate a formula in a finite model with Tarski's clauses; the outer quantifiers are unrolled over the domain.",
            {TF("Formula", "e.g. forall x exists y R(x,y)"), TF("Domain", "e.g. m1, m2, m3   or   1..4"), TF("Constants", "a=m1, b=m2"),
             TF("Relations", "P={m1}; R={(m1,m1),(m1,m2)}"), TF("Functions", "f(m1)=m2, f(m2)=m1"), TF("Assignment (free variables)", "x=m1, y=m2")},
            [](Feature& f) -> std::string { return AdvancedLogic::modelCheckReport(LReq(f, 0), MTxt(f, 1), LOpt(f, 5)); },
            {{"forall x exists y R(x,y)", "m1, m2", "", "R={(m1,m1),(m1,m2),(m2,m2)}", "", ""},
             {"forall x (P(x) -> R(x,x)) & exists x ~P(x)", "m1, m2, m3", "", "P={m1,m2}; R={(m1,m1),(m2,m2)}", "", ""},
             {"P(x) & R(x,a)", "m1, m2", "a=m1", "P={m1}; R={(m1,m1),(m1,m2)}", "", "x=m1"},
             {"forall x exists y f(x) = y & P(f(a))", "1, 2", "a=1", "P={2}", "f(1)=2, f(2)=1", ""}})));
        {
            Feature f = MkF("Finite Model Finder  (satisfiable / valid / consequence / counting)",
                "Searches all interpretations on domains of size 1..N (N up to 8, at most 2^21 interpretations per size). Premises are optional.",
                {CF("Question", {"Satisfiable? (find a model)", "Valid? (find a counter-model)", "Premises |= conclusion?", "Count models per size"}),
                 TF("Sentence / conclusion", "e.g. forall x P(x) | forall x Q(x)"), TF("Max domain size", "3", "3"), TF("Premise 1", "e.g. forall x (P(x) | Q(x))")},
                [](Feature& f) -> std::string {
                    std::vector<std::string> prem;
                    for (size_t i = 3; i < f.fields.size(); ++i) { std::string s = LOpt(f, (int)i); if (!s.empty()) prem.push_back(s); }
                    int n = LOpt(f, 2).empty() ? 3 : atoi(LOpt(f, 2).c_str());
                    static const AdvancedLogic::FinderMode modes[] = {AdvancedLogic::FinderMode::Satisfiable, AdvancedLogic::FinderMode::Validity,
                                                                      AdvancedLogic::FinderMode::Consequence, AdvancedLogic::FinderMode::CountModels};
                    return AdvancedLogic::findModelsReport(modes[Pick(f, 0)], LReq(f, 1), prem, n);
                },
                {{"2", "forall x P(x) | forall x Q(x)", "3", "forall x (P(x) | Q(x))"}, {"1", "forall x P(x) -> exists x P(x)", "3", ""},
                 {"0", "forall x exists y R(x,y) & forall x ~R(x,x)", "3", ""}, {"3", "forall x (P(x) | ~P(x))", "3", ""},
                 {"0", "exists x exists y ~(x = y) & forall x forall y forall z (x = y | x = z | y = z)", "3", ""}});
            f.noWrap = true; f.dynLabel = "Premise"; f.dynStart = 3; f.dynMin = 1; f.dynMax = 8;
            c.features.push_back(f);
        }
        {
            Feature f = MkF("Tableau Prover  (first-order, with counter-models)",
                "Proves premises |= conclusion by closing all branches; an open complete branch yields a counter-model. Leave the conclusion empty to test consistency.",
                {TF("Conclusion", "e.g. forall x P(x) -> ~exists x ~P(x)"), TF("Premise 1", "e.g. forall x (P(x) -> Q(x))")},
                [](Feature& f) -> std::string {
                    std::vector<std::string> prem;
                    for (size_t i = 1; i < f.fields.size(); ++i) { std::string s = LOpt(f, (int)i); if (!s.empty()) prem.push_back(s); }
                    return AdvancedLogic::tableauReport(prem, LOpt(f, 0));
                },
                {{"forall x P(x) -> ~exists x ~P(x)", ""}, {"Q(a)", "forall x (P(x) -> Q(x))", "P(a)"}, {"forall x P(x) | forall x Q(x)", "forall x (P(x) | Q(x))"},
                 {"exists y forall x R(x,y)", "forall x exists y R(x,y)"}, {"exists x (P(x) -> forall y P(y))", ""}});
            f.noWrap = true; f.dynLabel = "Premise"; f.dynStart = 1; f.dynMin = 1; f.dynMax = 8;
            c.features.push_back(f);
        }
        {
            Feature f = MkF("Resolution Prover  (clauses, unification)",
                "Refutation proof: Skolemise premises + negated conclusion, then resolve with most general unifiers until [] appears.",
                {TF("Conclusion", "e.g. Mortal(socrates)"), TF("Premise 1", "e.g. forall x (Man(x) -> Mortal(x))"), TF("Premise 2", "e.g. Man(socrates)")},
                [](Feature& f) -> std::string {
                    std::vector<std::string> prem;
                    for (size_t i = 1; i < f.fields.size(); ++i) { std::string s = LOpt(f, (int)i); if (!s.empty()) prem.push_back(s); }
                    return AdvancedLogic::resolutionReport(prem, LOpt(f, 0));
                },
                {{"Mortal(socrates)", "forall x (Man(x) -> Mortal(x))", "Man(socrates)"}, {"forall x exists y R(y,x)", "forall x exists y R(x,y)", "forall x forall y (R(x,y) -> R(y,x))"},
                 {"forall x P(x) | forall x Q(x)", "forall x (P(x) | Q(x))", ""}, {"exists x (P(x) -> forall y P(y))", "", ""}});
            f.noWrap = true; f.dynLabel = "Premise"; f.dynStart = 1; f.dynMin = 1; f.dynMax = 8;
            c.features.push_back(f);
        }
        c.features.push_back(L(MkF("Normal Forms: NNF, Prenex, Skolem, Clauses", "Negation normal form, prenex form (variables renamed apart), Skolem normal form and clause set of a first-order formula.",
            {TF("Formula", "e.g. forall x (P(x) -> exists y (R(x,y) & ~Q(y)))")},
            [](Feature& f) -> std::string { return AdvancedLogic::normalFormReport(LReq(f, 0)); },
            {{"forall x (P(x) -> exists y (R(x,y) & ~Q(y)))"}, {"~(forall x P(x) -> exists y Q(y))"}, {"(exists x P(x)) & (forall y exists z R(y,z))"}})));
        c.features.push_back(L(MkF("Unification  (most general unifier)", "Robinson's algorithm with occurs check for two terms or two atoms. Same variable name = same variable.",
            {TF("Term / atom 1", "e.g. P(x, f(y))"), TF("Term / atom 2", "e.g. P(a, f(z))")},
            [](Feature& f) -> std::string { return AdvancedLogic::unifyReport(LReq(f, 0), LReq(f, 1)); },
            {{"P(x, f(y))", "P(a, f(z))"}, {"f(x, g(y))", "f(g(z), w)"}, {"x", "f(x)"}, {"f(x)", "g(a)"}})));
        c.features.push_back(L(MkF("Models: Isomorphism, Submodels, Preservation", "Compare two finite models: isomorphic? is N a submodel of M? or check a sentence in both (Sigma_1 / Pi_1 preservation).",
            {CF("Question", {"M and N isomorphic?", "N a submodel of M?", "Sentence in M and N (preservation)"}), TF("Sentence (3rd question)", "e.g. exists x R(x,x)"),
             TF("M: Domain", "1, 2, 3"), TF("M: Constants", "a=1"), TF("M: Relations", "R={(1,2),(2,3)}"), TF("M: Functions", ""),
             TF("N: Domain", "a, b, c"), TF("N: Constants", ""), TF("N: Relations", "R={(c,a),(a,b)}"), TF("N: Functions", "")},
            [](Feature& f) -> std::string {
                return AdvancedLogic::modelRelationReport(Pick(f, 0), MTxt(f, 2), MTxt(f, 6), LOpt(f, 1));
            },
            {{"0", "", "1, 2, 3", "", "R={(1,2),(2,3)}", "", "a, b, c", "", "R={(c,a),(a,b)}", ""},
             {"1", "", "1, 2, 3", "", "P={1,2}; R={(1,1),(2,3)}", "", "1, 2", "", "P={1,2}; R={(1,1)}", ""},
             {"2", "exists x exists y (P(x) & R(x,y))", "1, 2, 3", "", "P={1,2}; R={(1,1),(2,3)}", "", "1, 2", "", "P={1,2}; R={(1,1)}", ""},
             {"2", "forall x forall y (R(x,y) -> P(x))", "1, 2, 3", "", "P={1,2}; R={(1,1),(2,3)}", "", "1, 2", "", "P={1,2}; R={(1,1)}", ""}})));
        c.features.push_back(L(MkF("Cantor: Diagonal Argument & Power Set", "Build the diagonal sequence d that is missing from a list; and the diagonal set D = {x : x not in f(x)} for f : S -> P(S).",
            {TF("Rows (H/T or 0/1)", "HHTHT, THTHT, TTTHH, HTHTT, TTTTH"), TF("Set | map f(x)", "a, b, c | a:{a,b}, b:{}, c:{a,c}")},
            [](Feature& f) -> std::string { return AdvancedLogic::cantorReport(LOpt(f, 0), LOpt(f, 1)); },
            {{"HHTHT, THTHT, TTTHH, HTHTT, TTTTH", "a, b, c | a:{a,b}, b:{}, c:{a,c}"}, {"0110, 1010, 0001, 1111", ""}, {"", "x, y, z, w"}})));
        c.features.push_back(L(MkF("Turing Machine Simulator", "Rules  state read -> newstate write move  (move L R N), separated by ';'. Halts when no rule applies; step limit protects against non-halting machines.",
            {TF("Rules", "q0 1 -> q0 1 R; q0 _ -> q1 1 N"), TF("Input tape", "111"), TF("Start state", "q0", "q0"), TF("Blank symbol", "_", "_"), TF("Max steps", "1000", "1000")},
            [](Feature& f) -> std::string {
                int steps = LOpt(f, 4).empty() ? 1000 : atoi(LOpt(f, 4).c_str());
                return AdvancedLogic::turingReport(LReq(f, 0), LOpt(f, 1), LReq(f, 2), LOpt(f, 3), steps);
            },
            {{"q0 1 -> q0 1 R; q0 _ -> q1 1 N", "111", "q0", "_", "1000"},
             {"b 0 -> b 0 R; b 1 -> b 1 R; b _ -> c _ L; c 1 -> c 0 L; c 0 -> h 1 N; c _ -> h 1 N", "1011", "b", "_", "1000"},
             {"A 0 -> B 1 R; A 1 -> B 1 L; B 0 -> A 1 L; B 1 -> H 1 N", "", "A", "0", "1000"},
             {"s 0 -> s 0 R; s 1 -> s 1 R", "101", "s", "_", "20"}})));
        c.features.push_back(L(MkF("Recursive Functions (Goedel's schema, mu-recursion)", "Evaluate expressions built from Z, S, P(i,k), C, R, M; show the library; or compute the Ackermann function.",
            {CF("Mode", {"Evaluate expression", "Show schema + library", "Ackermann A(m,n)"}), TF("Function", "add   or   R(P(1,1); C(S; P(3,3)))"), TF("Arguments", "3,4")},
            [](Feature& f) -> std::string { return AdvancedLogic::recursiveReport(Pick(f, 0), LOpt(f, 1), LOpt(f, 2)); },
            {{"0", "add", "3,4"}, {"0", "R(P(1,1); C(S; P(3,3)))", "10,20"}, {"0", "M(C(monus; P(1,2), C(mul; P(2,2), P(2,2))))", "50"},
             {"0", "C(mul; fact, fact)", "4"}, {"1", "", ""}, {"2", "", "3,3"}})));
        c.features.push_back(L(MkF("Goedel Numbering, Pairing, Sequence Coding", "Arithmetisation of syntax: encode / decode formulas as numbers, Cantor pairing pi(x,y), coding of finite sequences.",
            {CF("Mode", {"Encode a formula", "Decode a number", "Cantor pairing / unpairing", "Sequence coding"}), TF("Input", "formula, number, 'x,y' or 'a1,a2,...'")},
            [](Feature& f) -> std::string { return AdvancedLogic::godelReport(Pick(f, 0), LReq(f, 1)); },
            {{"0", "forall x (P(x) -> Q(x))"}, {"0", "~P(a)"}, {"1", "31796978159818222112714919699065011830798946688570267567791406250"}, {"2", "3,4"}, {"2", "32"}, {"3", "3,1,4"}})));
        c.features.push_back(L(MkF("Reference: overview, tableau rules, input syntax", "Summary of the advanced logic notes, the tableau rules and strategy, and the input syntax of this category.",
            {CF("Topic", {"Overview of advanced logic (notes)", "Tableau rules & strategy", "Input syntax"})},
            [](Feature& f) -> std::string {
                switch (Pick(f, 0)) {
                    case 0: return AdvancedLogic::advancedOverview();
                    case 1: return ADV_TABLEAU_TEXT;
                    default: return ADV_SYNTAX_TEXT;
                }
            })));
        g_cats.push_back(c);
    }
}

// ============================================================
//                    สถานะหน้าจอ / การนำทาง
// ============================================================
enum ScreenId { S_HOME, S_MENU, S_FORM, S_CALC, S_MATRIX, S_TABLE };
static int g_screen = S_HOME, g_cat = 0, g_feat = 0;
static Feature& CurFeature() { return g_cats[g_cat].features[g_feat]; }

static int g_mop = 0;     // การดำเนินการเมทริกซ์ปัจจุบัน
static int g_top = 0;     // การดำเนินการตารางปัจจุบัน

static void FocusFirstField(Feature& f) {
    ResetFocus();
    for (auto& fd : f.fields) if (fd.choices.empty() && fd.label != "(not used)") { g_focusStr = &fd.value; return; }
}
static void GoHome() { Defer([] { g_screen = S_HOME; ResetFocus(); }); }
static void GoBack() {
    Defer([] {
        switch (g_screen) {
            case S_FORM: case S_MATRIX: case S_TABLE: g_screen = S_MENU; break;
            case S_MENU: case S_CALC: g_screen = S_HOME; break;
            default: break;
        }
        ResetFocus();
    });
}
static void OpenCat(int c) {
    Defer([c] { g_cat = c; g_feat = 0; g_screen = (g_cats[c].features[0].kind == FK_CALC) ? S_CALC : S_MENU; ResetFocus(); });
}
static void OpenFeature(int fi) {
    Defer([fi] {
        g_feat = fi;
        Feature& f = CurFeature();
        switch (f.kind) {
            case FK_FORM:   g_screen = S_FORM; FocusFirstField(f); break;
            case FK_MATRIX: g_screen = S_MATRIX; g_mop = f.preset; ResetFocus(); break;
            case FK_TABLE:  g_screen = S_TABLE; g_top = f.preset; ResetFocus(); break;
            default: break;
        }
    });
}
static void TopBar(const std::string& title) {
    FillRectPx(0, 0, g_w, 46, RGB32(24, 26, 32));
    if (DrawButton(10, 8, 70, 30, "Home", COL_CHIP)) GoHome();
    if (DrawButton(86, 8, 84, 30, "< Back", COL_CHIP)) GoBack();
    const int ansW = 170, varW = 260;
    DrawText_(184, 23 - FontH(g_fontL) / 2, FitHead(g_fontL, title, g_w - 184 - ansW - varW - 64), COL_TEXT, g_fontL);
    DrawTextRight(g_w - 12, 23 - FontH(g_fontS) / 2, FitHead(g_fontS, "ans = " + Fmt(g_ans), ansW - 8), COL_GREEN, g_fontS);
    int vx = g_w - 12 - ansW - varW;
    DrawText_(vx - 40, 23 - FontH(g_fontM) / 2, "Vars", COL_DIM, g_fontM);
    TextBox(vx, 8, varW, 30, g_paramStr, "a=2, b=3  (any letters a-z A-Z)", g_fontM);
}
static bool ClickArea(int x, int y, int w, int h) {
    int id = NextId();
    bool over = Hit(x, y, w, h);
    if (over && ui.pressed) ui.active = id;
    return ui.released && ui.active == id && over;
}

// ============================================================
//                    หน้าแรก (7 หมวด) และเมนูย่อย
// ============================================================
static void ScreenHome() {
    DrawTextCenter(g_w / 2, 34, "MATHS SOLVER SYSTEM", COL_TEXT, g_fontL);
    DrawTextCenter(g_w / 2, 62, "Click a card (or press 1-" + std::to_string(g_cats.size()) + ") to start", COL_DIM, g_fontS);
    int cols = 2, gap = 14, x0 = 20, y0 = 90;
    int cw = (g_w - 2 * x0 - gap) / cols;
    int rowsN = ((int)g_cats.size() + 1 + cols - 1) / cols;
    int ch = std::clamp((g_h - y0 - 20 - (rowsN - 1) * gap) / rowsN, 56, 120);
    for (size_t i = 0; i < g_cats.size(); ++i) {
        int cx = x0 + (int)(i % cols) * (cw + gap), cy = y0 + (int)(i / cols) * (ch + gap);
        bool over = Hit(cx, cy, cw, ch);
        if (ClickArea(cx, cy, cw, ch)) OpenCat((int)i);
        FillRectPx(cx, cy, cw, ch, over ? RGB32(52, 62, 86) : RGB32(40, 46, 62));
        Border(cx, cy, cw, ch, over ? RGB32(90, 150, 255) : COL_BORDER);
        FillRectPx(cx, cy, 8, ch, over ? RGB32(90, 150, 255) : COL_BTN);
        DrawText_(cx + 22, cy + 14, "[" + std::to_string(i + 1) + "]  " + FitHead(g_fontL, g_cats[i].title, cw - 80), COL_TEXT, g_fontL);
        for (size_t k = 0, line = 0; k < g_cats[i].sub.size() && line < 2; ++line) {
            int per = std::max(10, (cw - 40) / CharW(g_fontS));
            DrawText_(cx + 22, cy + 14 + FontH(g_fontL) + 8 + (int)line * (FontH(g_fontS) + 2),
                      g_cats[i].sub.substr(k, per), COL_DIM, g_fontS);
            k += per;
        }
    }
    // การ์ดช่องที่ 8: เคล็ดลับ
    {
        size_t i = g_cats.size();
        int cx = x0 + (int)(i % cols) * (cw + gap), cy = y0 + (int)(i / cols) * (ch + gap);
        Border(cx, cy, cw, ch, COL_BORDER);
        DrawText_(cx + 14, cy + 10, "Tips", COL_GREEN, g_fontL);
        const char* tips[] = {"- Fill the boxes, press Calculate (or Enter)", "- 'Example' button fills sample values",
                              "- Tab = next box, Esc = back, wheel = scroll", "- 'ans' = your last numeric result"};
        for (int k = 0; k < 4; ++k) DrawText_(cx + 14, cy + 14 + FontH(g_fontL) + k * (FontH(g_fontM) + 3), tips[k], COL_DIM, g_fontM);
    }
    if (g_pendingChar >= '1' && g_pendingChar <= '0' + (int)g_cats.size()) OpenCat(g_pendingChar - '1');
}
static void ScreenMenu() {
    Category& cat = g_cats[g_cat];
    TopBar(cat.title);
    DrawText_(20, 58, cat.sub, COL_DIM, g_fontS);
    int x = 20, w = std::min(g_w - 40, 900), gap = 6, y = 90;
    int h = std::clamp((g_h - y - 10) / std::max(1, (int)cat.features.size()) - gap, 34, 46);
    for (size_t i = 0; i < cat.features.size(); ++i) {
        Feature& f = cat.features[i];
        bool over = Hit(x, y, w, h);
        if (ClickArea(x, y, w, h)) OpenFeature((int)i);
        FillRectPx(x, y, w, h, over ? RGB32(52, 62, 86) : RGB32(40, 46, 62));
        Border(x, y, w, h, over ? RGB32(90, 150, 255) : COL_BORDER);
        FillRectPx(x, y, 40, h, over ? RGB32(90, 150, 255) : COL_BTN);
        DrawTextCenter(x + 20, y + h / 2, std::to_string(i + 1), RGB32(255, 255, 255), g_fontS);
        DrawText_(x + 52, y + 7, FitHead(g_fontS, f.title, w - 64), COL_TEXT, g_fontS);
        DrawText_(x + 52, y + 7 + FontH(g_fontS) + 2, FitHead(g_fontM, f.desc, w - 64), COL_DIM, g_fontM);
        y += h + gap;
    }
    if (g_pendingChar >= '1' && g_pendingChar <= '9' && (size_t)(g_pendingChar - '1') < cat.features.size())
        OpenFeature(g_pendingChar - '1');
}

// ============================================================
//                    [1] Algebra Calculator
// ============================================================
struct Hist { std::string expr, res; };
static std::vector<Hist> g_hist;
static int g_histScroll = 0;
static std::string g_cExpr, g_cResult, g_cErr;
static bool g_cFresh = false;

static void CalcInsert(const std::string& s) {
    g_cErr.clear();
    if (g_cFresh) {
        bool isOp = s.size() == 1 && strchr("+-*/^%", s[0]);
        g_cExpr = isOp ? "ans" + s : s;      // พิมพ์เครื่องหมายต่อจากคำตอบเดิมได้เลย
        g_cFresh = false;
        return;
    }
    g_cExpr += s;
}
static void CalcDelete() { if (g_cFresh) { g_cExpr.clear(); g_cResult.clear(); g_cFresh = false; } else if (!g_cExpr.empty()) g_cExpr.pop_back(); g_cErr.clear(); }
static void CalcClear() { g_cExpr.clear(); g_cResult.clear(); g_cErr.clear(); g_cFresh = false; }
static void CalcEquals() {
    std::string e = Trim(g_cExpr);
    if (e.empty()) return;
    g_cErr.clear();
    try {
        g_calc.solver().setAns(g_ans);
        size_t as = e.find(":=");
        if (as != std::string::npos) {                        // กำหนดค่าตัวแปร a-z / A-Z:   a := 5
            std::string name = Trim(e.substr(0, as));
            if (name.size() != 1 || !isalpha((unsigned char)name[0]) || name[0] == 'e')
                throw std::runtime_error("Assign like   a := 5   (one letter a-z / A-Z, not e)");
            double v = EvalNum(e.substr(as + 2));
            SetParam(name[0], v);
            g_cResult = name + " = " + Fmt(v); g_cFresh = true;
            g_hist.push_back({e, g_cResult}); g_histScroll = 0;
            return;
        }
        std::string full = ReplaceAns(e), res;
        double v;
        if (full.find('=') != std::string::npos) {            // สมการ: ตัวแปรใดก็ได้ (a-z, A-Z)
            Prepared p = PrepVar(full);
            v = g_calc.solver().solveTwoSideEquation(g_calc, p.sub, 1.0, 1000, 1e-7, p.var);
            res = std::string(1, p.var) + " = " + Fmt(v);
        } else {
            v = EvalNum(e);
            res = Fmt(v);
        }
        SetAns(v);
        g_cResult = res; g_cFresh = true;
        g_hist.push_back({e, res});
        g_histScroll = 0;
    } catch (const std::exception& ex) { g_cErr = ex.what(); g_cResult.clear(); }
}
static void CalcStore() { g_calc.setVariable('x', g_ans); g_cErr.clear(); g_cResult = "x = " + Fmt(g_ans); g_cFresh = true; }

struct CKey { const char* label; const char* ins; int kind; uint32_t col; };   // kind: 0 ins, 1 AC, 2 DEL, 3 =, 4 STO
static const CKey CALC_KEYS[45] = {
    {"AC","",1,COL_CLEAR},{"DEL","",2,COL_CLEAR},{"(","(",0,COL_OP},{")",")",0,COL_OP},{"ans","ans",0,COL_BTN},
    {"sin","sin(",0,COL_BTN},{"cos","cos(",0,COL_BTN},{"tan","tan(",0,COL_BTN},{"^","^",0,COL_OP},{"sqrt","sqrt(",0,COL_BTN},
    {"asin","asin(",0,COL_BTN},{"acos","acos(",0,COL_BTN},{"atan","atan(",0,COL_BTN},{"ln","ln(",0,COL_BTN},{"log","log(",0,COL_BTN},
    {"exp","exp(",0,COL_BTN},{"abs","abs(",0,COL_BTN},{"fact","fact(",0,COL_BTN},{"pi","pi",0,COL_BTN},{"e","e",0,COL_BTN},
    {"root","root(",0,COL_BTN},{"inf","inf",0,COL_BTN},{"x^2","^2",0,COL_OP},{"1/x","1/(",0,COL_OP},{":=",":=",0,COL_OP},
    {"7","7",0,COL_NUM},{"8","8",0,COL_NUM},{"9","9",0,COL_NUM},{"/","/",0,COL_OP},{"x","x",0,COL_BTN},
    {"4","4",0,COL_NUM},{"5","5",0,COL_NUM},{"6","6",0,COL_NUM},{"*","*",0,COL_OP},{"%","%",0,COL_OP},
    {"1","1",0,COL_NUM},{"2","2",0,COL_NUM},{"3","3",0,COL_NUM},{"-","-",0,COL_OP},{",",",",0,COL_OP},
    {"0","0",0,COL_NUM},{".",".",0,COL_NUM},{"A=B","=",0,COL_OP},{"+","+",0,COL_OP},{"=","",3,COL_OK},
};
static void ScreenCalc() {
    TopBar("[1] Algebra Calculator");
    int lx = 10, lw = std::clamp(g_w * 56 / 100, 460, 640), top = 56;
    // จอแสดงผล
    int dispH = 118;
    FillRectPx(lx, top, lw, dispH, COL_PANEL);
    Border(lx, top, lw, dispH, COL_BORDER);
    {
        int per = std::max(1, (lw - 20) / CharW(g_fontS));
        std::vector<std::string> ls;
        for (size_t i = 0; i < g_cExpr.size(); i += per) ls.push_back(g_cExpr.substr(i, per));
        if (ls.size() > 2) ls.erase(ls.begin(), ls.end() - 2);
        if (ls.empty()) ls.push_back("");
        for (size_t i = 0; i < ls.size(); ++i)
            DrawText_(lx + 10, top + 8 + (int)i * (FontH(g_fontS) + 2), ls[i] + (i + 1 == ls.size() ? "_" : ""), COL_TEXT, g_fontS);
        FillRectPx(lx + 10, top + 8 + 2 * (FontH(g_fontS) + 2) + 4, lw - 20, 1, COL_BORDER);
        int ry = top + 8 + 2 * (FontH(g_fontS) + 2) + 14;
        if (!g_cErr.empty()) {
            auto el = BuildLines(g_cErr, std::max(10, (lw - 20) / CharW(g_fontM)), true);
            for (size_t i = 0; i < el.size() && i < 3; ++i) DrawText_(lx + 10, ry + (int)i * (FontH(g_fontM) + 1), el[i], COL_RED, g_fontM);
        } else if (!g_cResult.empty()) {
            std::string r = g_cResult;
            GuiFont* f = TextW(g_fontL, r) <= lw - 24 ? g_fontL : g_fontS;
            DrawTextRight(lx + lw - 12, ry + 4, FitTail(f, r, lw - 24), COL_GREEN, f);
        }
    }
    // ปุ่ม
    int keyTop = top + dispH + 10, gap = 6;
    int bw = (lw - 4 * gap) / 5;
    int bh = std::clamp((g_h - keyTop - 10 - 8 * gap) / 9, 28, 62);
    for (int r = 0; r < 9; ++r)
        for (int c = 0; c < 5; ++c) {
            const CKey& k = CALC_KEYS[r * 5 + c];
            if (DrawButton(lx + c * (bw + gap), keyTop + r * (bh + gap), bw, bh, k.label, k.col)) {
                switch (k.kind) {
                    case 0: CalcInsert(k.ins); break;
                    case 1: CalcClear(); break;
                    case 2: CalcDelete(); break;
                    case 3: CalcEquals(); break;
                    case 4: CalcStore(); break;
                }
            }
        }
    // ประวัติ
    int hx = lx + lw + 12, hw = g_w - hx - 10, hh = g_h - top - 10;
    FillRectPx(hx, top, hw, hh, COL_PANEL);
    Border(hx, top, hw, hh, COL_BORDER);
    DrawText_(hx + 10, top + 8, "History  (click an item to reuse it)", COL_DIM, g_fontS);
    if (DrawButton(hx + hw - 78, top + 4, 70, 24, "Clear", COL_CHIP, g_fontM)) { g_hist.clear(); }
    int itemH = 50, listTop = top + 34;
    int vis = std::max(1, (hh - 40) / itemH);
    int maxScroll = std::max(0, (int)g_hist.size() - vis);
    if (Hit(hx, top, hw, hh) && g_wheel) { g_histScroll += g_wheel; g_wheel = 0; }
    g_histScroll = std::clamp(g_histScroll, 0, maxScroll);
    for (int i = 0; i < vis && i + g_histScroll < (int)g_hist.size(); ++i) {
        const Hist& h = g_hist[g_hist.size() - 1 - (i + g_histScroll)];
        int iy = listTop + i * itemH;
        bool over = Hit(hx + 4, iy, hw - 8, itemH - 4);
        if (ClickArea(hx + 4, iy, hw - 8, itemH - 4)) { g_cExpr = h.expr; g_cFresh = false; g_cErr.clear(); }
        FillRectPx(hx + 4, iy, hw - 8, itemH - 4, over ? RGB32(40, 48, 68) : RGB32(28, 31, 40));
        DrawText_(hx + 10, iy + 5, FitTail(g_fontS, h.expr, hw - 28), COL_DIM, g_fontS);
        DrawText_(hx + 10, iy + 5 + FontH(g_fontS) + 3, (h.res.find('=') == std::string::npos ? "= " : "") + FitHead(g_fontS, h.res, hw - 40), COL_GREEN, g_fontS);
    }
    if (g_hist.empty()) DrawText_(hx + 10, listTop + 4, "(no calculations yet)", COL_DIM, g_fontM);
    if (g_enter) CalcEquals();
}

// ============================================================
//                    ฟอร์มทั่วไป (หมวด 2, 3, 4, 6, 7)
// ============================================================
static void RunFeature(Feature& f) {
    f.error.clear();
    try { f.result = f.run(f); }
    catch (const std::exception& e) { f.error = e.what(); f.result.clear(); }
    f.scroll = 0;
}
static void FieldInsert(const std::string& s) {
    if (!g_focusStr) return;
    if (s == "<DEL>") { if (!g_focusStr->empty()) g_focusStr->pop_back(); }
    else if (s == "<CLR>") g_focusStr->clear();
    else *g_focusStr += s;
}
struct FKey { const char* l; const char* ins; uint32_t col; };
static const FKey FORM_KEYS[42] = {
    {"7","7",COL_NUM},{"8","8",COL_NUM},{"9","9",COL_NUM},{"/","/",COL_OP},{"(","(",COL_OP},{")",")",COL_OP},
    {"4","4",COL_NUM},{"5","5",COL_NUM},{"6","6",COL_NUM},{"*","*",COL_OP},{"^","^",COL_OP},{",",",",COL_OP},
    {"1","1",COL_NUM},{"2","2",COL_NUM},{"3","3",COL_NUM},{"-","-",COL_OP},{"x","x",COL_BTN},{"y","y",COL_BTN},
    {"0","0",COL_NUM},{".",".",COL_NUM},{"=","=",COL_OP},{"+","+",COL_OP},{"pi","pi",COL_BTN},{"e","e",COL_BTN},
    {"sin","sin(",COL_BTN},{"cos","cos(",COL_BTN},{"tan","tan(",COL_BTN},{"ln","ln(",COL_BTN},{"log","log(",COL_BTN},{"exp","exp(",COL_BTN},
    {"asin","asin(",COL_BTN},{"acos","acos(",COL_BTN},{"atan","atan(",COL_BTN},{"sqrt","sqrt(",COL_BTN},{"abs","abs(",COL_BTN},{"fact","fact(",COL_BTN},
    {"ans","ans",COL_BTN},{"inf","inf",COL_BTN},{"DEL","<DEL>",COL_CLEAR},{"CLR","<CLR>",COL_CLEAR},{"t","t",COL_BTN},{"a","a",COL_BTN},
};
static const FKey LOGIC_KEYS[42] = {
    {"p","p",COL_BTN},{"q","q",COL_BTN},{"r","r",COL_BTN},{"s","s",COL_BTN},{"t","t",COL_BTN},{"u","u",COL_BTN},
    {"~","~",COL_OP},{"&","&",COL_OP},{"|","|",COL_OP},{"->","->",COL_OP},{"<->","<->",COL_OP},{"xor"," xor ",COL_OP},
    {"(","(",COL_OP},{")",")",COL_OP},{"T","T",COL_NUM},{"F","F",COL_NUM},{"nand"," nand ",COL_OP},{"nor"," nor ",COL_OP},
    {"forall","forall ",COL_BTN},{"exists","exists ",COL_BTN},{"in"," in ",COL_BTN},{":",": ",COL_BTN},{"{","{",COL_BTN},{"}","}",COL_BTN},
    {"x","x",COL_BTN},{"y","y",COL_BTN},{"0","0",COL_NUM},{"1","1",COL_NUM},{"2","2",COL_NUM},{"3","3",COL_NUM},
    {">",">",COL_NUM},{"<","<",COL_NUM},{">=",">=",COL_NUM},{"<=","<=",COL_NUM},{"=","=",COL_NUM},{"!=","!=",COL_NUM},
    {",",",",COL_OP},{"..","..",COL_OP},{"DEL","<DEL>",COL_CLEAR},{"CLR","<CLR>",COL_CLEAR},{"space"," ",COL_OP},{"^","^",COL_OP},
};
static const FKey ADV_KEYS[42] = {
    {"P","P",COL_BTN},{"Q","Q",COL_BTN},{"R","R",COL_BTN},{"x","x",COL_BTN},{"y","y",COL_BTN},{"z","z",COL_BTN},
    {"a","a",COL_NUM},{"b","b",COL_NUM},{"f","f",COL_NUM},{"(","(",COL_OP},{")",")",COL_OP},{",",",",COL_OP},
    {"forall","forall ",COL_BTN},{"exists","exists ",COL_BTN},{"~","~",COL_OP},{"&","&",COL_OP},{"|","|",COL_OP},{"=","=",COL_OP},
    {"->","->",COL_OP},{"<->","<->",COL_OP},{"!=","!=",COL_OP},{".",".",COL_OP},{"{","{",COL_BTN},{"}","}",COL_BTN},
    {"0","0",COL_NUM},{"1","1",COL_NUM},{"2","2",COL_NUM},{"3","3",COL_NUM},{"4","4",COL_NUM},{"5","5",COL_NUM},
    {"m1","m1",COL_NUM},{"m2","m2",COL_NUM},{"m3","m3",COL_NUM},{"H","H",COL_NUM},{"T","T",COL_NUM},{"_","_",COL_NUM},
    {";",";",COL_OP},{":",":",COL_OP},{"space"," ",COL_OP},{"S","S",COL_BTN},{"DEL","<DEL>",COL_CLEAR},{"CLR","<CLR>",COL_CLEAR},
};
static void MathKeypad(int x, int bottom, int w, const FKey* keys) {
    int cols = 6, rows = 7, gap = 4, kh = 28;
    int kw = (w - (cols - 1) * gap) / cols;
    int top = bottom - (rows * kh + (rows - 1) * gap);
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            const FKey& k = keys[r * cols + c];
            if (DrawButton(x + c * (kw + gap), top + r * (kh + gap), kw, kh, k.l, k.col)) FieldInsert(k.ins);
        }
}
static void ApplyExample(Feature& f) {
    if (f.examples.empty()) return;
    auto& ex = f.examples[f.exampleIdx % f.examples.size()];
    f.exampleIdx++;
    while (!f.dynLabel.empty() && ex.size() > f.fields.size() && (int)f.fields.size() - f.dynStart < f.dynMax)
        f.fields.push_back(TF(f.dynLabel + " " + std::to_string(f.fields.size() - f.dynStart + 1), ""));
    for (size_t i = 0; i < f.fields.size(); ++i) {
        if (i < ex.size()) {
            if (!f.fields[i].choices.empty()) f.fields[i].choice = std::clamp(atoi(ex[i].c_str()), 0, (int)f.fields[i].choices.size() - 1);
            else f.fields[i].value = ex[i];
        } else if (!f.dynLabel.empty() && (int)i >= f.dynStart && f.fields[i].choices.empty()) f.fields[i].value.clear();
    }
    f.error.clear();
    ResetFocus();
}
// แถบควบคุมกราฟ: Plot / Zoom / เลื่อนซ้าย-ขวา-ขึ้น-ลง / Reset + ช่องกำหนดช่วง x, y และความละเอียด
static int GraphControls(int x, int y, int w, GView& v, const std::function<void()>& rerun, bool& plotClicked) {
    int y0 = y;
    const char* names[8] = {"Plot", "Zoom +", "Zoom -", "< Left", "Right >", "Up", "Down", "Reset"};
    int bw = (w - 7 * 4) / 8;
    auto setX = [&](double a, double b) { v.xmin = Fmt(a); v.xmax = Fmt(b); };
    auto setY = [&](double a, double b) { v.ymin = Fmt(a); v.ymax = Fmt(b); };
    bool manualY = !Trim(v.ymin).empty() && !Trim(v.ymax).empty();
    for (int i = 0; i < 8; ++i) {
        uint32_t col = i == 0 ? COL_OK : i == 7 ? COL_CLEAR : COL_BTN;
        if (!DrawButton(x + i * (bw + 4), y, bw, 30, names[i], col, g_fontM)) continue;
        double cx = (v.cx0 + v.cx1) / 2, hx = v.cx1 - v.cx0, cy = (v.cy0 + v.cy1) / 2, hy = v.cy1 - v.cy0;
        switch (i) {
            case 0: plotClicked = true; break;
            case 1: setX(cx - hx / 4, cx + hx / 4); if (manualY) setY(cy - hy / 4, cy + hy / 4); rerun(); break;
            case 2: setX(cx - hx, cx + hx);         if (manualY) setY(cy - hy, cy + hy);         rerun(); break;
            case 3: setX(v.cx0 - hx / 4, v.cx1 - hx / 4); rerun(); break;
            case 4: setX(v.cx0 + hx / 4, v.cx1 + hx / 4); rerun(); break;
            case 5: setY(v.cy0 + hy / 4, v.cy1 + hy / 4); rerun(); break;      // เลื่อนมุมมองขึ้น
            case 6: setY(v.cy0 - hy / 4, v.cy1 - hy / 4); rerun(); break;      // เลื่อนมุมมองลง
            case 7: v.xmin = v.defX0; v.xmax = v.defX1; v.ymin.clear(); v.ymax.clear(); v.w.clear(); v.h.clear(); rerun(); break;
        }
    }
    y += 34;
    int gw = (w - 3 * 6) / 4;
    const char* lab[4] = {"xMin", "xMax", "yMin", "yMax"};
    std::string* ss[4] = {&v.xmin, &v.xmax, &v.ymin, &v.ymax};
    for (int i = 0; i < 4; ++i) {
        int bx = x + i * (gw + 6);
        DrawText_(bx, y + 7, lab[i], COL_DIM, g_fontM);
        TextBox(bx + 36, y, gw - 36, 28, *ss[i], "auto", g_fontM);
    }
    y += 34;
    DrawText_(x, y + 7, "Width", COL_DIM, g_fontM);  TextBox(x + 42, y, 56, 28, v.w, "auto", g_fontM);
    DrawText_(x + 108, y + 7, "Height", COL_DIM, g_fontM); TextBox(x + 156, y, 56, 28, v.h, "auto", g_fontM);
    if (DrawButton(x + 224, y, 90, 28, "Auto Y", COL_CHIP, g_fontM)) { v.ymin.clear(); v.ymax.clear(); rerun(); }
    DrawText_(x + 324, y + 7, "(blank y = auto scale)", COL_DIM, g_fontM);
    y += 36;
    return y - y0;
}

static void ScreenForm() {
    Feature& f = CurFeature();
    if (f.refresh) f.refresh(f);
    TopBar(f.title);
    int lx = 10, lw = 450, y = 56;
    {
        auto dl = BuildLines(f.desc, lw / CharW(g_fontM), true);
        for (size_t i = 0; i < dl.size() && i < 2; ++i) { DrawText_(lx, y, dl[i], COL_DIM, g_fontM); y += FontH(g_fontM) + 1; }
        y += 6;
    }
    int visible = 0;
    for (auto& fd : f.fields) if (fd.label != "(not used)") ++visible;
    bool compact = visible > 6;
    bool doRun = g_enter;
    for (size_t i = 0; i < f.fields.size(); ++i) {
        Field& fd = f.fields[i];
        if (fd.label == "(not used)") continue;
        if (!fd.choices.empty()) {
            DrawText_(lx, y, fd.label, COL_TEXT, g_fontS); y += FontH(g_fontS) + 3;
            int n = (int)fd.choices.size(), cw = (lw - (n - 1) * 4) / n;
            bool twoRows = n > 3 || cw < 110;
            int perRow = twoRows ? (n + 1) / 2 : n;
            cw = (lw - (perRow - 1) * 4) / perRow;
            for (int k = 0; k < n; ++k) {
                int cx = lx + (k % perRow) * (cw + 4), cy = y + (k / perRow) * 32;
                if (DrawButton(cx, cy, cw, 28, fd.choices[k], k == fd.choice ? COL_OK : COL_CHIP, g_fontM, k == fd.choice)) fd.choice = k;
            }
            y += (twoRows ? 64 : 32) + 6;
        } else if (compact) {
            TextBox(lx, y, lw, 28, fd.value, fd.label + (fd.hint.empty() ? "" : "   (" + fd.hint + ")"));
            y += 32;
        } else {
            DrawText_(lx, y, fd.label, COL_TEXT, g_fontS); y += FontH(g_fontS) + 3;
            TextBox(lx, y, lw, 28, fd.value, fd.hint);
            y += 28 + 8;
        }
    }
    if (!f.dynLabel.empty()) {
        int n = (int)f.fields.size() - f.dynStart;
        if (DrawButton(lx, y, 120, 28, "+ Add " + f.dynLabel, COL_CHIP, g_fontM) && n < f.dynMax)
            Defer([&f] { f.fields.push_back(TF(f.dynLabel + " " + std::to_string(f.fields.size() - f.dynStart + 1), "")); ResetFocus(); });
        if (DrawButton(lx + 126, y, 130, 28, "- Remove last", COL_CHIP, g_fontM) && n > f.dynMin)
            Defer([&f] { f.fields.pop_back(); ResetFocus(); });
        y += 36;
    }
    if (DrawButton(lx, y, 190, 36, "Calculate", COL_OK, g_fontS)) doRun = true;
    if (!f.examples.empty() && DrawButton(lx + 198, y, 120, 36, "Example", COL_BTN, g_fontS)) Defer([&f] { ApplyExample(f); });
    if (DrawButton(lx + 326, y, 124, 36, "Clear", COL_CLEAR, g_fontS)) {
        Defer([&f] { for (auto& fd : f.fields) { if (fd.choices.empty()) fd.value.clear(); } f.result.clear(); f.error.clear(); ResetFocus(); });
    }
    y += 44;
    MathKeypad(lx, g_h - 10, lw, g_cats[g_cat].title == "Mathematical Logic" ? LOGIC_KEYS : g_cats[g_cat].title == "Advanced Logic (University)" ? ADV_KEYS : FORM_KEYS);

    // ---- แผงขวา ----
    int rx = lx + lw + 12, rw = g_w - rx - 10, ry = 56;
    if (f.isGraph) {
        bool plotClicked = false;
        auto rerun = [&f]() { if (!f.result.empty() || !f.error.empty()) Defer([&f] { RunFeature(f); }); };
        ry += GraphControls(rx, ry, rw, g_gv, rerun, plotClicked);
        if (plotClicked) doRun = true;
    }
    ResultPanel(rx, ry, rw, g_h - ry - 10, f.result, f.error, f.scroll, !f.isGraph && !f.noWrap, f.noWrap ? &f.hscroll : nullptr);
    if (doRun) Defer([&f] { RunFeature(f); });
}

// ============================================================
//                    ตัวแก้ไขตาราง (Grid) ใช้ร่วมกัน Matrix / DataTable
// ============================================================
struct Grid {
    int r = 2, c = 2, scroll = 0, scrollC = 0;
    std::vector<std::string> cell, hdr;
};
static Grid MakeGrid(int r, int c) {
    Grid g; g.r = r; g.c = c; g.cell.assign((size_t)r * c, "");
    for (int j = 0; j < c; ++j) g.hdr.push_back("col" + std::to_string(j + 1));
    return g;
}
static void ResizeGrid(Grid& g, int nr, int nc) {
    std::vector<std::string> n((size_t)nr * nc);
    for (int i = 0; i < std::min(g.r, nr); ++i)
        for (int j = 0; j < std::min(g.c, nc); ++j) n[(size_t)i * nc + j] = g.cell[(size_t)i * g.c + j];
    g.hdr.resize(nc);
    for (int j = g.c; j < nc; ++j) g.hdr[j] = "col" + std::to_string(j + 1);
    g.cell.swap(n); g.r = nr; g.c = nc;
    g.scroll = std::clamp(g.scroll, 0, std::max(0, nr - 1)); g.scrollC = std::clamp(g.scrollC, 0, std::max(0, nc - 1));
    ResetFocus();
}
static bool LooksNumeric(const std::string& s) {
    const char* p = s.c_str(); char* end = nullptr;
    if (!*p) return false;
    strtod(p, &end);
    return end != p && *end == 0;
}
// แถวคั่นด้วย ; (หรือขึ้นบรรทัด) คอลัมน์คั่นด้วย , หรือ space/tab
// append = ต่อท้ายแถวใหม่   headerDetect = ถ้าแถวแรกเป็นข้อความทั้งหมดให้ใช้เป็นชื่อคอลัมน์
static void FillGridFromText(Grid& g, const std::string& text, int maxR, int maxC, bool append = false, bool headerDetect = false) {
    std::string t = text;
    for (char& ch : t) if (ch == '\n' || ch == '\r') ch = ';';
    std::vector<std::vector<std::string>> rows;
    for (auto& r : SplitTop(t, ";")) rows.push_back(SplitTop(r, ", \t"));
    if (rows.empty()) throw std::runtime_error("Type or paste values like  1,2;3,4  (rows separated by ; or new lines)");
    std::vector<std::string> hdr;
    if (headerDetect && rows.size() > 1) {
        bool allText = true;
        for (auto& tok : rows[0]) if (LooksNumeric(tok) || !isalpha((unsigned char)tok[0])) allText = false;
        if (allText) { hdr = rows[0]; rows.erase(rows.begin()); }
    }
    size_t cols = 0; for (auto& r : rows) cols = std::max(cols, r.size());
    if (append) cols = std::max(cols, (size_t)g.c);
    size_t totalRows = rows.size() + (append ? (size_t)g.r : 0);
    if ((long long)totalRows > maxR || (long long)cols > maxC) throw std::runtime_error("Too large (max " + std::to_string(maxR) + " x " + std::to_string(maxC) + ")");
    for (auto& r : rows) if (r.size() != cols && !(append && r.size() <= cols && r.size() > 0)) {
        if (r.size() != cols) throw std::runtime_error("Every row must have the same number of values");
    }
    Grid n = MakeGrid((int)totalRows, (int)cols);
    size_t off = 0;
    if (append) {
        for (int i = 0; i < g.r; ++i) for (int j = 0; j < g.c; ++j) n.cell[(size_t)i * cols + j] = g.cell[(size_t)i * g.c + j];
        off = g.r;
    }
    for (size_t i = 0; i < rows.size(); ++i) for (size_t j = 0; j < rows[i].size() && j < cols; ++j) n.cell[(off + i) * cols + j] = rows[i][j];
    n.hdr = g.hdr; n.hdr.resize(cols);
    for (size_t j = g.hdr.size(); j < cols; ++j) n.hdr[j] = "col" + std::to_string(j + 1);
    if (!hdr.empty()) for (size_t j = 0; j < hdr.size() && j < cols; ++j) n.hdr[j] = hdr[j];
    g = n;
    ResetFocus();
}
// วาดตาราง: fixedCw > 0 = โหมดตารางใหญ่ (เลื่อนแถว/คอลัมน์ได้ไม่จำกัด)
static int DrawGrid(int x, int y, int maxW, int maxRows, Grid& g, bool editHeaders, int fixedCw = 0) {
    int rowLab = fixedCw ? 46 : 30, ch = 26;
    int cw, visC;
    if (fixedCw) { cw = fixedCw; visC = std::clamp((maxW - rowLab - 110) / cw, 1, g.c); }
    else { cw = std::clamp((maxW - rowLab) / std::max(1, g.c), 50, 130); visC = g.c; }
    int vis = std::min(g.r, maxRows), maxScroll = std::max(0, g.r - vis), maxScrollC = std::max(0, g.c - visC);
    bool over = Hit(x, y, maxW, (vis + (editHeaders ? 1 : 0)) * ch);
    if (over && g_wheel) { g.scroll += g_wheel * (fixedCw ? 3 : 1); g_wheel = 0; }
    if (over && g_wheelH) { g.scrollC += g_wheelH * 2; g_wheelH = 0; }
    g.scroll = std::clamp(g.scroll, 0, maxScroll);
    g.scrollC = std::clamp(g.scrollC, 0, maxScrollC);
    int yy = y;
    if (editHeaders) {
        for (int j = 0; j < visC; ++j) TextBox(x + rowLab + j * cw, yy, cw - 2, ch - 2, g.hdr[g.scrollC + j], "name", g_fontM);
        yy += ch;
    }
    for (int i = 0; i < vis; ++i) {
        int r = g.scroll + i;
        DrawText_(x + 4, yy + (ch - FontH(g_fontM)) / 2, std::to_string(r + 1), COL_DIM, g_fontM);
        for (int j = 0; j < visC; ++j)
            TextBox(x + rowLab + j * cw, yy, cw - 2, ch - 2, g.cell[(size_t)r * g.c + g.scrollC + j], "0", g_fontM);
        yy += ch;
    }
    if (fixedCw) {
        int cx = x + rowLab + visC * cw + 8;
        DrawText_(cx, y, "r " + std::to_string(g.scroll + 1) + "-" + std::to_string(g.scroll + vis) + "/" + std::to_string(g.r), COL_DIM, g_fontM);
        DrawText_(cx, y + 16, "c " + std::to_string(g.scrollC + 1) + "-" + std::to_string(g.scrollC + visC) + "/" + std::to_string(g.c), COL_DIM, g_fontM);
        int by = y + 36;
        if (DrawButton(cx, by, 28, 24, "^", COL_CHIP, g_fontM)) g.scroll = std::max(0, g.scroll - std::max(1, vis - 1));
        if (DrawButton(cx + 32, by, 28, 24, "v", COL_CHIP, g_fontM)) g.scroll = std::min(maxScroll, g.scroll + std::max(1, vis - 1));
        if (DrawButton(cx, by + 28, 28, 24, "<", COL_CHIP, g_fontM)) g.scrollC = std::max(0, g.scrollC - std::max(1, visC - 1));
        if (DrawButton(cx + 32, by + 28, 28, 24, ">", COL_CHIP, g_fontM)) g.scrollC = std::min(maxScrollC, g.scrollC + std::max(1, visC - 1));
        DrawText_(cx, by + 58, "wheel / Shift+wheel", COL_DIM, g_fontM);
    } else if (maxScroll > 0) {
        DrawText_(x + rowLab + g.c * cw + 4, y, "rows " + std::to_string(g.scroll + 1) + "-" + std::to_string(g.scroll + vis) + " of " + std::to_string(g.r) + " (wheel)", COL_DIM, g_fontM);
    }
    return yy - y;
}
static bool Stepper(int x, int y, const std::string& label, int& value, int lo, int hi) {
    DrawText_(x, y + 6, label, COL_TEXT, g_fontS);
    int lx = x + TextW(g_fontS, label) + 8;
    bool changed = false;
    if (DrawButton(lx, y, 28, 28, "-", COL_CHIP) && value > lo) { value--; changed = true; }
    DrawTextCenter(lx + 28 + 18, y + 14, std::to_string(value), COL_TEXT, g_fontS);
    if (DrawButton(lx + 28 + 36, y, 28, 28, "+", COL_CHIP) && value < hi) { value++; changed = true; }
    return changed;
}

// ============================================================
//                    [5] Multi-Matrix Calculator (N ตัว, n x m)
// ============================================================
static std::vector<Grid> g_mats;
static std::vector<char> g_muse;
static int g_msel = 0;
static std::string g_mp[2], g_mquick, g_mres, g_merr;
static int g_mscroll = 0, g_mhscroll = 0;
static Matrix g_lastMat(1, 1);
static bool g_haveLast = false;
static const int MAT_MAX = 12, MAT_COUNT_MAX = 8;
static const char* MAT_OPS[10] = {"A+B+...", "A-B-...", "A*B*...", "Transpose", "Det", "Inverse", "d/dx", "Integral", "F(x)", "Solve AX=B"};
static std::string MatName(int i) { return std::string(1, (char)('A' + i)); }

static Matrix MatOf(const Grid& g) {
    std::vector<std::vector<double>> v(g.r, std::vector<double>(g.c, 0.0));
    for (int i = 0; i < g.r; ++i)
        for (int j = 0; j < g.c; ++j) {
            std::string s = Trim(g.cell[(size_t)i * g.c + j]);
            v[i][j] = s.empty() ? 0.0 : CellValue(s);
        }
    return Matrix(v);
}
static Matrix Elementwise(const Grid& g, const std::function<double(const std::string&)>& fn) {
    std::vector<std::vector<double>> v(g.r, std::vector<double>(g.c, 0.0));
    for (int i = 0; i < g.r; ++i)
        for (int j = 0; j < g.c; ++j) {
            std::string s = Trim(g.cell[(size_t)i * g.c + j]);
            v[i][j] = s.empty() ? 0.0 : fn(Norm(s));
        }
    return Matrix(v);
}
static std::string Dim(const Matrix& m) { return std::to_string(m.getRows()) + " x " + std::to_string(m.getCols()); }
static std::string MatrixRun() {
    g_haveLast = false;
    std::vector<int> used;
    for (size_t i = 0; i < g_mats.size(); ++i) if (g_muse[i]) used.push_back((int)i);
    if (g_mop <= 2) {
        if (used.size() < 2) throw std::runtime_error("Tick at least 2 matrices with the [x] boxes next to the tabs");
        static const char* sym[] = {" + ", " - ", " * "};
        Matrix R = MatOf(g_mats[used[0]]);
        std::string title = MatName(used[0]);
        for (size_t k = 1; k < used.size(); ++k) {
            Matrix B = MatOf(g_mats[used[k]]);
            R = g_mop == 0 ? R.add(B) : g_mop == 1 ? R.subtract(B) : R.multiply(B);
            title += sym[g_mop] + MatName(used[k]);
        }
        g_lastMat = R; g_haveLast = true;
        return title + "  =  (" + Dim(R) + ")\n" + FormatMatrix(R);
    }
    if (g_mop == 9) {
        if (used.size() < 2) throw std::runtime_error("Tick two matrices: first = A (coefficients), second = B (right side)");
        Matrix X = Matrix::solveLinearGeneral(MatOf(g_mats[used[0]]), MatOf(g_mats[used[1]]));
        g_lastMat = X; g_haveLast = true;
        return "X  such that  " + MatName(used[0]) + " X = " + MatName(used[1]) + "  (" + Dim(X) + ")\n" + FormatMatrix(X);
    }
    const Grid& A = g_mats[g_msel];
    std::string nm = MatName(g_msel);
    auto P = [&](int i, bool required) -> double {
        std::string s = Trim(g_mp[i]);
        if (s.empty()) { if (required) throw std::runtime_error("Please fill in the parameter boxes"); return 0.0; }
        return EvalNum(s);
    };
    Matrix R(1, 1);
    std::string head;
    switch (g_mop) {
        case 3: R = MatOf(A).transpose(); head = nm + "^T"; break;
        case 4: { double d = MatOf(A).determinant(); SetAns(d); return "det(" + nm + ") = " + Fmt(d); }
        case 5: R = MatOf(A).inverse(); head = nm + "^-1"; break;
        case 6: { double x0 = P(0, false); R = Elementwise(A, [x0](const std::string& e) { return g_calc.derivative(e, x0); }); head = "d/dx " + nm + "  at x0 = " + Fmt(x0); break; }
        case 7: { double a = P(0, true), b = P(1, true); R = Elementwise(A, [a, b](const std::string& e) { return g_calc.integral(e, a, b); }); head = "Integral[" + Fmt(a) + " -> " + Fmt(b) + "] " + nm; break; }
        default: { double x0 = P(0, true), xr = P(1, false); R = Elementwise(A, [x0, xr](const std::string& e) { return g_calc.indefiniteIntegral(e, x0, xr); }); head = "F(" + Fmt(x0) + ") of " + nm + " from x_ref = " + Fmt(xr); break; }
    }
    g_lastMat = R; g_haveLast = true;
    return head + "  (" + Dim(R) + ")\n" + FormatMatrix(R);
}
static void MatrixInit() {
    if (!g_mats.empty()) return;
    g_mats.push_back(MakeGrid(2, 2)); g_mats.push_back(MakeGrid(2, 2));
    const char* a[] = {"1", "2", "3", "4"}; const char* b[] = {"5", "6", "7", "8"};
    for (int i = 0; i < 4; ++i) { g_mats[0].cell[i] = a[i]; g_mats[1].cell[i] = b[i]; }
    g_muse = {1, 1};
}
static void ScreenMatrix() {
    MatrixInit();
    TopBar("[5] Multi-Matrix Calculator");
    int x = 10, w = g_w - 20, y = 54;
    // แท็บเมทริกซ์
    for (size_t i = 0; i < g_mats.size(); ++i) {
        int tx = x + (int)i * 84;
        if (DrawButton(tx, y, 40, 30, MatName((int)i), (int)i == g_msel ? COL_BTN : COL_CHIP, g_fontS, (int)i == g_msel))
            Defer([i] { g_msel = (int)i; ResetFocus(); });
        if (DrawButton(tx + 42, y, 34, 30, g_muse[i] ? "[x]" : "[ ]", g_muse[i] ? COL_OK : COL_CHIP, g_fontM))
            g_muse[i] = !g_muse[i];
    }
    int bx = x + (int)g_mats.size() * 84 + 6;
    if ((int)g_mats.size() < MAT_COUNT_MAX && DrawButton(bx, y, 120, 30, "+ Add matrix", COL_BTN, g_fontM))
        Defer([] { g_mats.push_back(MakeGrid(2, 2)); g_muse.push_back(0); g_msel = (int)g_mats.size() - 1; ResetFocus(); });
    if (g_mats.size() > 1 && DrawButton(bx + 126, y, 120, 30, "- Remove " + MatName(g_msel), COL_CLEAR, g_fontM))
        Defer([] { g_mats.erase(g_mats.begin() + g_msel); g_muse.erase(g_muse.begin() + g_msel); g_msel = std::max(0, std::min(g_msel, (int)g_mats.size() - 1)); ResetFocus(); });
    y += 36;
    DrawText_(x, y, "Edit matrix " + MatName(g_msel) + "   (" + std::to_string(g_mats[g_msel].r) + " x " + std::to_string(g_mats[g_msel].c) +
                    ").  Cells accept expressions (2*x, sin(pi/2), ...). Tick [x] the matrices used by + - * and Solve.", COL_DIM, g_fontM);
    y += FontH(g_fontM) + 6;
    Grid& G = g_mats[g_msel];
    int gh = DrawGrid(x, y, std::min(w, 800), 8, G, false);
    y += gh + 8;
    // ขนาด + กรอกเร็ว
    {
        int r = G.r, c = G.c;
        bool ch1 = Stepper(x, y, "Rows", r, 1, MAT_MAX);
        bool ch2 = Stepper(x + 150, y, "Cols", c, 1, MAT_MAX);
        if (ch1 || ch2) ResizeGrid(G, r, c);
        int qx = x + 310;
        DrawText_(qx, y + 6, "Quick fill", COL_TEXT, g_fontS);
        TextBox(qx + 84, y, std::max(100, w - 310 - 84 - 190), 28, g_mquick, "1,2;3,4  (rows split by ;)", g_fontM);
        int qb = qx + 84 + std::max(100, w - 310 - 84 - 190) + 6;
        if (DrawButton(qb, y, 78, 28, "Apply", COL_BTN, g_fontM))
            try { FillGridFromText(G, g_mquick, MAT_MAX, MAT_MAX); g_merr.clear(); } catch (const std::exception& e) { g_merr = e.what(); }
        if (DrawButton(qb + 84, y, 100, 28, "Clear cells", COL_CLEAR, g_fontM)) for (auto& s : G.cell) s.clear();
        y += 36;
    }
    // ปุ่มการดำเนินการ
    {
        int cw = (w - 4 * 4) / 5;
        for (int i = 0; i < 10; ++i)
            if (DrawButton(x + (i % 5) * (cw + 4), y + (i / 5) * 32, cw, 28, MAT_OPS[i], i == g_mop ? COL_OK : COL_CHIP, g_fontS, i == g_mop)) g_mop = i;
        y += 68;
    }
    bool run = g_enter;
    // พารามิเตอร์
    {
        const char* l0 = nullptr; const char* l1 = nullptr;
        if (g_mop == 6) l0 = "x0 (point)";
        else if (g_mop == 7) { l0 = "a (lower)"; l1 = "b (upper)"; }
        else if (g_mop == 8) { l0 = "x0 (evaluate at)"; l1 = "x_ref (default 0)"; }
        int px = x;
        if (l0) { DrawText_(px, y + 6, l0, COL_TEXT, g_fontS); px += TextW(g_fontS, l0) + 8; TextBox(px, y, 90, 28, g_mp[0], "0", g_fontM); px += 100; }
        if (l1) { DrawText_(px, y + 6, l1, COL_TEXT, g_fontS); px += TextW(g_fontS, l1) + 8; TextBox(px, y, 90, 28, g_mp[1], "0", g_fontM); px += 100; }
        if (!l0) DrawText_(px, y + 6, (g_mop <= 2 ? "Uses all ticked matrices in order." : g_mop == 9 ? "A = first ticked, B = second ticked." : "Uses the selected tab."), COL_DIM, g_fontS);
        int bx2 = std::max(px + 10, w - 330);
        if (DrawButton(bx2, y - 2, 150, 34, "Calculate", COL_OK)) run = true;
        if (DrawButton(bx2 + 156, y - 2, 170, 34, "Result -> new matrix", g_haveLast ? COL_BTN : COL_CHIP, g_fontM) && g_haveLast && (int)g_mats.size() < MAT_COUNT_MAX)
            Defer([] {
                Grid n = MakeGrid(g_lastMat.getRows(), g_lastMat.getCols());
                for (int i = 0; i < n.r; ++i) for (int j = 0; j < n.c; ++j) n.cell[(size_t)i * n.c + j] = Fmt(g_lastMat.getElement(i, j));
                g_mats.push_back(n); g_muse.push_back(0); g_msel = (int)g_mats.size() - 1; ResetFocus();
            });
        y += 42;
    }
    if (run) Defer([] {
        g_merr.clear();
        try { g_mres = MatrixRun(); } catch (const std::exception& e) { g_merr = e.what(); g_mres.clear(); }
        g_mscroll = 0;
    });
    ResultPanel(x, y, w, g_h - y - 10, g_mres, g_merr, g_mscroll, false, &g_mhscroll);
}

// ============================================================
//                    [6] Data Table  (ไม่จำกัดแถว / คอลัมน์ / จำนวนตาราง)
// ============================================================
static std::vector<Grid> g_tabs;
static std::vector<char> g_tuse;
static int g_tsel = 0, g_ttabOff = 0, g_tsig = -1;
static std::string g_tp[2], g_tquick, g_tres, g_terr, g_trowsS, g_tcolsS;
static int g_tscroll = 0, g_thscroll = 0;
static bool g_tlines = false;
static const long long TAB_MAX_CELLS = 30000000LL;   // กันหน่วยความจำหมด (ไม่จำกัดจำนวนแถว/คอลัมน์ในทางปฏิบัติ)
static const char* TAB_OPS[9] = {"Column stats", "Computed column", "Transpose", "Scalar x k", "Marginal / joint",
                                 "Expected value E[X]", "Scatter / line plot", "Add tables", "Multiply tables"};
static std::string TabName(int i) { return "T" + std::to_string(i + 1); }
static Grid& CurTab() { return g_tabs[g_tsel]; }

static Grid SampleTable() {
    Grid g = MakeGrid(5, 2);
    g.hdr = {"x", "y"};
    const char* v[] = {"1", "2", "2", "4", "3", "5", "4", "4", "5", "5"};
    for (int i = 0; i < 10; ++i) g.cell[i] = v[i];
    return g;
}
static void TableInit() {
    if (!g_tabs.empty()) return;
    g_tabs.push_back(SampleTable());
    g_tuse = {1};
}
static DataTable TableOf(const Grid& g) {
    DataTable t(g.r, g.c);
    t.setHeaders(g.hdr);
    for (int i = 0; i < g.r; ++i)
        for (int j = 0; j < g.c; ++j) {
            const std::string& s = g.cell[(size_t)i * g.c + j];
            t.setCell(i, j, s.empty() ? 0.0 : CellValue(s));
        }
    return t;
}
// "2,3,5"  "2-6"  "all"  (เลขคอลัมน์เริ่มที่ 1)
static std::vector<int> ParseColList(const std::string& s0, int ncols, int exclude = -1) {
    std::string s = Trim(s0);
    std::vector<int> out;
    if (s == "all") { for (int c = 0; c < ncols; ++c) if (c != exclude) out.push_back(c); }
    else for (auto& tok : SplitTop(s, ", ;")) {
        size_t d = tok.find('-');
        if (d != std::string::npos && d > 0) {
            int a = atoi(tok.substr(0, d).c_str()), b = atoi(tok.substr(d + 1).c_str());
            if (a > b) std::swap(a, b);
            for (int k = a; k <= b; ++k) out.push_back(k - 1);
        } else out.push_back(atoi(tok.c_str()) - 1);
    }
    if (out.empty()) throw std::runtime_error("Enter column numbers like  2   or  2,3,4   or  2-5   or  all");
    for (int c : out) if (c < 0 || c >= ncols) throw std::runtime_error("Column number out of range (1.." + std::to_string(ncols) + ")");
    return out;
}
static std::string TableRun() {
    Grid& G = CurTab();
    if (g_top == 7 || g_top == 8) {
        std::vector<int> used;
        for (size_t i = 0; i < g_tabs.size(); ++i) if (g_tuse[i]) used.push_back((int)i);
        if (used.size() < 2) throw std::runtime_error("Tick at least 2 tables with the [x] boxes next to the tabs");
        DataTable R = TableOf(g_tabs[used[0]]);
        std::string head = TabName(used[0]);
        for (size_t k = 1; k < used.size(); ++k) {
            DataTable B = TableOf(g_tabs[used[k]]);
            R = (g_top == 7) ? TableCalculate::addTables(R, B) : TableCalculate::multiplyTables(R, B);
            head += (g_top == 7 ? " + " : " x ") + TabName(used[k]);
        }
        return head + "  =  (" + std::to_string(R.getRows()) + " x " + std::to_string(R.getCols()) + ")\n" + FormatTable(R);
    }
    DataTable t = TableOf(G);
    switch (g_top) {
        case 0: {
            std::string out;
            char buf[256];
            snprintf(buf, sizeof buf, "%-14s %7s %13s %13s %13s %13s %13s %13s %13s\n", "Column", "n", "sum", "mean", "min", "max", "median", "variance", "stdev");
            out += buf;
            for (int c = 0; c < t.getCols(); ++c) {
                snprintf(buf, sizeof buf, "%-14s %7d %13.6g %13.6g %13.6g %13.6g %13.6g %13.6g %13.6g\n", t.getHeader(c).substr(0, 14).c_str(), t.getRows(),
                         TableCalculate::sumColumn(t, c), TableCalculate::meanColumn(t, c), TableCalculate::minColumn(t, c),
                         TableCalculate::maxColumn(t, c), TableCalculate::medianColumn(t, c),
                         t.getRows() > 1 ? TableCalculate::varianceColumn(t, c, true) : 0.0,
                         t.getRows() > 1 ? TableCalculate::stdevColumn(t, c, true) : 0.0);
                out += buf;
            }
            out += "(variance / stdev are sample statistics;  " + std::to_string(t.getCols()) + " column(s), " + std::to_string(t.getRows()) + " row(s))";
            return out;
        }
        case 1: {
            std::string e = ReplaceAns(Trim(g_tp[0]));
            if (e.empty()) throw std::runtime_error("Type an expression, e.g.  x^2 + 2*y  (variables = column names that are single letters)");
            static const std::string pool = "xyzwuvpqrstabcdfghijklmno";      // ไม่ใช้ e (ค่าคงที่)
            std::string used; std::vector<char> map;
            for (int c = 0; c < t.getCols(); ++c) {
                char ch = 0;
                const std::string& h = G.hdr[c];
                if (h.size() == 1 && isalpha((unsigned char)h[0]) && h[0] != 'e' && used.find(h[0]) == std::string::npos) ch = h[0];
                else for (char p : pool) if (used.find(p) == std::string::npos) { ch = p; break; }
                if (!ch) break;
                used += ch; map.push_back(ch);
            }
            e = ApplyParamsKeep(e, used);                       // พารามิเตอร์อื่น ๆ ใน Vars
            DataTable res = TableCalculate::applyExpression(t, g_calc, e, map, "result");
            std::string s = "Expression: " + e + "\nVariables: ";
            for (size_t c = 0; c < map.size(); ++c) s += std::string(1, map[c]) + "=" + G.hdr[c] + (c + 1 < map.size() ? ", " : "");
            return s + "\n" + FormatTable(res);
        }
        case 2: return "Transpose  (" + std::to_string(t.getCols()) + " x " + std::to_string(t.getRows()) + "):\n" + FormatTable(TableCalculate::transpose(t));
        case 3: { double k = EvalNum(g_tp[0].empty() ? "1" : g_tp[0]); return "Table x " + Fmt(k) + ":\n" + FormatTable(TableCalculate::multiplyScalar(t, k)); }
        case 4: return "Joint table with marginal totals (last row / column):\n" + FormatTable(TableCalculate::computeMarginalProbabilities(t));
        case 5: {
            int xc = (int)EvalNum(g_tp[0].empty() ? "1" : g_tp[0]) - 1, pc = (int)EvalNum(g_tp[1].empty() ? "2" : g_tp[1]) - 1;
            if (xc < 0 || pc < 0 || xc >= t.getCols() || pc >= t.getCols()) throw std::runtime_error("Column number out of range");
            double ev = TableCalculate::computeExpectedValue(t.getColumn(xc), t.getColumn(pc));
            SetAns(ev);
            return "E[X] = sum( x * P(x) ) = " + Fmt(ev) + "   (values: column " + std::to_string(xc + 1) + ", probabilities: column " + std::to_string(pc + 1) + ")";
        }
        default: {
            int xc = (int)EvalNum(g_tp[0].empty() ? "1" : g_tp[0]) - 1;
            if (xc < 0 || xc >= t.getCols()) throw std::runtime_error("X column number out of range (1.." + std::to_string(t.getCols()) + ")");
            auto cols = ParseColList(Trim(g_tp[1]).empty() ? (t.getCols() > 1 ? "2" : "1") : g_tp[1], t.getCols(), xc);
            PlotSpec sp; sp.kind = GK_SCATTER; sp.table = t; sp.xc = xc; sp.yc = cols[0]; sp.ycs = cols;
            for (int c : cols) sp.names.push_back(G.hdr[c]);
            sp.lines = g_tlines;
            sp.title = g_tlines ? "Line Plot (x = " + G.hdr[xc] + ")" : "Scatter Plot (x = " + G.hdr[xc] + ")";
            return GraphOut(g_gvScatter, sp);
        }
    }
}
static void TableExec() {
    g_terr.clear();
    try { g_tres = TableRun(); } catch (const std::exception& e) { g_terr = e.what(); g_tres.clear(); }
    g_tscroll = 0;
}
static void TableSetSize(int r, int c) {
    if (r < 1 || c < 1) { g_terr = "Rows and columns must be at least 1"; return; }
    if ((long long)r * c > TAB_MAX_CELLS) { g_terr = "Table too large for memory (limit " + std::to_string(TAB_MAX_CELLS) + " cells)"; return; }
    ResizeGrid(CurTab(), r, c);
    g_terr.clear();
}
static void ScreenTable() {
    TableInit();
    TopBar("[6] Data Table & Statistics");
    int x = 10, w = g_w - 20, y = 54;
    int n = (int)g_tabs.size();
    // ---- แท็บตาราง (ไม่จำกัดจำนวน) ----
    {
        int tabW = 80, maxTabs = std::max(1, (w - 300) / tabW);
        g_ttabOff = std::clamp(g_ttabOff, 0, std::max(0, n - maxTabs));
        int tx = x;
        if (n > maxTabs) { if (DrawButton(tx, y, 26, 30, "<", COL_CHIP)) g_ttabOff = std::max(0, g_ttabOff - 1); tx += 30; }
        for (int i = g_ttabOff; i < std::min(n, g_ttabOff + maxTabs); ++i) {
            if (DrawButton(tx, y, 40, 30, TabName(i), i == g_tsel ? COL_BTN : COL_CHIP, g_fontS, i == g_tsel)) Defer([i] { g_tsel = i; ResetFocus(); g_tsig = -1; });
            if (DrawButton(tx + 42, y, 34, 30, g_tuse[i] ? "[x]" : "[ ]", g_tuse[i] ? COL_OK : COL_CHIP, g_fontM)) g_tuse[i] = !g_tuse[i];
            tx += tabW;
        }
        if (n > maxTabs) { if (DrawButton(tx, y, 26, 30, ">", COL_CHIP)) g_ttabOff = std::min(n - maxTabs, g_ttabOff + 1); tx += 30; }
        if (DrawButton(tx, y, 110, 30, "+ Add table", COL_BTN, g_fontM))
            Defer([] { g_tabs.push_back(SampleTable()); g_tuse.push_back(0); g_tsel = (int)g_tabs.size() - 1; g_ttabOff = 1 << 20; ResetFocus(); g_tsig = -1; });
        if (n > 1 && DrawButton(tx + 116, y, 120, 30, "- Remove " + TabName(g_tsel), COL_CLEAR, g_fontM))
            Defer([] { g_tabs.erase(g_tabs.begin() + g_tsel); g_tuse.erase(g_tuse.begin() + g_tsel); g_tsel = std::min(g_tsel, (int)g_tabs.size() - 1); ResetFocus(); g_tsig = -1; });
        y += 36;
    }
    Grid& G = CurTab();
    DrawText_(x, y, TabName(g_tsel) + ": " + std::to_string(G.r) + " rows x " + std::to_string(G.c) + " columns (any size). Edit cells / headers, or paste data with Ctrl+V into Quick fill. Tick [x] tables for Add / Multiply.", COL_DIM, g_fontM);
    y += FontH(g_fontM) + 6;
    int maxRows = (g_top == 6) ? 2 : std::clamp((g_h - 600) / 26, 3, 14);
    y += DrawGrid(x, y, std::min(w, 1000), maxRows, G, true, 96) + 8;
    // ---- ขนาดตาราง ----
    {
        int sig = g_tsel * 1000003 + G.r * 1009 + G.c;
        if (sig != g_tsig) { g_tsig = sig; g_trowsS = std::to_string(G.r); g_tcolsS = std::to_string(G.c); }
        DrawText_(x, y + 7, "Rows", COL_TEXT, g_fontS);  TextBox(x + 40, y, 76, 28, g_trowsS, "rows", g_fontM);
        DrawText_(x + 128, y + 7, "Cols", COL_TEXT, g_fontS); TextBox(x + 166, y, 76, 28, g_tcolsS, "cols", g_fontM);
        if (DrawButton(x + 252, y, 84, 28, "Set size", COL_BTN, g_fontM))
            Defer([] { try { TableSetSize((int)EvalNum(g_trowsS), (int)EvalNum(g_tcolsS)); } catch (const std::exception& e) { g_terr = e.what(); } });
        int bx = x + 348;
        const char* nm[4] = {"+ Row", "- Row", "+ Col", "- Col"};
        for (int i = 0; i < 4; ++i)
            if (DrawButton(bx + i * 66, y, 62, 28, nm[i], COL_CHIP, g_fontM))
                Defer([i] { Grid& g = CurTab(); TableSetSize(g.r + (i == 0) - (i == 1), g.c + (i == 2) - (i == 3)); });
        if (DrawButton(bx + 272, y, 100, 28, "Clear cells", COL_CLEAR, g_fontM)) for (auto& s : G.cell) s.clear();
        y += 36;
    }
    // ---- กรอกเร็ว / วางข้อมูล (ซ่อนตอนพล็อตกราฟเพื่อเพิ่มพื้นที่กราฟ) ----
    if (g_top != 6) {
        int bw = 100, boxW = std::max(120, w - 84 - 2 * (bw + 6) - 6);
        DrawText_(x, y + 7, "Quick fill", COL_TEXT, g_fontS);
        TextBox(x + 84, y, boxW, 28, g_tquick, "paste with Ctrl+V:  x,y;1,2;3,4   (header row optional)", g_fontM);
        if (DrawButton(x + 84 + boxW + 6, y, bw, 28, "Replace", COL_BTN, g_fontM))
            Defer([] { try { FillGridFromText(CurTab(), g_tquick, 1 << 24, 1 << 20, false, true); g_terr.clear(); g_tquick.clear(); } catch (const std::exception& e) { g_terr = e.what(); } });
        if (DrawButton(x + 84 + boxW + 6 + bw + 6, y, bw, 28, "Add rows", COL_BTN, g_fontM))
            Defer([] { try { FillGridFromText(CurTab(), g_tquick, 1 << 24, 1 << 20, true, false); g_terr.clear(); g_tquick.clear(); } catch (const std::exception& e) { g_terr = e.what(); } });
        y += 36;
    }
    // ---- การดำเนินการ ----
    {
        int cw = (w - 4 * 4) / 5;
        for (int i = 0; i < 9; ++i)
            if (DrawButton(x + (i % 5) * (cw + 4), y + (i / 5) * 32, cw, 28, TAB_OPS[i], i == g_top ? COL_OK : COL_CHIP, g_fontS, i == g_top)) g_top = i;
        y += 68;
    }
    bool run = g_enter;
    {
        const char* l0 = nullptr; const char* l1 = nullptr; const char* h0 = "";
        if (g_top == 1) { l0 = "Expression"; h0 = "x^2 + 2*y"; }
        else if (g_top == 3) { l0 = "k (scalar)"; h0 = "2"; }
        else if (g_top == 5) { l0 = "X column #"; l1 = "P column #"; }
        else if (g_top == 6) { l0 = "X column #"; l1 = "Y column(s)"; }
        int px = x;
        if (l0) {
            DrawText_(px, y + 7, l0, COL_TEXT, g_fontS); px += TextW(g_fontS, l0) + 8;
            int bw2 = (g_top == 1) ? 300 : 60;
            TextBox(px, y, bw2, 28, g_tp[0], g_top >= 5 ? "1" : h0, g_fontM); px += bw2 + 12;
        }
        if (l1) {
            DrawText_(px, y + 7, l1, COL_TEXT, g_fontS); px += TextW(g_fontS, l1) + 8;
            int bw2 = (g_top == 6) ? 170 : 60;
            TextBox(px, y, bw2, 28, g_tp[1], g_top == 6 ? "2 / 2,3 / 2-5 / all" : "2", g_fontM); px += bw2 + 12;
        }
        if (g_top == 6) {
            if (DrawButton(px, y, 74, 28, "Points", !g_tlines ? COL_OK : COL_CHIP, g_fontM, !g_tlines)) g_tlines = false;
            if (DrawButton(px + 78, y, 74, 28, "Lines", g_tlines ? COL_OK : COL_CHIP, g_fontM, g_tlines)) g_tlines = true;
            px += 160;
        }
        if (!l0) DrawText_(px, y + 7, g_top == 0 ? "Statistics of every column." : g_top == 4 ? "Enter joint probabilities; totals are added." :
                                         (g_top >= 7 ? "Uses all ticked tables in order (T1, T2, ...)." : "No parameters."), COL_DIM, g_fontS);
        if (DrawButton(std::max(px + 10, w - 170), y - 2, 160, 34, "Calculate", COL_OK)) run = true;
        y += 42;
    }
    if (g_top == 6) {      // กราฟ: ซูม / เลื่อนซ้าย-ขวา-ขึ้น-ลง / กำหนดช่วง x,y / หลายซีรีส์
        bool pc = false;
        auto rr = []() { if (!g_tres.empty() || !g_terr.empty()) Defer(TableExec); };
        y += GraphControls(x, y, w, g_gvScatter, rr, pc);
        if (pc) run = true;
    }
    if (run) Defer(TableExec);
    ResultPanel(x, y, w, g_h - y - 10, g_tres, g_terr, g_tscroll, false, &g_thscroll);
}

// ============================================================
//                    Main loop
// ============================================================
static void Frame() {
    g_nextId = 0;
    FillRectPx(0, 0, g_w, g_h, COL_BG);
    if (g_escape) { g_escape = false; if (g_screen != S_HOME) GoBack(); }
    switch (g_screen) {
        case S_HOME:   ScreenHome(); break;
        case S_MENU:   ScreenMenu(); break;
        case S_FORM:   ScreenForm(); break;
        case S_CALC:   ScreenCalc(); break;
        case S_MATRIX: ScreenMatrix(); break;
        case S_TABLE:  ScreenTable(); break;
    }
}
static void Present() {
    int bi = AcquireBuffer();
    if (bi < 0) return;
    g_px = g_bufs[bi].data;
    g_tabPrev = g_tabNow; g_tabNow.clear();
    g_text.clear();
    Frame();
    if (ui.released) ui.active = 0;
    ui.pressed = ui.released = false;
    g_enter = false; g_wheel = 0; g_wheelKey = 0; g_pendingChar = 0;
    auto jobs = std::move(g_deferred);
    g_deferred.clear();
    for (auto& j : jobs) j();
    FlushText();
    g_bufs[bi].busy = true;
    wl_surface_attach(g_surf, g_bufs[bi].buf, 0, 0);
    if (wl_surface_get_version(g_surf) >= 4) wl_surface_damage_buffer(g_surf, 0, 0, g_w, g_h);
    else wl_surface_damage(g_surf, 0, 0, g_w, g_h);
    wl_surface_commit(g_surf);
    wl_display_flush(g_wl);
}
static void PasteText(std::string s) {
    std::string o;
    for (char c : s) {
        if (c == '\r') continue;
        if (c == '\n') o += ';';
        else if (c == '\t') o += ',';
        else if ((unsigned char)c >= 32 && (unsigned char)c < 127) o += c;
    }
    while (!o.empty() && o.back() == ';') o.pop_back();
    if (o.empty()) return;
    if (g_screen == S_CALC) CalcInsert(o);
    else if (g_focusStr) *g_focusStr += o;
}
static void TypeChar(char c) {
    if (g_screen == S_CALC) CalcInsert(std::string(1, c));
    else if (g_focusStr) *g_focusStr += c;
    else g_pendingChar = c;
}
static void FocusNext(bool back) {
    if (g_tabPrev.empty()) return;
    int idx = -1;
    for (size_t i = 0; i < g_tabPrev.size(); ++i) if (g_tabPrev[i] == g_focusStr) idx = (int)i;
    int n = (int)g_tabPrev.size();
    idx = idx < 0 ? 0 : (idx + (back ? n - 1 : 1)) % n;
    g_focusStr = g_tabPrev[idx];
}

// ============================================================
//                 Wayland: เหตุการณ์ / คลิปบอร์ด
// ============================================================
static bool g_needRedraw = false, g_extra = false;   // g_extra = วาดซ้ำอีกรอบเพื่อให้เห็นผลคลิก/พิมพ์ทันที
static int64_t NowMs() {
    timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// ---------- คลิปบอร์ด (wl_data_device) ----------
struct OfferInfo { bool utf8 = false, plain = false, utf8str = false, str = false; };
static std::map<wl_data_offer*, OfferInfo> g_offers;
static wl_data_offer* g_clip = nullptr;       // selection ปัจจุบัน (Ctrl+C จากโปรแกรมอื่น)
static wl_data_offer* g_dndOffer = nullptr;   // offer จากการลากวาง (ไม่ใช้ แค่ต้องคืนทรัพยากร)

static void OfferMime(void* data, wl_data_offer*, const char* mime) {
    OfferInfo* inf = (OfferInfo*)data;
    if (!strcmp(mime, "text/plain;charset=utf-8")) inf->utf8 = true;
    else if (!strcmp(mime, "text/plain")) inf->plain = true;
    else if (!strcmp(mime, "UTF8_STRING")) inf->utf8str = true;
    else if (!strcmp(mime, "STRING")) inf->str = true;
}
static void OfferSourceActions(void*, wl_data_offer*, uint32_t) {}
static void OfferAction(void*, wl_data_offer*, uint32_t) {}
static const wl_data_offer_listener g_offerListener = { OfferMime, OfferSourceActions, OfferAction };

static void DestroyOffer(wl_data_offer*& o) {
    if (!o) return;
    if (g_offers.erase(o)) wl_data_offer_destroy(o);
    o = nullptr;
}
static void DdDataOffer(void*, wl_data_device*, wl_data_offer* o) {
    OfferInfo& inf = g_offers[o];
    wl_data_offer_add_listener(o, &g_offerListener, &inf);
}
static void DdEnter(void*, wl_data_device*, uint32_t, wl_surface*, wl_fixed_t, wl_fixed_t, wl_data_offer* o) {
    DestroyOffer(g_dndOffer);
    g_dndOffer = o;
}
static void DdLeave(void*, wl_data_device*) { DestroyOffer(g_dndOffer); }
static void DdMotion(void*, wl_data_device*, uint32_t, wl_fixed_t, wl_fixed_t) {}
static void DdDrop(void*, wl_data_device*) { DestroyOffer(g_dndOffer); }
static void DdSelection(void*, wl_data_device*, wl_data_offer* o) {
    DestroyOffer(g_clip);
    g_clip = o;
}
static const wl_data_device_listener g_ddListener = { DdDataOffer, DdEnter, DdLeave, DdMotion, DdDrop, DdSelection };

static void RequestPaste() {
    if (!g_clip) return;
    auto it = g_offers.find(g_clip);
    if (it == g_offers.end()) return;
    const OfferInfo& inf = it->second;
    const char* mime = inf.utf8 ? "text/plain;charset=utf-8" : inf.plain ? "text/plain"
                     : inf.utf8str ? "UTF8_STRING" : inf.str ? "STRING" : nullptr;
    if (!mime) return;
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) < 0) return;
    wl_data_offer_receive(g_clip, mime, fds[1]);
    wl_display_flush(g_wl);
    close(fds[1]);
    std::string data;
    char buf[4096];
    int64_t deadline = NowMs() + 1500;
    for (;;) {
        int64_t left = deadline - NowMs();
        if (left <= 0) break;
        pollfd pfd = {fds[0], POLLIN, 0};
        int r = poll(&pfd, 1, (int)left);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) break;
        ssize_t n = read(fds[0], buf, sizeof buf);
        if (n <= 0) break;
        data.append(buf, (size_t)n);
        if (data.size() > (1u << 20)) break;
    }
    close(fds[0]);
    PasteText(data);
    g_needRedraw = g_extra = true;
}

// ---------- คีย์บอร์ด (xkbcommon) ----------
static xkb_context* g_xkbCtx = nullptr;
static xkb_keymap*  g_xkbMap = nullptr;
static xkb_state*   g_xkbState = nullptr;
static wl_keyboard* g_kb = nullptr;
static uint32_t g_repKey = 0;                 // 0 = ไม่มีปุ่มที่กำลังกดค้าง
static xkb_keysym_t g_repSym = 0;
static std::string g_repText;
static int64_t g_repNext = 0;
static int g_repRate = 25, g_repDelay = 400;  // ค่าเริ่มต้น (compositor ส่ง repeat_info มาทับ)

static bool ModActive(const char* name) {
    return g_xkbState && xkb_state_mod_name_is_active(g_xkbState, name, XKB_STATE_MODS_EFFECTIVE) > 0;
}
static bool ShiftDown() { return ModActive(XKB_MOD_NAME_SHIFT); }
static bool CtrlDown()  { return ModActive(XKB_MOD_NAME_CTRL); }

static void HandleKey(xkb_keysym_t ks, const std::string& text) {
    g_needRedraw = g_extra = true;
    if (ks == XKB_KEY_Escape) g_escape = true;
    else if (ks == XKB_KEY_Return || ks == XKB_KEY_KP_Enter) g_enter = true;
    else if (ks == XKB_KEY_Tab) FocusNext(false);
    else if (ks == XKB_KEY_ISO_Left_Tab) FocusNext(true);
    else if (ks == XKB_KEY_BackSpace) { if (g_screen == S_CALC) CalcDelete(); else if (g_focusStr && !g_focusStr->empty()) g_focusStr->pop_back(); }
    else if (ks == XKB_KEY_Delete) { if (g_screen == S_CALC) CalcClear(); else if (g_focusStr) g_focusStr->clear(); }
    else if (ks == XKB_KEY_Up) g_wheelKey = -1;
    else if (ks == XKB_KEY_Down) g_wheelKey = 1;
    else if (ks == XKB_KEY_Left) g_hKey = -1;
    else if (ks == XKB_KEY_Right) g_hKey = 1;
    else if (CtrlDown() && (ks == XKB_KEY_v || ks == XKB_KEY_V)) RequestPaste();
    else if (ks == XKB_KEY_Insert && ShiftDown()) RequestPaste();
    else if (ks == XKB_KEY_Prior) g_wheelKey = -8;
    else if (ks == XKB_KEY_Next) g_wheelKey = 8;
    else if (!CtrlDown() && text.size() == 1 && isprint((unsigned char)text[0])) TypeChar(text[0]);
}
static void KbKeymap(void*, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size) {
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) { close(fd); return; }
    char* map = (char*)mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) { close(fd); return; }
    xkb_keymap* km = xkb_keymap_new_from_string(g_xkbCtx, map, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map, size);
    close(fd);
    if (!km) return;
    xkb_state* st = xkb_state_new(km);
    if (!st) { xkb_keymap_unref(km); return; }
    if (g_xkbState) xkb_state_unref(g_xkbState);
    if (g_xkbMap) xkb_keymap_unref(g_xkbMap);
    g_xkbMap = km; g_xkbState = st;
}
static void KbEnter(void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*) { g_needRedraw = true; }
static void KbLeave(void*, wl_keyboard*, uint32_t, wl_surface*) { g_repKey = 0; }
static void KbKey(void*, wl_keyboard*, uint32_t, uint32_t, uint32_t key, uint32_t state) {
    if (!g_xkbState || !g_xkbMap) return;
    xkb_keycode_t code = key + 8;
    if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        xkb_keysym_t sym = xkb_state_key_get_one_sym(g_xkbState, code);
        char buf[16];
        int n = xkb_state_key_get_utf8(g_xkbState, code, buf, sizeof buf);
        std::string text = (n > 0 && n < (int)sizeof buf) ? std::string(buf, (size_t)n) : std::string();
        HandleKey(sym, text);
        if (g_repRate > 0 && xkb_keymap_key_repeats(g_xkbMap, code)) {
            g_repKey = key; g_repSym = sym; g_repText = text; g_repNext = NowMs() + g_repDelay;
        }
    } else if (key == g_repKey) {
        g_repKey = 0;
    }
}
static void KbModifiers(void*, wl_keyboard*, uint32_t, uint32_t dep, uint32_t lat, uint32_t loc, uint32_t grp) {
    if (g_xkbState) xkb_state_update_mask(g_xkbState, dep, lat, loc, 0, 0, grp);
}
static void KbRepeatInfo(void*, wl_keyboard*, int32_t rate, int32_t delay) { g_repRate = rate; g_repDelay = delay; }
static const wl_keyboard_listener g_kbListener = { KbKeymap, KbEnter, KbLeave, KbKey, KbModifiers, KbRepeatInfo };

// Wayland ไม่ทำ auto-repeat ให้ — จำลองเองเหมือนที่ X server เคยทำ
static int RepeatTimeoutMs() {
    if (!g_repKey || g_repRate <= 0) return -1;
    int64_t d = g_repNext - NowMs();
    return d < 0 ? 0 : (int)d;
}
static void RepeatTick() {
    if (!g_repKey || g_repRate <= 0) return;
    int64_t now = NowMs(), period = std::max(1, 1000 / g_repRate);
    for (int guard = 0; g_repKey && now >= g_repNext && guard < 5; ++guard) {
        HandleKey(g_repSym, g_repText);
        g_repNext += period;
    }
    if (g_repKey && now >= g_repNext) g_repNext = now + period;
}

// ---------- เมาส์ ----------
static wl_pointer* g_ptr = nullptr;
static bool g_ptrFocus = false;               // เมาส์อยู่บนพื้นที่เนื้อหา (ไม่ใช่ขอบหน้าต่างของ libdecor)
static uint32_t g_ptrSerial = 0;
static wl_surface* g_cursorSurf = nullptr;
static wl_cursor_image* g_cursorImg = nullptr;
static double g_axisAcc[2] = {0, 0};
static int g_axisDisc[2] = {0, 0};

static void SetCursor(wl_pointer* p) {
    if (g_cursorSurf && g_cursorImg)
        wl_pointer_set_cursor(p, g_ptrSerial, g_cursorSurf, (int32_t)g_cursorImg->hotspot_x, (int32_t)g_cursorImg->hotspot_y);
}
static void PtrEnter(void*, wl_pointer* p, uint32_t serial, wl_surface* s, wl_fixed_t sx, wl_fixed_t sy) {
    g_ptrFocus = (s == g_surf);
    if (!g_ptrFocus) return;
    g_ptrSerial = serial;
    ui.mx = wl_fixed_to_int(sx); ui.my = wl_fixed_to_int(sy);
    SetCursor(p);
    g_needRedraw = true;
}
static void PtrLeave(void*, wl_pointer*, uint32_t, wl_surface* s) { if (s == g_surf) g_ptrFocus = false; }
static void PtrMotion(void*, wl_pointer*, uint32_t, wl_fixed_t sx, wl_fixed_t sy) {
    if (!g_ptrFocus) return;
    ui.mx = wl_fixed_to_int(sx); ui.my = wl_fixed_to_int(sy);
    g_needRedraw = true;
}
static void PtrButton(void*, wl_pointer*, uint32_t, uint32_t, uint32_t button, uint32_t state) {
    if (!g_ptrFocus) return;
    bool down = (state == WL_POINTER_BUTTON_STATE_PRESSED);
    if (button == BTN_LEFT) {
        if (down) ui.down = ui.pressed = true;
        else { ui.down = false; ui.released = true; g_extra = true; }
    } else if (button == BTN_MIDDLE && down) {
        RequestPaste();       // X11 เดิมวาง PRIMARY; Wayland แกนหลักไม่มี primary selection จึงวางคลิปบอร์ดแทน
    }
    g_needRedraw = true;
}
static void FlushAxis() {
    int step[2] = {0, 0};
    for (int a = 0; a < 2; ++a) {
        if (g_axisDisc[a]) step[a] = g_axisDisc[a] > 0 ? 1 : -1;
        else if (g_axisAcc[a] >= 10.0 || g_axisAcc[a] <= -10.0) step[a] = g_axisAcc[a] > 0 ? 1 : -1;
        if (step[a]) g_axisAcc[a] = 0;
        g_axisDisc[a] = 0;
    }
    if (step[0]) { if (ShiftDown()) g_wheelH = step[0]; else g_wheel = step[0]; }
    if (step[1]) g_wheelH = step[1];
    if (step[0] || step[1]) g_needRedraw = true;
}
static void PtrAxis(void* , wl_pointer* p, uint32_t, uint32_t axis, wl_fixed_t value) {
    if (!g_ptrFocus || axis > 1) return;
    g_axisAcc[axis] += wl_fixed_to_double(value);
    if (wl_pointer_get_version(p) < 5) FlushAxis();       // ไม่มี event frame ใน wl_pointer เวอร์ชันเก่า
}
static void PtrFrame(void*, wl_pointer*) { if (g_ptrFocus) FlushAxis(); else { g_axisDisc[0] = g_axisDisc[1] = 0; } }
static void PtrAxisSource(void*, wl_pointer*, uint32_t) {}
static void PtrAxisStop(void*, wl_pointer*, uint32_t, uint32_t axis) { if (axis < 2) g_axisAcc[axis] = 0; }
static void PtrAxisDiscrete(void*, wl_pointer*, uint32_t axis, int32_t discrete) { if (axis < 2) g_axisDisc[axis] += discrete; }
static const wl_pointer_listener g_ptrListener = { PtrEnter, PtrLeave, PtrMotion, PtrButton, PtrAxis, PtrFrame,
                                                   PtrAxisSource, PtrAxisStop, PtrAxisDiscrete };

static void SetupCursor() {
    int size = 24;
    if (const char* s = getenv("XCURSOR_SIZE")) { int v = atoi(s); if (v > 0) size = v; }
    wl_cursor_theme* th = wl_cursor_theme_load(getenv("XCURSOR_THEME"), size, g_shm);
    if (!th) return;
    wl_cursor* c = wl_cursor_theme_get_cursor(th, "left_ptr");
    if (!c) c = wl_cursor_theme_get_cursor(th, "default");
    if (!c || c->image_count == 0) return;
    g_cursorImg = c->images[0];
    g_cursorSurf = wl_compositor_create_surface(g_compositor);
    wl_surface_attach(g_cursorSurf, wl_cursor_image_get_buffer(g_cursorImg), 0, 0);
    wl_surface_damage(g_cursorSurf, 0, 0, (int32_t)g_cursorImg->width, (int32_t)g_cursorImg->height);
    wl_surface_commit(g_cursorSurf);
}

// ---------- seat / registry ----------
static void SeatCaps(void*, wl_seat* seat, uint32_t caps) {
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !g_ptr) {
        g_ptr = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(g_ptr, &g_ptrListener, nullptr);
    } else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && g_ptr) {
        if (wl_pointer_get_version(g_ptr) >= 3) wl_pointer_release(g_ptr); else wl_pointer_destroy(g_ptr);
        g_ptr = nullptr; g_ptrFocus = false;
    }
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !g_kb) {
        g_kb = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(g_kb, &g_kbListener, nullptr);
    } else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && g_kb) {
        if (wl_keyboard_get_version(g_kb) >= 3) wl_keyboard_release(g_kb); else wl_keyboard_destroy(g_kb);
        g_kb = nullptr; g_repKey = 0;
    }
}
static void SeatName(void*, wl_seat*, const char*) {}
static const wl_seat_listener g_seatListener = { SeatCaps, SeatName };

static void RegGlobal(void*, wl_registry* reg, uint32_t name, const char* iface, uint32_t ver) {
    if (!strcmp(iface, wl_compositor_interface.name))
        g_compositor = (wl_compositor*)wl_registry_bind(reg, name, &wl_compositor_interface, std::min(ver, 4u));
    else if (!strcmp(iface, wl_shm_interface.name))
        g_shm = (wl_shm*)wl_registry_bind(reg, name, &wl_shm_interface, 1);
    else if (!strcmp(iface, wl_seat_interface.name) && !g_seat) {
        g_seat = (wl_seat*)wl_registry_bind(reg, name, &wl_seat_interface, std::min(ver, 5u));
        wl_seat_add_listener(g_seat, &g_seatListener, nullptr);
    } else if (!strcmp(iface, wl_data_device_manager_interface.name))
        g_ddm = (wl_data_device_manager*)wl_registry_bind(reg, name, &wl_data_device_manager_interface, std::min(ver, 3u));
}
static void RegGlobalRemove(void*, wl_registry*, uint32_t) {}
static const wl_registry_listener g_regListener = { RegGlobal, RegGlobalRemove };

// ---------- หน้าต่าง (libdecor = ไตเติลบาร์/ขอบ/ย่อขยาย ที่ใช้ได้ทุก compositor) ----------
static void DecError(libdecor*, enum libdecor_error err, const char* msg) {
    fprintf(stderr, "libdecor error %d: %s\n", (int)err, msg);
    g_running = false;
}
static libdecor_interface g_decIface = { DecError };

static const int W0 = 1040, H0 = 800, MINW = 940, MINH = 700;
static void FrameConfigure(libdecor_frame* frame, libdecor_configuration* config, void*) {
    int w = 0, h = 0;
    if (!libdecor_configuration_get_content_size(config, frame, &w, &h) || w <= 0 || h <= 0) {
        w = g_w > 0 ? g_w : W0;
        h = g_h > 0 ? g_h : H0;
    }
    if (w != g_w || h != g_h || !g_px) CreateBuffer(w, h);
    libdecor_state* st = libdecor_state_new(w, h);
    libdecor_frame_commit(frame, st, config);
    libdecor_state_free(st);
    g_configured = true;
    g_needRedraw = true;
}
static void FrameClose(libdecor_frame*, void*) { g_running = false; }
static void FrameCommit(libdecor_frame*, void*) { if (g_surf) wl_surface_commit(g_surf); }
static void FrameDismissPopup(libdecor_frame*, const char*, void*) {}
static libdecor_frame_interface g_frameIface = { FrameConfigure, FrameClose, FrameCommit, FrameDismissPopup };

int main_gui() {
    g_wl = wl_display_connect(nullptr);
    if (!g_wl) {
        fprintf(stderr, "Cannot connect to a Wayland display (is WAYLAND_DISPLAY set? are you in a Wayland session?)\n");
        return 1;
    }
    g_registry = wl_display_get_registry(g_wl);
    wl_registry_add_listener(g_registry, &g_regListener, nullptr);
    wl_display_roundtrip(g_wl);
    if (!g_compositor || !g_shm) { fprintf(stderr, "Compositor lacks wl_compositor / wl_shm\n"); return 1; }

    g_xkbCtx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (g_ddm && g_seat) {
        g_dataDev = wl_data_device_manager_get_data_device(g_ddm, g_seat);
        wl_data_device_add_listener(g_dataDev, &g_ddListener, nullptr);
    }
    SetupCursor();
    wl_display_roundtrip(g_wl);      // รับ capabilities ของ seat
    wl_display_roundtrip(g_wl);      // รับ keymap / repeat_info

    InitFonts();
    BuildCatalog();
    CreateBuffer(W0, H0);
    if (!g_running) return 1;

    g_surf = wl_compositor_create_surface(g_compositor);
    g_dec = libdecor_new(g_wl, &g_decIface);
    if (!g_dec) { fprintf(stderr, "libdecor_new failed\n"); return 1; }
    g_frame = libdecor_decorate(g_dec, g_surf, &g_frameIface, nullptr);
    if (!g_frame) { fprintf(stderr, "libdecor_decorate failed\n"); return 1; }
    libdecor_frame_set_app_id(g_frame, "maths-solver");
    libdecor_frame_set_title(g_frame, "Maths Solver System");
    libdecor_frame_set_min_content_size(g_frame, MINW, MINH);
    libdecor_frame_map(g_frame);

    while (g_running) {
        if (libdecor_dispatch(g_dec, RepeatTimeoutMs()) < 0) break;
        wl_display_dispatch_pending(g_wl);
        RepeatTick();
        if (g_running && g_configured && g_needRedraw) {
            bool extra = g_extra;
            g_needRedraw = g_extra = false;
            Present();
            if (extra) Present();      // วาดซ้ำเพื่อให้เห็นผลของการคลิก/พิมพ์ทันที
        }
    }

    DestroyOffer(g_clip);
    DestroyOffer(g_dndOffer);
    DestroyBuffers();
    if (g_xkbState) xkb_state_unref(g_xkbState);
    if (g_xkbMap) xkb_keymap_unref(g_xkbMap);
    if (g_xkbCtx) xkb_context_unref(g_xkbCtx);
    if (g_frame) libdecor_frame_unref(g_frame);
    if (g_dec) libdecor_unref(g_dec);
    if (g_surf) wl_surface_destroy(g_surf);
    wl_display_disconnect(g_wl);
    return 0;
}