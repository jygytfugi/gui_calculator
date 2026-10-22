#include "../lib/function_solver.hpp"
#include <cmath>
#include <cctype>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <functional>
#include <stdexcept>
#include <cstdlib>

// =====================================================================================
//  ข้อจำกัดที่ควรรู้ (ส่วน f(x) ทำงานแบบตัวเลข):
//   - โดเมน/เรนจ์/เพิ่มลด คำนวณจากการสแกนในช่วง [-window, window]; ปลายที่สแกนแล้วยังใช้ได้ จะถือว่า
//     ต่อไปถึง ±infinity (ถ้าคำตอบอยู่นอกช่วง ให้เพิ่ม setScanWindow)
//   - จุดที่ฟังก์ชันไม่นิยามแบบ "แคบกว่าความละเอียดกริด" และไม่ใช่จุดค่าตรง ๆ (k/stepsPerUnit) จะตรวจพบ
//     เฉพาะกรณีที่ |f| พุ่งสูงมาก (เสา/pole เช่น 1/(x-0.123)); รูโหว่แบบถอดได้ที่ไม่ใช่จุดกริดอาจหลุด
//   - ปลายเปิด/ปิดของช่วงที่ไม่ใช่ "เลขสวย" ใช้ heuristic (|f| ใหญ่ที่ขอบ -> เปิด)
//   - ปลายที่เข้าใกล้ค่าหนึ่งช้ามาก (เช่น x^0.01) อาจถูกจัดว่าไปถึง infinity
// =====================================================================================

namespace {

const double INF = std::numeric_limits<double>::infinity();

// ปัดเลขสวย (ทศนิยม 6 ตำแหน่ง) ถ้าอยู่ใกล้ในระยะ tol
double snapNice(double v, double tol = 1e-9, bool* snapped = nullptr) {
    double r = std::round(v * 1e6) / 1e6;
    if (r == 0.0) r = 0.0;
    if (std::fabs(v - r) < tol) { if (snapped) *snapped = true; return r; }
    if (snapped) *snapped = false;
    return v;
}

std::string fmtNum(double v) {              // สำหรับแสดงผล
    if (std::isinf(v)) return v > 0 ? "inf" : "-inf";
    if (v == 0.0) v = 0.0;
    std::ostringstream o; o << std::setprecision(10) << v; return o.str();
}

std::string exprNum(double v) {             // สำหรับสร้างนิพจน์ (ความแม่นยำเต็ม)
    std::ostringstream o; o << std::setprecision(17) << v;
    return (v < 0) ? "(" + o.str() + ")" : o.str();
}

bool isKnownName(const std::string& w) {
    static const char* names[] = {"sin","cos","tan","asin","acos","atan","sqrt","root","log","ln",
                                  "exp","abs","fact","pi","inf","infinity"};
    for (auto n : names) if (w == n) return true;
    return false;
}

// เติม * ที่ถูกละไว้: 2x -> 2*x, 2(x+1) -> 2*(x+1), )( -> )*(, xy -> x*y, 2sin(x) -> 2*sin(x)
std::string insertImplicitMultiplication(const std::string& in) {
    enum Prev { None, Op, Num, Var, Func, LParen, RParen };
    std::string out;
    Prev prev = None;
    size_t i = 0, n = in.size();
    auto needMul = [&]() { return prev == Num || prev == Var || prev == RParen; };

    while (i < n) {
        unsigned char c = static_cast<unsigned char>(in[i]);
        if (std::isspace(c)) { ++i; continue; }

        if (std::isdigit(c) || c == '.') {
            size_t j = i;
            while (j < n && (std::isdigit(static_cast<unsigned char>(in[j])) || in[j] == '.')) ++j;
            if (needMul()) out += '*';
            out += in.substr(i, j - i);
            prev = Num; i = j; continue;
        }
        if (std::isalpha(c)) {
            size_t j = i;
            while (j < n && std::isalpha(static_cast<unsigned char>(in[j]))) ++j;
            std::string word = in.substr(i, j - i);
            size_t k = j;
            while (k < n && std::isspace(static_cast<unsigned char>(in[k]))) ++k;
            bool callNext = (k < n && in[k] == '(');
            if (isKnownName(word)) {
                if (needMul()) out += '*';
                out += word;
                prev = (callNext && word != "pi" && word != "inf" && word != "infinity") ? Func : Var;
            } else {
                for (char ch : word) {
                    if (needMul()) out += '*';
                    out += ch; prev = Var;
                }
            }
            i = j; continue;
        }
        if (c == '(') { if (needMul()) out += '*'; out += '('; prev = LParen; ++i; continue; }
        if (c == ')') { out += ')'; prev = RParen; ++i; continue; }
        out += static_cast<char>(c); prev = Op; ++i;
    }
    return out;
}

// แทนที่ตัวแปรตัวอักษรเดียว var ด้วยข้อความ repl (ใช้กับสตริงที่ผ่าน insertImplicitMultiplication แล้ว)
std::string replaceVar(const std::string& e, char var, const std::string& repl) {
    std::string out;
    size_t i = 0, n = e.size();
    while (i < n) {
        if (std::isalpha(static_cast<unsigned char>(e[i]))) {
            size_t j = i;
            while (j < n && std::isalpha(static_cast<unsigned char>(e[j]))) ++j;
            if (j - i == 1 && e[i] == var) out += repl;
            else out += e.substr(i, j - i);
            i = j;
        } else out += e[i++];
    }
    return out;
}

std::string replaceWord(const std::string& e, const std::string& word, const std::string& repl) {
    std::string out;
    size_t i = 0, n = e.size();
    while (i < n) {
        if (std::isalpha(static_cast<unsigned char>(e[i]))) {
            size_t j = i;
            while (j < n && std::isalpha(static_cast<unsigned char>(e[j]))) ++j;
            std::string w = e.substr(i, j - i);
            out += (w == word) ? repl : w;
            i = j;
        } else out += e[i++];
    }
    return out;
}

std::string swapXYImpl(const std::string& e) {
    std::string out;
    size_t i = 0, n = e.size();
    while (i < n) {
        if (std::isalpha(static_cast<unsigned char>(e[i]))) {
            size_t j = i;
            while (j < n && std::isalpha(static_cast<unsigned char>(e[j]))) ++j;
            std::string w = e.substr(i, j - i);
            if (w == "x") w = "y"; else if (w == "y") w = "x";
            out += w; i = j;
        } else out += e[i++];
    }
    return out;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// ---------- ช่วงจำนวนจริง ----------
bool approxEq(double a, double b) {
    if (std::isinf(a) || std::isinf(b)) return a == b;
    return std::fabs(a - b) <= 1e-6 * (1.0 + std::max(std::fabs(a), std::fabs(b)));
}

std::vector<Interval> mergeIntervals(std::vector<Interval> v) {
    v.erase(std::remove_if(v.begin(), v.end(), [](const Interval& i) { return i.isEmpty(); }), v.end());
    std::sort(v.begin(), v.end(), [](const Interval& a, const Interval& b) {
        if (a.lo != b.lo) return a.lo < b.lo;
        return a.loClosed && !b.loClosed;
    });
    std::vector<Interval> out;
    for (const auto& iv : v) {
        if (!out.empty()) {
            Interval& c = out.back();
            if (iv.lo < c.hi || (iv.lo == c.hi && (c.hiClosed || iv.loClosed))) {
                if (iv.hi > c.hi) { c.hi = iv.hi; c.hiClosed = iv.hiClosed; }
                else if (iv.hi == c.hi) c.hiClosed = c.hiClosed || iv.hiClosed;
                continue;
            }
        }
        out.push_back(iv);
    }
    return out;
}

Interval intersect(const Interval& a, const Interval& b) {
    Interval r;
    if (a.lo > b.lo || (a.lo == b.lo && !a.loClosed)) { r.lo = a.lo; r.loClosed = a.loClosed; }
    else { r.lo = b.lo; r.loClosed = b.loClosed; }
    if (a.hi < b.hi || (a.hi == b.hi && !a.hiClosed)) { r.hi = a.hi; r.hiClosed = a.hiClosed; }
    else { r.hi = b.hi; r.hiClosed = b.hiClosed; }
    return r;   // อาจว่าง (isEmpty)
}

bool subsetOf(const Interval& a, const Interval& b) {   // a ⊆ b
    if (a.isEmpty()) return true;
    bool loOk = (a.lo > b.lo) || (approxEq(a.lo, b.lo) && (!a.loClosed || b.loClosed));
    bool hiOk = (a.hi < b.hi) || (approxEq(a.hi, b.hi) && (!a.hiClosed || b.hiClosed));
    return loOk && hiOk;
}

bool coveredBy(const Interval& A, const std::vector<Interval>& cover) {   // A ⊆ union(cover)
    for (const auto& c : mergeIntervals(cover)) if (subsetOf(A, c)) return true;
    return false;
}

bool sameInterval(const Interval& a, const Interval& b) {
    return approxEq(a.lo, b.lo) && approxEq(a.hi, b.hi) &&
           (std::isinf(a.lo) || a.loClosed == b.loClosed) &&
           (std::isinf(a.hi) || a.hiClosed == b.hiClosed);
}

// ---------- ตัวช่วยตัวเลข ----------
using ScalarFn = std::function<double(double)>;

// golden-section: หา min ของ g บน [lo,hi] (g อาจคืน INF ได้)
double goldenMin(const ScalarFn& g, double lo, double hi, double& xmin) {
    const double r = 0.3819660112501051;
    for (int it = 0; it < 120 && hi - lo > 1e-15 * (1.0 + std::fabs(lo)); ++it) {
        double m1 = lo + (hi - lo) * r, m2 = hi - (hi - lo) * r;
        if (g(m1) < g(m2)) hi = m2; else lo = m1;
    }
    xmin = 0.5 * (lo + hi);
    return g(xmin);
}

using EvalFn = std::function<bool(double, double&)>;

// หารากของ fn บน [L,H]: จุดเปลี่ยนเครื่องหมาย + ค่าศูนย์ตรง ๆ (+ รากแบบสัมผัสถ้า tangent = true)
std::vector<double> findRoots(const EvalFn& fn, double L, double H, int N, bool tangent) {
    std::vector<double> xs(N + 1), vs(N + 1);
    std::vector<char> ok(N + 1);
    for (int i = 0; i <= N; ++i) {
        xs[i] = L + (H - L) * static_cast<double>(i) / N;
        ok[i] = fn(xs[i], vs[i]);
    }
    std::vector<double> roots;
    for (int i = 0; i <= N; ++i) if (ok[i] && vs[i] == 0.0) roots.push_back(xs[i]);

    for (int i = 0; i < N; ++i) {
        if (!(ok[i] && ok[i + 1]) || !(vs[i] * vs[i + 1] < 0.0)) continue;
        double a = xs[i], b = xs[i + 1], va = vs[i], vm = 0.0, m = a;
        bool good = true;
        for (int it = 0; it < 200; ++it) {
            m = 0.5 * (a + b);
            if (m == a || m == b) break;
            if (!fn(m, vm)) { good = false; break; }
            if (vm == 0.0) break;
            if ((vm < 0) == (va < 0)) { a = m; va = vm; } else b = m;
        }
        if (good && fn(m, vm) && std::fabs(vm) < 1e-6) roots.push_back(m);   // ตัดเสา (pole) ที่เครื่องหมายสลับออก
    }

    if (tangent) {
        for (int i = 1; i < N; ++i) {
            if (!(ok[i - 1] && ok[i] && ok[i + 1]) || vs[i] == 0.0) continue;
            if (!(vs[i - 1] * vs[i] > 0 && vs[i] * vs[i + 1] > 0)) continue;
            if (!(std::fabs(vs[i]) <= std::fabs(vs[i - 1]) && std::fabs(vs[i]) <= std::fabs(vs[i + 1]))) continue;
            if (!(std::fabs(vs[i]) < std::fabs(vs[i - 1]) || std::fabs(vs[i]) < std::fabs(vs[i + 1]))) continue;   // ข้ามช่วงที่ค่าคงที่ (ไม่ใช่ขั้นต่ำเฉพาะที่)
            ScalarFn g = [&](double t) { double v; return fn(t, v) ? std::fabs(v) : INF; };
            double xm; double gv = goldenMin(g, xs[i - 1], xs[i + 1], xm);
            if (gv < 1e-9) roots.push_back(xm);
        }
    }

    for (auto& r : roots) r = snapNice(r);
    std::sort(roots.begin(), roots.end());
    std::vector<double> uniq;
    for (double r : roots) if (uniq.empty() || std::fabs(r - uniq.back()) > 1e-6) uniq.push_back(r);
    return uniq;
}

// ---------- การประกอบช่วงโดเมนจากจุดเปลี่ยน ----------
struct Brk { double pos; bool left, right, point; };   // ซ้าย/ขวาของจุดนี้ใช้ได้ไหม, ตัวจุดเองใช้ได้ไหม

std::vector<Interval> assemble(bool startsValid, std::vector<Brk> brks) {
    std::stable_sort(brks.begin(), brks.end(), [](const Brk& a, const Brk& b) { return a.pos < b.pos; });
    std::vector<Interval> out;
    bool in = startsValid;
    double start = -INF; bool startClosed = false;
    auto push = [&](double lo, double hi, bool lc, bool hc) {
        if (lo < hi || (lo == hi && lc && hc)) out.emplace_back(lo, hi, lc, hc);
    };
    for (const auto& b : brks) {
        if (in) {
            push(start, b.pos, startClosed, b.point);
            if (b.right) { start = b.pos; startClosed = b.point; in = true; } else in = false;
        } else if (b.right) { start = b.pos; startClosed = b.point; in = true; }
    }
    if (in) push(start, INF, startClosed, false);
    return out;
}


// ---------- จัดรูปสมการ ----------

// ลองประมาณ fn(x) ด้วยพหุนามดีกรี <= maxDeg (ใช้ node จำนวนเต็ม แล้วตรวจที่จุดอื่น); คืน false ถ้าไม่ใช่พหุนาม
bool fitPolynomial(const std::function<bool(double, double&)>& fn, std::vector<double>& coeffs, int maxDeg = 6) {
    const int n = maxDeg + 1;
    std::vector<std::vector<double>> M(n, std::vector<double>(n + 1));
    for (int i = 0; i < n; ++i) {
        double x = static_cast<double>(i) - maxDeg / 2.0;
        double v; if (!fn(x, v)) return false;
        double p = 1.0;
        for (int j = 0; j < n; ++j) { M[i][j] = p; p *= x; }
        M[i][n] = v;
    }
    for (int i = 0; i < n; ++i) {                      // Gauss-Jordan พร้อม partial pivoting
        int piv = i;
        for (int k = i + 1; k < n; ++k) if (std::fabs(M[k][i]) > std::fabs(M[piv][i])) piv = k;
        if (std::fabs(M[piv][i]) < 1e-12) return false;
        std::swap(M[i], M[piv]);
        for (int k = 0; k < n; ++k) {
            if (k == i) continue;
            double f = M[k][i] / M[i][i];
            for (int j = i; j <= n; ++j) M[k][j] -= f * M[i][j];
        }
    }
    coeffs.assign(n, 0.0);
    for (int i = 0; i < n; ++i) coeffs[i] = snapNice(M[i][n] / M[i][i], 1e-8);
    static const double checks[] = {0.37, -1.91, 2.43, 4.7, -5.3, 7.1};
    for (double x : checks) {
        double v; if (!fn(x, v)) return false;
        double p = 0.0;
        for (int j = n - 1; j >= 0; --j) p = p * x + coeffs[j];
        if (std::fabs(p - v) > 1e-7 * (1.0 + std::fabs(v))) return false;
    }
    while (coeffs.size() > 1 && coeffs.back() == 0.0) coeffs.pop_back();
    return true;
}

bool asFraction(double a, long long& p, long long& q);

// สัมประสิทธิ์สำหรับแสดงผล (ค่าบวก): จำนวนเต็ม / เศษส่วน (p/q) / ทศนิยม
std::string coefText(double a) {
    long long p, q;
    if (std::fabs(a - std::round(a)) < 1e-9) return std::to_string(std::llround(a));
    if (asFraction(a, p, q) && q <= 100) return "(" + std::to_string(p) + "/" + std::to_string(q) + ")";
    return fmtNum(a);
}

std::string polyString(const std::vector<double>& c) {     // รูปแบบคณิตศาสตร์: 2x^3 - x + 1
    std::string out;
    for (int k = static_cast<int>(c.size()) - 1; k >= 0; --k) {
        double v = c[k];
        if (v == 0.0) continue;
        double a = std::fabs(v);
        std::string term;
        if (k == 0) term = coefText(a);
        else {
            if (a != 1.0) term = coefText(a);
            term += "x";
            if (k > 1) term += "^" + std::to_string(k);
        }
        if (out.empty()) out = (v < 0 ? "-" : "") + term;
        else out += (v < 0 ? " - " : " + ") + term;
    }
    return out.empty() ? "0" : out;
}

// ---------- พหุนามสองตัวแปร x, y : กระจาย รวมพจน์ที่คล้ายกัน แล้วเรียงตามดีกรี ----------

bool fitBivariatePolynomial(const std::function<bool(double, double, double&)>& g,
                            std::vector<std::vector<double>>& C, int D = 6) {
    const int n = D + 1;
    auto node = [&](int a) { return static_cast<double>(a) - D / 2.0; };

    // Vinv = (Vandermonde)^-1 (node ชุดเดียวกันทั้ง x และ y)
    std::vector<std::vector<double>> M(n, std::vector<double>(2 * n, 0.0));
    for (int a = 0; a < n; ++a) {
        double p = 1.0;
        for (int i = 0; i < n; ++i) { M[a][i] = p; p *= node(a); }
        M[a][n + a] = 1.0;
    }
    for (int i = 0; i < n; ++i) {
        int piv = i;
        for (int k = i + 1; k < n; ++k) if (std::fabs(M[k][i]) > std::fabs(M[piv][i])) piv = k;
        if (std::fabs(M[piv][i]) < 1e-12) return false;
        std::swap(M[i], M[piv]);
        double d = M[i][i];
        for (int j = 0; j < 2 * n; ++j) M[i][j] /= d;
        for (int k = 0; k < n; ++k) {
            if (k == i) continue;
            double f = M[k][i];
            for (int j = 0; j < 2 * n; ++j) M[k][j] -= f * M[i][j];
        }
    }
    std::vector<std::vector<double>> Vinv(n, std::vector<double>(n));
    for (int i = 0; i < n; ++i) for (int j = 0; j < n; ++j) Vinv[i][j] = M[i][n + j];

    std::vector<std::vector<double>> V(n, std::vector<double>(n));
    for (int a = 0; a < n; ++a) for (int b = 0; b < n; ++b) if (!g(node(a), node(b), V[a][b])) return false;

    // C[i][j] = sum_a sum_b Vinv[i][a] * V[a][b] * Vinv[j][b]   (i = ยกกำลัง x, j = ยกกำลัง y)
    C.assign(n, std::vector<double>(n, 0.0));
    for (int i = 0; i < n; ++i)
        for (int b = 0; b < n; ++b) {
            double t = 0.0;
            for (int a = 0; a < n; ++a) t += Vinv[i][a] * V[a][b];
            for (int j = 0; j < n; ++j) C[i][j] += t * Vinv[j][b];
        }
    for (auto& row : C) for (auto& c : row) c = snapNice(c, 1e-8);

    static const double pts[][2] = {{0.37, -1.21}, {2.9, 1.7}, {-4.3, 3.1}, {5.1, -2.6},
                                    {-1.1, -5.2}, {0.6, 0.8}, {3.3, -4.4}, {-6.1, 2.2}};
    for (const auto& pt : pts) {
        double v; if (!g(pt[0], pt[1], v)) return false;
        double sum = 0.0;
        for (int i = 0; i < n; ++i) for (int j = 0; j < n; ++j) sum += C[i][j] * std::pow(pt[0], i) * std::pow(pt[1], j);
        if (std::fabs(sum - v) > 1e-7 * (1.0 + std::fabs(v))) return false;
    }
    return true;
}

// สัมประสิทธิ์ที่เป็นเศษส่วนสวย -> "p/q" (คืน false ถ้าไม่ใช่)
bool asFraction(double a, long long& p, long long& q) {
    for (q = 1; q <= 1000; ++q) {
        double t = a * static_cast<double>(q);
        if (std::fabs(t - std::round(t)) < 1e-9 * (1.0 + std::fabs(t))) { p = std::llround(t); return true; }
    }
    return false;
}

// เขียนสมการ "พหุนาม(x,y) = 0" ในรูปแบบมาตรฐาน เช่น 4x^2y^2 + 2x^3 - 3x + 2y = 0
std::string bivariateStandardForm(std::vector<std::vector<double>> C) {
    const int n = static_cast<int>(C.size());
    struct T { int i, j; double c; };
    std::vector<T> terms;
    for (int i = 0; i < n; ++i) for (int j = 0; j < n; ++j) if (C[i][j] != 0.0) terms.push_back({i, j, C[i][j]});
    if (terms.empty()) return "0 = 0";

    std::sort(terms.begin(), terms.end(), [](const T& a, const T& b) {
        if (a.i + a.j != b.i + b.j) return a.i + a.j > b.i + b.j;      // ดีกรีรวมมากก่อน
        return a.i > b.i;                                               // ดีกรีเท่ากัน: ยกกำลัง x มากก่อน
    });

    // ถ้าสัมประสิทธิ์เป็นจำนวนเต็มทั้งหมด หารด้วย ห.ร.ม.
    bool allInt = true;
    for (const auto& t : terms) if (std::fabs(t.c - std::round(t.c)) > 1e-9) allInt = false;
    if (allInt) {
        long long g = 0;
        for (const auto& t : terms) { long long v = std::llabs(std::llround(t.c)); long long a = g, b = v; while (b) { long long r = a % b; a = b; b = r; } g = a; }
        if (g > 1) for (auto& t : terms) t.c /= static_cast<double>(g);
    }
    if (terms.front().c < 0) for (auto& t : terms) t.c = -t.c;        // ให้พจน์นำเป็นบวก

    std::string out;
    for (size_t k = 0; k < terms.size(); ++k) {
        double a = std::fabs(terms[k].c);
        std::string mono;
        if (terms[k].i == 1) mono += "x"; else if (terms[k].i > 1) mono += "x^" + std::to_string(terms[k].i);
        if (terms[k].j == 1) mono += "y"; else if (terms[k].j > 1) mono += "y^" + std::to_string(terms[k].j);

        std::string coef;
        if (!(std::fabs(a - 1.0) < 1e-9 && !mono.empty())) coef = coefText(a);

        std::string term = coef + mono;
        if (k == 0) out = (terms[k].c < 0 ? "-" : "") + term;
        else out += (terms[k].c < 0 ? " - " : " + ") + term;
    }
    return out + " = 0";
}

bool parseWholeNumber(const std::string& s, double& v) {
    if (s.empty()) return false;
    char* end = nullptr;
    v = std::strtod(s.c_str(), &end);
    return end != nullptr && *end == '\0';
}


// ---------- พหุนามหลายตัวแปร (สำหรับ f(x+h) ฯลฯ) ----------

using MvTerm = std::pair<std::vector<int>, double>;      // (เลขชี้กำลังของแต่ละตัวแปร, สัมประสิทธิ์)

int varPriority(char c) {                                // x, y, z มาก่อน แล้วตัวอื่นเรียงตามตัวอักษร
    if (c == 'x') return 0;
    if (c == 'y') return 1;
    if (c == 'z') return 2;
    return 3 + (std::isupper(static_cast<unsigned char>(c)) ? 26 + (c - 'A') : (c - 'a'));
}

std::string multivarToString(const std::vector<char>& vars, std::vector<MvTerm> terms) {
    const size_t k = vars.size();
    std::vector<size_t> order(k);
    for (size_t i = 0; i < k; ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return varPriority(vars[a]) < varPriority(vars[b]); });

    auto totalDeg = [](const MvTerm& t) { int d = 0; for (int e : t.first) d += e; return d; };
    std::sort(terms.begin(), terms.end(), [&](const MvTerm& a, const MvTerm& b) {
        int da = totalDeg(a), db = totalDeg(b);
        if (da != db) return da > db;
        for (size_t m : order) if (a.first[m] != b.first[m]) return a.first[m] > b.first[m];
        return false;
    });

    std::string out;
    for (const auto& t : terms) {
        if (t.second == 0.0) continue;
        double a = std::fabs(t.second);
        std::string mono;
        for (size_t m : order) {
            if (t.first[m] == 0) continue;
            mono += vars[m];
            if (t.first[m] > 1) mono += "^" + std::to_string(t.first[m]);
        }
        std::string coef;
        if (!(std::fabs(a - 1.0) < 1e-9 && !mono.empty())) coef = coefText(a);
        std::string term = coef + mono;
        if (out.empty()) out = (t.second < 0 ? "-" : "") + term;
        else out += (t.second < 0 ? " - " : " + ") + term;
    }
    return out.empty() ? "0" : out;
}

// หาวงเล็บปิดที่คู่กับวงเล็บเปิดที่ตำแหน่ง open (คืน npos ถ้าไม่พบ)
size_t matchingParen(const std::string& s, size_t open) {
    int depth = 0;
    for (size_t i = open; i < s.size(); ++i) {
        if (s[i] == '(') ++depth;
        else if (s[i] == ')') { if (--depth == 0) return i; }
    }
    return std::string::npos;
}


// ลดวงเล็บที่ซ้อนเกินจำเป็น เช่น ((t+1)) -> (t+1), (x) -> x, (sin((t+1))) -> sin(t+1) (ใช้กับข้อความที่แสดงเท่านั้น)
std::string tidyParens(std::string s) {
    auto isSimple = [](const std::string& t) {
        if (t.empty()) return false;
        for (char c : t) if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.')) return false;
        return true;
    };
    bool changed = true;
    while (changed) {
        changed = false;
        if (s.size() >= 2 && s.front() == '(' && matchingParen(s, 0) == s.size() - 1) {
            s = s.substr(1, s.size() - 2); changed = true; continue;
        }
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] != '(') continue;
            size_t j = matchingParen(s, i);
            if (j == std::string::npos) continue;
            std::string inner = s.substr(i + 1, j - i - 1);
            bool doubleWrapped = inner.size() >= 2 && inner.front() == '(' && matchingParen(inner, 0) == inner.size() - 1;
            if (i > 0 && std::isalnum(static_cast<unsigned char>(s[i - 1]))) {               // วงเล็บของฟังก์ชัน เช่น sin(...)
                if (doubleWrapped) {                                                          // sin((t+1)) -> sin(t+1)
                    s = s.substr(0, i + 1) + inner.substr(1, inner.size() - 2) + s.substr(j);
                    changed = true;
                    break;
                }
                continue;
            }
            if (isSimple(inner) || doubleWrapped) {
                s = s.substr(0, i) + inner + s.substr(j + 1);
                changed = true;
                break;
            }
        }
    }
    return s;
}

// แทนการเรียก f(arg) ทุกจุดใน s (ทำซ้ำแบบ recursive เพื่อรองรับ f(g(x)) และฟังก์ชันที่อ้างฟังก์ชันอื่น)
std::string expandCallsImpl(const std::vector<FunctionDef>& defs, const std::string& s, int depth) {
    if (depth > 20) throw std::runtime_error("Function definitions are nested too deeply (recursive definition?).");
    std::string out;
    size_t i = 0, n = s.size();
    while (i < n) {
        if (!std::isalpha(static_cast<unsigned char>(s[i]))) { out += s[i++]; continue; }
        size_t j = i;
        while (j < n && std::isalpha(static_cast<unsigned char>(s[j]))) ++j;
        std::string word = s.substr(i, j - i);
        size_t k = j;
        while (k < n && std::isspace(static_cast<unsigned char>(s[k]))) ++k;

        const FunctionDef* def = nullptr;
        if (word.size() == 1 && k < n && s[k] == '(')
            for (const auto& d : defs) if (d.name == word[0]) { def = &d; break; }

        if (!def) { out += word; i = j; continue; }

        size_t close = matchingParen(s, k);
        if (close == std::string::npos) throw std::invalid_argument(std::string("Missing ')' after ") + word + "(");
        std::string arg = expandCallsImpl(defs, s.substr(k + 1, close - k - 1), depth + 1);
        if (trim(arg).empty()) throw std::invalid_argument(std::string("Empty argument in ") + word + "()");
        std::string body = insertImplicitMultiplication(expandCallsImpl(defs, def->body, depth + 1));
        out += "(" + replaceVar(body, def->var, "(" + insertImplicitMultiplication(arg) + ")") + ")";
        i = close + 1;
    }
    return out;
}

} // namespace

// =====================================================================================
//  Interval
// =====================================================================================

bool Interval::isEmpty() const {
    if (lo > hi) return true;
    if (lo == hi) return !(loClosed && hiClosed);
    return false;
}

bool Interval::contains(double x) const {
    if (isEmpty()) return false;
    bool a = (x > lo) || (x == lo && loClosed);
    bool b = (x < hi) || (x == hi && hiClosed);
    return a && b;
}

std::string Interval::toString() const {
    if (isEmpty()) return "{}";
    if (lo == hi) return "{" + fmtNum(lo) + "}";
    std::string s;
    s += (loClosed && !std::isinf(lo)) ? "[" : "(";
    s += fmtNum(lo) + ", " + fmtNum(hi);
    s += (hiClosed && !std::isinf(hi)) ? "]" : ")";
    return s;
}

// =====================================================================================
//  ส่วนที่ 1: ความสัมพันธ์แบบเซตจำกัด
// =====================================================================================

FunctionSolver::FunctionSolver(Calculator& calc) : calc_(calc) {}

void FunctionSolver::setScanWindow(double w) {
    if (!(w > 0)) throw std::invalid_argument("Scan window must be positive.");
    window_ = w;
}
void FunctionSolver::setStepsPerUnit(int s) {
    if (s < 1) throw std::invalid_argument("stepsPerUnit must be >= 1.");
    stepsPerUnit_ = s;
}
void FunctionSolver::setRelationWindow(double w) {
    if (!(w > 0)) throw std::invalid_argument("Relation window must be positive.");
    relWindow_ = w;
}

static FunctionSolver::NumSet sortedUnique(FunctionSolver::NumSet v) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
}

FunctionSolver::Relation FunctionSolver::cartesianProduct(const NumSet& A, const NumSet& B) {
    Relation r;
    for (double a : sortedUnique(A)) for (double b : sortedUnique(B)) r.emplace_back(a, b);
    return r;
}

std::vector<FunctionSolver::Tuple> FunctionSolver::cartesianProductN(const std::vector<NumSet>& sets, size_t maxResults) {
    std::vector<Tuple> result;
    if (sets.empty()) return result;

    std::vector<NumSet> S;
    double total = 1.0;
    for (const auto& st : sets) {
        S.push_back(sortedUnique(st));
        total *= static_cast<double>(S.back().size());
    }
    if (total == 0.0) return result;
    if (total > static_cast<double>(maxResults))
        throw std::length_error("Cartesian product has too many elements (" + fmtNum(total) + ").");

    // นับแบบ odometer: ตัวท้ายสุดเปลี่ยนเร็วสุด -> ได้ลำดับพจนานุกรม
    std::vector<size_t> idx(S.size(), 0);
    result.reserve(static_cast<size_t>(total));
    while (true) {
        Tuple t(S.size());
        for (size_t k = 0; k < S.size(); ++k) t[k] = S[k][idx[k]];
        result.push_back(std::move(t));
        size_t k = S.size();
        while (k > 0) {
            --k;
            if (++idx[k] < S[k].size()) break;
            idx[k] = 0;
            if (k == 0) return result;
        }
    }
}

std::string FunctionSolver::toString(const std::vector<Tuple>& tuples, size_t maxShown) {
    std::string s = "{";
    size_t n = (maxShown == 0 || maxShown > tuples.size()) ? tuples.size() : maxShown;
    for (size_t i = 0; i < n; ++i) {
        if (i) s += ", ";
        s += "(";
        for (size_t k = 0; k < tuples[i].size(); ++k) { if (k) s += ", "; s += fmtNum(tuples[i][k]); }
        s += ")";
    }
    if (n < tuples.size()) s += ", ... (" + std::to_string(tuples.size() - n) + " more)";
    return s + "}";
}

FunctionSolver::Relation FunctionSolver::relationFromCondition(const NumSet& A, const NumSet& B,
                                                               const std::string& condition) {
    // แยกตัวเปรียบเทียบ
    std::string op;
    size_t opPos = std::string::npos;
    for (size_t i = 0; i < condition.size() && opPos == std::string::npos; ++i) {
        char c = condition[i];
        if (c != '<' && c != '>' && c != '=' && c != '!') continue;
        if (i + 1 < condition.size() && condition[i + 1] == '=') { op = std::string(1, c) + "="; }
        else if (c == '!') continue;
        else op = std::string(1, c);
        opPos = i;
    }
    if (opPos == std::string::npos) throw std::invalid_argument("Condition must contain =, !=, <, <=, > or >=.");
    const size_t opLen = op.size();
    if (op == "==") op = "=";

    std::string lhs = insertImplicitMultiplication(condition.substr(0, opPos));
    std::string rhs = insertImplicitMultiplication(condition.substr(opPos + opLen));
    if (trim(lhs).empty() || trim(rhs).empty()) throw std::invalid_argument("Condition has an empty side.");

    Relation r;
    for (double a : sortedUnique(A)) {
        for (double b : sortedUnique(B)) {
            calc_.setVariable('x', a);
            calc_.setVariable('y', b);
            double L, R;
            try { L = calc_.evaluate(lhs); R = calc_.evaluate(rhs); } catch (...) { continue; }
            if (!std::isfinite(L) || !std::isfinite(R)) continue;
            bool eq = std::fabs(L - R) < 1e-9 * (1.0 + std::fabs(L) + std::fabs(R));
            bool keep = false;
            if (op == "=") keep = eq;
            else if (op == "!=") keep = !eq;
            else if (op == "<") keep = (L < R) && !eq;
            else if (op == "<=") keep = (L < R) || eq;
            else if (op == ">") keep = (L > R) && !eq;
            else if (op == ">=") keep = (L > R) || eq;
            if (keep) r.emplace_back(a, b);
        }
    }
    return r;
}

FunctionSolver::NumSet FunctionSolver::domainOf(const Relation& r) {
    NumSet s; for (const auto& p : r) s.push_back(p.first);
    return sortedUnique(s);
}

FunctionSolver::NumSet FunctionSolver::rangeOf(const Relation& r) {
    NumSet s; for (const auto& p : r) s.push_back(p.second);
    return sortedUnique(s);
}

FunctionSolver::Relation FunctionSolver::inverseRelation(const Relation& r) {
    Relation inv;
    for (const auto& p : r) inv.emplace_back(p.second, p.first);
    std::sort(inv.begin(), inv.end());
    inv.erase(std::unique(inv.begin(), inv.end()), inv.end());
    return inv;
}

std::string FunctionSolver::inverseCondition(const std::string& condition) {
    return swapXYImpl(condition);
}

bool FunctionSolver::isFunction(const Relation& r) {
    Relation s = r;
    std::sort(s.begin(), s.end());
    s.erase(std::unique(s.begin(), s.end()), s.end());
    for (size_t i = 1; i < s.size(); ++i)
        if (s[i].first == s[i - 1].first && s[i].second != s[i - 1].second) return false;
    return true;
}

bool FunctionSolver::isFunctionFromAToB(const Relation& r, const NumSet& A, const NumSet& B) {
    if (!isFunction(r)) return false;
    if (domainOf(r) != sortedUnique(A)) return false;
    NumSet sb = sortedUnique(B);
    for (double y : rangeOf(r)) if (!std::binary_search(sb.begin(), sb.end(), y)) return false;
    return true;
}

bool FunctionSolver::isOntoB(const Relation& r, const NumSet& B) {
    return isFunction(r) && rangeOf(r) == sortedUnique(B);
}

bool FunctionSolver::isOneToOne(const Relation& r) {
    return isFunction(r) && inverseRelation(r).size() == r.size() && isFunction(inverseRelation(r));
}

std::string FunctionSolver::toString(const Relation& r) {
    std::string s = "{";
    for (size_t i = 0; i < r.size(); ++i) {
        if (i) s += ", ";
        s += "(" + fmtNum(r[i].first) + ", " + fmtNum(r[i].second) + ")";
    }
    return s + "}";
}

std::string FunctionSolver::toString(const NumSet& v) {
    std::string s = "{";
    for (size_t i = 0; i < v.size(); ++i) { if (i) s += ", "; s += fmtNum(v[i]); }
    return s + "}";
}

std::string FunctionSolver::toString(const std::vector<Interval>& v) {
    if (v.empty()) return "{}";
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) { if (i) s += " U "; s += v[i].toString(); }
    return s;
}

// =====================================================================================
//  ตัวช่วยภายใน (ฟังก์ชัน f(x))
// =====================================================================================

std::string FunctionSolver::prepare(const std::string& expr, char var) {
    std::string e = expr;
    // รองรับคำว่า "ans" (ค่าล่าสุดที่ตั้งผ่าน calc.solver().setAns)
    e = replaceWord(e, "ans", exprNum(calc_.solver().getAns()));
    e = insertImplicitMultiplication(e);
    if (trim(e).empty()) throw std::invalid_argument("Empty expression.");
    return AlphaSolverEq::normalizeVariable(e, var);     // ตัวแปรอื่น (เช่น t) -> x
}

bool FunctionSolver::evalAt(const std::string& f, double x, double& out) {
    calc_.setVariable('x', x);
    try {
        double v = calc_.evaluate(f);
        if (!std::isfinite(v)) return false;
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}

void FunctionSolver::sampleBounds(const Interval& P, double& L, double& H) const {
    bool fl = std::isfinite(P.lo), fh = std::isfinite(P.hi);
    if (fl && fh) { L = P.lo; H = P.hi; }
    else if (fl)  { L = P.lo; H = P.lo + 2 * window_; }
    else if (fh)  { L = P.hi - 2 * window_; H = P.hi; }
    else          { L = -window_; H = window_; }
}

double FunctionSolver::evaluateAt(const std::string& expr, double x, char var) {
    std::string f = prepare(expr, var);
    calc_.setVariable('x', x);
    return calc_.evaluate(f);
}

// =====================================================================================
//  โดเมน
// =====================================================================================

namespace {
struct EndBehavior { int type = 0; double L = 0.0; bool flat = false; };   // 0=ไม่แน่ชัด 1=ลู่เข้า 2=พุ่งขึ้น +inf 3=พุ่งลง -inf
}

// พฤติกรรมของ f เมื่อ x เข้าใกล้ปลายเปิด/ปลายอนันต์ของช่วง P
static EndBehavior classifyEnd(const std::function<bool(double, double&)>& ev, const Interval& P, bool leftEnd) {
    std::vector<double> vals;
    double e = leftEnd ? P.lo : P.hi;
    if (std::isfinite(e)) {
        for (int k = 2; k <= 11; ++k) {
            double d = std::pow(10.0, -k);
            double x = leftEnd ? e + d : e - d;
            if (x <= P.lo || x >= P.hi) continue;
            double v; if (ev(x, v)) vals.push_back(v);
        }
    } else {
        for (int k = 2; k <= 24; ++k) {          // |x| = 10^(k/2) : 10, 31.6, 100, ..., 1e12 (บางค่าอาจล้นจึงถูกข้าม)
            double x = (leftEnd ? -1.0 : 1.0) * std::pow(10.0, k / 2.0);
            if (x <= P.lo || x >= P.hi) continue;
            double v; if (ev(x, v)) vals.push_back(v);
        }
    }
    EndBehavior r;
    if (vals.size() < 3) return r;
    double v1 = vals[vals.size() - 3], v2 = vals[vals.size() - 2], v3 = vals.back();
    if (std::fabs(v3 - v2) < 1e-4 * (1.0 + std::fabs(v3))) {
        r.type = 1;
        r.L = (std::fabs(v3) < 1e-5) ? 0.0 : snapNice(v3, 1e-5);
        r.flat = (v1 == v2 && v2 == v3);
    } else if (v1 < v2 && v2 < v3 && v3 > 10.0) r.type = 2;
    else if (v1 > v2 && v2 > v3 && v3 < -10.0) r.type = 3;
    return r;
}

std::vector<Interval> FunctionSolver::domainPrepared(const std::string& f) {
    const int S = stepsPerUnit_;
    const long long K = std::llround(window_ * S);
    const size_t n = static_cast<size_t>(2 * K + 1);

    std::vector<double> xs(n), ys(n, 0.0);
    std::vector<char> ok(n);
    for (size_t i = 0; i < n; ++i) {
        xs[i] = static_cast<double>(static_cast<long long>(i) - K) / S;   // k/S แม่นตรงที่จำนวนเต็ม
        ok[i] = evalAt(f, xs[i], ys[i]);
    }

    std::vector<Brk> brks;

    // (1) รอยต่อ ใช้ได้ <-> ใช้ไม่ได้
    for (size_t i = 0; i + 1 < n; ++i) {
        if (ok[i] == ok[i + 1]) continue;
        double a = ok[i] ? xs[i] : xs[i + 1];       // ฝั่งที่ใช้ได้
        double b = ok[i] ? xs[i + 1] : xs[i];       // ฝั่งที่ใช้ไม่ได้
        double tmp;
        for (int it = 0; it < 200; ++it) {
            double m = 0.5 * (a + b);
            if (m == a || m == b) break;
            if (evalAt(f, m, tmp)) a = m; else b = m;
        }
        double raw = 0.5 * (a + b);
        bool snapped = false;
        double p = snapNice(raw, 1e-9, &snapped);
        bool point;
        if (snapped) point = evalAt(f, p, tmp);
        else {                                       // ขอบไม่ใช่เลขสวย (เช่น sqrt(2)): ถ้า f พุ่งเข้าหาขอบ -> เปิด, ไม่งั้น -> ปิด
            bool validOnRight = !ok[i];
            Interval P = validOnRight ? Interval(raw, INF) : Interval(-INF, raw);
            auto ev = [&](double x, double& o) { return evalAt(f, x, o); };
            EndBehavior eb = classifyEnd(ev, P, validOnRight);
            point = !(eb.type == 2 || eb.type == 3);
        }
        Brk b2{p, false, false, point};
        if (ok[i]) { b2.left = true;  b2.right = false; }
        else       { b2.left = false; b2.right = true;  }
        brks.push_back(b2);
    }

    // (2) เสา (pole) ที่อยู่ระหว่างจุดกริด 2 จุดที่ใช้ได้ทั้งคู่: ค่า |f| มีจุดสูงสุดเฉพาะที่แล้วพุ่งสูงมาก
    for (size_t i = 1; i + 1 < n; ++i) {
        if (!(ok[i - 1] && ok[i] && ok[i + 1])) continue;
        double a0 = std::fabs(ys[i - 1]), a1 = std::fabs(ys[i]), a2 = std::fabs(ys[i + 1]);
        if (!(a1 >= a0 && a1 >= a2 && (a1 > a0 || a1 > a2) && a1 > 10.0)) continue;
        ScalarFn g = [&](double t) { double v; return evalAt(f, t, v) ? -std::fabs(v) : -INF; };
        double xm; double gv = goldenMin(g, xs[i - 1], xs[i + 1], xm);
        if (-gv > 1e8) {
            double c = snapNice(xm);
            bool dup = false;
            for (const auto& b : brks) if (std::fabs(b.pos - c) < 1e-6) dup = true;
            if (!dup) brks.push_back(Brk{c, true, true, false});
        }
    }

    return assemble(ok[0] != 0, brks);
}

std::vector<Interval> FunctionSolver::domain(const std::string& expr, char var) {
    return domainPrepared(prepare(expr, var));
}

std::vector<Interval> FunctionSolver::domainOn(const std::string& expr, const Interval& A, char var) {
    std::vector<Interval> out;
    for (const auto& d : domain(expr, var)) {
        Interval i = intersect(d, A);
        if (!i.isEmpty()) out.push_back(i);
    }
    return out;
}

// =====================================================================================
//  เรนจ์
// =====================================================================================

bool FunctionSolver::pieceRange(const std::string& f, const Interval& P, Interval& out) {
    if (P.lo == P.hi) {
        double v; if (!evalAt(f, P.lo, v)) return false;
        out = Interval(v, v, true, true); return true;
    }
    double L, H; sampleBounds(P, L, H);
    const int N = 4000;
    std::vector<double> xs, vs;
    for (int j = 0; j <= N; ++j) {
        double x = L + (H - L) * static_cast<double>(j) / N;
        if (x == P.lo && !P.loClosed) continue;
        if (x == P.hi && !P.hiClosed) continue;
        double v; if (evalAt(f, x, v)) { xs.push_back(x); vs.push_back(v); }
    }
    if (xs.empty()) return false;

    const bool leftOpen = !P.loClosed, rightOpen = !P.hiClosed;
    size_t a = 0, b = xs.size();                   // ช่วงตัวอย่างที่ถือเป็น "ภายใน"
    if (leftOpen && b - a > 2) ++a;                // ตัวอย่างติดปลายเปิดให้ใช้พฤติกรรมปลายแทน
    if (rightOpen && b - a > 2) --b;

    size_t imin = a, imax = a;
    for (size_t i = a; i < b; ++i) {
        if (vs[i] < vs[imin]) imin = i;
        if (vs[i] > vs[imax]) imax = i;
    }

    auto refine = [&](size_t idx, bool findMin) -> double {
        double x = xs[idx];
        bool endpoint = (x == P.lo) || (x == P.hi);
        if (endpoint) return vs[idx];
        double lo = idx > 0 ? xs[idx - 1] : x;
        double hi = idx + 1 < xs.size() ? xs[idx + 1] : x;
        if (lo == hi) return vs[idx];
        ScalarFn g = [&](double t) { double v; if (!evalAt(f, t, v)) return INF; return findMin ? v : -v; };
        double xm; double gv = goldenMin(g, lo, hi, xm);
        double val = findMin ? gv : -gv;
        return findMin ? std::min(val, vs[idx]) : std::max(val, vs[idx]);
    };

    const bool constantFn = (vs[imin] == vs[imax]);   // ค่าคงตัวบนช่วงนี้ (ปลายที่ลู่เข้าค่าเดียวกันจึงเป็นค่าที่ถึงจริง)
    double lowV  = snapNice(refine(imin, true));
    double highV = snapNice(refine(imax, false));
    bool lowClosed = true, highClosed = true;

    auto ev = [&](double x, double& o) { return evalAt(f, x, o); };
    auto handleEnd = [&](bool leftEnd) {
        EndBehavior e = classifyEnd(ev, P, leftEnd);
        if (e.type == 3) { lowV = -INF; lowClosed = false; }
        else if (e.type == 2) { highV = INF; highClosed = false; }
        else if (e.type == 1) {
            if (e.L < lowV) { lowV = e.L; lowClosed = false; }
            else if (e.L == lowV && !constantFn) lowClosed = false;
            if (e.L > highV) { highV = e.L; highClosed = false; }
            else if (e.L == highV && !constantFn) highClosed = false;
        }
    };
    if (leftOpen) handleEnd(true);
    if (rightOpen) handleEnd(false);

    out = Interval(lowV, highV, lowClosed && std::isfinite(lowV), highClosed && std::isfinite(highV));
    return true;
}

std::vector<Interval> FunctionSolver::rangeOnPieces(const std::string& f, const std::vector<Interval>& pieces) {
    std::vector<Interval> all;
    for (const auto& P : pieces) {
        Interval r;
        if (pieceRange(f, P, r)) all.push_back(r);
    }
    return mergeIntervals(all);
}

std::vector<Interval> FunctionSolver::range(const std::string& expr, char var) {
    std::string f = prepare(expr, var);
    return rangeOnPieces(f, domainPrepared(f));
}

std::vector<Interval> FunctionSolver::rangeOn(const std::string& expr, const Interval& A, char var) {
    std::string f = prepare(expr, var);
    std::vector<Interval> pieces;
    for (const auto& d : domainPrepared(f)) {
        Interval i = intersect(d, A);
        if (!i.isEmpty()) pieces.push_back(i);
    }
    return rangeOnPieces(f, pieces);
}

// =====================================================================================
//  ฟังก์ชันเพิ่ม / ลด
// =====================================================================================

std::vector<MonotonicPiece> FunctionSolver::monotonicOnPieces(const std::string& f, const std::vector<Interval>& pieces) {
    // ใช้อนุพันธ์เชิงสัญลักษณ์ของ Calculator ถ้าทำได้ ไม่งั้นใช้อนุพันธ์เชิงตัวเลข
    std::string ds;
    bool symbolic = false;
    try { ds = calc_.symbolicDerivative(f, 'x'); symbolic = true; } catch (...) { symbolic = false; }

    EvalFn d = [&](double x, double& o) -> bool {
        if (symbolic) return evalAt(ds, x, o);
        calc_.setVariable('x', x);
        try { o = calc_.derivative(f, x); return std::isfinite(o); } catch (...) { return false; }
    };

    std::vector<MonotonicPiece> result;
    for (const auto& P : pieces) {
        double L, H; sampleBounds(P, L, H);
        int N = static_cast<int>(std::min(20000.0, std::max(200.0, (H - L) * stepsPerUnit_)));
        std::vector<double> cuts;
        for (double r : findRoots(d, L, H, N, false))
            if (r > P.lo + 1e-9 && r < P.hi - 1e-9) cuts.push_back(r);

        std::vector<double> bounds;
        bounds.push_back(P.lo);
        for (double c : cuts) bounds.push_back(c);
        bounds.push_back(P.hi);

        std::vector<MonotonicPiece> local;
        for (size_t j = 0; j + 1 < bounds.size(); ++j) {
            double lo = bounds[j], hi = bounds[j + 1];
            double mid;
            if (std::isfinite(lo) && std::isfinite(hi)) mid = 0.5 * (lo + hi);
            else if (std::isfinite(hi)) mid = hi - 1.0;
            else if (std::isfinite(lo)) mid = lo + 1.0;
            else mid = 0.0;
            double dv;
            if (!d(mid, dv)) continue;
            int kind = dv > 1e-9 ? 1 : (dv < -1e-9 ? -1 : 0);
            bool lc = (j == 0) ? P.loClosed : true;
            bool hc = (j + 2 == bounds.size()) ? P.hiClosed : true;
            if (!local.empty() && local.back().kind == kind && local.back().range.hi == lo) {
                local.back().range.hi = hi; local.back().range.hiClosed = hc;
            } else {
                MonotonicPiece mp; mp.range = Interval(lo, hi, lc, hc); mp.kind = kind;
                local.push_back(mp);
            }
        }
        result.insert(result.end(), local.begin(), local.end());
    }
    return result;
}

std::vector<MonotonicPiece> FunctionSolver::monotonicity(const std::string& expr, char var) {
    std::string f = prepare(expr, var);
    return monotonicOnPieces(f, domainPrepared(f));
}

// =====================================================================================
//  ประเภทของฟังก์ชัน (1-1, จาก A ไป B, ไปทั่วถึง)
// =====================================================================================

bool FunctionSolver::oneToOneOnPieces(const std::string& f, const std::vector<Interval>& pieces) {
    // 1-1 <=> ในแต่ละช่วงของโดเมนเป็นฟังก์ชันเพิ่มล้วน/ลดล้วน และเรนจ์ของแต่ละช่วงไม่ทับกัน
    std::vector<Interval> ranges;
    for (const auto& P : pieces) {
        auto m = monotonicOnPieces(f, {P});
        if (m.size() != 1 || m[0].kind == 0) return false;
        Interval r;
        if (!pieceRange(f, P, r)) return false;
        for (const auto& prev : ranges) if (!intersect(prev, r).isEmpty()) return false;
        ranges.push_back(r);
    }
    return !pieces.empty();
}

bool FunctionSolver::isOneToOne(const std::string& expr, char var) {
    std::string f = prepare(expr, var);
    return oneToOneOnPieces(f, domainPrepared(f));
}

bool FunctionSolver::isOneToOneOn(const std::string& expr, const Interval& A, char var) {
    std::string f = prepare(expr, var);
    std::vector<Interval> pieces;
    for (const auto& d : domainPrepared(f)) {
        Interval i = intersect(d, A);
        if (!i.isEmpty()) pieces.push_back(i);
    }
    return oneToOneOnPieces(f, pieces);
}

bool FunctionSolver::isFunctionFromTo(const std::string& expr, const Interval& A, const Interval& B, char var) {
    std::string f = prepare(expr, var);
    std::vector<Interval> dom = domainPrepared(f);
    if (!coveredBy(A, dom)) return false;                // โดเมนของฟังก์ชันต้องครอบคลุม A ทั้งหมด
    std::vector<Interval> pieces;
    for (const auto& d : dom) { Interval i = intersect(d, A); if (!i.isEmpty()) pieces.push_back(i); }
    for (const auto& r : rangeOnPieces(f, pieces)) if (!subsetOf(r, B)) return false;
    return true;
}

bool FunctionSolver::isOnto(const std::string& expr, const Interval& A, const Interval& B, char var) {
    if (!isFunctionFromTo(expr, A, B, var)) return false;
    std::vector<Interval> r = rangeOn(expr, A, var);
    return r.size() == 1 && sameInterval(r[0], B);
}

// =====================================================================================
//  แก้ f(x) = y0  และฟังก์ชันผกผัน
// =====================================================================================

std::vector<double> FunctionSolver::solveFunctionEquals(const std::string& expr, double y0, char var) {
    std::string f = prepare(expr, var);
    EvalFn h = [&](double x, double& o) -> bool {
        double v; if (!evalAt(f, x, v)) return false; o = v - y0; return true;
    };
    std::vector<double> sols;
    for (const auto& P : domainPrepared(f)) {
        double L, H; sampleBounds(P, L, H);
        int N = static_cast<int>(std::min(40000.0, std::max(500.0, (H - L) * stepsPerUnit_)));
        for (double r : findRoots(h, L, H, N, true)) if (P.contains(r) || (r > P.lo && r < P.hi)) sols.push_back(r);
    }
    // ขัดเกลาด้วย AlphaSolverEq::solveGeneralEquation (Newton-Raphson) ถ้าทำให้ดีขึ้นจริง
    for (auto& r : sols) {
        try {
            std::string eq = "(" + f + ")-" + exprNum(y0);
            double refined = calc_.solver().solveGeneralEquation(calc_, eq, r, 100, 1e-13, 'x');
            double a, b;
            if (std::fabs(refined - r) < 1e-6 && evalAt(f, refined, a) && evalAt(f, r, b) &&
                std::fabs(a - y0) <= std::fabs(b - y0)) r = snapNice(refined);
        } catch (...) {}
    }
    std::sort(sols.begin(), sols.end());
    sols.erase(std::unique(sols.begin(), sols.end(),
                           [](double a, double b) { return std::fabs(a - b) < 1e-6; }), sols.end());
    return sols;
}

double FunctionSolver::inverseAt(const std::string& expr, double y0, char var) {
    if (!isOneToOne(expr, var)) throw std::runtime_error("Function is not one-to-one, so it has no inverse function.");
    std::vector<double> s = solveFunctionEquals(expr, y0, var);
    if (s.empty()) throw std::domain_error("y is not in the range of the function (within the scan window).");
    return s.front();
}

std::string FunctionSolver::inverseExpression(const std::string& expr, char var) {
    std::string f = prepare(expr, var);
    double f0, f1, f2, fm;
    if (!(evalAt(f, 0.0, f0) && evalAt(f, 1.0, f1) && evalAt(f, 2.0, f2) && evalAt(f, -3.0, fm))) return "";
    double a = f1 - f0, b = f0;
    // ตรวจความเป็นเชิงเส้นด้วยจุดเพิ่ม (กัน x^2, sin(x) ฯลฯ)
    if (std::fabs(f2 - (2 * a + b)) > 1e-9 * (1 + std::fabs(f2)) || std::fabs(fm - (-3 * a + b)) > 1e-9 * (1 + std::fabs(fm))) return "";
    double mid;
    if (!evalAt(f, 0.37, mid) || std::fabs(mid - (0.37 * a + b)) > 1e-9 * (1 + std::fabs(mid))) return "";
    if (std::fabs(a) < 1e-12) throw std::runtime_error("Constant function is not one-to-one, so it has no inverse function.");

    a = snapNice(a); b = snapNice(b);
    std::string num = "x";
    if (b > 0) num += "-" + fmtNum(b); else if (b < 0) num += "+" + fmtNum(-b);
    if (a == 1.0) return num;
    std::string den = (a < 0) ? "(" + fmtNum(a) + ")" : fmtNum(a);
    return (b == 0 ? std::string("x") : "(" + num + ")") + "/" + den;
}

// =====================================================================================
//  พีชคณิตของฟังก์ชัน และฟังก์ชันประกอบ
// =====================================================================================

std::string FunctionSolver::add(const std::string& f, const std::string& g) {
    return "(" + prepare(f, '\0') + ")+(" + prepare(g, '\0') + ")";
}
std::string FunctionSolver::subtract(const std::string& f, const std::string& g) {
    return "(" + prepare(f, '\0') + ")-(" + prepare(g, '\0') + ")";
}
std::string FunctionSolver::multiply(const std::string& f, const std::string& g) {
    return "(" + prepare(f, '\0') + ")*(" + prepare(g, '\0') + ")";
}
std::string FunctionSolver::divide(const std::string& f, const std::string& g) {
    return "(" + prepare(f, '\0') + ")/(" + prepare(g, '\0') + ")";
}
std::string FunctionSolver::compose(const std::string& g, const std::string& f) {
    // g∘f (x) = g(f(x)) : แทน x ใน g ด้วย (f)  -> โดเมนที่ได้จาก domain() คือ {x ∈ D_f | f(x) ∈ D_g} อัตโนมัติ
    return replaceVar(prepare(g, '\0'), 'x', "(" + prepare(f, '\0') + ")");
}

// =====================================================================================
//  ส่วนที่ 2: ความสัมพันธ์แบบสมการ
// =====================================================================================

// หาค่า y ทุกค่าที่ทำให้ lhs(x0,y) = rhs(x0,y)
std::vector<double> FunctionSolver::rootsInY(const std::string& lhs, const std::string& rhs, double x0,
                                               double W, int N, bool tangent) {
    EvalFn g = [&](double y, double& o) -> bool {
        calc_.setVariable('x', x0);
        calc_.setVariable('y', y);
        try {
            double l = calc_.evaluate(lhs), r = calc_.evaluate(rhs);
            if (!std::isfinite(l) || !std::isfinite(r)) return false;
            o = l - r; return true;
        } catch (...) { return false; }
    };
    if (W < 0) W = relWindow_;
    return findRoots(g, -W, W, N, tangent);
}

std::vector<Interval> FunctionSolver::relationDomain(const std::string& lhs, const std::string& rhs) {
    const double step = 0.1;
    const long long K = std::llround(relWindow_ / step);
    const size_t n = static_cast<size_t>(2 * K + 1);
    // มีคำตอบ y ไหม: ค้นในช่วงหลักก่อน ถ้าไม่พบให้สแกนหยาบในช่วงกว้างกว่า (กัน y หลุดกรอบ เช่น x*y=1 ที่ x เล็กมาก)
    auto hasSol = [&](double x) {
        if (!rootsInY(lhs, rhs, x).empty()) return true;
        return !rootsInY(lhs, rhs, x, 1e4, 4000, false).empty();
    };

    std::vector<double> xs(n);
    std::vector<char> ok(n);
    for (size_t i = 0; i < n; ++i) {
        xs[i] = static_cast<double>(static_cast<long long>(i) - K) * step;
        xs[i] = std::round(xs[i] * 1e9) / 1e9;
        ok[i] = hasSol(xs[i]);
    }
    std::vector<Brk> brks;
    for (size_t i = 0; i + 1 < n; ++i) {
        if (ok[i] == ok[i + 1]) continue;
        double a = ok[i] ? xs[i] : xs[i + 1];
        double b = ok[i] ? xs[i + 1] : xs[i];
        for (int it = 0; it < 60; ++it) {
            double m = 0.5 * (a + b);
            if (m == a || m == b) break;
            if (hasSol(m)) a = m; else b = m;
        }
        bool snapped = false;
        double p = snapNice(0.5 * (a + b), 1e-7, &snapped);
        bool point = snapped ? hasSol(p) : true;
        Brk br{p, ok[i] != 0, ok[i + 1] != 0, point};
        brks.push_back(br);
    }
    return assemble(ok[0] != 0, brks);
}

// พยายามแก้ lhs = rhs หา y: ตรวจว่า g(x,y) = lhs - rhs = A(x)*y^2 + B(x)*y + C(x)
//   - A, B เป็นค่าคงที่  -> สูตรกำลังสอง / เชิงเส้น
//   - A = 0 และ B เป็นพหุนามของ x -> y = -C(x)/B(x) (เช่น x*y = 1)
// ถ้า C(x) (และ B(x)) เป็นพหุนามของ x จะกระจายให้อ่านง่าย (เช่น x^2 + y^2 = 25 -> y = ±sqrt(-x^2 + 25))
std::string FunctionSolver::solveForYForm(const std::string& lhs, const std::string& rhs) {
    auto g = [&](double x, double y, double& o) -> bool {
        calc_.setVariable('x', x);
        calc_.setVariable('y', y);
        try {
            double l = calc_.evaluate(lhs), r = calc_.evaluate(rhs);
            if (!std::isfinite(l) || !std::isfinite(r)) return false;
            o = l - r; return true;
        } catch (...) { return false; }
    };

    static const double xs[] = {0.5, -1.3, 2.7, 4.1, -2.2};
    std::vector<double> As, Bs;
    for (double x : xs) {
        double g0, g1, gm;
        if (!(g(x, 0, g0) && g(x, 1, g1) && g(x, -1, gm))) return "";
        double A = (g1 + gm - 2 * g0) / 2, B = (g1 - gm) / 2;
        for (double y : {2.3, -3.7}) {                      // ตรวจว่าเป็นกำลังสองใน y จริง
            double gy; if (!g(x, y, gy)) return "";
            if (std::fabs(gy - (A * y * y + B * y + g0)) > 1e-8 * (1 + std::fabs(gy))) return "";
        }
        As.push_back(A); Bs.push_back(B);
    }
    auto allNear = [](const std::vector<double>& v, double t) {
        for (double e : v) if (std::fabs(e - t) > 1e-9 * (1 + std::fabs(t))) return false;
        return true;
    };
    const bool aZero = allNear(As, 0.0), aConst = allNear(As, As[0]), bConst = allNear(Bs, Bs[0]), bZero = allNear(Bs, 0.0);
    if (aZero && bZero) return "";                          // ไม่ขึ้นกับ y (เช่น x = 3)

    const double A = snapNice(As[0], 1e-8), B = snapNice(Bs[0], 1e-8);
    auto num = [](double v) { return v < 0 ? "(" + fmtNum(v) + ")" : fmtNum(v); };
    auto wrap = [](const std::string& t) {
        return (t.find_first_of(" +-") != std::string::npos) ? "(" + t + ")" : t;
    };
    auto nice = [](double c) {
        long long p, q;
        return std::fabs(c - std::round(c * 1e4) / 1e4) < 1e-9 || (asFraction(std::fabs(c), p, q) && q <= 100);
    };

    // C(x) = g(x,0)
    std::vector<double> cc;
    const bool cPoly = fitPolynomial([&](double x, double& o) { return g(x, 0, o); }, cc);
    const std::string cText = "(" + replaceVar(lhs, 'y', "0") + ")-(" + replaceVar(rhs, 'y', "0") + ")";
    // พหุนาม k*v (ถ้าสัมประสิทธิ์ทุกตัวสวย) -> สตริง
    auto scaledPoly = [&](const std::vector<double>& v, double k, std::string& out) {
        std::vector<double> r = v;
        for (auto& c : r) { c = snapNice(c * k, 1e-8); if (!nice(c)) return false; }
        out = polyString(r); return true;
    };

    if (aZero && bConst) {                                  // B*y + C = 0
        std::string poly;
        if (cPoly && scaledPoly(cc, -1.0 / B, poly)) return "y = " + poly;
        return "y = -(" + (cPoly ? polyString(cc) : cText) + ")/" + num(B);
    }
    if (aZero) {                                            // B(x)*y + C(x) = 0 : y = -C/B
        std::vector<double> bb;
        if (!cPoly || !fitPolynomial([&](double x, double& o) {
                double p, m; if (!(g(x, 1, p) && g(x, -1, m))) return false; o = (p - m) / 2; return true; }, bb)) return "";
        std::string numer, denom = polyString(bb);
        if (!scaledPoly(cc, -1.0, numer) || denom == "0") return "";
        return "y = " + wrap(numer) + "/" + wrap(denom);
    }
    if (!(aConst && bConst)) return "";

    if (std::fabs(B) < 1e-12) {                             // A*y^2 + C = 0
        std::string poly;
        if (cPoly && scaledPoly(cc, -1.0 / A, poly)) return poly == "0" ? "y = 0" : "y = \u00B1sqrt(" + poly + ")";
        std::string c = cPoly ? polyString(cc) : cText;
        if (A == 1.0) return "y = \u00B1sqrt(-(" + c + "))";
        if (A == -1.0) return "y = \u00B1sqrt(" + c + ")";
        return "y = \u00B1sqrt(-(" + c + ")/" + num(A) + ")";
    }
    // A*y^2 + B*y + C = 0 : y = (-B ± sqrt(B^2 - 4AC)) / (2A)
    std::string disc;
    if (cPoly) {
        std::vector<double> d = cc;
        for (auto& c : d) c *= -4.0 * A;
        d[0] += B * B;
        for (auto& c : d) c = snapNice(c, 1e-8);
        disc = polyString(d);
    } else {
        disc = fmtNum(B * B) + " - 4*" + num(A) + "*(" + cText + ")";
    }
    return "y = (" + fmtNum(-B) + " \u00B1 sqrt(" + disc + "))/" + num(2 * A);
}

RelationReport FunctionSolver::analyzeRelation(const std::string& equation) {
    size_t eq = std::string::npos;
    for (size_t i = 0; i < equation.size(); ++i) {
        if (equation[i] != '=') continue;
        bool part = (i > 0 && (equation[i - 1] == '<' || equation[i - 1] == '>' || equation[i - 1] == '!' || equation[i - 1] == '='))
                 || (i + 1 < equation.size() && equation[i + 1] == '=');
        if (!part) { eq = i; break; }
    }
    if (eq == std::string::npos) throw std::invalid_argument("analyzeRelation expects an equation such as \"y = 2x+1\" or \"x^2+y^2=25\".");

    std::string lhsRaw = equation.substr(0, eq), rhsRaw = equation.substr(eq + 1);
    std::string lhs = insertImplicitMultiplication(replaceWord(lhsRaw, "ans", exprNum(calc_.solver().getAns())));
    std::string rhs = insertImplicitMultiplication(replaceWord(rhsRaw, "ans", exprNum(calc_.solver().getAns())));
    if (trim(lhs).empty() || trim(rhs).empty()) throw std::invalid_argument("Equation has an empty side.");

    RelationReport rep;

    // รูปชัด y = f(x) (และไม่มี y อยู่อีกฝั่ง): ใช้การหาโดเมน/เรนจ์ของ f ตรง ๆ
    auto hasVarY = [](const std::string& s) {
        for (size_t i = 0; i < s.size();) {
            if (std::isalpha(static_cast<unsigned char>(s[i]))) {
                size_t j = i; while (j < s.size() && std::isalpha(static_cast<unsigned char>(s[j]))) ++j;
                if (j - i == 1 && s[i] == 'y') return true;
                i = j;
            } else ++i;
        }
        return false;
    };
    std::string tl = trim(lhs), tr = trim(rhs);
    std::string fexpr;
    if (tl == "y" && !hasVarY(tr)) fexpr = tr;
    else if (tr == "y" && !hasVarY(tl)) fexpr = tl;
    if (!fexpr.empty()) {
        rep.standardForm = "y = " + fexpr;
        rep.solvedForm = rep.standardForm;
        rep.explicitForm = true;
        rep.explicitExpr = fexpr;
        rep.isFunction = true;
        rep.domain = domain(fexpr, 'x');
        rep.range = range(fexpr, 'x');
        return rep;
    }

    // รูปไม่ชัด: จัดรูปเป็น "lhs - rhs = 0" และลองแก้หา y
    {
        double rv;
        if (tr == "0") rep.standardForm = tl + " = 0";
        else if (tl == "0") rep.standardForm = tr + " = 0";
        else if (parseWholeNumber(tr, rv)) rep.standardForm = tl + (rv > 0 ? " - " + fmtNum(rv) : " + " + fmtNum(-rv)) + " = 0";
        else rep.standardForm = tl + " - (" + tr + ") = 0";
        // จัดรูปให้สมบูรณ์: กระจาย + รวมพจน์คล้าย + เรียงตามดีกรี (ถ้าเป็นพหุนามใน x, y)
        try {
            std::vector<std::vector<double>> C;
            auto g2 = [&](double x, double y, double& o) -> bool {
                calc_.setVariable('x', x); calc_.setVariable('y', y);
                try {
                    double l = calc_.evaluate(lhs), r = calc_.evaluate(rhs);
                    if (!std::isfinite(l) || !std::isfinite(r)) return false;
                    o = l - r; return true;
                } catch (...) { return false; }
            };
            if (fitBivariatePolynomial(g2, C)) rep.standardForm = bivariateStandardForm(C);
        } catch (...) {}
        try { rep.solvedForm = solveForYForm(lhs, rhs); } catch (...) { rep.solvedForm.clear(); }
    }

    // ทดสอบเส้นตรงแนวตั้ง (หา y ทั้งหมดที่ได้จากแต่ละ x)
    rep.isFunction = true;

    // สมการที่ไม่มี y เลย (เช่น x = 3) คือเส้นตรงแนวตั้ง: x ที่เป็นคำตอบจับคู่กับ "ทุก y" -> ไม่เป็นฟังก์ชัน
    {
        auto gxy = [&](double x, double y, double& o) -> bool {
            calc_.setVariable('x', x); calc_.setVariable('y', y);
            try {
                double l = calc_.evaluate(lhs), r = calc_.evaluate(rhs);
                if (!std::isfinite(l) || !std::isfinite(r)) return false;
                o = l - r; return true;
            } catch (...) { return false; }
        };
        bool independent = true;
        for (double x : {0.37, -1.9, 2.6}) {
            double a, b, c;
            if (!(gxy(x, 0, a) && gxy(x, 1, b) && gxy(x, -2.5, c)) ||
                std::fabs(a - b) > 1e-12 * (1 + std::fabs(a)) || std::fabs(a - c) > 1e-12 * (1 + std::fabs(a))) { independent = false; break; }
        }
        if (independent) {
            EvalFn h = [&](double x, double& o) { return gxy(x, 0.0, o); };
            std::vector<double> xr = findRoots(h, -relWindow_, relWindow_, 2000, true);
            if (!xr.empty()) {
                rep.isFunction = false;
                rep.counterExampleX = xr.front();
                rep.counterExampleY.clear();               // ว่าง = ทุกค่าของ y
            }
        }
    }

    const double step = 0.1;
    for (long long k = 0; rep.isFunction && k < static_cast<long long>(2 * relWindow_ / step); ++k) {
        double x0 = -relWindow_ + (k + 0.5) * step;
        std::vector<double> ys = rootsInY(lhs, rhs, x0);
        if (ys.size() >= 2) {
            rep.isFunction = false;
            rep.counterExampleX = std::round(x0 * 1e9) / 1e9;
            rep.counterExampleY = ys;
            break;
        }
    }
    rep.domain = relationDomain(lhs, rhs);
    rep.range  = relationDomain(swapXYImpl(lhs), swapXYImpl(rhs));   // เรนจ์ = โดเมนของตัวผกผัน (สลับ x,y)
    return rep;
}

// =====================================================================================
//  ส่วนที่ 4: สัญกรณ์ฟังก์ชัน f(อะไรก็ได้)
// =====================================================================================

FunctionDef FunctionSolver::parseDefinition(const std::string& definition) {
    std::string d = trim(definition);
    FunctionDef def;
    size_t eq = d.find('=');
    if (eq == std::string::npos) {                         // ใส่แค่เนื้อฟังก์ชัน
        def.name = 'f';
        def.body = d;
        def.var = AlphaSolverEq::detectVariable(insertImplicitMultiplication(d));
    } else {
        std::string lhs, rest = trim(d.substr(eq + 1));
        for (char c : d.substr(0, eq)) if (!std::isspace(static_cast<unsigned char>(c))) lhs += c;
        if (lhs.size() != 4 || !std::isalpha(static_cast<unsigned char>(lhs[0])) || lhs[1] != '(' ||
            !std::isalpha(static_cast<unsigned char>(lhs[2])) || lhs[3] != ')')
            throw std::invalid_argument("Definition must look like f(x) = ...  (one letter name, one letter variable).");
        def.name = lhs[0];
        def.var = lhs[2];
        def.body = rest;
        if (def.name == def.var) throw std::invalid_argument("Function name and variable must be different letters.");
    }
    if (trim(def.body).empty()) throw std::invalid_argument("Function body is empty.");
    return def;
}

std::string FunctionSolver::expandCalls(const std::vector<FunctionDef>& defs, const std::string& query) {
    std::string q = replaceWord(query, "ans", exprNum(calc_.solver().getAns()));
    return insertImplicitMultiplication(expandCallsImpl(defs, q, 0));
}

// กระจายพหุนามหลายตัวแปร: ประมาณค่าด้วย Vandermonde ตามแต่ละแกน แล้วตรวจที่จุดสุ่ม
bool FunctionSolver::fitMultivariate(const std::string& expr, const std::vector<char>& vars,
                                     std::vector<std::pair<std::vector<int>, double>>& terms) {
    const int k = static_cast<int>(vars.size());
    if (k < 1 || k > 4) return false;
    static const int Dk[] = {0, 6, 5, 4, 3};
    const int D = Dk[k], n = D + 1;
    auto node = [&](int a) { return static_cast<double>(a) - D / 2.0 + 0.37; };   // เลี่ยงจุดจำนวนเต็ม (เสา/รูโหว่ที่ 0, 1, ...)

    std::vector<std::vector<double>> M(n, std::vector<double>(2 * n, 0.0));
    for (int a = 0; a < n; ++a) {
        double p = 1.0;
        for (int i = 0; i < n; ++i) { M[a][i] = p; p *= node(a); }
        M[a][n + a] = 1.0;
    }
    for (int i = 0; i < n; ++i) {
        int piv = i;
        for (int r = i + 1; r < n; ++r) if (std::fabs(M[r][i]) > std::fabs(M[piv][i])) piv = r;
        if (std::fabs(M[piv][i]) < 1e-12) return false;
        std::swap(M[i], M[piv]);
        double dv = M[i][i];
        for (int j = 0; j < 2 * n; ++j) M[i][j] /= dv;
        for (int r = 0; r < n; ++r) {
            if (r == i) continue;
            double f = M[r][i];
            for (int j = 0; j < 2 * n; ++j) M[r][j] -= f * M[i][j];
        }
    }
    std::vector<std::vector<double>> Vinv(n, std::vector<double>(n));
    for (int i = 0; i < n; ++i) for (int j = 0; j < n; ++j) Vinv[i][j] = M[i][n + j];

    size_t total = 1;
    for (int m = 0; m < k; ++m) total *= static_cast<size_t>(n);
    auto decode = [&](size_t flat, std::vector<int>& idx) {
        for (int m = k - 1; m >= 0; --m) { idx[m] = static_cast<int>(flat % n); flat /= n; }
    };
    auto evalPoint = [&](const std::vector<double>& pt, double& out) -> bool {
        for (int m = 0; m < k; ++m) calc_.setVariable(vars[m], pt[m]);
        try { out = calc_.evaluate(expr); return std::isfinite(out); } catch (...) { return false; }
    };

    std::vector<double> V(total);
    std::vector<int> idx(k);
    std::vector<double> pt(k);
    for (size_t flat = 0; flat < total; ++flat) {
        decode(flat, idx);
        for (int m = 0; m < k; ++m) pt[m] = node(idx[m]);
        if (!evalPoint(pt, V[flat])) return false;
    }
    size_t stride = total;
    for (int m = 0; m < k; ++m) {                          // คูณ Vinv ตามแกนที่ m
        stride /= static_cast<size_t>(n);
        std::vector<double> W(total, 0.0);
        for (size_t flat = 0; flat < total; ++flat) {
            decode(flat, idx);
            size_t base = flat - static_cast<size_t>(idx[m]) * stride;
            double sum = 0.0;
            for (int a = 0; a < n; ++a) sum += Vinv[idx[m]][a] * V[base + static_cast<size_t>(a) * stride];
            W[flat] = sum;
        }
        V.swap(W);
    }

    terms.clear();
    for (size_t flat = 0; flat < total; ++flat) {
        double c = snapNice(V[flat], 1e-8);
        if (c == 0.0) continue;
        decode(flat, idx);
        terms.emplace_back(idx, c);
    }

    unsigned seed = 12345u;
    auto rnd = [&]() { seed = seed * 1664525u + 1013904223u; return -3.0 + 6.0 * ((seed >> 8) / 16777216.0); };
    for (int t = 0; t < 10; ++t) {
        for (int m = 0; m < k; ++m) pt[m] = rnd();
        double v; if (!evalPoint(pt, v)) return false;
        double sum = 0.0;
        for (const auto& term : terms) {
            double prod = term.second;
            for (int m = 0; m < k; ++m) prod *= std::pow(pt[m], term.first[m]);
            sum += prod;
        }
        if (std::fabs(sum - v) > 1e-7 * (1.0 + std::fabs(v))) return false;
    }
    return true;
}

CallResult FunctionSolver::evaluateCall(const std::vector<std::string>& definitions, const std::string& query,
                                        const std::map<char, double>& values) {
    std::vector<FunctionDef> defs;
    for (const auto& d : definitions) defs.push_back(parseDefinition(d));

    CallResult res;
    const std::string rawSub = expandCalls(defs, query);
    res.substituted = tidyParens(rawSub);
    std::string expr = values.empty() ? rawSub : AlphaSolverEq::substituteVariables(res.substituted, values);
    res.variables = AlphaSolverEq::findVariables(expr);

    if (res.variables.empty()) {                           // ไม่เหลือตัวแปร -> ค่าตัวเลข
        calc_.setVariable('x', 0.0);
        res.value = calc_.evaluate(expr);
        res.hasValue = true;
        res.simplified = fmtNum(res.value);
        res.isPolynomial = true;
        return res;
    }

    std::vector<MvTerm> terms;
    if (fitMultivariate(expr, res.variables, terms)) {
        res.isPolynomial = true;
        res.simplified = multivarToString(res.variables, terms);
    } else {
        res.simplified = res.substituted;
    }
    return res;
}

CallEquationResult FunctionSolver::solveCallEquation(const std::vector<std::string>& definitions, const std::string& equation,
                                                     char unknown, const std::map<char, double>& values) {
    size_t eq = std::string::npos;
    for (size_t i = 0; i < equation.size(); ++i) {
        if (equation[i] != '=') continue;
        bool part = (i > 0 && (equation[i - 1] == '<' || equation[i - 1] == '>' || equation[i - 1] == '!' || equation[i - 1] == '='))
                 || (i + 1 < equation.size() && equation[i + 1] == '=');
        if (!part) { eq = i; break; }
    }
    if (eq == std::string::npos) throw std::invalid_argument("Expected an equation such as f(t+1) = 10.");

    std::vector<FunctionDef> defs;
    for (const auto& d : definitions) defs.push_back(parseDefinition(d));
    std::string lhs = expandCalls(defs, equation.substr(0, eq));
    std::string rhs = expandCalls(defs, equation.substr(eq + 1));
    if (trim(lhs).empty() || trim(rhs).empty()) throw std::invalid_argument("Equation has an empty side.");

    std::string expr = "(" + lhs + ")-(" + rhs + ")";
    if (!values.empty()) expr = AlphaSolverEq::substituteVariables(expr, values);

    CallEquationResult res;
    if (unknown == '\0') unknown = AlphaSolverEq::detectVariable(expr);       // x ก่อน, ตัวเดียวก็เลือกตัวนั้น, หลายตัวจะ throw
    res.unknown = unknown;

    std::vector<char> others;
    for (char c : AlphaSolverEq::findVariables(expr)) if (c != unknown) others.push_back(c);
    if (!others.empty()) {
        std::string list; for (char c : others) { if (!list.empty()) list += ", "; list += c; }
        throw std::runtime_error("Unknown value for parameter(s): " + list + ". Provide their values.");
    }

    std::vector<MvTerm> terms;
    if (fitMultivariate(expr, {unknown}, terms)) {                       // ---- พหุนาม ----
        res.isPolynomial = true;
        int deg = 0;
        for (const auto& t : terms) deg = std::max(deg, t.first[0]);
        std::vector<double> desc(deg + 1, 0.0);                          // สัมประสิทธิ์เรียงจากกำลังสูงไปต่ำ
        for (const auto& t : terms) desc[deg - t.first[0]] = t.second;
        res.equation = multivarToString({unknown}, terms) + " = 0";

        if (terms.empty()) { res.identity = true; return res; }
        if (deg == 0) return res;                                        // ค่าคงที่ไม่เป็นศูนย์ -> ไม่มีคำตอบ
        if (deg == 1) res.roots.push_back(std::complex<double>(-desc[1] / desc[0], 0.0));
        else res.roots = calc_.solver().solvePolynomial(desc);
        if (deg >= 2) { try { res.factored = calc_.solver().factorizePolynomial(desc, unknown); } catch (...) {} }
        for (auto& r : res.roots) {
            if (std::fabs(r.real()) < 1e-10) r = std::complex<double>(0.0, r.imag());
            if (std::fabs(r.imag()) < 1e-9) { r = std::complex<double>(snapNice(r.real(), 1e-9), 0.0); res.realRoots.push_back(r.real()); }
        }
        std::sort(res.realRoots.begin(), res.realRoots.end());
        return res;
    }

    // ---- ไม่ใช่พหุนาม: สแกนหารากจริง แล้วขัดเกลาด้วย solveGeneralEquation ----
    res.equation = tidyParens(lhs) + " - (" + tidyParens(rhs) + ") = 0";
    std::string f = AlphaSolverEq::normalizeVariable(expr, unknown);      // ตัวแปรที่เลือก -> x
    EvalFn h = [&](double x, double& o) { return evalAt(f, x, o); };
    const int N = static_cast<int>(std::min(40000.0, 2.0 * window_ * stepsPerUnit_));
    for (double r : findRoots(h, -window_, window_, N, true)) {
        try {
            double refined = calc_.solver().solveGeneralEquation(calc_, f, r, 100, 1e-13, 'x');
            double a, b;
            if (std::fabs(refined - r) < 1e-6 && evalAt(f, refined, a) && evalAt(f, r, b) && std::fabs(a) <= std::fabs(b)) r = snapNice(refined);
        } catch (...) {}
        res.realRoots.push_back(r);
        res.roots.push_back(std::complex<double>(r, 0.0));
    }
    return res;
}

// =====================================================================================
//  สรุปผลฟังก์ชัน
// =====================================================================================

std::string FunctionSolver::describeFunction(const std::string& expr, char var) {
    std::string f = prepare(expr, var);
    std::vector<Interval> dom = domainPrepared(f);
    std::ostringstream o;
    o << "f(x) = " << f << "\n";
    o << "Domain : " << toString(dom) << "\n";
    o << "Range  : " << toString(rangeOnPieces(f, dom)) << "\n";

    o << "Monotonic: ";
    auto mono = monotonicOnPieces(f, dom);
    if (mono.empty()) o << "(none)";
    for (size_t i = 0; i < mono.size(); ++i) {
        if (i) o << ", ";
        o << (mono[i].kind > 0 ? "increasing on " : mono[i].kind < 0 ? "decreasing on " : "constant on ")
          << mono[i].range.toString();
    }
    o << "\n";

    bool oneToOne = oneToOneOnPieces(f, dom);
    o << "One-to-one: " << (oneToOne ? "yes" : "no") << "\n";
    if (oneToOne) {
        std::string inv;
        try { inv = inverseExpression(f, 'x'); } catch (...) {}
        if (!inv.empty()) o << "Inverse: f^-1(x) = " << inv << "\n";
        else o << "Inverse: exists (use inverseAt(expr, y) for numeric values)\n";
    } else {
        o << "Inverse: not a function (restrict the domain to make f one-to-one)\n";
    }
    return o.str();
}