// เครื่องคิดเลขแบบมีปุ่มกด (GUI) สำหรับ Linux: Xlib + software framebuffer + immediate-mode widgets
// ใช้ "ทุกไฟล์ .hpp" ของโปรเจกต์ (แก้เฉพาะ main.cpp):
//   Calculator_calculus / Alpha_solver_eq / algebra  -> โหมด CALC   (คำนวณ, อนุพันธ์, ปริพันธ์, ลิมิต, แก้สมการ)
//   matrix                                           -> โหมด MATRIX (บวก ลบ คูณ det inverse transpose แก้ระบบสมการ)
//   table_calculate                                  -> โหมด STATS  (สถิติ, nCr/nPr, binomial/poisson/normal, ตาราง)
//   function_solver                                  -> โหมด FUNC   (โดเมน/เรนจ์, 1-1, เพิ่ม/ลด, ผกผัน, f+g, g∘f ...)
//   math_graph                                       -> โหมด GRAPH  (กราฟ ASCII แสดงในหน้าต่าง + ซูม/เลื่อน)
//   menu                                             -> include ไว้ (เมนูแบบ console ไม่ถูกเรียกใน GUI)
//
// ติดตั้ง header (Debian/Ubuntu): sudo apt install libx11-dev
// Build (main.cpp อยู่ใน src/ เหมือนไฟล์ .cpp อื่น เพราะ include แบบ "../lib/..."):
//   g++ -std=c++17 *.cpp -o calc -lX11
// (ลิงก์ทุกไฟล์ .cpp ได้เลย; ตั้งชื่อ main.cpp ที่มี main() เพียงไฟล์เดียว)
//
// คีย์บอร์ด: พิมพ์ได้ตามปกติ, Enter = "=", Backspace = ลบ, Esc/Delete = ล้าง, ลูกศรขึ้น/ลง/ล้อเมาส์ = เลื่อนผลลัพธ์
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
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#define RGB32(r,g,b) ((uint32_t)(((r)<<16)|((g)<<8)|(b)))

// ---------- X11 / framebuffer state ----------
static Display* g_dpy = nullptr;
static Window g_win;
static GC g_gc;
static XFontStruct* g_fontS = nullptr;   // ฟอนต์ปุ่ม/นิพจน์
static XFontStruct* g_fontM = nullptr;   // ฟอนต์เล็ก monospace (ผลลัพธ์หลายบรรทัด / กราฟ)
static XFontStruct* g_fontL = nullptr;   // ฟอนต์ใหญ่ (ผลลัพธ์บรรทัดเดียวของ CALC)
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
static std::vector<TextCmd> g_text;                // drawn after the framebuffer is blitted

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
    g_text.push_back({x, y, s, c, f});             // y = ขอบบนของข้อความ
}
static void DrawTextCenter(int cx, int cy, const std::string& s, uint32_t c, XFontStruct* f) {
    DrawText_(cx - TextW(f, s) / 2, cy - FontH(f) / 2, s, c, f);
}
static void DrawTextRight(int xr, int y, const std::string& s, uint32_t c, XFontStruct* f) {
    DrawText_(xr - TextW(f, s), y, s, c, f);
}

// ---------- Immediate-mode UI ----------
struct UIState {
    int mx = 0, my = 0;
    bool down = false, pressed = false, released = false;
    int hot = 0, active = 0;
} ui;

static bool Hit(int x, int y, int w, int h) {
    return ui.mx >= x && ui.mx < x + w && ui.my >= y && ui.my < y + h;
}
static uint32_t Shade(uint32_t c, int d) {
    int r = std::clamp((int)((c >> 16) & 255) + d, 0, 255);
    int g = std::clamp((int)((c >> 8) & 255) + d, 0, 255);
    int b = std::clamp((int)(c & 255) + d, 0, 255);
    return RGB32(r, g, b);
}
static bool DrawButton(int id, int x, int y, int w, int h, const std::string& label, uint32_t base) {
    bool over = Hit(x, y, w, h);
    if (over) ui.hot = id;
    if (over && ui.pressed) ui.active = id;
    bool clicked = ui.released && ui.active == id && over;
    uint32_t bg = (ui.active == id && ui.down && over) ? Shade(base, -25)
                : over ? Shade(base, 18) : base;
    FillRectPx(x, y, w, h, bg);
    Border(x, y, w, h, Shade(base, -50));
    DrawTextCenter(x + w / 2, y + h / 2, label, RGB32(255, 255, 255), g_fontS);
    return clicked;
}
static void BeginFrame() { ui.hot = 0; g_text.clear(); }
static void EndFrame() {
    if (ui.released) ui.active = 0;
    ui.pressed = ui.released = false;
}

// ============================================================
//                    Application state
// ============================================================
enum Mode { M_CALC, M_MATRIX, M_STATS, M_FUNC, M_GRAPH, M_COUNT };
static const char* MODE_NAMES[M_COUNT] = {"CALC", "MATRIX", "STATS", "FUNC", "GRAPH"};
static const char* HINTS[M_COUNT] = {
    "f;x0 d/dx | f;a;b int | f;a lim | f=g solve | Esc clear",
    "A | B  rows split by ;  e.g.  1,2;3,4 | 5,6;7,8",
    "list 1,2,3 | nCr n,r | binP k,n,p | poiP k,lam | normP x,mu,sd",
    "f | a (f(a), f^-1(a), f=y) | f | g | f(x)=x^2+1 | f(x+h)-f(x)",
    "f | x0 d/dx | f | a | b int | f | g | a | b area | x,y;x,y pts",
};

static Calculator g_calc;
static FunctionSolver g_fs(g_calc);

static int g_mode = M_CALC;
static std::string g_exprs[M_COUNT], g_results[M_COUNT];
static bool g_freshs[M_COUNT] = {false, false, false, false, false};
static int g_scroll[M_COUNT] = {0, 0, 0, 0, 0};
static int g_maxScroll = 0;
static std::string g_error;
static double g_ans = 0.0;

#define EXPR   g_exprs[g_mode]
#define RESULT g_results[g_mode]
#define FRESH  g_freshs[g_mode]

// ---------- Layout ----------
struct Layout {
    int M, GAP, tabY, tabH, dispX, dispY, dispW, dispH, hintY, keyTop, bw, bh;
    int outTop, outH, outCols, outRows, innerW;
};
static const int COLS = 5, ROWS = 9;

static Layout ComputeLayout(int mode) {
    Layout L;
    L.M = 10; L.GAP = 6; L.tabY = L.M; L.tabH = 30;
    L.dispX = L.M; L.dispY = L.tabY + L.tabH + 6; L.dispW = g_w - 2 * L.M;
    int fhS = FontH(g_fontS), fhM = FontH(g_fontM);
    int hintBlock = fhM + 10;
    if (mode == M_CALC) {
        L.dispH = 150;
        L.hintY = L.dispY + L.dispH + 4;
        L.keyTop = L.hintY + hintBlock;
        L.bh = std::max(20, (g_h - L.keyTop - L.M - (ROWS - 1) * L.GAP) / ROWS);
    } else {
        L.bh = 32;
        int keyH = ROWS * L.bh + (ROWS - 1) * L.GAP;
        L.dispH = std::max(150, g_h - L.dispY - hintBlock - keyH - L.M);
        L.hintY = L.dispY + L.dispH + 4;
        L.keyTop = L.hintY + hintBlock;
    }
    L.bw = (g_w - 2 * L.M - (COLS - 1) * L.GAP) / COLS;
    L.innerW = L.dispW - 16;
    L.outTop = L.dispY + 8 + 2 * (fhS + 2) + 10;           // ใต้เส้นคั่น
    L.outH = L.dispY + L.dispH - L.outTop - 4;
    L.outCols = std::max(10, L.innerW / CharW(g_fontM));
    L.outRows = std::max(3, L.outH / (fhM + 1));
    return L;
}

// ============================================================
//                    Helpers
// ============================================================
static std::string Fmt(double v) {
    if (std::isnan(v)) return "NaN";
    if (std::isinf(v)) return v > 0 ? "inf" : "-inf";
    char buf[64];
    snprintf(buf, sizeof buf, "%.10g", v);
    return buf;
}
static std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && isspace((unsigned char)s[a])) ++a;
    while (b > a && isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
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
    return out;
}
// แบ่งด้วยตัวคั่น 1 ตัว (เก็บส่วนว่างไว้ แต่ trim ให้)
static std::vector<std::string> Split(const std::string& s, char sep) {
    std::vector<std::string> parts;
    std::string cur;
    for (char c : s) {
        if (c == sep) { parts.push_back(Trim(cur)); cur.clear(); }
        else cur += c;
    }
    parts.push_back(Trim(cur));
    return parts;
}
// แบ่งด้วยชุดตัวคั่น โดยไม่แบ่งภายในวงเล็บ (ข้ามส่วนว่าง)
static std::vector<std::string> SplitTop(const std::string& s, const std::string& seps) {
    std::vector<std::string> out;
    std::string cur;
    int depth = 0;
    for (char c : s) {
        if (c == '(') ++depth; else if (c == ')') depth = std::max(0, depth - 1);
        if (depth == 0 && seps.find(c) != std::string::npos) {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}
static double EvalNum(const std::string& s) { return g_calc.evaluate(ReplaceAns(s)); }
static std::vector<std::string> Parts() { return Split(EXPR, '|'); }
static std::string Part(size_t i, const char* usage) {
    auto p = Parts();
    if (i >= p.size() || p[i].empty()) throw std::runtime_error(std::string("usage: ") + usage);
    return p[i];
}
static void SetText(const std::string& t) { RESULT = t; g_scroll[g_mode] = 0; }
static void SetValue(double v) {                           // ใช้ใน CALC
    g_ans = v; RESULT = Fmt(v); g_scroll[g_mode] = 0; FRESH = true;
}
static void Guard(void (*impl)()) {
    g_error.clear();
    try { impl(); }
    catch (const std::exception& ex) { g_error = ex.what(); RESULT.clear(); }
}
#define ACTION(name) static void name##_impl(); static void name() { Guard(name##_impl); } static void name##_impl()

static std::string FmtComplex(const std::complex<double>& z) {
    if (std::fabs(z.imag()) < 1e-9) return Fmt(z.real());
    return Fmt(z.real()) + (z.imag() >= 0 ? "+" : "-") + Fmt(std::fabs(z.imag())) + "i";
}

// ============================================================
//                    โหมด CALC
// ============================================================
ACTION(C_equals) {
    std::string e = Trim(EXPR);
    if (e.empty()) return;
    g_calc.solver().setAns(g_ans);
    if (e.find(';') != std::string::npos)
        throw std::runtime_error("';' is used with d/dx, int, lim");
    std::string full = ReplaceAns(e);
    if (full.find('=') != std::string::npos) {
        char var = 'x';
        try { var = AlphaSolverEq::detectVariable(full); } catch (...) {}
        double v = g_calc.solver().solveTwoSideEquation(g_calc, full);
        SetValue(v);
        RESULT = std::string(1, var) + " = " + Fmt(v);
    } else {
        SetValue(g_calc.evaluate(full));
    }
}
ACTION(C_deriv) {
    auto p = Split(Trim(EXPR), ';');
    if (p.size() == 1 && !p[0].empty()) { SetText(g_calc.symbolicDerivative(ReplaceAns(p[0]))); FRESH = true; }
    else if (p.size() == 2) SetValue(g_calc.derivative(ReplaceAns(p[0]), EvalNum(p[1])));
    else throw std::runtime_error("usage: f   or   f;x0");
}
ACTION(C_integ) {
    auto p = Split(Trim(EXPR), ';');
    if (p.size() == 1 && !p[0].empty()) { SetText(g_calc.symbolicIntegral(ReplaceAns(p[0])) + " + C"); FRESH = true; }
    else if (p.size() == 3) SetValue(g_calc.integral(ReplaceAns(p[0]), EvalNum(p[1]), EvalNum(p[2])));
    else throw std::runtime_error("usage: f   or   f;a;b");
}
ACTION(C_limit) {
    auto p = Split(Trim(EXPR), ';');
    if (p.size() != 2 && p.size() != 3) throw std::runtime_error("usage: f;a   or   f;a;+  /  f;a;-");
    char side = '\0';
    if (p.size() == 3) {
        if (p[2] == "+") side = '+';
        else if (p[2] == "-") side = '-';
        else throw std::runtime_error("side must be + or -");
    }
    SetValue(g_calc.limit(ReplaceAns(p[0]), EvalNum(p[1]), 1e-6, side));
}
static void C_store() {
    g_error.clear();
    g_calc.setVariable('x', g_ans);
    SetText("x = " + Fmt(g_ans));
}

// ============================================================
//                    โหมด MATRIX
// ============================================================
static Matrix ParseMatrix(const std::string& s) {
    std::vector<std::vector<double>> rows;
    for (auto& r : Split(s, ';')) {
        if (r.empty()) continue;
        std::vector<double> row;
        for (auto& t : SplitTop(r, ", \t")) row.push_back(EvalNum(t));
        rows.push_back(row);
    }
    if (rows.empty()) throw std::runtime_error("empty matrix");
    for (auto& r : rows)
        if (r.size() != rows[0].size()) throw std::runtime_error("matrix rows have different lengths");
    return Matrix(rows);
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
        for (int j = 0; j < m.getCols(); ++j) {
            out += std::string(w - cells[i][j].size(), ' ') + cells[i][j] + " ";
        }
        out += "]\n";
    }
    return out;
}
static void NeedTwo(std::vector<std::string>& p) {
    p = Parts();
    if (p.size() < 2 || p[1].empty()) throw std::runtime_error("usage: A | B");
}
ACTION(X_add) { std::vector<std::string> p; NeedTwo(p);
    SetText("A + B =\n" + FormatMatrix(ParseMatrix(p[0]).add(ParseMatrix(p[1])))); }
ACTION(X_sub) { std::vector<std::string> p; NeedTwo(p);
    SetText("A - B =\n" + FormatMatrix(ParseMatrix(p[0]).subtract(ParseMatrix(p[1])))); }
ACTION(X_mul) { std::vector<std::string> p; NeedTwo(p);
    SetText("A * B =\n" + FormatMatrix(ParseMatrix(p[0]).multiply(ParseMatrix(p[1])))); }
ACTION(X_scalar) { std::vector<std::string> p; NeedTwo(p);
    SetText("A * k =\n" + FormatMatrix(ParseMatrix(p[0]).multiplyScalar(EvalNum(p[1])))); }
ACTION(X_solve) { std::vector<std::string> p; NeedTwo(p);
    Matrix X = Matrix::solveLinearGeneral(ParseMatrix(p[0]), ParseMatrix(p[1]));
    SetText("X  (A X = B) =\n" + FormatMatrix(X)); }
ACTION(X_det) { SetText("det(A) = " + Fmt(ParseMatrix(Part(0, "A")).determinant())); }
ACTION(X_inv) { SetText("A^-1 =\n" + FormatMatrix(ParseMatrix(Part(0, "A")).inverse())); }
ACTION(X_trans) { SetText("A^T =\n" + FormatMatrix(ParseMatrix(Part(0, "A")).transpose())); }
ACTION(X_show) {
    auto p = Parts();
    std::string t = "A (" + std::to_string(ParseMatrix(p[0]).getRows()) + "x" +
                    std::to_string(ParseMatrix(p[0]).getCols()) + ") =\n" + FormatMatrix(ParseMatrix(p[0]));
    if (p.size() > 1 && !p[1].empty()) {
        Matrix B = ParseMatrix(p[1]);
        t += "B (" + std::to_string(B.getRows()) + "x" + std::to_string(B.getCols()) + ") =\n" + FormatMatrix(B);
    }
    SetText(t);
}

// ============================================================
//                    โหมด STATS
// ============================================================
static std::vector<double> Args(size_t lo, size_t hi, const char* usage) {
    std::vector<double> v;
    for (auto& t : SplitTop(EXPR, ", ;\t")) v.push_back(EvalNum(t));
    if (v.size() < lo || v.size() > hi) throw std::runtime_error(std::string("usage: ") + usage);
    return v;
}
static std::vector<double> NumList(const std::string& s) {
    std::vector<double> v;
    for (auto& t : SplitTop(s, ", ;\t")) v.push_back(EvalNum(t));
    if (v.empty()) throw std::runtime_error("enter a list of numbers, e.g. 1,2,3,4");
    return v;
}
static DataTable ListTable(const std::vector<double>& v) {
    DataTable t((int)v.size(), 1);
    t.setHeader(0, "x");
    for (size_t i = 0; i < v.size(); ++i) t.setCell((int)i, 0, v[i]);
    return t;
}
static std::string FormatTable(const DataTable& t) {
    std::string out;
    char buf[64];
    for (int c = 0; c < t.getCols(); ++c) { snprintf(buf, sizeof buf, "%-12s", t.getHeader(c).c_str()); out += buf; }
    out += "\n";
    for (int r = 0; r < t.getRows(); ++r) {
        for (int c = 0; c < t.getCols(); ++c) { snprintf(buf, sizeof buf, "%-12.6g", t.getCell(r, c)); out += buf; }
        out += "\n";
    }
    return out;
}
ACTION(S_sum)  { SetText("sum = "    + Fmt(TableCalculate::sumColumn(ListTable(NumList(EXPR)), 0))); }
ACTION(S_mean) { SetText("mean = "   + Fmt(TableCalculate::meanColumn(ListTable(NumList(EXPR)), 0))); }
ACTION(S_min)  { SetText("min = "    + Fmt(TableCalculate::minColumn(ListTable(NumList(EXPR)), 0))); }
ACTION(S_max)  { SetText("max = "    + Fmt(TableCalculate::maxColumn(ListTable(NumList(EXPR)), 0))); }
ACTION(S_med)  { SetText("median = " + Fmt(TableCalculate::medianColumn(ListTable(NumList(EXPR)), 0))); }
ACTION(S_var)  { auto v = NumList(EXPR); DataTable t = ListTable(v);
    SetText("variance (sample) = " + Fmt(TableCalculate::varianceColumn(t, 0, true)) +
            "\nvariance (population) = " + Fmt(TableCalculate::varianceColumn(t, 0, false))); }
ACTION(S_sd)   { auto v = NumList(EXPR); DataTable t = ListTable(v);
    SetText("stdev (sample) = " + Fmt(TableCalculate::stdevColumn(t, 0, true)) +
            "\nstdev (population) = " + Fmt(TableCalculate::stdevColumn(t, 0, false))); }
ACTION(S_all) {
    auto v = NumList(EXPR); DataTable t = ListTable(v);
    SetText("n = " + std::to_string(v.size()) +
            "\nsum = " + Fmt(TableCalculate::sumColumn(t, 0)) +
            "\nmean = " + Fmt(TableCalculate::meanColumn(t, 0)) +
            "\nmin = " + Fmt(TableCalculate::minColumn(t, 0)) +
            "\nmax = " + Fmt(TableCalculate::maxColumn(t, 0)) +
            "\nmedian = " + Fmt(TableCalculate::medianColumn(t, 0)) +
            "\nvariance = " + Fmt(TableCalculate::varianceColumn(t, 0, true)) +
            "\nstdev = " + Fmt(TableCalculate::stdevColumn(t, 0, true)));
}
ACTION(S_nCr) { auto a = Args(2, 2, "n,r");
    SetText("nCr = " + std::to_string(TableCalculate::nCr((int)a[0], (int)a[1]))); }
ACTION(S_nPr) { auto a = Args(2, 2, "n,r");
    SetText("nPr = " + std::to_string(TableCalculate::nPr((int)a[0], (int)a[1]))); }
ACTION(S_binP) { auto a = Args(3, 3, "k,n,p");
    SetText("P(X = k) = " + Fmt(TableCalculate::binomPDF((int)a[0], (int)a[1], a[2]))); }
ACTION(S_binC) { auto a = Args(3, 3, "k,n,p");
    SetText("P(X <= k) = " + Fmt(TableCalculate::binomCDF((int)a[0], (int)a[1], a[2]))); }
ACTION(S_poiP) { auto a = Args(2, 2, "k,lambda");
    SetText("P(X = k) = " + Fmt(TableCalculate::poissonPDF((int)a[0], a[1]))); }
ACTION(S_poiC) { auto a = Args(2, 2, "k,lambda");
    SetText("P(X <= k) = " + Fmt(TableCalculate::poissonCDF((int)a[0], a[1]))); }
ACTION(S_geo) { auto a = Args(2, 2, "k,p");
    SetText("P(X = k) = " + Fmt(TableCalculate::geometricPDF((int)a[0], a[1]))); }
ACTION(S_hyp) { auto a = Args(4, 4, "k,N,K,n");
    SetText("P(X = k) = " + Fmt(TableCalculate::hypergeomPDF((int)a[0], (int)a[1], (int)a[2], (int)a[3]))); }
ACTION(S_norP) { auto a = Args(1, 3, "x[,mean,stdev]");
    SetText("pdf = " + Fmt(TableCalculate::normalPDF(a[0], a.size() > 1 ? a[1] : 0.0, a.size() > 2 ? a[2] : 1.0))); }
ACTION(S_norC) { auto a = Args(1, 3, "x[,mean,stdev]");
    SetText("cdf = " + Fmt(TableCalculate::normalCDF(a[0], a.size() > 1 ? a[1] : 0.0, a.size() > 2 ? a[2] : 1.0))); }
ACTION(S_ex) {
    auto p = Parts();
    if (p.size() < 2) throw std::runtime_error("usage: v1,v2,... | p1,p2,...");
    auto vals = NumList(p[0]), probs = NumList(p[1]);
    if (vals.size() != probs.size()) throw std::runtime_error("values and probabilities must have the same length");
    SetText("E[X] = " + Fmt(TableCalculate::computeExpectedValue(vals, probs)));
}
ACTION(S_binTbl) { auto a = Args(2, 2, "n,p");
    SetText(FormatTable(TableCalculate::generateBinomialTable((int)a[0], a[1]))); }
ACTION(S_zTbl) {
    std::vector<double> a;
    if (!Trim(EXPR).empty()) a = Args(3, 3, "zMin,zMax,step   (or empty for default)");
    DataTable t = a.empty() ? TableCalculate::generateNormalZTable()
                            : TableCalculate::generateNormalZTable(a[0], a[1], a[2]);
    SetText(FormatTable(t));
}

// ============================================================
//                    โหมด FUNC
// ============================================================
static std::string FmtReal(const std::vector<double>& v) { return FunctionSolver::toString(v); }

ACTION(F_describe) { SetText(g_fs.describeFunction(ReplaceAns(Part(0, "f(x)")))); }
ACTION(F_domain)   { SetText("Domain = " + FunctionSolver::toString(g_fs.domain(ReplaceAns(Part(0, "f(x)"))))); }
ACTION(F_range)    { SetText("Range = "  + FunctionSolver::toString(g_fs.range(ReplaceAns(Part(0, "f(x)"))))); }
ACTION(F_1to1)     { SetText(std::string("f is ") + (g_fs.isOneToOne(ReplaceAns(Part(0, "f(x)"))) ? "" : "NOT ") + "one-to-one"); }
ACTION(F_mono) {
    auto pcs = g_fs.monotonicity(ReplaceAns(Part(0, "f(x)")));
    std::string t;
    for (auto& m : pcs)
        t += m.range.toString() + "  " + (m.kind > 0 ? "increasing" : m.kind < 0 ? "decreasing" : "constant") + "\n";
    SetText(t.empty() ? "no pieces found" : t);
}
ACTION(F_inv) {
    std::string r = g_fs.inverseExpression(ReplaceAns(Part(0, "f(x)")));
    SetText(r.empty() ? "f is not linear: use  f | y0  with  f^-1(a)" : "f^-1(x) = " + r);
}
ACTION(F_at) {
    std::string f = ReplaceAns(Part(0, "f | a"));
    double a = EvalNum(Part(1, "f | a"));
    SetText("f(" + Fmt(a) + ") = " + Fmt(g_fs.evaluateAt(f, a)));
}
ACTION(F_invAt) {
    std::string f = ReplaceAns(Part(0, "f | y0"));
    double y0 = EvalNum(Part(1, "f | y0"));
    SetText("f^-1(" + Fmt(y0) + ") = " + Fmt(g_fs.inverseAt(f, y0)));
}
ACTION(F_eqY) {
    std::string f = ReplaceAns(Part(0, "f | y0"));
    double y0 = EvalNum(Part(1, "f | y0"));
    SetText("f(x) = " + Fmt(y0) + "  ->  x in " + FmtReal(g_fs.solveFunctionEquals(f, y0)));
}
ACTION(F_rel) {
    RelationReport r = g_fs.analyzeRelation(ReplaceAns(Part(0, "equation")));
    std::string t = std::string("function (vertical line test): ") + (r.isFunction ? "yes" : "no");
    if (!r.isFunction) t += "\n  x = " + Fmt(r.counterExampleX) + " gives y in " + FmtReal(r.counterExampleY);
    t += "\nstandard form: " + r.standardForm;
    if (!r.solvedForm.empty()) t += "\nsolved for y: " + r.solvedForm;
    if (r.explicitForm) t += "\ny = " + r.explicitExpr;
    t += "\nDomain = " + FunctionSolver::toString(r.domain);
    t += "\nRange = "  + FunctionSolver::toString(r.range);
    SetText(t);
}
ACTION(F_add) { SetText("f+g = " + g_fs.add(ReplaceAns(Part(0, "f | g")), ReplaceAns(Part(1, "f | g")))); }
ACTION(F_sub) { SetText("f-g = " + g_fs.subtract(ReplaceAns(Part(0, "f | g")), ReplaceAns(Part(1, "f | g")))); }
ACTION(F_mul) { SetText("f*g = " + g_fs.multiply(ReplaceAns(Part(0, "f | g")), ReplaceAns(Part(1, "f | g")))); }
ACTION(F_div) { SetText("f/g = " + g_fs.divide(ReplaceAns(Part(0, "f | g")), ReplaceAns(Part(1, "f | g")))); }
ACTION(F_comp) { SetText("g(f(x)) = " + g_fs.compose(ReplaceAns(Part(0, "g | f")), ReplaceAns(Part(1, "g | f")))); }
ACTION(F_call) {
    auto p = Parts();
    if (p.size() < 2) throw std::runtime_error("usage: f(x)=x^2+1 | f(x+h)-f(x)");
    std::string query = p.back();
    p.pop_back();
    CallResult r = g_fs.evaluateCall(p, query);
    std::string t = "substituted: " + r.substituted + "\nsimplified: " + r.simplified;
    if (r.hasValue) t += "\nvalue = " + Fmt(r.value);
    SetText(t);
}
ACTION(F_eqCall) {
    auto p = Parts();
    if (p.size() < 2) throw std::runtime_error("usage: f(x)=x^2+1 | f(t+1)=10");
    std::string eq = p.back();
    p.pop_back();
    CallEquationResult r = g_fs.solveCallEquation(p, eq);
    std::string t = "equation: " + r.equation + "\nunknown: " + std::string(1, r.unknown);
    if (r.identity) t += "\n(true for every value)";
    if (!r.factored.empty()) t += "\nfactored: " + r.factored;
    if (!r.roots.empty()) {
        t += "\nroots:";
        for (auto& z : r.roots) t += "  " + FmtComplex(z);
    }
    t += "\nreal roots: " + FmtReal(r.realRoots);
    SetText(t);
}

// ============================================================
//                    โหมด GRAPH (ใช้ MathGraph)
// ============================================================
static double g_gx0 = -10.0, g_gx1 = 10.0;
static bool g_plotted = false;
static std::function<void(MathGraph&, double, double)> g_plotFn;

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
        if (c == 27) {                                      // ESC [ ... letter
            if (i + 1 < s.size() && s[i + 1] == '[') {
                i += 2;
                while (i < s.size() && !isalpha((unsigned char)s[i])) ++i;
            }
        } else if (c == '\t') out += ' ';
        else if (c == '\n' || (c >= 32 && c < 127)) out += (char)c;
    }
    return out;
}
static void RenderPlot() {
    if (!g_plotted || !g_plotFn) return;
    Layout L = ComputeLayout(M_GRAPH);
    MathGraph g(std::max(11, L.outCols - 1), std::max(6, L.outRows - 5));
    std::string out;
    {
        CoutCapture cap;
        try { g_plotFn(g, g_gx0, g_gx1); }
        catch (const std::exception& e) { cap.ss << "[Error] " << e.what() << "\n"; }
        out = cap.ss.str();
    }
    int keep = g_scroll[M_GRAPH];
    g_results[M_GRAPH] = CleanAscii(out);
    g_scroll[M_GRAPH] = keep;
}
static void RunPlot(std::function<void(MathGraph&, double, double)> fn) {
    g_plotFn = fn;
    g_plotted = true;
    g_scroll[M_GRAPH] = 0;
    RenderPlot();
}
ACTION(G_plot) {
    std::string f = ReplaceAns(Part(0, "f(x)"));
    RunPlot([f](MathGraph& g, double a, double b) { g.plotFunction(g_calc, f, a, b); });
}
ACTION(G_deriv) {
    std::string f = ReplaceAns(Part(0, "f | x0")); double x0 = EvalNum(Part(1, "f | x0"));
    RunPlot([f, x0](MathGraph& g, double a, double b) { g.plotDerivative(g_calc, f, x0, a, b); });
}
ACTION(G_integ) {
    std::string f = ReplaceAns(Part(0, "f | a | b"));
    double lo = EvalNum(Part(1, "f | a | b")), hi = EvalNum(Part(2, "f | a | b"));
    RunPlot([f, lo, hi](MathGraph& g, double a, double b) { g.plotIntegralArea(g_calc, f, lo, hi, a, b); });
}
ACTION(G_lim) {
    std::string f = ReplaceAns(Part(0, "f | a")); double t = EvalNum(Part(1, "f | a"));
    RunPlot([f, t](MathGraph& g, double a, double b) { g.plotLimit(g_calc, f, t, a, b); });
}
ACTION(G_inv) {
    std::string f = ReplaceAns(Part(0, "f(x)"));
    RunPlot([f](MathGraph& g, double a, double b) { g.plotInverseFunction(g_calc, f, a, b); });
}
ACTION(G_area) {
    std::string f1 = ReplaceAns(Part(0, "f | g | a | b")), f2 = ReplaceAns(Part(1, "f | g | a | b"));
    double lo = EvalNum(Part(2, "f | g | a | b")), hi = EvalNum(Part(3, "f | g | a | b"));
    RunPlot([f1, f2, lo, hi](MathGraph& g, double a, double b) { g.plotAreaBetweenCurves(g_calc, f1, f2, lo, hi, a, b); });
}
ACTION(G_poly) {
    std::vector<double> co = NumList(Part(0, "a,b,c,... (highest power first)"));
    RunPlot([co](MathGraph& g, double a, double b) { g.plotPolynomialWithRoots(g_calc, co, a, b); });
}
ACTION(G_indef) {
    std::string f = ReplaceAns(Part(0, "f | x_ref"));
    auto p = Parts();
    double xr = (p.size() > 1 && !p[1].empty()) ? EvalNum(p[1]) : 0.0;
    RunPlot([f, xr](MathGraph& g, double a, double b) { g.plotIndefiniteIntegral(g_calc, f, xr, a, b); });
}
ACTION(G_pts) {
    std::vector<std::vector<double>> rows;
    for (auto& r : Split(Part(0, "x,y; x,y; ..."), ';')) {
        if (r.empty()) continue;
        auto xy = SplitTop(r, ", \t");
        if (xy.size() != 2) throw std::runtime_error("each point needs x,y");
        rows.push_back({EvalNum(xy[0]), EvalNum(xy[1])});
    }
    if (rows.empty()) throw std::runtime_error("no points");
    DataTable t(rows, {"x", "y"});
    RunPlot([t](MathGraph& g, double, double) { g.plotDataTable(t, 0, 1, "Data points"); });
}
static void G_zoomIn()  { double c = (g_gx0 + g_gx1) / 2, h = (g_gx1 - g_gx0) / 4; g_gx0 = c - h; g_gx1 = c + h; RenderPlot(); }
static void G_zoomOut() { double c = (g_gx0 + g_gx1) / 2, h = (g_gx1 - g_gx0);     g_gx0 = c - h; g_gx1 = c + h; RenderPlot(); }
static void G_left()    { double d = (g_gx1 - g_gx0) / 4; g_gx0 -= d; g_gx1 -= d; RenderPlot(); }
static void G_right()   { double d = (g_gx1 - g_gx0) / 4; g_gx0 += d; g_gx1 += d; RenderPlot(); }
static void G_reset()   { g_gx0 = -10; g_gx1 = 10; RenderPlot(); }

// ============================================================
//                    ปุ่มทั่วไป
// ============================================================
static void DoClear() {
    EXPR.clear(); RESULT.clear(); g_error.clear(); FRESH = false; g_scroll[g_mode] = 0;
    if (g_mode == M_GRAPH) g_plotted = false;
}
static void DoDelete() {
    if (FRESH) { DoClear(); return; }
    if (!EXPR.empty()) EXPR.pop_back();
    g_error.clear();
}
static void Insert(const std::string& s) {
    g_error.clear();
    if (g_mode == M_CALC && FRESH) {
        bool isOp = s.size() == 1 && strchr("+-*/^%", s[0]);
        EXPR = isOp ? "ans" + s : s;      // พิมพ์เครื่องหมายต่อจากคำตอบเดิมได้เลย
        FRESH = false;
        return;
    }
    EXPR += s;
}
static void DoEquals() {
    switch (g_mode) {
        case M_CALC:   C_equals(); break;
        case M_MATRIX: X_show(); break;
        case M_STATS:  S_all(); break;
        case M_FUNC:   F_describe(); break;
        case M_GRAPH:  G_plot(); break;
    }
}

// ---------- ตารางปุ่ม ----------
enum Col { C_NUM, C_OP, C_FUNC, C_CLEAR, C_EQ };
struct Key { const char* label; const char* ins; void (*fn)(); Col col; };
#define I(l, s, c)  {l, s, nullptr, c}
#define F(l, f, c)  {l, nullptr, f, c}
#define NOKEY       {"", nullptr, nullptr, C_NUM}

static const Key ACTION_KEYS[M_COUNT][25] = {
    { // CALC
        F("AC", DoClear, C_CLEAR), F("DEL", DoDelete, C_CLEAR), I("(", "(", C_OP), I(")", ")", C_OP), I("ans", "ans", C_FUNC),
        I("sin", "sin(", C_FUNC), I("cos", "cos(", C_FUNC), I("tan", "tan(", C_FUNC), I("^", "^", C_OP), I("sqrt", "sqrt(", C_FUNC),
        I("asin", "asin(", C_FUNC), I("acos", "acos(", C_FUNC), I("atan", "atan(", C_FUNC), I("ln", "ln(", C_FUNC), I("log", "log(", C_FUNC),
        I("exp", "exp(", C_FUNC), I("abs", "abs(", C_FUNC), I("fact", "fact(", C_FUNC), I("pi", "pi", C_FUNC), I("e", "e", C_FUNC),
        F("d/dx", C_deriv, C_FUNC), F("int", C_integ, C_FUNC), F("lim", C_limit, C_FUNC), I(";", ";", C_OP), I("A=B", "=", C_OP),
    },
    { // MATRIX
        F("A+B", X_add, C_FUNC), F("A-B", X_sub, C_FUNC), F("A*B", X_mul, C_FUNC), F("A*k", X_scalar, C_FUNC), F("solve", X_solve, C_FUNC),
        F("det", X_det, C_FUNC), F("inv", X_inv, C_FUNC), F("A^T", X_trans, C_FUNC), F("show", X_show, C_FUNC), I("|", "|", C_OP),
        I(";", ";", C_OP), I("sp", " ", C_OP), I("(", "(", C_OP), I(")", ")", C_OP), I("pi", "pi", C_FUNC),
        I("sin", "sin(", C_FUNC), I("cos", "cos(", C_FUNC), I("^", "^", C_OP), I("sqrt", "sqrt(", C_FUNC), I("e", "e", C_FUNC),
        I("ans", "ans", C_FUNC), NOKEY, NOKEY, NOKEY, NOKEY,
    },
    { // STATS
        F("sum", S_sum, C_FUNC), F("mean", S_mean, C_FUNC), F("min", S_min, C_FUNC), F("max", S_max, C_FUNC), F("median", S_med, C_FUNC),
        F("var", S_var, C_FUNC), F("stdev", S_sd, C_FUNC), F("nCr", S_nCr, C_FUNC), F("nPr", S_nPr, C_FUNC), F("E[X]", S_ex, C_FUNC),
        F("binP", S_binP, C_FUNC), F("binC", S_binC, C_FUNC), F("poiP", S_poiP, C_FUNC), F("poiC", S_poiC, C_FUNC), F("geoP", S_geo, C_FUNC),
        F("normP", S_norP, C_FUNC), F("normC", S_norC, C_FUNC), F("hyper", S_hyp, C_FUNC), F("binTbl", S_binTbl, C_FUNC), F("zTbl", S_zTbl, C_FUNC),
        I("|", "|", C_OP), I(";", ";", C_OP), I("(", "(", C_OP), I(")", ")", C_OP), I("sp", " ", C_OP),
    },
    { // FUNC
        F("describe", F_describe, C_FUNC), F("domain", F_domain, C_FUNC), F("range", F_range, C_FUNC), F("1-1", F_1to1, C_FUNC), F("mono", F_mono, C_FUNC),
        F("inv", F_inv, C_FUNC), F("f(a)", F_at, C_FUNC), F("f^-1(a)", F_invAt, C_FUNC), F("f=y", F_eqY, C_FUNC), F("rel", F_rel, C_FUNC),
        F("f+g", F_add, C_FUNC), F("f-g", F_sub, C_FUNC), F("f*g", F_mul, C_FUNC), F("f/g", F_div, C_FUNC), F("g.f", F_comp, C_FUNC),
        F("call", F_call, C_FUNC), F("eqcall", F_eqCall, C_FUNC), I("|", "|", C_OP), I("x", "x", C_FUNC), I("y", "y", C_FUNC),
        I("(", "(", C_OP), I(")", ")", C_OP), I("^", "^", C_OP), I("sqrt", "sqrt(", C_FUNC), I("abs", "abs(", C_FUNC),
    },
    { // GRAPH
        F("plot", G_plot, C_EQ), F("zoom+", G_zoomIn, C_FUNC), F("zoom-", G_zoomOut, C_FUNC), F("<", G_left, C_FUNC), F(">", G_right, C_FUNC),
        F("reset", G_reset, C_FUNC), F("d/dx", G_deriv, C_FUNC), F("int", G_integ, C_FUNC), F("lim", G_lim, C_FUNC), F("inv", G_inv, C_FUNC),
        F("area", G_area, C_FUNC), F("poly", G_poly, C_FUNC), F("F(x)", G_indef, C_FUNC), F("pts", G_pts, C_FUNC), I("|", "|", C_OP),
        I("x", "x", C_FUNC), I("^", "^", C_OP), I("(", "(", C_OP), I(")", ")", C_OP), I(";", ";", C_OP),
        I("sin", "sin(", C_FUNC), I("cos", "cos(", C_FUNC), I("tan", "tan(", C_FUNC), I("exp", "exp(", C_FUNC), I("ln", "ln(", C_FUNC),
    },
};

static const Key PAD_CALC[20] = {
    I("7","7",C_NUM), I("8","8",C_NUM), I("9","9",C_NUM), I("/","/",C_OP), I("x","x",C_FUNC),
    I("4","4",C_NUM), I("5","5",C_NUM), I("6","6",C_NUM), I("*","*",C_OP), F("STO", C_store, C_FUNC),
    I("1","1",C_NUM), I("2","2",C_NUM), I("3","3",C_NUM), I("-","-",C_OP), I("%","%",C_OP),
    I("0","0",C_NUM), I(".",".",C_NUM), I(",",",",C_OP), I("+","+",C_OP), F("=", DoEquals, C_EQ),
};
static const Key PAD_OTHER[20] = {
    I("7","7",C_NUM), I("8","8",C_NUM), I("9","9",C_NUM), I("/","/",C_OP), F("AC", DoClear, C_CLEAR),
    I("4","4",C_NUM), I("5","5",C_NUM), I("6","6",C_NUM), I("*","*",C_OP), F("DEL", DoDelete, C_CLEAR),
    I("1","1",C_NUM), I("2","2",C_NUM), I("3","3",C_NUM), I("-","-",C_OP), I("sp"," ",C_OP),
    I("0","0",C_NUM), I(".",".",C_NUM), I(",",",",C_OP), I("+","+",C_OP), F("=", DoEquals, C_EQ),
};

static uint32_t ColorOf(Col c) {
    switch (c) {
        case C_NUM:   return RGB32(78, 84, 96);
        case C_OP:    return RGB32(214, 130, 40);
        case C_FUNC:  return RGB32(55, 110, 200);
        case C_CLEAR: return RGB32(190, 60, 60);
        case C_EQ:    return RGB32(46, 160, 90);
    }
    return RGB32(80, 80, 80);
}
static void Activate(const Key& k) {
    if (k.fn) k.fn();
    else if (k.ins) Insert(k.ins);
}
static void SwitchMode(int m) {
    if (m == g_mode) return;
    g_mode = m;
    g_error.clear();
    if (m == M_GRAPH) RenderPlot();
}

// ---------- เรียงข้อความเป็นบรรทัด ----------
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

// ---------- วาดหน้าจอ ----------
static void Frame() {
    Layout L = ComputeLayout(g_mode);
    FillRectPx(0, 0, g_w, g_h, RGB32(32, 34, 40));

    // แท็บโหมด
    int tabGap = 4;
    int tabW = (g_w - 2 * L.M - (M_COUNT - 1) * tabGap) / M_COUNT;
    for (int i = 0; i < M_COUNT; ++i) {
        uint32_t base = (i == g_mode) ? RGB32(46, 160, 90) : RGB32(60, 64, 76);
        if (DrawButton(200 + i, L.M + i * (tabW + tabGap), L.tabY, tabW, L.tabH, MODE_NAMES[i], base))
            SwitchMode(i);
    }

    // จอแสดงผล
    FillRectPx(L.dispX, L.dispY, L.dispW, L.dispH, RGB32(18, 20, 24));
    Border(L.dispX, L.dispY, L.dispW, L.dispH, RGB32(70, 74, 84));
    int pad = 8, fhS = FontH(g_fontS), cwS = CharW(g_fontS);
    int fhM = FontH(g_fontM);

    // นิพจน์ที่พิมพ์ (สูงสุด 2 บรรทัด แสดงส่วนท้ายสุด)
    {
        std::vector<std::string> ls;
        int per = std::max(1, L.innerW / cwS);
        for (size_t i = 0; i < EXPR.size(); i += per) ls.push_back(EXPR.substr(i, per));
        if (ls.size() > 2) ls.erase(ls.begin(), ls.end() - 2);
        if (ls.empty()) ls.push_back("");
        for (size_t i = 0; i < ls.size(); ++i)
            DrawText_(L.dispX + pad, L.dispY + 8 + (int)i * (fhS + 2),
                      ls[i] + (i + 1 == ls.size() ? "_" : ""), RGB32(200, 204, 214), g_fontS);
    }
    FillRectPx(L.dispX + pad, L.outTop - 5, L.dispW - 2 * pad, 1, RGB32(60, 64, 74));

    // ผลลัพธ์ / ข้อผิดพลาด
    g_maxScroll = 0;
    if (!g_error.empty()) {
        auto ls = BuildLines(g_error, L.innerW / CharW(g_fontM), true);
        for (size_t i = 0; i < ls.size() && (int)i < L.outRows; ++i)
            DrawText_(L.dispX + pad, L.outTop + (int)i * (fhM + 1), ls[i], RGB32(255, 110, 110), g_fontM);
    } else if (!RESULT.empty()) {
        int cwL = CharW(g_fontL), fhL = FontH(g_fontL);
        if (g_mode == M_CALC && RESULT.find('\n') == std::string::npos &&
            (int)RESULT.size() * cwL <= L.innerW) {
            DrawTextRight(L.dispX + L.dispW - pad, L.outTop + (L.outH - fhL) / 2, RESULT,
                          RGB32(120, 230, 150), g_fontL);
        } else {
            bool wrap = (g_mode != M_GRAPH);
            auto ls = BuildLines(RESULT, L.innerW / CharW(g_fontM), wrap);
            g_maxScroll = std::max(0, (int)ls.size() - L.outRows);
            g_scroll[g_mode] = std::clamp(g_scroll[g_mode], 0, g_maxScroll);
            for (int i = 0; i < L.outRows && i + g_scroll[g_mode] < (int)ls.size(); ++i)
                DrawText_(L.dispX + pad, L.outTop + i * (fhM + 1), ls[i + g_scroll[g_mode]],
                          RGB32(120, 230, 150), g_fontM);
            if (g_maxScroll > 0) {
                char b[32];
                snprintf(b, sizeof b, "[%d/%d]", g_scroll[g_mode] + 1, g_maxScroll + 1);
                DrawTextRight(L.dispX + L.dispW - 4, L.dispY + 4, b, RGB32(130, 134, 146), g_fontM);
            }
        }
    }

    DrawText_(L.M, L.hintY, HINTS[g_mode], RGB32(130, 134, 146), g_fontM);

    // ปุ่ม
    for (int r = 0; r < ROWS; ++r) {
        for (int c = 0; c < COLS; ++c) {
            const Key& k = (r < 5) ? ACTION_KEYS[g_mode][r * COLS + c]
                                   : (g_mode == M_CALC ? PAD_CALC : PAD_OTHER)[(r - 5) * COLS + c];
            if (!k.label[0]) continue;
            int x = L.M + c * (L.bw + L.GAP), y = L.keyTop + r * (L.bh + L.GAP);
            if (DrawButton(r * COLS + c + 1, x, y, L.bw, L.bh, k.label, ColorOf(k.col)))
                Activate(k);
        }
    }
}

static void Present() {
    BeginFrame(); Frame(); EndFrame();
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

int main() {
    g_dpy = XOpenDisplay(nullptr);
    if (!g_dpy) { fprintf(stderr, "Cannot open X display\n"); return 1; }
    int screen = DefaultScreen(g_dpy);
    const int W0 = 480, H0 = 800;
    g_win = XCreateSimpleWindow(g_dpy, RootWindow(g_dpy, screen), 0, 0, W0, H0, 0,
                                BlackPixel(g_dpy, screen), WhitePixel(g_dpy, screen));
    XStoreName(g_dpy, g_win, "Scientific Calculator");

    XSizeHints hints;
    memset(&hints, 0, sizeof hints);
    hints.flags = PMinSize;
    hints.min_width = 420; hints.min_height = 700;
    XSetWMNormalHints(g_dpy, g_win, &hints);

    XSelectInput(g_dpy, g_win, ExposureMask | StructureNotifyMask | PointerMotionMask |
                               ButtonPressMask | ButtonReleaseMask | KeyPressMask);
    Atom wmDelete = XInternAtom(g_dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(g_dpy, g_win, &wmDelete, 1);

    g_gc = XCreateGC(g_dpy, g_win, 0, nullptr);
    g_fontS = LoadFont("9x15", "8x13", "fixed");
    g_fontM = LoadFont("6x13", "7x13", "fixed");
    g_fontL = LoadFont("10x20", "9x15", "fixed");
    if (!g_fontM) g_fontM = g_fontS;
    if (!g_fontL) g_fontL = g_fontS;

    CreateBuffer(W0, H0);
    XMapWindow(g_dpy, g_win);

    bool running = true;
    while (running) {
        XEvent e;
        XNextEvent(g_dpy, &e);
        switch (e.type) {
        case ConfigureNotify:
            if (e.xconfigure.width != g_w || e.xconfigure.height != g_h) {
                CreateBuffer(e.xconfigure.width, e.xconfigure.height);
                if (g_mode == M_GRAPH) RenderPlot();
            }
            break;
        case MotionNotify: ui.mx = e.xmotion.x; ui.my = e.xmotion.y; break;
        case ButtonPress:
            if (e.xbutton.button == Button1) { ui.mx = e.xbutton.x; ui.my = e.xbutton.y; ui.down = ui.pressed = true; }
            else if (e.xbutton.button == Button4) g_scroll[g_mode] = std::max(0, g_scroll[g_mode] - 3);
            else if (e.xbutton.button == Button5) g_scroll[g_mode] = std::min(g_maxScroll, g_scroll[g_mode] + 3);
            break;
        case ButtonRelease:
            if (e.xbutton.button == Button1) { ui.mx = e.xbutton.x; ui.my = e.xbutton.y; ui.down = false; ui.released = true; }
            break;
        case KeyPress: {
            char buf[16];
            KeySym ks = NoSymbol;
            int n = XLookupString(&e.xkey, buf, sizeof buf - 1, &ks, nullptr);
            if (ks == XK_Escape || ks == XK_Delete) DoClear();
            else if (ks == XK_BackSpace) DoDelete();
            else if (ks == XK_Return || ks == XK_KP_Enter) DoEquals();
            else if (ks == XK_Up)   g_scroll[g_mode] = std::max(0, g_scroll[g_mode] - 1);
            else if (ks == XK_Down) g_scroll[g_mode] = std::min(g_maxScroll, g_scroll[g_mode] + 1);
            else if (ks == XK_Prior) g_scroll[g_mode] = std::max(0, g_scroll[g_mode] - 8);
            else if (ks == XK_Next)  g_scroll[g_mode] = std::min(g_maxScroll, g_scroll[g_mode] + 8);
            else if (n == 1 && isprint((unsigned char)buf[0])) Insert(std::string(1, buf[0]));
            break;
        }
        case ClientMessage:
            if ((Atom)e.xclient.data.l[0] == wmDelete) running = false;
            break;
        }
        // Redraw once the queue is drained (collapses bursts of motion events).
        if (running && XPending(g_dpy) == 0) Present();
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