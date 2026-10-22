#include "../lib/Alpha_solver_eq.hpp"
#include "../lib/Calculator_calculus.hpp"
#include "../lib/matrix.hpp"
#include <cmath>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <vector>
#include <complex>
#include <set>
#include <cctype>

// ---------- Variable handling helpers (รองรับตัวแปร a-z, A-Z) ----------
namespace {

bool isAsciiLetter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
bool isAsciiDigit(char c) { return c >= '0' && c <= '9'; }

// ชื่อฟังก์ชัน/ค่าคงที่ที่ไม่ถือว่าเป็นตัวแปร
bool isReservedWord(const std::string& w) {
    static const std::set<std::string> names = {
        "sin", "cos", "tan", "sec", "csc", "cot",
        "asin", "acos", "atan", "sinh", "cosh", "tanh",
        "exp", "log", "ln", "sqrt", "cbrt", "abs",
        "pi", "PI", "Pi", "ans" // "ans" กันไว้เผื่อหลุดมาไม่ได้ถูกแทนที่ก่อน (ปกติ substituteAns() จะแทนให้หมดก่อนถึงจุดนี้)
    };
    return names.count(w) > 0;
}

// === [เพิ่มใหม่] รองรับ "ans" ในสมการ: แทนที่คำว่า "ans" ที่เป็นคำเดี่ยวๆ (ไม่ใช่ส่วนหนึ่งของคำอื่น เช่น
// "scans") ด้วยค่าตัวเลขของคำตอบล่าสุด เพื่อให้พิมพ์สมการอย่าง "x + 2 = ans" อ้างอิงผลลัพธ์ก่อนหน้าได้
// ทำงานก่อนขั้นตอนอื่นใดเสมอ (ก่อน normalizeExpression/toZeroForm/findVariables) เพื่อไม่ให้ตัวอักษร
// a, n, s ใน "ans" ถูกเข้าใจผิดว่าเป็นตัวแปรแยกกัน (implicit multiplication a*n*s)
std::string substituteAns(const std::string& expr, double ansValue) {
    std::ostringstream numOss;
    numOss << std::setprecision(15) << ansValue;
    const std::string numStr = "(" + numOss.str() + ")"; // ครอบวงเล็บกันปัญหาลำดับการคำนวณ/เครื่องหมายลบ

    std::string result;
    result.reserve(expr.size());
    size_t i = 0;
    const size_t n = expr.size();
    while (i < n) {
        if (isAsciiLetter(expr[i])) {
            size_t j = i;
            while (j < n && isAsciiLetter(expr[j])) j++;
            std::string word = expr.substr(i, j - i);
            result += (word == "ans") ? numStr : word;
            i = j;
        } else {
            result += expr[i];
            ++i;
        }
    }
    return result;
}

// หาตำแหน่งของ "ตัวแปร" ทั้งหมดใน expression
//  - ข้ามตัวเลข (รวมรูปแบบ 1e-5, 2.5E3)
//  - คำที่เป็นชื่อฟังก์ชัน (sin, cos, ...) หรือคำหลายตัวอักษรที่ตามด้วย '(' ไม่ถือเป็นตัวแปร
//  - ตัวอักษรที่ติดกัน เช่น "xy" หรือ "ab" ถือเป็นตัวแปรแยกตัว (คูณโดยปริยาย)
std::vector<std::pair<size_t, char>> findVariableOccurrences(const std::string& s) {
    std::vector<std::pair<size_t, char>> result;
    const size_t n = s.size();
    size_t i = 0;
    while (i < n) {
        char c = s[i];

        if (isAsciiDigit(c) || (c == '.' && i + 1 < n && isAsciiDigit(s[i + 1]))) {
            while (i < n && (isAsciiDigit(s[i]) || s[i] == '.')) i++;
            if (i < n && (s[i] == 'e' || s[i] == 'E')) {   // scientific notation
                size_t j = i + 1;
                if (j < n && (s[j] == '+' || s[j] == '-')) j++;
                if (j < n && isAsciiDigit(s[j])) {
                    while (j < n && isAsciiDigit(s[j])) j++;
                    i = j;
                }
            }
            continue;
        }

        if (isAsciiLetter(c)) {
            size_t j = i;
            while (j < n && isAsciiLetter(s[j])) j++;
            std::string word = s.substr(i, j - i);

            size_t k = j;
            while (k < n && std::isspace(static_cast<unsigned char>(s[k]))) k++;
            bool looksLikeFunction = (word.size() > 1 && k < n && s[k] == '(');

            if (!looksLikeFunction && !isReservedWord(word)) {
                for (size_t p = i; p < j; ++p) result.push_back({p, s[p]});
            }
            i = j;
            continue;
        }
        i++;
    }
    return result;
}

// แปลง expression ที่ใช้ตัวแปร var ให้เป็นตัวแปร 'x' (ตัวที่ Calculator calculus รองรับ)
// var == '\0' หมายถึงตรวจหาตัวแปรอัตโนมัติ
std::string normalizeExpression(const std::string& expr, char var) {
    if (var == '\0') var = AlphaSolverEq::detectVariable(expr);
    if (!isAsciiLetter(var)) {
        throw std::invalid_argument("Variable name must be a letter (a-z or A-Z).");
    }
    if (var == 'x') return expr;

    auto occ = findVariableOccurrences(expr);
    for (const auto& o : occ) {
        if (o.second == 'x') {
            throw std::runtime_error(
                std::string("Cannot solve for '") + var +
                "' because the expression also contains a separate variable 'x'.");
        }
    }

    std::string out = expr;
    for (const auto& o : occ) {
        if (o.second == var) out[o.first] = 'x';
    }
    return out;
}

// ย้ายข้างสมการ "lhs = rhs" ให้เป็น "(lhs) - (rhs)" (ถ้าไม่มี '=' ให้คืนค่าเดิม ถือว่า expr = 0 อยู่แล้ว)
std::string toZeroForm(const std::string& eqStr) {
    size_t eqPos = eqStr.find('=');
    if (eqPos == std::string::npos) return eqStr;
    std::string lhs = eqStr.substr(0, eqPos);
    std::string rhs = eqStr.substr(eqPos + 1);
    return "(" + lhs + ") - (" + rhs + ")";
}

} // namespace

AlphaSolverEq::AlphaSolverEq() {}

void AlphaSolverEq::setAns(double value) { lastAnswer = value; }
double AlphaSolverEq::getAns() const { return lastAnswer; }

std::string AlphaSolverEq::normalizeVariable(const std::string& expr, char var) {
    return normalizeExpression(expr, var);
}

char AlphaSolverEq::detectVariable(const std::string& expr) {
    std::set<char> found;
    for (const auto& o : findVariableOccurrences(expr)) {
        if (o.second != 'e') found.insert(o.second);   // 'e' ถือเป็นค่าคงที่ของออยเลอร์
    }
    if (found.empty() || found.count('x')) return 'x';
    if (found.size() == 1) return *found.begin();

    std::string list;
    for (char c : found) { if (!list.empty()) list += ", "; list += c; }
    throw std::runtime_error("Ambiguous variable (found: " + list + "). Please specify which variable to solve for.");
}

std::vector<char> AlphaSolverEq::findVariables(const std::string& expr) {
    std::set<char> found;
    for (const auto& o : findVariableOccurrences(expr)) {
        if (o.second != 'e') found.insert(o.second);
    }
    return std::vector<char>(found.begin(), found.end());
}

std::string AlphaSolverEq::substituteVariables(const std::string& expr, const std::map<char, double>& values) {
    auto prevChar = [&](size_t pos) -> char {
        while (pos > 0) { --pos; if (!std::isspace(static_cast<unsigned char>(expr[pos]))) return expr[pos]; }
        return '\0';
    };
    auto nextChar = [&](size_t pos) -> char {
        while (++pos < expr.size()) { if (!std::isspace(static_cast<unsigned char>(expr[pos]))) return expr[pos]; }
        return '\0';
    };

    std::string out;
    size_t last = 0;
    for (const auto& o : findVariableOccurrences(expr)) {
        auto it = values.find(o.second);
        if (it == values.end()) continue;

        out += expr.substr(last, o.first - last);

        std::ostringstream num;
        num << std::fixed << std::setprecision(15) << it->second;

        char pc = prevChar(o.first), nc = nextChar(o.first);
        if (isAsciiLetter(pc) || isAsciiDigit(pc) || pc == ')' || pc == '.') out += '*';   // implicit multiplication
        out += "(" + num.str() + ")";
        if (isAsciiLetter(nc) || isAsciiDigit(nc) || nc == '(' || nc == '.') out += '*';

        last = o.first + 1;
    }
    out += expr.substr(last);
    return out;
}

// ---------- Polynomial Solvers ----------

std::vector<std::complex<double>> AlphaSolverEq::solveQuadratic(double a, double b, double c) {
    std::vector<std::complex<double>> roots;
    if (a == 0.0) {
        if (b != 0.0) roots.push_back(-c / b);
        return roots;
    }
    double disc = b * b - 4 * a * c;
    std::complex<double> sqrtDisc = std::sqrt(std::complex<double>(disc, 0.0));
    roots.push_back((-b + sqrtDisc) / (2.0 * a));
    roots.push_back((-b - sqrtDisc) / (2.0 * a));
    return roots;
}

std::vector<std::complex<double>> AlphaSolverEq::solveCubic(double a, double b, double c, double d) {
    if (a == 0.0) return solveQuadratic(b, c, d);

    double p = (3.0 * a * c - b * b) / (3.0 * a * a);
    double q = (2.0 * b * b * b - 9.0 * a * b * c + 27.0 * a * a * d) / (27.0 * a * a * a);

    std::complex<double> Q(p / 3.0, 0.0);
    std::complex<double> R(q / 2.0, 0.0);

    std::complex<double> disc = R * R + Q * Q * Q;
    std::complex<double> sqrtDisc = std::sqrt(disc);

    std::complex<double> u = std::pow(-R + sqrtDisc, 1.0 / 3.0);
    std::complex<double> v = std::pow(-R - sqrtDisc, 1.0 / 3.0);

    std::complex<double> w(-0.5, std::sqrt(3.0) / 2.0);
    std::complex<double> w2(-0.5, -std::sqrt(3.0) / 2.0);

    double shift = b / (3.0 * a);
    std::vector<std::complex<double>> roots;
    roots.push_back(u + v - shift);
    roots.push_back(u * w + v * w2 - shift);
    roots.push_back(u * w2 + v * w - shift);

    return roots;
}

std::vector<std::complex<double>> AlphaSolverEq::solveQuartic(double a, double b, double c, double d, double e) {
    if (a == 0.0) return solveCubic(b, c, d, e);

    std::vector<double> coeffs = {a, b, c, d, e};
    return solveHighDegreePoly(coeffs);
}

// Francis QR Algorithm บน Companion Matrix เพื่อความแม่นยำสูงสุด
std::vector<std::complex<double>> AlphaSolverEq::solveHighDegreePoly(const std::vector<double>& coeffs) {
    size_t start = 0;
    while (start < coeffs.size() && std::abs(coeffs[start]) < 1e-12) start++;

    int deg = static_cast<int>(coeffs.size()) - 1 - static_cast<int>(start);
    if (deg <= 0) return {};

    double lead = coeffs[start];
    std::vector<double> a(deg);
    for (int i = 0; i < deg; ++i) {
        a[i] = coeffs[start + 1 + i] / lead;
    }

    std::vector<std::vector<double>> C(deg, std::vector<double>(deg, 0.0));
    for (int i = 0; i < deg; ++i) {
        C[0][i] = -a[i];
        if (i < deg - 1) {
            C[i + 1][i] = 1.0;
        }
    }

    int n = deg;
    int maxIter = 1000;
    std::vector<std::complex<double>> roots;

    while (n > 2) {
        int iter = 0;
        while (std::abs(C[n - 1][n - 2]) > 1e-11 && iter < maxIter) {
            double tr = C[n - 2][n - 2] + C[n - 1][n - 1];
            double det = C[n - 2][n - 2] * C[n - 1][n - 1] - C[n - 2][n - 1] * C[n - 1][n - 2];
            std::complex<double> disc = std::sqrt(std::complex<double>(tr * tr - 4.0 * det, 0.0));
            std::complex<double> l1 = (tr + disc) / 2.0;
            std::complex<double> l2 = (tr - disc) / 2.0;

            double shift = (std::abs(l1.real() - C[n - 1][n - 1]) < std::abs(l2.real() - C[n - 1][n - 1])) ? l1.real() : l2.real();

            std::vector<double> c_rot(n - 1), s_rot(n - 1);
            for (int i = 0; i < n; ++i) C[i][i] -= shift;

            for (int i = 0; i < n - 1; ++i) {
                double x = C[i][i];
                double y = C[i + 1][i];
                double r = std::hypot(x, y);
                if (r < 1e-14) {
                    c_rot[i] = 1.0; s_rot[i] = 0.0;
                } else {
                    c_rot[i] = x / r; s_rot[i] = y / r;
                }

                for (int j = i; j < n; ++j) {
                    double temp = c_rot[i] * C[i][j] + s_rot[i] * C[i + 1][j];
                    C[i + 1][j] = -s_rot[i] * C[i][j] + c_rot[i] * C[i + 1][j];
                    C[i][j] = temp;
                }
            }

            for (int i = 0; i < n - 1; ++i) {
                for (int j = 0; j <= i + 1; ++j) {
                    double temp = c_rot[i] * C[j][i] + s_rot[i] * C[j][i + 1];
                    C[j][i + 1] = -s_rot[i] * C[j][i] + c_rot[i] * C[j][i + 1];
                    C[j][i] = temp;
                }
            }

            for (int i = 0; i < n; ++i) C[i][i] += shift;
            iter++;
        }

        if (std::abs(C[n - 1][n - 2]) <= 1e-11) {
            roots.push_back(std::complex<double>(C[n - 1][n - 1], 0.0));
            n--;
        } else {
            double a11 = C[n - 2][n - 2], a12 = C[n - 2][n - 1];
            double a21 = C[n - 1][n - 2], a22 = C[n - 1][n - 1];
            double tr = a11 + a22;
            double det = a11 * a22 - a12 * a21;
            std::complex<double> disc = std::sqrt(std::complex<double>(tr * tr - 4.0 * det, 0.0));
            roots.push_back((tr + disc) / 2.0);
            roots.push_back((tr - disc) / 2.0);
            n -= 2;
        }
    }

    if (n == 1) {
        roots.push_back(std::complex<double>(C[0][0], 0.0));
    } else if (n == 2) {
        double tr = C[0][0] + C[1][1];
        double det = C[0][0] * C[1][1] - C[0][1] * C[1][0];
        std::complex<double> disc = std::sqrt(std::complex<double>(tr * tr - 4.0 * det, 0.0));
        roots.push_back((tr + disc) / 2.0);
        roots.push_back((tr - disc) / 2.0);
    }

    for (size_t i = 0; i < roots.size(); ++i) {
        if (std::abs(roots[i].imag()) < 1e-5) {
            roots[i] = std::complex<double>(roots[i].real(), 0.0);
        }
    }

    return roots;
}

std::vector<std::complex<double>> AlphaSolverEq::solvePolynomial(const std::vector<double>& coeffs) {
    int degree = static_cast<int>(coeffs.size()) - 1;
    if (degree == 2) return solveQuadratic(coeffs[0], coeffs[1], coeffs[2]);
    if (degree == 3) return solveCubic(coeffs[0], coeffs[1], coeffs[2], coeffs[3]);
    return solveHighDegreePoly(coeffs);
}

// ---------- Polynomial Factorization ----------

std::string AlphaSolverEq::factorizePolynomial(const std::vector<double>& coeffs, char var) {
    if (!isAsciiLetter(var)) var = 'x';
    size_t start = 0;
    while (start < coeffs.size() && std::abs(coeffs[start]) < 1e-9) {
        start++;
    }

    if (start >= coeffs.size()) return "0";

    double leadingCoeff = coeffs[start];
    std::vector<double> validCoeffs(coeffs.begin() + start, coeffs.end());

    std::vector<std::complex<double>> roots = solvePolynomial(validCoeffs);

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(4);

    if (std::abs(leadingCoeff - 1.0) > 1e-9 && std::abs(leadingCoeff + 1.0) > 1e-9) {
        ss << leadingCoeff;
    } else if (std::abs(leadingCoeff + 1.0) <= 1e-9) {
        ss << "-";
    }

    if (roots.empty()) {
        ss << leadingCoeff;
        return ss.str();
    }

    // --- แยกรากออกเป็น "รากจริง" กับ "คู่รากเชิงซ้อนสังยุค (conjugate pair)" ---
    // เพราะสัมประสิทธิ์ของพหุนามเป็นจำนวนจริง รากเชิงซ้อนจะมาเป็นคู่สังยุคเสมอ
    // (a + bi) และ (a - bi) เราจึงรวมคู่นี้เป็นตัวประกอบกำลังสองแบบจำนวนจริง:
    //   (x - (a+bi))(x - (a-bi)) = x^2 - 2a x + (a^2 + b^2)
    // ทำให้อ่านง่ายกว่าการแสดง i แยกสองตัวประกอบ และไม่มีปัญหาเรื่องเครื่องหมายสับสน
    const double IMAG_TOL = 1e-5;
    const double REAL_TOL = 1e-5;
    const double GROUP_TOL = 1e-4;

    std::vector<double> realRoots;
    std::vector<std::pair<double, double>> complexPairs; // (realPart, imagPart > 0)

    for (const auto& r : roots) {
        double re = (std::abs(r.real()) < REAL_TOL) ? 0.0 : r.real();
        double im = (std::abs(r.imag()) < IMAG_TOL) ? 0.0 : r.imag();
        if (im == 0.0) {
            realRoots.push_back(re);
        } else if (im > 0.0) {
            complexPairs.push_back({re, im}); // เก็บเฉพาะตัวที่ imag > 0 (ตัวคู่สังยุคไม่ต้องเก็บซ้ำ)
        }
    }

    std::sort(realRoots.begin(), realRoots.end());
    std::sort(complexPairs.begin(), complexPairs.end());

    // --- รวมรากจริงที่ซ้ำกัน (multiplicity) เพื่อพิมพ์ในรูป (x - r)^n ---
    std::vector<std::pair<double, int>> groupedReal;
    for (double r : realRoots) {
        if (!groupedReal.empty() && std::abs(groupedReal.back().first - r) < GROUP_TOL) {
            groupedReal.back().second++;
        } else {
            groupedReal.push_back({r, 1});
        }
    }

    // --- รวมคู่รากเชิงซ้อนที่ซ้ำกัน เพื่อพิมพ์ในรูป (x^2 + bx + c)^n ---
    std::vector<std::pair<std::pair<double, double>, int>> groupedComplex;
    for (const auto& p : complexPairs) {
        if (!groupedComplex.empty() &&
            std::abs(groupedComplex.back().first.first - p.first) < GROUP_TOL &&
            std::abs(groupedComplex.back().first.second - p.second) < GROUP_TOL) {
            groupedComplex.back().second++;
        } else {
            groupedComplex.push_back({p, 1});
        }
    }

    // --- พิมพ์ตัวประกอบรากจริง: (x - r) หรือ (x + |r|) ---
    for (const auto& gr : groupedReal) {
        double r = gr.first;
        int cnt = gr.second;
        ss << "(" << var;
        if (r > REAL_TOL) ss << " - " << r;
        else if (r < -REAL_TOL) ss << " + " << -r;
        // ถ้า r == 0 จะได้แค่ "(x)"
        ss << ")";
        if (cnt > 1) ss << "^" << cnt;
    }

    // --- พิมพ์ตัวประกอบกำลังสองจากคู่รากเชิงซ้อน: (x^2 + bx + c) ---
    for (const auto& gc : groupedComplex) {
        double re = gc.first.first;
        double im = gc.first.second;
        int cnt = gc.second;

        double b = -2.0 * re;            // สัมประสิทธิ์ของ x
        double c = re * re + im * im;    // ค่าคงที่ (>= 0 เสมอ)

        ss << "(" << var << "^2";
        if (b > REAL_TOL) ss << " + " << b << var;
        else if (b < -REAL_TOL) ss << " - " << -b << var;
        if (c > REAL_TOL) ss << " + " << c;
        else if (c < -REAL_TOL) ss << " - " << -c;
        ss << ")";
        if (cnt > 1) ss << "^" << cnt;
    }

    return ss.str();
}

// ---------- General Non-linear Equation Solver ----------

double AlphaSolverEq::solveGeneralEquation(Calculator& calc, const std::string& expr, double initialGuess, int maxIter, double tol, char var) {
    const std::string expr_n = normalizeExpression(substituteAns(expr, lastAnswer), var);
    double x = initialGuess;
    for (int i = 0; i < maxIter; ++i) {
        calc.setVariable('x', x);
        double f = calc.evaluate(expr_n);
        double df = calc.derivative(expr_n, x);

        if (std::abs(df) < 1e-12) {
            throw std::runtime_error("Derivative too close to zero during iteration.");
        }

        double nextX = x - f / df;
        if (std::abs(nextX - x) < tol) {
            return nextX;
        }
        x = nextX;
    }
    return x;
}

// 1. หาค่า x ที่ทำให้ f'(x) = targetValue (เช่น หาจุดวิกฤตเมื่อ f'(x) = 0)
double AlphaSolverEq::solveDerivativeEq(Calculator& calc, const std::string& expr, double targetValue, double initialGuess, int maxIter, double tol, char var) {
    const std::string expr_n = normalizeExpression(substituteAns(expr, lastAnswer), var);
    double x = initialGuess;
    double h = 1e-5;

    for (int i = 0; i < maxIter; ++i) {
        // g(x) = f'(x) - targetValue
        double f_prime = calc.derivative(expr_n, x);
        double g = f_prime - targetValue;

        if (std::abs(g) < tol) return x;

        // หาอนุพันธ์อันดับสอง f''(x) เพื่อทำ Newton-Raphson
        double f_prime_plus = calc.derivative(expr_n, x + h);
        double f_prime_minus = calc.derivative(expr_n, x - h);
        double g_prime = (f_prime_plus - f_prime_minus) / (2.0 * h); // f''(x)

        if (std::abs(g_prime) < 1e-12) {
            throw std::runtime_error("Second derivative too close to zero during calculus solver iteration.");
        }

        double nextX = x - g / g_prime;
        if (std::abs(nextX - x) < tol) return nextX;
        x = nextX;
    }
    return x;
}

// 2. หาค่าขอบเขตบน b ที่ทำให้ Integral_a^b f(x) dx = targetValue
double AlphaSolverEq::solveIntegralBound(Calculator& calc, const std::string& expr, double lowerBoundA, double targetValue, double initialGuess, int maxIter, double tol, char var) {
    const std::string expr_n = normalizeExpression(substituteAns(expr, lastAnswer), var);
    double b = initialGuess;

    for (int i = 0; i < maxIter; ++i) {
        // g(b) = Integral[a, b] f(x) dx - targetValue
        double currentIntegral = calc.integral(expr_n, lowerBoundA, b);
        double g = currentIntegral - targetValue;

        if (std::abs(g) < tol) return b;

        // จาก ทฤษฎีบทหลักของแคลคูลัส (Fundamental Theorem of Calculus): d/db [Integral_a^b f(x) dx] = f(b)
        calc.setVariable('x', b);
        double g_prime = calc.evaluate(expr_n); // f(b)

        if (std::abs(g_prime) < 1e-12) {
            throw std::runtime_error("Function value f(b) too close to zero during integral bound solving.");
        }

        double nextB = b - g / g_prime;
        if (std::abs(nextB - b) < tol) return nextB;
        b = nextB;
    }
    return b;
}

// 3. หาค่า x0 ที่ทำให้ lim(x->x0) f(x) = targetValue
double AlphaSolverEq::solveLimitEq(Calculator& calc, const std::string& expr, double targetValue, double initialGuess, int maxIter, double tol, char var) {
    const std::string expr_n = normalizeExpression(substituteAns(expr, lastAnswer), var);
    double x = initialGuess;
    double h = 1e-5;

    for (int i = 0; i < maxIter; ++i) {
        // g(x) = lim_{t->x} f(t) - targetValue
        double limVal = calc.limit(expr_n, x);
        double g = limVal - targetValue;

        if (std::abs(g) < tol) return x;

        // คำนวณอนุพันธ์ของลิมิตเชิงตัวเลข
        double g_plus = calc.limit(expr_n, x + h) - targetValue;
        double g_minus = calc.limit(expr_n, x - h) - targetValue;
        double g_prime = (g_plus - g_minus) / (2.0 * h);

        if (std::abs(g_prime) < 1e-12) {
            throw std::runtime_error("Derivative of limit function too close to zero.");
        }

        double nextX = x - g / g_prime;
        if (std::abs(nextX - x) < tol) return nextX;
        x = nextX;
    }
    return x;
}

// 4. แก้สมการอินทิกรัลไม่จำกัดเขต: หาค่า x ที่ทำให้ F(x) = Int_{x_ref}^{x} f(t) dt = targetValue
double AlphaSolverEq::solveIndefiniteIntegralEq(Calculator& calc, const std::string& expr, double targetValue, double x_ref, double initialGuess, int maxIter, double tol, char var) {
    const std::string expr_n = normalizeExpression(substituteAns(expr, lastAnswer), var);
    double x = initialGuess;

    for (int i = 0; i < maxIter; ++i) {
        // คำนวณค่า F(x) = Integral[x_ref to x] f(t) dt
        double currentIntegral = calc.indefiniteIntegral(expr_n, x, x_ref);
        double g = currentIntegral - targetValue;

        if (std::abs(g) < tol) return x;

        // จาก Fundamental Theorem of Calculus: d/dx [F(x)] = f(x)
        calc.setVariable('x', x);
        double g_prime = calc.evaluate(expr_n); // f(x)

        if (std::abs(g_prime) < 1e-12) {
            throw std::runtime_error("Function value f(x) too close to zero during indefinite integral solving.");
        }

        double nextX = x - g / g_prime;
        if (std::abs(nextX - x) < tol) return nextX;
        x = nextX;
    }
    return x;
}

double AlphaSolverEq::solveTwoSideEquation(Calculator& calc, const std::string& fullEq, double initialGuess, int maxIter, double tol, char var) {
    // แทนคำว่า "ans" ด้วยค่าตัวเลขก่อน แล้วย้ายข้างมาเป็น (lhs) - (rhs) = 0 แล้วแก้แบบปกติ
    std::string combinedExpr = toZeroForm(substituteAns(fullEq, lastAnswer));
    return solveGeneralEquation(calc, combinedExpr, initialGuess, maxIter, tol, var);
}

// ---------- System of Equations Solvers (2+ ตัวแปร) ----------

// 4.1 แก้ระบบสมการเชิงเส้น เช่น {"2x + 3y = 8", "x - y = 1"}
// เทคนิค: เนื่องจากสมการเป็นเชิงเส้น (affine) การแทนค่าตัวแปรทุกตัว = 0 จะได้ค่าคงที่ c0
// และการแทนค่าตัวแปร v = 1 (ตัวอื่น = 0) จะได้ coeff(v) = f(e_v) - c0 พอดี (ไม่ต้อง parse สัมประสิทธิ์เอง)
// จากนั้นสร้างระบบ A*x = -c0 แล้วแก้ด้วย Matrix::solveLinear
//
// [สำคัญ] เทคนิค 2 จุด (f(0), f(1)) นี้ให้ "สัมประสิทธิ์" ที่ถูกต้องก็ต่อเมื่อสมการเป็นเชิงเส้นจริง ๆ
// ถ้าใส่สมการไม่เชิงเส้นเข้ามา (เช่น x^2, y^3, sin(x), x*y) ฟังก์ชันนี้จะไม่ throw แต่จะคำนวณ "สัมประสิทธิ์ปลอม"
// ออกมาเงียบ ๆ แล้วได้คำตอบที่ผิดโดยไม่มีสัญญาณเตือน จึงต้องตรวจสอบความเป็นเชิงเส้นจริงก่อนสร้างระบบสมการเสมอ:
//  - Scale check: f(2*e_v) - c0 ต้องเท่ากับ 2*coeff(v) พอดี (ตรวจจับ x^2, x^3, sqrt(x), sin(x) ฯลฯ)
//  - Additivity check: f(e_v1 + e_v2 + ...) - c0 ต้องเท่ากับผลรวม coeff ทุกตัว (ตรวจจับพจน์ผสม เช่น x*y)
// ถ้าตรวจพบว่าไม่เชิงเส้น จะ throw std::invalid_argument พร้อมบอกให้ไปใช้ solveNonlinearSystem แทน
std::map<char, double> AlphaSolverEq::solveLinearSystem(Calculator& calc, const std::vector<std::string>& equations) {
    std::vector<std::string> exprs;
    std::set<char> varSet;

    for (const auto& eqStr : equations) {
        std::string combined = toZeroForm(substituteAns(eqStr, lastAnswer)); // แทน "ans" ก่อนเสมอ
        exprs.push_back(combined);
        for (char v : findVariables(combined)) varSet.insert(v);
    }

    std::vector<char> vars(varSet.begin(), varSet.end());
    int n = static_cast<int>(vars.size());
    int numEq = static_cast<int>(equations.size());

    if (n == 0) throw std::invalid_argument("No variables found in the given equations.");
    if (numEq == 0) throw std::invalid_argument("At least one equation is required.");

    // A มีขนาด numEq x n (ไม่บังคับว่าต้องเป็นเมทริกซ์จัตุรัส)
    Matrix A(numEq, n);
    Matrix B(numEq, 1);

    const double linTol = 1e-6; // ทนต่อ error เชิงตัวเลขเล็กน้อยจาก floating point เท่านั้น

    for (int i = 0; i < numEq; ++i) {
        std::map<char, double> zeroMap;
        for (char v : vars) zeroMap[v] = 0.0;
        double c0 = calc.evaluate(substituteVariables(exprs[i], zeroMap));

        std::vector<double> coeffs(n);
        std::map<char, double> allOnesMap = zeroMap;

        for (int j = 0; j < n; ++j) {
            std::map<char, double> unitMap = zeroMap;
            unitMap[vars[j]] = 1.0;
            double val1 = calc.evaluate(substituteVariables(exprs[i], unitMap));
            double coeff = val1 - c0;
            coeffs[j] = coeff;
            A.setElement(i, j, coeff); // coefficient of vars[j] in equation i

            // --- Scale check: จับพจน์ไม่เชิงเส้นของตัวแปรเดี่ยว เช่น x^2, x^3, sqrt(x), sin(x) ---
            std::map<char, double> doubleMap = zeroMap;
            doubleMap[vars[j]] = 2.0;
            double val2 = calc.evaluate(substituteVariables(exprs[i], doubleMap));
            double expectedVal2 = c0 + 2.0 * coeff;
            double scale2 = std::max(1.0, std::abs(expectedVal2));
            if (std::abs(val2 - expectedVal2) > linTol * scale2) {
                throw std::invalid_argument(
                    "Equation #" + std::to_string(i + 1) + " (\"" + equations[i] +
                    "\") is not linear in variable '" + std::string(1, vars[j]) +
                    "' (it likely contains a power, root, or function like sin/log applied to it). "
                    "solveLinearSystem only accepts equations that are truly linear (a*x + b*y + ... = c); "
                    "use the Nonlinear System Solver (solveNonlinearSystem) instead.");
            }

            allOnesMap[vars[j]] = 1.0;
        }

        // --- Additivity check: จับพจน์ผสมระหว่างตัวแปร (Cross Term) เช่น x*y, x/y ---
        if (n >= 2) {
            double valAllOnes = calc.evaluate(substituteVariables(exprs[i], allOnesMap));
            double sumCoeffs = 0.0;
            for (double c : coeffs) sumCoeffs += c;
            double expectedAllOnes = c0 + sumCoeffs;
            double scaleAll = std::max(1.0, std::abs(expectedAllOnes));
            if (std::abs(valAllOnes - expectedAllOnes) > linTol * scaleAll) {
                throw std::invalid_argument(
                    "Equation #" + std::to_string(i + 1) + " (\"" + equations[i] +
                    "\") is not linear: it mixes two or more variables together in one term (e.g. x*y, x/y). "
                    "solveLinearSystem only accepts equations that are truly linear (a*x + b*y + ... = c); "
                    "use the Nonlinear System Solver (solveNonlinearSystem) instead.");
            }
        }

        B.setElement(i, 0, -c0);
    }

    // เลือกวิธีแก้ให้อัตโนมัติตามจำนวนสมการเทียบจำนวนตัวแปร (พอดี / เกิน=Least Squares / ขาด=Minimum-Norm)
    Matrix X = Matrix::solveLinearGeneral(A, B); // throws std::runtime_error if no (unique) solution exists

    std::map<char, double> result;
    for (int j = 0; j < n; ++j) result[vars[j]] = X.getElement(j, 0);
    return result;
}

// 4.2 แก้ระบบสมการทั่วไป/ไม่เชิงเส้น ด้วย Multivariable Newton-Raphson
// F(x) = 0 โดย F เป็นเวกเตอร์ของ (lhs_i - rhs_i), x เป็นเวกเตอร์ของตัวแปรทั้งหมดที่พบ
// อัปเดตแต่ละรอบด้วย x_new = x - J^-1 * F(x) โดย J หาแบบตัวเลข (central difference)
std::map<char, double> AlphaSolverEq::solveNonlinearSystem(Calculator& calc, const std::vector<std::string>& equations,
                                                             const std::map<char, double>& initialGuess,
                                                             int maxIter, double tol) {
    std::vector<std::string> exprs;
    std::set<char> varSet;

    for (const auto& eqStr : equations) {
        std::string combined = toZeroForm(substituteAns(eqStr, lastAnswer)); // แทน "ans" ก่อนเสมอ
        exprs.push_back(combined);
        for (char v : findVariables(combined)) varSet.insert(v);
    }

    std::vector<char> vars(varSet.begin(), varSet.end());
    int n = static_cast<int>(vars.size());
    int numEq = static_cast<int>(equations.size());

    if (n == 0) throw std::invalid_argument("No variables found in the given equations.");
    if (numEq == 0) throw std::invalid_argument("At least one equation is required.");

    std::map<char, double> x;
    for (char v : vars) {
        auto it = initialGuess.find(v);
        x[v] = (it != initialGuess.end()) ? it->second : 1.0;
    }

    const double h = 1e-6;

    for (int iter = 0; iter < maxIter; ++iter) {
        std::vector<double> F(numEq);
        for (int i = 0; i < numEq; ++i) {
            F[i] = calc.evaluate(substituteVariables(exprs[i], x));
        }

        double residualNorm = 0.0;
        for (double f : F) residualNorm += f * f;
        if (std::sqrt(residualNorm) < tol) return x;

        // Jacobian เชิงตัวเลขด้วย Central Difference (ขนาด numEq x n ไม่จำเป็นต้องเป็นจัตุรัส)
        Matrix J(numEq, n);
        for (int j = 0; j < n; ++j) {
            char vj = vars[j];
            std::map<char, double> xPlus = x, xMinus = x;
            xPlus[vj] += h;
            xMinus[vj] -= h;
            for (int i = 0; i < numEq; ++i) {
                double fPlus = calc.evaluate(substituteVariables(exprs[i], xPlus));
                double fMinus = calc.evaluate(substituteVariables(exprs[i], xMinus));
                J.setElement(i, j, (fPlus - fMinus) / (2.0 * h));
            }
        }

        Matrix B(numEq, 1);
        for (int i = 0; i < numEq; ++i) B.setElement(i, 0, -F[i]);

        // พอดี -> Newton-Raphson ปกติ, เกิน -> Gauss-Newton (Least Squares), ขาด -> Minimum-Norm step
        Matrix delta;
        try {
            delta = Matrix::solveLinearGeneral(J, B);
        } catch (const std::exception&) {
            throw std::runtime_error("Jacobian matrix is singular / rank deficient during nonlinear system solving (try a different initial guess).");
        }

        double stepNorm = 0.0;
        for (int j = 0; j < n; ++j) {
            double d = delta.getElement(j, 0);
            x[vars[j]] += d;
            stepNorm += d * d;
        }
        if (std::sqrt(stepNorm) < tol) return x;
    }

    return x; // คืนค่าประมาณล่าสุดถ้าครบจำนวนรอบสูงสุดแล้วยังไม่เข้าเกณฑ์ tol
}