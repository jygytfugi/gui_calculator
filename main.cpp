// เครื่องคิดเลขแบบมีปุ่มกด (GUI) สำหรับ Linux: Xlib + software framebuffer + immediate-mode widgets
// ใช้ Calculator / AlphaSolverEq เดิมของโปรเจกต์เป็นตัวคำนวณ (แก้เฉพาะ main.cpp)
//
// ติดตั้ง header (Debian/Ubuntu): sudo apt install libx11-dev
// Build (ไฟล์ main.cpp อยู่ใน src/ เหมือนไฟล์ .cpp อื่น ๆ ที่ include "../lib/..."):
//   g++ -std=c++17 main.cpp Calculator_calculus.cpp algebra.cpp Alpha_solver_eq.cpp matrix.cpp -o calc -lX11
//g++ -std=c++17 main.cpp src/Calculator_calculus.cpp src/algebra.cpp src/Alpha_solver_eq.cpp src/matrix.cpp -o calc -lX11
// (ไม่ต้องลิงก์ menu.cpp เพราะใช้ conio.h ของ Windows)
//
// วิธีใช้:
//   =        คำนวณนิพจน์ เช่น 2*(3+4)^2, sin(pi/2), sqrt(2)
//   A=B      ใส่เครื่องหมาย "=" ในสมการ แล้วกด "=" จะแก้สมการ เช่น x^2-4=0 , 2x+3=ans
//   d/dx     นิพจน์เดี่ยว -> อนุพันธ์เชิงสัญลักษณ์ ; "f;x0" -> ค่าอนุพันธ์ที่ x0
//   int      นิพจน์เดี่ยว -> ปริพันธ์ไม่จำกัดเขต ; "f;a;b" -> ปริพันธ์จำกัดเขต
//   lim      "f;a" หรือ "f;a;+" / "f;a;-" (ลิมิตด้านขวา/ซ้าย) ; a เป็น inf / -inf ได้
//   STO      เก็บคำตอบล่าสุดลงตัวแปร x
//   ans      คำตอบล่าสุด    (ตรีโกณมิติใช้หน่วยเรเดียน)
// คีย์บอร์ด: พิมพ์ได้ตามปกติ, Enter = คำนวณ, Backspace = ลบ, Esc/Delete = ล้างทั้งหมด
#include "lib/Calculator_calculus.hpp"

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
#include <limits>
#include <string>
#include <vector>

#define RGB32(r,g,b) ((uint32_t)(((r)<<16)|((g)<<8)|(b)))

// ---------- X11 / framebuffer state ----------
static Display* g_dpy = nullptr;
static Window g_win;
static GC g_gc;
static XFontStruct* g_fontS = nullptr;   // ฟอนต์เล็ก (ปุ่ม/นิพจน์)
static XFontStruct* g_fontL = nullptr;   // ฟอนต์ใหญ่ (ผลลัพธ์)
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

// y = ขอบบนของข้อความ
static void DrawText_(int x, int y, const std::string& s, uint32_t c, XFontStruct* f) {
    g_text.push_back({x, y, s, c, f});
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
//                    Calculator logic
// ============================================================
static Calculator g_calc;
static std::string g_expr;       // นิพจน์ที่กำลังพิมพ์
static std::string g_result;     // ผลลัพธ์ล่าสุด
static std::string g_error;      // ข้อความผิดพลาด (ว่าง = ไม่มี)
static double g_ans = 0.0;
static bool g_fresh = false;     // true = เพิ่งกด "=" (พิมพ์ต่อแล้วจะเริ่มใหม่ / ต่อด้วย ans)

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

// แทนคำว่า "ans" (เฉพาะทั้งคำ) ด้วยค่าล่าสุด
static std::string ReplaceAns(const std::string& s) {
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

static double EvalNum(const std::string& s) { return g_calc.evaluate(ReplaceAns(s)); }

static void SetValue(double v) {
    g_ans = v;
    g_result = Fmt(v);
    g_fresh = true;
}

static void DoEquals() {
    g_error.clear();
    std::string e = Trim(g_expr);
    if (e.empty()) return;
    try {
        g_calc.solver().setAns(g_ans);
        if (e.find(';') != std::string::npos)
            throw std::runtime_error("';' is used with d/dx, int, lim");
        std::string full = ReplaceAns(e);
        if (full.find('=') != std::string::npos) {
            char var = 'x';
            try { var = AlphaSolverEq::detectVariable(full); } catch (...) {}
            double v = g_calc.solver().solveTwoSideEquation(g_calc, full);
            SetValue(v);
            g_result = std::string(1, var) + " = " + Fmt(v);
        } else {
            SetValue(g_calc.evaluate(full));
        }
    } catch (const std::exception& ex) {
        g_error = ex.what();
        g_result.clear();
    }
}

static void DoDerivative() {
    g_error.clear();
    std::string e = Trim(g_expr);
    if (e.empty()) return;
    try {
        auto p = Split(e, ';');
        if (p.size() == 1) {
            g_result = g_calc.symbolicDerivative(ReplaceAns(p[0]));
            g_fresh = true;
        } else if (p.size() == 2) {
            SetValue(g_calc.derivative(ReplaceAns(p[0]), EvalNum(p[1])));
        } else throw std::runtime_error("usage: f   or   f;x0");
    } catch (const std::exception& ex) { g_error = ex.what(); g_result.clear(); }
}

static void DoIntegral() {
    g_error.clear();
    std::string e = Trim(g_expr);
    if (e.empty()) return;
    try {
        auto p = Split(e, ';');
        if (p.size() == 1) {
            g_result = g_calc.symbolicIntegral(ReplaceAns(p[0])) + " + C";
            g_fresh = true;
        } else if (p.size() == 3) {
            SetValue(g_calc.integral(ReplaceAns(p[0]), EvalNum(p[1]), EvalNum(p[2])));
        } else throw std::runtime_error("usage: f   or   f;a;b");
    } catch (const std::exception& ex) { g_error = ex.what(); g_result.clear(); }
}

static void DoLimit() {
    g_error.clear();
    std::string e = Trim(g_expr);
    if (e.empty()) return;
    try {
        auto p = Split(e, ';');
        if (p.size() != 2 && p.size() != 3) throw std::runtime_error("usage: f;a   or   f;a;+  /  f;a;-");
        char side = '\0';
        if (p.size() == 3) {
            if (p[2] == "+") side = '+';
            else if (p[2] == "-") side = '-';
            else throw std::runtime_error("side must be + or -");
        }
        SetValue(g_calc.limit(ReplaceAns(p[0]), EvalNum(p[1]), 1e-6, side));
    } catch (const std::exception& ex) { g_error = ex.what(); g_result.clear(); }
}

static void DoStore() {
    g_error.clear();
    g_calc.setVariable('x', g_ans);
    g_result = "x = " + Fmt(g_ans);
}

static void DoClear() { g_expr.clear(); g_result.clear(); g_error.clear(); g_fresh = false; }
static void DoDelete() {
    if (g_fresh) { DoClear(); return; }
    if (!g_expr.empty()) g_expr.pop_back();
    g_error.clear();
}

static void Insert(const std::string& s) {
    g_error.clear();
    if (g_fresh) {
        bool isOp = s.size() == 1 && strchr("+-*/^%", s[0]);
        g_expr = isOp ? "ans" + s : s;      // ต่อจากคำตอบเดิมถ้าเป็นเครื่องหมาย
        g_fresh = false;
        return;
    }
    g_expr += s;
}

// ---------- ปุ่ม ----------
enum Kind { K_INS, K_AC, K_DEL, K_EQ, K_DERIV, K_INT, K_LIM, K_STO };
enum Col  { C_NUM, C_OP, C_FUNC, C_CLEAR, C_EQ };
struct Key { const char* label; const char* ins; Kind kind; Col col; };

static const int COLS = 5, ROWS = 9;
static const Key KEYS[ROWS * COLS] = {
    {"AC","",K_AC,C_CLEAR},{"DEL","",K_DEL,C_CLEAR},{"(","(",K_INS,C_OP},{")",")",K_INS,C_OP},{"ans","ans",K_INS,C_FUNC},
    {"sin","sin(",K_INS,C_FUNC},{"cos","cos(",K_INS,C_FUNC},{"tan","tan(",K_INS,C_FUNC},{"^","^",K_INS,C_OP},{"sqrt","sqrt(",K_INS,C_FUNC},
    {"asin","asin(",K_INS,C_FUNC},{"acos","acos(",K_INS,C_FUNC},{"atan","atan(",K_INS,C_FUNC},{"ln","ln(",K_INS,C_FUNC},{"log","log(",K_INS,C_FUNC},
    {"exp","exp(",K_INS,C_FUNC},{"abs","abs(",K_INS,C_FUNC},{"fact","fact(",K_INS,C_FUNC},{"pi","pi",K_INS,C_FUNC},{"e","e",K_INS,C_FUNC},
    {"d/dx","",K_DERIV,C_FUNC},{"int","",K_INT,C_FUNC},{"lim","",K_LIM,C_FUNC},{";",";",K_INS,C_OP},{"A=B","=",K_INS,C_OP},
    {"7","7",K_INS,C_NUM},{"8","8",K_INS,C_NUM},{"9","9",K_INS,C_NUM},{"/","/",K_INS,C_OP},{"x","x",K_INS,C_FUNC},
    {"4","4",K_INS,C_NUM},{"5","5",K_INS,C_NUM},{"6","6",K_INS,C_NUM},{"*","*",K_INS,C_OP},{"STO","",K_STO,C_FUNC},
    {"1","1",K_INS,C_NUM},{"2","2",K_INS,C_NUM},{"3","3",K_INS,C_NUM},{"-","-",K_INS,C_OP},{"%","%",K_INS,C_OP},
    {"0","0",K_INS,C_NUM},{".",".",K_INS,C_NUM},{",",",",K_INS,C_OP},{"+","+",K_INS,C_OP},{"=","",K_EQ,C_EQ},
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
    switch (k.kind) {
        case K_INS:   Insert(k.ins); break;
        case K_AC:    DoClear(); break;
        case K_DEL:   DoDelete(); break;
        case K_EQ:    DoEquals(); break;
        case K_DERIV: DoDerivative(); break;
        case K_INT:   DoIntegral(); break;
        case K_LIM:   DoLimit(); break;
        case K_STO:   DoStore(); break;
    }
}

// แบ่งข้อความเป็นบรรทัด แล้วเก็บเฉพาะ maxLines บรรทัดสุดท้าย
static std::vector<std::string> WrapTail(const std::string& s, int perLine, int maxLines) {
    std::vector<std::string> lines;
    perLine = std::max(1, perLine);
    for (size_t i = 0; i < s.size(); i += perLine) lines.push_back(s.substr(i, perLine));
    if (lines.empty()) lines.push_back("");
    if ((int)lines.size() > maxLines) lines.erase(lines.begin(), lines.end() - maxLines);
    return lines;
}

// ---------- วาดหน้าจอ ----------
static void Frame() {
    FillRectPx(0, 0, g_w, g_h, RGB32(32, 34, 40));

    const int M = 10, GAP = 6;
    const int dispH = 150;
    FillRectPx(M, M, g_w - 2 * M, dispH, RGB32(18, 20, 24));
    Border(M, M, g_w - 2 * M, dispH, RGB32(70, 74, 84));

    // นิพจน์ (ฟอนต์เล็ก, สูงสุด 2 บรรทัด, แสดงส่วนท้ายสุด)
    int pad = 8;
    int innerW = g_w - 2 * M - 2 * pad;
    int cwS = CharW(g_fontS), fhS = FontH(g_fontS);
    auto exprLines = WrapTail(g_expr.empty() ? "" : g_expr, innerW / cwS, 2);
    for (size_t i = 0; i < exprLines.size(); ++i)
        DrawTextRight(g_w - M - pad, M + 8 + (int)i * (fhS + 2), exprLines[i], RGB32(200, 204, 214), g_fontS);
    if (g_expr.empty() && g_result.empty() && g_error.empty())
        DrawTextRight(g_w - M - pad, M + 8, "0", RGB32(120, 124, 134), g_fontS);

    FillRectPx(M + pad, M + 52, g_w - 2 * M - 2 * pad, 1, RGB32(60, 64, 74));

    // ผลลัพธ์ / ข้อผิดพลาด
    int areaTop = M + 60, areaH = dispH - 60 - 6;
    if (!g_error.empty()) {
        auto ls = WrapTail(g_error, innerW / cwS, std::max(1, areaH / (fhS + 2)));
        for (size_t i = 0; i < ls.size(); ++i)
            DrawText_(M + pad, areaTop + (int)i * (fhS + 2), ls[i], RGB32(255, 110, 110), g_fontS);
    } else if (!g_result.empty()) {
        int cwL = CharW(g_fontL), fhL = FontH(g_fontL);
        if ((int)g_result.size() * cwL <= innerW) {
            DrawTextRight(g_w - M - pad, areaTop + (areaH - fhL) / 2, g_result, RGB32(120, 230, 150), g_fontL);
        } else {   // ยาวเกิน -> ใช้ฟอนต์เล็กตัดบรรทัด (เช่น สูตรอนุพันธ์)
            auto ls = WrapTail(g_result, innerW / cwS, std::max(1, areaH / (fhS + 2)));
            for (size_t i = 0; i < ls.size(); ++i)
                DrawText_(M + pad, areaTop + (int)i * (fhS + 2), ls[i], RGB32(120, 230, 150), g_fontS);
        }
    }

    // บรรทัดช่วยเหลือ
    DrawText_(M, M + dispH + 6, "f;x0 d/dx | f;a;b int | f;a lim | f=g solve | Esc clear",
              RGB32(130, 134, 146), g_fontS);

    // ปุ่ม
    int top = M + dispH + 6 + fhS + 8;
    int bw = (g_w - 2 * M - (COLS - 1) * GAP) / COLS;
    int bh = std::max(20, (g_h - top - M - (ROWS - 1) * GAP) / ROWS);
    for (int r = 0; r < ROWS; ++r) {
        for (int c = 0; c < COLS; ++c) {
            const Key& k = KEYS[r * COLS + c];
            int x = M + c * (bw + GAP), y = top + r * (bh + GAP);
            if (DrawButton(r * COLS + c + 1, x, y, bw, bh, k.label, ColorOf(k.col)))
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
    const int W0 = 440, H0 = 700;
    g_win = XCreateSimpleWindow(g_dpy, RootWindow(g_dpy, screen), 0, 0, W0, H0, 0,
                                BlackPixel(g_dpy, screen), WhitePixel(g_dpy, screen));
    XStoreName(g_dpy, g_win, "Scientific Calculator");

    XSizeHints hints;
    memset(&hints, 0, sizeof hints);
    hints.flags = PMinSize;
    hints.min_width = 380; hints.min_height = 600;
    XSetWMNormalHints(g_dpy, g_win, &hints);

    XSelectInput(g_dpy, g_win, ExposureMask | StructureNotifyMask | PointerMotionMask |
                               ButtonPressMask | ButtonReleaseMask | KeyPressMask);
    Atom wmDelete = XInternAtom(g_dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(g_dpy, g_win, &wmDelete, 1);

    g_gc = XCreateGC(g_dpy, g_win, 0, nullptr);
    g_fontS = LoadFont("9x15", "8x13", "fixed");
    g_fontL = LoadFont("10x20", "9x15", "fixed");
    if (!g_fontL) g_fontL = g_fontS;

    CreateBuffer(W0, H0);
    XMapWindow(g_dpy, g_win);

    bool running = true;
    while (running) {
        XEvent e;
        XNextEvent(g_dpy, &e);
        switch (e.type) {
        case ConfigureNotify:
            if (e.xconfigure.width != g_w || e.xconfigure.height != g_h)
                CreateBuffer(e.xconfigure.width, e.xconfigure.height);
            break;
        case MotionNotify: ui.mx = e.xmotion.x; ui.my = e.xmotion.y; break;
        case ButtonPress:
            if (e.xbutton.button == Button1) { ui.mx = e.xbutton.x; ui.my = e.xbutton.y; ui.down = ui.pressed = true; }
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
    if (g_fontS) XFreeFont(g_dpy, g_fontS);
    XFreeGC(g_dpy, g_gc);
    XDestroyWindow(g_dpy, g_win);
    XCloseDisplay(g_dpy);
    return 0;
}