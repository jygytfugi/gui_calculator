#include "lib/algebra.hpp"
#include "lib/Alpha_solver_eq.hpp"
#include "lib/Calculator_calculus.hpp"
#include "lib/function_solver.hpp"
#include "lib/math_graph.hpp"
#include "lib/matrix.hpp"
#include "lib/menu.hpp"
#include "lib/table_calculate.hpp"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
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

// ---------- X11 / framebuffer state ----------
static Display* g_dpy = nullptr;
static Window g_win;
static GC g_gc;
static XFontStruct* g_fontS = nullptr;   // ฟอนต์ปุ่ม/ช่องกรอก
static XFontStruct* g_fontM = nullptr;   // ฟอนต์ monospace สำหรับผลลัพธ์/กราฟ
static XFontStruct* g_fontL = nullptr;   // ฟอนต์ใหญ่ (หัวข้อ)
static XImage* g_img = nullptr;
static uint32_t* g_px = nullptr;
static int g_w = 0, g_h = 0;

static void CreateBuffer(int w, int h) {
    if (w <= 0 || h <= 0) return;
    if (g_img) XDestroyImage(g_img);               // also frees g_px
    g_px = (uint32_t*)calloc((size_t)w * h, 4);
    int screen = DefaultScreen(g_dpy);
    g_img = XCreateImage(g_dpy, DefaultVisual(g_dpy, screen), DefaultDepth(g_dpy, screen),
                         ZPixmap, 0, (char*)g_px, w, h, 32, 0);
    g_w = w; g_h = h;
}

// ---------- Drawing primitives ----------
struct TextCmd { int x, y; std::string s; uint32_t c; XFontStruct* f; };
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
static int TextW(XFontStruct* f, const std::string& s) {
    return f ? XTextWidth(f, s.c_str(), (int)s.size()) : (int)s.size() * 8;
}
static int FontH(XFontStruct* f) { return f ? f->ascent + f->descent : 14; }
static int CharW(XFontStruct* f) { return f ? std::max(1, (int)f->max_bounds.width) : 8; }

static void DrawText_(int x, int y, const std::string& s, uint32_t c, XFontStruct* f) {
    if (!s.empty()) g_text.push_back({x, y, s, c, f});    // y = ขอบบนของข้อความ
}
static void DrawTextCenter(int cx, int cy, const std::string& s, uint32_t c, XFontStruct* f) {
    DrawText_(cx - TextW(f, s) / 2, cy - FontH(f) / 2, s, c, f);
}
static void DrawTextRight(int xr, int y, const std::string& s, uint32_t c, XFontStruct* f) {
    DrawText_(xr - TextW(f, s), y, s, c, f);
}
static std::string FitTail(XFontStruct* f, std::string s, int maxW) {
    while (!s.empty() && TextW(f, s) > maxW) s.erase(0, 1);
    return s;
}
static std::string FitHead(XFontStruct* f, std::string s, int maxW) {
    while (!s.empty() && TextW(f, s) > maxW) s.pop_back();
    return s;
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
                       XFontStruct* font = nullptr, bool selected = false) {
    int id = NextId();
    bool over = Hit(x, y, w, h);
    if (over && ui.pressed) ui.active = id;
    bool clicked = ui.released && ui.active == id && over;
    uint32_t bg = (ui.active == id && ui.down && over) ? Shade(base, -25)
                : over ? Shade(base, 20) : base;
    FillRectPx(x, y, w, h, bg);
    Border(x, y, w, h, selected ? RGB32(255, 255, 255) : Shade(base, -50));
    if (selected) Border(x + 1, y + 1, w - 2, h - 2, RGB32(255, 255, 255));
    XFontStruct* f = font ? font : g_fontS;
    if (TextW(f, label) > w - 6 && f == g_fontS) f = g_fontM;
    DrawTextCenter(x + w / 2, y + h / 2, FitHead(f, label, w - 4), RGB32(255, 255, 255), f);
    return clicked;
}

// ---------- ช่องกรอกข้อความ + ระบบโฟกัส ----------
static std::string* g_focusStr = nullptr;
static std::vector<std::string*> g_tabNow, g_tabPrev;
static void ResetFocus() { g_focusStr = nullptr; }

static bool TextBox(int x, int y, int w, int h, std::string& s, const std::string& hint,
                    XFontStruct* f = nullptr) {
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
static int  g_wheel = 0, g_wheelKey = 0;
static bool g_enter = false, g_escape = false;
static char g_pendingChar = 0;
static std::vector<std::function<void()>> g_deferred;
static void Defer(std::function<void()> fn) { g_deferred.push_back(std::move(fn)); }

// ============================================================
//                    ตัวช่วยทั่วไป
// ============================================================
static Calculator g_calc;
static FunctionSolver g_fs(g_calc);
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
static double EvalNum(const std::string& s) { return g_calc.evaluate(ReplaceAns(s)); }
static std::vector<double> ParseList(const std::string& s, const char* what = "numbers") {
    std::vector<double> v;
    for (auto& t : SplitTop(s, ", ;\t")) v.push_back(EvalNum(t));
    if (v.empty()) throw std::runtime_error(std::string("Please enter ") + what + " separated by commas");
    return v;
}
// ตัวแปรหลายตัวแปรเดียวกันใน Calculator ผูกกับ x; แปลงตัวแปรอื่นให้เป็น x ถ้าทำได้
static std::string Norm(const std::string& expr) {
    std::string e = ReplaceAns(expr);
    try { return AlphaSolverEq::normalizeVariable(e); } catch (...) { return e; }
}
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
static std::string FormatTable(const DataTable& t) {
    std::string out;
    char buf[96];
    for (int c = 0; c < t.getCols(); ++c) { snprintf(buf, sizeof buf, "%-14s", t.getHeader(c).c_str()); out += buf; }
    out += "\n";
    for (int r = 0; r < t.getRows(); ++r) {
        for (int c = 0; c < t.getCols(); ++c) { snprintf(buf, sizeof buf, "%-14.8g", t.getCell(r, c)); out += buf; }
        out += "\n";
    }
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
                        int& scroll, bool wrap = true) {
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
    auto lines = BuildLines(show, cols, isErr ? true : wrap);
    int maxScroll = std::max(0, (int)lines.size() - rows);
    if (Hit(x, y, w, h) && g_wheel) { scroll += g_wheel * 3; g_wheel = 0; }
    if (g_wheelKey) { scroll += g_wheelKey; g_wheelKey = 0; }
    scroll = std::clamp(scroll, 0, maxScroll);
    for (int i = 0; i < rows && i + scroll < (int)lines.size(); ++i)
        DrawText_(x + 8, y + 4 + i * fh, lines[i + scroll], isErr ? COL_RED : COL_GREEN, g_fontM);
    if (maxScroll > 0) {
        if (DrawButton(x + w - 20, y + 2, 18, 18, "^", COL_CHIP, g_fontM)) scroll = std::max(0, scroll - 3);
        if (DrawButton(x + w - 20, y + h - 20, 18, 18, "v", COL_CHIP, g_fontM)) scroll = std::min(maxScroll, scroll + 3);
        int trackY = y + 24, trackH = h - 48;
        FillRectPx(x + w - 14, trackY, 6, trackH, RGB32(40, 44, 54));
        int thumbH = std::max(14, trackH * rows / (int)lines.size());
        int thumbY = trackY + (trackH - thumbH) * scroll / maxScroll;
        FillRectPx(x + w - 14, thumbY, 6, thumbH, RGB32(110, 120, 140));
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
static double Num(Feature& f, int i) { return g_calc.evaluate(Req(f, i)); }
static double NumOr(Feature& f, int i, double d) { return Opt(f, i).empty() ? d : g_calc.evaluate(Opt(f, i)); }
static char VarOf(Feature& f, int i) { std::string s = Opt(f, i); return s.empty() ? '\0' : s[0]; }
static int Pick(Feature& f, int i) { return f.fields[i].choice; }
static std::vector<double> ParseSet(const std::string& s) {
    std::string t = s;
    for (char& c : t) if (c == '{' || c == '}') c = ' ';
    return ParseList(t, "set elements");
}

// ---------- กราฟ ----------
static std::string g_gxMinS = "-10", g_gxMaxS = "10", g_gwS, g_ghS;
static std::string GraphOut(const std::function<void(MathGraph&, double, double)>& fn) {
    double x0 = EvalNum(g_gxMinS.empty() ? "-10" : g_gxMinS), x1 = EvalNum(g_gxMaxS.empty() ? "10" : g_gxMaxS);
    if (!(x0 < x1)) throw std::runtime_error("xMin must be less than xMax");
    int W = g_gwS.empty() ? std::max(20, g_resCols - 3) : (int)EvalNum(g_gwS);
    int H = g_ghS.empty() ? std::max(8, g_resRows - 6) : (int)EvalNum(g_ghS);
    W = std::clamp(W, 20, 300); H = std::clamp(H, 8, 120);
    MathGraph g(W, H);
    std::string out;
    {
        CoutCapture cap;
        try { fn(g, x0, x1); }
        catch (const std::exception& e) { cap.ss << "[Error] " << e.what() << "\n"; }
        out = cap.ss.str();
    }
    return CleanAscii(out);
}
static void GraphSetRange(double x0, double x1) { g_gxMinS = Fmt(x0); g_gxMaxS = Fmt(x1); }

// ---------- สร้างแค็ตตาล็อกฟีเจอร์ ----------
static const char* DIST_NAMES[] = {"Binomial PDF", "Binomial CDF", "Poisson PDF", "Poisson CDF",
                                   "Hypergeometric", "Geometric", "Normal PDF", "Normal CDF"};
static const char* DIST_LABELS[8][4] = {
    {"k", "n", "p", ""}, {"k", "n", "p", ""}, {"k", "lambda", "", ""}, {"k", "lambda", "", ""},
    {"k", "N (population)", "K (successes)", "n (draws)"}, {"k", "p", "", ""},
    {"x", "mean (default 0)", "stdev (default 1)", ""}, {"x", "mean (default 0)", "stdev (default 1)", ""}};

static void BuildCatalog() {
    // ===================== [1] Algebra =====================
    {
        Category c{"Algebra Calculator", "Continuous calc, ans memory, all functions", {}};
        c.features.push_back(MkLab("Algebra Calculator", "", FK_CALC, 0));
        g_cats.push_back(c);
    }
    // ===================== [2] Calculus =====================
    {
        Category c{"Calculus Solver", "Derivative, Integral, Limit (numeric & symbolic)", {}};
        c.features.push_back(MkF("Derivative  d/dx f(x) at x0", "Numeric derivative of f at a point.",
            {TF("f(x)", "e.g. x^2 + sin(x)"), TF("x0  (point)", "e.g. 2")},
            [](Feature& f) -> std::string {
                std::string e = Norm(Req(f, 0)); double x0 = Num(f, 1);
                double v = g_calc.derivative(e, x0); SetAns(v);
                return "f'(" + Fmt(x0) + ") = " + Fmt(v);
            }, {{"x^2+sin(x)", "2"}, {"e^x", "0"}}));
        c.features.push_back(MkF("Definite / Improper Integral", "Integral of f over [a, b]. Use inf / -inf for improper integrals.",
            {TF("f(x)", "e.g. x^2"), TF("a  (lower)", "e.g. 0"), TF("b  (upper)", "e.g. 3  or  inf")},
            [](Feature& f) -> std::string {
                std::string e = Norm(Req(f, 0)); double a = Num(f, 1), b = Num(f, 2);
                double v = g_calc.integral(e, a, b); SetAns(v);
                return "Integral [" + Fmt(a) + " -> " + Fmt(b) + "] f(x) dx = " + Fmt(v);
            }, {{"x^2", "0", "3"}, {"1/(x^2+1)", "0", "inf"}, {"e^(-x)", "0", "inf"}}));
        c.features.push_back(MkF("Indefinite Integral F(x)", "F(x0) = integral of f from x_ref to x0.",
            {TF("f(x)", "e.g. 2x"), TF("x0  (evaluate at)", "e.g. 3"), TF("x_ref  (start, default 0)", "0", "0")},
            [](Feature& f) -> std::string {
                std::string e = Norm(Req(f, 0)); double x0 = Num(f, 1), xr = NumOr(f, 2, 0.0);
                double v = g_calc.indefiniteIntegral(e, x0, xr); SetAns(v);
                return "F(" + Fmt(x0) + ") = Integral[" + Fmt(xr) + " -> " + Fmt(x0) + "] f(x) dx = " + Fmt(v);
            }, {{"2*x", "3", "0"}}));
        c.features.push_back(MkF("Limit  lim x->a f(x)", "Two-sided or one-sided limit. a may be inf / -inf.",
            {TF("f(x)", "e.g. sin(x)/x"), TF("a  (x tends to)", "e.g. 0"), CF("Direction", {"both sides", "right (a+)", "left (a-)"})},
            [](Feature& f) -> std::string {
                std::string e = Norm(Req(f, 0)); double a = Num(f, 1);
                char side = Pick(f, 2) == 1 ? '+' : Pick(f, 2) == 2 ? '-' : '\0';
                double v = g_calc.limit(e, a, 1e-6, side); SetAns(v);
                return "lim x->" + Fmt(a) + (side ? std::string(1, side) : std::string()) + " f(x) = " + Fmt(v);
            }, {{"sin(x)/x", "0", "0"}, {"1/x", "0", "1"}}));
        c.features.push_back(MkF("Symbolic Derivative  f'(x)", "Returns f'(x) as a formula.",
            {TF("f(x)", "e.g. x^2 + sin(x)"), TF("Variable (blank = x)", "x")},
            [](Feature& f) -> std::string {
                char v = VarOf(f, 1); if (!v) v = 'x';
                return "f'(" + std::string(1, v) + ") = " + g_calc.symbolicDerivative(Req(f, 0), v);
            }, {{"x^2+sin(x)", ""}, {"x*e^x", ""}}));
        c.features.push_back(MkF("Symbolic Indefinite Integral  F(x)", "Returns F(x) as a formula (without + C).",
            {TF("f(x)", "e.g. x^2 + sin(x)"), TF("Variable (blank = x)", "x")},
            [](Feature& f) -> std::string {
                char v = VarOf(f, 1); if (!v) v = 'x';
                return "F(" + std::string(1, v) + ") = " + g_calc.symbolicIntegral(Req(f, 0), v) + " + C";
            }, {{"x^2+sin(x)", ""}, {"1/(x^2+1)", ""}}));
        g_cats.push_back(c);
    }
    // ===================== [3] Equation Solver =====================
    {
        Category c{"Alpha Equation Solver", "Polynomial, factorization, f(x)=0, systems of equations", {}};
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
        c.features.push_back(MkF("Polynomial Factorization", "Factor a polynomial into linear/quadratic factors.",
            {TF("Coefficients", "1 -3 2"), TF("Variable (blank = x)", "x")},
            [](Feature& f) -> std::string {
                auto co = ParseList(Req(f, 0), "coefficients");
                char v = VarOf(f, 1); if (!v) v = 'x';
                return g_calc.solver().factorizePolynomial(co, v);
            }, {{"1 -6 11 -6", ""}}));
        c.features.push_back(MkF("General Equation  f(x) = 0", "Numeric root finder (Newton).",
            {TF("f(x)  (= 0)", "e.g. cos(x) - x"), TF("Initial guess", "1", "1"), TF("Variable (blank = auto)", "x")},
            [](Feature& f) -> std::string {
                double v = g_calc.solver().solveGeneralEquation(g_calc, Req(f, 0), NumOr(f, 1, 1.0), 1000, 1e-7, VarOf(f, 2));
                SetAns(v); return "x = " + Fmt(v);
            }, {{"cos(x)-x", "1", ""}, {"x^3-2x-5", "2", ""}}));
        c.features.push_back(MkF("Derivative Equation  f'(x) = target", "Find x where f'(x) equals a value (target 0 = critical point).",
            {TF("f(x)", "e.g. x^3 - 3x"), TF("Target value", "0", "0"), TF("Initial guess", "1", "1"), TF("Variable (blank = auto)", "x")},
            [](Feature& f) -> std::string {
                double v = g_calc.solver().solveDerivativeEq(g_calc, Req(f, 0), NumOr(f, 1, 0.0), NumOr(f, 2, 1.0), 1000, 1e-7, VarOf(f, 3));
                SetAns(v); return "x = " + Fmt(v);
            }, {{"x^3-3x", "0", "2", ""}}));
        c.features.push_back(MkF("Integral Bound  Integral[a..b] f dx = target", "Find the upper bound b.",
            {TF("f(x)", "e.g. 2x"), TF("a  (lower bound)", "0", "0"), TF("Target value", "e.g. 9"), TF("Initial guess", "1", "1"), TF("Variable (blank = auto)", "x")},
            [](Feature& f) -> std::string {
                double v = g_calc.solver().solveIntegralBound(g_calc, Req(f, 0), NumOr(f, 1, 0.0), Num(f, 2), NumOr(f, 3, 1.0), 1000, 1e-7, VarOf(f, 4));
                SetAns(v); return "b = " + Fmt(v);
            }, {{"2x", "0", "9", "1", ""}}));
        c.features.push_back(MkF("Limit Equation  lim x->a f(x) = target", "Find a such that the limit equals target.",
            {TF("f(x)", "e.g. (x^2-1)/(x-1) - 5"), TF("Target value", "e.g. 0"), TF("Initial guess", "1", "1"), TF("Variable (blank = auto)", "x")},
            [](Feature& f) -> std::string {
                double v = g_calc.solver().solveLimitEq(g_calc, Req(f, 0), Num(f, 1), NumOr(f, 2, 1.0), 1000, 1e-7, VarOf(f, 3));
                SetAns(v); return "a = " + Fmt(v);
            }, {{"x^2", "9", "2", ""}}));
        c.features.push_back(MkF("Indefinite Integral Equation  F(x) = target", "Find x where F(x) (integral from x_ref) equals target.",
            {TF("f(x)", "e.g. 2x"), TF("Target value", "e.g. 16"), TF("x_ref (default 0)", "0", "0"), TF("Initial guess", "1", "1"), TF("Variable (blank = auto)", "x")},
            [](Feature& f) -> std::string {
                double v = g_calc.solver().solveIndefiniteIntegralEq(g_calc, Req(f, 0), Num(f, 1), NumOr(f, 2, 0.0), NumOr(f, 3, 1.0), 1000, 1e-7, VarOf(f, 4));
                SetAns(v); return "x = " + Fmt(v);
            }, {{"2x", "16", "0", "1", ""}}));
        c.features.push_back(MkF("Two-Sided Equation  lhs = rhs", "Any equation; use ans for your last result.",
            {TF("Equation", "e.g. x^2 + 2 = 3x"), TF("Initial guess", "1", "1")},
            [](Feature& f) -> std::string {
                g_calc.solver().setAns(g_ans);
                double v = g_calc.solver().solveTwoSideEquation(g_calc, Req(f, 0), NumOr(f, 1, 1.0));
                SetAns(v); return "x = " + Fmt(v);
            }, {{"x^2+2=3x", "1"}, {"x + 2 = ans", "1"}}));
        {
            Feature f = MkF("Linear System  (any # of equations / variables)",
                "One equation per box. Variables are detected automatically (x, y, z, ...). Use [+ Add] for more equations.",
                {TF("Equation 1", "e.g. 2x + 3y = 8"), TF("Equation 2", "e.g. x - y = 1")},
                [](Feature& f) -> std::string {
                    std::vector<std::string> eqs;
                    for (auto& fd : f.fields) { std::string s = ReplaceAns(Trim(fd.value)); if (!s.empty()) eqs.push_back(s); }
                    if (eqs.empty()) throw std::runtime_error("Please enter at least one equation");
                    auto sol = g_calc.solver().solveLinearSystem(g_calc, eqs);
                    std::string t = std::to_string(eqs.size()) + " equation(s), " + std::to_string(sol.size()) + " variable(s):\n";
                    for (auto& kv : sol) t += "  " + std::string(1, kv.first) + " = " + Fmt(kv.second) + "\n";
                    if (!sol.empty()) SetAns(sol.begin()->second);
                    return t;
                }, {{"2x+3y=8", "x-y=1"}, {"x+y+z=6", "2x-y+z=3", "x+2y-z=2"}});
            f.dynLabel = "Equation"; f.dynStart = 0; f.dynMin = 1; f.dynMax = 10;
            c.features.push_back(f);
        }
        {
            Feature f = MkF("Nonlinear System  (any # of equations / variables)",
                "Newton / Gauss-Newton. Optionally give initial guesses like  x=1, y=2.",
                {TF("Initial guess (optional)", "x=1, y=2"), TF("Equation 1", "e.g. x^2 + y^2 = 25"), TF("Equation 2", "e.g. x - y = 1")},
                [](Feature& f) -> std::string {
                    std::vector<std::string> eqs;
                    for (size_t i = 1; i < f.fields.size(); ++i) { std::string s = ReplaceAns(Trim(f.fields[i].value)); if (!s.empty()) eqs.push_back(s); }
                    if (eqs.empty()) throw std::runtime_error("Please enter at least one equation");
                    std::map<char, double> guess;
                    if (!Opt(f, 0).empty()) guess = ParseGuess(Opt(f, 0));
                    auto sol = g_calc.solver().solveNonlinearSystem(g_calc, eqs, guess);
                    std::string t = "Solution:\n";
                    for (auto& kv : sol) t += "  " + std::string(1, kv.first) + " = " + Fmt(kv.second) + "\n";
                    t += "(Different initial guesses may reveal other solutions.)";
                    if (!sol.empty()) SetAns(sol.begin()->second);
                    return t;
                }, {{"x=3,y=3", "x^2+y^2=25", "x-y=1"}});
            f.dynLabel = "Equation"; f.dynStart = 1; f.dynMin = 1; f.dynMax = 10;
            c.features.push_back(f);
        }
        g_cats.push_back(c);
    }
    // ===================== [4] Graph =====================
    {
        Category c{"2D Text Graph Plotter", "9 plot types, zoom in/out, pan, resolution", {}};
        auto G = [](Feature f) { f.isGraph = true; return f; };
        c.features.push_back(G(MkF("Plot f(x)", "Plot a function.",
            {TF("f(x)", "e.g. sin(x)  or  x^2 - 4")},
            [](Feature& f) -> std::string {
                std::string e = Norm(Req(f, 0));
                return GraphOut([e](MathGraph& g, double a, double b) { g.plotFunction(g_calc, e, a, b); });
            }, {{"sin(x)"}, {"x^2-4"}, {"x^3-3x"}})));
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
                std::string out = GraphOut([e](MathGraph& g, double a, double b) { g.plotFunction(g_calc, e, a, b); });
                auto roots = g_calc.solver().solvePolynomial(use);
                out += "\nRoots:";
                for (auto& z : roots) out += "   " + FmtComplex(z);
                return out;
            }, {{"1 -3 2"}, {"1 0 -4"}, {"1 0 1"}})));
        c.features.push_back(G(MkF("Plot Derivative & Tangent at x0", "Graph of f with its tangent line at x0.",
            {TF("f(x)", "e.g. x^2"), TF("x0", "e.g. 1")},
            [](Feature& f) -> std::string {
                std::string e = Norm(Req(f, 0)); double x0 = Num(f, 1);
                return GraphOut([e, x0](MathGraph& g, double a, double b) { g.plotDerivative(g_calc, e, x0, a, b); });
            }, {{"x^2", "1"}})));
        c.features.push_back(G(MkF("Plot Integral Area over [a, b]", "Shaded area under f.",
            {TF("f(x)", "e.g. x^2"), TF("a", "e.g. 0"), TF("b", "e.g. 2")},
            [](Feature& f) -> std::string {
                std::string e = Norm(Req(f, 0)); double lo = Num(f, 1), hi = Num(f, 2);
                return GraphOut([e, lo, hi](MathGraph& g, double a, double b) { g.plotIntegralArea(g_calc, e, lo, hi, a, b); });
            }, {{"x^2", "0", "2"}})));
        c.features.push_back(G(MkF("Plot Limit  x -> a", "Show f approaching a.",
            {TF("f(x)", "e.g. sin(x)/x"), TF("a", "e.g. 0")},
            [](Feature& f) -> std::string {
                std::string e = Norm(Req(f, 0)); double t = Num(f, 1);
                return GraphOut([e, t](MathGraph& g, double a, double b) { g.plotLimit(g_calc, e, t, a, b); });
            }, {{"sin(x)/x", "0"}})));
        c.features.push_back(G(MkF("Plot Indefinite Integral F(x)", "Graph of F(x) = integral of f from x_ref.",
            {TF("f(x)", "e.g. cos(x)"), TF("x_ref (default 0)", "0", "0")},
            [](Feature& f) -> std::string {
                std::string e = Norm(Req(f, 0)); double xr = NumOr(f, 1, 0.0);
                return GraphOut([e, xr](MathGraph& g, double a, double b) { g.plotIndefiniteIntegral(g_calc, e, xr, a, b); });
            }, {{"cos(x)", "0"}})));
        c.features.push_back(MkLab("Plot Scatter Plot from DataTable", "Enter a data table and plot two columns.", FK_TABLE, 6));
        c.features.push_back(G(MkF("Plot Area Between Two Curves", "Shaded area between f1 and f2 on [a, b].",
            {TF("f1(x)", "e.g. x"), TF("f2(x)", "e.g. x^2"), TF("a", "e.g. 0"), TF("b", "e.g. 1")},
            [](Feature& f) -> std::string {
                std::string e1 = Norm(Req(f, 0)), e2 = Norm(Req(f, 1)); double lo = Num(f, 2), hi = Num(f, 3);
                return GraphOut([e1, e2, lo, hi](MathGraph& g, double a, double b) { g.plotAreaBetweenCurves(g_calc, e1, e2, lo, hi, a, b); });
            }, {{"x", "x^2", "0", "1"}})));
        c.features.push_back(G(MkF("Plot Inverse Function f^-1(x)", "f, its inverse (reflection) and y = x.",
            {TF("f(x)", "e.g. 2x + 1  or  x^3")},
            [](Feature& f) -> std::string {
                std::string e = Norm(Req(f, 0));
                return GraphOut([e](MathGraph& g, double a, double b) { g.plotInverseFunction(g_calc, e, a, b); });
            }, {{"2x+1"}, {"x^3"}})));
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
                std::string cond = Req(f, 2);
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
                RelationReport r = g_fs.analyzeRelation(Req(f, 0));
                std::string t = std::string("Function (vertical line test): ") + (r.isFunction ? "yes" : "no");
                if (!r.isFunction) t += "\n  e.g. x = " + Fmt(r.counterExampleX) + " gives y in " + FunctionSolver::toString(r.counterExampleY);
                t += "\nStandard form: " + r.standardForm;
                if (!r.solvedForm.empty()) t += "\nSolved for y:  " + r.solvedForm;
                if (r.explicitForm) t += "\ny = " + r.explicitExpr;
                t += "\nDomain = " + FunctionSolver::toString(r.domain);
                t += "\nRange  = " + FunctionSolver::toString(r.range);
                return t;
            }, {{"x^2+y^2=25"}, {"y=1/(x+1)"}, {"y^2=x"}}));
        c.features.push_back(MkF("Analyze Function f(x)", "Domain, range, increasing/decreasing, one-to-one.",
            {TF("f(x)", "e.g. sqrt(x-2)   or   1/(x+1)")},
            [](Feature& f) -> std::string { return g_fs.describeFunction(Req(f, 0)); },
            {{"sqrt(x-2)"}, {"1/(x+1)"}, {"x^2"}}));
        c.features.push_back(MkF("Algebra of Functions  (f+g, f-g, fg, f/g)", "Combine two functions.",
            {TF("f(x)", "e.g. x^2"), TF("g(x)", "e.g. x - 1")},
            [](Feature& f) -> std::string {
                std::string a = Req(f, 0), b = Req(f, 1);
                struct It { const char* n; std::string e; };
                std::vector<It> items = {{"f+g", g_fs.add(a, b)}, {"f-g", g_fs.subtract(a, b)},
                                         {"f*g", g_fs.multiply(a, b)}, {"f/g", g_fs.divide(a, b)}};
                std::string t;
                for (auto& it : items) {
                    t += std::string(it.n) + " (x) = " + it.e + "\n";
                    try { t += "     Domain = " + FunctionSolver::toString(g_fs.domain(it.e)) + "\n"; } catch (...) {}
                }
                return t;
            }, {{"x^2", "x-1"}}));
        c.features.push_back(MkF("Composite Functions  (g o f, f o g)", "g(f(x)) and f(g(x)).",
            {TF("f(x)", "e.g. 2x + 1"), TF("g(x)", "e.g. x^2")},
            [](Feature& f) -> std::string {
                std::string a = Req(f, 0), b = Req(f, 1);
                std::string gf = g_fs.compose(b, a), fg = g_fs.compose(a, b);
                std::string t = "(g o f)(x) = g(f(x)) = " + gf + "\n";
                try { t += "     Domain = " + FunctionSolver::toString(g_fs.domain(gf)) + "\n"; } catch (...) {}
                t += "(f o g)(x) = f(g(x)) = " + fg + "\n";
                try { t += "     Domain = " + FunctionSolver::toString(g_fs.domain(fg)) + "\n"; } catch (...) {}
                return t;
            }, {{"2x+1", "x^2"}}));
        c.features.push_back(MkF("Inverse Function  f^-1", "Formula (linear f) and numeric f^-1(y0).",
            {TF("f(x)", "e.g. 3x - 6"), TF("y0  (optional, find f^-1(y0))", "e.g. 9")},
            [](Feature& f) -> std::string {
                std::string e = Req(f, 0);
                std::string t = std::string("f is one-to-one: ") + (g_fs.isOneToOne(e) ? "yes" : "NO (no inverse function)") + "\n";
                std::string inv = g_fs.inverseExpression(e);
                t += inv.empty() ? "f is not linear: no closed formula, use f^-1(y0) below.\n" : "f^-1(x) = " + inv + "\n";
                if (!Opt(f, 1).empty()) { double y0 = Num(f, 1); double v = g_fs.inverseAt(e, y0); SetAns(v); t += "f^-1(" + Fmt(y0) + ") = " + Fmt(v); }
                return t;
            }, {{"3x-6", "9"}, {"x^3", "8"}}));
        c.features.push_back(MkF("Function Type on A -> B  (Function / Onto / 1-1)", "Check a relation or f(x) between two sets / intervals.",
            {CF("Mode", {"Finite sets + relation condition", "f(x) on real intervals"}),
             TF("A", "e.g. 1,2,3   or   [0,inf)"), TF("B", "e.g. 3,5,7   or   [0,inf)"), TF("Condition  /  f(x)", "e.g. y=2x+1   or   x^2")},
            [](Feature& f) -> std::string {
                if (Pick(f, 0) == 0) {
                    auto A = ParseSet(Req(f, 1)), B = ParseSet(Req(f, 2));
                    auto r = g_fs.relationFromCondition(A, B, Req(f, 3));
                    return "r = " + FunctionSolver::toString(r) +
                           std::string("\nFunction A -> B: ") + (FunctionSolver::isFunctionFromAToB(r, A, B) ? "yes" : "no") +
                           "\nOnto B:          " + (FunctionSolver::isOntoB(r, B) ? "yes" : "no") +
                           "\nOne-to-one:      " + (FunctionSolver::isOneToOne(r) ? "yes" : "no");
                }
                Interval A = ParseInterval(Req(f, 1)), B = ParseInterval(Req(f, 2));
                std::string e = Req(f, 3);
                return std::string("f: A -> B is a function: ") + (g_fs.isFunctionFromTo(e, A, B) ? "yes" : "no") +
                       "\nOnto B:     " + (g_fs.isOnto(e, A, B) ? "yes" : "no") +
                       "\nOne-to-one: " + (g_fs.isOneToOneOn(e, A) ? "yes" : "no");
            }, {{"0", "1,2,3", "3,5,7,9", "y=2x+1"}, {"1", "[0,inf)", "[0,inf)", "x^2"}}));
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
                    std::map<char, double> vals;
                    if (!Opt(f, 2).empty()) vals = ParseGuess(Opt(f, 2));
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
    DrawText_(184, 23 - FontH(g_fontL) / 2, FitHead(g_fontL, title, g_w - 184 - 190), COL_TEXT, g_fontL);
    DrawTextRight(g_w - 12, 23 - FontH(g_fontS) / 2, FitHead(g_fontS, "ans = " + Fmt(g_ans), 180), COL_GREEN, g_fontS);
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
    DrawTextCenter(g_w / 2, 62, "Click a card (or press 1-7) to start", COL_DIM, g_fontS);
    int cols = 2, gap = 14, x0 = 20, y0 = 90;
    int cw = (g_w - 2 * x0 - gap) / cols;
    int ch = std::clamp((g_h - y0 - 20 - 3 * gap) / 4, 70, 120);
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
    int x = 20, w = std::min(g_w - 40, 900), h = 46, gap = 6, y = 90;
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
        std::string full = ReplaceAns(e), res;
        double v;
        if (full.find('=') != std::string::npos) {
            char var = 'x';
            try { var = AlphaSolverEq::detectVariable(full); } catch (...) {}
            v = g_calc.solver().solveTwoSideEquation(g_calc, full);
            res = std::string(1, var) + " = " + Fmt(v);
        } else {
            v = g_calc.evaluate(full);
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
    {"root","root(",0,COL_BTN},{"inf","inf",0,COL_BTN},{"x^2","^2",0,COL_OP},{"1/x","1/(",0,COL_OP},{"->x","",4,COL_BTN},
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
            XFontStruct* f = TextW(g_fontL, r) <= lw - 24 ? g_fontL : g_fontS;
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
    {"ans","ans",COL_BTN},{"inf","inf",COL_BTN},{"DEL","<DEL>",COL_CLEAR},{"CLR","<CLR>",COL_CLEAR},{";",";",COL_OP},{"space"," ",COL_OP},
};
static void MathKeypad(int x, int bottom, int w) {
    int cols = 6, rows = 7, gap = 4, kh = 28;
    int kw = (w - (cols - 1) * gap) / cols;
    int top = bottom - (rows * kh + (rows - 1) * gap);
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            const FKey& k = FORM_KEYS[r * cols + c];
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
    MathKeypad(lx, g_h - 10, lw);

    // ---- แผงขวา ----
    int rx = lx + lw + 12, rw = g_w - rx - 10, ry = 56;
    if (f.isGraph) {
        int bw = (rw - 5 * 4) / 6;
        auto rerun = [&f]() { if (!f.result.empty() || !f.error.empty()) Defer([&f] { RunFeature(f); }); };
        if (DrawButton(rx, ry, bw, 30, "Plot", COL_OK)) doRun = true;
        if (DrawButton(rx + (bw + 4) * 1, ry, bw, 30, "Zoom +", COL_BTN)) {
            try { double a = EvalNum(g_gxMinS), b = EvalNum(g_gxMaxS), c = (a + b) / 2, h = (b - a) / 4; GraphSetRange(c - h, c + h); rerun(); } catch (...) {}
        }
        if (DrawButton(rx + (bw + 4) * 2, ry, bw, 30, "Zoom -", COL_BTN)) {
            try { double a = EvalNum(g_gxMinS), b = EvalNum(g_gxMaxS), c = (a + b) / 2, h = (b - a); GraphSetRange(c - h, c + h); rerun(); } catch (...) {}
        }
        if (DrawButton(rx + (bw + 4) * 3, ry, bw, 30, "<  Left", COL_BTN)) {
            try { double a = EvalNum(g_gxMinS), b = EvalNum(g_gxMaxS), d = (b - a) / 4; GraphSetRange(a - d, b - d); rerun(); } catch (...) {}
        }
        if (DrawButton(rx + (bw + 4) * 4, ry, bw, 30, "Right  >", COL_BTN)) {
            try { double a = EvalNum(g_gxMinS), b = EvalNum(g_gxMaxS), d = (b - a) / 4; GraphSetRange(a + d, b + d); rerun(); } catch (...) {}
        }
        if (DrawButton(rx + (bw + 4) * 5, ry, bw, 30, "Reset", COL_CLEAR)) {
            g_gxMinS = "-10"; g_gxMaxS = "10"; g_gwS.clear(); g_ghS.clear(); rerun();
        }
        ry += 38;
        int tw = (rw - 4 * 70) / 4 - 4;
        int tx = rx;
        DrawText_(tx, ry + 7, "xMin", COL_DIM, g_fontM); TextBox(tx + 36, ry, tw - 20, 28, g_gxMinS, "-10", g_fontM); tx += tw + 18;
        DrawText_(tx, ry + 7, "xMax", COL_DIM, g_fontM); TextBox(tx + 36, ry, tw - 20, 28, g_gxMaxS, "10", g_fontM); tx += tw + 18;
        DrawText_(tx, ry + 7, "Width", COL_DIM, g_fontM); TextBox(tx + 40, ry, tw - 24, 28, g_gwS, "auto", g_fontM); tx += tw + 18;
        DrawText_(tx, ry + 7, "Height", COL_DIM, g_fontM); TextBox(tx + 44, ry, tw - 28, 28, g_ghS, "auto", g_fontM);
        ry += 36;
    }
    ResultPanel(rx, ry, rw, g_h - ry - 10, f.result, f.error, f.scroll, !f.isGraph);
    if (doRun) Defer([&f] { RunFeature(f); });
}

// ============================================================
//                    ตัวแก้ไขตาราง (Grid) ใช้ร่วมกัน Matrix / DataTable
// ============================================================
struct Grid {
    int r = 2, c = 2, scroll = 0;
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
    ResetFocus();
}
static void FillGridFromText(Grid& g, const std::string& text, int maxR, int maxC) {   // แถวคั่น ; หรือขึ้นบรรทัด, คอลัมน์คั่น , หรือ space
    std::string t = text;
    for (char& ch : t) if (ch == '\n') ch = ';';
    std::vector<std::vector<std::string>> rows;
    for (auto& r : SplitTop(t, ";")) rows.push_back(SplitTop(r, ", \t"));
    if (rows.empty()) throw std::runtime_error("Type values like  1,2;3,4  (rows separated by ;)");
    size_t cols = 0; for (auto& r : rows) cols = std::max(cols, r.size());
    if ((int)rows.size() > maxR || (int)cols > maxC) throw std::runtime_error("Too large (max " + std::to_string(maxR) + " x " + std::to_string(maxC) + ")");
    for (auto& r : rows) if (r.size() != cols) throw std::runtime_error("Every row must have the same number of values");
    Grid n = MakeGrid((int)rows.size(), (int)cols);
    for (size_t i = 0; i < rows.size(); ++i) for (size_t j = 0; j < cols; ++j) n.cell[i * cols + j] = rows[i][j];
    n.hdr = g.hdr; n.hdr.resize(cols);
    for (size_t j = g.hdr.size(); j < cols; ++j) n.hdr[j] = "col" + std::to_string(j + 1);
    g = n;
    ResetFocus();
}
static int DrawGrid(int x, int y, int maxW, int maxRows, Grid& g, bool editHeaders) {
    int rowLab = 30, ch = 26;
    int cw = std::clamp((maxW - rowLab) / std::max(1, g.c), 50, 130);
    int yy = y;
    if (editHeaders) {
        for (int j = 0; j < g.c; ++j) TextBox(x + rowLab + j * cw, yy, cw - 2, ch - 2, g.hdr[j], "name", g_fontM);
        yy += ch;
    }
    int vis = std::min(g.r, maxRows), maxScroll = std::max(0, g.r - vis);
    if (Hit(x, y, maxW, (vis + 1) * ch) && g_wheel) { g.scroll += g_wheel; g_wheel = 0; }
    g.scroll = std::clamp(g.scroll, 0, maxScroll);
    for (int i = 0; i < vis; ++i) {
        int r = g.scroll + i;
        DrawText_(x + 4, yy + (ch - FontH(g_fontM)) / 2, std::to_string(r + 1), COL_DIM, g_fontM);
        for (int j = 0; j < g.c; ++j) TextBox(x + rowLab + j * cw, yy, cw - 2, ch - 2, g.cell[(size_t)r * g.c + j], "0", g_fontM);
        yy += ch;
    }
    if (maxScroll > 0) DrawText_(x + rowLab + g.c * cw + 4, y, "rows " + std::to_string(g.scroll + 1) + "-" + std::to_string(g.scroll + vis) + " of " + std::to_string(g.r) + " (wheel)", COL_DIM, g_fontM);
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
static int g_mscroll = 0;
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
            v[i][j] = s.empty() ? 0.0 : EvalNum(s);
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
    ResultPanel(x, y, w, g_h - y - 10, g_mres, g_merr, g_mscroll, false);
}

// ============================================================
//                    [6] Data Table (สถิติ / joint table / scatter)
// ============================================================
static Grid g_tab;
static std::string g_tp[2], g_tquick, g_tres, g_terr;
static int g_tscroll = 0;
static const int TAB_MAX_R = 40, TAB_MAX_C = 8;
static const char* TAB_OPS[7] = {"Column stats", "Computed column", "Transpose", "Scalar x k", "Marginal / joint", "Expected value E[X]", "Scatter plot"};

static void TableInit() {
    if (g_tab.cell.size()) return;
    g_tab = MakeGrid(5, 2);
    g_tab.hdr = {"x", "y"};
    const char* v[] = {"1", "2", "2", "4", "3", "5", "4", "4", "5", "5"};
    for (int i = 0; i < 10; ++i) g_tab.cell[i] = v[i];
}
static DataTable TableOf(const Grid& g) {
    DataTable t(g.r, g.c);
    t.setHeaders(g.hdr);
    for (int i = 0; i < g.r; ++i)
        for (int j = 0; j < g.c; ++j) {
            std::string s = Trim(g.cell[(size_t)i * g.c + j]);
            t.setCell(i, j, s.empty() ? 0.0 : EvalNum(s));
        }
    return t;
}
static std::string TableRun() {
    DataTable t = TableOf(g_tab);
    switch (g_top) {
        case 0: {
            std::string out;
            char buf[256];
            snprintf(buf, sizeof buf, "%-12s %5s %12s %12s %12s %12s %12s %12s %12s\n", "Column", "n", "sum", "mean", "min", "max", "median", "variance", "stdev");
            out += buf;
            for (int c = 0; c < t.getCols(); ++c) {
                snprintf(buf, sizeof buf, "%-12s %5d %12.6g %12.6g %12.6g %12.6g %12.6g %12.6g %12.6g\n", t.getHeader(c).c_str(), t.getRows(),
                         TableCalculate::sumColumn(t, c), TableCalculate::meanColumn(t, c), TableCalculate::minColumn(t, c),
                         TableCalculate::maxColumn(t, c), TableCalculate::medianColumn(t, c),
                         t.getRows() > 1 ? TableCalculate::varianceColumn(t, c, true) : 0.0,
                         t.getRows() > 1 ? TableCalculate::stdevColumn(t, c, true) : 0.0);
                out += buf;
            }
            out += "(variance / stdev are sample statistics)";
            return out;
        }
        case 1: {
            std::string e = ReplaceAns(Trim(g_tp[0]));
            if (e.empty()) throw std::runtime_error("Type an expression, e.g.  x^2 + 2*y  (variables = column names, single letters)");
            std::vector<char> map;
            static const char def[] = {'x', 'y', 'z', 'w', 'u', 'v', 'p', 'q'};
            for (int c = 0; c < t.getCols(); ++c) map.push_back(g_tab.hdr[c].size() == 1 && isalpha((unsigned char)g_tab.hdr[c][0]) ? g_tab.hdr[c][0] : def[c % 8]);
            DataTable res = TableCalculate::applyExpression(t, g_calc, e, map, "result");
            std::string s = "Expression: " + e + "   (column letters: ";
            for (size_t c = 0; c < map.size(); ++c) s += std::string(1, map[c]) + (c + 1 < map.size() ? "," : "");
            return s + ")\n" + FormatTable(res);
        }
        case 2: return "Transpose:\n" + FormatTable(TableCalculate::transpose(t));
        case 3: { double k = EvalNum(g_tp[0].empty() ? "1" : g_tp[0]); return "Table x " + Fmt(k) + ":\n" + FormatTable(TableCalculate::multiplyScalar(t, k)); }
        case 4: {
            DataTable m = TableCalculate::computeMarginalProbabilities(t);
            return "Joint table with marginal totals (last row / column):\n" + FormatTable(m);
        }
        case 5: {
            int xc = (int)EvalNum(g_tp[0].empty() ? "1" : g_tp[0]) - 1, pc = (int)EvalNum(g_tp[1].empty() ? "2" : g_tp[1]) - 1;
            if (xc < 0 || pc < 0 || xc >= t.getCols() || pc >= t.getCols()) throw std::runtime_error("Column number out of range");
            double ev = TableCalculate::computeExpectedValue(t.getColumn(xc), t.getColumn(pc));
            SetAns(ev);
            return "E[X] = sum( x * P(x) ) = " + Fmt(ev) + "   (values: column " + std::to_string(xc + 1) + ", probabilities: column " + std::to_string(pc + 1) + ")";
        }
        default: {
            int xc = (int)EvalNum(g_tp[0].empty() ? "1" : g_tp[0]) - 1, yc = (int)EvalNum(g_tp[1].empty() ? "2" : g_tp[1]) - 1;
            if (xc < 0 || yc < 0 || xc >= t.getCols() || yc >= t.getCols()) throw std::runtime_error("Column number out of range");
            return GraphOut([t, xc, yc](MathGraph& g, double, double) { g.plotDataTable(t, xc, yc, "Scatter Plot"); });
        }
    }
}
static void ScreenTable() {
    TableInit();
    TopBar("[6] Data Table & Statistics");
    int x = 10, w = g_w - 20, y = 54;
    DrawText_(x, y, "Edit the table (headers on top, numbers below; expressions allowed). Pick an operation and press Calculate.", COL_DIM, g_fontM);
    y += FontH(g_fontM) + 6;
    int gh = DrawGrid(x, y, std::min(w, 900), 7, g_tab, true);
    y += gh + 8;
    {
        int r = g_tab.r, c = g_tab.c;
        bool ch1 = Stepper(x, y, "Rows", r, 1, TAB_MAX_R);
        bool ch2 = Stepper(x + 150, y, "Cols", c, 1, TAB_MAX_C);
        if (ch1 || ch2) ResizeGrid(g_tab, r, c);
        int qx = x + 310, qw = std::max(100, w - 310 - 84 - 190);
        DrawText_(qx, y + 6, "Quick fill", COL_TEXT, g_fontS);
        TextBox(qx + 84, y, qw, 28, g_tquick, "1,2;3,4;5,6  (rows split by ;)", g_fontM);
        int qb = qx + 84 + qw + 6;
        if (DrawButton(qb, y, 78, 28, "Apply", COL_BTN, g_fontM))
            try { FillGridFromText(g_tab, g_tquick, TAB_MAX_R, TAB_MAX_C); g_terr.clear(); } catch (const std::exception& e) { g_terr = e.what(); }
        if (DrawButton(qb + 84, y, 100, 28, "Clear cells", COL_CLEAR, g_fontM)) for (auto& s : g_tab.cell) s.clear();
        y += 36;
    }
    {
        int cw = (w - 3 * 4) / 4;
        for (int i = 0; i < 7; ++i)
            if (DrawButton(x + (i % 4) * (cw + 4), y + (i / 4) * 32, cw, 28, TAB_OPS[i], i == g_top ? COL_OK : COL_CHIP, g_fontS, i == g_top)) g_top = i;
        y += 68;
    }
    bool run = g_enter;
    {
        const char* l0 = nullptr; const char* l1 = nullptr; const char* h0 = "";
        if (g_top == 1) { l0 = "Expression"; h0 = "x^2 + 2*y"; }
        else if (g_top == 3) { l0 = "k (scalar)"; h0 = "2"; }
        else if (g_top == 5) { l0 = "X column #"; l1 = "P column #"; }
        else if (g_top == 6) { l0 = "X column #"; l1 = "Y column #"; }
        int px = x;
        if (l0) {
            DrawText_(px, y + 6, l0, COL_TEXT, g_fontS); px += TextW(g_fontS, l0) + 8;
            int bw2 = (g_top == 1) ? 300 : 70;
            TextBox(px, y, bw2, 28, g_tp[0], g_top >= 5 ? "1" : h0, g_fontM); px += bw2 + 10;
        }
        if (l1) { DrawText_(px, y + 6, l1, COL_TEXT, g_fontS); px += TextW(g_fontS, l1) + 8; TextBox(px, y, 70, 28, g_tp[1], "2", g_fontM); px += 80; }
        if (!l0) DrawText_(px, y + 6, g_top == 0 ? "Statistics of every column." : g_top == 4 ? "Enter joint probabilities; totals are added." : "No parameters.", COL_DIM, g_fontS);
        if (DrawButton(std::max(px + 10, w - 170), y - 2, 160, 34, "Calculate", COL_OK)) run = true;
        y += 42;
    }
    if (run) Defer([] {
        g_terr.clear();
        try { g_tres = TableRun(); } catch (const std::exception& e) { g_terr = e.what(); g_tres.clear(); }
        g_tscroll = 0;
    });
    std::string err = g_terr;
    ResultPanel(x, y, w, g_h - y - 10, g_tres, err, g_tscroll, false);
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
    g_tabPrev = g_tabNow; g_tabNow.clear();
    g_text.clear();
    Frame();
    if (ui.released) ui.active = 0;
    ui.pressed = ui.released = false;
    g_enter = false; g_wheel = 0; g_wheelKey = 0; g_pendingChar = 0;
    auto jobs = std::move(g_deferred);
    g_deferred.clear();
    for (auto& j : jobs) j();
    XPutImage(g_dpy, g_win, g_gc, g_img, 0, 0, 0, 0, g_w, g_h);
    for (auto& t : g_text) {
        XSetForeground(g_dpy, g_gc, t.c);
        if (t.f) XSetFont(g_dpy, g_gc, t.f->fid);
        int asc = t.f ? t.f->ascent : 12;
        XDrawString(g_dpy, g_win, g_gc, t.x, t.y + asc, t.s.c_str(), (int)t.s.size());
    }
    XFlush(g_dpy);
}
static XFontStruct* LoadFont(const char* a, const char* b, const char* c) {
    XFontStruct* f = XLoadQueryFont(g_dpy, a);
    if (!f) f = XLoadQueryFont(g_dpy, b);
    if (!f) f = XLoadQueryFont(g_dpy, c);
    return f;
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

int main() {
    g_dpy = XOpenDisplay(nullptr);
    if (!g_dpy) { fprintf(stderr, "Cannot open X display\n"); return 1; }
    int screen = DefaultScreen(g_dpy);
    const int W0 = 1040, H0 = 800;
    g_win = XCreateSimpleWindow(g_dpy, RootWindow(g_dpy, screen), 0, 0, W0, H0, 0,
                                BlackPixel(g_dpy, screen), WhitePixel(g_dpy, screen));
    XStoreName(g_dpy, g_win, "Maths Solver System");
    XSizeHints hints;
    memset(&hints, 0, sizeof hints);
    hints.flags = PMinSize;
    hints.min_width = 940; hints.min_height = 700;
    XSetWMNormalHints(g_dpy, g_win, &hints);
    XSelectInput(g_dpy, g_win, ExposureMask | StructureNotifyMask | PointerMotionMask |
                               ButtonPressMask | ButtonReleaseMask | KeyPressMask);
    Atom wmDelete = XInternAtom(g_dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(g_dpy, g_win, &wmDelete, 1);
    g_gc = XCreateGC(g_dpy, g_win, 0, nullptr);
    g_fontS = LoadFont("9x15", "8x13", "fixed");
    g_fontM = LoadFont("7x13", "6x13", "fixed");
    g_fontL = LoadFont("10x20", "9x15", "fixed");
    if (!g_fontM) g_fontM = g_fontS;
    if (!g_fontL) g_fontL = g_fontS;

    BuildCatalog();
    CreateBuffer(W0, H0);
    XMapWindow(g_dpy, g_win);

    bool running = true;
    while (running) {
        XEvent e;
        XNextEvent(g_dpy, &e);
        bool extra = false;
        switch (e.type) {
        case ConfigureNotify:
            if (e.xconfigure.width != g_w || e.xconfigure.height != g_h) CreateBuffer(e.xconfigure.width, e.xconfigure.height);
            break;
        case MotionNotify: ui.mx = e.xmotion.x; ui.my = e.xmotion.y; break;
        case ButtonPress:
            ui.mx = e.xbutton.x; ui.my = e.xbutton.y;
            if (e.xbutton.button == Button1) ui.down = ui.pressed = true;
            else if (e.xbutton.button == Button4) g_wheel = -1;
            else if (e.xbutton.button == Button5) g_wheel = 1;
            break;
        case ButtonRelease:
            if (e.xbutton.button == Button1) { ui.mx = e.xbutton.x; ui.my = e.xbutton.y; ui.down = false; ui.released = true; extra = true; }
            break;
        case KeyPress: {
            char buf[16];
            KeySym ks = NoSymbol;
            int n = XLookupString(&e.xkey, buf, sizeof buf - 1, &ks, nullptr);
            extra = true;
            if (ks == XK_Escape) g_escape = true;
            else if (ks == XK_Return || ks == XK_KP_Enter) g_enter = true;
            else if (ks == XK_Tab) FocusNext(false);
            else if (ks == XK_ISO_Left_Tab) FocusNext(true);
            else if (ks == XK_BackSpace) { if (g_screen == S_CALC) CalcDelete(); else if (g_focusStr && !g_focusStr->empty()) g_focusStr->pop_back(); }
            else if (ks == XK_Delete) { if (g_screen == S_CALC) CalcClear(); else if (g_focusStr) g_focusStr->clear(); }
            else if (ks == XK_Up) g_wheelKey = -1;
            else if (ks == XK_Down) g_wheelKey = 1;
            else if (ks == XK_Prior) g_wheelKey = -8;
            else if (ks == XK_Next) g_wheelKey = 8;
            else if (n == 1 && isprint((unsigned char)buf[0])) TypeChar(buf[0]);
            break;
        }
        case ClientMessage:
            if ((Atom)e.xclient.data.l[0] == wmDelete) running = false;
            break;
        }
        if (running && XPending(g_dpy) == 0) {
            Present();
            if (extra) Present();      // วาดซ้ำเพื่อให้เห็นผลของการคลิก/พิมพ์ทันที
        }
    }
    if (g_img) XDestroyImage(g_img);
    if (g_fontL && g_fontL != g_fontS) XFreeFont(g_dpy, g_fontL);
    if (g_fontM && g_fontM != g_fontS) XFreeFont(g_dpy, g_fontM);
    if (g_fontS) XFreeFont(g_dpy, g_fontS);
    XFreeGC(g_dpy, g_gc);
    XDestroyWindow(g_dpy, g_win);
    XCloseDisplay(g_dpy);
    return 0;
}